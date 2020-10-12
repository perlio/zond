/*
zond (fs_tree.c) - Akten, Beweisstücke, Unterlagen
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

#include "../enums.h"
#include "../global_types.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/baum.h"
#include "../99conv/general.h"

#include "../20allgemein/oeffnen.h"

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>


gchar*
fs_tree_get_basename( Projekt* zond, GtkTreeIter* iter )
{
    gchar* basename = NULL;

    gtk_tree_model_get( gtk_tree_view_get_model( zond->treeview[BAUM_FS] ), iter, 1,
            &basename, -1 );

    return basename;
}


gchar*
fs_tree_get_rel_path( Projekt* zond, GtkTreeIter* iter )
{
    GtkTreeIter iter_parent;
    gchar* rel_path = NULL;

    if ( iter )
    {
        GtkTreeIter* iter_child = gtk_tree_iter_copy( iter );
        gboolean parent = FALSE;

        do
        {
            parent = FALSE;
            gchar* path_segment = fs_tree_get_basename( zond, iter_child );

            rel_path = prepend_string( rel_path, path_segment );

            if ( gtk_tree_model_iter_parent( gtk_tree_view_get_model( zond->treeview[BAUM_FS] ), &iter_parent, iter_child ) )
            {
                parent = TRUE;
                gtk_tree_iter_free( iter_child );
                iter_child = gtk_tree_iter_copy( &iter_parent );

                rel_path = prepend_string( rel_path, g_strdup( "/" ) );
            }
        }
        while ( parent );

        gtk_tree_iter_free( iter_child );
    }

    return rel_path;
}


gchar*
fs_tree_get_full_path( Projekt* zond, GtkTreeIter* iter )
{
    gchar* path_dir = NULL;

    gchar* rel_path = fs_tree_get_rel_path( zond, iter );
    path_dir = g_strconcat( g_strdup( zond->project_dir ), "/", NULL );
    path_dir = add_string( path_dir, rel_path );

    return path_dir;
}


/** iter zeigt auf Verzeichnis, was zu füllen ist
    Es wurde bereits getestet, ob das Verzeichnis bereits geladen wurde
**/
gint
fs_tree_load_dir( Projekt* zond, GtkTreeIter* iter, gchar** errmsg )
{
    //path des directory aus tree holen
    gchar* path_dir = NULL;

    path_dir = fs_tree_get_full_path( zond, iter ); //keine Fehlerrückgabe0

    GError* error = NULL;

    GFile* dir = g_file_new_for_path( path_dir );
    g_free( path_dir );

    GFileEnumerator* enumer = g_file_enumerate_children( dir, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    g_object_unref( dir );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    GFile* child = NULL;
    GFileInfo* info_child = NULL;
    GtkTreeIter iter_new;

    while ( 1 )
    {
        if ( !g_file_enumerator_iterate( enumer, &info_child, &child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( child ) //es gibt noch Datei in Verzeichnis
        {
            //child in tree einfügen
            gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[BAUM_FS] )), &iter_new, iter, -1 );
            gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model(
                    zond->treeview[BAUM_FS] )), &iter_new,
                    0, g_file_info_get_icon( info_child ),
                    1, g_file_info_get_display_name( info_child ), -1 );

            //falls directory: überprüfen ob leer, falls nicht, dummy als child
            GFileType type = g_file_info_get_file_type( info_child );
            if ( type == G_FILE_TYPE_DIRECTORY )
            {
                GFileEnumerator* enumer_child = g_file_enumerate_children( child, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
                if ( !enumer_child )
                {
                    if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                            error->message, NULL );
                    g_error_free( error );
                    g_object_unref( enumer );

                    return -1;
                }

                GFile* grand_child = NULL;
                GtkTreeIter newest_iter;

                if ( !g_file_enumerator_iterate( enumer_child, NULL, &grand_child, NULL, &error ) )
                {
                    if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                            error->message, NULL );
                    g_error_free( error );
                    g_object_unref( enumer );
                    g_object_unref( enumer_child );

                    return -1;
                }
                g_object_unref( enumer_child );

                if ( grand_child ) gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[BAUM_FS] )), &newest_iter, &iter_new, -1 );
            }
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    return 0;
}


GFile*
fs_insert_dir( GFile* file, gboolean copy, gchar** errmsg )
{
    GError* error = NULL;
    gint zaehler = 0;
    GFile* file_dir = NULL;
    gchar* basename = NULL;

    GFile* file_parent = g_file_get_parent( file );

    file_dir = g_file_dup( file );
    basename = g_file_get_basename( file_dir );

    do
    {
        gboolean suc = FALSE;

        suc = g_file_make_directory( file_dir, NULL, &error );
        if ( suc ) break;
        else
        {
            if ( !g_error_matches( error, G_IO_ERROR, G_IO_ERROR_EXISTS) )
            {
                g_object_unref( file_dir );
                g_object_unref( file_parent );
                if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf "
                        "g_file_make_directory:\n", error->message, NULL );
                g_error_free( error );

                return NULL;
            }
            else if ( !suc && g_error_matches( error, G_IO_ERROR, G_IO_ERROR_EXISTS ) )
            {
                gchar* basename_new_try = NULL;
                gchar* zusatz = NULL;

                if ( copy && zaehler == 0 ) basename_new_try =
                        g_strconcat( basename, " - Kopie", NULL );
                else if ( copy && zaehler > 0 )
                {
                    zusatz = g_strdup_printf( " (%i)", zaehler + 1 );
                    basename_new_try = g_strconcat( basename, "- Kopie", zusatz, NULL );
                }
                else if ( !copy )
                {
                    zusatz = g_strdup_printf( " (%i)", zaehler + 2 );
                    basename_new_try = g_strconcat( basename, zusatz, NULL );
                }

                g_object_unref( file_dir );
                g_free( zusatz );
                g_clear_error( &error );

                zaehler++;

                file_dir = g_file_get_child( file_parent, basename_new_try );
                g_free( basename_new_try );

                continue;
            }
        }
    }
    while ( 1 );

    g_free( basename );
    g_object_unref( file_parent );

    return file_dir;
}


gint
fs_tree_insert_dir( Projekt* zond, gboolean child, gchar** errmsg )
{
    gchar* path = NULL;
    GFile* file = NULL;
    GFile* parent = NULL;
    GFileType type = G_FILE_TYPE_UNKNOWN;

    GtkTreeIter* iter = baum_abfragen_aktuellen_cursor( zond->treeview[BAUM_FS] );

    if ( !iter ) return 0;

    path = fs_tree_get_full_path( zond, iter );
    file = g_file_new_for_path( path );
    g_free( path );
    type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL );

    if ( !(type == G_FILE_TYPE_DIRECTORY) && child ) //keine Verzeichnis in Datei
    {
        g_object_unref( file );
        gtk_tree_iter_free( iter );

        return 0;
    }

    if ( child ) parent = file;
    else
    {
        parent = g_file_get_parent( file );
        g_object_unref( file );
    } //nur noch parent muß unrefed werden - file wurde übernommen

    GFile* file_dir = g_file_get_child( parent, "Neues Verzeichnis" );

    GFile* file_new_dir = fs_insert_dir( file_dir, FALSE, errmsg );
    g_object_unref( file_dir );
    if ( !file_new_dir )
    {
        gtk_tree_iter_free( iter );
        ERROR_PAO( "fs_insert_dir" )
    }

    //In Baum tun
    GtkTreeIter* iter_new = NULL;

    iter_new = baum_einfuegen_knoten( zond->treeview[BAUM_FS], iter, child );

    gtk_tree_iter_free( iter );

    GIcon* icon = g_icon_new_for_string( "folder", NULL );
    gchar* node_text = g_file_get_basename( file_new_dir );
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_FS] )), iter_new, 0, icon, 1, node_text, -1 );

    g_object_unref( file_new_dir );
    g_object_unref( icon );

    baum_setzen_cursor( zond, BAUM_FS, iter_new );

    gtk_tree_iter_free( iter_new );

    return 0;
}


gint
fs_tree_create_sojus_zentral( Projekt* zond, gchar** errmsg )
{/*
    gint rc = 0;
    gchar* sql = NULL;
    gchar* errmsg_ii = NULL;

    //Test, ob sojus_zentral.db besteht
    gchar* sojus_zentral = g_build_filename( , "sojus_zentral.db", NULL );
    rc = sqlite3_open_v2( sojus_zentral, &zond->sojus_zentral, SQLITE_OPEN_READWRITE, NULL );
    g_free( sojus_zentral );
    if ( rc == SQLITE_OK ) return 0;

    if ( rc != SQLITE_CANTOPEN )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open_v2:\n",
                sqlite3_errstr( rc ), NULL );
        return -1;
    }
    else
    {
        gint res = abfrage_frage( zond->app_window, "Sojus-Zentraldatenbank unter root nicht angelegt", "Anlegen?", NULL );
        if ( res != GTK_RESPONSE_YES)
        {
            if ( errmsg ) *errmsg = g_strdup( "Ohne Sojus-Zentraldatenbank kein "
                    "Betrieb möglich -\nDa Sie nicht kooperieren, wird das Programm beendet" );
            return -1;
        }
    }

    sojus_zentral = g_build_filename( zond->root, "sojus_zentral.db", NULL );
    rc = sqlite3_open_v2( sojus_zentral, &zond->sojus_zentral, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL );
    g_free( sojus_zentral );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open_v2:\n",
                sqlite3_errstr( rc ), NULL );
                printf("%i\n", rc );
        return -1;
    }

    //Tabellenstruktur erstellen
    sql = "CREATE TABLE dateien ("
            "guuid	TEXT NOT NULL, "
            "path TEXT NOT NULL UNIQUE, "
            "PRIMARY KEY(guuid) );"

            "CREATE TABLE anbindungen ("
            "projekt TEXT NOT NULL, "
            "datei	TEXT NOT NULL, "
            "FOREIGN KEY(datei) REFERENCES dateien (guuid) );";

    rc = sqlite3_exec( zond->sojus_zentral, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }
*/
    return 0;
}


gint
fs_tree_remove_node( Projekt* zond, GFile* file, GtkTreeIter* iter, gchar** errmsg )
{
    gint rc = 0;
    GFileEnumerator* file_enum = NULL;
    GError* error = NULL;

    file_enum = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );

    //path ist kein directory
    if ( !file_enum )
    {
        //anderer Fehler als ist kein dir
        if ( error->code != G_IO_ERROR_NOT_DIRECTORY )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                    error->message, NULL );
            g_error_free( error );

            return -1;
        }
        else //Fehler: einfach kein dir
        {
            g_error_free( error );

            gchar* uri = g_file_get_uri( file );
            gchar* uri_unesc = g_uri_unescape_string( uri, NULL );
            g_free( uri );
            gchar* rel_path = g_strdup( uri_unesc + 8 + strlen( zond->project_dir ) );
            g_free( uri_unesc );

            rc = db_get_node_id_from_rel_path( zond, rel_path, errmsg );
            g_free( rel_path );

            if ( rc == -1 ) ERROR_PAO( "db_get_node_id_from_rel_path" )

            if ( rc > 0 ) return 1; //rel_path ist angebunden

            if ( !g_file_delete( file, NULL, &error ) )
            {
                if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_delete:\n",
                        error->message, NULL );
                g_error_free( error );

                return -1;
            }

            if ( iter ) gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[BAUM_FS] )), iter );

            return 0;
        }
    }

    //ist directory
    GFile* child = NULL;
    gboolean rest = FALSE;

    while ( 1 )
    {
        if ( !g_file_enumerator_iterate( file_enum, NULL, &child, NULL, &error ) )
        { //Verzeichnis enthät keine Dateien
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( file_enum );

            return -1;
        }

        if ( child ) //es gibt noch Datei in Verzeichnis
        {
            rc = fs_tree_remove_node( zond, child, NULL, errmsg );
            if ( rc == -1 )
            {
                g_object_unref( file_enum );

                return -1;
            }
            else if ( rc == 1 ) rest = TRUE;
        } //ende if ( child )
        else break;
    }

    g_object_unref( file_enum );

    if ( !rest )
    {
        if ( !g_file_delete( file, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_delete:\n",
                    error->message, NULL );
            g_error_free( error );

            return -1;
        }
        if ( iter ) gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[BAUM_FS] )), iter );
    }
    else if ( iter )
    {
        //Merken, ob row expanded war
        gboolean expanded = FALSE;
        GtkTreePath* path = NULL;
        path = gtk_tree_model_get_path( gtk_tree_view_get_model( zond->treeview[BAUM_FS] ), iter );
        expanded = gtk_tree_view_row_expanded( zond->treeview[BAUM_FS], path );
        gtk_tree_path_free( path );

        //Verzeichnis im Baum leeren
        GtkTreeIter iter_children = { 0 };

        if ( gtk_tree_model_iter_children( gtk_tree_view_get_model( zond->treeview[BAUM_FS] ), &iter_children, iter ) )
        {
            while ( gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[BAUM_FS] )), &iter_children ) );
        }

        rc = fs_tree_load_dir( zond, iter, errmsg );
        if ( rc ) ERROR_PAO( "fs_tree_load_dir" )

        if ( expanded ) expand_row( zond, BAUM_FS, iter );
    }

    return 0;
}
