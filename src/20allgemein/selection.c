/*
zond (selection.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "../global_types.h"
#include "../error.h"

#include "../99conv/baum.h"
#include "../99conv/general.h"
#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/db_zu_baum.h"

#include "../20allgemein/fs_tree.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <sqlite3.h>


static gint
selection_foreach( Projekt* zond, Baum baum, GPtrArray* refs,
        gint (*foreach) ( Projekt*, Baum, GtkTreeIter*, gint, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    if ( !refs ) return 0;

    for ( gint i = 0; i < refs->len; i++ )
    {
        GtkTreeIter iter_ref;
        gboolean success = FALSE;
        gint rc = 0;
        gint node_id = 0;

        GtkTreeRowReference* ref = g_ptr_array_index( refs, i );
        GtkTreePath* path = gtk_tree_row_reference_get_path( ref );
        if ( !path ) continue;

        success = gtk_tree_model_get_iter( gtk_tree_view_get_model( zond->treeview[baum] ), &iter_ref, path );
        gtk_tree_path_free( path );
        if ( !success )
        {
            if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_"
                    "iter:\nKonnte keinen gültigen Iter ermitteln" );
            return -1;
        }

        if ( (baum == BAUM_INHALT) || (baum == BAUM_AUSWERTUNG) )
                gtk_tree_model_get( gtk_tree_view_get_model(
                zond->treeview[baum] ), &iter_ref, 2, &node_id, -1 );

        rc = foreach( zond, baum, &iter_ref, node_id, data, errmsg );
        if ( rc ) ERROR_PAO( "selection_cb_foreach" );
    }

    return 0;
}


typedef struct {
    Projekt* zond;
    Baum baum;
    gint parent_id;
    gint older_sibling_id;
} SSelectionVerschieben;


static gint
selection_foreach_verschieben( Projekt* zond, Baum baum, GtkTreeIter* iter,
        gint node_id, gpointer data, gchar** errmsg )
{
    gint rc = 0;

    SSelectionVerschieben* s_selection = (SSelectionVerschieben*) data;

    if ( baum == BAUM_INHALT )
    {
        gint typ = db_knotentyp_abfragen( zond, baum, node_id, errmsg );
        if ( typ == -1 ) ERROR_PAO( "db_knotentyp_abfragen" )
        else if ( typ == 2 ) return 0;
    }

    rc = knoten_verschieben( s_selection->zond, s_selection->baum, node_id, s_selection->parent_id,
            s_selection->older_sibling_id, errmsg );
    if ( rc ) ERROR_PAO ( "knoten_verschieben" )

    s_selection->older_sibling_id = node_id;

    return 0;
}


//Ist immer verschieben innerhalb des Baums
static gint
selection_verschieben( Projekt* zond, Baum baum, gint anchor_id, gboolean kind,
        gchar** errmsg )
{
    gint rc = 0;
    SSelectionVerschieben s_selection = { zond, baum, 0, 0 };

    if ( kind ) s_selection.parent_id = anchor_id;
    else
    {
        s_selection.parent_id = db_get_parent( zond, baum, anchor_id, errmsg );
        if ( s_selection.parent_id < 0 ) ERROR_PAO( "db_get_parent" )

        s_selection.older_sibling_id = anchor_id;
    }

    rc = selection_foreach( zond, baum, zond->clipboard.arr_ref, selection_foreach_verschieben, &s_selection, errmsg );
    if ( rc ) ERROR_PAO( "selection_foreach" )

    //Alte Auswahl löschen
    if ( zond->clipboard.arr_ref->len > 0 ) g_ptr_array_remove_range( zond->clipboard.arr_ref,
            0, zond->clipboard.arr_ref->len );

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[baum]) );

    GtkTreeIter* iter = baum_abfragen_iter( zond->treeview[baum], s_selection.older_sibling_id );

    if ( iter )
    {
        expand_row( zond, baum, iter );
        baum_setzen_cursor( zond, baum, iter );

        gtk_tree_iter_free( iter );
    }

    return 0;
}


typedef struct {
    Projekt* zond;
    GtkTreeIter* iter_dest;
    gint anchor_id;
    gboolean kind;
} SSelectionKopieren;


gint static
selection_foreach_kopieren( Projekt* zond, Baum baum, GtkTreeIter* iter, gint node_id, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter_new = NULL;
    gint new_node_id = 0;

    SSelectionKopieren* s_selection = (SSelectionKopieren*) data;

    rc = db_begin( s_selection->zond, errmsg );
    if ( rc ) ERROR_PAO( "db_begin" )

    new_node_id = db_kopieren_nach_auswertung_mit_kindern( s_selection->zond, FALSE, baum,
            node_id, s_selection->anchor_id, s_selection->kind, errmsg );
    if ( new_node_id == -1 ) ERROR_PAO_ROLLBACK( "db_kopieren_nach_auswertung_mit_kindern (urspr. Aufruf)" )

    iter_new = db_baum_knoten_mit_kindern( s_selection->zond, FALSE,
            BAUM_AUSWERTUNG, new_node_id, s_selection->iter_dest, s_selection->kind, errmsg );
    if ( !iter_new ) ERROR_PAO_ROLLBACK( "db_baum_knoten_mit_kindern (urspr. Aufruf)" )

    rc = db_commit( zond, errmsg );
    if ( rc ) ERROR_PAO_ROLLBACK( "db_commit" )

    if ( s_selection->iter_dest ) gtk_tree_iter_free( s_selection->iter_dest );
    s_selection->iter_dest = gtk_tree_iter_copy( iter_new );
    gtk_tree_iter_free( iter_new );

    s_selection->anchor_id = new_node_id;
    s_selection->kind = FALSE;

    return 0;
}


//Ziel ist immer BAUM_AUSWERTUNG
static gint
selection_kopieren( Projekt* zond, Baum baum_von, gint anchor_id, gboolean kind, gchar** errmsg )
{
    gint rc = 0;

    SSelectionKopieren s_selection = { zond, NULL, anchor_id, kind };
    s_selection.iter_dest = baum_abfragen_aktuellen_cursor( zond->treeview[BAUM_AUSWERTUNG] );

    rc = selection_foreach( zond, baum_von, zond->clipboard.arr_ref,
            selection_foreach_kopieren, &s_selection, errmsg );

    expand_row( zond, BAUM_AUSWERTUNG, s_selection.iter_dest );
    baum_setzen_cursor( zond, BAUM_AUSWERTUNG, s_selection.iter_dest );
    if ( s_selection.iter_dest ) gtk_tree_iter_free( s_selection.iter_dest );

    if ( rc ) ERROR_PAO( "selection_foreach" )

    return 0;
}


/** Dateien oder Ordner anbinden **/
static gchar*
selection_get_icon_name( Projekt* zond, GFile* file )
{
    GFileInfo* info = NULL;
    GIcon* icon = NULL;
    gchar* icon_name = NULL;
    gchar* content_type = NULL;

    info = g_file_query_info( file, "*", G_FILE_QUERY_INFO_NONE, NULL, NULL );
    if ( !info ) return g_strdup( "dialog-error" );

    content_type = (gchar*) g_file_info_get_content_type( info );

    if ( g_content_type_is_mime_type( content_type, "application/pdf" ) ) icon_name = g_strdup( "pdf" );
    else if ( g_content_type_is_mime_type( content_type, "audio" ) ) icon_name = g_strdup( "audio-x-generic" );
    else
    {
        icon = g_file_info_get_icon( info );
        icon_name = g_icon_to_string( icon );
        g_object_unref( icon );
    }

    g_free( content_type );
 //   g_object_unref( info );

    return icon_name;
}


static gchar*
selection_get_rel_path_from_file( Projekt* zond, GFile* file )
{
    //Überprüfung, ob schon angebunden
    gchar* rel_path = NULL;
    gchar* abs_path = g_file_get_path( (GFile*) file );

#ifdef _WIN32
    abs_path = g_strdelimit( abs_path, "\\", '/' );
#endif // _WIN32

    rel_path = full_path_to_rel_path( zond, abs_path );
    g_free( abs_path );

    return rel_path;
}


static gint
selection_datei_einfuegen_in_db( Projekt* zond, GFile* file, gint node_id,
        gboolean child, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;
    gchar* rel_path = NULL;

    gchar* icon_name = selection_get_icon_name( zond, (GFile*) file );
    gchar* basename = g_file_get_basename( (GFile*) file );

    new_node_id = db_insert_node( zond, BAUM_INHALT, node_id, child, icon_name,
            basename, errmsg );

    g_free( basename );
    g_free( icon_name );

    if ( new_node_id == -1 )
    {
        if ( errmsg ) *errmsg = prepend_string( *errmsg,
                g_strdup( "Bei Aufruf db_insert_node:\n" ) );

        return -1;
    }

    rel_path = selection_get_rel_path_from_file( zond, file );
    rc = db_set_datei( zond, new_node_id, rel_path, errmsg );
    g_free( rel_path );
    if ( rc ) ERROR_PAO( "db_set_datei" )

    return new_node_id;
}


/** Fehler: -1
    eingefügt: node_id
    nicht eingefügt, weil schon angebunden: 0 **/
static gint
selection_datei_anbinden( Projekt* zond, GFile* file, gint node_id, GtkTreeIter* iter,
        gboolean child, GtkTreeIter** new_iter, gint* zaehler, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    gchar* rel_path = selection_get_rel_path_from_file( zond, file );
    rc = db_get_node_id_from_rel_path( zond, rel_path, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR_PAO( "db_get_node_id_from_rel_path" )
    else if ( rc > 0 ) return 0;

    //datei in db einfügen
    rc = db_begin( zond, errmsg );
    if ( rc ) ERROR_PAO( "db_begin" )

    new_node_id = selection_datei_einfuegen_in_db( zond, file, node_id,
            child, errmsg );
    if ( new_node_id == -1 ) ERROR_PAO_ROLLBACK( "selection_datei_einfuegen_in_db" )

    //datei in baum_inhalt einfügen
    *new_iter = db_baum_knoten( zond, BAUM_INHALT, new_node_id, iter, child, errmsg );
    if ( !(*new_iter) ) ERROR_PAO_ROLLBACK( "db_baum_knoten" )

    rc = db_commit( zond, errmsg );
    if ( rc )
    {
        gtk_tree_iter_free( *new_iter );

        ERROR_PAO_ROLLBACK( "db_commit" )
    }

    (*zaehler)++;

    expand_row( zond, BAUM_INHALT, *new_iter );
    gtk_tree_view_columns_autosize( ((Projekt*) zond)->treeview[BAUM_INHALT] );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    return new_node_id;
}


/*  Fehler: Rückgabe -1
**  ansonsten: Id des zunächst erzeugten Knotens  */
static gint
selection_ordner_anbinden_rekursiv( Projekt* zond, GFile* file, gint node_id,
        GtkTreeIter* iter, gboolean child, GtkTreeIter** new_iter, gint* zaehler,
        gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;
    GError* error = NULL;

    rc = db_begin( zond, errmsg );
    if ( rc ) ERROR_PAO( "db_begin" )

    gchar* basename = g_file_get_basename( file );

    new_node_id = db_insert_node( zond, BAUM_INHALT, node_id, child, "folder",
            basename, errmsg );
    g_free( basename );
    if ( new_node_id == -1 ) ERROR_SQL_ROLLBACK( "db_insert_node" )

    //datei in baum_inhalt einfügen
    *new_iter = db_baum_knoten( zond, BAUM_INHALT, new_node_id, iter, child, errmsg );
    if ( !(*new_iter) ) ERROR_PAO_ROLLBACK( "db_baum_knoten" )

    rc = db_commit( zond, errmsg );
    if ( rc ) ERROR_SQL_ROLLBACK( "db_commit" )

    GFileEnumerator* enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    //new_anchor kopieren, da in der Schleife verändert wird
    //es soll aber der soeben erzeugt Punkt zurückgegegen werden
    GFile* file_child = NULL;
    GFileInfo* info_child = NULL;
    gint new_node_id_loop_tmp = 0;
    GtkTreeIter* new_iter_loop_tmp = NULL;;

    gint new_node_id_loop = new_node_id;
    GtkTreeIter* new_iter_loop = gtk_tree_iter_copy( *new_iter );

    child = TRUE;

    while ( 1 )
    {
        if ( !g_file_enumerator_iterate( enumer, &info_child, &file_child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( file_child ) //es gibt noch Datei in Verzeichnis
        {
            GFileType type = g_file_info_get_file_type( info_child );
            if ( type == G_FILE_TYPE_DIRECTORY )
            {
                new_node_id_loop_tmp = selection_ordner_anbinden_rekursiv( zond, file_child,
                        new_node_id_loop, new_iter_loop, child, &new_iter_loop_tmp,
                        zaehler, errmsg );

                if ( new_node_id_loop_tmp == -1 )
                {
                    gtk_tree_iter_free( new_iter_loop );
                    g_object_unref( enumer );

                    return -1;
                }
            }
            else if ( type == G_FILE_TYPE_REGULAR )
            {
                new_node_id_loop_tmp = selection_datei_anbinden( zond, file_child,
                        new_node_id_loop, new_iter_loop, child, &new_iter_loop_tmp, zaehler, errmsg );

                if ( new_node_id_loop_tmp == -1 )
                {
                    if ( errmsg ) *errmsg = prepend_string( *errmsg,
                            g_strdup( "Bei Aufruf datei_anbinden:\n" ) );
                    gtk_tree_iter_free( new_iter_loop );
                    g_object_unref( enumer );

                    return -1;
                }
                else if ( new_node_id_loop_tmp == 0 ) continue;
            }

            expand_row( zond, BAUM_INHALT, new_iter_loop_tmp );

            new_node_id_loop = new_node_id_loop_tmp;

            gtk_tree_iter_free( new_iter_loop );
            new_iter_loop = gtk_tree_iter_copy( new_iter_loop_tmp );
            gtk_tree_iter_free( new_iter_loop_tmp );

            child = FALSE;
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );
    gtk_tree_iter_free( new_iter_loop );

    return new_node_id;
}


typedef struct {
    Projekt* zond;
    GtkTreeIter* iter;
    gint anchor_id;
    gboolean kind;
    gint zaehler;
} SSelectionAnbinden;


static gint
selection_foreach_anbinden( Projekt* zond, Baum baum, GtkTreeIter* iter, gint node_id, gpointer data, gchar** errmsg )
{
    SSelectionAnbinden* s_selection = (SSelectionAnbinden*) data;

    //datei ermitteln und anbinden
    gchar* full_path = fs_tree_get_full_path( s_selection->zond, iter );
    GFile* file = g_file_new_for_path( full_path );
    GtkTreeIter* iter_new = NULL;
    gint new_node_id = 0;

    if ( g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL ) ==
            G_FILE_TYPE_DIRECTORY )
    {
        new_node_id = selection_ordner_anbinden_rekursiv( s_selection->zond, file, s_selection->anchor_id,
                s_selection->iter, s_selection->kind, &iter_new, &s_selection->zaehler, errmsg );
        g_free( full_path );
        g_object_unref( file );
        if ( new_node_id == -1 ) ERROR_PAO( "datei_ordner_anbinden_rekursiv" )
    }
    else
    {
        new_node_id = selection_datei_anbinden( s_selection->zond, file, s_selection->anchor_id, s_selection->iter, s_selection->kind,
                &iter_new, &s_selection->zaehler, errmsg );
        g_free( full_path );
        g_object_unref( file );
        if ( new_node_id == -1 ) ERROR_PAO( "datei_anbinden" )
    }

    if ( s_selection->iter ) gtk_tree_iter_free( s_selection->iter );
    s_selection->iter = gtk_tree_iter_copy( iter_new );
    gtk_tree_iter_free( iter_new );

    //nur bei erstem Durchgang child von root_node - danach darunter
    s_selection->anchor_id = new_node_id;
    s_selection->kind = FALSE;

    return 0;
}


static gint
selection_anbinden( Projekt* zond, gint anchor_id, gboolean kind, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter = NULL;
    SSelectionAnbinden s_selection = { 0 };

    //cursor in baum_inhalt ermitteln
    iter = baum_abfragen_aktuellen_cursor( zond->treeview[BAUM_INHALT] );

    s_selection.zond = zond;
    s_selection.iter = iter;
    s_selection.anchor_id = anchor_id;
    s_selection.kind = kind;
    s_selection.zaehler = 0;

    rc = selection_foreach( zond, BAUM_FS, zond->clipboard.arr_ref, selection_foreach_anbinden, &s_selection, errmsg );
    if ( s_selection.iter )
    {
        baum_setzen_cursor( zond, BAUM_INHALT, s_selection.iter );
        gtk_tree_iter_free( s_selection.iter );
    }
    if ( rc ) ERROR_PAO( "selection_foreach" )

    gchar* anzahl = g_strdup_printf( "%d Dateien angebunden", s_selection.zaehler );
    meldung( zond->app_window, anzahl, NULL );
    g_free( anzahl );

    return 0;
}


static gchar*
selection_move_file( Projekt* zond, GFile* file_source, GFile* file_parent, gboolean del,
        gchar** errmsg )
{
    gchar* basename = NULL;
    gint zaehler = 0;
    gboolean (*verschieben) ( GFile*, GFile*, GFileCopyFlags, GCancellable*,
            GFileProgressCallback, gpointer, GError** ); //function-ptr

    //moven oder kopieren
    if ( del ) verschieben = g_file_move;
    else verschieben = g_file_copy;

    //dest_file
    basename = g_file_get_basename( file_source );
    do
    {
        gchar* basename_new = NULL;
        GFile* file_dest = NULL;
        gboolean success = FALSE;
        GError* error = NULL;

        if ( zaehler == 0 ) basename_new = g_strdup( basename );
        else if ( zaehler == 1 ) basename_new = g_strconcat( basename, " - Kopie", NULL );
        else if ( zaehler > 1 )
        {
            gchar* zusatz = NULL;
            zusatz = g_strdup_printf( " - Kopie (%d)", zaehler - 1 );
            basename_new = g_strconcat( basename, zusatz, NULL );
            g_free( zusatz );
        }

        file_dest = g_file_get_child( file_parent, basename_new );

        success = verschieben ( file_source, file_dest, G_FILE_COPY_NONE, NULL,
                NULL, NULL, &error );

        //db-datei ändern
        if ( success && del )
        {
            gint rc = 0;
            gchar* rel_path_old = NULL;
            gchar* rel_path_new = NULL;

            rel_path_old = selection_get_rel_path_from_file( zond, file_source );
            rel_path_new = selection_get_rel_path_from_file( zond, file_dest );

            rc = db_update_path( zond, rel_path_old, rel_path_new, errmsg );
            g_free( rel_path_old );
            g_free( rel_path_new );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_update_path:\n", *errmsg, NULL );

                gchar* errmsg_ii = NULL;
                rc = db_rollback_both( zond, &errmsg_ii );
                if ( rc )
                {
                    errmsg_ii = prepend_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
                    if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
                    g_free( errmsg_ii );
                }

                g_free( basename );
                g_free( basename_new );
                g_object_unref( file_dest );

                return NULL;
            }
        }
        g_object_unref( file_dest );

        if ( success )
        {
            g_free( basename );
            basename = basename_new;

            break;
        }
        else
        {
            g_free( basename_new );

            if ( g_error_matches( error, G_IO_ERROR, G_IO_ERROR_EXISTS ) )
            {
                g_clear_error( &error );
                zaehler++;

                continue;
            }
            else
            {
                if ( errmsg && error ) *errmsg = g_strconcat( "Bei Aufruf g_file_move/copy:\n",
                        error->message, NULL );

                g_free( basename );
                g_error_free( error );

                return NULL;
            }
        }
    } while ( 1 );

    return basename;
}


static gchar*
selection_copy_dir( Projekt* zond, GFile* file_source, GFile* file_parent, gchar** errmsg )
{
    gchar* basename = NULL;
    gchar* basename_new = NULL;
    GError* error = NULL;
    GFile* file_dir = NULL;
    GFile* file_dir_new = NULL;

    //path_new_dir = file_parent + basename(file_source)
    basename = g_file_get_basename( file_source );

    file_dir = g_file_get_child( file_parent, basename );
    g_free( basename );

    file_dir_new = fs_insert_dir( file_dir, TRUE, errmsg );
    g_object_unref( file_dir );

    if ( !file_dir_new )  return NULL;

    GFileEnumerator* enumer = g_file_enumerate_children( file_source, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        g_object_unref( file_dir_new );
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return NULL;
    }

    GFile* child = NULL;
    GFileInfo* info_child = NULL;

    while ( 1 )
    {
        if ( !g_file_enumerator_iterate( enumer, &info_child, &child, NULL, &error ) )
        {
            g_object_unref( file_dir_new );
            g_object_unref( enumer );
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );

            return NULL;
        }

        if ( child ) //es gibt noch Datei in Verzeichnis
        {
            GFileType type = g_file_info_get_file_type( info_child );
            if ( type == G_FILE_TYPE_DIRECTORY )
            {
                if ( !fs_insert_dir( child, TRUE, errmsg ) )
                {
                    g_object_unref( enumer );
                    g_object_unref( file_dir_new );

                    ERROR_PAO_R( "fs_insert_dir", NULL );
                }
            }
            else if ( type == G_FILE_TYPE_REGULAR )
            {
                if ( !selection_move_file( zond, child, file_dir_new, FALSE, errmsg ) )
                {
                    g_object_unref( file_dir_new );
                    g_object_unref( enumer );

                    ERROR_PAO_R( "fs_insert_dir", NULL );
                }

            }
        } //ende if ( child )
        else break;
    }

    basename_new = g_file_get_basename( file_dir_new );

    g_object_unref( file_dir_new );
    g_object_unref( enumer );

    return basename_new;
}


typedef struct {
    Projekt* zond;
    GFile* file_parent;
    GtkTreeIter* iter_cursor;
    gboolean kind;
    gboolean expanded;
    gboolean inserted;
} SSelectionFSTree;


static gint
selection_foreach_baum_fs_to_baum_fs( Projekt* zond, Baum baum,
        GtkTreeIter* iter, gint node_id, gpointer data, gchar** errmsg )
{
    GFile* file_source = NULL;
    gchar* basename = NULL;
    gchar* path_source = NULL;
    gboolean same_dir = FALSE;
    gboolean dir_with_children = FALSE;

    SSelectionFSTree* s_selection = (SSelectionFSTree*) data;

    //Namen der Datei holen
    path_source = fs_tree_get_full_path( s_selection->zond, iter );

    //source-file
    file_source = g_file_new_for_path( path_source );
    g_free( path_source );

    //prüfen, ob innerhalb des gleichen Verzeichnisses verschoben/kopiert werden
    //soll - dann: same_dir = TRUE
    GFile* file_parent_source = g_file_get_parent( file_source );
    same_dir = g_file_equal( s_selection->file_parent, file_parent_source );
    g_object_unref( file_parent_source );

    //verschieben im gleichen Verzeichnis ist Quatsch
    if ( same_dir && zond->clipboard.ausschneiden )
    {
        g_object_unref( file_source );
        return 0;
    }

    //Falls Verzeichnis: Prüfen, ob Datei drinne, dann dir_with_children = TRUE
    if ( g_file_query_file_type( file_source, G_FILE_QUERY_INFO_NONE, NULL ) ==
            G_FILE_TYPE_DIRECTORY && (!s_selection->kind || s_selection->expanded) )
    {
        GError* error = NULL;
        GFile* file_out = NULL;

        GFileEnumerator* enumer = g_file_enumerate_children( file_source,
                "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
        if ( !enumer )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( file_source );

            return -1;
        }
        if ( !g_file_enumerator_iterate( enumer, NULL, &file_out, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );
            g_object_unref( file_source );

            return -1;
        }

        if ( file_out ) dir_with_children = TRUE;

        g_object_unref( enumer );
    }

    //Kopieren/verschieben
    if ( g_file_query_file_type( file_source, G_FILE_QUERY_INFO_NONE, NULL )
            != G_FILE_TYPE_REGULAR && !zond->clipboard.ausschneiden )
            basename = selection_copy_dir( zond, file_source, s_selection->file_parent, errmsg );
    else basename = selection_move_file( zond, file_source, s_selection->file_parent, zond->clipboard.ausschneiden, errmsg );

    g_object_unref( file_source );

    if ( !basename ) ERROR_PAO( "selection_move_file" )

    s_selection->inserted = TRUE;

    //Knoten müssen nur eingefügt werden, wenn Row expanded ist; sonst passiert das im callback beim Öffnen
    if ( (!s_selection->kind) || s_selection->expanded )
    {
        //Ziel-FS-tree eintragen
        GtkTreeIter* iter_new = NULL;

        iter_new = baum_einfuegen_knoten( zond->treeview[BAUM_FS],
                s_selection->iter_cursor, s_selection->kind );

        gtk_tree_iter_free( s_selection->iter_cursor );
        s_selection->iter_cursor = iter_new;
        s_selection->kind = FALSE;

        //Falls Verzeichnis mit Datei innendrin, Knoten in tree einfügen
        if ( dir_with_children )
        {
            GtkTreeIter iter_tmp;
            gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
                zond->treeview[BAUM_FS] )), &iter_tmp, s_selection->iter_cursor, -1 );
        }

        //alte Daten holen
        GIcon* icon = NULL;

        gtk_tree_model_get( gtk_tree_view_get_model( zond->treeview[BAUM_FS] ),
                iter, 0, &icon, -1 );

        //in neu erzeugten node einsetzen
        gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model(
                zond->treeview[BAUM_FS] )), s_selection->iter_cursor, 0, icon, 1, basename, -1 );

        g_object_unref( icon );
    }

    g_free( basename );

    //falls ausschneiden: in Quell-FS-Tree löschen
    if ( zond->clipboard.ausschneiden ) gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
                zond->treeview[BAUM_FS] )), iter );

    return 0;
}


static gint
selection_baum_fs_to_baum_fs( Projekt* zond, gboolean kind, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter_cursor = NULL;
    gchar* path = NULL;
    GFile* file_cursor = NULL;
    GFile* file_parent = NULL;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;
    gboolean expanded = FALSE;

    //Datei unter cursor holen
    iter_cursor = baum_abfragen_aktuellen_cursor( zond->treeview[BAUM_FS] );
    if ( !iter_cursor ) ERROR_PAO( "baum_abfragen_aktuellen_cursor:\nKein "
            "cursor gewählt" )
    path = fs_tree_get_full_path( zond, iter_cursor );
    if ( !path )
    {
        gtk_tree_iter_free( iter_cursor );
        ERROR_PAO( "fs_tree_get_full_path:\nKein Pfadname" )
    }

    file_cursor = g_file_new_for_path( path );
    g_free( path );
    file_type = g_file_query_file_type( file_cursor, G_FILE_QUERY_INFO_NONE, NULL );

    //if kind && datei != dir: Fehler
    if ( (file_type != G_FILE_TYPE_DIRECTORY) && kind )
    {
        g_object_unref( file_cursor );
        gtk_tree_iter_free( iter_cursor );
        if ( errmsg ) *errmsg = g_strdup( "Einfügen als Unterpunkt von Dateien "
                "nicht zulässig" );

        return -1;
    }
    else if ( kind ) //und Verzeichnis, ist aber ja schon die 1. Var.
    {
        //dann ist Datei unter cursor parent
        file_parent = file_cursor;

        //prüfen, ob geöffnet ist
        GtkTreePath* path = NULL;

        path = gtk_tree_model_get_path( gtk_tree_view_get_model(
                zond->treeview[BAUM_FS]), iter_cursor );
        expanded = gtk_tree_view_row_expanded( zond->treeview[BAUM_FS], path );
        gtk_tree_path_free( path );
    }
    else //unterhalb angefügt
    {
        //dann ist parent parent!
        file_parent = g_file_get_parent( file_cursor );
        g_object_unref( file_cursor );
    }

    SSelectionFSTree s_selection = { zond, file_parent, iter_cursor, kind, expanded, FALSE };

    rc = selection_foreach( zond, BAUM_FS, zond->clipboard.arr_ref,
            selection_foreach_baum_fs_to_baum_fs, (gpointer) &s_selection, errmsg );
    g_object_unref( file_parent );
    if ( rc )
    {
        gtk_tree_iter_free( s_selection.iter_cursor );
        ERROR_PAO( "selection_foreach" )
    }

    //Wenn in nicht ausgeklapptes Verzeichnis etwas eingefügt wurde:
    //Dummy einfügen
    if ( s_selection.inserted && kind && !expanded )
    {
        GtkTreeIter iter_tmp;
        gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_FS] )), &iter_tmp, s_selection.iter_cursor, -1 );
    }

    //Alte Auswahl löschen - muß vor baum_setzen_cursor geschehen,
    //da in change_row-callback ref abgefragt wird
    if ( zond->clipboard.ausschneiden && zond->clipboard.arr_ref->len > 0 )
            g_ptr_array_remove_range( zond->clipboard.arr_ref,
            0, zond->clipboard.arr_ref->len );
    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[BAUM_FS]) );

/*
    //wie Dateimanager - wenn in nicht ausgeklappten Knoten eingefügt, nix öffnen
    if ( kind && !expanded )
    {
        //Knoten, in den eingefügt wurde, erweitern
        GtkTreePath* path = NULL;

        path = gtk_tree_model_get_path( gtk_tree_view_get_model(
                zond->treeview[BAUM_FS] ), s_selection.iter_cursor );
        gtk_tree_view_expand_row( zond->treeview[BAUM_FS], path, FALSE );

        gtk_tree_path_free( path );
    } */
    //wenn nicht in nicht ausgeklapptes Verzeichnis eingefügt wurde: ausklappen
    if ( !kind || expanded ) baum_setzen_cursor( zond, BAUM_FS,
            s_selection.iter_cursor );

    gtk_tree_iter_free( s_selection.iter_cursor );

    return 0;
}


//überprüft beim verschieben, ob auf zu verschiebenden Knoten oder dessen
//Nachfahren verschoben werden soll
//Falls ja: Rückgabe 1
static gint
selection_testen_cursor_ist_abkoemmling( Projekt* zond, Baum baum )
{
    GtkTreePath* path = NULL;

    if ( zond->clipboard.arr_ref->len == 0 ) return 0;

    gtk_tree_view_get_cursor( zond->treeview[baum], &path, NULL );
    if ( !path ) return 2;

    GtkTreePath* path_sel = NULL;
    gboolean descend = FALSE;
    for ( gint i = 0; i < zond->clipboard.arr_ref->len; i++ )
    {
        path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( zond->clipboard.arr_ref, i ) );
        if ( !path_sel ) continue;
        descend = gtk_tree_path_is_descendant( path, path_sel );
        gtk_tree_path_free( path_sel );
        if ( descend )
        {
            gtk_tree_path_free( path );

            return 1;
        }
    }

    gtk_tree_path_free( path );

    return 0;
}


void
selection_paste( Projekt* zond, gboolean kind )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Baum baum = KEIN_BAUM;
    baum = baum_abfragen_aktiver_treeview( zond );

    Baum baum_selection = zond->clipboard.baum;

    //Todo: kopieren so ändern, daß zukünftig diese Beschränkung nur für
    //ausschneiden erforderlich ist (erst Knoten mit Kindern komplett kopieren,
    //und dann erst einfügen
    //Muß auf jeden Fall geprüft werden (derzeit)
            //will ich das wirklich?

    if ( baum == baum_selection ) //wenn innerhalb des gleichen Baums
    {
        rc = selection_testen_cursor_ist_abkoemmling( zond, baum );
        if ( rc == 1 )
        {
            meldung( zond->app_window, "Clipboard kann dorthin nicht "
                    "verschoben werden\nAbkömmling von zu verschiebendem "
                    "Knoten", NULL );
            return;
        }
        else if ( rc == 2 ) return;
    }

    gint anchor_id = 0;
    if ( baum != BAUM_FS ) anchor_id =
            baum_abfragen_aktuelle_node_id( zond->treeview[baum] );

    //Jetzt die einzelnen Varianten
    if ( baum_selection == BAUM_FS )
    {
        if ( baum == BAUM_FS )
        {
            rc = selection_baum_fs_to_baum_fs( zond, kind, &errmsg );
            if ( rc )
            {
                meldung( zond->app_window, "Selection kann nicht kopiert/verschoben "
                        "werden\n\nBei Aufruf selection_baum_fs_to_baum_fs:\n",
                        errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else if ( baum == BAUM_INHALT && !zond->clipboard.ausschneiden )
        {
            if ( anchor_id == 0 ) kind = TRUE;
            else
            {
                rc = hat_vorfahre_datei( zond, baum, anchor_id, kind, &errmsg );
                if ( rc == -1 )
                {
                    meldung( zond->app_window, "Bei Aufruf selection_testen_"
                            "zulaessige_vorfahren:\n\n", errmsg, NULL );
                    g_free( errmsg );

                    return;
                }
                else if ( rc == 1 )
                {
                    meldung( zond->app_window, "Clipboard kann dorthin nicht "
                            "verschoben werden\nAbkömmling von Anbindung",
                            NULL );

                    return;
                }
            }

            rc = selection_anbinden( zond, anchor_id, kind, &errmsg );
            if ( rc )
            {
                meldung( zond->app_window, "Einfügen nicht möglich -\n\n"
                        "Bei Aufruf selection_anbinden:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else meldung( zond->app_window, "Nicht zulässig", NULL );
    }
    else if ( baum_selection == BAUM_INHALT )
    {
        if ( baum == BAUM_INHALT && zond->clipboard.ausschneiden )
        {
            if ( anchor_id == 0 ) kind = TRUE;
            else
            {
                rc = hat_vorfahre_datei( zond, baum, anchor_id, kind, &errmsg );
                if ( rc == -1 )
                {
                    meldung( zond->app_window, "Bei Aufruf selection_testen_"
                            "zulaessige_vorfahren:\n\n", errmsg, NULL );
                    g_free( errmsg );

                    return;
                }
                else if ( rc == 1 )
                {
                    meldung( zond->app_window, "Clipboard kann dorthin nicht "
                            "verschoben werden\nAbkömmling von Anbindung",
                            NULL );

                    return;
                }
            }

            rc = selection_verschieben( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Verschieben nicht möglich -\n\n"
                        "Bei Aufruf selection_verschieben:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else if ( baum == BAUM_INHALT && !zond->clipboard.ausschneiden )
        {//kopieren innerhalb BAUM_INHALT = verschieben von Anbindungen

        }
        else if ( baum == BAUM_AUSWERTUNG && !zond->clipboard.ausschneiden )
        {
            rc = selection_kopieren( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Fehler in Kopieren:\n\n"
                        "Bei Aufruf selection_kopieren:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else meldung( zond->app_window, "Nicht zulässig", NULL );
    }
    else if ( baum_selection == BAUM_AUSWERTUNG )
    {
        if ( baum == BAUM_AUSWERTUNG && zond->clipboard.ausschneiden )
        {
            rc = selection_verschieben( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Bei Aufruf selection_"
                        "inhalt_nach_inhalt:\n\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else if ( baum == BAUM_AUSWERTUNG && !zond->clipboard.ausschneiden )
        {
            rc = selection_kopieren( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Fehler in Kopieren:\n\n"
                        "Bei Aufruf selection_kopieren:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else meldung( zond->app_window, "Nicht zulässig", NULL );
    }
    else meldung( zond->app_window, "Nicht zulässig", NULL );

    return;
}


static GPtrArray*
selection_get_refs( Projekt* zond, Baum baum )
{
    GList* selected = gtk_tree_selection_get_selected_rows(
            zond->selection[baum], NULL );

    if ( !selected ) return NULL;

    GPtrArray* refs = g_ptr_array_new_with_free_func( (GDestroyNotify)
            gtk_tree_row_reference_free );

    GList* list = selected;
    do g_ptr_array_add( refs, gtk_tree_row_reference_new(
            gtk_tree_view_get_model( zond->treeview[baum] ), list->data ) );
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return refs;
}


//Gibt nichts zurück; Fehlerbehandlung findet in dieser Funktion statt
void
selection_copy_or_cut( Projekt* zond, gboolean ausschneiden )
{
    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return;

    GPtrArray* refs = selection_get_refs( zond, baum );
    if ( !refs ) return;

    //wenn ausgeschnitten war, alle rows wieder normal zeichnen
    if ( zond->clipboard.ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[zond->clipboard.baum]) );

    //Alte Auswahl löschen, falls vorhanden
    g_ptr_array_unref( zond->clipboard.arr_ref );

    //clipboard setzen
    zond->clipboard.baum = baum;
    zond->clipboard.ausschneiden = ausschneiden;
    zond->clipboard.arr_ref = refs;

    if ( ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[zond->clipboard.baum]) );

    return;
}


static gint
selection_foreach_entfernen_anbindung( Projekt* zond, Baum baum,
        GtkTreeIter* iter, gint node_id, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint typ = 0;
    gint older_sibling = 0;
    gint parent = 0;
    gint child = 0;

    typ = db_knotentyp_abfragen( zond, baum, node_id, errmsg );
    if ( typ == -1 ) ERROR_PAO ( "db_knotentyp_abfragen" )
    if ( typ != 2 ) return 0;

    //herausfinden, ob zu löschender Knoten älteres Geschwister hat
    older_sibling = db_get_older_sibling( zond, baum, node_id, errmsg );
    if ( older_sibling < 0 ) ERROR_PAO( "db_get_older_sibling" )

    //Elternknoten ermitteln
    parent = db_get_parent( zond, baum, node_id, errmsg );
    if ( parent < 0 ) ERROR_PAO( "db_get_parent" )

    rc = db_begin( zond, errmsg );
    if ( rc ) ERROR_PAO( "db_begin" )

    child = 0;
    while ( (child = db_get_first_child( zond, BAUM_INHALT, node_id,
            errmsg )) )
    {
        if ( child < 0 ) ERROR_PAO_ROLLBACK( "db_get_first_child" )

        rc = knoten_verschieben( zond, baum, child, parent,
                older_sibling, errmsg );
        if ( rc == -1 ) ERROR_PAO_ROLLBACK( "knoten_verschieben" )

        older_sibling = child;
    }

    rc = db_remove_node( zond, BAUM_INHALT, node_id, errmsg );
    if ( rc ) ERROR_PAO_ROLLBACK( "db_remove_node" )

    gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[baum] )), iter );

    rc = db_commit( zond, errmsg );
    if ( rc ) ERROR_PAO_ROLLBACK( "db_commit" )

    return 0;
}


//Funktioniert nur im BAUM_INHALT - Abfrage im cb
gint
selection_entfernen_anbindung( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    GPtrArray* refs = selection_get_refs( zond, BAUM_INHALT );
    if ( !refs ) return 0;

    rc = selection_foreach( zond, BAUM_INHALT, refs,
            selection_foreach_entfernen_anbindung, NULL, errmsg );
    g_ptr_array_unref( refs );
    if ( rc ) ERROR_PAO_ROLLBACK( "selection_foreach" )

    return 0;
}


static gint
selection_foreach_loeschen( Projekt* zond, Baum baum, GtkTreeIter* iter,
        gint node_id, gpointer data, gchar** errmsg )
{
    if ( baum != BAUM_FS )
    {
        gint rc = db_remove_node( zond, baum, node_id, errmsg );
        if ( rc ) ERROR_PAO ( "db_remove_node" )

        gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
                zond->treeview[baum] )), iter );
    }
    else
    {
        gchar* full_path = NULL;
        GFile* file = NULL;

        full_path = fs_tree_get_full_path( zond, iter );
        file = g_file_new_for_path( full_path );
        g_free( full_path );

        gint rc = fs_tree_remove_node( zond, file, iter, errmsg );
        g_object_unref( file );
        if ( rc == -1 ) ERROR_PAO( "fs_tree_remove_node" )
    }

    return 0;
}


void
selection_loeschen( Projekt* zond )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return;

    GPtrArray* refs = selection_get_refs( zond, baum );
    if ( !refs ) return;

    rc = selection_foreach( zond, baum, refs, selection_foreach_loeschen, NULL, &errmsg );
    g_ptr_array_unref( refs );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler Punkte Löschen -\n\nBei Aufruf "
                "selection_foreach:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static gint
selection_foreach_change_icon_id( Projekt* zond, Baum baum, GtkTreeIter* iter,
        gint node_id, gpointer data, gchar** errmsg )
{
    gint rc = 0;

    gchar* icon_name = (gchar*) data;

    rc = db_set_icon_id( zond, baum, node_id, icon_name, errmsg );
    if ( rc ) ERROR_PAO( "db_set_icon_id" )

    //neuen icon_name im tree speichern
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[baum] )),
            iter, 0, icon_name, -1 );

    return 0;
}


void
selection_change_icon_id( Projekt* zond, const gchar* icon_name )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM || baum == BAUM_FS ) return;

    GPtrArray* refs = selection_get_refs( zond, baum );
    if ( !refs ) return;

    rc = selection_foreach( zond, baum, refs, selection_foreach_change_icon_id, (gpointer) icon_name, &errmsg );
    g_ptr_array_unref( refs );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler Ändern Icons - \n\nBei Aufruf "
                "selection_foreach:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


