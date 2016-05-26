/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Nemo
 *
 * Copyright (C) 2000, 2001 Eazel, Inc.
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: John Sullivan <sullivan@eazel.com>
 */

/* nemo-window-menus.h - implementation of nemo window menu operations,
 *                           split into separate file just for convenience.
 */
#include <config.h>

#include <locale.h>

#include "nemo-window-menus.h"
#include "nemo-actions.h"
#include "nemo-application.h"
#include "nemo-connect-server-dialog.h"
#include "nemo-file-management-properties.h"
#include "nemo-navigation-action.h"
#include "nemo-notebook.h"
#include "nemo-window-private.h"
#include "nemo-desktop-window.h"
#include "nemo-location-entry.h"
#include "nemo-canvas-view.h"
#include "nemo-list-view.h"
#include "nemo-toolbar.h"
#include "nemo-plugin-manager.h"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

#include <eel/eel-gtk-extensions.h>
#include <eel/eel-stock-dialogs.h>

#include <libnemo-extension/nemo-menu-provider.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-names.h>
#include <libnemo-private/nemo-ui-utilities.h>
#include <libnemo-private/nemo-module.h>
#include <libnemo-private/nemo-program-choosing.h>
#include <libnemo-private/nemo-search-directory.h>
#include <libnemo-private/nemo-search-engine.h>
#include <libnemo-private/nemo-signaller.h>
#include <libnemo-private/nemo-trash-monitor.h>
#include <string.h>

#define MENU_PATH_EXTENSION_ACTIONS                     "/MenuBar/File/Extension Actions"
#define POPUP_PATH_EXTENSION_ACTIONS                     "/background/Before Zoom Items/Extension Actions"

#define NETWORK_URI          "network:"
#define COMPUTER_URI         "computer:"

static void
action_close_window_slot (GSimpleAction *action,
                          GVariant *state,
                          gpointer user_data)
{
	NemoWindow *window;
	NemoWindowPane *pane;
	NemoWindowSlot *slot;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);
	pane = nemo_window_slot_get_pane (slot);

	nemo_window_pane_close_slot (pane, slot);
}

static void
action_connect_to_server (GSimpleAction *action,
                          GVariant *state,
                          gpointer user_data)
{
	NemoWindow *window = NEMO_WINDOW (user_data);
    NemoApplication *app = NEMO_APPLICATION (g_application_get_default ());
    nemo_application_connect_server (app, window);
}

static void
action_stop (GSimpleAction *action,
             GVariant *state,
             gpointer user_data)
{
	NemoWindow *window;
	NemoWindowSlot *slot;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);

	nemo_window_slot_stop_loading (slot);
}

#ifdef TEXT_CHANGE_UNDO
static void
action_undo_callback (GtkAction *action, 
		      gpointer user_data) 
{
	NemoApplication *app;

	app = NEMO_APPLICATION (g_application_get_default ());
	nemo_undo_manager_undo (app->undo_manager);
}
#endif

static void
action_go_home (GSimpleAction *action,
             GVariant *state,
             gpointer user_data)
{
	NemoWindow *window;
	NemoWindowSlot *slot;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);

	nemo_window_slot_go_home (slot,
				      nemo_event_get_window_open_flags ());
}

static void
action_go_to_computer (GSimpleAction *action,
                       GVariant *state,
                       gpointer user_data)
{
	NemoWindow *window;
	NemoWindowSlot *slot;
	GFile *computer;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);

	computer = g_file_new_for_uri (COMPUTER_URI);
	nemo_window_slot_open_location (slot, computer,
					    nemo_event_get_window_open_flags ());
	g_object_unref (computer);
}

static void
action_go_to_network (GSimpleAction *action,
        			  GVariant *state,
                      gpointer user_data)
{
	NemoWindow *window;
	NemoWindowSlot *slot;
	GFile *network;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);

	network = g_file_new_for_uri (NETWORK_URI);
	nemo_window_slot_open_location (slot, network,
					    nemo_event_get_window_open_flags ());
	g_object_unref (network);
}

static void
action_go_to_templates (GSimpleAction *action,
                        GVariant *state,
                        gpointer user_data)
{
	NemoWindow *window;
	NemoWindowSlot *slot;
	char *path;
	GFile *location;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);

	path = nemo_get_templates_directory ();
	location = g_file_new_for_path (path);
	g_free (path);
	nemo_window_slot_open_location (slot, location,
					    nemo_event_get_window_open_flags ());
	g_object_unref (location);
}

static void
action_go_to_trash (GSimpleAction *action,
                    GVariant *state,
                    gpointer user_data)
{
	NemoWindow *window;
	NemoWindowSlot *slot;
	GFile *trash;

	window = NEMO_WINDOW (user_data);
	slot = nemo_window_get_active_slot (window);

	trash = g_file_new_for_uri ("trash:///");
	nemo_window_slot_open_location (slot, trash,
					    nemo_event_get_window_open_flags ());
	g_object_unref (trash);
}

static void
action_reload (GSimpleAction *action,
               GVariant *state,
               gpointer user_data)
{
	NemoWindowSlot *slot;

	slot = nemo_window_get_active_slot (NEMO_WINDOW (user_data));
	nemo_window_slot_queue_reload (slot);
}

static NemoView *
get_current_view (NemoWindow *window)
{
	NemoWindowSlot *slot;
	NemoView *view;

	slot = nemo_window_get_active_slot (window);
	view = nemo_window_slot_get_current_view (slot);

	return view;
}

static void
action_zoom_in (GSimpleAction *action,
                GVariant *state,
                gpointer user_data)
{
	nemo_view_bump_zoom_level (get_current_view (user_data), 1);
}

static void
action_zoom_out (GSimpleAction *action,
                 GVariant *state,
                 gpointer user_data)
{
	nemo_view_bump_zoom_level (get_current_view (user_data), -1);
}

static void
action_zoom_normal (GSimpleAction *action,
                    GVariant *state,
                    gpointer user_data)
{
	nemo_view_restore_default_zoom_level (get_current_view (user_data));
}

static void
action_preferences (GSimpleAction *action,
                    GVariant *state,
                    gpointer user_data)
{
	GtkWindow *window;

	window = GTK_WINDOW (user_data);

	nemo_file_management_properties_dialog_show (window);
}

static void
action_plugins (GSimpleAction *action,
                GVariant *state,
                gpointer user_data)
{
    nemo_plugin_manager_show ();
}

static void
action_about_nemo (GSimpleAction *action,
                   GVariant *state,
                   gpointer user_data)
{	
	const gchar *license[] = {
		N_("Nemo is free software; you can redistribute it and/or modify "
		   "it under the terms of the GNU General Public License as published by "
		   "the Free Software Foundation; either version 2 of the License, or "
		   "(at your option) any later version."),
		N_("Nemo is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License "
		   "along with Nemo; if not, write to the Free Software Foundation, Inc., "
		   "51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA")
	};
	gchar *license_trans;
	GDateTime *date;

	license_trans = g_strjoin ("\n\n", _(license[0]), _(license[1]),
					     _(license[2]), NULL);

	date = g_date_time_new_now_local ();	

	gtk_show_about_dialog (GTK_WINDOW (user_data),
			       "program-name", _("Nemo"),
			       "version", VERSION,
			       "comments", _("Nemo lets you organize "
					     "files and folders, both on "
					     "your computer and online."),			       
			       "license", license_trans,
			       "wrap-license", TRUE,			       				
			      "logo-icon-name", "folder",			      
			      NULL);

	g_free (license_trans);
	g_date_time_unref (date);
}

static void
action_up (GSimpleAction *action,
           GVariant *state,
           gpointer user_data)
{
	NemoWindow *window = user_data;
	NemoWindowSlot *slot;

	slot = nemo_window_get_active_slot (window);
	nemo_window_slot_go_up (slot, nemo_event_get_window_open_flags ());
}

static void
action_nemo_manual (GSimpleAction *action,
                    GVariant *state,
                    gpointer user_data)
{
	NemoWindow *window;
	GError *error;
	GtkWidget *dialog;
	char *helpuri;
	char *helpprefix;

	error = NULL;
	window = NEMO_WINDOW (user_data);
	
	if (!g_strcmp0(g_getenv("XDG_CURRENT_DESKTOP"), "Unity"))
	{
		helpprefix = "ubuntu-help";
	} else {
		helpprefix = "gnome-help";
	}

	helpuri = g_strconcat ("help:", helpprefix, "/files", NULL);

	if (NEMO_IS_DESKTOP_WINDOW (window)) {
		nemo_launch_application_from_command (gtk_window_get_screen (GTK_WINDOW (window)), helpprefix, FALSE, NULL);
	} else {
		gtk_show_uri (gtk_window_get_screen (GTK_WINDOW (window)),
			      helpuri,
			      gtk_get_current_event_time (), &error);
	}
	
	g_free(helpuri);

	if (error) {
		dialog = gtk_message_dialog_new (GTK_WINDOW (window),
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("There was an error displaying help: \n%s"),
						 error->message);
		g_signal_connect (G_OBJECT (dialog), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);

		gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
		gtk_widget_show (dialog);
		g_error_free (error);
	}
}

static void
menu_item_select_cb (GtkMenuItem *proxy,
		     NemoWindow *window)
{
	GtkAction *action;
	char *message;

	action = gtk_activatable_get_related_action (GTK_ACTIVATABLE (proxy));
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message) {
		gtk_statusbar_push (GTK_STATUSBAR (window->details->statusbar),
				    window->details->help_message_cid, message);
		g_free (message);
	}
}

static void
menu_item_deselect_cb (GtkMenuItem *proxy,
		       NemoWindow *window)
{
	gtk_statusbar_pop (GTK_STATUSBAR (window->details->statusbar),
			   window->details->help_message_cid);
}

static void
disconnect_proxy_cb (GtkUIManager *manager,
		     GtkAction *action,
		     GtkWidget *proxy,
		     NemoWindow *window)
{
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_select_cb), window);
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_deselect_cb), window);
	}
}

static void
trash_state_changed_cb (NemoTrashMonitor *monitor,
			gboolean state,
			NemoWindow *window)
{
	GtkActionGroup *action_group;
	GtkAction *action;
	GIcon *gicon;

	action_group = nemo_window_get_main_action_group (window);
	action = gtk_action_group_get_action (action_group, "Go to Trash");

	gicon = nemo_trash_monitor_get_icon ();

	if (gicon) {
		g_object_set (action, "gicon", gicon, NULL);
		g_object_unref (gicon);
	}
}

static void
nemo_window_initialize_trash_icon_monitor (NemoWindow *window)
{
	NemoTrashMonitor *monitor;

	monitor = nemo_trash_monitor_get ();

	trash_state_changed_cb (monitor, TRUE, window);

	g_signal_connect (monitor, "trash_state_changed",
			  G_CALLBACK (trash_state_changed_cb), window);
}

#define MENU_ITEM_MAX_WIDTH_CHARS 32

enum {
	SIDEBAR_PLACES,
	SIDEBAR_TREE
};

static void
action_close_all_windows (GSimpleAction *action,
                          GVariant *state,
                          gpointer user_data)
{
	nemo_application_close_all_windows (NEMO_APPLICATION (g_application_get_default ()));
}

static void
action_back (GSimpleAction *action,
             GVariant *state,
             gpointer user_data)
{
	nemo_window_back_or_forward (NEMO_WINDOW (user_data), 
					 TRUE, 0, nemo_event_get_window_open_flags ());
}

static void
action_forward (GSimpleAction *action,
                GVariant *state,
                gpointer user_data)
{
	nemo_window_back_or_forward (NEMO_WINDOW (user_data), 
					 FALSE, 0, nemo_event_get_window_open_flags ());
}

static void
action_split_view_switch_next_pane (GSimpleAction *action,
        							GVariant *state,
									gpointer user_data)
{
	nemo_window_pane_grab_focus (nemo_window_get_next_pane (NEMO_WINDOW (user_data)));
}

static void
action_split_view_same_location (GSimpleAction *action,
                                 GVariant *state,
                                 gpointer user_data)
{
	NemoWindow *window;
	NemoWindowPane *next_pane;
	GFile *location;

	window = NEMO_WINDOW (user_data);
	next_pane = nemo_window_get_next_pane (window);

	if (!next_pane) {
		return;
	}
	location = nemo_window_slot_get_location (next_pane->active_slot);
	if (location) {
		nemo_window_slot_open_location (nemo_window_get_active_slot (window),
						    location, 0);
	}
}

static void
action_show_sidebar (GSimpleAction *action,
                     GVariant *state,
                     gpointer user_data)
{
	NemoWindow *window;

	window = NEMO_WINDOW (user_data);

	if (g_variant_get_boolean (state)) {
		nemo_window_show_sidebar (window);
	} else {
		nemo_window_hide_sidebar (window);
	}
	g_simple_action_set_state (action, state);
}

static void
action_split_view (GSimpleAction *action,
                   GVariant *state,
                   gpointer user_data)
{
	NemoWindow *window;
	gboolean is_active;

	window = NEMO_WINDOW (user_data);

	is_active = g_variant_get_boolean (state);
	if (is_active != nemo_window_split_view_showing (window)) {
		NemoWindowSlot *slot;

		if (is_active) {
			nemo_window_split_view_on (window);
		} else {
			nemo_window_split_view_off (window);
		}

		slot = nemo_window_get_active_slot (window);
		if (slot != NULL) {
			nemo_view_update_menus (nemo_window_slot_get_view(slot));
		}
	}
	g_simple_action_set_state (action, state);
}

static void
nemo_window_update_split_view_actions_sensitivity (NemoWindow *window)
{
	GtkActionGroup *action_group;
	GtkAction *action;
	gboolean have_multiple_panes;
	gboolean next_pane_is_in_same_location;
	GFile *active_pane_location;
	GFile *next_pane_location;
	NemoWindowPane *next_pane;
	NemoWindowSlot *active_slot;

	active_slot = nemo_window_get_active_slot (window);
	action_group = nemo_window_get_main_action_group (window);

	/* collect information */
	have_multiple_panes = nemo_window_split_view_showing (window);
	if (active_slot != NULL) {
		active_pane_location = nemo_window_slot_get_location (active_slot);
	} else {
		active_pane_location = NULL;
	}

	next_pane = nemo_window_get_next_pane (window);
	if (next_pane && next_pane->active_slot) {
		next_pane_location = nemo_window_slot_get_location (next_pane->active_slot);
		next_pane_is_in_same_location = (active_pane_location && next_pane_location &&
						 g_file_equal (active_pane_location, next_pane_location));
	} else {
		next_pane_location = NULL;
		next_pane_is_in_same_location = FALSE;
	}

	/* switch to next pane */
	action = gtk_action_group_get_action (action_group, "SplitViewNextPane");
	gtk_action_set_sensitive (action, have_multiple_panes);

	/* same location */
	action = gtk_action_group_get_action (action_group, "SplitViewSameLocation");
	gtk_action_set_sensitive (action, have_multiple_panes && !next_pane_is_in_same_location);
}

static void
sidebar_radio_entry_changed_cb (GtkAction *action,
                GtkRadioAction *current,
                gpointer user_data)
{
    gint current_value;
    NemoWindow *window = NEMO_WINDOW (user_data);

    current_value = gtk_radio_action_get_current_value (current);

    if (current_value == SIDEBAR_PLACES) {
        nemo_window_set_sidebar_id (window, NEMO_WINDOW_SIDEBAR_PLACES);
    } else if (current_value == SIDEBAR_TREE) {
        nemo_window_set_sidebar_id (window, NEMO_WINDOW_SIDEBAR_TREE);
    }
}

/* TODO: bind all of this with g_settings_bind and GBinding */
static guint
sidebar_id_to_value (const gchar *sidebar_id)
{
	guint retval = SIDEBAR_PLACES;

	if (g_strcmp0 (sidebar_id, NEMO_WINDOW_SIDEBAR_TREE) == 0)
		retval = SIDEBAR_TREE;

	return retval;
}

static void
update_side_bar_radio_buttons (NemoWindow *window)
{
    GtkActionGroup *action_group;
    GtkAction *action;
    guint current_value;

    action_group = nemo_window_get_main_action_group (window);

    action = gtk_action_group_get_action (action_group,
                          "Sidebar Places");
    current_value = sidebar_id_to_value (nemo_window_get_sidebar_id (window));

    g_signal_handlers_block_by_func (action, sidebar_radio_entry_changed_cb, window);
    gtk_radio_action_set_current_value (GTK_RADIO_ACTION (action), current_value);
    g_signal_handlers_unblock_by_func (action, sidebar_radio_entry_changed_cb, window);
}

void
nemo_window_update_show_hide_menu_items (NemoWindow *window) 
{
	g_action_group_change_action_state (G_ACTION_GROUP (window),
	                                    "show-extra-pane",
	                                    g_variant_new_boolean (nemo_window_split_view_showing (window)));

	nemo_window_update_split_view_actions_sensitivity (window);
    update_side_bar_radio_buttons (window);
}

static void
action_add_bookmark (GSimpleAction *action,
                     GVariant *state,
                     gpointer user_data)
{
	NemoWindow *window = user_data;
	NemoApplication *app = NEMO_APPLICATION (g_application_get_default ());
	NemoWindowSlot *slot;

	slot = nemo_window_get_active_slot (window);
	nemo_bookmark_list_append (nemo_application_get_bookmarks (app),
				       nemo_window_slot_get_bookmark (slot));
}

static void
action_edit_bookmarks (GSimpleAction *action,
                       GVariant *state,
                       gpointer user_data)
{
	NemoApplication *app = NEMO_APPLICATION (g_application_get_default ());
	nemo_application_edit_bookmarks (app, NEMO_WINDOW (user_data));
}

static void
connect_proxy_cb (GtkActionGroup *action_group,
                  GtkAction *action,
                  GtkWidget *proxy,
                  NemoWindow *window)
{
    GtkWidget *label;

	if (!GTK_IS_MENU_ITEM (proxy))
		return;

    label = gtk_bin_get_child (GTK_BIN (proxy));

    if (GTK_IS_LABEL (label)) {
       gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
       gtk_label_set_max_width_chars (GTK_LABEL (label), MENU_ITEM_MAX_WIDTH_CHARS);
    }

	g_signal_connect (proxy, "select",
			  G_CALLBACK (menu_item_select_cb), window);
	g_signal_connect (proxy, "deselect",
			  G_CALLBACK (menu_item_deselect_cb), window);
}

static const char* icon_entries[] = {
	"/MenuBar/Other Menus/Go/Home",
	"/MenuBar/Other Menus/Go/Computer",
	"/MenuBar/Other Menus/Go/Go to Templates",
	"/MenuBar/Other Menus/Go/Go to Trash",
	"/MenuBar/Other Menus/Go/Go to Network",
	"/MenuBar/Other Menus/Go/Edit Location"
};

/**
 * refresh_go_menu:
 * 
 * Refresh list of bookmarks at end of Go menu to match centralized history list.
 * @window: The NemoWindow whose Go menu will be refreshed.
 **/
static void
nemo_window_initialize_go_menu (NemoWindow *window)
{
	GtkUIManager *ui_manager;
	GtkWidget *menuitem;
	int i;

	ui_manager = nemo_window_get_ui_manager (NEMO_WINDOW (window));

	for (i = 0; i < G_N_ELEMENTS (icon_entries); i++) {
		menuitem = gtk_ui_manager_get_widget (
				ui_manager,
				icon_entries[i]);
		if (menuitem) {
			gtk_image_menu_item_set_always_show_image (
					GTK_IMAGE_MENU_ITEM (menuitem), TRUE);
		}
	}
}

static void
action_new_window (GSimpleAction *action,
                   GVariant *state,
                   gpointer user_data)
{
	NemoApplication *application;
	NemoWindow *current_window, *new_window;
    gchar *uri;

	current_window = NEMO_WINDOW (user_data);

    uri = nemo_window_slot_get_current_uri (nemo_window_get_active_slot (current_window));
    GFile *loc = g_file_new_for_uri (uri);

	application = NEMO_APPLICATION (g_application_get_default ());

	new_window = nemo_application_create_window (
				application,
				gtk_window_get_screen (GTK_WINDOW (current_window)));

    nemo_window_slot_open_location (nemo_window_get_active_slot (new_window), loc, 0);
    g_object_unref (loc);
    g_free (uri);
}

static void
action_new_tab (GSimpleAction *action,
                GVariant *state,
                gpointer user_data)
{
	NemoWindow *window;

	window = NEMO_WINDOW (user_data);
	nemo_window_new_tab (window);
}

void action_toggle_location_entry_callback (GtkToggleAction *action, gpointer user_data);

static void
toggle_location_entry_setting (NemoWindow     *window,
                               NemoWindowPane *pane,
                               gboolean        from_accel_or_menu)
{
    gboolean current_show, temp_toolbar_visible, default_toolbar_visible, grab_focus_only, already_has_focus;
    GtkToggleAction *button_action;
    GtkActionGroup *action_group;


    current_show = nemo_toolbar_get_show_location_entry (NEMO_TOOLBAR (pane->tool_bar));
    temp_toolbar_visible = pane->temporary_navigation_bar;
    default_toolbar_visible = g_settings_get_boolean (nemo_window_state,
                                                      NEMO_WINDOW_STATE_START_WITH_TOOLBAR);
    already_has_focus = gtk_widget_has_focus (GTK_WIDGET (pane->location_entry));

    grab_focus_only = from_accel_or_menu && (pane->last_focus_widget == NULL || !already_has_focus) && current_show;

    if ((temp_toolbar_visible || default_toolbar_visible) && !grab_focus_only) {
        nemo_toolbar_set_show_location_entry (NEMO_TOOLBAR (pane->tool_bar), !current_show);
        g_settings_set_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_LOCATION_ENTRY, !current_show);

        action_group = pane->toolbar_action_group;
        button_action = GTK_TOGGLE_ACTION (gtk_action_group_get_action (action_group, NEMO_ACTION_TOGGLE_LOCATION));

        g_signal_handlers_block_by_func (button_action, action_toggle_location_entry_callback, window);
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (button_action), !current_show);
        g_signal_handlers_unblock_by_func (button_action, action_toggle_location_entry_callback, window);
    } else {
        nemo_window_pane_ensure_location_entry (pane);
    }
}

void
action_toggle_location_entry_callback (GtkToggleAction *action,
                                        gpointer user_data)
{
    NemoWindow *window = user_data;
    NemoWindowPane *pane;

    pane = nemo_window_get_active_pane (window);
    toggle_location_entry_setting(window, pane, FALSE);
}

void nemo_window_show_location_entry (NemoWindow *window) {
	NemoWindowPane *pane;

    pane = nemo_window_get_active_pane (window);
    toggle_location_entry_setting(window, pane, TRUE);
}

static void
action_edit_location (GSimpleAction *action,
                      GVariant *state,
                      gpointer user_data)
{
	NemoWindow *window = user_data;
	NemoWindowPane *pane;

    pane = nemo_window_get_active_pane (window);
    toggle_location_entry_setting(window, pane, TRUE);
}

enum {
    ICON_VIEW,
    LIST_VIEW,
    COMPACT_VIEW,
    NULL_VIEW
};

static void
action_icon_view_callback (GtkAction *action,
                           gpointer user_data)
{
    NemoWindow *window;
    NemoWindowSlot *slot;
    window = NEMO_WINDOW (user_data);
    slot = nemo_window_get_active_slot (window);
    nemo_window_slot_set_content_view (slot, NEMO_CANVAS_VIEW_ID);
    toolbar_set_view_button (ICON_VIEW, nemo_window_get_active_pane(window));
}


static void
action_list_view_callback (GtkAction *action,
                           gpointer user_data)
{
    NemoWindow *window;
    NemoWindowSlot *slot;
    window = NEMO_WINDOW (user_data);
    slot = nemo_window_get_active_slot (window);
    nemo_window_slot_set_content_view (slot, NEMO_LIST_VIEW_ID);
    toolbar_set_view_button (LIST_VIEW, nemo_window_get_active_pane(window));
}


static void
action_compact_view_callback (GtkAction *action,
                           gpointer user_data)
{
    NemoWindow *window;
    NemoWindowSlot *slot;
    window = NEMO_WINDOW (user_data);
    slot = nemo_window_get_active_slot (window);
    nemo_window_slot_set_content_view (slot, NEMO_COMPACT_VIEW_ID);
    toolbar_set_view_button (COMPACT_VIEW, nemo_window_get_active_pane(window));
}

guint
toolbar_action_for_view_id (const char *view_id)
{
    if (g_strcmp0(view_id, NEMO_CANVAS_VIEW_ID) == 0) {
        return ICON_VIEW;
    } else if (g_strcmp0(view_id, NEMO_LIST_VIEW_ID) == 0) {
        return LIST_VIEW;
    } else if (g_strcmp0(view_id, NEMO_COMPACT_VIEW_ID) == 0) {
        return COMPACT_VIEW;
    } else {
        return NULL_VIEW;
    }
}

void
toolbar_set_view_button (guint action_id, NemoWindowPane *pane)
{
    GtkAction *action, *action1, *action2;
    GtkActionGroup *action_group;
    if (action_id == NULL_VIEW) {
        return;
    }
    action_group = nemo_window_pane_get_toolbar_action_group (pane);


    action = gtk_action_group_get_action(action_group,
                                         NEMO_ACTION_ICON_VIEW);
    action1 = gtk_action_group_get_action(action_group,
                                         NEMO_ACTION_LIST_VIEW);
    action2 = gtk_action_group_get_action(action_group,
                                         NEMO_ACTION_COMPACT_VIEW);

    g_signal_handlers_block_matched (action,
                         G_SIGNAL_MATCH_FUNC,
                         0, 0,
                         NULL,
                         action_icon_view_callback,
                         NULL);

    g_signal_handlers_block_matched (action1,
                         G_SIGNAL_MATCH_FUNC,
                         0, 0,
                         NULL,
                         action_list_view_callback,
                         NULL);
    g_signal_handlers_block_matched (action2,
                         G_SIGNAL_MATCH_FUNC,
                         0, 0,
                         NULL,
                         action_compact_view_callback,
                         NULL);

    if (action_id != ICON_VIEW) {
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);
    } else {
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), TRUE);
    }

    if (action_id != LIST_VIEW) {
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action1), FALSE);
    } else {
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action1), TRUE);
    }

    if (action_id != COMPACT_VIEW) {
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action2), FALSE);
    } else {
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action2), TRUE);
    }

    g_signal_handlers_unblock_matched (action,
                           G_SIGNAL_MATCH_FUNC,
                           0, 0,
                           NULL,
                           action_icon_view_callback,
                           NULL);


    g_signal_handlers_unblock_matched (action1,
                           G_SIGNAL_MATCH_FUNC,
                           0, 0,
                           NULL,
                           action_list_view_callback,
                           NULL);


    g_signal_handlers_unblock_matched (action2,
                           G_SIGNAL_MATCH_FUNC,
                           0, 0,
                           NULL,
                           action_compact_view_callback,
                           NULL);

}

static void
action_tab_previous (GSimpleAction *action,
                      GVariant *state,
                      gpointer user_data)
{
	NemoWindowPane *pane;
	NemoWindow *window = user_data;

	pane = nemo_window_get_active_pane (window);
	nemo_notebook_prev_page (NEMO_NOTEBOOK (pane->notebook));
}

static void
action_tab_next (GSimpleAction *action,
                  GVariant *state,
                  gpointer user_data)
{
	NemoWindowPane *pane;
	NemoWindow *window = user_data;

	pane = nemo_window_get_active_pane (window);
	nemo_notebook_next_page (NEMO_NOTEBOOK (pane->notebook));
}

static void
reorder_tab (NemoWindowPane *pane, int offset)
{
	int page_num;

	g_return_if_fail (pane != NULL);

	page_num = gtk_notebook_get_current_page (
		GTK_NOTEBOOK (pane->notebook));
	g_return_if_fail (page_num != -1);
	nemo_notebook_reorder_child_relative (
		NEMO_NOTEBOOK (pane->notebook), page_num, offset);
}

static void
action_tab_move_left (GSimpleAction *action,
                       GVariant *state,
                       gpointer user_data)
{
	NemoWindow *window = user_data;
	reorder_tab (nemo_window_get_active_pane (window), -1);
}

static void
action_tab_move_right (GSimpleAction *action,
                        GVariant *state,
                        gpointer user_data)
{
	NemoWindow *window = user_data;
	reorder_tab (nemo_window_get_active_pane (window), 1);
}

static void
action_go_to_tab (GSimpleAction *action,
                  GVariant *value,
                  gpointer user_data)
{
	NemoWindowPane *pane;
	NemoWindow *window = NEMO_WINDOW (user_data);
	GtkNotebook *notebook;
	gint16 num;

	pane = nemo_window_get_active_pane (window);
	notebook = GTK_NOTEBOOK (pane->notebook);

 	num = g_variant_get_int32 (value);
	if (num < gtk_notebook_get_n_pages (notebook)) {
		gtk_notebook_set_current_page (notebook, num);
	}
}

static void
action_toggle_search (GSimpleAction *action,
                      GVariant *state,
                      gpointer user_data)
{
	NemoWindowSlot *slot;

	slot = nemo_window_get_active_slot (NEMO_WINDOW (user_data));
	nemo_window_slot_set_search_visible (slot, g_variant_get_boolean (state));

	g_simple_action_set_state (action, state);
}

static void
action_new_folder  (GSimpleAction *action,
                    GVariant *state,
                    gpointer user_data)
{
    g_assert (NEMO_IS_WINDOW (user_data));
    NemoWindow *window = user_data;
    NemoView *view = get_current_view (window);

    nemo_view_new_folder (view, FALSE);
}

static void
open_in_terminal_other (const gchar *path)
{
    gchar *argv[2];
    argv[0] = g_settings_get_string (gnome_terminal_preferences, GNOME_DESKTOP_TERMINAL_EXEC);
    argv[1] = NULL;
    g_spawn_async(path, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

static void
action_open_terminal  (GSimpleAction *action,
                       GVariant *state,
                       gpointer user_data)
{  
    NemoWindow *window;
    NemoView *view;
    
    window = NEMO_WINDOW(user_data);

    view = get_current_view (window);

    gchar *path;
    gchar *uri = nemo_view_get_uri (view);
    GFile *gfile = g_file_new_for_uri (uri);
    path = g_file_get_path (gfile);
    open_in_terminal_other (path);
    g_free (uri);
    g_free (path);
    g_object_unref (gfile);
}

const GActionEntry win_entries[] = {
	/* { name, activate_callback, parameter_type, state, change_state_callback} */
	{ "close-current-view", action_close_window_slot },
	{ "preferences", action_preferences },
	{ "plugins", action_plugins },
	#ifdef TEXT_CHANGE_UNDO
	{ "undo", action_undo },
	#endif
	{ "nemo-manual", action_nemo_manual },
	{ "about-nemo", action_about_nemo },
	{ "zoom-in", action_zoom_in },
   	{ "zoom-out", action_zoom_out },
	{ "zoom-normal", action_zoom_normal },
	{ "connect-to-server", action_connect_to_server },
	{ "go-home", action_go_home },
	{ "go-to-computer", action_go_to_computer },
	{ "go-to-network", action_go_to_network },
	{ "go-to-templates", action_go_to_templates },
	{ "go-to-trash", action_go_to_trash },
	{ "toggle-search", NULL, NULL, "false", action_toggle_search },
	{ "new-window", action_new_window },
	{ "close-all-windows", action_close_all_windows },
	{ "back", action_back },
	{ "forward", action_forward },
	{ "up", action_up },
	{ "reload", action_reload },
	{ "stop", action_stop },
	{ "new-tab", action_new_tab },
	{ "edit-location", action_edit_location },
	{ "split-view-switch-next_pane", action_split_view_switch_next_pane },
	{ "split-view-same-location", action_split_view_same_location },
	{ "add-bookmark", action_add_bookmark },
	{ "edit-bookmarks", action_edit_bookmarks },
	{ "tab-previous", action_tab_previous },
	{ "tab-next", action_tab_next },
	{ "tab-move-left", action_tab_move_left },
	{ "tab_move_right", action_tab_move_right },
 	{ "go-to-tab", NULL, "i", "0", action_go_to_tab },
	{ "open-in-terminal", action_open_terminal },
	{ "new-folder", action_new_folder },
	{ "show-hidden-files", NULL, NULL, "false" },
	{ "show-toolbar", NULL, NULL, "true" },
	{ "show-sidebar", NULL, NULL, "true", action_show_sidebar },
	{ "show-statusbar", NULL, NULL, "true" },
	{ "show-menubar", NULL, NULL, "true" },
	{ "show-extra-pane", NULL, NULL, "false", action_split_view }
};

static const GtkActionEntry main_entries[] = {
  /* name, stock id, label */  { "File", NULL, N_("_File") },
  /* name, stock id, label */  { "Edit", NULL, N_("_Edit") },
  /* name, stock id, label */  { "View", NULL, N_("_View") },
  /* name, stock id, label */  { "Help", NULL, N_("_Help") },
  /* name, stock id */         { NEMO_ACTION_CLOSE, GTK_STOCK_CLOSE,
  /* label, accelerator */       N_("_Close"), "<control>W",
  /* tooltip */                  N_("Close this folder") },
                               { "Preferences", GTK_STOCK_PREFERENCES,
                                 N_("Prefere_nces"),               
                                 NULL, N_("Edit Nemo preferences")},
                               { NEMO_ACTION_PLUGIN_MANAGER, NULL,
                                 N_("Plugins"),               
                                 "<alt>p", N_("Manage extensions, actions and scripts") },
#ifdef TEXT_CHANGE_UNDO
  /* name, stock id, label */  { "Undo", NULL, N_("_Undo"),
                                 "<control>Z", N_("Undo the last text change"),
                                 G_CALLBACK (action_undo_callback) },
#endif
  /* name, stock id, label */  { NEMO_ACTION_UP, GTK_STOCK_GO_UP, N_("Open _Parent"),
                                 "<alt>Up", N_("Open the parent folder")},
  /* name, stock id */         { NEMO_ACTION_STOP, GTK_STOCK_STOP,
  /* label, accelerator */       N_("_Stop"), NULL,
  /* tooltip */                  N_("Stop loading the current location")},
  /* name, stock id */         { NEMO_ACTION_RELOAD, GTK_STOCK_REFRESH,
  /* label, accelerator */       N_("_Reload"), "<control>R",
  /* tooltip */                  N_("Reload the current location") },
  /* name, stock id */         { "NemoHelp", GTK_STOCK_HELP,
  /* label, accelerator */       N_("_All Topics"), "F1",
  /* tooltip */                  N_("Display Nemo help") },
  /* name, stock id */         { "About Nemo", GTK_STOCK_ABOUT,
  /* label, accelerator */       N_("_About"), NULL,
  /* tooltip */                  N_("Display credits for the creators of Nemo") },
  /* name, stock id */         { NEMO_ACTION_ZOOM_IN, GTK_STOCK_ZOOM_IN,
  /* label, accelerator */       N_("Zoom _In"), "<control>plus",
  /* tooltip */                  N_("Increase the view size") },
  /* name, stock id */         { "ZoomInAccel", NULL,
  /* label, accelerator */       "ZoomInAccel", "<control>equal",
  /* tooltip */                  NULL },
  /* name, stock id */         { "ZoomInAccel2", NULL,
  /* label, accelerator */       "ZoomInAccel2", "<control>KP_Add",
  /* tooltip */                  NULL },
  /* name, stock id */         { NEMO_ACTION_ZOOM_OUT, GTK_STOCK_ZOOM_OUT,
  /* label, accelerator */       N_("Zoom _Out"), "<control>minus",
  /* tooltip */                  N_("Decrease the view size") },
  /* name, stock id */         { "ZoomOutAccel", NULL,
  /* label, accelerator */       "ZoomOutAccel", "<control>KP_Subtract",
  /* tooltip */                  NULL },
  /* name, stock id */         { NEMO_ACTION_ZOOM_NORMAL, GTK_STOCK_ZOOM_100,
  /* label, accelerator */       N_("Normal Si_ze"), "<control>0",
  /* tooltip */                  N_("Use the normal view size") },
  /* name, stock id */         { "Connect to Server", NULL, 
  /* label, accelerator */       N_("Connect to _Server…"), NULL,
  /* tooltip */                  N_("Connect to a remote computer or shared disk") },
  /* name, stock id */         { "Home", NEMO_ICON_HOME,
  /* label, accelerator */       N_("_Home"), "<alt>Home",
  /* tooltip */                  N_("Open your personal folder") },
  /* name, stock id */         { "Go to Computer", NEMO_ICON_COMPUTER,
  /* label, accelerator */       N_("_Computer"), NULL,
  /* tooltip */                  N_("Browse all local and remote disks and folders accessible from this computer") },
  /* name, stock id */         { "Go to Network", NEMO_ICON_NETWORK,
  /* label, accelerator */       N_("_Network"), NULL,
  /* tooltip */                  N_("Browse bookmarked and local network locations")},
  /* name, stock id */         { "Go to Templates", NEMO_ICON_TEMPLATE,
  /* label, accelerator */       N_("T_emplates"), NULL,
  /* tooltip */                  N_("Open your personal templates folder")},
  /* name, stock id */         { "Go to Trash", NEMO_ICON_TRASH,
  /* label, accelerator */       N_("_Trash"), NULL,
  /* tooltip */                  N_("Open your personal trash folder") },
  /* name, stock id, label */  { "Go", NULL, N_("_Go") },
  /* name, stock id, label */  { "Bookmarks", NULL, N_("_Bookmarks") },
  /* name, stock id, label */  { "Tabs", NULL, N_("_Tabs") },
  /* name, stock id, label */  { "New Window", "window-new", N_("New _Window"),
                                 "<control>N", N_("Open another Nemo window for the displayed location") },
  /* name, stock id, label */  { NEMO_ACTION_NEW_TAB, "tab-new", N_("New _Tab"),
                                 "<control>T", N_("Open another tab for the displayed location") },
  /* name, stock id, label */  { NEMO_ACTION_CLOSE_ALL_WINDOWS, NULL, N_("Close _All Windows"),
                                 "<control>Q", N_("Close all Navigation windows") },
  /* name, stock id, label */  { NEMO_ACTION_BACK, GTK_STOCK_GO_BACK, N_("_Back"),
				 "<alt>Left", N_("Go to the previous visited location") },
  /* name, stock id, label */  { NEMO_ACTION_FORWARD, GTK_STOCK_GO_FORWARD, N_("_Forward"),
				 "<alt>Right", N_("Go to the next visited location") },
  /* name, stock id, label */  { NEMO_ACTION_EDIT_LOCATION, NULL, N_("Toggle _Location Entry"),
                                 "<control>L", N_("Switch between location entry and breadcrumbs") },
  /* name, stock id, label */  { "SplitViewNextPane", NULL, N_("S_witch to Other Pane"),
				 "F6", N_("Move focus to the other pane in a split view window") },
  /* name, stock id, label */  { "SplitViewSameLocation", NULL, N_("Sa_me Location as Other Pane"),
				 NULL, N_("Go to the same location as in the extra pane") },
  /* name, stock id, label */  { NEMO_ACTION_ADD_BOOKMARK, GTK_STOCK_ADD, N_("_Add Bookmark"),
                                 "<control>d", N_("Add a bookmark for the current location to this menu") },
  /* name, stock id, label */  { NEMO_ACTION_EDIT_BOOKMARKS, NULL, N_("_Edit Bookmarks…"),
                                 "<control>b", N_("Display a window that allows editing the bookmarks in this menu") },
  { "TabsPrevious", NULL, N_("_Previous Tab"), "<control>Page_Up",
    N_("Activate previous tab") },
  { "TabsNext", NULL, N_("_Next Tab"), "<control>Page_Down",
    N_("Activate next tab") },
  { "TabsMoveLeft", NULL, N_("Move Tab _Left"), "<shift><control>Page_Up",
    N_("Move current tab to left") },
  { "TabsMoveRight", NULL, N_("Move Tab _Right"), "<shift><control>Page_Down",
    N_("Move current tab to right") },
  { "Sidebar List", NULL, N_("Sidebar") }
};

static const GtkToggleActionEntry main_toggle_entries[] = {
  /* name, stock id */         { "Show Hidden Files", NULL,
  /* label, accelerator */       N_("Show _Hidden Files"), "<control>H",
  /* tooltip */                  N_("Toggle the display of hidden files in the current window"),
                                 NULL,
                                 TRUE },
  /* name, stock id */     { "Show Hide Toolbar", NULL,
  /* label, accelerator */   N_("_Main Toolbar"), NULL,
  /* tooltip */              N_("Change the visibility of this window's main toolbar"),
			     NULL,
  /* is_active */            TRUE }, 
  /* name, stock id */     { "Show Hide Sidebar", NULL,
  /* label, accelerator */   N_("_Show Sidebar"), "F9",
  /* tooltip */              N_("Change the visibility of this window's side pane"),
                             NULL,
  /* is_active */            TRUE }, 
  /* name, stock id */     { "Show Hide Statusbar", NULL,
  /* label, accelerator */   N_("St_atusbar"), NULL,
  /* tooltip */              N_("Change the visibility of this window's statusbar"),
                             NULL,
  /* is_active */            TRUE },
  /* name, stock id */     { "Show Hide Menubar", NULL,
  /* label, accelerator */   N_("M_enubar"), NULL,
  /* tooltip */              N_("Change the default visibility of the menubar"),
                             NULL,
  /* is_active */            TRUE },
  /* name, stock id */     { "Search", "edit-find",
  /* label, accelerator */   N_("_Search for Files…"), "<control>f",
  /* tooltip */              N_("Search documents and folders by name"),
			     NULL,
  /* is_active */            FALSE },
  /* name, stock id */     { "Show Hide Extra Pane", NULL,
  /* label, accelerator */   N_("E_xtra Pane"), "F3",
  /* tooltip */              N_("Open an extra folder view side-by-side"),
                             NULL,
  /* is_active */            FALSE },
};

static const GtkRadioActionEntry main_radio_entries[] = {
	{ "Sidebar Places", NULL,
	  N_("Places"), NULL, N_("Select Places as the default sidebar"),
	  SIDEBAR_PLACES },
	{ "Sidebar Tree", NULL,
	  N_("Tree"), NULL, N_("Select Tree as the default sidebar"),
	  SIDEBAR_TREE }
};

GtkActionGroup *
nemo_window_create_toolbar_action_group (NemoWindow *window)
{
	//gboolean show_location_entry_initially;

	//NemoNavigationState *navigation_state;
	GtkActionGroup *action_group;
	//GtkAction *action;

	action_group = gtk_action_group_new ("ToolbarActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);

	/*

   	g_object_unref (action);

    action = GTK_ACTION (gtk_toggle_action_new (NEMO_ACTION_TOGGLE_LOCATION,
                                                _("Location"),
                                                _("Toggle Location Entry"),
                                                NULL));
    gtk_action_group_add_action (action_group, GTK_ACTION (action));
    show_location_entry_initially = g_settings_get_boolean (nemo_preferences, NEMO_PREFERENCES_SHOW_LOCATION_ENTRY);
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show_location_entry_initially);
    g_signal_connect (action, "activate",
                      G_CALLBACK (action_toggle_location_entry_callback), window);
    gtk_action_set_icon_name (GTK_ACTION (action), "location-symbolic");

    g_object_unref (action);


    action = GTK_ACTION (gtk_toggle_action_new (NEMO_ACTION_ICON_VIEW,
                         _("Icons"),
                         _("Icon View"),
                         NULL));
    g_signal_connect (action, "activate",
                      G_CALLBACK (action_icon_view_callback),
                      window);
   	gtk_action_group_add_action (action_group, action);
    gtk_action_set_icon_name (GTK_ACTION (action), "view-grid-symbolic");
   	g_object_unref (action);

    action = GTK_ACTION (gtk_toggle_action_new (NEMO_ACTION_LIST_VIEW,
                         _("List"),
                         _("List View"),
                         NULL));
    g_signal_connect (action, "activate",
                      G_CALLBACK (action_list_view_callback),
                      window);
   	gtk_action_group_add_action (action_group, action);
    gtk_action_set_icon_name (GTK_ACTION (action), "view-list-symbolic");

   	g_object_unref (action);

    action = GTK_ACTION (gtk_toggle_action_new (NEMO_ACTION_COMPACT_VIEW,
                         _("Compact"),
                         _("Compact View"),
                         NULL));
   	g_signal_connect (action, "activate",
                      G_CALLBACK (action_compact_view_callback),
                      window);
   	gtk_action_group_add_action (action_group, action);
    gtk_action_set_icon_name (GTK_ACTION (action), "view-compact-symbolic");

   	g_object_unref (action);

 	action = GTK_ACTION (gtk_toggle_action_new (NEMO_ACTION_SEARCH,
 				_("Search"),_("Search documents and folders by name"),
 				NULL));
 
  	gtk_action_group_add_action (action_group, action);
    gtk_action_set_icon_name (GTK_ACTION (action), "edit-find-symbolic");
  
  	g_object_unref (action);

	navigation_state = nemo_window_get_navigation_state (window);
	nemo_navigation_state_add_group (navigation_state, action_group);
	*/
	return action_group;
}

static gboolean
settings_map_get_bool_variant (GValue   *value,
                               GVariant *variant,
                               gpointer  user_data)
{
	g_return_val_if_fail (g_variant_is_of_type (variant,
	                                            G_VARIANT_TYPE_BOOLEAN),
	                      FALSE);

	g_value_set_variant (value, variant);

	return TRUE;
}

static GVariant*
settings_map_set_variant (const GValue       *value,
                           const GVariantType *expected_type,
                           gpointer            user_data)
{
	g_return_val_if_fail (g_variant_is_of_type (g_value_get_variant (value), expected_type), NULL);

	return g_value_dup_variant (value);
}

static gboolean
transform_bool_to_variant (GBinding *binding,
                           const GValue *from_value,
                           GValue *to_value,
                           gpointer user_data) {
	gboolean from_bool;
	GVariant *to_variant;

	from_bool = g_value_get_boolean (from_value);
	to_variant = g_variant_new_boolean(from_bool);
	g_value_take_variant (to_value, to_variant);
	return TRUE;
}

static gboolean
transform_bool_from_variant (GBinding *binding,
                             const GValue *from_value,
                             GValue *to_value,
                             gpointer user_data) {
	GVariant *from_variant;
	gboolean to_bool;


	from_variant = g_value_get_variant (from_value);
	to_bool = g_variant_get_boolean(from_variant);
	g_value_set_boolean (to_value, to_bool);
	return TRUE;
}

static void
window_menus_set_bindings (NemoWindow *window)
{

	GActionMap *action_map;
	GAction *action;

	action_map = G_ACTION_MAP (window);

	action = g_action_map_lookup_action (action_map, "show-hidden-files");
	g_settings_bind_with_mapping (gtk_filechooser_preferences,
			 NEMO_PREFERENCES_SHOW_HIDDEN,
			 action,
			 "state",
			 G_SETTINGS_BIND_DEFAULT,
             settings_map_get_bool_variant,
             settings_map_set_variant,
             NULL, NULL);

	action = g_action_map_lookup_action (action_map, "show-toolbar");
	g_settings_bind_with_mapping (nemo_window_state,
			 NEMO_WINDOW_STATE_START_WITH_TOOLBAR,
			 action,
			 "state",
			 G_SETTINGS_BIND_DEFAULT,
             settings_map_get_bool_variant,
             settings_map_set_variant,
             NULL, NULL);

	action = g_action_map_lookup_action (action_map, "show-statusbar");
	g_settings_bind_with_mapping (nemo_window_state,
			 NEMO_WINDOW_STATE_START_WITH_STATUS_BAR,
			 action,
			 "state",
			 G_SETTINGS_BIND_DEFAULT,
             settings_map_get_bool_variant,
             settings_map_set_variant,
             NULL, NULL);

	action = g_action_map_lookup_action (action_map, "show-menubar");
    g_settings_bind_with_mapping (nemo_window_state,
             NEMO_WINDOW_STATE_START_WITH_MENU_BAR,
             action,
             "state",
             G_SETTINGS_BIND_DEFAULT,
             settings_map_get_bool_variant,
             settings_map_set_variant,
             NULL, NULL);

	action = g_action_map_lookup_action (action_map, "show-sidebar");
    g_object_bind_property_full (window,
             "show-sidebar",
             action,
             "state",
             G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE,
			 transform_bool_to_variant,
			 transform_bool_from_variant,
             NULL, NULL);
}

void 
nemo_window_initialize_actions (NemoWindow *window)
{
	GtkActionGroup *action_group;
	const gchar *nav_state_actions[] = {
		NEMO_ACTION_BACK, NEMO_ACTION_FORWARD, NEMO_ACTION_UP, NEMO_ACTION_RELOAD, NEMO_ACTION_COMPUTER, NEMO_ACTION_HOME, NEMO_ACTION_EDIT_LOCATION,
		NEMO_ACTION_TOGGLE_LOCATION, NULL
	};

	action_group = nemo_window_get_main_action_group (window);
	window->details->nav_state = nemo_navigation_state_new (action_group,
								    nav_state_actions);

	window_menus_set_bindings (window);
	nemo_window_update_show_hide_menu_items (window);

	g_signal_connect (window, "loading_uri",
			  G_CALLBACK (nemo_window_update_split_view_actions_sensitivity),
			  NULL);
}

/**
 * nemo_window_initialize_menus
 * 
 * Create and install the set of menus for this window.
 * @window: A recently-created NemoWindow.
 */
void 
nemo_window_initialize_menus (NemoWindow *window)
{
	GtkActionGroup *action_group_;
	GtkUIManager *ui_manager;

	GtkBuilder *builder;
	gint i;

	if (window->details->ui_manager == NULL){
        window->details->ui_manager = gtk_ui_manager_new ();
    }
	ui_manager = window->details->ui_manager;

	if (window->details->builder == NULL){
        window->details->builder = gtk_builder_new ();
    }
	builder = window->details->builder;

	/* win actions */
	g_action_map_add_action_entries (G_ACTION_MAP (window),
	 					             win_entries,
	 					             G_N_ELEMENTS (win_entries),
	 					             window);

	action_group_ = gtk_action_group_new ("ShellActions");
	gtk_action_group_set_translation_domain (action_group_, GETTEXT_PACKAGE);
	window->details->main_action_group = action_group_;
	gtk_action_group_add_actions (action_group_, 
				      main_entries, G_N_ELEMENTS (main_entries),
				      window);
	gtk_action_group_add_toggle_actions (action_group_, 
					     main_toggle_entries, G_N_ELEMENTS (main_toggle_entries),
					     window);
	gtk_action_group_add_radio_actions (action_group_,
					    main_radio_entries, G_N_ELEMENTS (main_radio_entries),
					    0, G_CALLBACK (sidebar_radio_entry_changed_cb),
					    window);

	g_signal_connect_object (NEMO_WINDOW (window), "notify::sidebar-view-id",
                             G_CALLBACK (update_side_bar_radio_buttons), window, 0);


    NemoApplication *app = NEMO_APPLICATION (g_application_get_default ());

	/* Alt+N for the first 10 tabs */
	for (i = 0; i < 10; ++i) {
		gchar detailed_action_name[80];
		gchar accelerator[80];

		snprintf(detailed_action_name, sizeof (detailed_action_name), "win.go-to-tab(%d)", i);
		snprintf(accelerator, sizeof (accelerator), "<alt>%d", (i+1)%10);
		nemo_application_set_accelerator (app, detailed_action_name, accelerator);
	}

	gtk_ui_manager_insert_action_group (ui_manager, action_group_, 0);
	g_object_unref (action_group_); /* owned by ui_manager */

	gtk_window_add_accel_group (GTK_WINDOW (window),
				    gtk_ui_manager_get_accel_group (ui_manager));
	
	g_signal_connect (ui_manager, "connect-proxy",
			  G_CALLBACK (connect_proxy_cb), window);
	g_signal_connect (ui_manager, "disconnect_proxy",
			  G_CALLBACK (disconnect_proxy_cb), window);

	/* add the UI */
	GError  *error = NULL;
	gtk_builder_add_from_resource (builder, "/org/nemo/nemo-shell-ui.xml", &error);
	g_assert_no_error (error);

	nemo_window_initialize_trash_icon_monitor (window);
	nemo_window_initialize_go_menu (window);
}

void
nemo_window_finalize_menus (NemoWindow *window)
{
	g_signal_handlers_disconnect_by_func (nemo_trash_monitor_get(),
					      trash_state_changed_cb, window);
}

static GList *
get_extension_menus (NemoWindow *window)
{
	NemoFile *file;
	NemoWindowSlot *slot;
	GList *providers;
	GList *items;
	GList *l;
	
	providers = nemo_module_get_extensions_for_type (NEMO_TYPE_MENU_PROVIDER);
	items = NULL;

	slot = nemo_window_get_active_slot (window);
	file = nemo_window_slot_get_file (slot);

	for (l = providers; l != NULL; l = l->next) {
		NemoMenuProvider *provider;
		GList *file_items;
		
		provider = NEMO_MENU_PROVIDER (l->data);
		file_items = nemo_menu_provider_get_background_items (provider,
									  GTK_WIDGET (window),
									  file);
		items = g_list_concat (items, file_items);
	}

	nemo_module_extension_list_free (providers);

	return items;
}

static void
add_extension_menu_items (NemoWindow *window,
			  guint merge_id,
			  GtkActionGroup *action_group,
			  GList *menu_items,
			  const char *subdirectory)
{
	GtkUIManager *ui_manager;
	GList *l;

	ui_manager = window->details->ui_manager;
	
	for (l = menu_items; l; l = l->next) {
		NemoMenuItem *item;
		NemoMenu *menu;
		GtkAction *action;
		char *path;
		
		item = NEMO_MENU_ITEM (l->data);
		
		g_object_get (item, "menu", &menu, NULL);
		
		action = nemo_action_from_menu_item (item, GTK_WIDGET (window));
		gtk_action_group_add_action_with_accel (action_group, action, NULL);
		
		path = g_build_path ("/", POPUP_PATH_EXTENSION_ACTIONS, subdirectory, NULL);
		gtk_ui_manager_add_ui (ui_manager,
				       merge_id,
				       path,
				       gtk_action_get_name (action),
				       gtk_action_get_name (action),
				       (menu != NULL) ? GTK_UI_MANAGER_MENU : GTK_UI_MANAGER_MENUITEM,
				       FALSE);
		g_free (path);

		path = g_build_path ("/", MENU_PATH_EXTENSION_ACTIONS, subdirectory, NULL);
		gtk_ui_manager_add_ui (ui_manager,
				       merge_id,
				       path,
				       gtk_action_get_name (action),
				       gtk_action_get_name (action),
				       (menu != NULL) ? GTK_UI_MANAGER_MENU : GTK_UI_MANAGER_MENUITEM,
				       FALSE);
		g_free (path);

		/* recursively fill the menu */		       
		if (menu != NULL) {
			char *subdir;
			GList *children;
			
			children = nemo_menu_get_items (menu);
			
			subdir = g_build_path ("/", subdirectory, "/", gtk_action_get_name (action), NULL);
			add_extension_menu_items (window,
						  merge_id,
						  action_group,
						  children,
						  subdir);

			nemo_menu_item_list_free (children);
			g_free (subdir);
		}			

                g_object_unref (action);
	}
}

void
nemo_window_load_extension_menus (NemoWindow *window)
{
	GtkActionGroup *action_group;
	GList *items;
	guint merge_id;

	if (window->details->extensions_menu_merge_id != 0) {
		gtk_ui_manager_remove_ui (window->details->ui_manager,
					  window->details->extensions_menu_merge_id);
		window->details->extensions_menu_merge_id = 0;
	}

	if (window->details->extensions_menu_action_group != NULL) {
		gtk_ui_manager_remove_action_group (window->details->ui_manager,
						    window->details->extensions_menu_action_group);
		window->details->extensions_menu_action_group = NULL;
	}
	
	merge_id = gtk_ui_manager_new_merge_id (window->details->ui_manager);
	window->details->extensions_menu_merge_id = merge_id;
	action_group = gtk_action_group_new ("ExtensionsMenuGroup");
	window->details->extensions_menu_action_group = action_group;
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_ui_manager_insert_action_group (window->details->ui_manager, action_group, 0);
	g_object_unref (action_group); /* owned by ui manager */

	items = get_extension_menus (window);

	if (items != NULL) {
		add_extension_menu_items (window, merge_id, action_group, items, "");

		g_list_foreach (items, (GFunc) g_object_unref, NULL);
		g_list_free (items);
	}
}
