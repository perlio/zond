/*
zond (init.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include "../global_types.h"
#include "../error.h"

#include "../99conv/general.h"
#include "../99conv/db_zu_baum.h"
#include "../99conv/mupdf.h"

#include "../20allgemein/project.h"
#include "../20allgemein/zieleplus.h"
#include "../20allgemein/fs_tree.h"

#include "icons.h"
#include "app_window.h"
#include "headerbar.h"

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>

#ifdef _WIN32
#include <errhandlingapi.h>
#include <libloaderapi.h>
#endif // _WIN32


static gint
init_stmts_for_sb_sz( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    /*  db_sz_get_path  */
    rc = sqlite3_prepare_v2( zond->sojus_zentral,
            "SELECT path FROM dateien "
            "WHERE guuid=?;",
            -1, &(zond->stmts_db_sz.db_sz_get_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_sz_get_path[0])" )

    return 0;
}


static void
recover( Projekt* zond, gchar* project, GApplication* app )
{
    gint rc = 0;
    gchar* path_bak = NULL;
    gchar* path_tmp = NULL;

    gchar* text_abfrage = g_strconcat( "Projekt ", project, " wurde nicht richtig "
            "geschlossen. Wiederherstellen?", NULL );
    gint res = abfrage_frage( zond->app_window, "Wiederherstellen", text_abfrage, NULL );
    g_free( text_abfrage );

    if ( res == GTK_RESPONSE_YES )
    {
        path_bak = g_strconcat( project, ".bak", NULL);
        rc = g_rename( project, path_bak );
        if ( rc ) meldung( zond->app_window, "g_rename .ZND to .bak:\n",
                strerror( errno ), NULL );
        g_free( path_bak );

        path_tmp = g_strconcat( project, ".tmp", NULL );
        rc = g_rename( path_tmp, project );
        if ( rc ) meldung( zond->app_window, "rename .tmp to .ZND:\n",
                strerror( errno ), NULL );
        else meldung( zond->app_window, project, " erfolgreich wiederhergestellt",
                NULL );
        g_free( path_tmp );
    }
    else if ( res != GTK_RESPONSE_NO) g_application_quit( app );

    g_settings_set_string( zond->settings, "project", "" );
    g_settings_set_boolean( zond->settings, "speichern", FALSE );

    return;
}


static void
set_icon( Icon* icon, const gchar* icon_name,
        const gchar* display_name )
{
    icon->icon_name = icon_name;
    icon->display_name = display_name;

    return;
}


static void
init_icons( Projekt* zond )
{
    GResource* resource = icons_get_resource( );
    g_resources_register( resource );

    GtkIconTheme* icon_theme = gtk_icon_theme_get_default( );
    gtk_icon_theme_add_resource_path( icon_theme, "/icons" );

//    zond->icon[ICON_NOTHING] = { "dialog-error", "Nix" };
    set_icon( &zond->icon[ICON_NOTHING], "dialog-error", "Nix" );
    set_icon( &zond->icon[ICON_NORMAL], "emblem-new", "Punkt" );
    set_icon( &zond->icon[ICON_ORDNER], "folder", "Ordner" );
    set_icon( &zond->icon[ICON_DATEI], "document-open", "Datei" );
    set_icon( &zond->icon[ICON_PDF], "pdf", "PDF" );
    set_icon( &zond->icon[ICON_ANBINDUNG], "anbindung", "Anbindung" );
    set_icon( &zond->icon[ICON_AKTE], "akte", "Akte" );
    set_icon( &zond->icon[ICON_EXE], "application-x-executable", "Ausführbar" );
    set_icon( &zond->icon[ICON_TEXT], "text-x-generic", "Text" );
    set_icon( &zond->icon[ICON_DOC], "x-office-document", "Writer/Word" );
    set_icon( &zond->icon[ICON_PPP], "x-office-presentation", "PowerPoint" );
    set_icon( &zond->icon[ICON_SPREAD], "x-office-spreadsheet", "Tabelle" );
    set_icon( &zond->icon[ICON_IMAGE], "emblem-photos", "Bild" );
    set_icon( &zond->icon[ICON_VIDEO], "video-x-generic", "Video" );
    set_icon( &zond->icon[ICON_AUDIO], "audio-x-generic", "Audio" );
    set_icon( &zond->icon[ICON_EMAIL], "mail-unread", "E-Mail" );
    set_icon( &zond->icon[ICON_HTML], "emblem-web", "HTML" ); //16

    //Platzhalter
    set_icon( &zond->icon[17], "process-stop", "Frei" );
    set_icon( &zond->icon[18], "process-stop", "Frei" );
    set_icon( &zond->icon[19], "process-stop", "Frei" );
    set_icon( &zond->icon[20], "process-stop", "Frei" );
    set_icon( &zond->icon[21], "process-stop", "Frei" );
    set_icon( &zond->icon[22], "process-stop", "Frei" );
    set_icon( &zond->icon[23], "process-stop", "Frei" );
    set_icon( &zond->icon[24], "process-stop", "Frei" );

    set_icon( &zond->icon[ICON_DURCHS], "system-log-out", "Durchsuchung" );
    set_icon( &zond->icon[ICON_ORT], "mark-location", "Ort" );
    set_icon( &zond->icon[ICON_PHONE], "phone", "TKÜ" );
    set_icon( &zond->icon[ICON_WICHTIG], "emblem-important", "Wichtig" );
    set_icon( &zond->icon[ICON_OBS], "camera-web", "Observation" );
    set_icon( &zond->icon[ICON_CD], "media-optical", "CD" );
    set_icon( &zond->icon[ICON_PERSON], "user-info", "Person" );
    set_icon( &zond->icon[ICON_PERSONEN], "system-users", "Personen" );
    set_icon( &zond->icon[ICON_ORANGE], "orange", "Orange" );
    set_icon( &zond->icon[ICON_BLAU], "blau", "Blau" );
    set_icon( &zond->icon[ICON_ROT], "rot", "Rot" );
    set_icon( &zond->icon[ICON_GRUEN], "gruen", "Grün" );
    set_icon( &zond->icon[ICON_TUERKIS], "tuerkis", "Türkis" );
    set_icon( &zond->icon[ICON_MAGENTA], "magenta", "Magenta" );

    return;
}


void
open_file( Projekt* zond, gpointer files )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GFile** g_file;
    g_file = (GFile**) files;

    gchar* uri = g_file_get_uri( g_file[0] );
    gchar* uri_unesc = g_uri_unescape_string( uri, NULL );
    g_free( uri );
    rc = projekt_aktivieren( zond, uri_unesc + 8, FALSE, &errmsg );
    g_free( uri_unesc );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler - Projekt kann nicht geöffnet "
                "werden\n\nBei Aufruf projekt_aktivieren:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    rc = db_baum_refresh( zond, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler in open_app:\nBei Aufruf db_baum_"
                "refresh:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


Projekt*
init( GtkApplication* app )
{
    Projekt* zond = NULL;

    zond = g_malloc0( sizeof( Projekt ) );

    //benötigte Arrays erzeugen
    zond->clipboard.arr_ref = g_ptr_array_new_with_free_func( (GDestroyNotify) gtk_tree_row_reference_free );

    zond->arr_pv = g_ptr_array_new( );
    zond->arr_docs = g_ptr_array_new( );

    init_app_window( zond );
    gtk_application_add_window( app, GTK_WINDOW(zond->app_window) );

    init_icons( zond );

    init_headerbar( zond );

    //GSettings
    zond->settings = g_settings_new( "de.perlio.zond" );
    g_settings_bind( zond->settings, "internalviewer",
            G_OBJECT(zond->menu.internal_vieweritem), "active",
            G_SETTINGS_BIND_DEFAULT );

    //Wiederherstellung bei Absturz
    //(d.h. in den Settings wurde project nicht auf "" gesetzt)
    gchar* proj_settings = g_settings_get_string( zond->settings, "project" );
    gboolean speichern = g_settings_get_boolean( zond->settings, "speichern" );
    if ( g_strcmp0( proj_settings, "" ) != 0 && speichern ) recover( zond,
            proj_settings, G_APPLICATION(app) );
    g_free( proj_settings );

    projekt_set_widgets_sensitiv( zond, FALSE );
    reset_project_changed( zond );

    DisableDebug( );

    gtk_widget_show_all( zond->app_window );
//    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    zond->ctx = mupdf_init( );
    if ( !zond->ctx )
    {
        meldung( zond->app_window, "zond->ctx konnte nicht initialisiert werden",
                NULL );
                gboolean ret = FALSE;
        g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

        return NULL;
    }
/*
    rc = fs_tree_create_sojus_zentral( zond, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Keine sojus_zentral-Datenbank\n\n"
                "Bei Aufruf fs_tree_create_sojus_zentral:\n", errmsg, NULL );
        g_free( errmsg );

        gboolean ret = FALSE;
        g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

        return NULL;
    }
*/
/*
    zond->root = g_strdup( "C:/lfd Akten" );

    rc = fs_tree_load_dir( zond, NULL, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "fs_tree konnte nicht geladen werden:\n\n"
                "Bei Aufruf fs_tree_load_dir:\n", errmsg, NULL );
        g_free( errmsg );
        gboolean ret = FALSE;
        g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

        return NULL;
    }
*/

    return zond;
}



