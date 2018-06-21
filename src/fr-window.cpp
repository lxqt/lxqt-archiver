/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Engrampa
 *
 *  Copyright (C) 2007 Free Software Foundation, Inc.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include <math.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <QMessageBox>

/*
#include "actions.h"
#include "dlg-batch-add.h"
#include "dlg-delete.h"
#include "dlg-extract.h"
#include "dlg-open-with.h"
#include "dlg-ask-password.h"
#include "dlg-package-installer.h"
#include "dlg-update.h"
#include "eggtreemultidnd.h"
*/

#include "fr-marshal.h"
// #include "fr-list-model.h"
#include "fr-archive.h"
#include "fr-error.h"
// #include "fr-stock.h"
#include "fr-window.h"
#include "file-data.h"
#include "file-utils.h"
#include "glib-utils.h"
#include "fr-init.h"
// #include "gtk-utils.h"
#include "open-file.h"
#include "typedefs.h"
// #include "ui.h"

#ifdef __GNUC__
#define UNUSED_VARIABLE __attribute__ ((unused))
#else
#define UNUSED_VARIABLE
#endif

#define LAST_OUTPUT_DIALOG_NAME "last-output"
#define MAX_HISTORY_LEN 5
#define ACTIVITY_DELAY 100
#define ACTIVITY_PULSE_STEP (0.033)
#define MAX_MESSAGE_LENGTH 50

#define PROGRESS_TIMEOUT_MSECS 5000
#define PROGRESS_DIALOG_DEFAULT_WIDTH 500
#define PROGRESS_BAR_HEIGHT 10
#undef  LOG_PROGRESS

#define HIDE_PROGRESS_TIMEOUT_MSECS 500
#define DEFAULT_NAME_COLUMN_WIDTH 250
#define OTHER_COLUMNS_WIDTH 100
#define RECENT_ITEM_MAX_WIDTH 25

#define DEF_WIN_WIDTH 600
#define DEF_WIN_HEIGHT 480
#define DEF_SIDEBAR_WIDTH 200

#define FILE_LIST_ICON_SIZE GTK_ICON_SIZE_LARGE_TOOLBAR
#define DIR_TREE_ICON_SIZE GTK_ICON_SIZE_MENU

#define BAD_CHARS "/\\*"

static GHashTable*     pixbuf_hash = NULL;
static GHashTable*     tree_pixbuf_hash = NULL;
// static GtkIconTheme*   icon_theme = NULL;
static int             file_list_icon_size = 0;
static int             dir_tree_icon_size = 0;

#define XDS_FILENAME "xds.txt"
#define MAX_XDS_ATOM_VAL_LEN 4096
#define XDS_ATOM   gdk_atom_intern  ("XdndDirectSave0", FALSE)
#define TEXT_ATOM  gdk_atom_intern  ("text/plain", FALSE)
#define OCTET_ATOM gdk_atom_intern  ("application/octet-stream", FALSE)
#define XFR_ATOM   gdk_atom_intern  ("XdndEngrampa0", FALSE)

#define FR_CLIPBOARD (gdk_atom_intern_static_string ("_RNGRAMPA_SPECIAL_CLIPBOARD"))
#define FR_SPECIAL_URI_LIST (gdk_atom_intern_static_string ("application/engrampa-uri-list"))

#if 0
static GtkTargetEntry clipboard_targets[] = {
    { "application/engrampa-uri-list", 0, 1 }
};

static GtkTargetEntry target_table[] = {
    { "XdndEngrampa0", 0, 0 },
    { "text/uri-list", 0, 1 },
};

static GtkTargetEntry folder_tree_targets[] = {
    { "XdndEngrampa0", 0, 0 },
    { "XdndDirectSave0", 0, 2 }
};

#endif

struct FRBatchAction {
    FrBatchActionType type;
    void*             data;
    GFreeFunc         free_func;
};


typedef struct {
    guint      converting : 1;
    char*      temp_dir;
    FrArchive* new_archive;
    char*      password;
    gboolean   encrypt_header;
    guint      volume_size;
    char*      new_file;
} FRConvertData;




struct ExtractData {
    GList*       file_list;
    char*        extract_to_dir;
    char*        base_dir;
    gboolean     skip_older;
    FrOverwrite  overwrite;
    gboolean     junk_paths;
    char*        password;
    gboolean     extract_here;
    gboolean     ask_to_open_destination;
} ;


typedef enum {
    FR_WINDOW_AREA_MENUBAR,
    FR_WINDOW_AREA_TOOLBAR,
    FR_WINDOW_AREA_LOCATIONBAR,
    FR_WINDOW_AREA_CONTENTS,
    FR_WINDOW_AREA_FILTERBAR,
    FR_WINDOW_AREA_STATUSBAR,
} FrWindowArea;


typedef enum {
    DIALOG_RESPONSE_NONE = 1,
    DIALOG_RESPONSE_OPEN_ARCHIVE,
    DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER,
    DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER_AND_QUIT,
    DIALOG_RESPONSE_QUIT
} DialogResponse;


/* -- FrClipboardData -- */


struct FrClipboardData {
    int            refs;
    char*          archive_filename;
    char*          archive_password;
    FRClipboardOp  op;
    char*          base_dir;
    GList*         files;
    char*          tmp_dir;
    char*          current_dir;
};


static FrClipboardData*
fr_clipboard_data_new(void) {
    FrClipboardData* data;

    data = g_new0(FrClipboardData, 1);
    data->refs = 1;

    return data;
}


static FrClipboardData*
fr_clipboard_data_ref(FrClipboardData* clipboard_data) {
    clipboard_data->refs++;
    return clipboard_data;
}


static void
fr_clipboard_data_unref(FrClipboardData* clipboard_data) {
    if(clipboard_data == NULL) {
        return;
    }
    if(--clipboard_data->refs > 0) {
        return;
    }

    g_free(clipboard_data->archive_filename);
    g_free(clipboard_data->archive_password);
    g_free(clipboard_data->base_dir);
    g_free(clipboard_data->tmp_dir);
    g_free(clipboard_data->current_dir);
    g_list_foreach(clipboard_data->files, (GFunc) g_free, NULL);
    g_list_free(clipboard_data->files);
    g_free(clipboard_data);
}


static void
fr_clipboard_data_set_password(FrClipboardData* clipboard_data,
                               const char*      password) {
    if(clipboard_data->archive_password != password) {
        g_free(clipboard_data->archive_password);
    }
    if(password != NULL) {
        clipboard_data->archive_password = g_strdup(password);
    }
}


struct FrWindowPrivateData {
    /*
    GtkWidget*         layout;
    GtkWidget*         contents;
    GtkWidget*         list_view;
    GtkListStore*      list_store;
    GtkWidget*         tree_view;
    GtkTreeStore*      tree_store;
    GtkWidget*         toolbar;
    GtkWidget*         statusbar;
    GtkWidget*         progress_bar;
    GtkWidget*         location_bar;
    GtkWidget*         location_entry;
    GtkWidget*         location_label;
    GtkWidget*         filter_bar;
    GtkWidget*         filter_entry;
    GtkWidget*         paned;
    GtkWidget*         sidepane;
    GtkTreePath*       tree_hover_path;
    GtkTreePath*       list_hover_path;
    GtkTreeViewColumn* filename_column;
    */

    gboolean         filter_mode;
    gint             current_view_length;

    guint            help_message_cid;
    guint            list_info_cid;
    guint            progress_cid;

    /*
    GtkWidget*       up_arrows[5];
    GtkWidget*       down_arrows[5];
    */

    FrAction         action;
    gboolean         archive_present;
    gboolean         archive_new;        /* A new archive has been created
					      * but it doesn't contain any
					      * file yet.  The real file will
					      * be created only when the user
					      * adds some file to the
					      * archive.*/

    char*            archive_uri;
    char*            open_default_dir;    /* default directory to be used
					       * in the Open dialog. */
    char*            add_default_dir;     /* default directory to be used
					       * in the Add dialog. */
    char*            extract_default_dir; /* default directory to be used
					       * in the Extract dialog. */
    gboolean         freeze_default_dir;
    gboolean         asked_for_password;
    gboolean         ask_to_open_destination_after_extraction;
    gboolean         destroy_with_error_dialog;

    FRBatchAction    current_batch_action;

    gboolean         give_focus_to_the_list;
    gboolean         single_click;

    // GtkTreePath*     path_clicked;
    FrWindowSortMethod sort_method;

    //GtkSortType      sort_type;

    char*            last_location;

    gboolean         view_folders;
    FrWindowListMode list_mode;
    FrWindowListMode last_list_mode;
    GList*           history;
    GList*           history_current;
    char*            password;
    char*            password_for_paste;
    gboolean         encrypt_header;
    FrCompression    compression;
    guint            volume_size;

    guint            activity_timeout_handle;   /* activity timeout
						     * handle. */
    gint             activity_ref;              /* when > 0 some activity
						     * is present. */

    guint            update_timeout_handle;     /* update file list
						     * timeout handle. */

    FRConvertData    convert_data;

    gboolean         stoppable;
    gboolean         closing;

    FrClipboardData* clipboard_data;
    FrClipboardData* copy_data;

    FrArchive*       copy_from_archive;

    /*
    GtkActionGroup*  actions;

    GtkWidget*        file_popup_menu;
    GtkWidget*        folder_popup_menu;
    GtkWidget*        sidebar_folder_popup_menu;
    GtkWidget*        mitem_recents_menu;
    */

    /* dragged files data */

    char*             drag_destination_folder;
    char*             drag_base_dir;
    GError*           drag_error;
    GList*            drag_file_list;        /* the list of files we are
					 	  * dragging*/

    /* progress dialog data */

    /*
    GtkWidget*        progress_dialog;
    GtkWidget*        pd_action;
    GtkWidget*        pd_message;
    GtkWidget*        pd_progress_bar;
    GtkWidget*        pd_cancel_button;
    GtkWidget*        pd_close_button;
    GtkWidget*        pd_open_archive_button;
    GtkWidget*        pd_open_destination_button;
    GtkWidget*        pd_open_destination_and_quit_button;
    GtkWidget*        pd_quit_button;
    GtkWidget*        pd_icon;
    */

    gboolean          progress_pulse;
    guint             progress_timeout;  /* Timeout to display the progress dialog. */
    guint             hide_progress_timeout;  /* Timeout to hide the progress dialog. */
    char*             pd_last_archive;
    char*             working_archive;
    double            pd_last_fraction;
    char*             pd_last_message;
    gboolean          use_progress_dialog;

    /* update dialog data */

    gpointer          update_dialog;
    GList*            open_files;

    /* batch mode data */

    gboolean          batch_mode;          /* whether we are in a non interactive
					 	* mode. */
    GList*            batch_action_list;   /* FRBatchAction * elements */
    GList*            batch_action;        /* current action. */
    char*             batch_title;

    /* misc */

    GSettings* settings_listing;
    GSettings* settings_ui;
    GSettings* settings_general;
    GSettings* settings_dialogs;
    GSettings* settings_caja;

    gulong            theme_changed_handler_id;
    gboolean          non_interactive;
    char*             extract_here_dir;
    gboolean          extract_interact_use_default_dir;
    gboolean          update_dropped_files;
    gboolean          batch_adding_one_file;

    // GtkWindow*        load_error_parent_window;
    gboolean          showing_error_dialog;
    // GtkWindow*        error_dialog_parent;
};


/* -- FrWindow::free_private_data -- */


void
FrWindow::free_batch_data() {
    GList* scan;

    for(scan = priv->batch_action_list; scan; scan = scan->next) {
        FRBatchAction* adata = (FRBatchAction*)scan->data;

        if((adata->data != NULL) && (adata->free_func != NULL)) {
            (*adata->free_func)(adata->data);
        }
        g_free(adata);
    }

    g_list_free(priv->batch_action_list);
    priv->batch_action_list = NULL;
    priv->batch_action = NULL;

    g_free(priv->batch_title);
    priv->batch_title = NULL;
}


static void
gh_unref_pixbuf(gpointer key,
                gpointer value,
                gpointer user_data) {
    g_object_unref(value);
}


void
FrWindow::clipboard_remove_file_list(
    GList*    file_list) {
    GList* scan1;

    if(priv->copy_data == NULL) {
        return;
    }

    if(file_list == NULL) {
        fr_clipboard_data_unref(priv->copy_data);
        priv->copy_data = NULL;
        return;
    }

    for(scan1 = file_list; scan1; scan1 = scan1->next) {
        const char* name1 = (const char*)scan1->data;
        GList*      scan2;

        for(scan2 = priv->copy_data->files; scan2;) {
            const char* name2 = (const char*)scan2->data;

            if(strcmp(name1, name2) == 0) {
                GList* tmp = scan2->next;
                priv->copy_data->files = g_list_remove_link(priv->copy_data->files, scan2);
                g_free(scan2->data);
                g_list_free(scan2);
                scan2 = tmp;
            }
            else {
                scan2 = scan2->next;
            }
        }
    }

    if(priv->copy_data->files == NULL) {
        fr_clipboard_data_unref(priv->copy_data);
        priv->copy_data = NULL;
    }
}


void
FrWindow::history_clear() {
    if(priv->history != NULL) {
        path_list_free(priv->history);
    }
    priv->history = NULL;
    priv->history_current = NULL;
    g_free(priv->last_location);
    priv->last_location = NULL;
}


void
FrWindow::free_open_files() {
    GList* scan;

    for(scan = priv->open_files; scan; scan = scan->next) {
        OpenFile* file = (OpenFile*)scan->data;

        if(file->monitor != NULL) {
            g_file_monitor_cancel(file->monitor);
        }
        open_file_free(file);
    }
    g_list_free(priv->open_files);
    priv->open_files = NULL;
}


void
FrWindow::convert_data_free(
    gboolean    all) {
    if(all) {
        g_free(priv->convert_data.new_file);
        priv->convert_data.new_file = NULL;
    }

    priv->convert_data.converting = FALSE;

    if(priv->convert_data.temp_dir != NULL) {
        g_free(priv->convert_data.temp_dir);
        priv->convert_data.temp_dir = NULL;
    }

    if(priv->convert_data.new_archive != NULL) {
        g_object_unref(priv->convert_data.new_archive);
        priv->convert_data.new_archive = NULL;
    }

    if(priv->convert_data.password != NULL) {
        g_free(priv->convert_data.password);
        priv->convert_data.password = NULL;
    }
}


void
FrWindow::free_private_data() {
    if(priv->update_timeout_handle != 0) {
        g_source_remove(priv->update_timeout_handle);
        priv->update_timeout_handle = 0;
    }

    while(priv->activity_ref > 0) {
        stop_activity_mode();
    }

    if(priv->progress_timeout != 0) {
        g_source_remove(priv->progress_timeout);
        priv->progress_timeout = 0;
    }

    if(priv->hide_progress_timeout != 0) {
        g_source_remove(priv->hide_progress_timeout);
        priv->hide_progress_timeout = 0;
    }

    history_clear();

    g_free(priv->open_default_dir);
    g_free(priv->add_default_dir);
    g_free(priv->extract_default_dir);
    g_free(priv->archive_uri);

    g_free(priv->password);
    g_free(priv->password_for_paste);

    /*
    g_object_unref(priv->list_store);
    */

    if(priv->clipboard_data != NULL) {
        fr_clipboard_data_unref(priv->clipboard_data);
        priv->clipboard_data = NULL;
    }
    if(priv->copy_data != NULL) {
        fr_clipboard_data_unref(priv->copy_data);
        priv->copy_data = NULL;
    }
    if(priv->copy_from_archive != NULL) {
        g_object_unref(priv->copy_from_archive);
        priv->copy_from_archive = NULL;
    }

    free_open_files();

    convert_data_free(TRUE);

    g_clear_error(&priv->drag_error);
    path_list_free(priv->drag_file_list);
    priv->drag_file_list = NULL;

    /*
        if(priv->file_popup_menu != NULL) {
            gtk_widget_destroy(priv->file_popup_menu);
            priv->file_popup_menu = NULL;
        }

        if(priv->folder_popup_menu != NULL) {
            gtk_widget_destroy(priv->folder_popup_menu);
            priv->folder_popup_menu = NULL;
        }

        if(priv->sidebar_folder_popup_menu != NULL) {
            gtk_widget_destroy(priv->sidebar_folder_popup_menu);
            priv->sidebar_folder_popup_menu = NULL;
        }
    */

    g_free(priv->last_location);

    free_batch_data();
    reset_current_batch_action();

    g_free(priv->pd_last_archive);
    g_free(priv->pd_last_message);
    g_free(priv->extract_here_dir);

    g_settings_set_enum(priv->settings_listing, PREF_LISTING_SORT_METHOD, priv->sort_method);
    // g_settings_set_enum(priv->settings_listing, PREF_LISTING_SORT_TYPE, priv->sort_type);
    g_settings_set_enum(priv->settings_listing, PREF_LISTING_LIST_MODE, priv->last_list_mode);

    _g_object_unref(priv->settings_listing);
    _g_object_unref(priv->settings_ui);
    _g_object_unref(priv->settings_general);
    _g_object_unref(priv->settings_dialogs);

    if(priv->settings_caja) {
        _g_object_unref(priv->settings_caja);
    }
}


FrWindow::~FrWindow() {
    free_open_files();

    if(archive != NULL) {
        g_object_unref(archive);
        archive = NULL;
    }

    if(priv != NULL) {
        free_private_data();
        g_free(priv);
        priv = NULL;
    }
    /*
        if(gtk_application_get_windows(GTK_APPLICATION(g_application_get_default())) == NULL) {
            if(pixbuf_hash != NULL) {
                g_hash_table_foreach(pixbuf_hash,
                                     gh_unref_pixbuf,
                                     NULL);
                g_hash_table_destroy(pixbuf_hash);
                pixbuf_hash = NULL;
            }
            if(tree_pixbuf_hash != NULL) {
                g_hash_table_foreach(tree_pixbuf_hash,
                                     gh_unref_pixbuf,
                                     NULL);
                g_hash_table_destroy(tree_pixbuf_hash);
                tree_pixbuf_hash = NULL;
            }
        }
    */
}

void
FrWindow::close() {
    if(priv->activity_ref > 0) {
        return;
    }

    priv->closing = TRUE;
    /*
        if(gtk_widget_get_realized(GTK_WIDGET())) {
            int width, height;

            width = gtk_widget_get_allocated_width(GTK_WIDGET());
            height = gtk_widget_get_allocated_height(GTK_WIDGET());
            g_settings_set_int(priv->settings_ui, PREF_UI_WINDOW_WIDTH, width);
            g_settings_set_int(priv->settings_ui, PREF_UI_WINDOW_HEIGHT, height);

            width = gtk_paned_get_position(GTK_PANED(priv->paned));
            if(width > 0) {
                g_settings_set_int(priv->settings_ui, PREF_UI_SIDEBAR_WIDTH, width);
            }

            width = gtk_tree_view_column_get_width(priv->filename_column);
            if(width > 0) {
                g_settings_set_int(priv->settings_listing, PREF_LISTING_NAME_COLUMN_WIDTH, width);
            }
        }
    */
    deleteLater();
}

/*
static void
clipboard_owner_change_cb(GtkClipboard* clipboard,
                          GdkEvent*     event,
                          gpointer      user_data) {
    FrWindow::update_paste_command_sensitivity((FrWindow*) user_data, clipboard);
}

static void
FrWindow::realized(GtkWidget* window,
                   gpointer*  data) {
    GtkClipboard* clipboard;

    clipboard = gtk_widget_get_clipboard(FR_CLIPBOARD);
    g_signal_connect(clipboard,
                     "owner_change",
                     G_CALLBACK(clipboard_owner_change_cb),
                     window);
}


static void
FrWindow::unrealized(GtkWidget* window,
                     gpointer*  data) {
    GtkClipboard* clipboard;

    clipboard = gtk_widget_get_clipboard(FR_CLIPBOARD);
    g_signal_handlers_disconnect_by_func(clipboard,
                                         G_CALLBACK(clipboard_owner_change_cb),
                                         window);
}

*/

FrWindow::FrWindow() {
    priv = g_new0(FrWindowPrivateData, 1);
    priv->update_dropped_files = FALSE;
    priv->filter_mode = FALSE;
    priv->batch_title = NULL;
    priv->use_progress_dialog = TRUE;
    priv->batch_title = NULL;

    /*
        g_signal_connect(window,
                         "realize",
                         G_CALLBACK(FrWindow::realized),
                         NULL);
        g_signal_connect(window,
                         "unrealize",
                         G_CALLBACK(FrWindow::unrealized),
                         NULL);
    */
}


/* -- window history -- */


#if 0
static void
FrWindow::history_print() {
    GList* list;

    debug(DEBUG_INFO, "history:\n");
    for(list = priv->history; list; list = list->next)
        g_print("\t%s %s\n",
                (char*) list->data,
                (list == priv->history_current) ? "<-" : "");
    g_print("\n");
}
#endif


void FrWindow::history_add(const char* path) {
    if((priv->history_current == NULL) || (g_strcmp0(path, (char*)priv->history_current->data) != 0)) {
        GList* scan;
        GList* new_current = NULL;

        /* search the path in the history */
        for(scan = priv->history_current; scan; scan = scan->next) {
            char* path_in_history = (char*)scan->data;

            if(g_strcmp0(path, path_in_history) == 0) {
                new_current = scan;
                break;
            }
        }

        if(new_current != NULL) {
            priv->history_current = new_current;
        }
        else {
            /* remove all the paths after the current position */
            for(scan = priv->history; scan && (scan != priv->history_current); /* void */) {
                GList* next = scan->next;

                priv->history = g_list_remove_link(priv->history, scan);
                path_list_free(scan);

                scan = next;
            }

            priv->history = g_list_prepend(priv->history, g_strdup(path));
            priv->history_current = priv->history;
        }
    }
}


void FrWindow::history_pop() {
    GList* first;

    if(priv->history == NULL) {
        return;
    }

    first = priv->history;
    priv->history = g_list_remove_link(priv->history, first);
    if(priv->history_current == first) {
        priv->history_current = priv->history;
    }
    g_free(first->data);
    g_list_free(first);
}


/* -- window_update_file_list -- */


GPtrArray*
FrWindow::get_current_dir_list() {
    GPtrArray* files;
    int        i;

    files = g_ptr_array_sized_new(128);

    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fdata = (FileData*)g_ptr_array_index(archive->command->files, i);

        if(fdata->list_name == NULL) {
            continue;
        }
        g_ptr_array_add(files, fdata);
    }

    return files;
}


static gint
sort_by_name(gconstpointer  ptr1,
             gconstpointer  ptr2) {
    FileData* fdata1 = *((FileData**) ptr1);
    FileData* fdata2 = *((FileData**) ptr2);

    if(file_data_is_dir(fdata1) != file_data_is_dir(fdata2)) {
        if(file_data_is_dir(fdata1)) {
            return -1;
        }
        else {
            return 1;
        }
    }

    return strcmp(fdata1->sort_key, fdata2->sort_key);
}


static gint
sort_by_size(gconstpointer  ptr1,
             gconstpointer  ptr2) {
    FileData* fdata1 = *((FileData**) ptr1);
    FileData* fdata2 = *((FileData**) ptr2);

    if(file_data_is_dir(fdata1) != file_data_is_dir(fdata2)) {
        if(file_data_is_dir(fdata1)) {
            return -1;
        }
        else {
            return 1;
        }
    }
    else if(file_data_is_dir(fdata1) && file_data_is_dir(fdata2)) {
        if(fdata1->dir_size > fdata2->dir_size) {
            return 1;
        }
        else {
            return -1;
        }
    }

    if(fdata1->size == fdata2->size) {
        return sort_by_name(ptr1, ptr2);
    }
    else if(fdata1->size > fdata2->size) {
        return 1;
    }
    else {
        return -1;
    }
}


static gint
sort_by_type(gconstpointer  ptr1,
             gconstpointer  ptr2) {
    FileData*    fdata1 = *((FileData**) ptr1);
    FileData*    fdata2 = *((FileData**) ptr2);
    int          result;
    const char*  desc1, *desc2;

    if(file_data_is_dir(fdata1) != file_data_is_dir(fdata2)) {
        if(file_data_is_dir(fdata1)) {
            return -1;
        }
        else {
            return 1;
        }
    }
    else if(file_data_is_dir(fdata1) && file_data_is_dir(fdata2)) {
        return sort_by_name(ptr1, ptr2);
    }

    desc1 = g_content_type_get_description(fdata1->content_type);
    desc2 = g_content_type_get_description(fdata2->content_type);

    result = strcasecmp(desc1, desc2);
    if(result == 0) {
        return sort_by_name(ptr1, ptr2);
    }
    else {
        return result;
    }
}


static gint
sort_by_time(gconstpointer  ptr1,
             gconstpointer  ptr2) {
    FileData* fdata1 = *((FileData**) ptr1);
    FileData* fdata2 = *((FileData**) ptr2);

    if(file_data_is_dir(fdata1) != file_data_is_dir(fdata2)) {
        if(file_data_is_dir(fdata1)) {
            return -1;
        }
        else {
            return 1;
        }
    }
    else if(file_data_is_dir(fdata1) && file_data_is_dir(fdata2)) {
        return sort_by_name(ptr1, ptr2);
    }

    if(fdata1->modified == fdata2->modified) {
        return sort_by_name(ptr1, ptr2);
    }
    else if(fdata1->modified > fdata2->modified) {
        return 1;
    }
    else {
        return -1;
    }
}


static gint
sort_by_path(gconstpointer  ptr1,
             gconstpointer  ptr2) {
    FileData* fdata1 = *((FileData**) ptr1);
    FileData* fdata2 = *((FileData**) ptr2);
    int       result;

    if(file_data_is_dir(fdata1) != file_data_is_dir(fdata2)) {
        if(file_data_is_dir(fdata1)) {
            return -1;
        }
        else {
            return 1;
        }
    }
    else if(file_data_is_dir(fdata1) && file_data_is_dir(fdata2)) {
        return sort_by_name(ptr1, ptr2);
    }

    /* 2 files */

    result = strcasecmp(fdata1->path, fdata2->path);
    if(result == 0) {
        return sort_by_name(ptr1, ptr2);
    }
    else {
        return result;
    }
}


guint64
FrWindow::get_dir_size(
    const char* current_dir,
    const char* name) {
    guint64  size;
    char*    dirname;
    int      dirname_l;
    int      i;

    dirname = g_strconcat(current_dir, name, "/", NULL);
    dirname_l = strlen(dirname);

    size = 0;
    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fd = (FileData*)g_ptr_array_index(archive->command->files, i);

        if(strncmp(dirname, fd->full_path, dirname_l) == 0) {
            size += fd->size;
        }
    }

    g_free(dirname);

    return size;
}


gboolean
FrWindow::file_data_respects_filter(
    FileData* fdata) {
    // FIXME:
    /*
    const char* filter;
    filter = gtk_entry_get_text(GTK_ENTRY(priv->filter_entry));
    if((fdata == NULL) || (filter == NULL) || (*filter == '\0')) {
        return TRUE;
    }

    if(fdata->dir || (fdata->name == NULL)) {
        return FALSE;
    }
    return strncasecmp(fdata->name, filter, strlen(filter)) == 0;
    */
    return false;
}


gboolean
FrWindow::compute_file_list_name(
    FileData*   fdata,
    const char* current_dir,
    int         current_dir_len,
    GHashTable* names_hash,
    gboolean*   different_name) {
    register char* scan, *end;

    *different_name = FALSE;
    // FIXME:
#if 0
    if(! file_data_respects_filter(fdata)) {
        return FALSE;
    }

    if(priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
        if(!(fdata->dir)) {
            file_data_set_list_name(fdata, fdata->name);
        }
        return FALSE;
    }

    if(strncmp(fdata->full_path, current_dir, current_dir_len) != 0) {
        *different_name = TRUE;
        return FALSE;
    }

    if(strlen(fdata->full_path) == current_dir_len) {
        return FALSE;
    }

    scan = fdata->full_path + current_dir_len;
    end = strchr(scan, '/');
    if((end == NULL) && ! fdata->dir) {  /* file */
        file_data_set_list_name(fdata, scan);
    }
    else { /* folder */
        char* dir_name;

        if(end != NULL) {
            dir_name = g_strndup(scan, end - scan);
        }
        else {
            dir_name = g_strdup(scan);
        }

        /* avoid to insert duplicated folders */
        if(g_hash_table_lookup(names_hash, dir_name) != NULL) {
            g_free(dir_name);
            return FALSE;
        }
        g_hash_table_insert(names_hash, dir_name, GINT_TO_POINTER(1));

        if((end != NULL) && (*(end + 1) != '\0')) {
            fdata->list_dir = TRUE;
        }
        file_data_set_list_name(fdata, dir_name);
        fdata->dir_size = get_dir_size(current_dir, dir_name);
    }
#endif
    return TRUE;
}


void
FrWindow::compute_list_names(
    GPtrArray* files) {
    const char* current_dir;
    int         current_dir_len;
    GHashTable* names_hash;
    int         i;
    gboolean    visible_list_started = FALSE;
    gboolean    visible_list_completed = FALSE;
    gboolean    different_name;

    current_dir = get_current_location();
    current_dir_len = strlen(current_dir);
    names_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for(i = 0; i < files->len; i++) {
        FileData* fdata = (FileData*)g_ptr_array_index(files, i);

        file_data_set_list_name(fdata, NULL);
        fdata->list_dir = FALSE;

        /* the files array is sorted by path, when the visible list
         * is started and we find a path that doesn't match the
         * current_dir path, the following files can't match
         * the current_dir path. */

        if(visible_list_completed) {
            continue;
        }

        if(compute_file_list_name(fdata, current_dir, current_dir_len, names_hash, &different_name)) {
            visible_list_started = TRUE;
        }
        else if(visible_list_started && different_name) {
            visible_list_completed = TRUE;
        }
    }

    g_hash_table_destroy(names_hash);
}


gboolean
FrWindow::dir_exists_in_archive(
    const char* dir_name) {
    int dir_name_len;
    int i;

    if(dir_name == NULL) {
        return FALSE;
    }

    dir_name_len = strlen(dir_name);
    if(dir_name_len == 0) {
        return TRUE;
    }

    if(strcmp(dir_name, "/") == 0) {
        return TRUE;
    }

    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fdata = (FileData*)g_ptr_array_index(archive->command->files, i);

        if(strncmp(dir_name, fdata->full_path, dir_name_len) == 0) {
            return TRUE;
        }
        else if(fdata->dir
                && (fdata->full_path[strlen(fdata->full_path) - 1] != '/')
                && (strncmp(dir_name, fdata->full_path, dir_name_len - 1) == 0)) {
            return TRUE;
        }
    }

    return FALSE;
}


static char*
get_parent_dir(const char* current_dir) {
    char* dir;
    char* new_dir;
    char* retval;

    if(current_dir == NULL) {
        return NULL;
    }
    if(strcmp(current_dir, "/") == 0) {
        return g_strdup("/");
    }

    dir = g_strdup(current_dir);
    dir[strlen(dir) - 1] = 0;
    new_dir = remove_level_from_path(dir);
    g_free(dir);

    if(new_dir[strlen(new_dir) - 1] == '/') {
        retval = new_dir;
    }
    else {
        retval = g_strconcat(new_dir, "/", NULL);
        g_free(new_dir);
    }

    return retval;
}


#if 0
// FIXME:
static GdkPixbuf*
get_mime_type_icon(const char* mime_type) {
    GdkPixbuf* pixbuf = NULL;

    pixbuf = g_hash_table_lookup(tree_pixbuf_hash, mime_type);
    if(pixbuf != NULL) {
        g_object_ref(G_OBJECT(pixbuf));
        return pixbuf;
    }

    pixbuf = get_mime_type_pixbuf(mime_type, file_list_icon_size, icon_theme);
    if(pixbuf == NULL) {
        return NULL;
    }

    pixbuf = gdk_pixbuf_copy(pixbuf);
    g_hash_table_insert(tree_pixbuf_hash, (gpointer) mime_type, pixbuf);
    g_object_ref(G_OBJECT(pixbuf));

    return pixbuf;
}


static GdkPixbuf*
get_icon(GtkWidget* widget,
         FileData*  fdata) {
    GdkPixbuf*  pixbuf = NULL;
    const char* content_type;
    GIcon*      icon;

    if(file_data_is_dir(fdata)) {
        content_type = MIME_TYPE_DIRECTORY;
    }
    else {
        content_type = fdata->content_type;
    }

    /* look in the hash table. */

    pixbuf = g_hash_table_lookup(pixbuf_hash, content_type);
    if(pixbuf != NULL) {
        g_object_ref(G_OBJECT(pixbuf));
        return pixbuf;
    }

    icon = g_content_type_get_icon(content_type);
    pixbuf = get_icon_pixbuf(icon, file_list_icon_size, icon_theme);
    g_object_unref(icon);

    if(pixbuf == NULL) {
        return NULL;
    }

    pixbuf = gdk_pixbuf_copy(pixbuf);
    g_hash_table_insert(pixbuf_hash, (gpointer) content_type, pixbuf);
    g_object_ref(G_OBJECT(pixbuf));

    return pixbuf;
}


static GdkPixbuf*
get_emblem(GtkWidget* widget,
           FileData*  fdata) {
    GdkPixbuf* pixbuf = NULL;

    if(! fdata->encrypted) {
        return NULL;
    }

    /* encrypted */

    pixbuf = g_hash_table_lookup(pixbuf_hash, "emblem-nowrite");
    if(pixbuf != NULL) {
        g_object_ref(G_OBJECT(pixbuf));
        return pixbuf;
    }

    pixbuf = gtk_icon_theme_load_icon(icon_theme,
                                      "emblem-nowrite",
                                      file_list_icon_size,
                                      0,
                                      NULL);
    if(pixbuf == NULL) {
        return NULL;
    }

    pixbuf = gdk_pixbuf_copy(pixbuf);
    g_hash_table_insert(pixbuf_hash, (gpointer) "emblem-nowrite", pixbuf);
    g_object_ref(G_OBJECT(pixbuf));

    return pixbuf;
}
#endif


static int
get_column_from_sort_method(FrWindowSortMethod sort_method) {
    switch(sort_method) {
    case FR_WINDOW_SORT_BY_NAME:
        return COLUMN_NAME;
    case FR_WINDOW_SORT_BY_SIZE:
        return COLUMN_SIZE;
    case FR_WINDOW_SORT_BY_TYPE:
        return COLUMN_TYPE;
    case FR_WINDOW_SORT_BY_TIME:
        return COLUMN_TIME;
    case FR_WINDOW_SORT_BY_PATH:
        return COLUMN_PATH;
    default:
        break;
    }

    return COLUMN_NAME;
}


static int
get_sort_method_from_column(int column_id) {
    switch(column_id) {
    case COLUMN_NAME:
        return FR_WINDOW_SORT_BY_NAME;
    case COLUMN_SIZE:
        return FR_WINDOW_SORT_BY_SIZE;
    case COLUMN_TYPE:
        return FR_WINDOW_SORT_BY_TYPE;
    case COLUMN_TIME:
        return FR_WINDOW_SORT_BY_TIME;
    case COLUMN_PATH:
        return FR_WINDOW_SORT_BY_PATH;
    default:
        break;
    }

    return FR_WINDOW_SORT_BY_NAME;
}

// FIXME:
#if 0
static void
add_selected_from_list_view(GtkTreeModel* model,
                            GtkTreePath*  path,
                            GtkTreeIter*  iter,
                            gpointer      data) {
    GList**    list = data;
    FileData*  fdata;

    gtk_tree_model_get(model, iter,
                       COLUMN_FILE_DATA, &fdata,
                       -1);
    *list = g_list_prepend(*list, fdata);
}


static void
add_selected_from_tree_view(GtkTreeModel* model,
                            GtkTreePath*  path,
                            GtkTreeIter*  iter,
                            gpointer      data) {
    GList** list = data;
    char*   dir_path;

    gtk_tree_model_get(model, iter,
                       TREE_COLUMN_PATH, &dir_path,
                       -1);
    *list = g_list_prepend(*list, dir_path);
}


static void
add_selected_fd(GtkTreeModel* model,
                GtkTreePath*  path,
                GtkTreeIter*  iter,
                gpointer      data) {
    GList**    list = data;
    FileData*  fdata;

    gtk_tree_model_get(model, iter,
                       COLUMN_FILE_DATA, &fdata,
                       -1);
    if(! fdata->list_dir) {
        *list = g_list_prepend(*list, fdata);
    }
}
#endif

GList* FrWindow::get_selection_as_fd() {
#if 0
    GtkTreeSelection* selection;
    GList*            list = NULL;

    if(! gtk_widget_get_realized(priv->list_view)) {
        return NULL;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    if(selection == NULL) {
        return NULL;
    }
    gtk_tree_selection_selected_foreach(selection, add_selected_fd, &list);

    return list;
#endif
    return nullptr;
}


void FrWindow::update_statusbar_list_info() {
#if 0
    char*    info, *archive_info, *selected_info;
    char*    size_txt, *sel_size_txt;
    int      tot_n, sel_n;
    goffset  tot_size, sel_size;
    GList*   scan;

    if(window == NULL) {
        return;
    }

    if((archive == NULL) || (archive->command == NULL)) {
        gtk_statusbar_pop(GTK_STATUSBAR(priv->statusbar), priv->list_info_cid);
        return;
    }

    tot_n = 0;
    tot_size = 0;

    if(priv->archive_present) {
        GPtrArray* files = get_current_dir_list();
        int        i;

        for(i = 0; i < files->len; i++) {
            FileData* fd = g_ptr_array_index(files, i);

            tot_n++;
            if(! file_data_is_dir(fd)) {
                tot_size += fd->size;
            }
            else {
                tot_size += fd->dir_size;
            }
        }
        g_ptr_array_free(files, TRUE);
    }

    sel_n = 0;
    sel_size = 0;

    if(priv->archive_present) {
        GList* selection = get_selection_as_fd();

        for(scan = selection; scan; scan = scan->next) {
            FileData* fd = scan->data;

            sel_n++;
            if(! file_data_is_dir(fd)) {
                sel_size += fd->size;
            }
        }
        g_list_free(selection);
    }

    size_txt = g_format_size(tot_size);
    sel_size_txt = g_format_size(sel_size);

    if(tot_n == 0) {
        archive_info = g_strdup("");
    }
    else {
        archive_info = g_strdup_printf(ngettext("%d object (%s)", "%d objects (%s)", tot_n), tot_n, size_txt);
    }

    if(sel_n == 0) {
        selected_info = g_strdup("");
    }
    else {
        selected_info = g_strdup_printf(ngettext("%d object selected (%s)", "%d objects selected (%s)", sel_n), sel_n, sel_size_txt);
    }

    info = g_strconcat(archive_info,
                       ((sel_n == 0) ? NULL : ", "),
                       selected_info,
                       NULL);

    gtk_statusbar_push(GTK_STATUSBAR(priv->statusbar), priv->list_info_cid, info);

    g_free(size_txt);
    g_free(sel_size_txt);
    g_free(archive_info);
    g_free(selected_info);
    g_free(info);
#endif
}


void FrWindow::populate_file_list(
    GPtrArray* files) {
#if 0
    int i;

    gtk_list_store_clear(priv->list_store);

    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(priv->list_store),
                                         GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                         GTK_SORT_ASCENDING);

    for(i = 0; i < files->len; i++) {
        FileData*    fdata = g_ptr_array_index(files, i);
        GtkTreeIter  iter;
        GdkPixbuf*   icon, *emblem;
        char*        utf8_name;

        if(fdata->list_name == NULL) {
            continue;
        }

        gtk_list_store_append(priv->list_store, &iter);

        icon = get_icon(GTK_WIDGET(), fdata);
        utf8_name = g_filename_display_name(fdata->list_name);
        emblem = get_emblem(GTK_WIDGET(), fdata);

        if(file_data_is_dir(fdata)) {
            char* utf8_path;
            char* tmp;
            char* s_size;
            char* s_time;

            if(fdata->list_dir) {
                tmp = remove_ending_separator());
            }

            else {
                tmp = remove_level_from_path(fdata->path);
            }
            utf8_path = g_filename_display_name(tmp);
            g_free(tmp);

            s_size = g_format_size(fdata->dir_size);

            if(fdata->list_dir) {
                s_time = g_strdup("");
            }
            else {
                s_time = get_time_string(fdata->modified);
            }

            gtk_list_store_set(priv->list_store, &iter,
                               COLUMN_FILE_DATA, fdata,
                               COLUMN_ICON, icon,
                               COLUMN_NAME, utf8_name,
                               COLUMN_EMBLEM, emblem,
                               COLUMN_TYPE, _("Folder"),
                               COLUMN_SIZE, s_size,
                               COLUMN_TIME, s_time,
                               COLUMN_PATH, utf8_path,
                               -1);
            g_free(utf8_path);
            g_free(s_size);
            g_free(s_time);
        }
        else {
            char*       utf8_path;
            char*       s_size;
            char*       s_time;
            const char* desc;

            utf8_path = g_filename_display_name(fdata->path);

            s_size = g_format_size(fdata->size);
            s_time = get_time_string(fdata->modified);
            desc = g_content_type_get_description(fdata->content_type);

            gtk_list_store_set(priv->list_store, &iter,
                               COLUMN_FILE_DATA, fdata,
                               COLUMN_ICON, icon,
                               COLUMN_NAME, utf8_name,
                               COLUMN_EMBLEM, emblem,
                               COLUMN_TYPE, desc,
                               COLUMN_SIZE, s_size,
                               COLUMN_TIME, s_time,
                               COLUMN_PATH, utf8_path,
                               -1);
            g_free(utf8_path);
            g_free(s_size);
            g_free(s_time);
        }
        g_free(utf8_name);
        if(icon != NULL) {
            g_object_unref(icon);
        }
        if(emblem != NULL) {
            g_object_unref(emblem);
        }
    }

    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(priv->list_store),
                                         get_column_from_sort_method(priv->sort_method),
                                         priv->sort_type);

    update_statusbar_list_info();
    stop_activity_mode();
#endif
}


static int
path_compare(gconstpointer a,
             gconstpointer b) {
    char* path_a = *((char**) a);
    char* path_b = *((char**) b);

    return strcmp(path_a, path_b);
}

#if 0
static gboolean
get_tree_iter_from_path(
    const char*  path,
    GtkTreeIter* parent,
    GtkTreeIter* iter) {
    gboolean    result = FALSE;

    if(! gtk_tree_model_iter_children(GTK_TREE_MODEL(priv->tree_store), iter, parent)) {
        return FALSE;
    }

    do {
        GtkTreeIter  tmp;
        char*        iter_path;

        if(get_tree_iter_from_path(path, iter, &tmp)) {
            *iter = tmp;
            return TRUE;
        }

        gtk_tree_model_get(GTK_TREE_MODEL(priv->tree_store),
                           iter,
                           TREE_COLUMN_PATH, &iter_path,
                           -1);

        if((iter_path != NULL) && (strcmp(path, iter_path) == 0)) {
            result = TRUE;
            g_free(iter_path);
            break;
        }
        g_free(iter_path);
    }
    while(gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->tree_store), iter));

    return result;
}
#endif

static void
set_sensitive(
    const char* action_name,
    gboolean    sensitive) {
#if 0
    GtkAction* action;

    action = gtk_action_group_get_action(priv->actions, action_name);
    g_object_set(action, "sensitive", sensitive, NULL);
#endif
}


void FrWindow::update_current_location() {
    const char* current_dir = get_current_location();
    char*       path;

#if 0
    GtkTreeIter iter;

    if(priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
        gtk_widget_hide(priv->location_bar);
        return;
    }

    gtk_widget_show(priv->location_bar);

    gtk_entry_set_text(GTK_ENTRY(priv->location_entry), priv->archive_present ? current_dir : "");

    set_sensitive("GoBack", priv->archive_present && (current_dir != NULL) && (priv->history_current != NULL) && (priv->history_current->next != NULL));
    set_sensitive("GoForward", priv->archive_present && (current_dir != NULL) && (priv->history_current != NULL) && (priv->history_current->prev != NULL));
    set_sensitive("GoUp", priv->archive_present && (current_dir != NULL) && (strcmp(current_dir, "/") != 0));
    set_sensitive("GoHome", priv->archive_present);
    gtk_widget_set_sensitive(priv->location_entry, priv->archive_present);
    gtk_widget_set_sensitive(priv->location_label, priv->archive_present);
    gtk_widget_set_sensitive(priv->filter_entry, priv->archive_present);

    path = remove_ending_separator(current_dir);
    if(get_tree_iter_from_path(path, NULL, &iter)) {
        GtkTreeSelection* selection;
        GtkTreePath*      t_path;

        t_path = gtk_tree_model_get_path(GTK_TREE_MODEL(priv->tree_store), &iter);
        gtk_tree_view_expand_to_path(GTK_TREE_VIEW(priv->tree_view), t_path);
        gtk_tree_path_free(t_path);

        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
        gtk_tree_selection_select_iter(selection, &iter);
    }
    g_free(path);

#endif
}

void FrWindow::update_dir_tree() {
#if 0
    GPtrArray*  dirs;
    GHashTable* dir_cache;
    int         i;
    GdkPixbuf*  icon;

    gtk_tree_store_clear(priv->tree_store);

    if(! priv->view_folders
            || ! priv->archive_present
            || (priv->list_mode == FR_WINDOW_LIST_MODE_FLAT)) {
        gtk_widget_set_sensitive(priv->tree_view, FALSE);
        gtk_widget_hide(priv->sidepane);
        return;
    }
    else {
        gtk_widget_set_sensitive(priv->tree_view, TRUE);
        if(! gtk_widget_get_visible(priv->sidepane)) {
            gtk_widget_show_all(priv->sidepane);
        }
    }

    if(gtk_widget_get_realized(priv->tree_view)) {
        gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(priv->tree_view), 0, 0);
    }

    /**/

    dirs = g_ptr_array_sized_new(128);

    dir_cache = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fdata = (FileData*)g_ptr_array_index(archive->command->files, i);
        char*     dir;

        if(gtk_entry_get_text(GTK_ENTRY(priv->filter_entry)) != NULL) {
            if(! file_data_respects_filter(fdata)) {
                continue;
            }
        }

        if(fdata->dir) {
            dir = remove_ending_separator(fdata->full_path);
        }
        else {
            dir = remove_level_from_path(fdata->full_path);
        }

        while((dir != NULL) && (strcmp(dir, "/") != 0)) {
            char* new_dir;

            if(g_hash_table_lookup(dir_cache, dir) != NULL) {
                break;
            }

            new_dir = dir;
            g_ptr_array_add(dirs, new_dir);
            g_hash_table_replace(dir_cache, new_dir, "1");

            dir = remove_level_from_path(new_dir);
        }

        g_free(dir);
    }
    g_hash_table_destroy(dir_cache);

    g_ptr_array_sort(dirs, path_compare);
    dir_cache = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) gtk_tree_path_free);

    /**/

    icon = get_mime_type_icon(MIME_TYPE_ARCHIVE);
    {
        GtkTreeIter  node;
        char*        uri;
        char*        name;

        uri = g_file_get_uri(archive->file);
        name = g_uri_display_basename(uri);

        gtk_tree_store_append(priv->tree_store, &node, NULL);
        gtk_tree_store_set(priv->tree_store, &node,
                           TREE_COLUMN_ICON, icon,
                           TREE_COLUMN_NAME, name,
                           TREE_COLUMN_PATH, "/",
                           TREE_COLUMN_WEIGHT, PANGO_WEIGHT_BOLD,
                           -1);
        g_hash_table_replace(dir_cache, "/", gtk_tree_model_get_path(GTK_TREE_MODEL(priv->tree_store), &node));

        g_free(name);
        g_free(uri);
    }
    g_object_unref(icon);

    /**/

    icon = get_mime_type_icon(MIME_TYPE_DIRECTORY);
    for(i = 0; i < dirs->len; i++) {
        char*        dir = g_ptr_array_index(dirs, i);
        char*        parent_dir;
        GtkTreePath* parent_path;
        GtkTreeIter  parent;
        GtkTreeIter  node;

        parent_dir = remove_level_from_path(dir);
        if(parent_dir == NULL) {
            continue;
        }

        parent_path = g_hash_table_lookup(dir_cache, parent_dir);
        gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->tree_store),
                                &parent,
                                parent_path);
        gtk_tree_store_append(priv->tree_store, &node, &parent);
        gtk_tree_store_set(priv->tree_store, &node,
                           TREE_COLUMN_ICON, icon,
                           TREE_COLUMN_NAME, file_name_from_path(dir),
                           TREE_COLUMN_PATH, dir,
                           TREE_COLUMN_WEIGHT, PANGO_WEIGHT_NORMAL,
                           -1);
        g_hash_table_replace(dir_cache, dir, gtk_tree_model_get_path(GTK_TREE_MODEL(priv->tree_store), &node));

        g_free(parent_dir);
    }
    g_hash_table_destroy(dir_cache);
    if(icon != NULL) {
        g_object_unref(icon);
    }

    g_ptr_array_free(dirs, TRUE);

    update_current_location();
#endif
}

void
FrWindow::update_filter_bar_visibility() {
#if 0
    const char* filter;

    filter = gtk_entry_get_text(GTK_ENTRY(priv->filter_entry));
    if((filter == NULL) || (*filter == '\0')) {
        gtk_widget_hide(priv->filter_bar);
    }
    else {
        gtk_widget_show(priv->filter_bar);
    }
#endif
}


void FrWindow::update_file_list(
    gboolean  update_view) {
#if 0
    GPtrArray*  files;
    gboolean    free_files = FALSE;

    if(gtk_widget_get_realized(priv->list_view)) {
        gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(priv->list_view), 0, 0);
    }

    if(! priv->archive_present || priv->archive_new) {
        if(update_view) {
            gtk_list_store_clear(priv->list_store);
        }

        priv->current_view_length = 0;

        if(priv->archive_new) {
            gtk_widget_set_sensitive(priv->list_view, TRUE);
            gtk_widget_show_all(gtk_widget_get_parent(priv->list_view));
        }
        else {
            gtk_widget_set_sensitive(priv->list_view, FALSE);
            gtk_widget_hide(gtk_widget_get_parent(priv->list_view));
        }

        return;
    }
    else {
        gtk_widget_set_sensitive(priv->list_view, TRUE);
        gtk_widget_show_all(gtk_widget_get_parent(priv->list_view));
    }

    if(priv->give_focus_to_the_list) {
        gtk_widget_grab_focus(priv->list_view);
        priv->give_focus_to_the_list = FALSE;
    }

    /**/

    start_activity_mode();

    if(priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
        compute_list_names(archive->command->files);
        files = archive->command->files;
        free_files = FALSE;
    }
    else {
        char* current_dir = g_strdup());

        while(! dir_exists_in_archive(current_dir)) {
            char* tmp;

            history_pop();

            tmp = get_parent_dir(current_dir);
            g_free(current_dir);
            current_dir = tmp;

            history_add(current_dir);
        }
        g_free(current_dir);

        compute_list_names(archive->command->files);
        files = get_current_dir_list();
        free_files = TRUE;
    }

    if(files != NULL) {
        priv->current_view_length = files->len;
    }
    else {
        priv->current_view_length = 0;
    }

    if(update_view) {
        populate_file_list(files);
    }

    if(free_files) {
        g_ptr_array_free(files, TRUE);
    }
#endif
}


void
FrWindow::update_list_order() {
#if 0
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(priv->list_store), get_column_from_sort_method(priv->sort_method), priv->sort_type);
#endif
}


void FrWindow::update_title() {
    if(! priv->archive_present) {
        setWindowTitle(_("Archive Manager"));
    }
    else {
        char* title;
        char* name;

        name = g_uri_display_basename(get_archive_uri());
        title = g_strdup_printf("%s %s",
                                name,
                                archive->read_only ? _("[read only]") : "");

        setWindowTitle(title);
        g_free(title);
        g_free(name);
    }
}

#if 0

static void
check_whether_has_a_dir(GtkTreeModel* model,
                        GtkTreePath*  path,
                        GtkTreeIter*  iter,
                        gpointer      data) {
    gboolean* has_a_dir = data;
    FileData* fdata;

    gtk_tree_model_get(model, iter,
                       COLUMN_FILE_DATA, &fdata,
                       -1);
    if(file_data_is_dir(fdata)) {
        *has_a_dir = TRUE;
    }
}


static gboolean
selection_has_a_dir() {
    GtkTreeSelection* selection;
    gboolean          has_a_dir = FALSE;

    if(! gtk_widget_get_realized(priv->list_view)) {
        return FALSE;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    if(selection == NULL) {
        return FALSE;
    }

    gtk_tree_selection_selected_foreach(selection,
                                        check_whether_has_a_dir,
                                        &has_a_dir);

    return has_a_dir;
}

static void
set_active(
    const char* action_name,
    gboolean    is_active) {
    GtkAction* action;

    action = gtk_action_group_get_action(priv->actions, action_name);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), is_active);
}


void FrWindow::update_paste_command_sensitivity(
    GtkClipboard* clipboard) {
    gboolean running;
    gboolean no_archive;
    gboolean ro;
    gboolean compr_file;

    if(priv->closing) {
        return;
    }

    if(clipboard == NULL) {
        clipboard = gtk_widget_get_clipboard(GTK_WIDGET(), FR_CLIPBOARD);
    }
    running    = priv->activity_ref > 0;
    no_archive = (archive == NULL) || ! priv->archive_present;
    ro         = ! no_archive && archive->read_only;
    compr_file = ! no_archive && archive->is_compressed_file;

    set_sensitive("Paste", ! no_archive && ! ro && ! running && ! compr_file && (priv->list_mode != FR_WINDOW_LIST_MODE_FLAT) && gtk_clipboard_wait_is_target_available(clipboard, FR_SPECIAL_URI_LIST));
}
#endif

void FrWindow::update_sensitivity() {
    gboolean no_archive;
    gboolean ro;
    gboolean file_op;
    gboolean running;
    gboolean compr_file;
    gboolean sel_not_null;
    gboolean one_file_selected;
    gboolean dir_selected;
    int      n_selected;

    if(priv->batch_mode) {
        return;
    }
#if 0
    running           = priv->activity_ref > 0;
    no_archive        = (archive == NULL) || ! priv->archive_present;
    ro                = ! no_archive && archive->read_only;
    file_op           = ! no_archive && ! priv->archive_new  && ! running;
    compr_file        = ! no_archive && archive->is_compressed_file;
    n_selected        = get_n_selected_files();
    sel_not_null      = n_selected > 0;
    one_file_selected = n_selected == 1;
    dir_selected      = selection_has_a_dir();

    set_sensitive("AddFiles", ! no_archive && ! ro && ! running && ! compr_file);
    set_sensitive("AddFiles_Toolbar", ! no_archive && ! ro && ! running && ! compr_file);
    set_sensitive("AddFolder", ! no_archive && ! ro && ! running && ! compr_file);
    set_sensitive("AddFolder_Toolbar", ! no_archive && ! ro && ! running && ! compr_file);
    set_sensitive("Copy", ! no_archive && ! ro && ! running && ! compr_file && sel_not_null && (priv->list_mode != FR_WINDOW_LIST_MODE_FLAT));
    set_sensitive("Cut", ! no_archive && ! ro && ! running && ! compr_file && sel_not_null && (priv->list_mode != FR_WINDOW_LIST_MODE_FLAT));
    set_sensitive("Delete", ! no_archive && ! ro && ! priv->archive_new && ! running && ! compr_file);
    set_sensitive("DeselectAll", ! no_archive && sel_not_null);
    set_sensitive("Extract", file_op);
    set_sensitive("Extract_Toolbar", file_op);
    set_sensitive("Find", ! no_archive);
    set_sensitive("LastOutput", ((archive != NULL)
                                 && (archive->process != NULL)
                                 && (archive->process->out.raw != NULL)));
    set_sensitive("New", ! running);
    set_sensitive("Open", ! running);
    set_sensitive("Open_Toolbar", ! running);
    set_sensitive("OpenSelection", file_op && sel_not_null && ! dir_selected);
    set_sensitive("OpenFolder", file_op && one_file_selected && dir_selected);
    set_sensitive("Password", ! running && (priv->asked_for_password || (! no_archive && archive->command->propPassword)));
    set_sensitive("Properties", file_op);
    set_sensitive("Close", !running || priv->stoppable);
    set_sensitive("Reload", !(no_archive || running));
    set_sensitive("Rename", ! no_archive && ! ro && ! running && ! compr_file && one_file_selected);
    set_sensitive("SaveAs", ! no_archive && ! compr_file && ! running);
    set_sensitive("SelectAll", ! no_archive);
    set_sensitive("Stop", running && priv->stoppable);
    set_sensitive("TestArchive", ! no_archive && ! running && archive->command->propTest);
    set_sensitive("ViewSelection", file_op && one_file_selected && ! dir_selected);
    set_sensitive("ViewSelection_Toolbar", file_op && one_file_selected && ! dir_selected);

    if(priv->progress_dialog != NULL)
        gtk_dialog_set_response_sensitive(GTK_DIALOG(priv->progress_dialog),
                                          GTK_RESPONSE_OK,
                                          running && priv->stoppable);

    update_paste_command_sensitivity(NULL);

    set_sensitive("SelectAll", (priv->current_view_length > 0) && (priv->current_view_length != n_selected));
    set_sensitive("DeselectAll", n_selected > 0);
    set_sensitive("OpenRecent", ! running);
    set_sensitive("OpenRecent_Toolbar", ! running);

    set_sensitive("ViewFolders", (priv->list_mode == FR_WINDOW_LIST_MODE_AS_DIR));

    set_sensitive("ViewAllFiles", ! priv->filter_mode);
    set_sensitive("ViewAsFolder", ! priv->filter_mode);
#endif
}

#if 0
static gboolean
location_entry_key_press_event_cb(GtkWidget*   widget,
                                  GdkEventKey* event,
                                  FrWindow*    window) {
    if((event->keyval == GDK_KEY_Return)
            || (event->keyval == GDK_KEY_KP_Enter)
            || (event->keyval == GDK_KEY_ISO_Enter)) {
        go_to_location(gtk_entry_get_text(GTK_ENTRY(priv->location_entry)), FALSE);
    }

    return FALSE;
}


static gboolean
real_close_progress_dialog(gpointer data) {
    FrWindow* window = data;

    if(priv->hide_progress_timeout != 0) {
        g_source_remove(priv->hide_progress_timeout);
        priv->hide_progress_timeout = 0;
    }

    if(priv->progress_dialog != NULL) {
        gtk_widget_hide(priv->progress_dialog);
    }

    return FALSE;
}

#endif


void
FrWindow::close_progress_dialog(
    gboolean  close_now) {
#if 0
    if(priv->progress_timeout != 0) {
        g_source_remove(priv->progress_timeout);
        priv->progress_timeout = 0;
    }

    if(! priv->batch_mode) {
        // gtk_widget_hide(priv->progress_bar);
    }

    if(priv->progress_dialog == NULL) {
        return;
    }

    if(close_now) {
        if(priv->hide_progress_timeout != 0) {
            g_source_remove(priv->hide_progress_timeout);
            priv->hide_progress_timeout = 0;
        }
        real_close_progress_dialog();
    }
    else {
        if(priv->hide_progress_timeout != 0) {
            return;
        }
        priv->hide_progress_timeout = g_timeout_add(HIDE_PROGRESS_TIMEOUT_MSECS,
                                      real_close_progress_dialog,
                                      window);
    }
#endif
}

#if 0

static gboolean
progress_dialog_delete_event(GtkWidget* caller,
                             GdkEvent*  event,
                             FrWindow*  window) {
    if(priv->stoppable) {
        activate_action_stop(NULL, window);
        close_progress_dialog(TRUE);
    }

    return TRUE;
}


static void
open_folder(GtkWindow*  parent,
            const char* folder) {
    GError* error = NULL;

    if(folder == NULL) {
        return;
    }

    if(! gtk_show_uri_on_window(parent, folder, GDK_CURRENT_TIME, &error)) {
        GtkWidget* d;
        char*      utf8_name;
        char*      message;

        utf8_name = g_filename_display_name(folder);
        message = g_strdup_printf(_("Could not display the folder \"%s\""), utf8_name);
        g_free(utf8_name);

        d = _gtk_error_dialog_new(parent,
                                  GTK_DIALOG_MODAL,
                                  NULL,
                                  message,
                                  "%s",
                                  error->message);
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);

        g_free(message);
        g_clear_error(&error);
    }
}


static void
FrWindow::view_extraction_destination_folder() {
    open_folder(GTK_WINDOW(), fr_archive_get_last_extraction_destination(archive));
}


static void
progress_dialog_response(GtkDialog* dialog,
                         int        response_id,
                         FrWindow*  window) {
    GtkWidget* new_window;

    switch(response_id) {
    case GTK_RESPONSE_CANCEL:
        if(priv->stoppable) {
            activate_action_stop(NULL, window);
            close_progress_dialog(TRUE);
        }
        break;
    case GTK_RESPONSE_CLOSE:
        close_progress_dialog(TRUE);
        break;
    case DIALOG_RESPONSE_OPEN_ARCHIVE:
        new_window = FrWindow::new();
        gtk_widget_show(new_window);
        FrWindow::archive_open(FR_WINDOW(new_window), priv->convert_data.new_file, GTK_WINDOW(new_window));
        close_progress_dialog(TRUE);
        break;
    case DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER:
        view_extraction_destination_folder();
        close_progress_dialog(TRUE);
        break;
    case DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER_AND_QUIT:
        view_extraction_destination_folder();
        close_progress_dialog(TRUE);
        close();
        break;
    case DIALOG_RESPONSE_QUIT:
        close();
        break;
    default:
        break;
    }
}
#endif

static char*
get_action_description(FrAction    action,
                       const char* uri) {
    char* basename;
    char* message;

    basename = (uri != NULL) ? g_uri_display_basename(uri) : NULL;

    message = NULL;
    switch(action) {
    case FR_ACTION_CREATING_NEW_ARCHIVE:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Creating \"%s\""), basename);
        break;
    case FR_ACTION_LOADING_ARCHIVE:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Loading \"%s\""), basename);
        break;
    case FR_ACTION_LISTING_CONTENT:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Reading \"%s\""), basename);
        break;
    case FR_ACTION_DELETING_FILES:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Deleting files from \"%s\""), basename);
        break;
    case FR_ACTION_TESTING_ARCHIVE:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Testing \"%s\""), basename);
        break;
    case FR_ACTION_GETTING_FILE_LIST:
        message = g_strdup(_("Getting the file list"));
        break;
    case FR_ACTION_COPYING_FILES_FROM_REMOTE:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Copying the files to add to \"%s\""), basename);
        break;
    case FR_ACTION_ADDING_FILES:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Adding files to \"%s\""), basename);
        break;
    case FR_ACTION_EXTRACTING_FILES:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Extracting files from \"%s\""), basename);
        break;
    case FR_ACTION_COPYING_FILES_TO_REMOTE:
        message = g_strdup(_("Copying the extracted files to the destination"));
        break;
    case FR_ACTION_CREATING_ARCHIVE:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Creating \"%s\""), basename);
        break;
    case FR_ACTION_SAVING_REMOTE_ARCHIVE:
        /* Translators: %s is a filename */
        message = g_strdup_printf(_("Saving \"%s\""), basename);
        break;
    case FR_ACTION_NONE:
        break;
    }
    g_free(basename);

    return message;
}

void FrWindow::progress_dialog_update_action_description() {
#if 0
    const char* current_archive;
    char*        description;
    char*       description_markup;

    if(priv->progress_dialog == NULL) {
        return;
    }

    if(priv->convert_data.converting) {
        current_archive = priv->convert_data.new_file;
    }
    else if(priv->working_archive != NULL) {
        current_archive = priv->working_archive;
    }
    else {
        current_archive = priv->archive_uri;
    }

    g_free(priv->pd_last_archive);
    priv->pd_last_archive = NULL;
    if(current_archive != NULL) {
        priv->pd_last_archive = g_strdup(current_archive);
    }

    description = get_action_description(priv->action, priv->pd_last_archive);
    description_markup = g_markup_printf_escaped("<span weight=\"bold\" size=\"larger\">%s</span>", description);
    gtk_label_set_markup(GTK_LABEL(priv->pd_action), description_markup);

    g_free(description_markup);
    g_free(description);
#endif
}

gboolean FrWindow::working_archive_cb(FrCommand*  command,
                                      const char* archive_filename,
                                      FrWindow*   window) {
    g_free(priv->working_archive);
    priv->working_archive = NULL;
    if(archive_filename != NULL) {
        priv->working_archive = g_strdup(archive_filename);
    }
    progress_dialog_update_action_description();

    return TRUE;
}


gboolean FrWindow::message_cb(FrCommand*  command,
                              const char* msg,
                              FrWindow*   window) {
#if 0
    if(priv->pd_last_message != msg) {
        g_free(priv->pd_last_message);
        priv->pd_last_message = g_strdup(msg);
    }

    if(priv->progress_dialog == NULL) {
        return TRUE;
    }

    if(msg != NULL) {
        while(*msg == ' ') {
            msg++;
        }
        if(*msg == 0) {
            msg = NULL;
        }
    }

    if(msg != NULL) {
        char* utf8_msg;

        if(! g_utf8_validate(msg, -1, NULL)) {
            utf8_msg = g_locale_to_utf8(msg, -1, 0, 0, 0);
        }
        else {
            utf8_msg = g_strdup(msg);
        }
        if(utf8_msg == NULL) {
            return TRUE;
        }

        if(g_utf8_validate(utf8_msg, -1, NULL)) {
            gtk_label_set_text(GTK_LABEL(priv->pd_message), utf8_msg);
        }

        g_free(priv->pd_last_message);
        priv->pd_last_message = g_strdup(utf8_msg);

        g_signal_emit(G_OBJECT(),
                      FrWindow::signals[PROGRESS],
                      0,
                      priv->pd_last_fraction,
                      priv->pd_last_message);

#ifdef LOG_PROGRESS
        g_print("message > %s\n", utf8_msg);
#endif

        g_free(utf8_msg);
    }
    else {
        gtk_label_set_text(GTK_LABEL(priv->pd_message), "");
    }
#endif

    progress_dialog_update_action_description();

    return TRUE;
}


void FrWindow::create_the_progress_dialog() {
#if 0
    GtkWindow*     parent;
    GtkDialogFlags flags;
    GtkDialog*     d;
    GtkWidget*     hbox;
    GtkWidget*     vbox;
    GtkWidget*     progress_vbox;
    GtkWidget*     lbl;
    PangoAttrList* attr_list;
    GdkPixbuf*     icon;

    if(priv->progress_dialog != NULL) {
        return;
    }

    flags = GTK_DIALOG_DESTROY_WITH_PARENT;
    if(priv->batch_mode) {
        parent = NULL;
    }
    else {
        parent = GTK_WINDOW();
        flags |= GTK_DIALOG_MODAL;
    }

    priv->progress_dialog = gtk_dialog_new_with_buttons((priv->batch_mode ? priv->batch_title : NULL),
                            parent,
                            flags,
                            NULL,
                            NULL);

    priv->pd_quit_button = gtk_dialog_add_button(GTK_DIALOG(priv->progress_dialog), "gtk-quit", DIALOG_RESPONSE_QUIT);
    priv->pd_open_archive_button = gtk_dialog_add_button(GTK_DIALOG(priv->progress_dialog), _("_Open the Archive"), DIALOG_RESPONSE_OPEN_ARCHIVE);
    priv->pd_open_destination_button = gtk_dialog_add_button(GTK_DIALOG(priv->progress_dialog), _("_Show the Files"), DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER);
    priv->pd_open_destination_and_quit_button = gtk_dialog_add_button(GTK_DIALOG(priv->progress_dialog), _("Show the _Files and Quit"), DIALOG_RESPONSE_OPEN_DESTINATION_FOLDER_AND_QUIT);
    priv->pd_close_button = gtk_dialog_add_button(GTK_DIALOG(priv->progress_dialog), "gtk-close", GTK_RESPONSE_CLOSE);
    priv->pd_cancel_button = gtk_dialog_add_button(GTK_DIALOG(priv->progress_dialog), "gtk-cancel", GTK_RESPONSE_CANCEL);

    d = GTK_DIALOG(priv->progress_dialog);
    gtk_window_set_resizable(GTK_WINDOW(d), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(d), PROGRESS_DIALOG_DEFAULT_WIDTH, -1);

    /* Main */

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 6);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(d)), hbox, FALSE, FALSE, 10);

    icon = get_mime_type_pixbuf("package-x-generic", _gtk_widget_lookup_for_size(GTK_WIDGET(), GTK_ICON_SIZE_DIALOG), NULL);
    priv->pd_icon = gtk_image_new_from_pixbuf(icon);
    g_object_unref(icon);

    gtk_widget_set_valign(priv->pd_icon, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hbox), priv->pd_icon, FALSE, FALSE, 0);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    /* action description */

    lbl = priv->pd_action = gtk_label_new("");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_valign(lbl, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_widget_set_vexpand(lbl, TRUE);
    gtk_widget_set_margin_bottom(lbl, 12);

    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, TRUE, TRUE, 0);

    /* archive name */

    g_free(priv->pd_last_archive);
    priv->pd_last_archive = NULL;

    if(priv->archive_uri != NULL) {
        priv->pd_last_archive = g_strdup(priv->archive_uri);
    }

    /* progress and details */

    progress_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(progress_vbox, GTK_ALIGN_START);
    gtk_widget_set_hexpand(progress_vbox, TRUE);
    gtk_widget_set_vexpand(progress_vbox, TRUE);
    gtk_widget_set_margin_bottom(progress_vbox, 6);
    gtk_box_pack_start(GTK_BOX(vbox), progress_vbox, TRUE, TRUE, 0);

    /* progress bar */

    priv->pd_progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(priv->pd_progress_bar), ACTIVITY_PULSE_STEP);
    gtk_box_pack_start(GTK_BOX(progress_vbox), priv->pd_progress_bar, TRUE, TRUE, 0);

    /* details label */

    lbl = priv->pd_message = gtk_label_new("");

    attr_list = pango_attr_list_new();
    pango_attr_list_insert(attr_list, pango_attr_size_new(9000));
    gtk_label_set_attributes(GTK_LABEL(lbl), attr_list);
    pango_attr_list_unref(attr_list);

    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(progress_vbox), lbl, TRUE, TRUE, 0);

    gtk_widget_show_all(hbox);

    progress_dialog_update_action_description();

    /* signals */

    g_signal_connect(G_OBJECT(priv->progress_dialog),
                     "response",
                     G_CALLBACK(progress_dialog_response),
                     window);
    g_signal_connect(G_OBJECT(priv->progress_dialog),
                     "delete_event",
                     G_CALLBACK(progress_dialog_delete_event),
                     window);
#endif
}


gboolean FrWindow::display_progress_dialog(gpointer data) {
#if 0
    if(priv->progress_timeout != 0) {
        g_source_remove(priv->progress_timeout);
    }

    if(priv->use_progress_dialog && (priv->progress_dialog != NULL)) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(priv->progress_dialog),
                                          GTK_RESPONSE_OK,
                                          priv->stoppable);
        if(! priv->non_interactive) {
            gtk_widget_show(GTK_WIDGET());
        }
        gtk_widget_hide(priv->progress_bar);
        gtk_widget_show(priv->progress_dialog);
        FrWindow::message_cb(NULL, priv->pd_last_message, window);
    }
#endif

    priv->progress_timeout = 0;
    return FALSE;
}


void FrWindow::open_progress_dialog(gboolean  open_now) {
#if 0
    if(priv->hide_progress_timeout != 0) {
        g_source_remove(priv->hide_progress_timeout);
        priv->hide_progress_timeout = 0;
    }

    if(open_now) {
        if(priv->progress_timeout != 0) {
            g_source_remove(priv->progress_timeout);
        }
        priv->progress_timeout = 0;
    }

    if((priv->progress_timeout != 0)
            || ((priv->progress_dialog != NULL) && gtk_widget_get_visible(priv->progress_dialog))) {
        return;
    }

    if(! priv->batch_mode && ! open_now) {
        gtk_widget_show(priv->progress_bar);
    }

    create_the_progress_dialog();
    gtk_widget_show(priv->pd_cancel_button);
    gtk_widget_hide(priv->pd_open_archive_button);
    gtk_widget_hide(priv->pd_open_destination_button);
    gtk_widget_hide(priv->pd_open_destination_and_quit_button);
    gtk_widget_hide(priv->pd_quit_button);
    gtk_widget_hide(priv->pd_close_button);

    if(open_now) {
        display_progress_dialog();
    }
    else
        priv->progress_timeout = g_timeout_add(PROGRESS_TIMEOUT_MSECS,
                                               display_progress_dialog,
                                               window);
#endif
}

gboolean FrWindow::progress_cb(FrArchive* archive,
                               double     fraction) {

#if 0
    priv->progress_pulse = (fraction < 0.0);
    if(! priv->progress_pulse) {
        fraction = CLAMP(fraction, 0.0, 1.0);
        if(priv->progress_dialog != NULL) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->pd_progress_bar), fraction);
        }
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress_bar), fraction);

        if((archive != NULL) && (archive->command != NULL) && (archive->command->n_files > 0)) {
            char* message = NULL;
            int   remaining_files;

            remaining_files = archive->command->n_files - archive->command->n_file + 1;

            switch(priv->action) {
            case FR_ACTION_ADDING_FILES:
            case FR_ACTION_EXTRACTING_FILES:
            case FR_ACTION_DELETING_FILES:
                message = g_strdup_printf(ngettext("%d file remaining",
                                                   "%'d files remaining",
                                                   remaining_files), remaining_files);
                break;
            default:
                break;
            }

            if(message != NULL) {
                fr_command_message(archive->command, message);
            }
        }

        priv->pd_last_fraction = fraction;

        g_signal_emit(G_OBJECT(),
                      FrWindow::signals[PROGRESS],
                      0,
                      priv->pd_last_fraction,
                      priv->pd_last_message);

#ifdef LOG_PROGRESS
        g_print("progress > %2.2f\n", fraction);
#endif
    }
#endif
    return TRUE;
}


void FrWindow::open_progress_dialog_with_open_destination() {
#if 0
    priv->ask_to_open_destination_after_extraction = FALSE;

    if(priv->hide_progress_timeout != 0) {
        g_source_remove(priv->hide_progress_timeout);
        priv->hide_progress_timeout = 0;
    }
    if(priv->progress_timeout != 0) {
        g_source_remove(priv->progress_timeout);
        priv->progress_timeout = 0;
    }

    create_the_progress_dialog();
    gtk_widget_hide(priv->pd_cancel_button);
    gtk_widget_hide(priv->pd_open_archive_button);
    gtk_widget_show(priv->pd_open_destination_button);
    gtk_widget_show(priv->pd_open_destination_and_quit_button);
    gtk_widget_show(priv->pd_quit_button);
    gtk_widget_show(priv->pd_close_button);
    display_progress_dialog();
    FrWindow::progress_cb(NULL, 1.0, window);
    FrWindow::message_cb(NULL, _("Extraction completed successfully"), window);
#endif
}


void FrWindow::open_progress_dialog_with_open_archive() {
#if 0
    if(priv->hide_progress_timeout != 0) {
        g_source_remove(priv->hide_progress_timeout);
        priv->hide_progress_timeout = 0;
    }
    if(priv->progress_timeout != 0) {
        g_source_remove(priv->progress_timeout);
        priv->progress_timeout = 0;
    }

    create_the_progress_dialog();
    gtk_widget_hide(priv->pd_cancel_button);
    gtk_widget_hide(priv->pd_open_destination_button);
    gtk_widget_hide(priv->pd_open_destination_and_quit_button);
    gtk_widget_show(priv->pd_open_archive_button);
    gtk_widget_show(priv->pd_close_button);
    display_progress_dialog();
    FrWindow::progress_cb(NULL, 1.0, window);
    FrWindow::message_cb(NULL, _("Archive created successfully"), window);
#endif
}


void FrWindow::push_message(
    const char* msg) {
#if 0
    gtk_statusbar_push(GTK_STATUSBAR(priv->statusbar),
                       priv->progress_cid,
                       msg);
#endif
}


void FrWindow::pop_message() {
#if 0
    if(! gtk_widget_get_mapped(GTK_WIDGET())) {
        return;
    }
    gtk_statusbar_pop(GTK_STATUSBAR(priv->statusbar), priv->progress_cid);
    if(priv->progress_dialog != NULL) {
        gtk_label_set_text(GTK_LABEL(priv->pd_message), "");
    }
#endif
}

void FrWindow::action_started(FrArchive* archive,
                              FrAction   action,
                              gpointer   data) {
    char*     message;

    priv->action = action;
    start_activity_mode();

#ifdef DEBUG
    debug(DEBUG_INFO, "%s [START] (FR::Window)\n", action_names[action]);
#endif

    message = get_action_description(action, priv->pd_last_archive);
    push_message(message);
    g_free(message);

    switch(action) {
    case FR_ACTION_EXTRACTING_FILES:
        open_progress_dialog(priv->ask_to_open_destination_after_extraction || priv->convert_data.converting || priv->batch_mode);
        break;
    default:
        open_progress_dialog(priv->batch_mode);
        break;
    }

    if(archive->command != NULL) {
        fr_command_progress(archive->command, -1.0);
        fr_command_message(archive->command, _("Please wait…"));
    }
}


void FrWindow::add_to_recent_list(
    char*     uri) {
    if(priv->batch_mode) {
        return;
    }

    if(is_temp_dir(uri)) {
        return;
    }
#if 0
    if(archive->content_type != NULL) {
        GtkRecentData* recent_data;

        recent_data = g_new0(GtkRecentData, 1);
        recent_data->mime_type = g_content_type_get_mime_type(archive->content_type);
        recent_data->app_name = "Engrampa";
        recent_data->app_exec = "engrampa";
        gtk_recent_manager_add_full(gtk_recent_manager_get_default(), uri, recent_data);

        g_free(recent_data);
    }
    else {
        gtk_recent_manager_add_item(gtk_recent_manager_get_default(), uri);
    }
#endif
}


void FrWindow::remove_from_recent_list(

    char*     filename) {
#if 0
    if(filename != NULL) {
        gtk_recent_manager_remove_item(gtk_recent_manager_get_default(), filename, NULL);
    }
#endif
}


#if 0

static void
error_dialog_response_cb(GtkDialog* dialog,
                         gint       arg1,
                         gpointer   user_data) {
    FrWindow*  window = user_data;
    GtkWindow* dialog_parent = priv->error_dialog_parent;

    priv->showing_error_dialog = FALSE;
    priv->error_dialog_parent = NULL;

    if((dialog_parent != NULL) && (gtk_widget_get_toplevel(GTK_WIDGET(dialog_parent)) != (GtkWidget*) dialog_parent)) {
        gtk_window_set_modal(dialog_parent, TRUE);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));

    if(priv->destroy_with_error_dialog) {
        gtk_widget_destroy(GTK_WIDGET());
    }
}


static void
FrWindow::show_error_dialog(
    GtkWidget*  dialog,
    GtkWindow*  dialog_parent,
    const char* details) {
    if(priv->batch_mode && ! priv->use_progress_dialog) {
        GError* error;

        error = g_error_new_literal(FR_ERROR, FR_PROC_ERROR_GENERIC, details ? details : _("Command exited abnormally."));
        g_signal_emit(window,
                      FrWindow::signals[READY],
                      0,
                      error);

        gtk_widget_destroy(GTK_WIDGET());

        return;
    }

    close_progress_dialog(TRUE);

    if(priv->batch_mode) {
        destroy_with_error_dialog();
    }

    if(dialog_parent != NULL) {
        gtk_window_set_modal(dialog_parent, FALSE);
    }
    g_signal_connect(dialog,
                     "response",
                     G_CALLBACK(error_dialog_response_cb),
                     window);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_widget_show(dialog);

    priv->showing_error_dialog = TRUE;
    priv->error_dialog_parent = dialog_parent;
}

#endif

void
FrWindow::destroy_with_error_dialog() {
    priv->destroy_with_error_dialog = TRUE;
}


gboolean
FrWindow::handle_errors(
    FrArchive*   archive,
    FrAction     action,
    FrProcError* error) {

#if 0
    if(error->type == FR_PROC_ERROR_ASK_PASSWORD) {
        close_progress_dialog(TRUE);
        dlg_ask_password();
        return FALSE;
    }
    else if(error->type == FR_PROC_ERROR_UNSUPPORTED_FORMAT) {
        close_progress_dialog(TRUE);
        dlg_package_installer(archive, action);
        return FALSE;
    }
#if 0
    else if(error->type == FR_PROC_ERROR_BAD_CHARSET) {
        close_progress_dialog(TRUE);
        /* dlg_ask_archive_charset (); FIXME: implement after feature freeze */
        return FALSE;
    }
#endif
    else if(error->type == FR_PROC_ERROR_STOPPED) {
        /* nothing */
    }
    else if(error->type != FR_PROC_ERROR_NONE) {
        char*      msg = NULL;
        char*      utf8_name;
        char*      details = NULL;
        GtkWindow* dialog_parent;
        GtkWidget* dialog;
        FrProcess* process = archive->process;
        GList*     output = NULL;

        if(priv->batch_mode) {
            dialog_parent = NULL;
            priv->load_error_parent_window = NULL;
        }
        else {
            dialog_parent = (GtkWindow*) window;
            if(priv->load_error_parent_window == NULL) {
                priv->load_error_parent_window = (GtkWindow*) window;
            }
        }

        if((action == FR_ACTION_LISTING_CONTENT) || (action == FR_ACTION_LOADING_ARCHIVE)) {
            archive_close();
        }

        switch(action) {
        case FR_ACTION_CREATING_NEW_ARCHIVE:
            dialog_parent = priv->load_error_parent_window;
            msg = _("Could not create the archive");
            break;

        case FR_ACTION_EXTRACTING_FILES:
        case FR_ACTION_COPYING_FILES_TO_REMOTE:
            msg = _("An error occurred while extracting files.");
            break;

        case FR_ACTION_LOADING_ARCHIVE:
            dialog_parent = priv->load_error_parent_window;
            utf8_name = g_uri_display_basename(priv->archive_uri);
            msg = g_strdup_printf(_("Could not open \"%s\""), utf8_name);
            g_free(utf8_name);
            break;

        case FR_ACTION_LISTING_CONTENT:
            msg = _("An error occurred while loading the archive.");
            break;

        case FR_ACTION_DELETING_FILES:
            msg = _("An error occurred while deleting files from the archive.");
            break;

        case FR_ACTION_ADDING_FILES:
        case FR_ACTION_GETTING_FILE_LIST:
        case FR_ACTION_COPYING_FILES_FROM_REMOTE:
            msg = _("An error occurred while adding files to the archive.");
            break;

        case FR_ACTION_TESTING_ARCHIVE:
            msg = _("An error occurred while testing archive.");
            break;

        case FR_ACTION_SAVING_REMOTE_ARCHIVE:
            msg = _("An error occurred while saving the archive.");
            break;

        default:
            msg = _("An error occurred.");
            break;
        }

        switch(error->type) {
        case FR_PROC_ERROR_COMMAND_NOT_FOUND:
            details = _("Command not found.");
            break;
        case FR_PROC_ERROR_EXITED_ABNORMALLY:
            details = _("Command exited abnormally.");
            break;
        case FR_PROC_ERROR_SPAWN:
            details = error->gerror->message;
            break;
        default:
            if(error->gerror != NULL) {
                details = error->gerror->message;
            }
            else {
                details = NULL;
            }
            break;
        }

        if(error->type != FR_PROC_ERROR_GENERIC) {
            output = (process->err.raw != NULL) ? process->err.raw : process->out.raw;
        }

        dialog = _gtk_error_dialog_new(dialog_parent,
                                       0,
                                       output,
                                       msg,
                                       ((details != NULL) ? "%s" : NULL),
                                       details);
        show_error_dialog(dialog, dialog_parent, details);

        return FALSE;
    }
#endif

    return TRUE;
}

void
FrWindow::convert__action_performed(FrArchive*   archive,
                                    FrAction     action,
                                    FrProcError* error,
                                    gpointer     data) {

#if 0
    // FIXME

#ifdef DEBUG
    debug(DEBUG_INFO, "%s [CONVERT::DONE] (FR::Window)\n", action_names[action]);
#endif

    if((action == FR_ACTION_GETTING_FILE_LIST) || (action == FR_ACTION_ADDING_FILES)) {
        stop_activity_mode();
        pop_message();
        close_progress_dialog(FALSE);
    }

    if(action != FR_ACTION_ADDING_FILES) {
        return;
    }

    handle_errors(archive, action, error);

    if(error->type == FR_PROC_ERROR_NONE) {
        open_progress_dialog_with_open_archive();
    }

    remove_local_directory(priv->convert_data.temp_dir);
    convert_data_free(FALSE);

    update_sensitivity();
    update_statusbar_list_info();
#endif
}

void
FrWindow::action_performed(FrArchive*   archive,
                           FrAction     action,
                           FrProcError* error,
                           gpointer     data) {
    gboolean  continue_batch = FALSE;
    char*     archive_dir;
    gboolean  temp_dir;

#ifdef DEBUG
    debug(DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

    stop_activity_mode();
    pop_message();

    continue_batch = handle_errors(archive, action, error);

    if((error->type == FR_PROC_ERROR_ASK_PASSWORD)
            || (error->type == FR_PROC_ERROR_UNSUPPORTED_FORMAT)
            /*|| (error->type == FR_PROC_ERROR_BAD_CHARSET)*/) {
        return;
    }

    switch(action) {
    case FR_ACTION_CREATING_NEW_ARCHIVE:
    case FR_ACTION_CREATING_ARCHIVE:
        close_progress_dialog(FALSE);
        if(error->type != FR_PROC_ERROR_STOPPED) {
            history_clear();
            go_to_location("/", TRUE);
            update_dir_tree();
            update_title();
            update_sensitivity();
        }
        break;

    case FR_ACTION_LOADING_ARCHIVE:
        close_progress_dialog(FALSE);
        if(error->type != FR_PROC_ERROR_NONE) {
            remove_from_recent_list(priv->archive_uri);
            if(priv->non_interactive) {
                archive_close();
                stop_batch();
            }
        }
        else {
            add_to_recent_list(priv->archive_uri);
            if(! priv->non_interactive) {
                activateWindow();
            }
        }
        continue_batch = FALSE;
        Q_EMIT archive_loaded(error->type == FR_PROC_ERROR_NONE);

        break;

    case FR_ACTION_LISTING_CONTENT:
        /* update the uri because multi-volume archives can have
         * a different name after loading. */
        g_free(priv->archive_uri);
        priv->archive_uri = g_file_get_uri(archive->file);

        close_progress_dialog(FALSE);
        if(error->type != FR_PROC_ERROR_NONE) {
            remove_from_recent_list(priv->archive_uri);
            archive_close();
            set_password(NULL);
            break;
        }

        archive_dir = remove_level_from_path(priv->archive_uri);
        temp_dir = is_temp_dir(archive_dir);
        if(! priv->archive_present) {
            priv->archive_present = TRUE;

            history_clear();
            history_add("/");

            if(! temp_dir) {
                set_open_default_dir(archive_dir);
                set_add_default_dir(archive_dir);
                if(! priv->freeze_default_dir) {
                    set_extract_default_dir(archive_dir, FALSE);
                }
            }

            priv->archive_new = FALSE;
        }
        g_free(archive_dir);

        if(! temp_dir) {
            add_to_recent_list(priv->archive_uri);
        }

        update_title();
        go_to_location(get_current_location(), TRUE);
        update_dir_tree();
        if(! priv->batch_mode && priv->non_interactive) {
            activateWindow();
        }
        break;

    case FR_ACTION_DELETING_FILES:
        close_progress_dialog(FALSE);
        if(error->type != FR_PROC_ERROR_STOPPED) {
            archive_reload();
        }
        return;

    case FR_ACTION_ADDING_FILES:
        close_progress_dialog(FALSE);

        /* update the uri because multi-volume archives can have
         * a different name after creation. */
        g_free(priv->archive_uri);
        priv->archive_uri = g_file_get_uri(archive->file);

        if(error->type == FR_PROC_ERROR_NONE) {
            if(priv->archive_new) {
                priv->archive_new = FALSE;
            }
            add_to_recent_list(priv->archive_uri);
        }
        if(! priv->batch_mode && (error->type != FR_PROC_ERROR_STOPPED)) {
            archive_reload();
            return;
        }
        break;

    case FR_ACTION_TESTING_ARCHIVE:
        close_progress_dialog(FALSE);
        if(error->type == FR_PROC_ERROR_NONE) {
            view_last_output(_("Test Result"));
        }
        return;

    case FR_ACTION_EXTRACTING_FILES:
        if(error->type != FR_PROC_ERROR_NONE) {
            if(priv->convert_data.converting) {
                remove_local_directory(priv->convert_data.temp_dir);
                convert_data_free(TRUE);
            }
            break;
        }
        if(priv->convert_data.converting) {
            char* source_dir;

            source_dir = g_filename_to_uri(priv->convert_data.temp_dir, NULL, NULL);
            fr_archive_add_with_wildcard(
                priv->convert_data.new_archive,
                "*",
                NULL,
                NULL,
                source_dir,
                NULL,
                FALSE,
                TRUE,
                priv->convert_data.password,
                priv->convert_data.encrypt_header,
                priv->compression,
                priv->convert_data.volume_size);
            g_free(source_dir);
        }
        else {
            if(priv->ask_to_open_destination_after_extraction) {
                open_progress_dialog_with_open_destination();
            }
            else {
                close_progress_dialog(FALSE);
            }
        }
        break;

    default:
        close_progress_dialog(FALSE);
        continue_batch = FALSE;
        break;
    }

    if(priv->batch_action == NULL) {
        update_sensitivity();
        update_statusbar_list_info();
    }

    if(continue_batch) {
        if(error->type != FR_PROC_ERROR_NONE) {
            stop_batch();
        }
        else {
            exec_next_batch_action();
        }
    }
}


/* -- selections -- */


#undef DEBUG_GET_DIR_LIST_FROM_PATH


GList*
FrWindow::get_dir_list_from_path(
    char*     path) {
    char*  dirname;
    int    dirname_l;
    GList* list = NULL;
    int    i;

    if(path[strlen(path) - 1] != '/') {
        dirname = g_strconcat(path, "/", NULL);
    }
    else {
        dirname = g_strdup(path);
    }
    dirname_l = strlen(dirname);
    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fd = (FileData*)g_ptr_array_index(archive->command->files, i);
        gboolean  matches = FALSE;

#ifdef DEBUG_GET_DIR_LIST_FROM_PATH
        g_print("%s <=> %s (%d)\n", dirname, fd->full_path, dirname_l);
#endif

        if(fd->dir) {
            int full_path_l = strlen(fd->full_path);
            if((full_path_l == dirname_l - 1) && (strncmp(dirname, fd->full_path, full_path_l) == 0))
                /* example: dirname is '/path/to/dir/' and fd->full_path is '/path/to/dir' */
            {
                matches = TRUE;
            }
            else if(strcmp(dirname, fd->full_path) == 0) {
                matches = TRUE;
            }
        }

        if(! matches && strncmp(dirname, fd->full_path, dirname_l) == 0) {
            matches = TRUE;
        }

        if(matches) {
#ifdef DEBUG_GET_DIR_LIST_FROM_PATH
            g_print("`-> OK\n");
#endif
            list = g_list_prepend(list, g_strdup(fd->original_path));
        }
    }
    g_free(dirname);

    return g_list_reverse(list);
}


GList*
FrWindow::get_dir_list_from_file_data(
    FileData* fdata) {
    char*  dirname;
    GList* list;

    dirname = g_strconcat(get_current_location(),
                          fdata->list_name,
                          NULL);
    list = get_dir_list_from_path(dirname);
    g_free(dirname);

    return list;
}


GList*
FrWindow::get_file_list_selection(
    gboolean  recursive,
    gboolean* has_dirs) {
    GList*            selections = NULL, *list, *scan;
#if 0
    GtkTreeSelection* selection;



    if(has_dirs != NULL) {
        *has_dirs = FALSE;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    if(selection == NULL) {
        return NULL;
    }
    gtk_tree_selection_selected_foreach(selection, add_selected_from_list_view, &selections);

    list = NULL;
    for(scan = selections; scan; scan = scan->next) {
        FileData* fd = scan->data;

        if(!fd) {
            continue;
        }

        if(file_data_is_dir(fd)) {
            if(has_dirs != NULL) {
                *has_dirs = TRUE;
            }

            if(recursive) {
                list = g_list_concat(list, get_dir_list_from_file_data(fd));
            }
        }
        else {
            list = g_list_prepend(list, g_strdup(fd->original_path));
        }
    }
    if(selections) {
        g_list_free(selections);
    }
#endif
    return g_list_reverse(list);
}


GList*
FrWindow::get_folder_tree_selection(
    gboolean  recursive,
    gboolean* has_dirs) {
#if 0
    GtkTreeSelection* tree_selection;
    GList*            selections, *list, *scan;



    if(has_dirs != NULL) {
        *has_dirs = FALSE;
    }

    tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
    if(tree_selection == NULL) {
        return NULL;
    }

    selections = NULL;
    gtk_tree_selection_selected_foreach(tree_selection, add_selected_from_tree_view, &selections);
    if(selections == NULL) {
        return NULL;
    }

    if(has_dirs != NULL) {
        *has_dirs = TRUE;
    }

    list = NULL;
    for(scan = selections; scan; scan = scan->next) {
        char* path = scan->data;

        if(recursive) {
            list = g_list_concat(list, get_dir_list_from_path(path));
        }
    }
    path_list_free(selections);

    return g_list_reverse(list);
#endif
    return nullptr;
}


GList*
FrWindow::get_file_list_from_path_list(
    GList*    path_list,
    gboolean* has_dirs) {
#if 0
    GtkTreeModel* model;
    GList*        selections, *list, *scan;



    model = GTK_TREE_MODEL(priv->list_store);
    selections = NULL;

    if(has_dirs != NULL) {
        *has_dirs = FALSE;
    }

    for(scan = path_list; scan; scan = scan->next) {
        GtkTreeRowReference* reference = scan->data;
        GtkTreePath*         path;
        GtkTreeIter          iter;
        FileData*            fdata;

        path = gtk_tree_row_reference_get_path(reference);
        if(path == NULL) {
            continue;
        }

        if(! gtk_tree_model_get_iter(model, &iter, path)) {
            continue;
        }

        gtk_tree_model_get(model, &iter,
                           COLUMN_FILE_DATA, &fdata,
                           -1);

        selections = g_list_prepend(selections, fdata);
    }

    list = NULL;
    for(scan = selections; scan; scan = scan->next) {
        FileData* fd = scan->data;

        if(!fd) {
            continue;
        }

        if(file_data_is_dir(fd)) {
            if(has_dirs != NULL) {
                *has_dirs = TRUE;
            }
            list = g_list_concat(list, get_dir_list_from_file_data(fd));
        }
        else {
            list = g_list_prepend(list, g_strdup(fd->original_path));
        }
    }

    if(selections != NULL) {
        g_list_free(selections);
    }

    return g_list_reverse(list);
#endif
    return nullptr;
}


GList*
FrWindow::get_file_list_pattern(
    const char*  pattern) {
    GRegex** regexps;
    GList*   list;
    int      i;

    regexps = search_util_get_regexps(pattern, G_REGEX_CASELESS);
    list = NULL;
    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fd = (FileData*)g_ptr_array_index(archive->command->files, i);
        char*     utf8_name;

        /* FIXME: only files in the current location ? */

        if(fd == NULL) {
            continue;
        }

        utf8_name = g_filename_to_utf8(fd->name, -1, NULL, NULL, NULL);
        if(match_regexps(regexps, utf8_name, (GRegexMatchFlags)0)) {
            list = g_list_prepend(list, g_strdup(fd->original_path));
        }
        g_free(utf8_name);
    }
    free_regexps(regexps);

    return g_list_reverse(list);
}


GList*
FrWindow::get_file_list() {
    GList* list;
    int    i;

    list = NULL;
    for(i = 0; i < archive->command->files->len; i++) {
        FileData* fd = (FileData*)g_ptr_array_index(archive->command->files, i);
        list = g_list_prepend(list, g_strdup(fd->original_path));
    }

    return g_list_reverse(list);
}


int
FrWindow::get_n_selected_files() {
    // FIXME:
    /*
        return _gtk_count_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view)));
    */

    return 0;
}

#if 0
static int
dir_tree_button_press_cb(GtkWidget*      widget,
                         GdkEventButton* event,
                         gpointer        data) {
    FrWindow*         window = data;
    GtkTreeSelection* selection;

    if(event->window != gtk_tree_view_get_bin_window(GTK_TREE_VIEW(priv->tree_view))) {
        return FALSE;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
    if(selection == NULL) {
        return FALSE;
    }

    if((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
        GtkTreePath* path;
        GtkTreeIter  iter;

        if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(priv->tree_view),
                                         event->x, event->y,
                                         &path, NULL, NULL, NULL)) {

            if(! gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->tree_store), &iter, path)) {
                gtk_tree_path_free(path);
                return FALSE;
            }
            gtk_tree_path_free(path);

            if(! gtk_tree_selection_iter_is_selected(selection, &iter)) {
                gtk_tree_selection_unselect_all(selection);
                gtk_tree_selection_select_iter(selection, &iter);
            }

            gtk_menu_popup_at_pointer(GTK_MENU(priv->sidebar_folder_popup_menu),
                                      (const GdkEvent*) event);
        }
        else {
            gtk_tree_selection_unselect_all(selection);
        }

        return TRUE;
    }
    else if((event->type == GDK_BUTTON_PRESS) && (event->button == 8)) {
        go_back();
        return TRUE;
    }
    else if((event->type == GDK_BUTTON_PRESS) && (event->button == 9)) {
        go_forward();
        return TRUE;
    }

    return FALSE;
}
#endif

FileData*
FrWindow::get_selected_item_from_file_list() {
#if 0
    GtkTreeSelection* tree_selection;
    GList*            selection;
    FileData*         fdata = NULL;

    tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    if(tree_selection == NULL) {
        return NULL;
    }

    selection = NULL;
    gtk_tree_selection_selected_foreach(tree_selection, add_selected_from_list_view, &selection);
    if((selection == NULL) || (selection->next != NULL)) {
        /* return NULL if the selection contains more than one entry. */
        g_list_free(selection);
        return NULL;
    }

    fdata = file_data_copy(selection->data);
    g_list_free(selection);

    return fdata;
#endif
    return nullptr;
}


char*
FrWindow::get_selected_folder_in_tree_view() {
#if 0
    GtkTreeSelection* tree_selection;
    GList*            selections;
    char*             path = NULL;

    tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
    if(tree_selection == NULL) {
        return NULL;
    }

    selections = NULL;
    gtk_tree_selection_selected_foreach(tree_selection, add_selected_from_tree_view, &selections);

    if(selections != NULL) {
        path = selections->data;
        g_list_free(selections);
    }

    return path;
#endif
    return nullptr;
}


void
FrWindow::current_folder_activated(
    gboolean   from_sidebar) {
    char* dir_path;

    if(! from_sidebar) {
        FileData* fdata;
        char*     dir_name;

        fdata = get_selected_item_from_file_list();
        if((fdata == NULL) || ! file_data_is_dir(fdata)) {
            file_data_free(fdata);
            return;
        }
        dir_name = g_strdup(fdata->list_name);
        dir_path = g_strconcat(get_current_location(),
                               dir_name,
                               "/",
                               NULL);
        g_free(dir_name);
        file_data_free(fdata);
    }
    else {
        dir_path = get_selected_folder_in_tree_view();
    }

    go_to_location(dir_path, FALSE);

    g_free(dir_path);
}

#if 0
static gboolean
row_activated_cb(GtkTreeView*       tree_view,
                 GtkTreePath*       path,
                 GtkTreeViewColumn* column,
                 gpointer           data) {
    FrWindow*    window = data;
    FileData*    fdata;
    GtkTreeIter  iter;

    if(! gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->list_store),
                                 &iter,
                                 path)) {
        return FALSE;
    }

    gtk_tree_model_get(GTK_TREE_MODEL(priv->list_store), &iter,
                       COLUMN_FILE_DATA, &fdata,
                       -1);

    if(! file_data_is_dir(fdata)) {
        GList* list = g_list_prepend(NULL, fdata->original_path);
        open_files(list, FALSE);
        g_list_free(list);
    }
    else if(priv->list_mode == FR_WINDOW_LIST_MODE_AS_DIR) {
        char* new_dir;
        new_dir = g_strconcat(),
        fdata->list_name,
        "/",
        NULL);
        go_to_location(new_dir, FALSE);
        g_free(new_dir);
    }

    return FALSE;
}


static int
file_button_press_cb(GtkWidget*      widget,
                     GdkEventButton* event,
                     gpointer        data) {
    FrWindow*         window = data;
    GtkTreeSelection* selection;

    if(event->window != gtk_tree_view_get_bin_window(GTK_TREE_VIEW(priv->list_view))) {
        return FALSE;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    if(selection == NULL) {
        return FALSE;
    }

    if(priv->path_clicked != NULL) {
        gtk_tree_path_free(priv->path_clicked);
        priv->path_clicked = NULL;
    }

    if((event->type == GDK_BUTTON_PRESS) && (event->button == 3)) {
        GtkTreePath* path;
        GtkTreeIter  iter;
        int          n_selected;

        if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(priv->list_view),
                                         event->x, event->y,
                                         &path, NULL, NULL, NULL)) {

            if(! gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->list_store), &iter, path)) {
                gtk_tree_path_free(path);
                return FALSE;
            }
            gtk_tree_path_free(path);

            if(! gtk_tree_selection_iter_is_selected(selection, &iter)) {
                gtk_tree_selection_unselect_all(selection);
                gtk_tree_selection_select_iter(selection, &iter);
            }
        }
        else {
            gtk_tree_selection_unselect_all(selection);
        }

        n_selected = get_n_selected_files();
        if((n_selected == 1) && selection_has_a_dir())
            gtk_menu_popup_at_pointer(GTK_MENU(priv->folder_popup_menu),
                                      (const GdkEvent*) event);
        else
            gtk_menu_popup_at_pointer(GTK_MENU(priv->file_popup_menu),
                                      (const GdkEvent*) event);
        return TRUE;
    }
    else if((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
        GtkTreePath* path = NULL;

        if(! gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(priv->list_view),
                                           event->x, event->y,
                                           &path, NULL, NULL, NULL)) {
            gtk_tree_selection_unselect_all(selection);
        }

        if(priv->path_clicked != NULL) {
            gtk_tree_path_free(priv->path_clicked);
            priv->path_clicked = NULL;
        }

        if(path != NULL) {
            priv->path_clicked = gtk_tree_path_copy(path);
            gtk_tree_path_free(path);
        }

        return FALSE;
    }
    else if((event->type == GDK_BUTTON_PRESS) && (event->button == 8)) {
        // go back
        go_back();
        return TRUE;
    }
    else if((event->type == GDK_BUTTON_PRESS) && (event->button == 9)) {
        // go forward
        go_forward();
        return TRUE;
    }

    return FALSE;
}


static int
file_button_release_cb(GtkWidget*      widget,
                       GdkEventButton* event,
                       gpointer        data) {
    FrWindow*         window = data;
    GtkTreeSelection* selection;

    if(event->window != gtk_tree_view_get_bin_window(GTK_TREE_VIEW(priv->list_view))) {
        return FALSE;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    if(selection == NULL) {
        return FALSE;
    }

    if(priv->path_clicked == NULL) {
        return FALSE;
    }

    if((event->type == GDK_BUTTON_RELEASE)
            && (event->button == 1)
            && (priv->path_clicked != NULL)) {
        GtkTreePath* path = NULL;

        if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(priv->list_view),
                                         event->x, event->y,
                                         &path, NULL, NULL, NULL)) {

            if((gtk_tree_path_compare(priv->path_clicked, path) == 0)
                    && priv->single_click
                    && !((event->state & GDK_CONTROL_MASK) || (event->state & GDK_SHIFT_MASK))) {
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),
                                         path,
                                         NULL,
                                         FALSE);
                gtk_tree_view_row_activated(GTK_TREE_VIEW(widget),
                                            path,
                                            NULL);
            }
        }

        if(path != NULL) {
            gtk_tree_path_free(path);
        }
    }

    if(priv->path_clicked != NULL) {
        gtk_tree_path_free(priv->path_clicked);
        priv->path_clicked = NULL;
    }

    return FALSE;
}


static gboolean
file_motion_notify_callback(GtkWidget* widget,
                            GdkEventMotion* event,
                            gpointer user_data) {
    FrWindow*    window = user_data;
    GdkCursor*   cursor;
    GtkTreePath* last_hover_path;
    GdkDisplay*  display;
    GtkTreeIter  iter;

    if(! priv->single_click) {
        return FALSE;
    }

    if(event->window != gtk_tree_view_get_bin_window(GTK_TREE_VIEW(priv->list_view))) {
        return FALSE;
    }

    last_hover_path = priv->list_hover_path;

    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                  event->x, event->y,
                                  &priv->list_hover_path,
                                  NULL, NULL, NULL);

    display = gtk_widget_get_display(GTK_WIDGET(widget));

    if(priv->list_hover_path != NULL) {
        cursor = gdk_cursor_new_for_display(display, GDK_HAND2);
    }
    else {
        cursor = NULL;
    }

    gdk_window_set_cursor(event->window, cursor);

    /* only redraw if the hover row has changed */
    if(!(last_hover_path == NULL && priv->list_hover_path == NULL) &&
            (!(last_hover_path != NULL && priv->list_hover_path != NULL) ||
             gtk_tree_path_compare(last_hover_path, priv->list_hover_path))) {
        if(last_hover_path) {
            gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->list_store),
                                    &iter, last_hover_path);
            gtk_tree_model_row_changed(GTK_TREE_MODEL(priv->list_store),
                                       last_hover_path, &iter);
        }

        if(priv->list_hover_path) {
            gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->list_store),
                                    &iter, priv->list_hover_path);
            gtk_tree_model_row_changed(GTK_TREE_MODEL(priv->list_store),
                                       priv->list_hover_path, &iter);
        }
    }

    gtk_tree_path_free(last_hover_path);

    return FALSE;
}


static gboolean
file_leave_notify_callback(GtkWidget* widget,
                           GdkEventCrossing* event,
                           gpointer user_data) {
    FrWindow*    window = user_data;
    GtkTreeIter  iter;

    if(priv->single_click && (priv->list_hover_path != NULL)) {
        gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->list_store),
                                &iter,
                                priv->list_hover_path);
        gtk_tree_model_row_changed(GTK_TREE_MODEL(priv->list_store),
                                   priv->list_hover_path,
                                   &iter);

        gtk_tree_path_free(priv->list_hover_path);
        priv->list_hover_path = NULL;
    }

    return FALSE;
}

#endif

/* -- drag and drop -- */


static GList*
get_uri_list_from_selection_data(char* uri_list) {
    GList*  list = NULL;
    char**  uris;
    int     i;

    if(uri_list == NULL) {
        return NULL;
    }

    uris = g_uri_list_extract_uris(uri_list);
    for(i = 0; uris[i] != NULL; i++) {
        list = g_list_prepend(list, g_strdup(uris[i]));
    }
    g_strfreev(uris);

    return g_list_reverse(list);
}

#if 0
static gboolean
FrWindow::drag_motion(GtkWidget*      widget,
                      GdkDragContext* context,
                      gint            x,
                      gint            y,
                      guint           time,
                      gpointer        user_data) {
    FrWindow*  window = user_data;

    if((gtk_drag_get_source_widget(context) == priv->list_view)
            || (gtk_drag_get_source_widget(context) == priv->tree_view)) {
        gdk_drag_status(context, 0, time);
        return FALSE;
    }

    return TRUE;
}
#endif


FrClipboardData*
FrWindow::get_clipboard_data_from_selection_data(
    const char* data) {
    FrClipboardData*  clipboard_data;
    char**            uris;
    int               i;

    clipboard_data = fr_clipboard_data_new();

    uris = g_strsplit(data, "\r\n", -1);

    clipboard_data->archive_filename = g_strdup(uris[0]);
    if(priv->password_for_paste != NULL) {
        clipboard_data->archive_password = g_strdup(priv->password_for_paste);
    }
    else if(strcmp(uris[1], "") != 0) {
        clipboard_data->archive_password = g_strdup(uris[1]);
    }
    clipboard_data->op = (strcmp(uris[2], "copy") == 0) ? FR_CLIPBOARD_OP_COPY : FR_CLIPBOARD_OP_CUT;
    clipboard_data->base_dir = g_strdup(uris[3]);
    for(i = 4; uris[i] != NULL; i++)
        if(uris[i][0] != '\0') {
            clipboard_data->files = g_list_prepend(clipboard_data->files, g_strdup(uris[i]));
        }
    clipboard_data->files = g_list_reverse(clipboard_data->files);

    g_strfreev(uris);

    return clipboard_data;
}

#if 0
static void
FrWindow::drag_data_received(GtkWidget*          widget,
                             GdkDragContext*     context,
                             gint                x,
                             gint                y,
                             GtkSelectionData*   data,
                             guint               info,
                             guint               time,
                             gpointer            extra_data) {
    FrWindow*  window = extra_data;
    GList*     list;
    gboolean   one_file;
    gboolean   is_an_archive;

    debug(DEBUG_INFO, "::DragDataReceived -->\n");

    if((gtk_drag_get_source_widget(context) == priv->list_view)
            || (gtk_drag_get_source_widget(context) == priv->tree_view)) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    if(!((gtk_selection_data_get_length(data) >= 0) && (gtk_selection_data_get_format(data) == 8))) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    if(priv->activity_ref > 0) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    gtk_drag_finish(context, TRUE, FALSE, time);

    if(gtk_selection_data_get_target(data) == XFR_ATOM) {
        FrClipboardData* dnd_data;

        dnd_data = get_clipboard_data_from_selection_data((char*) gtk_selection_data_get_data(data));
        dnd_data->current_dir = g_strdup());
        paste_from_clipboard_data(dnd_data);

        return;
    }

    list = get_uri_list_from_selection_data((char*) gtk_selection_data_get_data(data));
    if(list == NULL) {
        GtkWidget* d;

        d = _gtk_error_dialog_new(GTK_WINDOW(),
                                  GTK_DIALOG_MODAL,
                                  NULL,
                                  _("Could not perform the operation"),
                                  NULL);
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);

        return;
    }

    one_file = (list->next == NULL);
    if(one_file) {
        is_an_archive = uri_is_archive(list->data);
    }
    else {
        is_an_archive = FALSE;
    }

    if(priv->archive_present
            && (archive != NULL)
            && ! archive->read_only
            && ! archive->is_compressed_file) {
        if(one_file && is_an_archive) {
            GtkWidget* d;
            gint       r;

            d = _gtk_message_dialog_new(GTK_WINDOW(),
                                        GTK_DIALOG_MODAL,
                                        "dialog-question",
                                        _("Do you want to add this file to the current archive or open it as a new archive?"),
                                        NULL,
                                        "gtk-cancel", GTK_RESPONSE_CANCEL,
                                        "gtk-add", 0,
                                        "gtk-open", 1,
                                        NULL);

            gtk_dialog_set_default_response(GTK_DIALOG(d), 2);

            r = gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(GTK_WIDGET(d));

            if(r == 0) { /* Add */
                archive_add_dropped_items(list, FALSE);
            }
            else if(r == 1) { /* Open */
                archive_open(list->data, GTK_WINDOW());
            }
        }
        else {
            archive_add_dropped_items(list, FALSE);
        }
    }
    else {
        if(one_file && is_an_archive) {
            archive_open(list->data, GTK_WINDOW());
        }
        else {
            GtkWidget* d;
            int        r;

            d = _gtk_message_dialog_new(GTK_WINDOW(),
                                        GTK_DIALOG_MODAL,
                                        "dialog-question",
                                        _("Do you want to create a new archive with these files?"),
                                        NULL,
                                        "gtk-cancel", GTK_RESPONSE_CANCEL,
                                        _("Create _Archive"), GTK_RESPONSE_YES,
                                        NULL);

            gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
            r = gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(GTK_WIDGET(d));

            if(r == GTK_RESPONSE_YES) {
                char*       first_item;
                char*       folder;
                char*       local_path = NULL;
                char*       utf8_path = NULL;
                const char* archive_name;

                free_batch_data();
                append_batch_action(,
                                    FR_BATCH_ACTION_ADD,
                                    path_list_dup(list),
                                    (GFreeFunc) path_list_free);

                first_item = (char*) list->data;
                folder = remove_level_from_path(first_item);
                if(folder != NULL) {
                    set_open_default_dir(folder);
                }

                if((list->next != NULL) && (folder != NULL)) {
                    archive_name = file_name_from_path(folder);
                }
                else {
                    if(uri_is_local(first_item)) {
                        local_path = g_filename_from_uri(first_item, NULL, NULL);
                        if(local_path) {
                            utf8_path = g_filename_to_utf8(local_path, -1, NULL, NULL, NULL);
                        }
                        if(!utf8_path) {
                            utf8_path = g_strdup(first_item);
                        }
                        g_free(local_path);
                    }
                    else {
                        utf8_path = g_strdup(first_item);
                    }
                    archive_name = file_name_from_path(utf8_path);
                }

                show_new_archive_dialog(archive_name);
                g_free(utf8_path);

                g_free(folder);
            }
        }
    }

    path_list_free(list);

    debug(DEBUG_INFO, "::DragDataReceived <--\n");
}


static gboolean
file_list_drag_begin(GtkWidget*          widget,
                     GdkDragContext*     context,
                     gpointer            data) {
    FrWindow* window = data;

    debug(DEBUG_INFO, "::DragBegin -->\n");

    if(priv->activity_ref > 0) {
        return FALSE;
    }

    g_free(priv->drag_destination_folder);
    priv->drag_destination_folder = NULL;

    g_free(priv->drag_base_dir);
    priv->drag_base_dir = NULL;

    gdk_property_change(gdk_drag_context_get_source_window(context),
                        XDS_ATOM, TEXT_ATOM,
                        8, GDK_PROP_MODE_REPLACE,
                        (guchar*) XDS_FILENAME,
                        strlen(XDS_FILENAME));

    return TRUE;
}


static void
file_list_drag_end(GtkWidget*      widget,
                   GdkDragContext* context,
                   gpointer        data) {
    FrWindow* window = data;

    debug(DEBUG_INFO, "::DragEnd -->\n");

    gdk_property_delete(gdk_drag_context_get_source_window(context), XDS_ATOM);

    if(priv->drag_error != NULL) {
        _gtk_error_dialog_run(GTK_WINDOW(),
                              _("Extraction not performed"),
                              "%s",
                              priv->drag_error->message);
        g_clear_error(&priv->drag_error);
    }
    else if(priv->drag_destination_folder != NULL) {
        archive_extract(,
                        priv->drag_file_list,
                        priv->drag_destination_folder,
                        priv->drag_base_dir,
                        FALSE,
                        FR_OVERWRITE_ASK,
                        FALSE,
                        FALSE);
        path_list_free(priv->drag_file_list);
        priv->drag_file_list = NULL;
    }

    debug(DEBUG_INFO, "::DragEnd <--\n");
}


/* The following three functions taken from bugzilla
 * (http://bugzilla.mate.org/attachment.cgi?id=49362&action=view)
 * Author: Christian Neumair
 * Copyright: 2005 Free Software Foundation, Inc
 * License: GPL */
static char*
get_xds_atom_value(GdkDragContext* context) {
    gint actual_length;
    char* data;
    char* ret;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(gdk_drag_context_get_source_window(context) != NULL, NULL);

    if(gdk_property_get(gdk_drag_context_get_source_window(context),
                        XDS_ATOM, TEXT_ATOM,
                        0, MAX_XDS_ATOM_VAL_LEN,
                        FALSE, NULL, NULL, &actual_length,
                        (unsigned char**) &data)) {
        /* add not included \0 to the end of the string */
        ret = g_strndup((gchar*) data, actual_length);
        g_free(data);
        return ret;
    }

    return NULL;
}


static gboolean
context_offers_target(GdkDragContext* context,
                      GdkAtom target) {
    return (g_list_find(gdk_drag_context_list_targets(context), target) != NULL);
}


static gboolean
caja_xds_dnd_is_valid_xds_context(GdkDragContext* context) {
    char* tmp;
    gboolean ret;

    g_return_val_if_fail(context != NULL, FALSE);

    tmp = NULL;
    if(context_offers_target(context, XDS_ATOM)) {
        tmp = get_xds_atom_value(context);
    }

    ret = (tmp != NULL);
    g_free(tmp);

    return ret;
}

#endif


char*
FrWindow::get_selection_data_from_clipboard_data(
    FrClipboardData* data) {
    GString* list;
    char*    local_filename;
    GList*   scan;

    list = g_string_new(NULL);

    local_filename = g_file_get_uri(archive->local_copy);
    g_string_append(list, local_filename);
    g_free(local_filename);

    g_string_append(list, "\r\n");
    if(priv->password != NULL) {
        g_string_append(list, priv->password);
    }
    g_string_append(list, "\r\n");
    g_string_append(list, (data->op == FR_CLIPBOARD_OP_COPY) ? "copy" : "cut");
    g_string_append(list, "\r\n");
    g_string_append(list, data->base_dir);
    g_string_append(list, "\r\n");
    for(scan = data->files; scan; scan = scan->next) {
        g_string_append(list, (char*)scan->data);
        g_string_append(list, "\r\n");
    }

    return g_string_free(list, FALSE);
}


#if 0
static gboolean
FrWindow::folder_tree_drag_data_get(GtkWidget*        widget,
                                    GdkDragContext*   context,
                                    GtkSelectionData* selection_data,
                                    guint             info,
                                    guint             time,
                                    gpointer          user_data) {
    FrWindow* window = user_data;
    GList*    file_list;
    char*     destination;
    char*     destination_folder;

    debug(DEBUG_INFO, "::DragDataGet -->\n");

    if(priv->activity_ref > 0) {
        return FALSE;
    }

    file_list = get_folder_tree_selection(TRUE, NULL);
    if(file_list == NULL) {
        return FALSE;
    }

    if(gtk_selection_data_get_target(selection_data) == XFR_ATOM) {
        FrClipboardData* tmp;
        char*            data;

        tmp = fr_clipboard_data_new();
        tmp->files = file_list;
        tmp->op = FR_CLIPBOARD_OP_COPY;
        tmp->base_dir = g_strdup());

        data = get_selection_data_from_clipboard_data(tmp);
        gtk_selection_data_set(selection_data, XFR_ATOM, 8, (guchar*) data, strlen(data));

        fr_clipboard_data_unref(tmp);
        g_free(data);

        return TRUE;
    }

    if(! caja_xds_dnd_is_valid_xds_context(context)) {
        return FALSE;
    }

    destination = get_xds_atom_value(context);
    g_return_val_if_fail(destination != NULL, FALSE);

    destination_folder = remove_level_from_path(destination);
    g_free(destination);

    /* check whether the extraction can be performed in the destination
     * folder */

    g_clear_error(&priv->drag_error);

    if(! check_permissions(destination_folder, R_OK | W_OK)) {
        char* destination_folder_display_name;

        destination_folder_display_name = g_filename_display_name(destination_folder);
        priv->drag_error = g_error_new(FR_ERROR, 0, _("You don't have the right permissions to extract archives in the folder \"%s\""), destination_folder_display_name);
        g_free(destination_folder_display_name);
    }

    if(priv->drag_error == NULL) {
        g_free(priv->drag_destination_folder);
        g_free(priv->drag_base_dir);
        path_list_free(priv->drag_file_list);
        priv->drag_destination_folder = g_strdup(destination_folder);
        priv->drag_base_dir = get_selected_folder_in_tree_view();
        priv->drag_file_list = file_list;
    }

    g_free(destination_folder);

    /* sends back the response */

    gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), 8, (guchar*)((priv->drag_error == NULL) ? "S" : "E"), 1);

    debug(DEBUG_INFO, "::DragDataGet <--\n");

    return TRUE;
}


gboolean
FrWindow::file_list_drag_data_get(
    GdkDragContext*   context,
    GtkSelectionData* selection_data,
    GList*            path_list) {
    char* destination;
    char* destination_folder;

    debug(DEBUG_INFO, "::DragDataGet -->\n");

    if(priv->path_clicked != NULL) {
        gtk_tree_path_free(priv->path_clicked);
        priv->path_clicked = NULL;
    }

    if(priv->activity_ref > 0) {
        return FALSE;
    }

    if(gtk_selection_data_get_target(selection_data) == XFR_ATOM) {
        FrClipboardData* tmp;
        char*            data;

        tmp = fr_clipboard_data_new();
        tmp->files = get_file_list_selection(TRUE, NULL);
        tmp->op = FR_CLIPBOARD_OP_COPY;
        tmp->base_dir = g_strdup());

        data = get_selection_data_from_clipboard_data(tmp);
        gtk_selection_data_set(selection_data, XFR_ATOM, 8, (guchar*) data, strlen(data));

        fr_clipboard_data_unref(tmp);
        g_free(data);

        return TRUE;
    }

    if(! caja_xds_dnd_is_valid_xds_context(context)) {
        return FALSE;
    }

    destination = get_xds_atom_value(context);
    g_return_val_if_fail(destination != NULL, FALSE);

    destination_folder = remove_level_from_path(destination);
    g_free(destination);

    /* check whether the extraction can be performed in the destination
     * folder */

    g_clear_error(&priv->drag_error);

    if(! check_permissions(destination_folder, R_OK | W_OK)) {
        char* destination_folder_display_name;

        destination_folder_display_name = g_filename_display_name(destination_folder);
        priv->drag_error = g_error_new(FR_ERROR, 0, _("You don't have the right permissions to extract archives in the folder \"%s\""), destination_folder_display_name);
        g_free(destination_folder_display_name);
    }

    if(priv->drag_error == NULL) {
        g_free(priv->drag_destination_folder);
        g_free(priv->drag_base_dir);
        path_list_free(priv->drag_file_list);
        priv->drag_destination_folder = g_strdup(destination_folder);
        priv->drag_base_dir = g_strdup());
        priv->drag_file_list = get_file_list_from_path_list(path_list, NULL);
    }

    g_free(destination_folder);

    /* sends back the response */

    gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), 8, (guchar*)((priv->drag_error == NULL) ? "S" : "E"), 1);

    debug(DEBUG_INFO, "::DragDataGet <--\n");

    return TRUE;
}
#endif


void FrWindow::deactivate_filter() {
    priv->filter_mode = FALSE;
    priv->list_mode = priv->last_list_mode;
#if 0
    gtk_entry_set_text(GTK_ENTRY(priv->filter_entry), "");
    update_filter_bar_visibility();

    gtk_list_store_clear(priv->list_store);

#endif

    update_columns_visibility();
    update_file_list(TRUE);
    update_dir_tree();
    update_current_location();
}


#if 0
static gboolean
key_press_cb(GtkWidget*   widget,
             GdkEventKey* event,
             gpointer     data) {
    FrWindow* window = data;
    gboolean  retval = FALSE;
    gboolean  alt;

    if(gtk_widget_has_focus(priv->location_entry)) {
        return FALSE;
    }

    if(gtk_widget_has_focus(priv->filter_entry)) {
        switch(event->keyval) {
        case GDK_KEY_Escape:
            deactivate_filter();
            retval = TRUE;
            break;
        default:
            break;
        }
        return retval;
    }

    alt = (event->state & GDK_MOD1_MASK) == GDK_MOD1_MASK;

    switch(event->keyval) {
    case GDK_KEY_Escape:
        activate_action_stop(NULL, window);
        if(priv->filter_mode) {
            deactivate_filter();
        }
        retval = TRUE;
        break;

    case GDK_KEY_F10:
        if(event->state & GDK_SHIFT_MASK) {
            GtkTreeSelection* selection;

            selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
            if(selection == NULL) {
                return FALSE;
            }

            gtk_menu_popup_at_pointer(GTK_MENU(priv->file_popup_menu),
                                      (const GdkEvent*) event);
            retval = TRUE;
        }
        break;

    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
        if(alt) {
            go_up_one_level();
            retval = TRUE;
        }
        break;

    case GDK_KEY_BackSpace:
        go_up_one_level();
        retval = TRUE;
        break;

    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
        if(alt) {
            go_forward();
            retval = TRUE;
        }
        break;

    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
        if(alt) {
            go_back();
            retval = TRUE;
        }
        break;

    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
        if(alt) {
            go_to_location("/", FALSE);
            retval = TRUE;
        }
        break;

    default:
        break;
    }

    return retval;
}


static gboolean
dir_tree_selection_changed_cb(GtkTreeSelection* selection,
                              gpointer          user_data) {
    FrWindow*    window = user_data;
    GtkTreeIter  iter;

    if(gtk_tree_selection_get_selected(selection, NULL, &iter)) {
        char* path;

        gtk_tree_model_get(GTK_TREE_MODEL(priv->tree_store),
                           &iter,
                           TREE_COLUMN_PATH, &path,
                           -1);
        go_to_location(path, FALSE);
        g_free(path);
    }

    return FALSE;
}


static gboolean
selection_changed_cb(GtkTreeSelection* selection,
                     gpointer          user_data) {
    FrWindow* window = user_data;

    update_statusbar_list_info();
    update_sensitivity();

    return FALSE;
}


static void
FrWindow::delete_event_cb(GtkWidget* caller,
                          GdkEvent*  event,
                          FrWindow*  window) {
    close();
}


static gboolean
is_single_click_policy() {
    char*     value;
    gboolean  result = FALSE;

    if(priv->settings_caja) {
        value = g_settings_get_string(priv->settings_caja, CAJA_CLICK_POLICY);
        result = (value != NULL) && (strncmp(value, "single", 6) == 0);
        g_free(value);
    }

    return result;
}


static void
filename_cell_data_func(GtkTreeViewColumn* column,
                        GtkCellRenderer*   renderer,
                        GtkTreeModel*      model,
                        GtkTreeIter*       iter,
                        FrWindow*          window) {
    char*           text;
    GtkTreePath*    path;
    PangoUnderline  underline;

    gtk_tree_model_get(model, iter,
                       COLUMN_NAME, &text,
                       -1);

    if(priv->single_click) {
        path = gtk_tree_model_get_path(model, iter);

        if((priv->list_hover_path == NULL)
                || gtk_tree_path_compare(path, priv->list_hover_path)) {
            underline = PANGO_UNDERLINE_NONE;
        }
        else {
            underline = PANGO_UNDERLINE_SINGLE;
        }

        gtk_tree_path_free(path);
    }
    else {
        underline = PANGO_UNDERLINE_NONE;
    }

    g_object_set(G_OBJECT(renderer),
                 "text", text,
                 "underline", underline,
                 NULL);

    g_free(text);
}


static void
add_dir_tree_columns(
    GtkTreeView* treeview) {
    GtkCellRenderer*   renderer;
    GtkTreeViewColumn* column;
    GValue             value = { 0, };

    /* First column. */

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Folders"));

    /* icon */

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "pixbuf", TREE_COLUMN_ICON,
                                        NULL);

    /* name */

    renderer = gtk_cell_renderer_text_new();

    g_value_init(&value, PANGO_TYPE_ELLIPSIZE_MODE);
    g_value_set_enum(&value, PANGO_ELLIPSIZE_END);
    g_object_set_property(G_OBJECT(renderer), "ellipsize", &value);
    g_value_unset(&value);

    gtk_tree_view_column_pack_start(column,
                                    renderer,
                                    TRUE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", TREE_COLUMN_NAME,
                                        "weight", TREE_COLUMN_WEIGHT,
                                        NULL);

    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_column_set_sort_column_id(column, TREE_COLUMN_NAME);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
}


static void
add_file_list_columns(
    GtkTreeView* treeview) {
    static char*       titles[] = {NC_("File", "Size"),
                                   NC_("File", "Type"),
                                   NC_("File", "Date Modified"),
                                   NC_("File", "Location")
                                  };
    GtkCellRenderer*   renderer;
    GtkTreeViewColumn* column;
    GValue             value = { 0, };
    int                i, j, w;

    /* First column. */

    priv->filename_column = column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, C_("File", "Name"));

    /* emblem */

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_end(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "pixbuf", COLUMN_EMBLEM,
                                        NULL);

    /* icon */

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "pixbuf", COLUMN_ICON,
                                        NULL);

    /* name */

    priv->single_click = is_single_click_policy();

    renderer = gtk_cell_renderer_text_new();

    g_value_init(&value, PANGO_TYPE_ELLIPSIZE_MODE);
    g_value_set_enum(&value, PANGO_ELLIPSIZE_END);
    g_object_set_property(G_OBJECT(renderer), "ellipsize", &value);
    g_value_unset(&value);

    gtk_tree_view_column_pack_start(column,
                                    renderer,
                                    TRUE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", COLUMN_NAME,
                                        NULL);

    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    w = g_settings_get_int(priv->settings_listing, PREF_LISTING_NAME_COLUMN_WIDTH);
    if(w <= 0) {
        w = DEFAULT_NAME_COLUMN_WIDTH;
    }
    gtk_tree_view_column_set_fixed_width(column, w);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sort_column_id(column, COLUMN_NAME);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            (GtkTreeCellDataFunc) filename_cell_data_func,
                                            window, NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    /* Other columns */

    for(j = 0, i = COLUMN_SIZE; i < NUMBER_OF_COLUMNS; i++, j++) {
        GValue  value = { 0, };

        renderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes(g_dpgettext2(NULL, "File", titles[j]),
                 renderer,
                 "text", i,
                 NULL);

        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width(column, OTHER_COLUMNS_WIDTH);
        gtk_tree_view_column_set_resizable(column, TRUE);

        gtk_tree_view_column_set_sort_column_id(column, i);

        g_value_init(&value, PANGO_TYPE_ELLIPSIZE_MODE);
        g_value_set_enum(&value, PANGO_ELLIPSIZE_END);
        g_object_set_property(G_OBJECT(renderer), "ellipsize", &value);
        g_value_unset(&value);

        gtk_tree_view_append_column(treeview, column);
    }
}


static int
name_column_sort_func(GtkTreeModel* model,
                      GtkTreeIter*  a,
                      GtkTreeIter*  b,
                      gpointer      user_data) {
    FileData* fdata1, *fdata2;

    gtk_tree_model_get(model, a, COLUMN_FILE_DATA, &fdata1, -1);
    gtk_tree_model_get(model, b, COLUMN_FILE_DATA, &fdata2, -1);

    return sort_by_name(&fdata1, &fdata2);
}


static int
size_column_sort_func(GtkTreeModel* model,
                      GtkTreeIter*  a,
                      GtkTreeIter*  b,
                      gpointer      user_data) {
    FileData* fdata1, *fdata2;

    gtk_tree_model_get(model, a, COLUMN_FILE_DATA, &fdata1, -1);
    gtk_tree_model_get(model, b, COLUMN_FILE_DATA, &fdata2, -1);

    return sort_by_size(&fdata1, &fdata2);
}


static int
type_column_sort_func(GtkTreeModel* model,
                      GtkTreeIter*  a,
                      GtkTreeIter*  b,
                      gpointer      user_data) {
    FileData* fdata1, *fdata2;

    gtk_tree_model_get(model, a, COLUMN_FILE_DATA, &fdata1, -1);
    gtk_tree_model_get(model, b, COLUMN_FILE_DATA, &fdata2, -1);

    return sort_by_type(&fdata1, &fdata2);
}


static int
time_column_sort_func(GtkTreeModel* model,
                      GtkTreeIter*  a,
                      GtkTreeIter*  b,
                      gpointer      user_data) {
    FileData* fdata1, *fdata2;

    gtk_tree_model_get(model, a, COLUMN_FILE_DATA, &fdata1, -1);
    gtk_tree_model_get(model, b, COLUMN_FILE_DATA, &fdata2, -1);

    return sort_by_time(&fdata1, &fdata2);
}


static int
path_column_sort_func(GtkTreeModel* model,
                      GtkTreeIter*  a,
                      GtkTreeIter*  b,
                      gpointer      user_data) {
    FileData* fdata1, *fdata2;

    gtk_tree_model_get(model, a, COLUMN_FILE_DATA, &fdata1, -1);
    gtk_tree_model_get(model, b, COLUMN_FILE_DATA, &fdata2, -1);

    return sort_by_path(&fdata1, &fdata2);
}


static int
no_sort_column_sort_func(GtkTreeModel* model,
                         GtkTreeIter*  a,
                         GtkTreeIter*  b,
                         gpointer      user_data) {
    return -1;
}


static void
sort_column_changed_cb(GtkTreeSortable* sortable,
                       gpointer         user_data) {
    FrWindow*    window = user_data;
    GtkSortType  order;
    int          column_id;

    if(! gtk_tree_sortable_get_sort_column_id(sortable,
            &column_id,
            &order)) {
        return;
    }

    priv->sort_method = get_sort_method_from_column(column_id);
    priv->sort_type = order;

    /*set_active (get_action_from_sort_method (priv->sort_method), TRUE);
    set_active ("SortReverseOrder", (priv->sort_type == GTK_SORT_DESCENDING));*/
}


static gboolean
FrWindow::show_cb(GtkWidget* widget,
                  FrWindow*  window) {
    update_current_location();

    set_active("ViewToolbar", g_settings_get_boolean(priv->settings_ui, PREF_UI_VIEW_TOOLBAR));
    set_active("ViewStatusbar", g_settings_get_boolean(priv->settings_ui, PREF_UI_VIEW_STATUSBAR));

    priv->view_folders = g_settings_get_boolean(priv->settings_ui, PREF_UI_VIEW_FOLDERS);
    set_active("ViewFolders", priv->view_folders);

    update_filter_bar_visibility();

    return TRUE;
}
#endif


/* preferences changes notification callbacks */


void FrWindow::pref_history_len_changed(GSettings* settings,
                                        const char* key,
                                        gpointer user_data) {
    int        limit;
#if 0
    GtkAction* action;

    limit = g_settings_get_int(settings, PREF_UI_HISTORY_LEN);

    action = gtk_action_group_get_action(priv->actions, "OpenRecent");
    gtk_recent_chooser_set_limit(GTK_RECENT_CHOOSER(action), limit);

    action = gtk_action_group_get_action(priv->actions, "OpenRecent_Toolbar");
    gtk_recent_chooser_set_limit(GTK_RECENT_CHOOSER(action), limit);
#endif
}

#if 0
void FrWindow::pref_view_toolbar_changed(GSettings* settings,
        const char* key,
        gpointer user_data) {
    set_toolbar_visibility(g_settings_get_boolean(settings, key));
}


void
FrWindow::pref_view_statusbar_changed(GSettings* settings,
                                      const char* key,
                                      gpointer user_data) {
    set_statusbar_visibility(g_settings_get_boolean(settings, key));
}


static void
pref_view_folders_changed(GSettings* settings,
                          const char* key,
                          gpointer user_data) {
    FrWindow* window = user_data;

    set_folders_visibility(g_settings_get_boolean(settings, key));
}


static void
pref_show_field_changed(GSettings* settings,
                        const char* key,
                        gpointer user_data) {
    FrWindow* window = user_data;

    update_columns_visibility();
}


static void
pref_click_policy_changed(GSettings* settings,
                          const char* key,
                          gpointer user_data) {
    FrWindow*   window = user_data;
    GdkWindow*  win = gtk_tree_view_get_bin_window(GTK_TREE_VIEW(priv->list_view));
    GdkDisplay* display;

    priv->single_click = is_single_click_policy();

    gdk_window_set_cursor(win, NULL);
    display = gtk_widget_get_display(GTK_WIDGET(priv->list_view));
    if(display != NULL) {
        gdk_display_flush(display);
    }
}


static void
pref_use_mime_icons_changed(GSettings* settings,
                            const char* key,
                            gpointer user_data) {
    FrWindow* window = user_data;

    if(pixbuf_hash != NULL) {
        g_hash_table_foreach(pixbuf_hash,
                             gh_unref_pixbuf,
                             NULL);
        g_hash_table_destroy(pixbuf_hash);
        pixbuf_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }
    if(tree_pixbuf_hash != NULL) {
        g_hash_table_foreach(tree_pixbuf_hash,
                             gh_unref_pixbuf,
                             NULL);
        g_hash_table_destroy(tree_pixbuf_hash);
        tree_pixbuf_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }

    update_file_list(FALSE);
    update_dir_tree();
}


static void
theme_changed_cb(GtkIconTheme* theme, FrWindow* window) {
    file_list_icon_size = _gtk_widget_lookup_for_size(GTK_WIDGET(), FILE_LIST_ICON_SIZE);
    dir_tree_icon_size = _gtk_widget_lookup_for_size(GTK_WIDGET(), DIR_TREE_ICON_SIZE);

    if(pixbuf_hash != NULL) {
        g_hash_table_foreach(pixbuf_hash,
                             gh_unref_pixbuf,
                             NULL);
        g_hash_table_destroy(pixbuf_hash);
        pixbuf_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }
    if(tree_pixbuf_hash != NULL) {
        g_hash_table_foreach(tree_pixbuf_hash,
                             gh_unref_pixbuf,
                             NULL);
        g_hash_table_destroy(tree_pixbuf_hash);
        tree_pixbuf_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }

    update_file_list(TRUE);
    update_dir_tree();
}


static gboolean
FrWindow::stoppable_cb(FrCommand*  command,
                       gboolean    stoppable,
                       FrWindow*   window) {
    priv->stoppable = stoppable;
    set_sensitive("Stop", stoppable);
    if(priv->progress_dialog != NULL)
        gtk_dialog_set_response_sensitive(GTK_DIALOG(priv->progress_dialog),
                                          GTK_RESPONSE_OK,
                                          stoppable);
    return TRUE;
}
#endif


gboolean
FrWindow::fake_load(FrArchive* archive,
                    gpointer   data) {
    /* fake loads are disabled to allow exact progress dialogs (#153281) */

    return FALSE;

#if 0
    FrWindow* window = data;
    gboolean  add_after_opening = FALSE;
    gboolean  extract_after_opening = FALSE;
    GList*    scan;

    /* fake loads are used only in batch mode to avoid unnecessary
     * archive loadings. */

    if(! priv->batch_mode) {
        return FALSE;
    }

    /* Check whether there is an ADD or EXTRACT action in the batch list. */

    for(scan = priv->batch_action; scan; scan = scan->next) {
        FRBatchAction* action;

        action = (FRBatchAction*) scan->data;
        if(action->type == FR_BATCH_ACTION_ADD) {
            add_after_opening = TRUE;
            break;
        }
        if((action->type == FR_BATCH_ACTION_EXTRACT)
                || (action->type == FR_BATCH_ACTION_EXTRACT_HERE)
                || (action->type == FR_BATCH_ACTION_EXTRACT_INTERACT)) {
            extract_after_opening = TRUE;
            break;
        }
    }

    /* use fake load when in batch mode and the archive type supports all
     * of the required features */

    return (priv->batch_mode
            && !(add_after_opening && priv->update_dropped_files && ! archive->command->propAddCanUpdate)
            && !(add_after_opening && ! priv->update_dropped_files && ! archive->command->propAddCanReplace)
            && !(extract_after_opening && !archive->command->propCanExtractAll));
#endif
}

#if 0
static void
menu_item_select_cb(GtkMenuItem* proxy,
                    FrWindow*    window) {
    GtkAction* action;
    char*      message;

    action = gtk_activatable_get_related_action(GTK_ACTIVATABLE(proxy));
    g_return_if_fail(action != NULL);

    g_object_get(G_OBJECT(action), "tooltip", &message, NULL);
    if(message) {
        gtk_statusbar_push(GTK_STATUSBAR(priv->statusbar),
                           priv->help_message_cid, message);
        g_free(message);
    }
}


static void
menu_item_deselect_cb(GtkMenuItem* proxy,
                      FrWindow*    window) {
    gtk_statusbar_pop(GTK_STATUSBAR(priv->statusbar),
                      priv->help_message_cid);
}


static void
disconnect_proxy_cb(GtkUIManager* manager,
                    GtkAction*    action,
                    GtkWidget*    proxy,
                    FrWindow*     window) {
    if(GTK_IS_MENU_ITEM(proxy)) {
        g_signal_handlers_disconnect_by_func
        (proxy, G_CALLBACK(menu_item_select_cb), window);
        g_signal_handlers_disconnect_by_func
        (proxy, G_CALLBACK(menu_item_deselect_cb), window);
    }
}


static void
connect_proxy_cb(GtkUIManager* manager,
                 GtkAction*    action,
                 GtkWidget*    proxy,
                 FrWindow*     window) {
    if(GTK_IS_MENU_ITEM(proxy)) {
        g_signal_connect(proxy, "select",
                         G_CALLBACK(menu_item_select_cb), window);
        g_signal_connect(proxy, "deselect",
                         G_CALLBACK(menu_item_deselect_cb), window);
    }
}


static void
view_as_radio_action(GtkAction*      action,
                     GtkRadioAction* current,
                     gpointer        data) {
    FrWindow* window = data;
    set_list_mode(gtk_radio_action_get_current_value(current));
}


static void
sort_by_radio_action(GtkAction*      action,
                     GtkRadioAction* current,
                     gpointer        data) {
    FrWindow* window = data;

    priv->sort_method = gtk_radio_action_get_current_value(current);
    priv->sort_type = GTK_SORT_ASCENDING;
    update_list_order();
}


static void
recent_chooser_item_activated_cb(GtkRecentChooser* chooser,
                                 FrWindow*         window) {
    char* uri;

    uri = gtk_recent_chooser_get_current_uri(chooser);
    if(uri != NULL) {
        archive_open(uri, GTK_WINDOW());
        g_free(uri);
    }
}


static void
FrWindow::init_recent_chooser(
    GtkRecentChooser* chooser) {
    GtkRecentFilter* filter;
    int              i;

    g_return_if_fail(chooser != NULL);

    filter = gtk_recent_filter_new();
    gtk_recent_filter_set_name(filter, _("All archives"));
    for(i = 0; open_type[i] != -1; i++) {
        gtk_recent_filter_add_mime_type(filter, mime_type_desc[open_type[i]].mime_type);
    }
    gtk_recent_filter_add_application(filter, "Engrampa");
    gtk_recent_chooser_add_filter(chooser, filter);

    gtk_recent_chooser_set_local_only(chooser, FALSE);
    gtk_recent_chooser_set_limit(chooser, g_settings_get_int(priv->settings_ui, PREF_UI_HISTORY_LEN));
    gtk_recent_chooser_set_show_not_found(chooser, TRUE);
    gtk_recent_chooser_set_sort_type(chooser, GTK_RECENT_SORT_MRU);

    g_signal_connect(G_OBJECT(chooser),
                     "item_activated",
                     G_CALLBACK(recent_chooser_item_activated_cb),
                     window);
}


static void
close_sidepane_button_clicked_cb(GtkButton* button,
                                 FrWindow*  window) {
    set_folders_visibility(FALSE);
}
#endif

void FrWindow::activate_filter() {
#if 0
    GtkTreeView*       tree_view = GTK_TREE_VIEW(priv->list_view);
    GtkTreeViewColumn* column;

    update_filter_bar_visibility();
    priv->list_mode = FR_WINDOW_LIST_MODE_FLAT;

    gtk_list_store_clear(priv->list_store);

    column = gtk_tree_view_get_column(tree_view, 4);
    gtk_tree_view_column_set_visible(column, TRUE);
#endif
    update_file_list(TRUE);
    update_dir_tree();
    update_current_location();
}

#if 0
static void
filter_entry_activate_cb(GtkEntry* entry,
                         FrWindow* window) {
    activate_filter();
}


static void
filter_entry_icon_release_cb(GtkEntry*             entry,
                             GtkEntryIconPosition  icon_pos,
                             GdkEventButton*       event,
                             gpointer              user_data) {
    FrWindow* window = FR_WINDOW(user_data);

    if((event->button == 1) && (icon_pos == GTK_ENTRY_ICON_SECONDARY)) {
        deactivate_filter();
    }
}

static void
FrWindow::attach(
    GtkWidget*     child,
    FrWindowArea   area) {
    int position;


    g_return_if_fail(FR_IS_WINDOW());
    g_return_if_fail(child != NULL);
    g_return_if_fail(GTK_IS_WIDGET(child));

    switch(area) {
    case FR_WINDOW_AREA_MENUBAR:
        position = 0;
        break;
    case FR_WINDOW_AREA_TOOLBAR:
        position = 1;
        break;
    case FR_WINDOW_AREA_LOCATIONBAR:
        position = 2;
        break;
    case FR_WINDOW_AREA_CONTENTS:
        position = 3;
        if(priv->contents != NULL) {
            gtk_widget_destroy(priv->contents);
        }
        priv->contents = child;
        gtk_widget_set_vexpand(child, TRUE);
        break;
    case FR_WINDOW_AREA_FILTERBAR:
        position = 4;
        break;
    case FR_WINDOW_AREA_STATUSBAR:
        position = 5;
        break;
    default:
        g_critical("%s: area not recognized!", G_STRFUNC);
        return;
        break;
    }

    gtk_widget_set_hexpand(child, TRUE);
    gtk_grid_attach(GTK_GRID(priv->layout),
                    child,
                    0, position,
                    1, 1);
}


static void
set_action_important(GtkUIManager* ui,
                     const char*   action_name) {
    GtkAction* action;

    action = gtk_ui_manager_get_action(ui, action_name);
    g_object_set(action, "is_important", TRUE, NULL);
    g_object_unref(action);
}
#endif

void FrWindow::construct() {
#if 0
    GtkWidget*        menubar;
    GtkWidget*        toolbar;
    GtkWidget*        list_scrolled_window;
    GtkWidget*        location_box;
    GtkStatusbar*     statusbar;
    GtkWidget*        statusbar_box;
    GtkWidget*        filter_box;
    GtkWidget*        tree_scrolled_window;
    GtkWidget*        sidepane_title;
    GtkWidget*        sidepane_title_box;
    GtkWidget*        sidepane_title_label;
    GtkWidget*        close_sidepane_button;
    GtkTreeSelection* selection;
    GtkActionGroup*   actions;
    GtkAction*        action;
    GtkUIManager*     ui;
    GError*           error = NULL;
    GSettingsSchemaSource* schema_source;
    GSettingsSchema*  caja_schema;

    /* data common to all windows. */

    if(pixbuf_hash == NULL) {
        pixbuf_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }
    if(tree_pixbuf_hash == NULL) {
        tree_pixbuf_hash = g_hash_table_new(g_str_hash, g_str_equal);
    }

    if(icon_theme == NULL) {
        icon_theme = gtk_icon_theme_get_default();
    }

    /* Create the settings objects */

    priv->settings_listing = g_settings_new(ENGRAMPA_SCHEMA_LISTING);
    priv->settings_ui = g_settings_new(ENGRAMPA_SCHEMA_UI);
    priv->settings_general = g_settings_new(ENGRAMPA_SCHEMA_GENERAL);
    priv->settings_dialogs = g_settings_new(ENGRAMPA_SCHEMA_DIALOGS);

    schema_source = g_settings_schema_source_get_default();
    caja_schema = g_settings_schema_source_lookup(schema_source, CAJA_SCHEMA, FALSE);
    if(caja_schema) {
        priv->settings_caja = g_settings_new(CAJA_SCHEMA);
        g_settings_schema_unref(caja_schema);
    }

    /* Create the application. */

    priv->layout = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(), priv->layout);
    gtk_widget_show(priv->layout);

    gtk_window_set_title(GTK_WINDOW(), _("Archive Manager"));

    g_signal_connect(G_OBJECT(),
                     "delete_event",
                     G_CALLBACK(FrWindow::delete_event_cb),
                     window);

    g_signal_connect(G_OBJECT(),
                     "show",
                     G_CALLBACK(FrWindow::show_cb),
                     window);

    priv->theme_changed_handler_id =
        g_signal_connect(icon_theme,
                         "changed",
                         G_CALLBACK(theme_changed_cb),
                         window);

    file_list_icon_size = _gtk_widget_lookup_for_size(GTK_WIDGET(), FILE_LIST_ICON_SIZE);
    dir_tree_icon_size = _gtk_widget_lookup_for_size(GTK_WIDGET(), DIR_TREE_ICON_SIZE);

    gtk_window_set_default_size(GTK_WINDOW(),
                                g_settings_get_int(priv->settings_ui, PREF_UI_WINDOW_WIDTH),
                                g_settings_get_int(priv->settings_ui, PREF_UI_WINDOW_HEIGHT));

    gtk_drag_dest_set(GTK_WIDGET(),
                      GTK_DEST_DEFAULT_ALL,
                      target_table, G_N_ELEMENTS(target_table),
                      GDK_ACTION_COPY);

    g_signal_connect(G_OBJECT(),
                     "drag_data_received",
                     G_CALLBACK(FrWindow::drag_data_received),
                     window);
    g_signal_connect(G_OBJECT(),
                     "drag_motion",
                     G_CALLBACK(FrWindow::drag_motion),
                     window);

    g_signal_connect(G_OBJECT(),
                     "key_press_event",
                     G_CALLBACK(key_press_cb),
                     window);

    /* Initialize Data. */

    archive = fr_archive_new();
    g_signal_connect(G_OBJECT(archive),
                     "start",
                     G_CALLBACK(action_started),
                     window);
    g_signal_connect(G_OBJECT(archive),
                     "done",
                     G_CALLBACK(action_performed),
                     window);
    g_signal_connect(G_OBJECT(archive),
                     "progress",
                     G_CALLBACK(FrWindow::progress_cb),
                     window);
    g_signal_connect(G_OBJECT(archive),
                     "message",
                     G_CALLBACK(FrWindow::message_cb),
                     window);
    g_signal_connect(G_OBJECT(archive),
                     "stoppable",
                     G_CALLBACK(FrWindow::stoppable_cb),
                     window);
    g_signal_connect(G_OBJECT(archive),
                     "working_archive",
                     G_CALLBACK(FrWindow::working_archive_cb),
                     window);

    fr_archive_set_fake_load_func(archive,
                                  FrWindow::fake_load,
                                  window);

    priv->sort_method = g_settings_get_enum(priv->settings_listing, PREF_LISTING_SORT_METHOD);
    priv->sort_type = g_settings_get_enum(priv->settings_listing, PREF_LISTING_SORT_TYPE);

    priv->list_mode = priv->last_list_mode = g_settings_get_enum(priv->settings_listing, PREF_LISTING_LIST_MODE);
    g_settings_set_boolean(priv->settings_listing, PREF_LISTING_SHOW_PATH, (priv->list_mode == FR_WINDOW_LIST_MODE_FLAT));

    priv->history = NULL;
    priv->history_current = NULL;

    priv->action = FR_ACTION_NONE;

    priv->open_default_dir = g_strdup(get_home_uri());
    priv->add_default_dir = g_strdup(get_home_uri());
    priv->extract_default_dir = g_strdup(get_home_uri());

    priv->give_focus_to_the_list = FALSE;

    priv->activity_ref = 0;
    priv->activity_timeout_handle = 0;

    priv->update_timeout_handle = 0;

    priv->archive_present = FALSE;
    priv->archive_new = FALSE;
    priv->archive_uri = NULL;

    priv->drag_destination_folder = NULL;
    priv->drag_base_dir = NULL;
    priv->drag_error = NULL;
    priv->drag_file_list = NULL;

    priv->batch_mode = FALSE;
    priv->batch_action_list = NULL;
    priv->batch_action = NULL;
    priv->extract_interact_use_default_dir = FALSE;
    priv->non_interactive = FALSE;

    priv->password = NULL;
    priv->compression = g_settings_get_enum(priv->settings_general, PREF_GENERAL_COMPRESSION_LEVEL);
    priv->encrypt_header = g_settings_get_boolean(priv->settings_general, PREF_GENERAL_ENCRYPT_HEADER);
    priv->volume_size = 0;

    priv->convert_data.converting = FALSE;
    priv->convert_data.temp_dir = NULL;
    priv->convert_data.new_archive = NULL;
    priv->convert_data.password = NULL;
    priv->convert_data.encrypt_header = FALSE;
    priv->convert_data.volume_size = 0;

    priv->stoppable = TRUE;

    priv->batch_adding_one_file = FALSE;

    priv->path_clicked = NULL;

    priv->current_view_length = 0;

    priv->current_batch_action.type = FR_BATCH_ACTION_NONE;
    priv->current_batch_action.data = NULL;
    priv->current_batch_action.free_func = NULL;

    priv->pd_last_archive = NULL;
    priv->pd_last_message = NULL;
    priv->pd_last_fraction = 0.0;

    /* Create the widgets. */

    /* * File list. */

    priv->list_store = fr_list_model_new(NUMBER_OF_COLUMNS,
                                         G_TYPE_POINTER,
                                         GDK_TYPE_PIXBUF,
                                         G_TYPE_STRING,
                                         GDK_TYPE_PIXBUF,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING);
    g_object_set_data(G_OBJECT(priv->list_store), "FrWindow", window);
    priv->list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(priv->list_store));

    add_file_list_columns(GTK_TREE_VIEW(priv->list_view));
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(priv->list_view),
                                    TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(priv->list_view),
                                    COLUMN_NAME);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                    COLUMN_NAME, name_column_sort_func,
                                    NULL, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                    COLUMN_SIZE, size_column_sort_func,
                                    NULL, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                    COLUMN_TYPE, type_column_sort_func,
                                    NULL, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                    COLUMN_TIME, time_column_sort_func,
                                    NULL, NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                    COLUMN_PATH, path_column_sort_func,
                                    NULL, NULL);

    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(priv->list_store),
                                            no_sort_column_sort_func,
                                            NULL, NULL);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    g_signal_connect(selection,
                     "changed",
                     G_CALLBACK(selection_changed_cb),
                     window);
    g_signal_connect(G_OBJECT(priv->list_view),
                     "row_activated",
                     G_CALLBACK(row_activated_cb),
                     window);

    g_signal_connect(G_OBJECT(priv->list_view),
                     "button_press_event",
                     G_CALLBACK(file_button_press_cb),
                     window);
    g_signal_connect(G_OBJECT(priv->list_view),
                     "button_release_event",
                     G_CALLBACK(file_button_release_cb),
                     window);
    g_signal_connect(G_OBJECT(priv->list_view),
                     "motion_notify_event",
                     G_CALLBACK(file_motion_notify_callback),
                     window);
    g_signal_connect(G_OBJECT(priv->list_view),
                     "leave_notify_event",
                     G_CALLBACK(file_leave_notify_callback),
                     window);

    g_signal_connect(G_OBJECT(priv->list_store),
                     "sort_column_changed",
                     G_CALLBACK(sort_column_changed_cb),
                     window);

    g_signal_connect(G_OBJECT(priv->list_view),
                     "drag_begin",
                     G_CALLBACK(file_list_drag_begin),
                     window);
    g_signal_connect(G_OBJECT(priv->list_view),
                     "drag_end",
                     G_CALLBACK(file_list_drag_end),
                     window);
    egg_tree_multi_drag_add_drag_support(GTK_TREE_VIEW(priv->list_view));

    list_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(list_scrolled_window),
                                        GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(list_scrolled_window), priv->list_view);

    /* filter bar */

    priv->filter_bar = filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(filter_box), 3);
    attach(FR_WINDOW(), priv->filter_bar, FR_WINDOW_AREA_FILTERBAR);

    gtk_box_pack_start(GTK_BOX(filter_box),
                       gtk_label_new(_("Find:")), FALSE, FALSE, 0);

    /* * filter entry */

    priv->filter_entry = GTK_WIDGET(gtk_entry_new());
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(priv->filter_entry),
                                      GTK_ENTRY_ICON_SECONDARY,
                                      "edit-clear");

    gtk_widget_set_size_request(priv->filter_entry, 300, -1);
    gtk_box_pack_start(GTK_BOX(filter_box),
                       priv->filter_entry, FALSE, FALSE, 6);

    g_signal_connect(G_OBJECT(priv->filter_entry),
                     "activate",
                     G_CALLBACK(filter_entry_activate_cb),
                     window);
    g_signal_connect(G_OBJECT(priv->filter_entry),
                     "icon-release",
                     G_CALLBACK(filter_entry_icon_release_cb),
                     window);

    gtk_widget_show_all(filter_box);

    /* tree view */

    priv->tree_store = gtk_tree_store_new(TREE_NUMBER_OF_COLUMNS,
                                          G_TYPE_STRING,
                                          GDK_TYPE_PIXBUF,
                                          G_TYPE_STRING,
                                          PANGO_TYPE_WEIGHT);
    priv->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(priv->tree_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(priv->tree_view), FALSE);
    add_dir_tree_columns(GTK_TREE_VIEW(priv->tree_view));

    g_signal_connect(G_OBJECT(priv->tree_view),
                     "button_press_event",
                     G_CALLBACK(dir_tree_button_press_cb),
                     window);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->tree_view));
    g_signal_connect(selection,
                     "changed",
                     G_CALLBACK(dir_tree_selection_changed_cb),
                     window);

    g_signal_connect(G_OBJECT(priv->tree_view),
                     "drag_begin",
                     G_CALLBACK(file_list_drag_begin),
                     window);
    g_signal_connect(G_OBJECT(priv->tree_view),
                     "drag_end",
                     G_CALLBACK(file_list_drag_end),
                     window);
    g_signal_connect(G_OBJECT(priv->tree_view),
                     "drag_data_get",
                     G_CALLBACK(FrWindow::folder_tree_drag_data_get),
                     window);
    gtk_drag_source_set(priv->tree_view,
                        GDK_BUTTON1_MASK,
                        folder_tree_targets, G_N_ELEMENTS(folder_tree_targets),
                        GDK_ACTION_COPY);

    tree_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tree_scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(tree_scrolled_window),
                                        GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(tree_scrolled_window), priv->tree_view);

    /* side pane */

    priv->sidepane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    sidepane_title = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(sidepane_title), GTK_SHADOW_ETCHED_IN);

    sidepane_title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(sidepane_title_box), 2);
    gtk_container_add(GTK_CONTAINER(sidepane_title), sidepane_title_box);
    sidepane_title_label = gtk_label_new(_("Folders"));

    gtk_label_set_xalign(GTK_LABEL(sidepane_title_label), 0.0);
    gtk_box_pack_start(GTK_BOX(sidepane_title_box), sidepane_title_label, TRUE, TRUE, 0);

    close_sidepane_button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(close_sidepane_button),
                      gtk_image_new_from_icon_name("window-close",
                              GTK_ICON_SIZE_MENU));
    gtk_button_set_relief(GTK_BUTTON(close_sidepane_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(close_sidepane_button, _("Close the folders pane"));
    g_signal_connect(close_sidepane_button,
                     "clicked",
                     G_CALLBACK(close_sidepane_button_clicked_cb),
                     window);
    gtk_box_pack_end(GTK_BOX(sidepane_title_box), close_sidepane_button, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(priv->sidepane), sidepane_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(priv->sidepane), tree_scrolled_window, TRUE, TRUE, 0);

    /* main content */

    priv->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(priv->paned), priv->sidepane, FALSE, TRUE);
    gtk_paned_pack2(GTK_PANED(priv->paned), list_scrolled_window, TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(priv->paned), g_settings_get_int(priv->settings_ui, PREF_UI_SIDEBAR_WIDTH));

    attach(FR_WINDOW(), priv->paned, FR_WINDOW_AREA_CONTENTS);
    gtk_widget_show_all(priv->paned);

    /* Build the menu and the toolbar. */

    ui = gtk_ui_manager_new();

    priv->actions = actions = gtk_action_group_new("Actions");

    /* open recent toolbar item action  */

    action = g_object_new(GTK_TYPE_RECENT_ACTION,
                          "name", "OpenRecent",
                          /* Translators: this is the label for the "open recent file" sub-menu. */
                          "label", _("Open _Recent"),
                          "tooltip", _("Open a recently used archive"),
                          "stock-id", "gtk-open",
                          NULL);
    init_recent_chooser(GTK_RECENT_CHOOSER(action));
    gtk_action_group_add_action(actions, action);
    g_object_unref(action);

    /* open recent toolbar item action  */

    action = g_object_new(GTK_TYPE_RECENT_ACTION,
                          "name", "OpenRecent_Toolbar",
                          "label", _("Open"),
                          "tooltip", _("Open a recently used archive"),
                          "stock-id", "gtk-open",
                          "is-important", TRUE,
                          NULL);
    init_recent_chooser(GTK_RECENT_CHOOSER(action));
    g_signal_connect(action,
                     "activate",
                     G_CALLBACK(activate_action_open),
                     window);
    gtk_action_group_add_action(actions, action);
    g_object_unref(action);

    /* other actions */

    gtk_action_group_set_translation_domain(actions, NULL);
    gtk_action_group_add_actions(actions,
                                 action_entries,
                                 n_action_entries,
                                 window);
    gtk_action_group_add_toggle_actions(actions,
                                        action_toggle_entries,
                                        n_action_toggle_entries,
                                        window);
    gtk_action_group_add_radio_actions(actions,
                                       view_as_entries,
                                       n_view_as_entries,
                                       priv->list_mode,
                                       G_CALLBACK(view_as_radio_action),
                                       window);
    gtk_action_group_add_radio_actions(actions,
                                       sort_by_entries,
                                       n_sort_by_entries,
                                       priv->sort_type,
                                       G_CALLBACK(sort_by_radio_action),
                                       window);

    g_signal_connect(ui, "connect_proxy",
                     G_CALLBACK(connect_proxy_cb), window);
    g_signal_connect(ui, "disconnect_proxy",
                     G_CALLBACK(disconnect_proxy_cb), window);

    gtk_ui_manager_insert_action_group(ui, actions, 0);
    gtk_window_add_accel_group(GTK_WINDOW(),
                               gtk_ui_manager_get_accel_group(ui));

    /* Add a hidden short cut Ctrl-Q for power users */
    gtk_accel_group_connect(gtk_ui_manager_get_accel_group(ui),
                            GDK_KEY_q, GDK_CONTROL_MASK, 0,
                            g_cclosure_new_swap(G_CALLBACK(NULL));


    if(! gtk_ui_manager_add_ui_from_resource(ui, "/org/mate/Engrampa/ui/menus-toolbars.ui", &error)) {
    g_message("building menus failed: %s", error->message);
        g_error_free(error);
    }

    menubar = gtk_ui_manager_get_widget(ui, "/MenuBar");
              attach(FR_WINDOW(), menubar, FR_WINDOW_AREA_MENUBAR);
              gtk_widget_show(menubar);

              priv->toolbar = toolbar = gtk_ui_manager_get_widget(ui, "/ToolBar");
              gtk_toolbar_set_show_arrow(GTK_TOOLBAR(toolbar), TRUE);
              gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
              set_action_important(ui, "/ToolBar/Extract_Toolbar");

              /* location bar */

              priv->location_bar = gtk_ui_manager_get_widget(ui, "/LocationBar");
              gtk_toolbar_set_show_arrow(GTK_TOOLBAR(priv->location_bar), FALSE);
              gtk_toolbar_set_style(GTK_TOOLBAR(priv->location_bar), GTK_TOOLBAR_BOTH_HORIZ);
              gtk_style_context_add_class(gtk_widget_get_style_context(priv->location_bar), GTK_STYLE_CLASS_TOOLBAR);
              set_action_important(ui, "/LocationBar/GoBack");

              /* current location */

              location_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
              /* Translators: after the colon there is a folder name. */
              priv->location_label = gtk_label_new_with_mnemonic(_("_Location:"));
              gtk_box_pack_start(GTK_BOX(location_box),
                                 priv->location_label, FALSE, FALSE, 5);

              priv->location_entry = gtk_entry_new();
              gtk_entry_set_icon_from_icon_name(GTK_ENTRY(priv->location_entry),
                      GTK_ENTRY_ICON_PRIMARY,
                      "folder");

              gtk_box_pack_start(GTK_BOX(location_box),
                                 priv->location_entry, TRUE, TRUE, 5);

              g_signal_connect(G_OBJECT(priv->location_entry),
                               "key_press_event",
                               G_CALLBACK(location_entry_key_press_event_cb),
                               window);

    {
        GtkToolItem* tool_item;

        tool_item = gtk_separator_tool_item_new();
        gtk_widget_show_all(GTK_WIDGET(tool_item));
        gtk_toolbar_insert(GTK_TOOLBAR(priv->location_bar), tool_item, -1);

        tool_item = gtk_tool_item_new();
        gtk_tool_item_set_expand(tool_item, TRUE);
        gtk_container_add(GTK_CONTAINER(tool_item), location_box);
        gtk_widget_show_all(GTK_WIDGET(tool_item));
        gtk_toolbar_insert(GTK_TOOLBAR(priv->location_bar), tool_item, -1);
    }

    attach(FR_WINDOW(), priv->location_bar, FR_WINDOW_AREA_LOCATIONBAR);
    if(priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
    gtk_widget_hide(priv->location_bar);
    }
    else {
        gtk_widget_show(priv->location_bar);
    }

    /**/

    attach(FR_WINDOW(), priv->toolbar, FR_WINDOW_AREA_TOOLBAR);
    if(g_settings_get_boolean(priv->settings_ui, PREF_UI_VIEW_TOOLBAR)) {
    gtk_widget_show(toolbar);
    }
    else {
        gtk_widget_hide(toolbar);
    }

    priv->file_popup_menu = gtk_ui_manager_get_widget(ui, "/FilePopupMenu");
                            priv->folder_popup_menu = gtk_ui_manager_get_widget(ui, "/FolderPopupMenu");
                            priv->sidebar_folder_popup_menu = gtk_ui_manager_get_widget(ui, "/SidebarFolderPopupMenu");

                            /* Create the statusbar. */

                            priv->statusbar = gtk_statusbar_new();
                            priv->help_message_cid = gtk_statusbar_get_context_id(GTK_STATUSBAR(priv->statusbar), "help_message");
                            priv->list_info_cid = gtk_statusbar_get_context_id(GTK_STATUSBAR(priv->statusbar), "list_info");
                            priv->progress_cid = gtk_statusbar_get_context_id(GTK_STATUSBAR(priv->statusbar), "progress");

                            statusbar = GTK_STATUSBAR(priv->statusbar);

                            /*reduce size of statusbar */
                            gtk_widget_set_margin_top(GTK_WIDGET(statusbar), 0);
                            gtk_widget_set_margin_bottom(GTK_WIDGET(statusbar), 0);

                            statusbar_box = gtk_statusbar_get_message_area(statusbar);
                            gtk_box_set_homogeneous(GTK_BOX(statusbar_box), FALSE);
                            gtk_box_set_spacing(GTK_BOX(statusbar_box), 4);
                            gtk_box_set_child_packing(GTK_BOX(statusbar_box), gtk_statusbar_get_message_area(statusbar), TRUE, TRUE, 0, GTK_PACK_START);

                            priv->progress_bar = gtk_progress_bar_new();
                            gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(priv->progress_bar), ACTIVITY_PULSE_STEP);
                            gtk_widget_set_size_request(priv->progress_bar, -1, PROGRESS_BAR_HEIGHT);
    {
        GtkWidget* vbox;

        vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(statusbar_box), vbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), priv->progress_bar, TRUE, TRUE, 1);
        gtk_widget_show(vbox);
    }
    gtk_widget_show(statusbar_box);

    attach(FR_WINDOW(), priv->statusbar, FR_WINDOW_AREA_STATUSBAR);
    if(g_settings_get_boolean(priv->settings_ui, PREF_UI_VIEW_STATUSBAR)) {
    gtk_widget_show(priv->statusbar);
    }
    else {
        gtk_widget_hide(priv->statusbar);
    }

    /**/

    update_title();
    update_sensitivity();
    update_file_list(FALSE);
    update_dir_tree();
    update_current_location();
    update_columns_visibility();

    /* Add notification callbacks. */

    g_signal_connect(priv->settings_ui,
                     "changed::" PREF_UI_HISTORY_LEN,
                     G_CALLBACK(pref_history_len_changed),
                     window);
    g_signal_connect(priv->settings_ui,
                     "changed::" PREF_UI_VIEW_TOOLBAR,
                     G_CALLBACK(pref_view_toolbar_changed),
                     window);
    g_signal_connect(priv->settings_ui,
                     "changed::" PREF_UI_VIEW_STATUSBAR,
                     G_CALLBACK(pref_view_statusbar_changed),
                     window);
    g_signal_connect(priv->settings_ui,
                     "changed::" PREF_UI_VIEW_FOLDERS,
                     G_CALLBACK(pref_view_folders_changed),
                     window);
    g_signal_connect(priv->settings_listing,
                     "changed::" PREF_LISTING_SHOW_TYPE,
                     G_CALLBACK(pref_show_field_changed),
                     window);
    g_signal_connect(priv->settings_listing,
                     "changed::" PREF_LISTING_SHOW_SIZE,
                     G_CALLBACK(pref_show_field_changed),
                     window);
    g_signal_connect(priv->settings_listing,
                     "changed::" PREF_LISTING_SHOW_TIME,
                     G_CALLBACK(pref_show_field_changed),
                     window);
    g_signal_connect(priv->settings_listing,
                     "changed::" PREF_LISTING_SHOW_PATH,
                     G_CALLBACK(pref_show_field_changed),
                     window);
    g_signal_connect(priv->settings_listing,
                     "changed::" PREF_LISTING_USE_MIME_ICONS,
                     G_CALLBACK(pref_use_mime_icons_changed),
                     window);

    if(priv->settings_caja)
    g_signal_connect(priv->settings_caja,
                     "changed::" CAJA_CLICK_POLICY,
                     G_CALLBACK(pref_click_policy_changed),
                     window);

    /* Give focus to the list. */

    gtk_widget_grab_focus(priv->list_view);
#endif
}


void FrWindow::set_archive_uri(const char* uri) {
    if(priv->archive_uri != NULL) {
        g_free(priv->archive_uri);
    }
    priv->archive_uri = g_strdup(uri);
}


gboolean
FrWindow::archive_new(const char* uri) {
#if 0
    if(! fr_archive_create(archive, uri)) {
        GtkWindow* file_sel = g_object_get_data(G_OBJECT(), "fr_file_sel");

        priv->load_error_parent_window = file_sel;
        fr_archive_action_completed(archive,
                                    FR_ACTION_CREATING_NEW_ARCHIVE,
                                    FR_PROC_ERROR_GENERIC,
                                    _("Archive type not supported."));

        return FALSE;
    }

    set_archive_uri(uri);
    priv->archive_present = TRUE;
    priv->archive_new = TRUE;

    fr_archive_action_completed(archive,
                                FR_ACTION_CREATING_NEW_ARCHIVE,
                                FR_PROC_ERROR_NONE,
                                NULL);
#endif
    return TRUE;
}

#if 0
FrWindow* FrWindow::archive_open(
    const char* uri,
    GtkWindow*  parent) {
    FrWindow* window = current_window;

    if(current_priv->archive_present) {
        window = (FrWindow*) FrWindow::new();
    }

    g_return_val_if_fail(window != NULL, FALSE);

    archive_close();

    set_archive_uri(uri);
    priv->archive_present = FALSE;
    priv->give_focus_to_the_list = TRUE;
    priv->load_error_parent_window = parent;

    set_current_batch_action(,
                             FR_BATCH_ACTION_LOAD,
                             g_strdup(priv->archive_uri),
                             (GFreeFunc) g_free);

    fr_archive_load(archive, priv->archive_uri, priv->password);

    return window;
}
#endif

void FrWindow::archive_close() {
    if(! priv->archive_new && ! priv->archive_present) {
        return;
    }

    free_open_files();
    fr_clipboard_data_unref(priv->copy_data);
    priv->copy_data = NULL;

    set_password(NULL);
    set_volume_size(0);
    history_clear();

    priv->archive_new = FALSE;
    priv->archive_present = FALSE;

    update_title();
    update_sensitivity();
    update_file_list(FALSE);
    update_dir_tree();
    update_current_location();
    update_statusbar_list_info();
}


const char*
FrWindow::get_archive_uri() {


    return priv->archive_uri;
}


const char*
FrWindow::get_paste_archive_uri() {


    if(priv->clipboard_data != NULL) {
        return priv->clipboard_data->archive_filename;
    }
    else {
        return NULL;
    }
}


gboolean
FrWindow::archive_is_present() {
    return priv->archive_present;
}


typedef struct {
    char*     uri;
    char*     password;
    gboolean  encrypt_header;
    guint     volume_size;
} SaveAsData;


static SaveAsData*
save_as_data_new(const char* uri,
                 const char* password,
                 gboolean    encrypt_header,
                 guint       volume_size) {
    SaveAsData* sdata;

    sdata = g_new0(SaveAsData, 1);
    if(uri != NULL) {
        sdata->uri = g_strdup(uri);
    }
    if(password != NULL) {
        sdata->password = g_strdup(password);
    }
    sdata->encrypt_header = encrypt_header;
    sdata->volume_size = volume_size;

    return sdata;
}


static void
save_as_data_free(SaveAsData* sdata) {
    if(sdata == NULL) {
        return;
    }
    g_free(sdata->uri);
    g_free(sdata->password);
    g_free(sdata);
}


void
FrWindow::archive_save_as(
    const char* uri,
    const char* password,
    gboolean    encrypt_header,
    guint       volume_size) {

#if 0
    g_return_if_fail(uri != NULL);
    g_return_if_fail(archive != NULL);

    convert_data_free(TRUE);
    priv->convert_data.new_file = g_strdup(uri);

    /* create the new archive */

    priv->convert_data.new_archive = fr_archive_new();
    if(! fr_archive_create(priv->convert_data.new_archive, uri)) {
        GtkWidget* d;
        char*      utf8_name;
        char*      message;

        utf8_name = g_uri_display_basename(uri);
        message = g_strdup_printf(_("Could not save the archive \"%s\""), utf8_name);
        g_free(utf8_name);

        d = _gtk_error_dialog_new(GTK_WINDOW(),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  NULL,
                                  message,
                                  "%s",
                                  _("Archive type not supported."));
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);

        g_free(message);

        g_object_unref(priv->convert_data.new_archive);
        priv->convert_data.new_archive = NULL;

        return;
    }

    g_return_if_fail(priv->convert_data.new_archive->command != NULL);

    if(password != NULL) {
        priv->convert_data.password = g_strdup(password);
        priv->convert_data.encrypt_header = encrypt_header;
    }
    else {
        priv->convert_data.encrypt_header = FALSE;
    }
    priv->convert_data.volume_size = volume_size;

    set_current_batch_action(,
                             FR_BATCH_ACTION_SAVE_AS,
                             save_as_data_new(uri, password, encrypt_header, volume_size),
                             (GFreeFunc) save_as_data_free);

    g_signal_connect(G_OBJECT(priv->convert_data.new_archive),
                     "start",
                     G_CALLBACK(action_started),
                     window);
    g_signal_connect(G_OBJECT(priv->convert_data.new_archive),
                     "done",
                     G_CALLBACK(convert__action_performed),
                     window);
    g_signal_connect(G_OBJECT(priv->convert_data.new_archive),
                     "progress",
                     G_CALLBACK(FrWindow::progress_cb),
                     window);
    g_signal_connect(G_OBJECT(priv->convert_data.new_archive),
                     "message",
                     G_CALLBACK(FrWindow::message_cb),
                     window);
    g_signal_connect(G_OBJECT(priv->convert_data.new_archive),
                     "stoppable",
                     G_CALLBACK(FrWindow::stoppable_cb),
                     window);

    priv->convert_data.converting = TRUE;
    priv->convert_data.temp_dir = get_temp_work_dir(NULL);

    fr_process_clear(archive->process);
    fr_archive_extract_to_local(archive,
                                NULL,
                                priv->convert_data.temp_dir,
                                NULL,
                                TRUE,
                                FALSE,
                                FALSE,
                                priv->password);
    fr_process_start(archive->process);
#endif
}


void FrWindow::archive_reload() {
    if(priv->activity_ref > 0) {
        return;
    }
    if(priv->archive_new) {
        return;
    }

    fr_archive_reload(archive, priv->password);
}


void
FrWindow::archive_add_files(
    GList*    file_list, /* GFile list */
    gboolean  update) {
    GFile* base;
    char*  base_dir;
    int    base_len;
    GList* files = NULL;
    GList* scan;
    char*  base_uri;

    base = g_file_get_parent((GFile*) file_list->data);
    base_dir = g_file_get_path(base);
    base_len = 0;
    if(strcmp(base_dir, "/") != 0) {
        base_len = strlen(base_dir);
    }

    for(scan = file_list; scan; scan = scan->next) {
        GFile* file = (GFile*)scan->data;
        char*  path;
        char*  rel_path;

        path = g_file_get_path(file);
        rel_path = g_strdup(path + base_len + 1);
        files = g_list_prepend(files, rel_path);

        g_free(path);
    }

    base_uri = g_file_get_uri(base);

    fr_archive_add_files(archive,
                         files,
                         base_uri,
                         get_current_location(),
                         update,
                         priv->password,
                         priv->encrypt_header,
                         priv->compression,
                         priv->volume_size);

    g_free(base_uri);
    path_list_free(files);
    g_free(base_dir);
    g_object_unref(base);
}


void
FrWindow::archive_add_with_wildcard(
    const char*    include_files,
    const char*    exclude_files,
    const char*    exclude_folders,
    const char*    base_dir,
    const char*    dest_dir,
    gboolean       update,
    gboolean       follow_links) {
    fr_archive_add_with_wildcard(archive,
                                 include_files,
                                 exclude_files,
                                 exclude_folders,
                                 base_dir,
                                 (dest_dir == NULL) ? get_current_location() : dest_dir,
                                 update,
                                 follow_links,
                                 priv->password,
                                 priv->encrypt_header,
                                 priv->compression,
                                 priv->volume_size);
}


void
FrWindow::archive_add_directory(
    const char*    directory,
    const char*    base_dir,
    const char*    dest_dir,
    gboolean       update) {
    fr_archive_add_directory(archive,
                             directory,
                             base_dir,
                             (dest_dir == NULL) ? get_current_location() : dest_dir,
                             update,
                             priv->password,
                             priv->encrypt_header,
                             priv->compression,
                             priv->volume_size);
}


void
FrWindow::archive_add_items(
    GList*         item_list,
    const char*    base_dir,
    const char*    dest_dir,
    gboolean       update) {
    fr_archive_add_items(archive,
                         item_list,
                         base_dir,
                         (dest_dir == NULL) ? get_current_location() : dest_dir,
                         update,
                         priv->password,
                         priv->encrypt_header,
                         priv->compression,
                         priv->volume_size);
}


void
FrWindow::archive_add_dropped_items(
    GList*    item_list,
    gboolean  update) {
    fr_archive_add_dropped_items(archive,
                                 item_list,
                                 get_current_location(),
                                 get_current_location(),
                                 update,
                                 priv->password,
                                 priv->encrypt_header,
                                 priv->compression,
                                 priv->volume_size);
}


void
FrWindow::archive_remove(
    GList*         file_list) {
    clipboard_remove_file_list(file_list);

    fr_process_clear(archive->process);
    fr_archive_remove(archive, file_list, priv->compression);
    fr_process_start(archive->process);
}


/* -- window_archive_extract -- */


static ExtractData*
extract_data_new(GList*       file_list,
                 const char*  extract_to_dir,
                 const char*  base_dir,
                 gboolean     skip_older,
                 FrOverwrite  overwrite,
                 gboolean     junk_paths,
                 gboolean     extract_here,
                 gboolean     ask_to_open_destination) {
    ExtractData* edata;

    edata = g_new0(ExtractData, 1);
    edata->file_list = path_list_dup(file_list);
    if(extract_to_dir != NULL) {
        edata->extract_to_dir = g_strdup(extract_to_dir);
    }
    edata->skip_older = skip_older;
    edata->overwrite = overwrite;
    edata->junk_paths = junk_paths;
    if(base_dir != NULL) {
        edata->base_dir = g_strdup(base_dir);
    }
    edata->extract_here = extract_here;
    edata->ask_to_open_destination = ask_to_open_destination;

    return edata;
}


static ExtractData*
extract_to_data_new(const char* extract_to_dir) {
    return extract_data_new(NULL,
                            extract_to_dir,
                            NULL,
                            FALSE,
                            FR_OVERWRITE_NO,
                            FALSE,
                            FALSE,
                            FALSE);
}


static void
extract_data_free(ExtractData* edata) {
    g_return_if_fail(edata != NULL);

    path_list_free(edata->file_list);
    g_free(edata->extract_to_dir);
    g_free(edata->base_dir);

    g_free(edata);
}


gboolean
FrWindow::archive_is_encrypted(
    GList*    file_list) {
    gboolean encrypted = FALSE;

    if(file_list == NULL) {
        int i;

        for(i = 0; ! encrypted && i < archive->command->files->len; i++) {
            FileData* fdata = (FileData*)g_ptr_array_index(archive->command->files, i);

            if(fdata->encrypted) {
                encrypted = TRUE;
            }
        }
    }
    else {

        GHashTable* file_hash;
        int         i;
        GList*      scan;

        file_hash = g_hash_table_new(g_str_hash, g_str_equal);
        for(i = 0; i < archive->command->files->len; i++) {
            FileData* fdata = (FileData*)g_ptr_array_index(archive->command->files, i);
            g_hash_table_insert(file_hash, fdata->original_path, fdata);
        }

        for(scan = file_list; ! encrypted && scan; scan = scan->next) {
            char*     filename = (char*)scan->data;
            FileData* fdata;

            fdata = (FileData*)g_hash_table_lookup(file_hash, filename);
            g_return_val_if_fail(fdata != NULL, FALSE);

            if(fdata->encrypted) {
                encrypted = TRUE;
            }
        }

        g_hash_table_destroy(file_hash);
    }

    return encrypted;
}


void FrWindow::archive_extract_here(
    gboolean    skip_older,
    FrOverwrite    overwrite,
    gboolean    junk_paths) {
    ExtractData* edata;

    edata = extract_data_new(NULL,
                             NULL,
                             NULL,
                             skip_older,
                             overwrite,
                             junk_paths,
                             TRUE,
                             FALSE);
    set_current_batch_action(FR_BATCH_ACTION_EXTRACT,
                             edata,
                             (GFreeFunc) extract_data_free);

    if(archive_is_encrypted(NULL) && (priv->password == NULL)) {
#if 0
        // FIXME:
        // dlg_ask_password();
#endif
        return;
    }

    priv->ask_to_open_destination_after_extraction = edata->ask_to_open_destination;

    fr_process_clear(archive->process);
    if(fr_archive_extract_here(archive,
                               edata->skip_older,
                               edata->overwrite,
                               edata->junk_paths,
                               priv->password)) {
        fr_process_start(archive->process);
    }
}


/* -- FrWindow::archive_extract -- */


struct OverwriteData {
    FrWindow*    window;
    ExtractData* edata;
    GList*       current_file;
    gboolean     extract_all;
};


#define _FR_RESPONSE_OVERWRITE_YES_ALL 100
#define _FR_RESPONSE_OVERWRITE_YES     101
#define _FR_RESPONSE_OVERWRITE_NO      102


void FrWindow::archive_extract_from_edata(
    ExtractData* edata) {
    priv->ask_to_open_destination_after_extraction = edata->ask_to_open_destination;

    fr_process_clear(archive->process);
    fr_archive_extract(archive,
                       edata->file_list,
                       edata->extract_to_dir,
                       edata->base_dir,
                       edata->skip_older,
                       edata->overwrite == FR_OVERWRITE_YES,
                       edata->junk_paths,
                       priv->password);
    fr_process_start(archive->process);
}

#if 0
static void
overwrite_dialog_response_cb(GtkDialog* dialog,
                             int        response_id,
                             gpointer   user_data) {
    OverwriteData* odata = user_data;
    gboolean       do_not_extract = FALSE;

    switch(response_id) {
    case _FR_RESPONSE_OVERWRITE_YES_ALL:
        odata->edata->overwrite = FR_OVERWRITE_YES;
        break;

    case _FR_RESPONSE_OVERWRITE_YES:
        odata->current_file = odata->current_file->next;
        break;

    case _FR_RESPONSE_OVERWRITE_NO: {
        /* remove the file from the list to extract */
        GList* next = odata->current_file->next;
        odata->edata->file_list = g_list_remove_link(odata->edata->file_list, odata->current_file);
        path_list_free(odata->current_file);
        odata->current_file = next;
        odata->extract_all = FALSE;
    }
    break;

    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CANCEL:
        do_not_extract = TRUE;
        break;

    default:
        break;
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));

    if(do_not_extract) {
        FrWindow::stop_batch(odata->window);
        g_free(odata);
        return;
    }

    _FrWindow::ask_overwrite_dialog(odata);
}
#endif

void FrWindow::ask_overwrite_dialog(OverwriteData* odata) {
#if 0
    while((odata->edata->overwrite == FR_OVERWRITE_ASK) && (odata->current_file != NULL)) {
        const char* base_name;
        char*       e_base_name;
        char*       dest_uri;
        GFile*      file;
        GFileInfo*  info;
        GFileType   file_type;

        base_name = _g_path_get_base_name((char*) odata->current_file->data, odata->edata->base_dir, odata->edata->junk_paths);
        e_base_name = g_uri_escape_string(base_name, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, TRUE);
        dest_uri = g_strdup_printf("%s/%s", odata->edata->extract_to_dir, e_base_name);
        file = g_file_new_for_uri(dest_uri);
        info = g_file_query_info(file,
                                 G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                 NULL,
                                 NULL);

        g_free(dest_uri);
        g_free(e_base_name);

        if(info == NULL) {
            g_object_unref(file);
            odata->current_file = odata->current_file->next;
            continue;
        }

        file_type = g_file_info_get_file_type(info);
        if((file_type != G_FILE_TYPE_UNKNOWN) && (file_type != G_FILE_TYPE_DIRECTORY)) {
            char*      msg;
            GFile*     parent;
            char*      parent_name;
            char*      details;
            GtkWidget* d;

            msg = g_strdup_printf(_("Replace file \"%s\"?"), g_file_info_get_display_name(info));
            parent = g_file_get_parent(file);
            parent_name = g_file_get_parse_name(parent);
            details = g_strdup_printf(_("Another file with the same name already exists in \"%s\"."), parent_name);
            d = _gtk_message_dialog_new(GTK_WINDOW(odata->window),
                                        GTK_DIALOG_MODAL,
                                        "dialog-question",
                                        msg,
                                        details,
                                        "gtk-cancel", GTK_RESPONSE_CANCEL,
                                        _("Replace _All"), _FR_RESPONSE_OVERWRITE_YES_ALL,
                                        _("_Skip"), _FR_RESPONSE_OVERWRITE_NO,
                                        _("_Replace"), _FR_RESPONSE_OVERWRITE_YES,
                                        NULL);
            gtk_dialog_set_default_response(GTK_DIALOG(d), _FR_RESPONSE_OVERWRITE_YES);
            g_signal_connect(d,
                             "response",
                             G_CALLBACK(overwrite_dialog_response_cb),
                             odata);
            gtk_widget_show(d);

            g_free(parent_name);
            g_object_unref(parent);
            g_object_unref(info);
            g_object_unref(file);

            return;
        }
        else {
            odata->current_file = odata->current_file->next;
        }

        g_object_unref(info);
        g_object_unref(file);
    }

    if(odata->edata->file_list != NULL) {
        /* speed optimization: passing NULL when extracting all the
         * files is faster if the command supports the
         * propCanExtractAll property. */
        if(odata->extract_all) {
            path_list_free(odata->edata->file_list);
            odata->edata->file_list = NULL;
        }
        odata->edata->overwrite = FR_OVERWRITE_YES;
        _FrWindow::archive_extract_from_edata(odata->window, odata->edata);
    }
    else {
        GtkWidget* d;

        d = _gtk_message_dialog_new(GTK_WINDOW(odata->window),
                                    0,
                                    "dialog-warning",
                                    _("Extraction not performed"),
                                    NULL,
                                    "gtk-ok", GTK_RESPONSE_OK,
                                    NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
        FrWindow::show_error_dialog(odata->window, d, GTK_WINDOW(odata->window), _("Extraction not performed"));

        FrWindow::stop_batch(odata->window);
    }
    g_free(odata);
#endif
}


void
FrWindow::archive_extract(
    GList*       file_list,
    const char*  extract_to_dir,
    const char*  base_dir,
    gboolean     skip_older,
    FrOverwrite  overwrite,
    gboolean     junk_paths,
    gboolean     ask_to_open_destination) {
    ExtractData* edata;
    gboolean     do_not_extract = FALSE;
    GError*      error = NULL;

#if 0
    edata = extract_data_new(file_list,
                             extract_to_dir,
                             base_dir,
                             skip_older,
                             overwrite,
                             junk_paths,
                             FALSE,
                             ask_to_open_destination);

    set_current_batch_action(,
                             FR_BATCH_ACTION_EXTRACT,
                             edata,
                             (GFreeFunc) extract_data_free);

    if(archive_is_encrypted(edata->file_list) && (priv->password == NULL)) {
        dlg_ask_password();
        return;
    }

    if(! uri_is_dir(edata->extract_to_dir)) {

        /* There is nothing to ask if the destination doesn't exist. */
        if(edata->overwrite == FR_OVERWRITE_ASK) {
            edata->overwrite = FR_OVERWRITE_YES;
        }

        if(! ForceDirectoryCreation) {
            GtkWidget* d;
            int        r;
            char*      folder_name;
            char*      msg;

            folder_name = g_filename_display_name(edata->extract_to_dir);
            msg = g_strdup_printf(_("Destination folder \"%s\" does not exist.\n\nDo you want to create it?"), folder_name);
            g_free(folder_name);

            d = _gtk_message_dialog_new(GTK_WINDOW(),
                                        GTK_DIALOG_MODAL,
                                        "dialog-question",
                                        msg,
                                        NULL,
                                        "gtk-cancel", GTK_RESPONSE_CANCEL,
                                        _("Create _Folder"), GTK_RESPONSE_YES,
                                        NULL);

            gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
            r = gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(GTK_WIDGET(d));

            g_free(msg);

            if(r != GTK_RESPONSE_YES) {
                do_not_extract = TRUE;
            }
        }

        if(! do_not_extract && ! ensure_dir_exists(edata->extract_to_dir, 0755, &error)) {
            GtkWidget* d;
            char*      details;

            details = g_strdup_printf(_("Could not create the destination folder: %s."), error->message);
            d = _gtk_error_dialog_new(GTK_WINDOW(),
                                      0,
                                      NULL,
                                      _("Extraction not performed"),
                                      "%s",
                                      details);
            g_clear_error(&error);
            show_error_dialog(d, GTK_WINDOW(), details);
            stop_batch();

            g_free(details);

            return;
        }
    }

    if(do_not_extract) {
        GtkWidget* d;

        d = _gtk_message_dialog_new(GTK_WINDOW(),
                                    0,
                                    "dialog-warning",
                                    _("Extraction not performed"),
                                    NULL,
                                    "gtk-ok", GTK_RESPONSE_OK,
                                    NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
        show_error_dialog(d, GTK_WINDOW(), _("Extraction not performed"));
        stop_batch();

        return;
    }

    if(edata->overwrite == FR_OVERWRITE_ASK) {
        OverwriteData* odata;

        odata = g_new0(OverwriteData, 1);
        odata->window = window;
        odata->edata = edata;
        odata->extract_all = (edata->file_list == NULL) || (g_list_length(edata->file_list) == archive->command->files->len);
        if(edata->file_list == NULL) {
            edata->file_list = get_file_list();
        }
        odata->current_file = odata->edata->file_list;
        _FrWindow::ask_overwrite_dialog(odata);
    }
    else {
        _archive_extract_from_edata(edata);
    }
#endif
}


void
FrWindow::archive_test() {
    set_current_batch_action(FR_BATCH_ACTION_TEST,
                             NULL,
                             NULL);
    fr_archive_test(archive, priv->password);
}


void
FrWindow::set_password(
    const char* password) {


    if(priv->password != NULL) {
        g_free(priv->password);
        priv->password = NULL;
    }

    if((password != NULL) && (password[0] != '\0')) {
        priv->password = g_strdup(password);
    }
}

void
FrWindow::set_password_for_paste(
    const char* password) {
    if(priv->password_for_paste != NULL) {
        g_free(priv->password_for_paste);
        priv->password_for_paste = NULL;
    }

    if((password != NULL) && (password[0] != '\0')) {
        priv->password_for_paste = g_strdup(password);
    }
}

const char*
FrWindow::get_password() {
    return priv->password;
}


void
FrWindow::set_encrypt_header(
    gboolean  encrypt_header) {
    priv->encrypt_header = encrypt_header;
}


gboolean
FrWindow::get_encrypt_header() {
    return priv->encrypt_header;
}


void
FrWindow::set_compression(
    FrCompression  compression) {


    priv->compression = compression;
}


FrCompression
FrWindow::get_compression() {
    return priv->compression;
}


void
FrWindow::set_volume_size(
    guint     volume_size) {


    priv->volume_size = volume_size;
}


guint
FrWindow::get_volume_size() {
    return priv->volume_size;
}


void
FrWindow::go_to_location(
    const char* path,
    gboolean    force_update) {
    char* dir;


    g_return_if_fail(path != NULL);

    if(force_update) {
        g_free(priv->last_location);
        priv->last_location = NULL;
    }

    if(path[strlen(path) - 1] != '/') {
        dir = g_strconcat(path, "/", NULL);
    }
    else {
        dir = g_strdup(path);
    }

    if((priv->last_location == NULL) || (strcmp(priv->last_location, dir) != 0)) {
        g_free(priv->last_location);
        priv->last_location = dir;

        history_add(dir);
        update_file_list(TRUE);
        update_current_location();
    }
    else {
        g_free(dir);
    }
}


const char*
FrWindow::get_current_location() {
    if(priv->history_current == NULL) {
        history_add("/");
        return (char*)priv->history_current->data;
    }
    else {
        return (const char*) priv->history_current->data;
    }
}


void
FrWindow::go_up_one_level() {
    char* parent_dir;
    parent_dir = get_parent_dir(get_current_location());
    go_to_location(parent_dir, FALSE);
    g_free(parent_dir);
}


void
FrWindow::go_back() {


    if(priv->history == NULL) {
        return;
    }
    if(priv->history_current == NULL) {
        return;
    }
    if(priv->history_current->next == NULL) {
        return;
    }
    priv->history_current = priv->history_current->next;

    go_to_location((char*)priv->history_current->data, FALSE);
}


void
FrWindow::go_forward() {


    if(priv->history == NULL) {
        return;
    }
    if(priv->history_current == NULL) {
        return;
    }
    if(priv->history_current->prev == NULL) {
        return;
    }
    priv->history_current = priv->history_current->prev;

    go_to_location((char*)priv->history_current->data, FALSE);
}


void
FrWindow::set_list_mode(
    FrWindowListMode  list_mode) {


    priv->list_mode = priv->last_list_mode = list_mode;
    if(priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
        history_clear();
        history_add("/");
    }

    g_settings_set_enum(priv->settings_listing, PREF_LISTING_LIST_MODE, priv->last_list_mode);
    g_settings_set_boolean(priv->settings_listing, PREF_LISTING_SHOW_PATH, (priv->list_mode == FR_WINDOW_LIST_MODE_FLAT));

    update_file_list(TRUE);
    update_dir_tree();
    update_current_location();
}

#if 0
GtkTreeModel*
FrWindow::get_list_store() {
    return GTK_TREE_MODEL(priv->list_store);
}
#endif

void FrWindow::find() {
    priv->filter_mode = TRUE;
#if 0
    gtk_widget_show(priv->filter_bar);
    gtk_widget_grab_focus(priv->filter_entry);
#endif
}


void
FrWindow::select_all() {
//    gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view)));
}


void
FrWindow::unselect_all() {
//    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->list_view)));
}

#if 0
void
FrWindow::set_sort_type(
    GtkSortType   sort_type) {
    priv->sort_type = sort_type;
    update_list_order();
}
#endif

void
FrWindow::stop() {
    if(! priv->stoppable) {
        return;
    }

    if(priv->activity_ref > 0) {
        fr_archive_stop(archive);
    }

    if(priv->convert_data.converting) {
        convert_data_free(TRUE);
    }
}


/* -- start/stop activity mode -- */


int FrWindow::activity_cb(gpointer data) {
#if 0
    if((priv->pd_progress_bar != NULL) && priv->progress_pulse) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(priv->pd_progress_bar));
    }
    if(priv->progress_pulse) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(priv->progress_bar));
    }
#endif
    return TRUE;
}


void
FrWindow::start_activity_mode() {
#if 0

    if(priv->activity_ref++ > 0) {
        return;
    }

    priv->activity_timeout_handle = g_timeout_add(ACTIVITY_DELAY,
                                    activity_cb,
                                    window);
    update_sensitivity();
#endif
}


void
FrWindow::stop_activity_mode() {
#if 0

    if(priv->activity_ref == 0) {
        return;
    }

    priv->activity_ref--;

    if(priv->activity_ref > 0) {
        return;
    }

    if(priv->activity_timeout_handle == 0) {
        return;
    }

    g_source_remove(priv->activity_timeout_handle);
    priv->activity_timeout_handle = 0;

    if(! gtk_widget_get_realized(GTK_WIDGET())) {
        return;
    }

    if(priv->progress_dialog != NULL) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->pd_progress_bar), 0.0);
    }

    if(! priv->batch_mode) {
        if(priv->progress_bar != NULL) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress_bar), 0.0);
        }
        update_sensitivity();
    }
#endif
}

#if 0
static gboolean
last_output_window__unrealize_cb(GtkWidget*  widget,
                                 gpointer    data) {
    pref_util_save_window_geometry(GTK_WINDOW(widget), LAST_OUTPUT_DIALOG_NAME);
    return FALSE;
}
#endif


void
FrWindow::view_last_output(
    const char* title) {

#if 0
    GtkWidget*     dialog;
    GtkWidget*     vbox;
    GtkWidget*     text_view;
    GtkWidget*     scrolled;
    GtkTextBuffer* text_buffer;
    GtkTextIter    iter;
    GList*         scan;

    if(title == NULL) {
        title = _("Last Output");
    }

    dialog = gtk_dialog_new_with_buttons(title,
                                         GTK_WINDOW(),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "gtk-close", GTK_RESPONSE_CLOSE,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
    gtk_widget_set_size_request(dialog, 500, 300);

    /* Add text */

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                        GTK_SHADOW_ETCHED_IN);

    text_buffer = gtk_text_buffer_new(NULL);
    gtk_text_buffer_create_tag(text_buffer, "monospace",
                               "family", "monospace", NULL);

    text_view = gtk_text_view_new_with_buffer(text_buffer);
    g_object_unref(text_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);

    /**/

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

    gtk_container_add(GTK_CONTAINER(scrolled), text_view);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled,
                       TRUE, TRUE, 0);

    gtk_widget_show_all(vbox);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       vbox,
                       TRUE, TRUE, 0);

    /* signals */

    g_signal_connect(G_OBJECT(dialog),
                     "response",
                     G_CALLBACK(gtk_widget_destroy),
                     NULL);

    g_signal_connect(G_OBJECT(dialog),
                     "unrealize",
                     G_CALLBACK(last_output_window__unrealize_cb),
                     NULL);

    /**/

    gtk_text_buffer_get_iter_at_offset(text_buffer, &iter, 0);
    scan = archive->process->out.raw;
    for(; scan; scan = scan->next) {
        char*        line = scan->data;
        char*        utf8_line;
        gsize        bytes_written;

        utf8_line = g_locale_to_utf8(line, -1, NULL, &bytes_written, NULL);
        gtk_text_buffer_insert_with_tags_by_name(text_buffer,
                &iter,
                utf8_line,
                bytes_written,
                "monospace", NULL);
        g_free(utf8_line);
        gtk_text_buffer_insert(text_buffer, &iter, "\n", 1);
    }

    /**/

    pref_util_restore_window_geometry(GTK_WINDOW(dialog), LAST_OUTPUT_DIALOG_NAME);
#endif
}


/* -- FrWindow::rename_selection -- */


typedef struct {
    char*     path_to_rename;
    char*     old_name;
    char*     new_name;
    char*     current_dir;
    gboolean  is_dir;
    gboolean  dir_in_archive;
    char*     original_path;
} RenameData;


static RenameData*
rename_data_new(const char* path_to_rename,
                const char* old_name,
                const char* new_name,
                const char* current_dir,
                gboolean    is_dir,
                gboolean    dir_in_archive,
                const char* original_path) {
    RenameData* rdata;

    rdata = g_new0(RenameData, 1);
    rdata->path_to_rename = g_strdup(path_to_rename);
    if(old_name != NULL) {
        rdata->old_name = g_strdup(old_name);
    }
    if(new_name != NULL) {
        rdata->new_name = g_strdup(new_name);
    }
    if(current_dir != NULL) {
        rdata->current_dir = g_strdup(current_dir);
    }
    rdata->is_dir = is_dir;
    rdata->dir_in_archive = dir_in_archive;
    if(original_path != NULL) {
        rdata->original_path = g_strdup(original_path);
    }

    return rdata;
}


static void
rename_data_free(RenameData* rdata) {
    g_return_if_fail(rdata != NULL);

    g_free(rdata->path_to_rename);
    g_free(rdata->old_name);
    g_free(rdata->new_name);
    g_free(rdata->current_dir);
    g_free(rdata->original_path);
    g_free(rdata);
}


static void
rename_selection(
    const char* path_to_rename,
    const char* old_name,
    const char* new_name,
    const char* current_dir,
    gboolean    is_dir,
    gboolean    dir_in_archive,
    const char* original_path) {
    FrArchive*  archive = archive;
    RenameData* rdata;
    char*       tmp_dir;
    GList*      file_list;
    gboolean    added_dir;
    char*       new_dirname;
    GList*      new_file_list;
    GList*      scan;
#if 0
    rdata = rename_data_new(path_to_rename,
                            old_name,
                            new_name,
                            current_dir,
                            is_dir,
                            dir_in_archive,
                            original_path);
    set_current_batch_action(FR_BATCH_ACTION_RENAME,
                             rdata,
                             (GFreeFunc) rename_data_free);

    fr_process_clear(archive->process);

    tmp_dir = get_temp_work_dir(NULL);

    if(is_dir) {
        file_list = get_dir_list_from_path(rdata->path_to_rename);
    }
    else {
        file_list = g_list_append(NULL, g_strdup(rdata->path_to_rename));
    }

    fr_archive_extract_to_local(archive,
                                file_list,
                                tmp_dir,
                                NULL,
                                FALSE,
                                TRUE,
                                FALSE,
                                priv->password);

    /* temporarily add the dir to rename to the list if it's stored in the
     * archive, this way it will be removed from the archive... */
    added_dir = FALSE;
    if(is_dir && dir_in_archive && ! g_list_find_custom(file_list, original_path, (GCompareFunc) strcmp)) {
        file_list = g_list_prepend(file_list, g_strdup(original_path));
        added_dir = TRUE;
    }

    fr_archive_remove(archive, file_list, priv->compression);
    clipboard_remove_file_list(file_list);

    /* ...and remove it from the list again */
    if(added_dir) {
        GList* tmp;

        tmp = file_list;
        file_list = g_list_remove_link(file_list, tmp);

        g_free(tmp->data);
        g_list_free(tmp);
    }

    /* rename the files. */

    new_dirname = g_build_filename(rdata->current_dir + 1, rdata->new_name, "/", NULL);
    new_file_list = NULL;
    if(rdata->is_dir) {
        char* old_path;
        char* new_path;

        old_path = g_build_filename(tmp_dir, rdata->current_dir, rdata->old_name, NULL);
        new_path = g_build_filename(tmp_dir, rdata->current_dir, rdata->new_name, NULL);

        fr_process_begin_command(archive->process, "mv");
        fr_process_add_arg(archive->process, "-f");
        fr_process_add_arg(archive->process, old_path);
        fr_process_add_arg(archive->process, new_path);
        fr_process_end_command(archive->process);

        g_free(old_path);
        g_free(new_path);
    }

    for(scan = file_list; scan; scan = scan->next) {
        const char* current_dir_relative = rdata->current_dir + 1;
        const char* filename = (char*) scan->data;
        char*       old_path = NULL, *common = NULL, *new_path = NULL;
        char*       new_filename;

        old_path = g_build_filename(tmp_dir, filename, NULL);

        if(strlen(filename) > (strlen(rdata->current_dir) + strlen(rdata->old_name))) {
            common = g_strdup(filename + strlen(rdata->current_dir) + strlen(rdata->old_name));
        }
        new_path = g_build_filename(tmp_dir, rdata->current_dir, rdata->new_name, common, NULL);

        if(! rdata->is_dir) {
            fr_process_begin_command(archive->process, "mv");
            fr_process_add_arg(archive->process, "-f");
            fr_process_add_arg(archive->process, old_path);
            fr_process_add_arg(archive->process, new_path);
            fr_process_end_command(archive->process);
        }

        new_filename = g_build_filename(current_dir_relative, rdata->new_name, common, NULL);
        new_file_list = g_list_prepend(new_file_list, new_filename);

        g_free(old_path);
        g_free(common);
        g_free(new_path);
    }
    new_file_list = g_list_reverse(new_file_list);

    /* FIXME: this is broken for tar archives.
    if (is_dir && dir_in_archive && ! g_list_find_custom (new_file_list, new_dirname, (GCompareFunc) strcmp))
        new_file_list = g_list_prepend (new_file_list, g_build_filename (rdata->current_dir + 1, rdata->new_name, NULL));
    */

    fr_archive_add(archive,
                   new_file_list,
                   tmp_dir,
                   NULL,
                   FALSE,
                   FALSE,
                   priv->password,
                   priv->encrypt_header,
                   priv->compression,
                   priv->volume_size);

    g_free(new_dirname);
    path_list_free(new_file_list);
    path_list_free(file_list);

    /* remove the tmp dir */

    fr_process_begin_command(archive->process, "rm");
    fr_process_set_working_dir(archive->process, g_get_tmp_dir());
    fr_process_set_sticky(archive->process, TRUE);
    fr_process_add_arg(archive->process, "-rf");
    fr_process_add_arg(archive->process, tmp_dir);
    fr_process_end_command(archive->process);

    fr_process_start(archive->process);

    g_free(tmp_dir);
#endif
}


static gboolean
valid_name(const char*  new_name,
           const char*  old_name,
           char**       reason) {
    char*     utf8_new_name;
    gboolean  retval = TRUE;

    new_name = eat_spaces(new_name);
    utf8_new_name = g_filename_display_name(new_name);

    if(*new_name == '\0') {
        /* Translators: the name references to a filename.  This message can appear when renaming a file. */
        *reason = g_strdup(_("New name is void, please type a name."));
        retval = FALSE;
    }
    else if(strcmp(new_name, old_name) == 0) {
        /* Translators: the name references to a filename.  This message can appear when renaming a file. */
        *reason = g_strdup(_("New name is the same as old one, please type other name."));
        retval = FALSE;
    }
    else if(strchrs(new_name, BAD_CHARS)) {
        /* Translators: the %s references to a filename.  This message can appear when renaming a file. */
        *reason = g_strdup_printf(_("Name \"%s\" is not valid because it contains at least one of the following characters: %s, please type other name."), utf8_new_name, BAD_CHARS);
        retval = FALSE;
    }

    g_free(utf8_new_name);

    return retval;
}


gboolean
FrWindow::name_is_present(
    const char*  current_dir,
    const char*  new_name,
    char**       reason) {
    gboolean  retval = FALSE;
    int       i;
    char*     new_filename;
    int       new_filename_l;

    *reason = NULL;

    new_filename = g_build_filename(current_dir, new_name, NULL);
    new_filename_l = strlen(new_filename);

    for(i = 0; i < archive->command->files->len; i++) {
        FileData*   fdata = (FileData*)g_ptr_array_index(archive->command->files, i);
        const char* filename = fdata->full_path;

        if((strncmp(filename, new_filename, new_filename_l) == 0)
                && ((filename[new_filename_l] == '\0')
                    || (filename[new_filename_l] == G_DIR_SEPARATOR))) {
            char* utf8_name = g_filename_display_name(new_name);

            if(filename[new_filename_l] == G_DIR_SEPARATOR) {
                *reason = g_strdup_printf(_("A folder named \"%s\" already exists.\n\n%s"), utf8_name, _("Please use a different name."));
            }
            else {
                *reason = g_strdup_printf(_("A file named \"%s\" already exists.\n\n%s"), utf8_name, _("Please use a different name."));
            }

            retval = TRUE;
            break;
        }
    }

    g_free(new_filename);

    return retval;
}


void
FrWindow::rename_selection(
    gboolean  from_sidebar) {
    char*     path_to_rename;
    char*     parent_dir;
    char*     old_name;
    gboolean  renaming_dir = FALSE;
    gboolean  dir_in_archive = FALSE;
    char*     original_path = NULL;
    char*     utf8_old_name;
    char*     utf8_new_name;

#if 0
    if(from_sidebar) {
        path_to_rename = get_selected_folder_in_tree_view();
        if(path_to_rename == NULL) {
            return;
        }
        parent_dir = remove_level_from_path(path_to_rename);
        old_name = g_strdup(file_name_from_path(path_to_rename));
        renaming_dir = TRUE;
    }
    else {
        FileData* selected_item;

        selected_item = get_selected_item_from_file_list();
        if(selected_item == NULL) {
            return;
        }

        renaming_dir = file_data_is_dir(selected_item);
        dir_in_archive = selected_item->dir;
        original_path = g_strdup(selected_item->original_path);

        if(renaming_dir && ! dir_in_archive) {
            parent_dir = g_strdup());
            old_name = g_strdup(selected_item->list_name);
            path_to_rename = g_build_filename(parent_dir, old_name, NULL);
        }
        else {
            if(renaming_dir) {
                path_to_rename = remove_ending_separator(selected_item->full_path);
                parent_dir = remove_level_from_path(path_to_rename);
            }
            else {
                path_to_rename = g_strdup(selected_item->original_path);
                parent_dir = remove_level_from_path(selected_item->full_path);
            }
            old_name = g_strdup(selected_item->name);
        }

        file_data_free(selected_item);
    }

retry__rename_selection:
    utf8_old_name = g_locale_to_utf8(old_name, -1, 0, 0, 0);
    utf8_new_name = _gtk_request_dialog_run(GTK_WINDOW(),
                                            (GTK_DIALOG_DESTROY_WITH_PARENT
                                                    | GTK_DIALOG_MODAL),
                                            _("Rename"),
                                            (renaming_dir ? _("_New folder name:") : _("_New file name:")),
                                            utf8_old_name,
                                            1024,
                                            "gtk-cancel",
                                            _("_Rename"));
    g_free(utf8_old_name);

    if(utf8_new_name != NULL) {
        char* new_name;
        char* reason = NULL;

        new_name = g_filename_from_utf8(utf8_new_name, -1, 0, 0, 0);
        g_free(utf8_new_name);

        if(! valid_name(new_name, old_name, &reason)) {
            char*      utf8_name = g_filename_display_name(new_name);
            GtkWidget* dlg;

            dlg = _gtk_error_dialog_new(GTK_WINDOW(),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        NULL,
                                        (renaming_dir ? _("Could not rename the folder") : _("Could not rename the file")),
                                        "%s",
                                        reason);
            gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);

            g_free(reason);
            g_free(utf8_name);
            g_free(new_name);

            goto retry__rename_selection;
        }

        if(name_is_present(parent_dir, new_name, &reason)) {
            GtkWidget* dlg;

            dlg = _gtk_message_dialog_new(GTK_WINDOW(),
                                          GTK_DIALOG_MODAL,
                                          "dialog-question",
                                          (renaming_dir ? _("Could not rename the folder") : _("Could not rename the file")),
                                          reason,
                                          "gtk-close", GTK_RESPONSE_OK,
                                          NULL);
            gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);
            g_free(reason);
            g_free(new_name);
            goto retry__rename_selection;
        }

        rename_selection(window,
                         path_to_rename,
                         old_name,
                         new_name,
                         parent_dir,
                         renaming_dir,
                         dir_in_archive,
                         original_path);

        g_free(new_name);
    }

    g_free(old_name);
    g_free(parent_dir);
    g_free(path_to_rename);
    g_free(original_path);
#endif
}

#if 0
static void
fr_clipboard_get(GtkClipboard*     clipboard,
                 GtkSelectionData* selection_data,
                 guint             info,
                 gpointer          user_data_or_owner) {
    FrWindow* window = user_data_or_owner;
    char*     data;

    if(gtk_selection_data_get_target(selection_data) != FR_SPECIAL_URI_LIST) {
        return;
    }

    data = get_selection_data_from_clipboard_data(priv->copy_data);
    gtk_selection_data_set(selection_data,
                           gtk_selection_data_get_target(selection_data),
                           8,
                           (guchar*) data,
                           strlen(data));
    g_free(data);
}


static void
fr_clipboard_clear(GtkClipboard* clipboard,
                   gpointer      user_data_or_owner) {
    FrWindow* window = user_data_or_owner;

    if(priv->copy_data != NULL) {
        fr_clipboard_data_unref(priv->copy_data);
        priv->copy_data = NULL;
    }
}
#endif


GList*
FrWindow::get_selection(
    gboolean    from_sidebar,
    char**      return_base_dir) {
    GList* files;
    char*  base_dir;
#if 0
    if(from_sidebar) {
        char* selected_folder;
        char* parent_folder;

        files = get_folder_tree_selection(TRUE, NULL);
        selected_folder = get_selected_folder_in_tree_view();
        parent_folder = remove_level_from_path(selected_folder);
        if(parent_folder == NULL) {
            base_dir = g_strdup("/");
        }
        else if(parent_folder[strlen(parent_folder) - 1] == '/') {
            base_dir = g_strdup(parent_folder);
        }
        else {
            base_dir = g_strconcat(parent_folder, "/", NULL);
        }
        g_free(selected_folder);
        g_free(parent_folder);
    }
    else {
        files = get_file_list_selection(TRUE, NULL);
        base_dir = g_strdup());
    }

    if(return_base_dir) {
        *return_base_dir = base_dir;
    }
    else {
        g_free(base_dir);
    }

    return files;
#endif
    return nullptr;
}


void FrWindow::copy_or_cut_selection(
    FRClipboardOp  op,
    gboolean       from_sidebar) {

#if 0
    GList*        files;
    char*         base_dir;
    GtkClipboard* clipboard;

    files = get_selection(from_sidebar, &base_dir);

    if(priv->copy_data != NULL) {
        fr_clipboard_data_unref(priv->copy_data);
    }
    priv->copy_data = fr_clipboard_data_new();
    priv->copy_data->files = files;
    priv->copy_data->op = op;
    priv->copy_data->base_dir = base_dir;

    clipboard = gtk_clipboard_get(FR_CLIPBOARD);
    gtk_clipboard_set_with_owner(clipboard,
                                 clipboard_targets,
                                 G_N_ELEMENTS(clipboard_targets),
                                 fr_clipboard_get,
                                 fr_clipboard_clear,
                                 G_OBJECT());
#endif

    update_sensitivity();
}


void
FrWindow::copy_selection(
    gboolean  from_sidebar) {
    copy_or_cut_selection(FR_CLIPBOARD_OP_COPY, from_sidebar);
}


void
FrWindow::cut_selection(
    gboolean  from_sidebar) {
    copy_or_cut_selection(FR_CLIPBOARD_OP_CUT, from_sidebar);
}


static gboolean
always_fake_load(FrArchive* archive,
                 gpointer   data) {
    return TRUE;
}


void FrWindow::add_pasted_files(
    FrClipboardData* data) {
    const char* current_dir_relative = data->current_dir + 1;
    GList*      scan;
    GList*      new_file_list = NULL;

    if(priv->password_for_paste != NULL) {
        g_free(priv->password_for_paste);
        priv->password_for_paste = NULL;
    }

    fr_process_clear(archive->process);
    for(scan = data->files; scan; scan = scan->next) {
        const char* old_name = (char*) scan->data;
        char*       new_name = g_build_filename(current_dir_relative, old_name + strlen(data->base_dir) - 1, NULL);

        /* skip folders */

        if((strcmp(old_name, new_name) != 0)
                && (old_name[strlen(old_name) - 1] != '/')) {
            fr_process_begin_command(archive->process, "mv");
            fr_process_set_working_dir(archive->process, data->tmp_dir);
            fr_process_add_arg(archive->process, "-f");
            if(old_name[0] == '/') {
                old_name = old_name + 1;
            }
            fr_process_add_arg(archive->process, old_name);
            fr_process_add_arg(archive->process, new_name);
            fr_process_end_command(archive->process);
        }

        new_file_list = g_list_prepend(new_file_list, new_name);
    }

    fr_archive_add(archive,
                   new_file_list,
                   data->tmp_dir,
                   NULL,
                   FALSE,
                   FALSE,
                   priv->password,
                   priv->encrypt_header,
                   priv->compression,
                   priv->volume_size);

    path_list_free(new_file_list);

    /* remove the tmp dir */

    fr_process_begin_command(archive->process, "rm");
    fr_process_set_working_dir(archive->process, g_get_tmp_dir());
    fr_process_set_sticky(archive->process, TRUE);
    fr_process_add_arg(archive->process, "-rf");
    fr_process_add_arg(archive->process, data->tmp_dir);
    fr_process_end_command(archive->process);

    fr_process_start(archive->process);
}


void FrWindow::copy_from_archive_action_performed_cb(FrArchive*   archive,
        FrAction     action,
        FrProcError* error,
        gpointer     data) {
    gboolean  UNUSED_VARIABLE continue_batch = FALSE;

#ifdef DEBUG
    debug(DEBUG_INFO, "%s [DONE] (FR::Window)\n", action_names[action]);
#endif

    stop_activity_mode();
    pop_message();
    close_progress_dialog(FALSE);

    if(error->type == FR_PROC_ERROR_ASK_PASSWORD) {
#if 0
        // FIXME
        dlg_ask_password_for_paste_operation();
#endif
        return;
    }

    (void) handle_errors(archive, action, error);

    if(error->type != FR_PROC_ERROR_NONE) {
        fr_clipboard_data_unref(priv->clipboard_data);
        priv->clipboard_data = NULL;
        return;
    }

    switch(action) {
    case FR_ACTION_LISTING_CONTENT:
        fr_process_clear(priv->copy_from_archive->process);
        fr_archive_extract_to_local(priv->copy_from_archive,
                                    priv->clipboard_data->files,
                                    priv->clipboard_data->tmp_dir,
                                    NULL,
                                    FALSE,
                                    TRUE,
                                    FALSE,
                                    priv->clipboard_data->archive_password);
        fr_process_start(priv->copy_from_archive->process);
        break;

    case FR_ACTION_EXTRACTING_FILES:
        if(priv->clipboard_data->op == FR_CLIPBOARD_OP_CUT) {
            fr_process_clear(priv->copy_from_archive->process);
            fr_archive_remove(priv->copy_from_archive,
                              priv->clipboard_data->files,
                              priv->compression);
            fr_process_start(priv->copy_from_archive->process);
        }
        else {
            add_pasted_files(priv->clipboard_data);
        }
        break;

    case FR_ACTION_DELETING_FILES:
        add_pasted_files(priv->clipboard_data);
        break;

    default:
        break;
    }
}


void FrWindow::paste_from_clipboard_data(
    FrClipboardData* data) {
    const char* current_dir_relative;
    GHashTable* created_dirs;
    GList*      scan;
#if 0
    if(priv->password_for_paste != NULL) {
        fr_clipboard_data_set_password(data, priv->password_for_paste);
    }

    if(priv->clipboard_data != data) {
        fr_clipboard_data_unref(priv->clipboard_data);
        priv->clipboard_data = data;
    }

    set_current_batch_action(,
                             FR_BATCH_ACTION_PASTE,
                             fr_clipboard_data_ref(data),
                             (GFreeFunc) fr_clipboard_data_unref);

    current_dir_relative = data->current_dir + 1;

    data->tmp_dir = get_temp_work_dir(NULL);
    created_dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for(scan = data->files; scan; scan = scan->next) {
        const char* old_name = (char*) scan->data;
        char*       new_name = g_build_filename(current_dir_relative, old_name + strlen(data->base_dir) - 1, NULL);
        char*       dir = remove_level_from_path(new_name);

        if((dir != NULL) && (g_hash_table_lookup(created_dirs, dir) == NULL)) {
            char* dir_path;

            dir_path = g_build_filename(data->tmp_dir, dir, NULL);
            debug(DEBUG_INFO, "mktree %s\n", dir_path);
            make_directory_tree_from_path(dir_path, 0700, NULL);

            g_free(dir_path);
            g_hash_table_replace(created_dirs, g_strdup(dir), "1");
        }

        g_free(dir);
        g_free(new_name);
    }
    g_hash_table_destroy(created_dirs);

    /**/

    if(priv->copy_from_archive == NULL) {
        priv->copy_from_archive = fr_archive_new();
        g_signal_connect(G_OBJECT(priv->copy_from_archive),
                         "start",
                         G_CALLBACK(action_started),
                         window);
        g_signal_connect(G_OBJECT(priv->copy_from_archive),
                         "done",
                         G_CALLBACK(copy_from_archive_action_performed_cb),
                         window);
        g_signal_connect(G_OBJECT(priv->copy_from_archive),
                         "progress",
                         G_CALLBACK(FrWindow::progress_cb),
                         window);
        g_signal_connect(G_OBJECT(priv->copy_from_archive),
                         "message",
                         G_CALLBACK(FrWindow::message_cb),
                         window);
        g_signal_connect(G_OBJECT(priv->copy_from_archive),
                         "stoppable",
                         G_CALLBACK(FrWindow::stoppable_cb),
                         window);
        fr_archive_set_fake_load_func(priv->copy_from_archive, always_fake_load, NULL);
    }
    fr_archive_load_local(priv->copy_from_archive,
                          data->archive_filename,
                          data->archive_password);
timeout:
# 5s
#endif
}


void
FrWindow::paste_selection_to(
    const char* current_dir) {
#if 0
    GtkClipboard*     clipboard;
    GtkSelectionData* selection_data;
    FrClipboardData*  paste_data;

    clipboard = gtk_clipboard_get(FR_CLIPBOARD);
    selection_data = gtk_clipboard_wait_for_contents(clipboard, FR_SPECIAL_URI_LIST);
    if(selection_data == NULL) {
        return;
    }

    paste_data = get_clipboard_data_from_selection_data((char*) gtk_selection_data_get_data(selection_data));
    paste_data->current_dir = g_strdup(current_dir);
    paste_from_clipboard_data(paste_data);

    gtk_selection_data_free(selection_data);
#endif
}


void
FrWindow::paste_selection(
    gboolean  from_sidebar) {
    char* utf8_path, *utf8_old_path, *destination;
    char* current_dir;

    if(priv->list_mode == FR_WINDOW_LIST_MODE_FLAT) {
        return;
    }

    /**/
#if 0
    utf8_old_path = g_filename_to_utf8(), -1, NULL, NULL, NULL);
    utf8_path = _gtk_request_dialog_run(GTK_WINDOW(),
                                        (GTK_DIALOG_DESTROY_WITH_PARENT
                                         | GTK_DIALOG_MODAL),
                                        _("Paste Selection"),
                                        _("_Destination folder:"),
                                        utf8_old_path,
                                        1024,
                                        "gtk-cancel",
                                        "gtk-paste");
    g_free(utf8_old_path);

    if(utf8_path == NULL) {
    return;
}

destination = g_filename_from_utf8(utf8_path, -1, NULL, NULL, NULL);
g_free(utf8_path);

if(destination[0] != '/') {
        current_dir = build_uri(), destination, NULL);
    }
    else {
        current_dir = g_strdup(destination);
    }
    g_free(destination);

    paste_selection_to(current_dir);

    g_free(current_dir);
#endif
}


/* -- FrWindow::open_files -- */


void
FrWindow::open_files_with_command(
    GList*    file_list,
    char*     command) {
    GAppInfo* app;
    GError*   error = NULL;

    app = g_app_info_create_from_commandline(command, NULL, G_APP_INFO_CREATE_NONE, &error);
    if(error != NULL) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Could not perform the operation") + QString::fromUtf8(error->message));
        g_clear_error(&error);
        return;
    }

    open_files_with_application(file_list, app);
}


void
FrWindow::open_files_with_application(
    GList*    file_list,
    GAppInfo* app) {
#if 0
    GList*               uris;
    GList*               scan;
    GdkAppLaunchContext* context;
    GError*              error = NULL;

    if(priv->activity_ref > 0) {
        return;
    }

    g_assert(file_list != NULL);

    uris = NULL;
    for(scan = file_list; scan; scan = scan->next) {
        uris = g_list_prepend(uris, g_filename_to_uri(scan->data, NULL, NULL));
    }

    context = gdk_display_get_app_launch_context(gtk_widget_get_display(GTK_WIDGET()));
    gdk_app_launch_context_set_screen(context, gtk_widget_get_screen(GTK_WIDGET()));
    gdk_app_launch_context_set_timestamp(context, 0);

    if(! g_app_info_launch_uris(app, uris, G_APP_LAUNCH_CONTEXT(context), &error)) {
        _gtk_error_dialog_run(GTK_WINDOW(),
                              _("Could not perform the operation"),
                              "%s",
                              error->message);
        g_clear_error(&error);
    }

    g_object_unref(context);
    path_list_free(uris);
#endif
}


struct OpenFilesData {
    FrWindow*    window;
    GList*       file_list;
    gboolean     ask_application;
    CommandData* cdata;
};


static OpenFilesData*
open_files_data_new(
    GList*    file_list,
    gboolean  ask_application)

{
    OpenFilesData* odata;
    GList*         scan;
#if 0
    odata = g_new0(OpenFilesData, 1);
    odata->window = window;
    odata->file_list = path_list_dup(file_list);
    odata->ask_application = ask_application;
    odata->cdata = g_new0(CommandData, 1);
    odata->cdata->temp_dir = get_temp_work_dir(NULL);
    odata->cdata->file_list = NULL;
    for(scan = file_list; scan; scan = scan->next) {
        char* file = scan->data;
        char* filename;

        filename = g_strconcat(odata->cdata->temp_dir,
                               "/",
                               file,
                               NULL);
        odata->cdata->file_list = g_list_prepend(odata->cdata->file_list, filename);
    }

    /* Add to CommandList so the cdata is released on exit. */
    CommandList = g_list_prepend(CommandList, odata->cdata);
#endif
    return odata;
}


static void
open_files_data_free(OpenFilesData* odata) {
    g_return_if_fail(odata != NULL);

    path_list_free(odata->file_list);
    g_free(odata);
}


void
FrWindow::update_dialog_closed() {
    priv->update_dialog = NULL;
}


gboolean
FrWindow::update_files(
    GList*    file_list) {
    GList* scan;

    if(priv->activity_ref > 0) {
        return FALSE;
    }

    if(archive->read_only) {
        return FALSE;
    }

    fr_process_clear(archive->process);

    for(scan = file_list; scan; scan = scan->next) {
        OpenFile* file = (OpenFile*)scan->data;
        GList*    local_file_list;

        local_file_list = g_list_append(NULL, file->path);
        fr_archive_add(archive,
                       local_file_list,
                       file->temp_dir,
                       "/",
                       FALSE,
                       FALSE,
                       priv->password,
                       priv->encrypt_header,
                       priv->compression,
                       priv->volume_size);
        g_list_free(local_file_list);
    }

    fr_process_start(archive->process);

    return TRUE;
}


void
FrWindow::open_file_modified_cb(GFileMonitor*     monitor,
                                GFile*            monitor_file,
                                GFile*            other_file,
                                GFileMonitorEvent event_type,
                                gpointer          user_data) {
#if 0
    FrWindow* window = user_data;
    char*     monitor_uri;
    OpenFile* file;
    GList*    scan;

    if((event_type != G_FILE_MONITOR_EVENT_CHANGED)
            && (event_type != G_FILE_MONITOR_EVENT_CREATED)) {
        return;
    }

    monitor_uri = g_file_get_uri(monitor_file);
    file = NULL;
    for(scan = priv->open_files; scan; scan = scan->next) {
        OpenFile* test = scan->data;
        if(uricmp(test->extracted_uri, monitor_uri) == 0) {
            file = test;
            break;
        }
    }
    g_free(monitor_uri);

    g_return_if_fail(file != NULL);

    if(priv->update_dialog == NULL) {
        priv->update_dialog = dlg_update();
    }
    dlg_update_add_file(priv->update_dialog, file);
#endif
}


void
FrWindow::monitor_open_file(
    OpenFile* file) {
    GFile* f;
#if 0
    priv->open_files = g_list_prepend(priv->open_files, file);
    f = g_file_new_for_uri(file->extracted_uri);
    file->monitor = g_file_monitor_file(f, G_FILE_MONITOR_NONE, NULL, NULL);
    g_signal_connect(file->monitor,
                     "changed",
                     G_CALLBACK(open_file_modified_cb),
                     window);
    g_object_unref(f);
#endif
}


void
FrWindow::monitor_extracted_files(OpenFilesData* odata) {
    FrWindow* window = odata->window;
    GList*    scan1, *scan2;

    for(scan1 = odata->file_list, scan2 = odata->cdata->file_list;
            scan1 && scan2;
            scan1 = scan1->next, scan2 = scan2->next) {
        OpenFile*   ofile;
        const char* file = (const char*)scan1->data;
        const char* extracted_path = (const char*)scan2->data;

        ofile = open_file_new(file, extracted_path, odata->cdata->temp_dir);
        if(ofile != NULL) {
            monitor_open_file(ofile);
        }
    }
}


gboolean
FrWindow::open_extracted_files(OpenFilesData* odata) {
    gboolean             result;
#if 0
    GList*               file_list = odata->cdata->file_list;
    const char*          first_file;
    const char*          first_mime_type;
    GAppInfo*            app;
    GList*               files_to_open = NULL;
    GdkAppLaunchContext* context;
    GError*              error = NULL;

    g_return_val_if_fail(file_list != NULL, FALSE);

    first_file = (char*) file_list->data;
    if(first_file == NULL) {
        return FALSE;
    }

    if(! odata->archive->read_only) {
        monitor_extracted_files(odata);
    }

    if(odata->ask_application) {
        dlg_open_with(odata->window, file_list);
        return FALSE;
    }

    first_mime_type = get_file_mime_type_for_path(first_file, FALSE);
    app = g_app_info_get_default_for_type(first_mime_type, FALSE);

    if(app == NULL) {
        dlg_open_with(odata->window, file_list);
        return FALSE;
    }

    files_to_open = g_list_append(files_to_open, g_filename_to_uri(first_file, NULL, NULL));

    if(g_app_info_supports_files(app)) {
        GList* scan;

        for(scan = file_list->next; scan; scan = scan->next) {
            const char* path = scan->data;
            const char* mime_type;

            mime_type = get_file_mime_type_for_path(path, FALSE);
            if(mime_type == NULL) {
                continue;
            }

            if(strcmp(mime_type, first_mime_type) == 0) {
                files_to_open = g_list_append(files_to_open, g_filename_to_uri(path, NULL, NULL));
            }
            else {
                GAppInfo* app2;

                app2 = g_app_info_get_default_for_type(mime_type, FALSE);
                if(g_app_info_equal(app, app2)) {
                    files_to_open = g_list_append(files_to_open, g_filename_to_uri(path, NULL, NULL));
                }
                g_object_unref(app2);
            }
        }
    }

    context = gdk_display_get_app_launch_context(gtk_widget_get_display(GTK_WIDGET(odata->window)));
    gdk_app_launch_context_set_screen(context, gtk_widget_get_screen(GTK_WIDGET(odata->window)));
    gdk_app_launch_context_set_timestamp(context, 0);
    result = g_app_info_launch_uris(app, files_to_open, G_APP_LAUNCH_CONTEXT(context), &error);
    if(! result) {
        _gtk_error_dialog_run(GTK_WINDOW(odata->window),
                              _("Could not perform the operation"),
                              "%s",
                              error->message);
        g_clear_error(&error);
    }

    g_object_unref(context);
    g_object_unref(app);
    path_list_free(files_to_open);
#endif
    return result;
}


void
FrWindow::open_files__extract_done_cb(FrArchive*   archive,
                                      FrAction     action,
                                      FrProcError* error,
                                      gpointer     callback_data) {
    OpenFilesData* odata = (OpenFilesData*)callback_data;

    g_signal_handlers_disconnect_matched(G_OBJECT(archive),
                                         G_SIGNAL_MATCH_DATA,
                                         0,
                                         0, NULL,
                                         0,
                                         odata);

    if(error->type == FR_PROC_ERROR_NONE) {
        FrWindow::open_extracted_files(odata);
    }
}


void
FrWindow::open_files(
    GList*    file_list,
    gboolean  ask_application) {
    OpenFilesData* odata;
#if 0
    if(priv->activity_ref > 0) {
        return;
    }

    odata = open_files_data_new(file_list, ask_application);
    set_current_batch_action(,
                             FR_BATCH_ACTION_OPEN_FILES,
                             odata,
                             (GFreeFunc) open_files_data_free);

    g_signal_connect(G_OBJECT(archive),
                     "done",
                     G_CALLBACK(open_files__extract_done_cb),
                     odata);

    fr_process_clear(archive->process);
    fr_archive_extract_to_local(archive,
                                odata->file_list,
                                odata->cdata->temp_dir,
                                NULL,
                                FALSE,
                                TRUE,
                                FALSE,
                                priv->password);
    fr_process_start(archive->process);
#endif
}


/**/


static char*
get_default_dir(const char* dir) {
    if(! is_temp_dir(dir)) {
        return g_strdup(dir);
    }
    else {
        return NULL;
    }
}


void
FrWindow::set_open_default_dir(
    const char* default_dir) {

    g_return_if_fail(default_dir != NULL);

    if(priv->open_default_dir != NULL) {
        g_free(priv->open_default_dir);
    }
    priv->open_default_dir = get_default_dir(default_dir);
}


const char*
FrWindow::get_open_default_dir() {
    if(priv->open_default_dir == NULL) {
        return get_home_uri();
    }
    else {
        return  priv->open_default_dir;
    }
}


void
FrWindow::set_add_default_dir(
    const char* default_dir) {

    g_return_if_fail(default_dir != NULL);

    if(priv->add_default_dir != NULL) {
        g_free(priv->add_default_dir);
    }
    priv->add_default_dir = get_default_dir(default_dir);
}


const char*
FrWindow::get_add_default_dir() {
    if(priv->add_default_dir == NULL) {
        return get_home_uri();
    }
    else {
        return  priv->add_default_dir;
    }
}


void
FrWindow::set_extract_default_dir(
    const char* default_dir,
    gboolean    freeze) {

    g_return_if_fail(default_dir != NULL);

    /* do not change this dir while it's used by the non-interactive
     * extraction operation. */
    if(priv->extract_interact_use_default_dir) {
        return;
    }

    priv->extract_interact_use_default_dir = freeze;

    if(priv->extract_default_dir != NULL) {
        g_free(priv->extract_default_dir);
    }
    priv->extract_default_dir = get_default_dir(default_dir);
}


const char*
FrWindow::get_extract_default_dir() {
    if(priv->extract_default_dir == NULL) {
        return get_home_uri();
    }
    else {
        return  priv->extract_default_dir;
    }
}


void
FrWindow::set_default_dir(
    const char* default_dir,
    gboolean    freeze) {

    g_return_if_fail(default_dir != NULL);

    priv->freeze_default_dir = freeze;

    set_open_default_dir(default_dir);
    set_add_default_dir(default_dir);
    set_extract_default_dir(default_dir, FALSE);
}


void
FrWindow::update_columns_visibility() {
#if 0
    GtkTreeView*       tree_view = GTK_TREE_VIEW(priv->list_view);
    GtkTreeViewColumn* column;

    column = gtk_tree_view_get_column(tree_view, 1);
    gtk_tree_view_column_set_visible(column, g_settings_get_boolean(priv->settings_listing, PREF_LISTING_SHOW_SIZE));

    column = gtk_tree_view_get_column(tree_view, 2);
    gtk_tree_view_column_set_visible(column, g_settings_get_boolean(priv->settings_listing, PREF_LISTING_SHOW_TYPE));

    column = gtk_tree_view_get_column(tree_view, 3);
    gtk_tree_view_column_set_visible(column, g_settings_get_boolean(priv->settings_listing, PREF_LISTING_SHOW_TIME));

    column = gtk_tree_view_get_column(tree_view, 4);
    gtk_tree_view_column_set_visible(column, g_settings_get_boolean(priv->settings_listing, PREF_LISTING_SHOW_PATH));
#endif
}


void
FrWindow::set_toolbar_visibility(
    gboolean  visible) {
#if 0

    if(visible) {
        gtk_widget_show(priv->toolbar);
    }
    else {
        gtk_widget_hide(priv->toolbar);
    }
    set_active("ViewToolbar", visible);
#endif
}


void
FrWindow::set_statusbar_visibility(
    gboolean  visible) {

#if 0
    if(visible) {
        gtk_widget_show(priv->statusbar);
    }
    else {
        gtk_widget_hide(priv->statusbar);
    }

    set_active("ViewStatusbar", visible);
#endif
}


void
FrWindow::set_folders_visibility(
    gboolean    value) {
#if 0

    priv->view_folders = value;
    update_dir_tree();

    set_active("ViewFolders", priv->view_folders);
#endif
}


void
FrWindow::use_progress_dialog(
    gboolean  value) {
    priv->use_progress_dialog = value;
}


/* -- batch mode procedures -- */

void
FrWindow::exec_batch_action(
    FRBatchAction* action) {
    ExtractData*   edata;
    RenameData*    rdata;
    OpenFilesData* odata;
    SaveAsData*    sdata;
#if 0
    switch(action->type) {
    case FR_BATCH_ACTION_LOAD:
        debug(DEBUG_INFO, "[BATCH] LOAD\n");

        if(! uri_exists((char*) action->data)) {
            archive_new((char*) action->data);
        }
        else {
            archive_open((char*) action->data, GTK_WINDOW());
        }
        break;

    case FR_BATCH_ACTION_ADD:
        debug(DEBUG_INFO, "[BATCH] ADD\n");

        archive_add_dropped_items((GList*) action->data, FALSE);
        break;

    case FR_BATCH_ACTION_OPEN:
        debug(DEBUG_INFO, "[BATCH] OPEN\n");

        push_message(_("Add files to an archive"));
        dlg_batch_add_files((GList*) action->data);
        break;

    case FR_BATCH_ACTION_EXTRACT:
        debug(DEBUG_INFO, "[BATCH] EXTRACT\n");

        edata = action->data;
        archive_extract(,
                        edata->file_list,
                        edata->extract_to_dir,
                        edata->base_dir,
                        edata->skip_older,
                        edata->overwrite,
                        edata->junk_paths,
                        TRUE);
        break;

    case FR_BATCH_ACTION_EXTRACT_HERE:
        debug(DEBUG_INFO, "[BATCH] EXTRACT HERE\n");

        archive_extract_here(,
                             FALSE,
                             TRUE,
                             FALSE);
        break;

    case FR_BATCH_ACTION_EXTRACT_INTERACT:
        debug(DEBUG_INFO, "[BATCH] EXTRACT_INTERACT\n");

        if(priv->extract_interact_use_default_dir
                && (priv->extract_default_dir != NULL)) {
            archive_extract(,
                            NULL,
                            priv->extract_default_dir,
                            NULL,
                            FALSE,
                            FR_OVERWRITE_ASK,
                            FALSE,
                            TRUE);
        }
        else {
            push_message(_("Extract archive"));
            dlg_extract(NULL, window);
        }
        break;

    case FR_BATCH_ACTION_RENAME:
        debug(DEBUG_INFO, "[BATCH] RENAME\n");

        rdata = action->data;
        rename_selection(window,
                         rdata->path_to_rename,
                         rdata->old_name,
                         rdata->new_name,
                         rdata->current_dir,
                         rdata->is_dir,
                         rdata->dir_in_archive,
                         rdata->original_path);
        break;

    case FR_BATCH_ACTION_PASTE:
        debug(DEBUG_INFO, "[BATCH] PASTE\n");

        paste_from_clipboard_data((FrClipboardData*) action->data);
        break;

    case FR_BATCH_ACTION_OPEN_FILES:
        debug(DEBUG_INFO, "[BATCH] OPEN FILES\n");

        odata = action->data;
        open_files(odata->file_list, odata->ask_application);
        break;

    case FR_BATCH_ACTION_SAVE_AS:
        debug(DEBUG_INFO, "[BATCH] SAVE_AS\n");

        sdata = action->data;
        archive_save_as(,
                        sdata->uri,
                        sdata->password,
                        sdata->encrypt_header,
                        sdata->volume_size);
        break;

    case FR_BATCH_ACTION_TEST:
        debug(DEBUG_INFO, "[BATCH] TEST\n");

        archive_test();
        break;

    case FR_BATCH_ACTION_CLOSE:
        debug(DEBUG_INFO, "[BATCH] CLOSE\n");

        archive_close();
        exec_next_batch_action();
        break;

    case FR_BATCH_ACTION_QUIT:
        debug(DEBUG_INFO, "[BATCH] QUIT\n");

        g_signal_emit(window,
                      FrWindow::signals[READY],
                      0,
                      NULL);

        if((priv->progress_dialog != NULL) && (gtk_widget_get_parent(priv->progress_dialog) != GTK_WIDGET())) {
            gtk_widget_destroy(priv->progress_dialog);
        }
        gtk_widget_destroy(GTK_WIDGET());
        break;

    default:
        break;
    }
#endif
}


void
FrWindow::reset_current_batch_action() {
    FRBatchAction* adata = &priv->current_batch_action;

    if((adata->data != NULL) && (adata->free_func != NULL)) {
        (*adata->free_func)(adata->data);
    }
    adata->type = FR_BATCH_ACTION_NONE;
    adata->data = NULL;
    adata->free_func = NULL;
}


void
FrWindow::set_current_batch_action(
    FrBatchActionType  action,
    void*              data,
    GFreeFunc          free_func) {
    FRBatchAction* adata = &priv->current_batch_action;

    reset_current_batch_action();

    adata->type = action;
    adata->data = data;
    adata->free_func = free_func;
}


void
FrWindow::restart_current_batch_action() {
    exec_batch_action(&priv->current_batch_action);
}


void
FrWindow::append_batch_action(
    FrBatchActionType  action,
    void*              data,
    GFreeFunc          free_func) {
    FRBatchAction* a_desc;



    a_desc = g_new0(FRBatchAction, 1);
    a_desc->type = action;
    a_desc->data = data;
    a_desc->free_func = free_func;

    priv->batch_action_list = g_list_append(priv->batch_action_list, a_desc);
}


void
FrWindow::exec_current_batch_action() {
    FRBatchAction* action;

    if(priv->batch_action == NULL) {
        priv->batch_mode = FALSE;
        return;
    }
    action = (FRBatchAction*) priv->batch_action->data;
    exec_batch_action(action);
}


void
FrWindow::exec_next_batch_action() {
    if(priv->batch_action != NULL) {
        priv->batch_action = g_list_next(priv->batch_action);
    }
    else {
        priv->batch_action = priv->batch_action_list;
    }
    exec_current_batch_action();
}


void
FrWindow::start_batch() {


    if(priv->batch_mode) {
        return;
    }

    if(priv->batch_action_list == NULL) {
        return;
    }
#if 0
    if(priv->progress_dialog != NULL)
        gtk_window_set_title(GTK_WINDOW(priv->progress_dialog),
                             priv->batch_title);
#endif
    priv->batch_mode = TRUE;
    priv->batch_action = priv->batch_action_list;
    archive->can_create_compressed_file = priv->batch_adding_one_file;

    exec_current_batch_action();
}


void
FrWindow::stop_batch() {
    if(! priv->non_interactive) {
        return;
    }

    priv->extract_interact_use_default_dir = FALSE;
    archive->can_create_compressed_file = FALSE;

    if(priv->batch_mode) {
        if(! priv->showing_error_dialog) {
#if 0
            gtk_widget_destroy(GTK_WIDGET());
#endif
            return;
        }
    }
    else {
        activateWindow();
        archive_close();
    }

    priv->batch_mode = FALSE;
}


void
FrWindow::resume_batch() {
    exec_current_batch_action();
}


gboolean
FrWindow::is_batch_mode() {
    return priv->batch_mode;
}


void
FrWindow::new_batch(
    const char* title) {
    free_batch_data();
    priv->non_interactive = TRUE;
    g_free(priv->batch_title);
    priv->batch_title = g_strdup(title);
}


void
FrWindow::set_batch__extract_here(
    const char* filename) {

    g_return_if_fail(filename != NULL);

    append_batch_action(FR_BATCH_ACTION_LOAD,
                        g_strdup(filename),
                        (GFreeFunc) g_free);
    append_batch_action(FR_BATCH_ACTION_EXTRACT_HERE,
                        extract_to_data_new(NULL),
                        (GFreeFunc) extract_data_free);
    append_batch_action(FR_BATCH_ACTION_CLOSE,
                        NULL,
                        NULL);
}


void
FrWindow::set_batch__extract(
    const char* filename,
    const char* dest_dir) {

    g_return_if_fail(filename != NULL);

    append_batch_action(FR_BATCH_ACTION_LOAD,
                        g_strdup(filename),
                        (GFreeFunc) g_free);
    if(dest_dir != NULL)
        append_batch_action(FR_BATCH_ACTION_EXTRACT,
                            extract_to_data_new(dest_dir),
                            (GFreeFunc) extract_data_free);
    else
        append_batch_action(FR_BATCH_ACTION_EXTRACT_INTERACT,
                            NULL,
                            NULL);
    append_batch_action(FR_BATCH_ACTION_CLOSE,
                        NULL,
                        NULL);
}


void
FrWindow::set_batch__add(
    const char* archive,
    GList*      file_list) {
    priv->batch_adding_one_file = (file_list->next == NULL) && (uri_is_file((char*)file_list->data));

    if(archive != NULL)
        append_batch_action(FR_BATCH_ACTION_LOAD,
                            g_strdup(archive),
                            (GFreeFunc) g_free);
    else
        append_batch_action(FR_BATCH_ACTION_OPEN,
                            file_list,
                            NULL);
    append_batch_action(FR_BATCH_ACTION_ADD,
                        file_list,
                        NULL);
    append_batch_action(FR_BATCH_ACTION_CLOSE,
                        NULL,
                        NULL);
}
