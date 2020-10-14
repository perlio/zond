/*
zond (baum_inhalt_und_auswertung.c) - Akten, Beweisstücke, Unterlagen
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
#include "../99conv/mupdf.h"

#include "../20allgemein/oeffnen.h"
#include "../20allgemein/ziele.h"
#include "../20allgemein/project.h"
#include "../20allgemein/fs_tree.h"

#include "../40viewer/viewer.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>




static void
cb_row_activated( GtkTreeView* tv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;
    Baum baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(tv) );

    //aktuellen Knoten abfragen
    gint node_id = baum_abfragen_aktuelle_node_id( tv );
    if ( !node_id )
    {
        meldung( zond->app_window, "Kein Punkt ausgewählt", NULL );

        return;
    }

    rc = oeffnen_node( zond, baum, node_id, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler - Öffnen\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


void
cb_cursor_changed( GtkTreeView* treeview, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    gint node_id = baum_abfragen_aktuelle_node_id( treeview );
    if ( node_id == 0 ) //letzter Knoten wird gelöscht
    {
        gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
        gtk_text_buffer_set_text( gtk_text_view_get_buffer( zond->textview ), "", -1 );

        return;
    }

    Baum baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(treeview) );

//status_label setzen
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;
    gchar* text_label = NULL;
    gchar* text = NULL;

    rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, &errmsg );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler in cb_cursor_changed:\n\n"
                "Bei Aufruf db_get_rel_path:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    if ( rc == 2 ) text_label = g_strdup( "" );
    else if ( rc == 1 ) text_label = g_strconcat( "Datei: ", rel_path, NULL );
    else if ( rc == 0 )
    {
        text_label = g_strdup_printf( "Datei: %s, von Seite %i, "
                "Index %i, bis Seite %i, index %i", rel_path,
                anbindung->von.seite, anbindung->von.index, anbindung->bis.seite,
                anbindung->bis.index );
        g_free( anbindung );
    }

    gtk_label_set_text( zond->label_status, text_label );
    g_free( rel_path );

    if ( baum == BAUM_INHALT ) return;

    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), TRUE );

    //TextBuffer laden
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );

    //neuen text einfügen
    rc = db_get_text( zond, node_id, &text, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler in cb_cursor_changed:\n\nBei Aufruf "
                "db_get_text\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    if ( text )
    {
        gtk_text_buffer_set_text( buffer, text, -1 );
        g_free( text );
    }
    else gtk_text_buffer_set_text( buffer, "", -1 );

    g_object_set_data( G_OBJECT(gtk_text_view_get_buffer( zond->textview )),
            "changed", NULL );
    g_object_set_data( G_OBJECT(zond->textview),
            "node-id", GINT_TO_POINTER(node_id) );

    return;
}


gboolean
cb_focus_out( GtkWidget* treeview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = baum_get_baum_from_treeview( zond, treeview );

    //cursor-changed-signal ausschalten
    if ( baum != BAUM_FS && zond->cursor_changed_signal )
    {
        g_signal_handler_disconnect( treeview, zond->cursor_changed_signal );
        zond->cursor_changed_signal = 0;
    }

    g_object_set(zond->renderer_text[baum], "editable", FALSE, NULL);

    zond->last_baum = baum;

    return FALSE;
}


gboolean
cb_focus_in( GtkWidget* treeview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = baum_get_baum_from_treeview( zond, treeview );

    //cursor-changed-signal für den aktivierten treeview anschalten
    if ( baum != BAUM_FS ) zond->cursor_changed_signal =
            g_signal_connect( treeview, "cursor-changed",
            G_CALLBACK(cb_cursor_changed), zond );

    if ( baum != zond->last_baum )
    {
        //selection in "altem" treeview löschen
        gtk_tree_selection_unselect_all( zond->selection[zond->last_baum] );

        //Cursor gewählter treeview selektieren
        GtkTreePath* path = NULL;
        GtkTreeViewColumn* focus_column = NULL;
        gtk_tree_view_get_cursor( GTK_TREE_VIEW(treeview), &path, &focus_column );
        if ( path )
        {
            gtk_tree_view_set_cursor( GTK_TREE_VIEW(treeview), path,
                    focus_column, FALSE );
            gtk_tree_path_free( path );
        }
    }

    g_object_set(zond->renderer_text[baum], "editable", TRUE, NULL);

    if ( baum != BAUM_AUSWERTUNG )
            gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
    else g_signal_emit_by_name( zond->treeview[baum], "cursor-changed" );

    return FALSE;
}


static void
cb_drag_begin( GtkWidget* widget, GdkDragContext* context, gpointer zond )
{
    ((Projekt*) zond)->dnd.dndactive = TRUE;
    ((Projekt*) zond)->dnd.first_change = TRUE;

    return;
}


void
cb_row_changed( GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
        gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    if ( !(zond->dnd.dndactive)) return;
    if ( !(zond->dnd.first_change) ) return;

    gtk_tree_model_get( model, iter, 2, &(zond->dnd.node_id), -1 );

    zond->dnd.first_change = FALSE;
    return;
}


static void
cb_drag_end( GtkWidget* widget, GdkDragContext* context, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    GtkTreeIter* iter = baum_abfragen_iter( zond->treeview[BAUM_AUSWERTUNG],
            zond->dnd.node_id );

    gint new_parent_id = baum_abfragen_parent_id( zond, BAUM_AUSWERTUNG, iter );
    gint new_older_sibling_id = baum_abfragen_older_sibling_id( zond,
            BAUM_AUSWERTUNG, iter );

    gtk_tree_iter_free( iter );

    gchar* errmsg = NULL;
    gint rc = 0;
    rc = db_verschieben_knoten( zond, BAUM_AUSWERTUNG, zond->dnd.node_id, new_parent_id,
            new_older_sibling_id, &errmsg );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler Drag'n Drop:\n\nBei Aufruf "
                "db_verschieben_knoten:\n", errmsg, NULL );
        g_free( errmsg );
    }

    zond->dnd.dndactive = FALSE;

    return;
}


static gint
treeviews_edit_node_fs_tree( Projekt* zond, GtkTreeIter* iter,
        const gchar* old_path, const gchar* new_path, gchar** errmsg )
{
    gint rc = 0;

    rc = db_begin_both( zond, errmsg );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_begin_both:\n", *errmsg, NULL );

        gchar* errmsg_ii = NULL;
        rc = db_rollback_both( zond, &errmsg_ii );
        if ( rc )
        {
            errmsg_ii = prepend_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
            if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
            g_free( errmsg_ii );
        }

        return -1;
    }

    gchar* old_rel_path = full_path_to_rel_path( zond, old_path );
    gchar* new_rel_path = full_path_to_rel_path( zond, new_path );

    rc = db_update_path( zond, old_rel_path, new_rel_path, errmsg );
    g_free( old_rel_path );
    g_free( new_rel_path );
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

        return -1;
    }

    GFile* old_file = g_file_new_for_path( old_path );
    GFile* new_file = g_file_new_for_path( new_path );
    GError* error = NULL;

    gboolean success = g_file_move( old_file, new_file, G_FILE_COPY_NONE, NULL, NULL, NULL, &error );
    g_object_unref( old_file );
    g_object_unref( new_file );
    if ( !success )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_move:\n",
                error->message, NULL );
        g_error_free( error );

        gchar* errmsg_ii = NULL;
        rc = db_rollback_both( zond, &errmsg_ii );
        if ( rc )
        {
            errmsg_ii = prepend_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
            if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
            g_free( errmsg_ii );
        }

        return -1;
    }

    rc = db_commit_both( zond, errmsg );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_commit:\n",
                error->message, NULL );
        g_error_free( error );

        gchar* errmsg_ii = NULL;
        rc = db_rollback_both( zond, &errmsg_ii );
        if ( rc )
        {
            errmsg_ii = prepend_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
            if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
            g_free( errmsg_ii );
        }

        return -1;
    }

    return 0;
}


static void
treeviews_cb_cell_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;
    Baum baum = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(cell), "treeview" ));

    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    GtkTreeIter iter;
    gtk_tree_model_get_iter_from_string( model, &iter, path_string );

    if ( baum == BAUM_FS )
    {
        gchar* path = NULL;
        gchar* old_path = NULL;
        gchar* new_path = NULL;
        gchar* old_text = NULL;
        GtkTreeIter iter_parent = { 0 };

        if ( gtk_tree_model_iter_parent( model, &iter_parent, &iter ) )
        {
            path = fs_tree_get_full_path( zond, &iter_parent );
        }
        else path = g_strdup( zond->project_dir );//g_settings_get_string( zond->settings, "root" );

        gtk_tree_model_get( model, &iter, 1, &old_text, -1 );

        old_path = g_strconcat( path, "/", old_text, NULL );
        new_path = g_strconcat( path, "/", new_text, NULL );

        g_free( old_text );
        g_free( path );

        gboolean dif = g_strcmp0( old_path, new_path );
        if ( dif ) rc = treeviews_edit_node_fs_tree( zond, &iter, old_path, new_path, &errmsg );
        g_free( old_path );
        g_free( new_path );
        if ( !dif ) return;
        if ( rc )
        {
            meldung( zond->app_window, "Umbenennen nicht möglich\n\nBei "
                    "Aufruf treeviews_edit_node:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }
    }
    else
    {
        //node_id holen, node_text in db ändern
        gint node_id = 0;
        gtk_tree_model_get( model, &iter, 2, &node_id, -1 );

        rc = db_set_node_text( zond, baum, node_id, new_text, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Knoten umbenennen nicht möglich\n\n"
                    "Bei Aufruf db_set_node_text:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }
    }

    gtk_tree_store_set( GTK_TREE_STORE(model), &iter, 1, new_text, -1 );
    gtk_tree_view_columns_autosize( zond->treeview[baum] );

    return;
}


static void
treeviews_cb_row_collapsed( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    gtk_tree_view_columns_autosize( tree_view );

    return;
}



/*
static gboolean
cb_show_popupmenu( GtkTreeView* treeview, GdkEventButton* event,
        GtkMenu* contextmenu_tv )
{
    //Rechtsklick
    if ( ((event->button) == 3) && (event->type == GDK_BUTTON_PRESS) )
    {
        GtkTreePath* path;
        gtk_tree_view_get_cursor( treeview, &path, NULL );
        if ( !path ) return FALSE;
        gtk_tree_path_free( path );

        gtk_menu_popup_at_pointer( contextmenu_tv, NULL );

        return TRUE;
    }

    return FALSE;
}
*/

void
treeviews_cell_data_function( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* treemodel, GtkTreeIter* iter, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(renderer),
            "treeview" ));

    //underline wenn Reihe cursor hat
    GtkTreePath* path = gtk_tree_model_get_path( treemodel, iter );
    GtkTreePath* path_cursor = NULL;
    gtk_tree_view_get_cursor( zond->treeview[baum], &path_cursor, NULL );

    if ( path_cursor )
    {
        if ( !gtk_tree_path_compare( path, path_cursor ) ) g_object_set(
                G_OBJECT(renderer), "underline-set", TRUE, NULL );
        else g_object_set( G_OBJECT(renderer), "underline-set", FALSE, NULL );
    }
    else g_object_set( G_OBJECT(renderer), "underline-set", FALSE, NULL );

    gtk_tree_path_free( path_cursor );

    //Cell ausgrauen, wenn node_id in zond->arr_selection enthalten
    if ( baum == zond->clipboard.baum && zond->clipboard.ausschneiden )
    {
        gboolean enthalten = FALSE;
        GtkTreePath* path_sel = NULL;
        for ( gint i = 0; i < zond->clipboard.arr_ref->len; i++ )
        {
            path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( zond->clipboard.arr_ref, i ) );
            enthalten = !gtk_tree_path_compare( path, path_sel );
            gtk_tree_path_free( path_sel );
            if ( enthalten ) break;
        }

        if ( enthalten ) g_object_set( G_OBJECT(renderer), "sensitive", FALSE,
                NULL );
        else g_object_set( G_OBJECT(renderer), "sensitive", TRUE, NULL );
    }
    else g_object_set( G_OBJECT(renderer), "sensitive", TRUE, NULL );

    gint rc = 0;
    gchar* errmsg = NULL;

    if ( baum == BAUM_AUSWERTUNG )
    {
        //Hintergrund icon rot wenn Text in textview
        gchar* text = NULL;
        gint node_id = 0;

        node_id = baum_abfragen_node_id( zond->treeview[BAUM_AUSWERTUNG], path, &errmsg );
        if ( node_id == -1 )
        {
            meldung( zond->app_window, "Fehler in cb_cursor_changed\n\n"
                    "Bei Aufruf baum_abfragen_node_id:\n", errmsg, NULL );
            g_free( errmsg );
        }
        else
        {
            rc = db_get_text( zond, node_id, &text, &errmsg );
            if ( rc )
            {
                meldung( zond->app_window, "Warnung -\n\nBei Aufruf db_get_text:\n",
                        errmsg, NULL );
                g_free( errmsg );
            }
            else if ( !text || !g_strcmp0( text, "" ) ) g_object_set( G_OBJECT(renderer),
                    "background-set", FALSE, NULL );
            else g_object_set( G_OBJECT(renderer), "background-set", TRUE, NULL );

            g_free( text );
        }
    }
    else if ( baum == BAUM_FS ) //wenn angebunden, Hintergrund
    {
        gchar* rel_path = fs_tree_get_rel_path( zond, iter );
        if ( rel_path )
        {
            rc = db_get_node_id_from_rel_path( zond, rel_path, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Warnung -\n\nBei Aufruf db_get_node_"
                        "id_from_rel_path:\n", errmsg, NULL );
                g_free( errmsg );
            }
            else if ( rc == 0 ) g_object_set( G_OBJECT(renderer),
                    "background-set", FALSE, NULL );
            else g_object_set( G_OBJECT(renderer), "background-set", TRUE, NULL );

            g_free( rel_path );
        }
    }

    gtk_tree_path_free( path );

    return;
}


static gboolean
treeviews_selection_func( GtkTreeSelection* selection, GtkTreeModel* model,
        GtkTreePath* path, gboolean selected, gpointer data )
{
    if ( selected ) return TRUE; //abschalten geht immer

    GList* list = gtk_tree_selection_get_selected_rows( selection, NULL );
    GList* l = list;
    while ( l )
    {
        if ( gtk_tree_path_is_ancestor( path, l->data ) ||
                gtk_tree_path_is_descendant( path, l->data ) )
        {
            g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
            return FALSE;
        }

        l = l->next;
    }

    g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);

    return TRUE;
}


void
init_treeviews( Projekt* zond )
{
    for ( Baum baum = BAUM_INHALT; baum <= BAUM_AUSWERTUNG; baum++ )
    {
        //der treeview
        zond->treeview[baum] = GTK_TREE_VIEW(gtk_tree_view_new( ));

        //Tree-Model erzeugen und verbinden
        GtkTreeStore* tree_store = gtk_tree_store_new( 3, G_TYPE_STRING,
                G_TYPE_STRING, G_TYPE_INT );
        gtk_tree_view_set_model( zond->treeview[baum], GTK_TREE_MODEL(tree_store) );
        g_object_unref( tree_store );

        gtk_tree_view_set_headers_visible( zond->treeview[baum], FALSE );
        gtk_tree_view_set_fixed_height_mode( zond->treeview[baum], TRUE );
        gtk_tree_view_set_enable_tree_lines( zond->treeview[baum], TRUE );
        gtk_tree_view_set_enable_search( zond->treeview[baum], FALSE );

        //die renderer
        GtkCellRenderer* renderer_pixbuf = gtk_cell_renderer_pixbuf_new();
        zond->renderer_text[baum] = gtk_cell_renderer_text_new();

        g_object_set(zond->renderer_text[baum], "editable", TRUE, NULL);
        g_object_set( zond->renderer_text[baum], "underline", PANGO_UNDERLINE_SINGLE, NULL );
        if ( baum == BAUM_AUSWERTUNG )
        {
            GdkRGBA gdkrgba;
            gdkrgba.alpha = 1.0;
            gdkrgba.red = 0.95;
            gdkrgba.blue = 0.95;
            gdkrgba.green = 0.95;

            g_object_set( G_OBJECT(zond->renderer_text[baum]), "background-rgba", &gdkrgba,
                    NULL );
        }

        g_object_set_data( G_OBJECT(zond->renderer_text[baum]), "treeview",
                GINT_TO_POINTER(baum) );

        //die column
        GtkTreeViewColumn* column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_resizable(column, FALSE);
        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

        gtk_tree_view_column_set_cell_data_func( column, zond->renderer_text[baum],
                (GtkTreeCellDataFunc) treeviews_cell_data_function, (gpointer) zond,
                NULL );

        gtk_tree_view_column_pack_start( column, renderer_pixbuf, FALSE );
        gtk_tree_view_column_pack_start( column, zond->renderer_text[baum], TRUE );

        gtk_tree_view_column_set_attributes(column, renderer_pixbuf, "icon-name", 0, NULL);
        gtk_tree_view_column_set_attributes(column, zond->renderer_text[baum], "text", 1, NULL);

        gtk_tree_view_append_column(zond->treeview[baum], column);

        //die Selection
        zond->selection[baum] = gtk_tree_view_get_selection(
                zond->treeview[baum] );
        gtk_tree_selection_set_mode( zond->selection[baum],
                GTK_SELECTION_MULTIPLE );
        gtk_tree_selection_set_select_function( zond->selection[baum],
                (GtkTreeSelectionFunc) treeviews_selection_func, zond, NULL );

/*        //Kontextmenu erzeugen, welches bei Rechtsklick auf treeview angezeigt wird
        GtkWidget* contextmenu_tv = gtk_menu_new();

        GtkWidget* datei_oeffnen_item = gtk_menu_item_new_with_label( "Öffnen" );
        gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu_tv), datei_oeffnen_item );
        gtk_widget_show( datei_oeffnen_item );

        //Die Signale
        //Rechtsklick - Kontextmenu
        g_signal_connect( zond->treeview[BAUM_AUSWERTUNG], "button-press-event",
                G_CALLBACK(cb_show_popupmenu), (gpointer) contextmenu_tv );

        g_signal_connect( datei_oeffnen_item, "activate",
                G_CALLBACK(cb_datei_oeffnen), (gpointer) zond );
    */
        //Text-Spalte wird editiert
        g_signal_connect(zond->renderer_text[baum], "edited", G_CALLBACK(treeviews_cb_cell_edited),
                (gpointer) zond); //Klick in textzelle = Inhalt editieren

        // Doppelklick = angebundene Datei anzeigen
        g_signal_connect( zond->treeview[baum], "row-activated",
                G_CALLBACK(cb_row_activated), (gpointer) zond );

        //Zeile expandiert oder kollabiert
        g_signal_connect( zond->treeview[baum], "row-expanded",
                G_CALLBACK(treeviews_cb_row_collapsed), NULL );
        g_signal_connect( zond->treeview[baum], "row-collapsed",
                G_CALLBACK(treeviews_cb_row_collapsed), NULL );

        //focus-in
        zond->treeview_focus_in_signal[baum] = g_signal_connect( zond->treeview[baum],
                "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
        g_signal_connect( zond->treeview[baum], "focus-out-event",
                G_CALLBACK(cb_focus_out), (gpointer) zond );
    }

    gtk_tree_view_set_reorderable( zond->treeview[BAUM_INHALT], FALSE );
    gtk_tree_view_set_reorderable( zond->treeview[BAUM_AUSWERTUNG], TRUE );

    //dnd erfassen - nur BAUM_AUSWERTUNG
    g_signal_connect( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), "drag-begin",
            G_CALLBACK(cb_drag_begin), (gpointer) zond );
    g_signal_connect( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), "drag-end",
            G_CALLBACK(cb_drag_end), (gpointer) zond );

    g_signal_connect( gtk_tree_view_get_model(zond->treeview[BAUM_AUSWERTUNG]),
            "row-changed", G_CALLBACK(cb_row_changed), (gpointer) zond );

    return;
}


static void
treeviews_cb_fs_tree_row_activated( GtkTreeView* tree_view, GtkTreePath* path,
        GtkTreeViewColumn* column, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    gchar* pathname = NULL;
    GtkTreeIter iter;
    gint rc = 0;
    gchar* errmsg = NULL;

    gtk_tree_model_get_iter( gtk_tree_view_get_model( zond->treeview[BAUM_FS] ), &iter, path );

    pathname = fs_tree_get_full_path( zond, &iter );

    rc = oeffnen_datei( zond, pathname, NULL, NULL, &errmsg );
    g_free( pathname );
    if ( rc )
    {
        meldung( zond->app_window, "Öffnen nicht möglich\n\nBei Aufruf "
                "oeffnen_datei:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
treeviews_cb_fs_tree_row_collapsed( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    GtkTreeIter iter_child;
    gboolean not_empty = TRUE;

    gtk_tree_model_iter_children( gtk_tree_view_get_model( tree_view ),
            &iter_child, iter );

    do {
        not_empty = gtk_tree_store_remove(
                GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )), &iter_child );
    } while ( not_empty );

    //dummy einfügen, dir ist ja nicht leer
    gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
            &iter_child, iter, -1 );

    return;
}


static void
treeviews_cb_fs_tree_row_expanded( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreeIter new_iter;
    gchar* string = NULL;

    gtk_tree_model_iter_nth_child( gtk_tree_view_get_model( tree_view ), &new_iter, iter, 0 );
    gtk_tree_model_get( gtk_tree_view_get_model( tree_view ), &new_iter, 1, &string, -1 );
    if ( !string )
    {
        rc = fs_tree_load_dir( zond, iter, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Directory konnte nicht geladen werden\n\n"
                    "Bei Aufruf fs_tree_load_dir:\n", errmsg, NULL );
            g_free( errmsg );
        }
        gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
                &new_iter );
    }
    gtk_tree_view_columns_autosize( tree_view );

    return;
}


gboolean
cb_fs_tree_button_press( GtkWidget* treeview, GdkEvent* event, gpointer data )
{/*
    Projekt* zond = (Projekt*) data;

    GList* list = gtk_tree_selection_get_selected_rows( zond->selection[BAUM_FS], NULL );

    if ( list ) return TRUE;
*/
    return FALSE;
}


void
treeviews_init_fs_tree( Projekt* zond )
{
    //renderer
    GtkCellRenderer* fs_tree_renderer_icon = gtk_cell_renderer_pixbuf_new( );
    zond->renderer_text[BAUM_FS] = gtk_cell_renderer_text_new( );
    g_object_set_data( G_OBJECT(zond->renderer_text[BAUM_FS]), "treeview",
            GINT_TO_POINTER(BAUM_FS) );
    g_object_set(zond->renderer_text[BAUM_FS], "editable", TRUE, NULL);
    g_object_set( zond->renderer_text[BAUM_FS], "underline", PANGO_UNDERLINE_SINGLE, NULL );

    GdkRGBA gdkrgba;
    gdkrgba.alpha = 1.0;
    gdkrgba.red = 0.95;
    gdkrgba.blue = 0.95;
    gdkrgba.green = 0.95;

    g_object_set( G_OBJECT(zond->renderer_text[BAUM_FS]), "background-rgba", &gdkrgba,
            NULL );

    //die column
    GtkTreeViewColumn* fs_tree_column = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable( fs_tree_column, FALSE );
    gtk_tree_view_column_set_sizing(fs_tree_column, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_pack_start( fs_tree_column, fs_tree_renderer_icon, FALSE );
    gtk_tree_view_column_pack_start( fs_tree_column, zond->renderer_text[BAUM_FS], TRUE );

    gtk_tree_view_column_set_attributes( fs_tree_column,
            fs_tree_renderer_icon, "gicon", 0, NULL );
    gtk_tree_view_column_set_attributes( fs_tree_column,
            zond->renderer_text[BAUM_FS], "text", 1, NULL );

    zond->treeview[BAUM_FS] = (GtkTreeView*) gtk_tree_view_new( );
    gtk_tree_view_set_headers_visible( zond->treeview[BAUM_FS], FALSE );
    gtk_tree_view_set_fixed_height_mode( zond->treeview[BAUM_FS], TRUE );
    gtk_tree_view_set_enable_tree_lines( zond->treeview[BAUM_FS], TRUE );
    gtk_tree_view_set_enable_search( zond->treeview[BAUM_FS], FALSE );

    gtk_tree_view_append_column( zond->treeview[BAUM_FS], fs_tree_column );

    gtk_tree_view_column_set_cell_data_func( fs_tree_column, zond->renderer_text[BAUM_FS],
            (GtkTreeCellDataFunc) treeviews_cell_data_function, (gpointer) zond, NULL );

    GtkTreeStore* tree_store = gtk_tree_store_new( 2, G_TYPE_ICON,
            G_TYPE_STRING );
    gtk_tree_view_set_model( GTK_TREE_VIEW(zond->treeview[BAUM_FS]), GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );

    //Zeile expandiert
    g_signal_connect( zond->treeview[BAUM_FS], "row-expanded",
            G_CALLBACK(treeviews_cb_fs_tree_row_expanded), zond );
    //Zeile kollabiert
    g_signal_connect( zond->treeview[BAUM_FS], "row-collapsed",
            G_CALLBACK(treeviews_cb_fs_tree_row_collapsed), zond );
    //Text-Spalte wird editiert
    g_signal_connect( zond->renderer_text[BAUM_FS], "edited", G_CALLBACK(treeviews_cb_cell_edited),
            (gpointer) zond); //Klick in textzelle = Datei umbenennen


    //die Selection
    zond->selection[BAUM_FS] = gtk_tree_view_get_selection(
            zond->treeview[BAUM_FS] );
    gtk_tree_selection_set_mode( zond->selection[BAUM_FS],
            GTK_SELECTION_MULTIPLE );
    gtk_tree_selection_set_select_function( zond->selection[BAUM_FS],
            (GtkTreeSelectionFunc) treeviews_selection_func, zond, NULL );

    // Doppelklick = angebundene Datei anzeigen
    g_signal_connect( zond->treeview[BAUM_FS], "row-activated",
            G_CALLBACK(treeviews_cb_fs_tree_row_activated), (gpointer) zond );

    //focus-in
    zond->treeview_focus_in_signal[BAUM_FS] = g_signal_connect( zond->treeview[BAUM_FS],
            "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
    g_signal_connect( zond->treeview[BAUM_FS], "focus-out-event",
            G_CALLBACK(cb_focus_out), (gpointer) zond );

    g_signal_connect( zond->treeview[BAUM_FS], "button-press-event",
            G_CALLBACK(cb_fs_tree_button_press), (gpointer) zond );

/*
    //dnd erfassen - nur BAUM_AUSWERTUNG
    g_signal_connect( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), "drag-begin",
            G_CALLBACK(cb_drag_begin), (gpointer) zond );
    g_signal_connect( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), "drag-end",
            G_CALLBACK(cb_drag_end), (gpointer) zond );

    g_signal_connect( gtk_tree_view_get_model(zond->treeview[BAUM_AUSWERTUNG]),
            "row-changed", G_CALLBACK(cb_row_changed), (gpointer) zond );
*/



    return;
}
