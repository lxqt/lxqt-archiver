/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * LXQt Archiver
 * Copyright (C) 2025 The LXQt team.
 * Some of the following code is derived from Engrampa and File Roller.
 */

/*
 *  Copyright (C) 2001 The Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-command.h"
#include "fr-command-bsdtar.h"

static void fr_command_bsdtar_class_init  (FrCommandBsdtarClass *class);
static void fr_command_bsdtar_init        (FrCommand         *afile);
static void fr_command_bsdtar_finalize    (GObject           *object);

/* Parent Class */

static FrCommandClass *parent_class = NULL;


/* -- listing -- */

static time_t
mktime_from_string (char *month,
		    char *mday,
		    char *year)
{
	static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm     tm = {0, };

	tm.tm_isdst = -1;

	if (month != NULL) {
		int i;
		for (i = 0; i < 12; i++)
			if (strcmp (months[i], month) == 0) {
				tm.tm_mon = i;
				break;
			}
	} else
		tm.tm_mon = 0;

	if (mday != NULL)
		tm.tm_mday = atoi (mday);
	else
		tm.tm_mday = 1;

	if (year != NULL) {
		if (strchr (year, ':') != NULL) {
			char **fields = g_strsplit (year, ":", 2);
			if (g_strv_length (fields) == 2) {
				tm.tm_hour = atoi (fields[0]);
				tm.tm_min = atoi (fields[1]);

				time_t      now;
				struct tm  *now_tm;
				now = time(NULL);
				now_tm = localtime (&now);
				tm.tm_year = now_tm->tm_year;
				if (difftime (now, mktime (&tm)) < 0) /* not in future */
					-- tm.tm_year;
			}
		} else
			tm.tm_year = atoi (year) - 1900;
	} else
		tm.tm_year = 70;

	return mktime (&tm);
}


static void
list__process_line (char     *line,
		    gpointer  data)
{
	FileData    *fdata;
	FrCommand   *comm = FR_COMMAND (data);
	char       **fields;
	const char  *name_field;
	char        *name;
	int          ofs = 0;

	g_return_if_fail (line != NULL);

	fdata = file_data_new ();

	/* Handle char and block device files */
	if ((line[0] == 'c') || (line[0] == 'b')) {
		fields = split_line (line, 9);
		ofs = 1;
		fdata->size = 0;
		/* TODO We should also specify the content type */
	}
	else {
		fields = split_line (line, 8);
		fdata->size = g_ascii_strtoull (fields[4], NULL, 10);
	}
	fdata->modified = mktime_from_string (fields[5+ofs], fields[6+ofs], fields[7+ofs]);
	g_strfreev (fields);

	name_field = get_last_field (line, 9+ofs);

	fields = g_strsplit (name_field, " -> ", 2);

	if (fields[1] == NULL) {
		g_strfreev (fields);
		fields = g_strsplit (name_field, " link to ", 2);
	}

	fdata->dir = line[0] == 'd';

	name = g_strcompress (fields[0]);
	if (*(fields[0]) == '/') {
		fdata->full_path = g_strdup (name);
		fdata->original_path = fdata->full_path;
	}
	else {
		fdata->full_path = g_strconcat ("/", name, NULL);
		fdata->original_path = fdata->full_path + 1;
	}
	if (fdata->dir && (name[strlen (name) - 1] != '/')) {
		char *old_full_path = fdata->full_path;
		fdata->full_path = g_strconcat (old_full_path, "/", NULL);
		g_free (old_full_path);
		fdata->original_path = g_strdup (name);
		fdata->free_original_path = TRUE;
	}
	g_free (name);

	if (fields[1] != NULL)
		fdata->link = g_strcompress (fields[1]);
	g_strfreev (fields);

	if (fdata->dir)
		fdata->name = dir_name_from_path (fdata->full_path);
	else
		fdata->name = g_strdup (file_name_from_path (fdata->full_path));
	fdata->path = remove_level_from_path (fdata->full_path);

	if (*fdata->name == 0)
		file_data_free (fdata);
	else
		fr_command_add_file (comm, fdata);
}


static void
fr_command_bsdtar_list (FrCommand *comm)
{
	fr_process_set_out_line_func (comm->process, list__process_line, comm);

	fr_process_begin_command (comm->process, "bsdtar");
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, "-tv");
	fr_process_end_command (comm->process);
	fr_process_start (comm->process);
}


static void
fr_command_bsdtar_extract (FrCommand  *comm,
		        const char  *from_file,
			GList      *file_list,
			const char *dest_dir,
			gboolean    overwrite,
			gboolean    skip_older,
			gboolean    junk_paths)
{
	GList   *scan;

	fr_process_begin_command (comm->process, "bsdtar");
	if (dest_dir != NULL)
        fr_process_set_working_dir (comm->process, dest_dir);
	fr_process_add_arg (comm->process, "-f");
	fr_process_add_arg (comm->process, comm->filename);
	fr_process_add_arg (comm->process, "-x");
	for (scan = file_list; scan; scan = scan->next) {
		fr_process_add_arg (comm->process, scan->data);
	}

	fr_process_end_command (comm->process);
}


const char *bsdtar_mime_type[] = { "application/x-rpm",
				"application/x-source-rpm",
				"compressed-disk-image", /* a virtual type*/
				"application/vnd.android.package-archive", /* 7z takes priority */
				NULL };


static const char **
fr_command_bsdtar_get_mime_types (FrCommand *comm)
{
	return bsdtar_mime_type;
}


static FrCommandCap
fr_command_bsdtar_get_capabilities (FrCommand  *comm,
			         const char *mime_type,
				 gboolean    check_command)
{
	FrCommandCap capabilities;

	capabilities = FR_COMMAND_CAN_ARCHIVE_MANY_FILES;
	if (is_program_available ("bsdtar", check_command))
		capabilities |= FR_COMMAND_CAN_READ;

	return capabilities;
}


static const char *
fr_command_bsdtar_get_packages (FrCommand  *comm,
			     const char *mime_type)
{
	return PACKAGES ("bsdtar");
}


static void
fr_command_bsdtar_class_init (FrCommandBsdtarClass *class)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
        FrCommandClass *afc;

        parent_class = g_type_class_peek_parent (class);
	afc = (FrCommandClass*) class;

	gobject_class->finalize = fr_command_bsdtar_finalize;

        afc->list             = fr_command_bsdtar_list;
	afc->extract          = fr_command_bsdtar_extract;
	afc->get_mime_types   = fr_command_bsdtar_get_mime_types;
	afc->get_capabilities = fr_command_bsdtar_get_capabilities;
	afc->get_packages     = fr_command_bsdtar_get_packages;
}


static void
fr_command_bsdtar_init (FrCommand *comm)
{
	comm->propAddCanUpdate             = FALSE;
	comm->propAddCanReplace            = FALSE;
	comm->propExtractCanAvoidOverwrite = FALSE;
	comm->propExtractCanSkipOlder      = FALSE;
	comm->propExtractCanJunkPaths      = FALSE;
	comm->propPassword                 = FALSE;
	comm->propTest                     = FALSE;
}


static void
fr_command_bsdtar_finalize (GObject *object)
{
		g_return_if_fail (object != NULL);
		g_return_if_fail (FR_IS_COMMAND_BSDTAR (object));

		/* Chain up */
		if (G_OBJECT_CLASS (parent_class)->finalize)
		G_OBJECT_CLASS (parent_class)->finalize (object);
}


GType
fr_command_bsdtar_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (FrCommandBsdtarClass),
			NULL,
			NULL,
			(GClassInitFunc) fr_command_bsdtar_class_init,
			NULL,
			NULL,
			sizeof (FrCommandBsdtar),
			0,
			(GInstanceInitFunc) fr_command_bsdtar_init
		};

		type = g_type_register_static (FR_TYPE_COMMAND,
					       "FRCommandBsdtar",
					       &type_info,
					       0);
        }

        return type;
}
