/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nautilus-monitor.c: file and directory change monitoring for nautilus
 
   Copyright (C) 2000, 2001 Eazel, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
  
   Authors: Seth Nickell <seth@eazel.com>
            Darin Adler <darin@bentspoon.com>
	    Alex Graveley <alex@ximian.com>
*/

#include <config.h>
#include "nautilus-monitor.h"
#include "nautilus-file-changes-queue.h"
#include "nautilus-file-utilities.h"

#include <gio/gdirectorymonitor.h>
#include <libgnome/gnome-util.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>

struct NautilusMonitor {
	GDirectoryMonitor *monitor;
};

gboolean
nautilus_monitor_active (void)
{
	static gboolean tried_monitor = FALSE;
	static gboolean monitor_success;
	GDirectoryMonitor *dir_monitor;
	GFile *file;

	if (tried_monitor == FALSE) {	
		file = g_file_new_for_path (g_get_home_dir ());
		dir_monitor = g_file_monitor_directory (file, G_FILE_MONITOR_FLAGS_NONE, NULL);
		g_object_unref (file);
		
		monitor_success = (dir_monitor != NULL);
		if (dir_monitor) {
			g_object_unref (dir_monitor);
		}

		tried_monitor = TRUE;
	}

	return monitor_success;
}

static gboolean call_consume_changes_idle_id = 0;

static gboolean
call_consume_changes_idle_cb (gpointer not_used)
{
	nautilus_file_changes_consume_changes (TRUE);
	call_consume_changes_idle_id = 0;
	return FALSE;
}

static void
dir_changed (GDirectoryMonitor* monitor,
	     GFile *child,
	     GFile *other_file,
	     GFileMonitorEvent event_type,
	     gpointer user_data)
{
	char *uri, *to_uri;
	
	uri = g_file_get_uri (child);
	to_uri = NULL;
	if (other_file) {
		to_uri = g_file_get_uri (other_file);
	}

	switch (event_type) {
	default:
	case G_FILE_MONITOR_EVENT_CHANGED:
		/* ignore */
		break;
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		nautilus_file_changes_queue_file_changed (uri);
		break;
	case G_FILE_MONITOR_EVENT_DELETED:
		nautilus_file_changes_queue_schedule_metadata_remove (uri);
		nautilus_file_changes_queue_file_removed (uri);
		break;
	case G_FILE_MONITOR_EVENT_CREATED:
		nautilus_file_changes_queue_file_added (uri);
		break;
		
	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
		/* TODO: Do something */
		break;
	case G_FILE_MONITOR_EVENT_UNMOUNTED:
		/* TODO: Do something */
		break;
	}

	g_free (uri);
	g_free (to_uri);

	if (call_consume_changes_idle_id == 0) {
		call_consume_changes_idle_id = 
			g_idle_add (call_consume_changes_idle_cb, NULL);
	}
}
 
NautilusMonitor *
nautilus_monitor_directory (GFile *location)
{
	GDirectoryMonitor *dir_monitor;
	NautilusMonitor *ret;

	dir_monitor = g_file_monitor_directory (location, G_FILE_MONITOR_FLAGS_MONITOR_MOUNTS, NULL);

	if (dir_monitor == NULL) {
		return NULL;
	}

	ret = g_new0 (NautilusMonitor, 1);
	ret->monitor = dir_monitor;

	g_signal_connect (ret->monitor, "changed", (GCallback)dir_changed, ret);

	return ret;
}

void 
nautilus_monitor_cancel (NautilusMonitor *monitor)
{
	if (monitor->monitor != NULL) {
		g_signal_handlers_disconnect_by_func (monitor->monitor, dir_changed, monitor);
		g_directory_monitor_cancel (monitor->monitor);
		g_object_unref (monitor->monitor);
	}

	g_free (monitor);
}
