/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 *  Nautilus
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 2000, 2001 Eazel, Inc.
 *
 *  Nautilus is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  Nautilus is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>,
 *           Darin Adler <darin@bentspoon.com>
 *
 */

#include <config.h>
#include "nautilus-application.h"


#include "file-manager/fm-ditem-page.h"
#include "file-manager/fm-desktop-icon-view.h"
#include "file-manager/fm-icon-view.h"
#include "file-manager/fm-list-view.h"
#include "file-manager/fm-tree-view.h"
#if ENABLE_EMPTY_VIEW
#include "file-manager/fm-empty-view.h"
#endif /* ENABLE_EMPTY_VIEW */
#include "nautilus-information-panel.h"
#include "nautilus-history-sidebar.h"
#include "nautilus-places-sidebar.h"
#include "nautilus-notes-viewer.h"
#include "nautilus-emblem-sidebar.h"
#include "nautilus-image-properties-page.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "nautilus-desktop-window.h"
#include "nautilus-first-time-druid.h"
#include "nautilus-main.h"
#include "nautilus-spatial-window.h"
#include "nautilus-navigation-window.h"
#include "nautilus-shell-interface.h"
#include "nautilus-shell.h"
#include "nautilus-window-bookmarks.h"
#include "nautilus-window-private.h"
#include "nautilus-window-manage-views.h"
#include <libxml/xmlsave.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-object.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-stock-dialogs.h>
#include <eel/eel-string-list.h>
#include <eel/eel-string.h>
#include <gdk/gdkx.h>
#include <gtk/gtkinvisible.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <libgnome/gnome-config.h>
#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-authentication-manager.h>
#include <libgnomeui/gnome-client.h>
#include <libgnomeui/gnome-messagebox.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>
#include <libnautilus-private/nautilus-debug-log.h>
#include <libnautilus-private/nautilus-file-utilities.h>
#include <libnautilus-private/nautilus-global-preferences.h>
#include <libnautilus-private/nautilus-icon-factory.h>
#include <libnautilus-private/nautilus-metafile-factory.h>
#include <libnautilus-private/nautilus-module.h>
#include <libnautilus-private/nautilus-undo-manager.h>
#include <libnautilus-private/nautilus-desktop-link-monitor.h>
#include <libnautilus-private/nautilus-lockdown-manager.h>
#include <libnautilus-private/nautilus-directory-private.h>
#include <libnautilus-private/nautilus-signaller.h>
#include <libnautilus-extension/nautilus-menu-provider.h>
#include <bonobo-activation/bonobo-activation.h>
#ifdef HAVE_STARTUP_NOTIFICATION
#define SN_API_NOT_YET_FROZEN Yes_i_know_DO_IT
#include <libsn/sn-launchee.h>
#endif

/* Needed for the is_kdesktop_present check */
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#define FACTORY_IID	     "OAFIID:Nautilus_Factory"
#define SEARCH_LIST_VIEW_IID "OAFIID:Nautilus_File_Manager_Search_List_View"
#define SHELL_IID	     "OAFIID:Nautilus_Shell"
#define TREE_VIEW_IID         "OAFIID:Nautilus_File_Manager_Tree_View"

/* Keeps track of all the desktop windows. */
static GList *nautilus_application_desktop_windows;

/* Keeps track of all the nautilus windows. */
static GList *nautilus_application_window_list;

/* Keeps track of all the object windows */
static GList *nautilus_application_spatial_window_list;

static void     desktop_changed_callback          (gpointer                  user_data);
static void     desktop_location_changed_callback (gpointer                  user_data);
static void     volume_unmounted_callback         (GnomeVFSVolumeMonitor    *monitor,
						   GnomeVFSVolume           *volume,
						   NautilusApplication      *application);
static void     volume_mounted_callback           (GnomeVFSVolumeMonitor    *monitor,
						   GnomeVFSVolume           *volume,
						   NautilusApplication      *application);
static void     update_session                    (gpointer                  callback_data);
static void     init_session                      (void);
static gboolean is_kdesktop_present               (void);

static char    *save_session_to_file              (void);

BONOBO_CLASS_BOILERPLATE (NautilusApplication, nautilus_application,
			  BonoboGenericFactory, BONOBO_TYPE_GENERIC_FACTORY)

static CORBA_Object
create_object (PortableServer_Servant servant,
	       const CORBA_char *iid,
	       CORBA_Environment *ev)
{
	BonoboObject *object;
	NautilusApplication *application;

	if (strcmp (iid, SHELL_IID) == 0) {
		application = NAUTILUS_APPLICATION (bonobo_object_from_servant (servant));
		object = BONOBO_OBJECT (nautilus_shell_new (application));
	} else if (strcmp (iid, METAFILE_FACTORY_IID) == 0) {
		object = BONOBO_OBJECT (nautilus_metafile_factory_get_instance ());
	} else {
		object = CORBA_OBJECT_NIL;
	}

	return CORBA_Object_duplicate (BONOBO_OBJREF (object), ev);
}

GList *
nautilus_application_get_window_list (void)
{
	return nautilus_application_window_list;
}

GList *
nautilus_application_get_spatial_window_list (void)
{
	return nautilus_application_spatial_window_list;
}

unsigned int
nautilus_application_get_n_windows (void)
{
	return g_list_length (nautilus_application_window_list) +
	       g_list_length (nautilus_application_desktop_windows);
}

static void
nautilus_application_instance_init (NautilusApplication *application)
{
	/* Create an undo manager */
	application->undo_manager = nautilus_undo_manager_new ();

	application->shell = nautilus_shell_new (application);
	
	/* Watch for volume mounts so we can restore open windows
	 * This used to be for showing new window on mount, but is not
	 * used anymore */

	/* Watch for volume unmounts so we can close open windows */
	g_signal_connect_object (gnome_vfs_get_volume_monitor (), "volume_unmounted",
				 G_CALLBACK (volume_unmounted_callback), application, 0);
	g_signal_connect_object (gnome_vfs_get_volume_monitor (), "volume_pre_unmount",
				 G_CALLBACK (volume_unmounted_callback), application, 0);
	g_signal_connect_object (gnome_vfs_get_volume_monitor (), "volume_mounted",
				 G_CALLBACK (volume_mounted_callback), application, 0);

	/* register views */
	fm_icon_view_register ();
	fm_desktop_icon_view_register ();
	fm_list_view_register ();
#if ENABLE_EMPTY_VIEW
	fm_empty_view_register ();
#endif /* ENABLE_EMPTY_VIEW */

	/* register sidebars */
	nautilus_places_sidebar_register ();
	nautilus_information_panel_register ();
	fm_tree_view_register ();
	nautilus_history_sidebar_register ();
	nautilus_notes_viewer_register (); /* also property page */
	nautilus_emblem_sidebar_register ();

	/* register property pages */
	nautilus_image_properties_page_register ();
}

NautilusApplication *
nautilus_application_new (void)
{
	NautilusApplication *application;

	application = g_object_new (NAUTILUS_TYPE_APPLICATION, NULL);
	
	bonobo_generic_factory_construct_noreg (BONOBO_GENERIC_FACTORY (application),
						FACTORY_IID,
						NULL);
	
	return application;
}

static void
nautilus_application_destroy (BonoboObject *object)
{
	NautilusApplication *application;

	application = NAUTILUS_APPLICATION (object);

	nautilus_bookmarks_exiting ();
	
	g_object_unref (application->undo_manager);

	if (application->shell_registered) {
		bonobo_activation_unregister_active_server (SHELL_IID, BONOBO_OBJREF (application->shell));
	}
	bonobo_object_unref (application->shell);

	EEL_CALL_PARENT (BONOBO_OBJECT_CLASS, destroy, (object));
}

static gboolean
check_required_directories (NautilusApplication *application)
{
	char *user_directory;
	char *desktop_directory;
	EelStringList *directories;
	char *directories_as_string;
	char *error_string;
	char *detail_string;
	GtkDialog *dialog;
	int failed_count;
	
	g_assert (NAUTILUS_IS_APPLICATION (application));

	user_directory = nautilus_get_user_directory ();
	desktop_directory = nautilus_get_desktop_directory ();

	directories = eel_string_list_new (TRUE);
	
	if (!g_file_test (user_directory, G_FILE_TEST_IS_DIR)) {
		eel_string_list_insert (directories, user_directory);
	}
	g_free (user_directory);	    
	    
	if (!g_file_test (desktop_directory, G_FILE_TEST_IS_DIR)) {
		eel_string_list_insert (directories, desktop_directory);
	}
	g_free (desktop_directory);

	failed_count = eel_string_list_get_length (directories);

	if (failed_count != 0) {
		directories_as_string = eel_string_list_as_string (directories, ", ", EEL_STRING_LIST_ALL_STRINGS);

		if (failed_count == 1) {
			error_string = g_strdup_printf (_("Nautilus could not create the required folder \"%s\"."),
							directories_as_string);
			detail_string = _("Before running Nautilus, please create the following folder, or "
					  "set permissions such that Nautilus can create it.");
		} else {
			error_string = g_strdup_printf (_("Nautilus could not create the following required folders: "
							  "%s."), directories_as_string);
  			detail_string = _("Before running Nautilus, please create these folders, or "
					  "set permissions such that Nautilus can create them.");
		}
		
		dialog = eel_show_error_dialog (error_string, detail_string, NULL);
		/* We need the main event loop so the user has a chance to see the dialog. */
		nautilus_main_event_loop_register (GTK_OBJECT (dialog));

		g_free (directories_as_string);
		g_free (error_string);
	}

	eel_string_list_free (directories);

	return failed_count == 0;
}

static Nautilus_URIList *
nautilus_make_uri_list_from_shell_strv (const char * const *strv)
{
	int length, i;
	Nautilus_URIList *uri_list;
	char *translated_uri;

	length = g_strv_length ((char **) strv);

	uri_list = Nautilus_URIList__alloc ();
	uri_list->_maximum = length;
	uri_list->_length = length;
	uri_list->_buffer = CORBA_sequence_Nautilus_URI_allocbuf (length);
	for (i = 0; i < length; i++) {
		translated_uri = gnome_vfs_make_uri_from_shell_arg (strv[i]);
		uri_list->_buffer[i] = CORBA_string_dup (translated_uri);
		g_free (translated_uri);
		translated_uri = NULL;
	}
	CORBA_sequence_set_release (uri_list, CORBA_TRUE);

	return uri_list;
}

static void
migrate_old_nautilus_files (void)
{
	char *new_desktop_dir;
	char *old_desktop_dir;
	char *migrated_file;
	char *link_name;
	char *link_path;
	int fd;
	
	old_desktop_dir = nautilus_get_gmc_desktop_directory ();
	if (!g_file_test (old_desktop_dir, G_FILE_TEST_IS_DIR) ||
	    g_file_test (old_desktop_dir, G_FILE_TEST_IS_SYMLINK)) {
		g_free (old_desktop_dir);
		return;
	}
	migrated_file = g_build_filename (old_desktop_dir, ".migrated", NULL);
	if (!g_file_test (migrated_file, G_FILE_TEST_EXISTS)) {
		link_name = g_filename_from_utf8 (_("Link To Old Desktop"), -1, NULL, NULL, NULL);
		new_desktop_dir = nautilus_get_desktop_directory ();
		link_path = g_build_filename (new_desktop_dir, link_name, NULL);
	
		
		symlink ("../.gnome-desktop", link_path);
		
		g_free (link_name);
		g_free (new_desktop_dir);
		g_free (link_path);

		fd = creat (migrated_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd >= 0) {
			close (fd);
		}
		
		eel_show_info_dialog (_("A link called \"Link To Old Desktop\" has been created on the desktop."),
				      _("The location of the desktop directory has changed in GNOME 2.4. "
					"You can open the link and move over the files you want, then delete the link."),
				      NULL);
	}
	g_free (old_desktop_dir);
	g_free (migrated_file);
}

static void
menu_provider_items_updated_handler (NautilusMenuProvider *provider, GtkWidget* parent_window, gpointer data)
{

	g_signal_emit_by_name (nautilus_signaller_get_current (),
			       "popup_menu_changed");
}

static void
menu_provider_init_callback (void)
{
        GList *items;
        GList *providers;
        GList *l;

        providers = nautilus_module_get_extensions_for_type (NAUTILUS_TYPE_MENU_PROVIDER);
        items = NULL;

        for (l = providers; l != NULL; l = l->next) {
                NautilusMenuProvider *provider = NAUTILUS_MENU_PROVIDER (l->data);

		g_signal_connect_after (G_OBJECT (provider), "items_updated",
                           (GCallback)menu_provider_items_updated_handler,
                           NULL);
        }

        nautilus_module_extension_list_free (providers);
}

static void
finish_startup (NautilusApplication *application)
{
	/* initialize nautilus modules */
	nautilus_module_init ();

	nautilus_module_add_type (FM_TYPE_DITEM_PAGE);

	/* attach menu-provider module callback */
	menu_provider_init_callback ();
	
	/* initialize URI authentication manager */
	gnome_authentication_manager_init ();

	/* Make the desktop work with old Nautilus. */
	migrate_old_nautilus_files ();

	/* Initialize the desktop link monitor singleton */
	nautilus_desktop_link_monitor_get ();

    /* Initialize lockdown manager */
    nautilus_lockdown_manager_get ();
}

static void
initialize_kde_trash_hack (void)
{
	char *trash_dir;
	char *desktop_dir, *desktop_uri, *kde_trash_dir;
	char *dir, *basename;
	char *kde_conf_file;
	char *key;
	gboolean def;
	
	trash_dir = NULL;

	desktop_uri = nautilus_get_desktop_directory_uri_no_create ();
	desktop_dir = gnome_vfs_get_local_path_from_uri (desktop_uri);
	g_free (desktop_uri);
	
	if (g_file_test (desktop_dir, G_FILE_TEST_EXISTS)) {
		/* Look for trash directory */
		kde_conf_file = g_build_filename (g_get_home_dir(), ".kde/share/config/kdeglobals", NULL);
		key = g_strconcat ("=", kde_conf_file, "=/Paths/Trash", NULL);
		kde_trash_dir = gnome_config_get_string_with_default (key, &def);
		gnome_config_drop_file (kde_conf_file);
		g_free (kde_conf_file);
		g_free (key);

		if (kde_trash_dir == NULL) {
			kde_conf_file = "/usr/share/config/kdeglobals";
			key = g_strconcat ("=", kde_conf_file, "=/Paths/Trash", NULL);
			kde_trash_dir = gnome_config_get_string_with_default (key, &def);
			gnome_config_drop_file (kde_conf_file);
			g_free (key);
		}

		if (kde_trash_dir != NULL) {
			basename = g_path_get_basename (kde_trash_dir);
			g_free (kde_trash_dir);
			
			dir = g_build_filename (desktop_dir, basename, NULL);

			if (g_file_test (dir, G_FILE_TEST_IS_DIR)) {
				trash_dir = g_strdup (basename);
			} 
			g_free (basename);
			g_free (dir);
		} 

		if (trash_dir != NULL) {
			nautilus_set_kde_trash_name (trash_dir);
		}

		g_free (trash_dir);
	}
	g_free (desktop_dir);
}


static Bonobo_RegistrationResult
nautilus_bonobo_activation_register_for_display (const char    *iid,
						 Bonobo_Unknown ref)
{
	const char *display_name;
	GSList *reg_env ;
	Bonobo_RegistrationResult result;
	
	display_name = gdk_display_get_name (gdk_display_get_default());
	reg_env = bonobo_activation_registration_env_set (NULL,
							  "DISPLAY", display_name);
	result = bonobo_activation_register_active_server (iid, ref, reg_env);
	bonobo_activation_registration_env_free (reg_env);
	return result;
}

void
nautilus_application_startup (NautilusApplication *application,
			      gboolean kill_shell,
			      gboolean restart_shell,
			      gboolean no_default_window,
			      gboolean no_desktop,
			      gboolean do_first_time_druid_check,
			      gboolean browser_window,
			      const char *startup_id,
			      const char *geometry,
			      const char *session_to_load,
			      const char *urls[])
{
	CORBA_Environment ev;
	Nautilus_Shell shell;
	Bonobo_RegistrationResult result;
	const char *message, *detailed_message;
	GtkDialog *dialog;
	Nautilus_URIList *url_list;
	const CORBA_char *corba_startup_id;
	const CORBA_char *corba_geometry;
	int num_failures;

	num_failures = 0;

	/* Check the user's ~/.nautilus directories and post warnings
	 * if there are problems.
	 */
	if (!kill_shell && !check_required_directories (application)) {
		return;
	}

	initialize_kde_trash_hack ();

	CORBA_exception_init (&ev);

	/* Start up the factory. */
	while (TRUE) {
		/* Try to register the file manager view factory. */
		result = nautilus_bonobo_activation_register_for_display
			(SHELL_IID, BONOBO_OBJREF (application->shell));

		switch (result) {
		case Bonobo_ACTIVATION_REG_SUCCESS:
			/* We are registered and all is right with the world. */
			application->shell_registered = TRUE;
			finish_startup (application);
			message = NULL;
			detailed_message = NULL;
			break;
		case Bonobo_ACTIVATION_REG_ALREADY_ACTIVE:
			/* Another copy of nautilus already is running and registered. */
			message = NULL;
			detailed_message = NULL;
			break;
		case Bonobo_ACTIVATION_REG_NOT_LISTED:
			/* Can't register myself due to trouble locating the
			 * Nautilus_Shell.server file. This has happened when you
			 * launch Nautilus with an LD_LIBRARY_PATH that
			 * doesn't include the directory containing the oaf
			 * library. It could also happen if the
			 * Nautilus_Shell.server file was not present for some
			 * reason. Sometimes killing oafd and gconfd fixes
			 * this problem but we don't exactly understand why,
			 * since neither of the above causes explain it.
			 */
			message = _("Nautilus can't be used now. "
				    "Running the command \"bonobo-slay\""
				    " from the console may fix the problem. If not,"
				    " you can try rebooting the computer or"
				    " installing Nautilus again.");
			/* FIXME bugzilla.gnome.org 42536: The guesses and stuff here are lame. */
			detailed_message = _("Nautilus can't be used now. "
					     "Running the command \"bonobo-slay\" "
					     "from the console may fix the problem. If not, "
					     "you can try rebooting the computer or "
					     "installing Nautilus again.\n\n"
					     "Bonobo couldn't locate the Nautilus_shell.server file. "
					     "One cause of this seems to be an LD_LIBRARY_PATH "
					     "that does not include the bonobo-activation library's directory. "
					     "Another possible cause would be bad install "
					     "with a missing Nautilus_Shell.server file.\n\n"
					     "Running \"bonobo-slay\" will kill all "
					     "Bonobo Activation and GConf processes, which may be needed by "
					     "other applications.\n\n"
					     "Sometimes killing bonobo-activation-server and gconfd fixes "
					     "the problem, but we don't know why.\n\n"
					     "We have also seen this error when a faulty "
					     "version of bonobo-activation was installed.");
			break;
		default:
			/* This should never happen. */
			g_warning ("bad error code from bonobo_activation_active_server_register");
		case Bonobo_ACTIVATION_REG_ERROR:
			/* Some misc. error (can never happen with current
			 * version of bonobo-activation). Show dialog and terminate the
			 * program.
			 */
			/* FIXME bugzilla.gnome.org 42537: Looks like this does happen with the
			 * current OAF. I guess I read the code wrong. Need to figure out when and make a
			 * good message.
			 */
			message = _("Nautilus can't be used now, due to an unexpected error.");
			detailed_message = _("Nautilus can't be used now, due to an unexpected error "
					     "from Bonobo when attempting to register the file manager view server.");
			break;
		}

		/* Get the shell object. */
		if (message == NULL) {
			shell = bonobo_activation_activate_from_id (SHELL_IID, Bonobo_ACTIVATION_FLAG_EXISTING_ONLY, NULL, NULL);
			if (!CORBA_Object_is_nil (shell, &ev)) {
				break;
			}

			/* If we couldn't find ourselves it's a bad problem so
			 * we better stop looping.
			 */
			if (result == Bonobo_ACTIVATION_REG_SUCCESS) {
				/* FIXME bugzilla.gnome.org 42538: When can this happen? */
				message = _("Nautilus can't be used now, due to an unexpected error.");
				detailed_message = _("Nautilus can't be used now, due to an unexpected error "
						     "from Bonobo when attempting to locate the factory. "
						     "Killing bonobo-activation-server and restarting Nautilus may help fix the problem.");
			} else {
				num_failures++;
				if (num_failures > 20) {
					message = _("Nautilus can't be used now, due to an unexpected error.");
					detailed_message = _("Nautilus can't be used now, due to an unexpected error "
							     "from Bonobo when attempting to locate the shell object. "
							     "Killing bonobo-activation-server and restarting Nautilus may help fix the problem.");
					
				}
			}
		}

		if (message != NULL) {
			dialog = eel_show_error_dialog_with_details (message, NULL, detailed_message, NULL);
			/* We need the main event loop so the user has a chance to see the dialog. */
			nautilus_main_event_loop_register (GTK_OBJECT (dialog));
			goto out;
		}
	}

	if (kill_shell) {
		Nautilus_Shell_quit (shell, &ev);
	} else if (restart_shell) {
		Nautilus_Shell_restart (shell, &ev);
	} else {
		/* If KDE desktop is running, then force no_desktop */
		if (is_kdesktop_present ()) {
			no_desktop = TRUE;
		}
		
		if (!no_desktop && eel_preferences_get_boolean (NAUTILUS_PREFERENCES_SHOW_DESKTOP)) {
			Nautilus_Shell_start_desktop (shell, &ev);
		}
		
		/* Monitor the preference to show or hide the desktop */
		eel_preferences_add_callback_while_alive (NAUTILUS_PREFERENCES_SHOW_DESKTOP,
							  desktop_changed_callback,
							  application,
							  G_OBJECT (application));

		/* Monitor the preference to have the desktop */
		/* point to the Unix home folder */
		eel_preferences_add_callback_while_alive (NAUTILUS_PREFERENCES_DESKTOP_IS_HOME_DIR,
							  desktop_location_changed_callback,
							  NULL,
							  G_OBJECT (application));

		/* CORBA C mapping doesn't allow NULL to be passed
		   for string parameters */
		corba_geometry   = (geometry   != NULL) ? geometry   : "";
		corba_startup_id = (startup_id != NULL) ? startup_id : "";

	  	/* Create the other windows. */
		if (urls != NULL) {
			url_list = nautilus_make_uri_list_from_shell_strv (urls);
			Nautilus_Shell_open_windows (shell, url_list, corba_startup_id, corba_geometry, browser_window, &ev);
			CORBA_free (url_list);
		} else if (!no_default_window) {
			g_assert (session_to_load == NULL);
			Nautilus_Shell_open_default_window (shell, corba_startup_id, corba_geometry, browser_window, &ev);
		}

		if (session_to_load != NULL) {
			Nautilus_Shell_load_session (shell, session_to_load, &ev);
		}
		
		/* Add ourselves to the session */
		init_session ();
	}

 out:
	CORBA_exception_free (&ev);
}


static void 
selection_get_cb (GtkWidget          *widget,
		  GtkSelectionData   *selection_data,
		  guint               info,
		  guint               time)
{
	/* No extra targets atm */
}

static GtkWidget *
get_desktop_manager_selection (GdkDisplay *display, int screen)
{
	char *selection_name;
	GdkAtom selection_atom;
	Window selection_owner;
	GtkWidget *selection_widget;

	selection_name = g_strdup_printf ("_NET_DESKTOP_MANAGER_S%d", screen);
	selection_atom = gdk_atom_intern (selection_name, FALSE);
	g_free (selection_name);

	selection_owner = XGetSelectionOwner (GDK_DISPLAY_XDISPLAY (display),
					      gdk_x11_atom_to_xatom_for_display (display, 
										 selection_atom));
	if (selection_owner != None) {
		return NULL;
	}
	
	selection_widget = gtk_invisible_new_for_screen (gdk_display_get_screen (display, screen));
	/* We need this for gdk_x11_get_server_time() */
	gtk_widget_add_events (selection_widget, GDK_PROPERTY_CHANGE_MASK);

	if (gtk_selection_owner_set_for_display (display,
						 selection_widget,
						 selection_atom,
						 gdk_x11_get_server_time (selection_widget->window))) {
		
		g_signal_connect (selection_widget, "selection_get",
				  G_CALLBACK (selection_get_cb), NULL);
		return selection_widget;
	}

	gtk_widget_destroy (selection_widget);
	
	return NULL;
}

static void
desktop_unrealize_cb (GtkWidget        *widget,
		      GtkWidget        *selection_widget)
{
	gtk_widget_destroy (selection_widget);
}

static gboolean
selection_clear_event_cb (GtkWidget	        *widget,
			  GdkEventSelection     *event,
			  NautilusDesktopWindow *window)
{
	gtk_widget_destroy (GTK_WIDGET (window));
	
	nautilus_application_desktop_windows =
		g_list_remove (nautilus_application_desktop_windows, window);

	return TRUE;
}

static void
nautilus_application_create_desktop_windows (NautilusApplication *application)
{
	static gboolean create_in_progress = FALSE;
	GdkDisplay *display;
	NautilusDesktopWindow *window;
	GtkWidget *selection_widget;
	int screens, i;

	g_return_if_fail (nautilus_application_desktop_windows == NULL);
	g_return_if_fail (NAUTILUS_IS_APPLICATION (application));

	if (create_in_progress) {
		return;
	}

	create_in_progress = TRUE;

	display = gdk_display_get_default ();
	screens = gdk_display_get_n_screens (display);

	for (i = 0; i < screens; i++) {
		selection_widget = get_desktop_manager_selection (display, i);
		if (selection_widget != NULL) {
			window = nautilus_desktop_window_new (application,
							      gdk_display_get_screen (display, i));
			
			g_signal_connect (selection_widget, "selection_clear_event",
					  G_CALLBACK (selection_clear_event_cb), window);
			
			g_signal_connect (window, "unrealize",
					  G_CALLBACK (desktop_unrealize_cb), selection_widget);
			
			/* We realize it immediately so that the NAUTILUS_DESKTOP_WINDOW_ID
			   property is set so gnome-settings-daemon doesn't try to set the
			   background. And we do a gdk_flush() to be sure X gets it. */
			gtk_widget_realize (GTK_WIDGET (window));
			gdk_flush ();

			
			nautilus_application_desktop_windows =
				g_list_prepend (nautilus_application_desktop_windows, window);
		}
	}

	create_in_progress = FALSE;
}

void
nautilus_application_open_desktop (NautilusApplication *application)
{
	if (nautilus_application_desktop_windows == NULL) {
		nautilus_application_create_desktop_windows (application);
		
		/* Make sure we update the session when the desktop is created */
		update_session (gnome_master_client ());
	}
}

void
nautilus_application_close_desktop (void)
{
	if (nautilus_application_desktop_windows != NULL) {
		g_list_foreach (nautilus_application_desktop_windows,
				(GFunc) gtk_widget_destroy, NULL);
		g_list_free (nautilus_application_desktop_windows);
		nautilus_application_desktop_windows = NULL;

		/* Make sure we update the session when the desktop goes away */
		update_session (gnome_master_client ());
	}
}

void
nautilus_application_close_all_navigation_windows (void)
{
	GList *list_copy;
	GList *l;
	
	list_copy = g_list_copy (nautilus_application_window_list);
	for (l = list_copy; l != NULL; l = l->next) {
		NautilusWindow *window;
		
		window = NAUTILUS_WINDOW (l->data);
		
		if (NAUTILUS_IS_NAVIGATION_WINDOW (window)) {
			nautilus_window_close (window);
		}
	}
	g_list_free (list_copy);
}

static NautilusSpatialWindow *
nautilus_application_get_existing_spatial_window (const char *location)
{
	GList *l;
	
	for (l = nautilus_application_get_spatial_window_list ();
	     l != NULL; l = l->next) {
		char *window_location;
		
		window_location = nautilus_window_get_location (NAUTILUS_WINDOW (l->data));
		if (window_location != NULL &&
		    strcmp (location, window_location) == 0) {
			g_free (window_location);
			return NAUTILUS_SPATIAL_WINDOW (l->data);
		}
		g_free (window_location);
	}
	return NULL;
}

static NautilusSpatialWindow *
find_parent_spatial_window (NautilusSpatialWindow *window)
{
	NautilusFile *file;
	NautilusFile *parent_file;
	char *location;
	char *desktop_directory;

	location = nautilus_window_get_location (NAUTILUS_WINDOW (window));
	if (location == NULL) {
		return NULL;
	}
	file = nautilus_file_get (location);
	g_free (location);

	if (!file) {
		return NULL;
	}
	
	desktop_directory = nautilus_get_desktop_directory_uri ();	
	
	parent_file = nautilus_file_get_parent (file);
	nautilus_file_unref (file);
	while (parent_file) {
		NautilusSpatialWindow *parent_window;
		
		location = nautilus_file_get_uri (parent_file);

		/* Stop at the desktop directory, as this is the
		 * conceptual root of the spatial windows */
		if (!strcmp (location, desktop_directory)) {
			g_free (location);
			g_free (desktop_directory);
			nautilus_file_unref (parent_file);
			return NULL;
		}

		parent_window = nautilus_application_get_existing_spatial_window (location);
		g_free (location);
		
		if (parent_window) {
			nautilus_file_unref (parent_file);
			return parent_window;
		}
		file = parent_file;
		parent_file = nautilus_file_get_parent (file);
		nautilus_file_unref (file);
	}
	g_free (desktop_directory);

	return NULL;
}

void
nautilus_application_close_parent_windows (NautilusSpatialWindow *window)
{
	NautilusSpatialWindow *parent_window;
	NautilusSpatialWindow *new_parent_window;

	g_return_if_fail (NAUTILUS_IS_SPATIAL_WINDOW (window));

	parent_window = find_parent_spatial_window (window);
	
	while (parent_window) {
		
		new_parent_window = find_parent_spatial_window (parent_window);
		nautilus_window_close (NAUTILUS_WINDOW (parent_window));
		parent_window = new_parent_window;
	}
}

void
nautilus_application_close_all_spatial_windows (void)
{
	GList *list_copy;
	GList *l;
	
	list_copy = g_list_copy (nautilus_application_spatial_window_list);
	for (l = list_copy; l != NULL; l = l->next) {
		NautilusWindow *window;
		
		window = NAUTILUS_WINDOW (l->data);
		
		if (NAUTILUS_IS_SPATIAL_WINDOW (window)) {
			nautilus_window_close (window);
		}
	}
	g_list_free (list_copy);
}

static void
nautilus_application_destroyed_window (GtkObject *object, NautilusApplication *application)
{
	nautilus_application_window_list = g_list_remove (nautilus_application_window_list, object);
}

static gboolean
nautilus_window_delete_event_callback (GtkWidget *widget,
				       GdkEvent *event,
				       gpointer user_data)
{
	NautilusWindow *window;

	window = NAUTILUS_WINDOW (widget);
	nautilus_window_close (window);

	return TRUE;
}				       


static NautilusWindow *
create_window (NautilusApplication *application,
	       GType window_type,
	       const char *startup_id,
	       GdkScreen *screen)
{
	NautilusWindow *window;
	
	g_return_val_if_fail (NAUTILUS_IS_APPLICATION (application), NULL);
	
	window = NAUTILUS_WINDOW (gtk_widget_new (window_type,
						  "app", application,
						  "screen", screen,
						  NULL));
	/* Must be called after construction finished */
	nautilus_window_constructed (window);

	g_signal_connect_data (window, "delete_event",
			       G_CALLBACK (nautilus_window_delete_event_callback), NULL, NULL,
			       G_CONNECT_AFTER);

	g_signal_connect_object (window, "destroy",
				 G_CALLBACK (nautilus_application_destroyed_window), application, 0);

	nautilus_application_window_list = g_list_prepend (nautilus_application_window_list, window);

	/* Do not yet show the window. It will be shown later on if it can
	 * successfully display its initial URI. Otherwise it will be destroyed
	 * without ever having seen the light of day.
	 */

	return window;
}

static void
spatial_window_destroyed_callback (void *user_data, GObject *window)
{
	nautilus_application_spatial_window_list = g_list_remove (nautilus_application_spatial_window_list, window);
		
}

NautilusWindow *
nautilus_application_present_spatial_window (NautilusApplication *application,
					     NautilusWindow      *requesting_window,
					     const char          *startup_id,
					     const char          *location,
					     GdkScreen           *screen)
{
	return nautilus_application_present_spatial_window_with_selection (application,
									   requesting_window,
									   startup_id,
									   location,
									   NULL,
									   screen);
}

#ifdef HAVE_STARTUP_NOTIFICATION

static void
sn_error_trap_push (SnDisplay *display,
                    Display   *xdisplay)
{
	gdk_error_trap_push ();
}

static void
sn_error_trap_pop (SnDisplay *display,
                   Display   *xdisplay)
{
	gdk_error_trap_pop ();
}

static void
end_startup_notification (GtkWidget  *widget, 
			  const char *startup_id)
{
	SnDisplay *sn_display;
	SnLauncheeContext *context;
	GdkDisplay *display;
	GdkScreen  *screen;

	if (startup_id == NULL) {
		return;
	}
  
	if (!GTK_WIDGET_REALIZED (widget)) {
		gtk_widget_realize (widget);
	}
  
	context = NULL;
	sn_display = NULL;

	/* Set up window for launch notification */
	/* FIXME In principle all transient children of this
	 * window should get the same startup_id
	 */

	screen = gtk_widget_get_screen (widget);
	display = gdk_screen_get_display (screen);
      
	sn_display = sn_display_new (gdk_x11_display_get_xdisplay (display),
				     sn_error_trap_push,
				     sn_error_trap_pop);
      
	context = sn_launchee_context_new (sn_display,
					   gdk_screen_get_number (screen),
					   startup_id);

	/* Handle the setup for the window if the startup_id is valid;
	 * I don't think it can hurt to do this even if it was
	 * invalid, but why do the extra work...
	 */
	if (strncmp (sn_launchee_context_get_startup_id (context), "_TIME", 5) != 0) {
		sn_launchee_context_setup_window (context,
						  GDK_WINDOW_XWINDOW (widget->window));
	}

	/* Now, set the _NET_WM_USER_TIME for the new window to the timestamp
	 * that caused the window to be launched.
	 */
	if (sn_launchee_context_get_id_has_timestamp (context)) {
		gulong startup_id_timestamp;
		startup_id_timestamp = sn_launchee_context_get_timestamp (context);
		gdk_x11_window_set_user_time (widget->window, startup_id_timestamp);
	}
  
	sn_launchee_context_complete (context);
	sn_launchee_context_unref (context);
	sn_display_unref (sn_display);
}

#endif

NautilusWindow *
nautilus_application_present_spatial_window_with_selection (NautilusApplication *application,
							    NautilusWindow      *requesting_window,
							    const char          *startup_id,
							    const char          *location,
							    GList		*new_selection,
							    GdkScreen           *screen)
{
	NautilusWindow *window;
	GList *l;

	g_return_val_if_fail (NAUTILUS_IS_APPLICATION (application), NULL);

	for (l = nautilus_application_get_spatial_window_list ();
	     l != NULL; l = l->next) {
		NautilusWindow *existing_window;
		char *existing_location;
               	     
		existing_window = NAUTILUS_WINDOW (l->data);
		existing_location = existing_window->details->pending_location;
		
		if (existing_location == NULL) {
			existing_location = existing_window->details->location;
		}

		if (gnome_vfs_uris_match (existing_location, location)) {
#ifdef HAVE_STARTUP_NOTIFICATION
			end_startup_notification (GTK_WIDGET (existing_window),
						  startup_id);
#endif

			gtk_window_present (GTK_WINDOW (existing_window));
			if (new_selection &&
			    existing_window->content_view != NULL) {
				nautilus_view_set_selection (existing_window->content_view, new_selection);
			}

			nautilus_debug_log (FALSE, NAUTILUS_DEBUG_LOG_DOMAIN_USER,
					    "present EXISTING spatial window=%p: %s",
					    existing_window, location);
			return existing_window;
		}
	}

	window = create_window (application, NAUTILUS_TYPE_SPATIAL_WINDOW, startup_id, screen);
#ifdef HAVE_STARTUP_NOTIFICATION
	end_startup_notification (GTK_WIDGET (window),
				  startup_id);
#endif
	if (requesting_window) {
		/* Center the window over the requesting window by default */
		int orig_x, orig_y, orig_width, orig_height;
		int new_x, new_y, new_width, new_height;
		
		gtk_window_get_position (GTK_WINDOW (requesting_window), 
					 &orig_x, &orig_y);
		gtk_window_get_size (GTK_WINDOW (requesting_window), 
				     &orig_width, &orig_height);
		gtk_window_get_default_size (GTK_WINDOW (window),
					     &new_width, &new_height);
		
		new_x = orig_x + (orig_width - new_width) / 2;
		new_y = orig_y + (orig_height - new_height) / 2;
		
		if (orig_width - new_width < 10) {
			new_x += 10;
			new_y += 10;
		}

		gtk_window_move (GTK_WINDOW (window), new_x, new_y);
	}

	nautilus_application_spatial_window_list = g_list_prepend (nautilus_application_spatial_window_list, window);
	g_object_weak_ref (G_OBJECT (window), 
			   spatial_window_destroyed_callback, NULL);
	
	nautilus_window_go_to_with_selection (window, location, new_selection);

	nautilus_debug_log (FALSE, NAUTILUS_DEBUG_LOG_DOMAIN_USER,
			    "present NEW spatial window=%p: %s",
			    window, location);
	
	return window;
}

static gboolean
another_navigation_window_already_showing (NautilusWindow *the_window)
{
	GList *list, *item;
	
	list = nautilus_application_get_window_list ();
	for (item = list; item != NULL; item = item->next) {
		if (item->data != the_window &&
		    NAUTILUS_IS_NAVIGATION_WINDOW (item->data)) {
			return TRUE;
		}
	}
	
	return FALSE;
}

NautilusWindow *
nautilus_application_create_navigation_window (NautilusApplication *application,
					       const char          *startup_id,
					       GdkScreen           *screen)
{
	NautilusWindow *window;
	char *geometry_string;
	gboolean maximized;

	g_return_val_if_fail (NAUTILUS_IS_APPLICATION (application), NULL);
	
	window = create_window (application, NAUTILUS_TYPE_NAVIGATION_WINDOW, startup_id, screen);
#ifdef HAVE_STARTUP_NOTIFICATION
	end_startup_notification (GTK_WIDGET (window),
				  startup_id);
#endif

	maximized = eel_preferences_get_boolean
			(NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_MAXIMIZED);
	if (maximized) {
		gtk_window_maximize (GTK_WINDOW (window));
	} else {
		gtk_window_unmaximize (GTK_WINDOW (window));
	}

	geometry_string = eel_preferences_get
			(NAUTILUS_PREFERENCES_NAVIGATION_WINDOW_SAVED_GEOMETRY);
	if (geometry_string != NULL &&
	    geometry_string[0] != 0) {
		/* Ignore saved window position if a window with the same
		 * location is already showing. That way the two windows
		 * wont appear at the exact same location on the screen.
		 */
		eel_gtk_window_set_initial_geometry_from_string 
			(GTK_WINDOW (window), 
			 geometry_string,
			 NAUTILUS_WINDOW_MIN_WIDTH, 
			 NAUTILUS_WINDOW_MIN_HEIGHT,
			 another_navigation_window_already_showing (window));
	}
	g_free (geometry_string);

	nautilus_debug_log (FALSE, NAUTILUS_DEBUG_LOG_DOMAIN_USER,
			    "create new navigation window=%p",
			    window);

	return window;
}

/* callback for changing the directory the desktop points to */
static void
desktop_location_changed_callback (gpointer user_data)
{
	if (nautilus_application_desktop_windows != NULL) {
		g_list_foreach (nautilus_application_desktop_windows,
				(GFunc) nautilus_desktop_window_update_directory, NULL);
	}
}

/* callback for showing or hiding the desktop based on the user's preference */
static void
desktop_changed_callback (gpointer user_data)
{
	NautilusApplication *application;
	
	application = NAUTILUS_APPLICATION (user_data);
	if ( eel_preferences_get_boolean (NAUTILUS_PREFERENCES_SHOW_DESKTOP)) {
		nautilus_application_open_desktop (application);
	} else {
		nautilus_application_close_desktop ();
	}
}

static gboolean
window_can_be_closed (NautilusWindow *window)
{
	if (!NAUTILUS_IS_DESKTOP_WINDOW (window)) {
		return TRUE;
	}
	
	return FALSE;
}

static void
volume_mounted_callback (GnomeVFSVolumeMonitor *monitor,
			 GnomeVFSVolume *volume,
			 NautilusApplication *application)
{
	char *activation_uri;
	NautilusDirectory *directory;
		
	activation_uri = gnome_vfs_volume_get_activation_uri (volume);
	directory = nautilus_directory_get_existing (activation_uri);
	g_free (activation_uri);
	if (directory != NULL) {
		nautilus_directory_force_reload (directory);
		nautilus_directory_unref (directory);
	}
}

/* Called whenever a volume is unmounted. Check and see if there are any windows open
 * displaying contents on the volume. If there are, close them.
 * It would also be cool to save open window and position info.
 *
 * This is also called on pre_unmount.
 */
static void
volume_unmounted_callback (GnomeVFSVolumeMonitor *monitor,
			   GnomeVFSVolume *volume,
			   NautilusApplication *application)
{
	GList *window_list, *node, *close_list;
	NautilusWindow *window;
	char *uri, *activation_uri, *path;
	GnomeVFSVolumeMonitor *volume_monitor;
	GnomeVFSVolume *window_volume;
	
	close_list = NULL;
	
	/* Check and see if any of the open windows are displaying contents from the unmounted volume */
	window_list = nautilus_application_get_window_list ();

	volume_monitor = gnome_vfs_get_volume_monitor ();

	activation_uri = gnome_vfs_volume_get_activation_uri (volume);
	/* Construct a list of windows to be closed. Do not add the non-closable windows to the list. */
	for (node = window_list; node != NULL; node = node->next) {
		window = NAUTILUS_WINDOW (node->data);
		if (window != NULL && window_can_be_closed (window)) {
			uri = nautilus_window_get_location (window);
			if (eel_str_has_prefix (uri, activation_uri)) {
				close_list = g_list_prepend (close_list, window);
			} else {
				path = gnome_vfs_get_local_path_from_uri (uri);
				if (path != NULL) {
					window_volume = gnome_vfs_volume_monitor_get_volume_for_path (volume_monitor,
												      path);
					if (window_volume != NULL && window_volume == volume) {
						close_list = g_list_prepend (close_list, window);
					}
					gnome_vfs_volume_unref (window_volume);
					g_free (path);
				}
				
			}
			g_free (uri);
		}
	}
	g_free (activation_uri);
		
	/* Handle the windows in the close list. */
	for (node = close_list; node != NULL; node = node->next) {
		window = NAUTILUS_WINDOW (node->data);
		if (NAUTILUS_IS_SPATIAL_WINDOW (window)) {
			nautilus_window_close (window);
		} else {
			nautilus_window_go_home (window);
		}
	}
		
	g_list_free (close_list);
}


static void
removed_from_session (GnomeClient *client, gpointer data)
{
	nautilus_main_event_loop_quit (FALSE);
}

static char *
save_session_to_file (void)
{
	xmlDocPtr doc;
	xmlNodePtr root_node, history_node;
	GList *l;
	char *dir, *filename;
	unsigned n_processed;
	int fd;
	xmlSaveCtxtPtr ctx;

	doc = xmlNewDoc ("1.0");

	root_node = xmlNewNode (NULL, "session");
	xmlDocSetRootElement (doc, root_node);

	history_node = xmlNewChild (root_node, NULL, "history", NULL);

	n_processed = 0;
	for (l = nautilus_get_history_list (); l != NULL; l = l->next) {
		NautilusBookmark *bookmark;
		xmlNodePtr bookmark_node;
		char *tmp;

		bookmark = l->data;

		bookmark_node = xmlNewChild (history_node, NULL, "bookmark", NULL);

		tmp = nautilus_bookmark_get_name (bookmark);
		xmlNewProp (bookmark_node, "name", tmp);
		g_free (tmp);

		tmp = nautilus_bookmark_get_icon (bookmark);
		xmlNewProp (bookmark_node, "icon", tmp);
		g_free (tmp);

		tmp = nautilus_bookmark_get_uri (bookmark);
		xmlNewProp (bookmark_node, "uri", tmp);
		g_free (tmp);

		if (nautilus_bookmark_get_has_custom_name (bookmark)) {
			xmlNewProp (bookmark_node, "has_custom_name", "TRUE");
		}

		if (++n_processed > 50) { /* prevent history list from growing arbitrarily large. */
			break;
		}
	}

	for (l = nautilus_application_window_list; l != NULL; l = l->next) {
		xmlNodePtr win_node;
		NautilusWindow *window;
		char *tmp;

		window = l->data;

		win_node = xmlNewChild (root_node, NULL, "window", NULL);

		xmlNewProp (win_node, "type", NAUTILUS_IS_NAVIGATION_WINDOW (window) ? "navigation" : "spatial");

		if (NAUTILUS_IS_NAVIGATION_WINDOW (window)) { /* spatial windows store their state as file metadata */
			tmp = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));
			xmlNewProp (win_node, "geometry", tmp);
			g_free (tmp);

			if (GTK_WIDGET (window)->window &&
			    gdk_window_get_state (GTK_WIDGET (window)->window) & GDK_WINDOW_STATE_MAXIMIZED) {
				xmlNewProp (win_node, "maximized", "TRUE");
			}
		}

		tmp = nautilus_window_get_location (window);
		xmlNewProp (win_node, "location", tmp);
		g_free (tmp);
	}

	dir = nautilus_get_user_directory ();
	filename = g_build_filename (dir, "saved-session-XXXXXX", NULL);
	g_free (dir);

	fd = g_mkstemp (filename);
	if (fd < 0) {
		g_message ("failed to open session file %s", filename);
		g_free (filename);
		filename = NULL;
		goto out;
	}

	xmlIndentTreeOutput = 1;
	ctx = xmlSaveToFd (fd, NULL, XML_SAVE_FORMAT);
	if (xmlSaveDoc (ctx, doc) < 0 ||
	    xmlSaveFlush (ctx) < 0) {
		g_message ("failed to save session to %s", filename);
		g_free (filename);
		filename = NULL;
	}
	
	xmlSaveClose(ctx);
	close (fd);	

 out:
	xmlFreeDoc (doc);

	return filename;
}

void
nautilus_application_load_session (NautilusApplication *application,
				   const char *filename)
{
	xmlDocPtr doc;
	gboolean bail;

	g_assert (filename != NULL);

	bail = TRUE;

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		xmlNodePtr root_node;

		doc = xmlReadFile (filename, NULL, 0);
		if (doc != NULL && (root_node = xmlDocGetRootElement (doc)) != NULL) {
			xmlNodePtr node;

			bail = FALSE;

			for (node = root_node->children; node != NULL; node = node->next) {

				if (!strcmp (node->name, "text")) {
					continue;
				} else if (!strcmp (node->name, "history")) {
					xmlNodePtr bookmark_node;
					gboolean emit_change;

					emit_change = FALSE;

					for (bookmark_node = node->children; bookmark_node != NULL; bookmark_node = bookmark_node->next) {
						if (!strcmp (bookmark_node->name, "text")) {
							continue;
						} else if (!strcmp (bookmark_node->name, "bookmark")) {
							xmlChar *name, *icon, *uri;
							gboolean has_custom_name;

							uri = xmlGetProp (bookmark_node, "uri");
							name = xmlGetProp (bookmark_node, "name");
							has_custom_name = xmlHasProp (bookmark_node, "has_custom_name") ? TRUE : FALSE;
							icon = xmlGetProp (bookmark_node, "icon");

							emit_change |= nautilus_add_to_history_list_no_notify (uri, name, has_custom_name, icon);

							xmlFree (name);
							xmlFree (uri);
							xmlFree (icon);
						} else {
							g_message ("unexpected bookmark node %s while parsing %s", bookmark_node->name, filename);
							bail = TRUE;
							continue;
						}
					}

					if (emit_change) {
						nautilus_send_history_list_changed ();
					}
				} else if (!strcmp (node->name, "window")) {
					NautilusWindow *window;
					xmlChar *type, *location;

					type = xmlGetProp (node, "type");
					if (type == NULL) {
						g_message ("empty type node while parsing %s", filename);
						bail = TRUE;
						continue;
					}

					location = xmlGetProp (node, "location");
					if (location == NULL) {
						g_message ("empty location node while parsing %s", filename);
						bail = TRUE;
						xmlFree (type);
						continue;
					}

					if (!strcmp (type, "navigation")) {
						xmlChar *geometry;

						window = nautilus_application_create_navigation_window (application, NULL, gdk_screen_get_default ());

						geometry = xmlGetProp (node, "geometry");
						if (geometry != NULL) {
							eel_gtk_window_set_initial_geometry_from_string 
								(GTK_WINDOW (window), 
								 geometry,
								 NAUTILUS_WINDOW_MIN_WIDTH, 
								 NAUTILUS_WINDOW_MIN_HEIGHT,
								 FALSE);
						}
						xmlFree (geometry);

						if (xmlHasProp (node, "maximized")) {
							gtk_window_maximize (GTK_WINDOW (window));
						} else {
							gtk_window_unmaximize (GTK_WINDOW (window));
						}

						nautilus_window_open_location (window, location, FALSE);
					} else if (!strcmp (type, "spatial")) {
						window = nautilus_application_present_spatial_window (application, NULL, NULL, location, gdk_screen_get_default ());
					} else {
						g_message ("unknown window type \"%s\" while parsing %s", type, filename);
						bail = TRUE;
					}

					xmlFree (type);
					xmlFree (location);
				} else {
					g_message ("unexpected node %s while parsing %s", node->name, filename);
					bail = TRUE;
					continue;
				}
			}
		}

		if (doc != NULL) {
			xmlFreeDoc (doc);
		}
	}

	if (bail) {
		g_message ("failed to load session from %s", filename);
	} else {
		char *uri;
		GnomeVFSFileInfo *info;
		/* only remove file if it is regular, user-owned and the user has write access. */

		uri = gnome_vfs_get_uri_from_local_path (filename);
		info = gnome_vfs_file_info_new ();
		if (uri != NULL &&
		    gnome_vfs_get_file_info (uri, info, GNOME_VFS_FILE_INFO_DEFAULT) == GNOME_VFS_OK &&
		    (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_TYPE) &&
		    (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_PERMISSIONS) &&
		    (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_IDS) &&
		    info->type == GNOME_VFS_FILE_TYPE_REGULAR &&
		    (info->permissions & (GNOME_VFS_PERM_USER_WRITE |
					  GNOME_VFS_PERM_USER_WRITE)) &&
		    info->uid == geteuid ()) {
			g_remove (filename);
		}
		gnome_vfs_file_info_unref (info);
		g_free (uri);
	}
}

static gint
save_session (GnomeClient *client, gint phase, GnomeSaveStyle save_style, gint shutdown,
	      GnomeInteractStyle interact_style, gint fast, gpointer data)
{
	char *argv[3] = { NULL };

	argv[0] = "nautilus";

	argv[2] = save_session_to_file ();
	if (argv[2] != NULL) {
		argv[1] = "--load-session";
	}

	gnome_client_set_restart_command (client, 
					  G_N_ELEMENTS (argv), 
					  argv);

	if (argv[2] != NULL) {
		g_free (argv[2]);
	}

	return TRUE;
}

static void
set_session_restart (GnomeClient *client, gboolean restart)
{
	gnome_client_set_priority (client, 40);

	if (restart && g_getenv ("NAUTILUS_DEBUG") == NULL) {
		/* Don't respawn in debug mode */
		gnome_client_set_restart_style (client, GNOME_RESTART_IMMEDIATELY);
	} else {
		gnome_client_set_restart_style (client, GNOME_RESTART_IF_RUNNING);
	}
}

static void
update_session (gpointer callback_data)
{
	set_session_restart (callback_data,
			     /* Only ever add ourselves to the session
			      * if we have a desktop window. Prevents the
			      * session thrashing that's seen otherwise
			      */
			     nautilus_application_desktop_windows != NULL);
}

static void
init_session (void)
{
	GnomeClient *client;

	client = gnome_master_client ();

	g_signal_connect (client, "save_yourself",
			  G_CALLBACK (save_session), NULL);
	
	g_signal_connect (client, "die",
			  G_CALLBACK (removed_from_session), NULL);
	
	update_session (client);
}

#ifdef UGLY_HACK_TO_DETECT_KDE

static gboolean
get_self_typed_prop (Window      xwindow,
                     Atom        atom,
                     gulong     *val)
{  
	Atom type;
	int format;
	gulong nitems;
	gulong bytes_after;
	gulong *num;
	int err;
  
	gdk_error_trap_push ();
	type = None;
	XGetWindowProperty (gdk_display,
			    xwindow,
			    atom,
			    0, G_MAXLONG,
			    False, atom, &type, &format, &nitems,
			    &bytes_after, (guchar **)&num);  

	err = gdk_error_trap_pop ();
	if (err != Success) {
		return FALSE;
	}
  
	if (type != atom) {
		return FALSE;
	}

	if (val)
		*val = *num;
  
	XFree (num);

	return TRUE;
}

static gboolean
has_wm_state (Window xwindow)
{
	return get_self_typed_prop (xwindow,
				    XInternAtom (gdk_display, "WM_STATE", False),
				    NULL);
}

static gboolean
look_for_kdesktop_recursive (Window xwindow)
{
  
	Window ignored1, ignored2;
	Window *children;
	unsigned int n_children;
	unsigned int i;
	gboolean retval;
  
	/* If WM_STATE is set, this is a managed client, so look
	 * for the class hint and end recursion. Otherwise,
	 * this is probably just a WM frame, so keep recursing.
	 */
	if (has_wm_state (xwindow)) {      
		XClassHint ch;
      
		gdk_error_trap_push ();
		ch.res_name = NULL;
		ch.res_class = NULL;
      
		XGetClassHint (gdk_display, xwindow, &ch);
      
		gdk_error_trap_pop ();
      
		if (ch.res_name)
			XFree (ch.res_name);
      
		if (ch.res_class) {
			if (strcmp (ch.res_class, "kdesktop") == 0) {
				XFree (ch.res_class);
				return TRUE;
			}
			else
				XFree (ch.res_class);
		}

		return FALSE;
	}
  
	retval = FALSE;
  
	gdk_error_trap_push ();
  
	XQueryTree (gdk_display,
		    xwindow,
		    &ignored1, &ignored2, &children, &n_children);

	if (gdk_error_trap_pop ()) {
		return FALSE;
	}

	i = 0;
	while (i < n_children) {
		if (look_for_kdesktop_recursive (children[i])) {
			retval = TRUE;
			break;
		}
      
		++i;
	}
  
	if (children)
		XFree (children);

	return retval;
}
#endif /* UGLY_HACK_TO_DETECT_KDE */

static gboolean
is_kdesktop_present (void)
{
#ifdef UGLY_HACK_TO_DETECT_KDE
	/* FIXME this is a pretty lame hack, should be replaced
	 * eventually with e.g. a requirement that desktop managers
	 * support a manager selection, ICCCM sec 2.8
	 */
	return look_for_kdesktop_recursive (GDK_ROOT_WINDOW ());
#else
	return FALSE;
#endif
}

static void
nautilus_application_class_init (NautilusApplicationClass *class)
{
	BONOBO_OBJECT_CLASS (class)->destroy = nautilus_application_destroy;
	BONOBO_GENERIC_FACTORY_CLASS (class)->epv.createObject = create_object;
}
