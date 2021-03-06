/*
zond (app_window.c) - Akten, Beweisstücke, Unterlagen
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

#include "../99conv/general.h"
#include "../99conv/db_write.h"
#include "../99conv/mupdf.h"

#include "../20allgemein/project.h"

#include "treeviews.h"

#include "../40viewer/viewer.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>



static gboolean
cb_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    //Geöffnete Viewer schließen
    for ( gint i = 0; i < zond->arr_pv->len; i++ ) viewer_schliessen(
            g_ptr_array_index( zond->arr_pv, i ) );

    if ( zond->changed )
    {
        gint rc = 0;
        rc = abfrage_frage( zond->app_window, "zond beenden", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return TRUE; //Abbruch gewählt
    }

    projekt_schliessen( (Projekt*) zond );

    gtk_widget_destroy( zond->app_window );

    pdf_drop_document( zond->ctx, zond->pv_clip );

    mupdf_close_context( zond->ctx );

    g_ptr_array_unref( zond->arr_docs );
    g_ptr_array_unref( zond->arr_pv );
    g_ptr_array_unref( zond->clipboard.arr_ref );

    g_free( zond );

    return TRUE;
}


static gboolean
cb_pao_button_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    ((Projekt*)data)->state = event->button.state;

    return FALSE;
}


void
cb_text_buffer_changed( GtkTextBuffer* buffer, gpointer zond )
{
    project_set_changed( zond );
    g_object_set_data( G_OBJECT(gtk_text_view_get_buffer(
            ((Projekt*) zond)->textview )), "changed", GINT_TO_POINTER(1) );

    return;
}


gboolean
cb_textview_focus_in( GtkWidget* textview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->text_buffer_changed_signal =
            g_signal_connect( gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) ),
            "changed", G_CALLBACK(cb_text_buffer_changed), (gpointer) zond );

    return FALSE;
}


gboolean
cb_textview_focus_out( GtkWidget* textview, GdkEvent* event, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    g_signal_handler_disconnect( gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) ),
            zond->text_buffer_changed_signal );
    zond->text_buffer_changed_signal = 0;

    GtkTextBuffer* buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) );

    if ( g_object_get_data( G_OBJECT(buffer), "changed" ) )
    {
        //inhalt textview abspeichern
        GtkTextIter start;
        GtkTextIter end;

        gtk_text_buffer_get_start_iter( buffer, &start );
        gtk_text_buffer_get_end_iter( buffer, &end );

        gchar* text = gtk_text_buffer_get_text( buffer, &start, &end, FALSE );

        gint node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(zond->textview),
                "node-id" ) );

        rc = db_speichern_textview( zond, node_id, text, &errmsg );
        g_free( text );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler in cb_textview_focus_out:\n\n"
                    "Bei Aufruf db_speichern_textview:\n", errmsg, NULL );
            g_free( errmsg );

            return FALSE;
        }
    }

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) );

    return FALSE;
}


void
init_app_window( Projekt* zond )
{
    //ApplicationWindow erzeugen
    zond->app_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(zond->app_window), 800, 1000 );

    //vbox für Statuszeile im unteren Bereich
    GtkWidget* vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(zond->app_window), vbox );

/*
**  oberer Teil der VBox  */
    //zuerst links/rechts teilen
    zond->hpaned = gtk_paned_new( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_set_position( GTK_PANED(zond->hpaned), 350);

    //jetzt in oberen Teil der vbox einfügen
    gtk_box_pack_start( GTK_BOX(vbox), zond->hpaned, TRUE, TRUE, 0 );

    //TreeView erzeugen und in das scrolled window
    init_treeviews( zond );
    treeviews_init_fs_tree( zond );

    //BAUM_FS
    zond->tree[BAUM_FS] = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(zond->tree[BAUM_FS]), GTK_WIDGET(zond->treeview[BAUM_FS]) );
    gtk_widget_show_all( zond->tree[BAUM_FS] );

    //BAUM_INHALT
    zond->tree[BAUM_INHALT] = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(zond->tree[BAUM_INHALT]), GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    //BAUM_AUSWERTUNG
    //VPaned erzeugen
    zond->tree[BAUM_AUSWERTUNG] = gtk_paned_new( GTK_ORIENTATION_VERTICAL );
    gtk_paned_set_position( GTK_PANED(zond->tree[BAUM_AUSWERTUNG]), 500);

    GtkWidget* swindow_treeview_auswertung = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow_treeview_auswertung), GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) );

    gtk_paned_pack1( GTK_PANED(zond->tree[BAUM_AUSWERTUNG]), swindow_treeview_auswertung, TRUE, TRUE);

    //in die untere Hälfte des vpaned kommt text_view in swindow
    //Scrolled Window zum Anzeigen erstellen
    GtkWidget* swindow_textview = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow_textview),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack2( GTK_PANED(zond->tree[BAUM_AUSWERTUNG]), swindow_textview, TRUE, TRUE );

    //text_view erzeugen
    zond->textview = GTK_TEXT_VIEW(gtk_text_view_new( ));
    gtk_text_view_set_wrap_mode( zond->textview, GTK_WRAP_WORD );
    gtk_text_view_set_accepts_tab( zond->textview, FALSE );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    //Und dann in untere Hälfte des übergebenen vpaned reinpacken
    gtk_container_add( GTK_CONTAINER(swindow_textview),
            GTK_WIDGET(zond->textview) );

    //Zum Start: links BAUM_INHALT, rechts BAUM_AUSWERTUNG
    gtk_paned_add1( GTK_PANED(zond->hpaned), zond->tree[BAUM_INHALT] );
    gtk_paned_add2( GTK_PANED(zond->hpaned), zond->tree[BAUM_AUSWERTUNG] );

    //Hört die Signale
    g_signal_connect( zond->textview, "focus-in-event",
            G_CALLBACK(cb_textview_focus_in), (gpointer) zond );
    g_signal_connect( zond->textview, "focus-out-event",
            G_CALLBACK(cb_textview_focus_out), (gpointer) zond );

    g_signal_connect( zond->app_window, "button-press-event",
            G_CALLBACK(cb_pao_button_event), zond );
    g_signal_connect( zond->app_window, "button-release-event",
            G_CALLBACK(cb_pao_button_event), zond );

/*
**  untere Hälfte vbox: Status-Zeile  */
    //Label erzeugen
    zond->label_status = GTK_LABEL(gtk_label_new( "" ));
    gtk_label_set_xalign( zond->label_status, 0.02 );
    gtk_box_pack_end( GTK_BOX(vbox), GTK_WIDGET(zond->label_status), FALSE, FALSE, 0 );

    //Signal für App-Fenster schließen
    g_signal_connect( zond->app_window, "delete-event",
            G_CALLBACK(cb_delete_event), zond );

    return;
}


