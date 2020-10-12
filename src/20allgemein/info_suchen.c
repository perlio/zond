/*
zond (info_suchen.c) - Akten, Beweisstücke, Unterlagen
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
#include "../enums.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../20allgemein/ziele.h"
#include "../99conv/baum.h"
#include "../99conv/db_write.h"
#include "../99conv/db_zu_baum.h"

#include <gtk/gtk.h>
#include <sqlite3.h>


static gint
suchen_fuellen_row( Projekt* zond, GtkWidget* list_box, Baum baum, gint node_id,
        gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;
    gint icon_id = 0;
    gchar* node_text = NULL;

    rc = db_get_icon_id_and_node_text( zond, baum, node_id,
            &icon_id, &node_text, errmsg );
    if ( rc ) ERROR_PAO( "db_get_icon_id_and_node_text" )

    //Beschriftung
    gchar* text_label = NULL;
    GtkWidget* label_anbindung = NULL;

    if ( baum == BAUM_INHALT )
    {
        rc = db_get_rel_path( zond, baum, node_id, &rel_path, errmsg );
        if ( rc == -1 ) ERROR_PAO( "db_get_rel_path" )

        if ( rc == 0 )
        {
            Anbindung* anbindung = ziele_abfragen_anbindung( zond, node_id, errmsg );
            if ( !anbindung && *errmsg )
            {
                g_free( rel_path );
                ERROR_PAO( "ziele_abfragen_anbindung" )
            }

            text_label = g_strdup( rel_path );
            if ( anbindung ) text_label = add_string( text_label,
                    g_strdup_printf( ", S. %i - %i)", anbindung->von.seite,
                    anbindung->bis.seite ) );

            g_free( anbindung );
            g_free( rel_path );
        }
    }

    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

    if ( baum == BAUM_AUSWERTUNG )
    {
        GtkWidget* fill = gtk_image_new_from_icon_name( "go-next", GTK_ICON_SIZE_BUTTON );
        gtk_box_pack_start( GTK_BOX(hbox), fill, FALSE, FALSE, 4 );
    }

    GtkWidget* icon = gtk_image_new_from_pixbuf( zond->icon[icon_id].pixbuf );
    gtk_box_pack_start( GTK_BOX(hbox), icon, FALSE, FALSE, 0 );
    GtkWidget* label = gtk_label_new( (const gchar*) node_text );
    g_free( node_text );
    gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 0 );

    label_anbindung = gtk_label_new( text_label );
    g_free( text_label );
    gtk_box_pack_end( GTK_BOX(hbox), label_anbindung, FALSE, FALSE, 0 );

    gtk_list_box_insert( GTK_LIST_BOX(list_box), hbox, -1 );
    GtkWidget* list_box_row = gtk_widget_get_parent( hbox );

    g_object_set_data( G_OBJECT(list_box_row), "baum", GINT_TO_POINTER(baum) );
    g_object_set_data( G_OBJECT(list_box_row), "node-id", GINT_TO_POINTER(node_id) );

    return 0;
}


typedef struct _Foreach_Info
{
    Projekt* projekt;
    GArray* treffer;
    GtkWidget* window;
}  ForeachInfo;

typedef struct _SQL_Row
{
    gboolean case_sensitive;
    gchar* key;
    gboolean not_like;
    gchar* value;
    gboolean or;
} SQLRow;


static gboolean
suchen_foreach_baum_auswertung(GtkTreeModel *model, GtkTreePath *path,
        GtkTreeIter *iter, gpointer data )
{
    ForeachPipe* fpipe = data;

    ForeachInfo* finfo = fpipe->question;

    Projekt* zond = finfo->projekt;
    GArray* info_treffer_auswertung = finfo->treffer;
    GtkWidget* ergebnisfenster = finfo->window;

    gchar** errmsg = fpipe->answer;

    GtkWidget* list_box = g_object_get_data( G_OBJECT(ergebnisfenster), "list-box" );

    gint rc = 0;
    gint node_id = 0;
    gchar* errmsg_ii = NULL;

    gtk_tree_model_get( model, iter, 2, &node_id, -1 );

    for ( gint i = 0; i < info_treffer_auswertung->len;i++ )
    {
        if ( g_array_index( info_treffer_auswertung, gint, i ) == node_id )
        {
            rc = suchen_fuellen_row( zond, list_box, BAUM_AUSWERTUNG, node_id,
                    &errmsg_ii );
            if ( rc )
            {
                *errmsg = g_strconcat( "Bei Aufruf suchen_fuellen_row:\n",
                        errmsg_ii, NULL );
                g_free( errmsg_ii );

                return TRUE;
            }

            break;
        }
    }

    return FALSE;
}


static gboolean
suchen_foreach_baum_inhalt( GtkTreeModel *model, GtkTreePath *path,
        GtkTreeIter *iter, gpointer data )
{
    ForeachPipe* fpipe = data;

    gchar** errmsg = fpipe->answer;

    ForeachInfo* finfo = fpipe->question;
    Projekt* zond = finfo->projekt;
    GArray* info_treffer = finfo->treffer;
    GtkWidget* ergebnisfenster = finfo->window;

    GtkWidget* list_box = g_object_get_data( G_OBJECT(ergebnisfenster), "list-box" );

    gint node_id = 0;
    gchar* errmsg_ii = NULL;

    gtk_tree_model_get( model, iter, 2, &node_id, -1 );

    gint rc = 0;
    sqlite3_stmt* stmt = NULL;
    gint result = 0;
    gchar* sql = NULL;

    for ( gint i = 0; i < info_treffer->len; i++ )
    {
        if ( g_array_index( info_treffer, gint, i ) == node_id )
        {
            rc = suchen_fuellen_row( zond, list_box, BAUM_INHALT, node_id, &errmsg_ii );
            if ( rc )
            {
                *errmsg = g_strconcat( "Bei Aufruf suchen_fuellen_row:\n",
                        errmsg_ii, NULL );
                g_free( errmsg_ii );

                return TRUE;
            }

            //Jetzt Baum Auswertung
            GArray* info_treffer_auswertung = g_array_new( FALSE, FALSE,
                    sizeof( gint ) );

            sql = g_strdup_printf( "SELECT node_id FROM baum_auswertung WHERE "
                    "ref_id=%i;", node_id );

            rc = sqlite3_prepare_v2( zond->db, sql, -1, &stmt, NULL );
            g_free( sql );
            if ( rc != SQLITE_OK )
            {
                *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2:\n",
                        sqlite3_errstr( rc ), NULL );
                sqlite3_finalize( stmt );
                g_array_free( info_treffer_auswertung, TRUE );

                return TRUE;
            }

            do
            {
                rc = sqlite3_step( stmt );
                if ( rc == SQLITE_ROW )
                {
                    result = sqlite3_column_int( stmt, 0 );
                    g_array_append_val( info_treffer_auswertung, result );
                }
                else if ( rc != SQLITE_DONE )
                {
                    *errmsg = g_strconcat( "Bei Aufruft sqlite3_step:\n",
                            sqlite3_errstr( rc ), "\nerrmsg: ",
                            sqlite3_errmsg( zond->db ), NULL );
                    sqlite3_finalize( stmt );
                    g_array_free( info_treffer_auswertung, TRUE );

                    return TRUE;
                }
            } while ( rc == SQLITE_ROW );

            sqlite3_finalize( stmt );

            ForeachInfo finfo_ii;

            finfo_ii.projekt = zond;
            finfo_ii.treffer = info_treffer_auswertung;
            finfo_ii.window = ergebnisfenster;

            ForeachPipe fpipe_ii;
            fpipe_ii.question = &finfo_ii;
            fpipe_ii.answer = &errmsg_ii;

            gtk_tree_model_foreach( gtk_tree_view_get_model(
                    zond->treeview[BAUM_AUSWERTUNG] ),
                    (GtkTreeModelForeachFunc) suchen_foreach_baum_auswertung,
                    &fpipe_ii );

            g_array_free( info_treffer_auswertung, TRUE );

            if ( errmsg_ii )
            {
                *errmsg = g_strconcat( "Bei Aufruf gtk_tree_model_"
                        "foreach (baum_auswertung):\n", errmsg_ii, NULL );
                g_free( errmsg_ii );

                return TRUE;
            }

            break;
        }
    }

    return FALSE;
}


static gint
suchen_fuellen_ergebnisfenster( Projekt* zond, GtkWidget* ergebnisfenster,
        GArray* info_treffer, gchar** errmsg )
{
    GtkWidget* list_box = g_object_get_data( G_OBJECT(ergebnisfenster), "list-box" );

    gchar* errmsg_ii = NULL;

    ForeachInfo finfo;

    finfo.projekt = zond;
    finfo.treffer = info_treffer;
    finfo.window = ergebnisfenster;

    ForeachPipe fpipe;
    fpipe.question = &finfo;
    fpipe.answer = &errmsg_ii;

    gtk_tree_model_foreach( gtk_tree_view_get_model( zond->treeview[BAUM_INHALT] ),
            (GtkTreeModelForeachFunc) suchen_foreach_baum_inhalt, &fpipe );

    if ( errmsg_ii )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf gtk_tree_model_foreach:\n",
                errmsg_ii, NULL );
        g_free( errmsg_ii );

        return -1;
    }

    gtk_widget_show_all( list_box );

    return 0;
}


static void
cb_suchen_nach_auswertung( GtkMenuItem* item, gpointer user_data )
{
    GList* selected = NULL;
    GList* list = NULL;

    Projekt* zond =(Projekt*) user_data;
    GtkWidget* list_box = g_object_get_data( G_OBJECT(item), "list-box" );
    gboolean child = (gboolean) GPOINTER_TO_INT(g_object_get_data(
            G_OBJECT(item), "child" ));

    selected = gtk_list_box_get_selected_rows( GTK_LIST_BOX(list_box) );

    if ( !selected )
    {
        meldung( zond->app_window, "Kopieren nicht möglich - keine Punkte "
                "ausgewählt", NULL );

        return;
    }

    //aktuellen cursor im BAUM_AUSWERTUNG: node_id und iter abfragen
    gint anchor_id = baum_abfragen_aktuelle_node_id( zond->treeview[BAUM_AUSWERTUNG] );

    gint rc = 0;
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    gint new_node_id = 0;
    gchar* errmsg = NULL;

    list = selected;
    do
    {
        baum = (Baum) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(list->data), "baum" ));
        node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(list->data), "node-id" ));

        rc = db_begin( zond, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler in Suchen/Kopieren in Auswertung -\n\n"
                    "Bei Aufruf db_begin:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }

        new_node_id = db_kopieren_nach_auswertung( zond, baum, node_id,
                anchor_id, child, &errmsg );
        if ( new_node_id == -1 )
        {
            meldung( zond->app_window, "Fehler in Suchen/Kopieren in Auswertung -\n\n"
                    "Bei Aufruf db_kopieren_nach_auswertung:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }

        rc = db_commit( zond, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler in kopieren nach BAUM_AUSWERTUNG:\n\n"
                        "Bei Aufruf db_commit:\n", errmsg, NULL );

            return;
        }

        GtkTreeIter* iter = baum_abfragen_iter( zond->treeview[BAUM_AUSWERTUNG],
                anchor_id );

        GtkTreeIter* new_iter = db_baum_knoten( zond, BAUM_AUSWERTUNG,
                new_node_id, iter, child, &errmsg );
        if ( iter ) gtk_tree_iter_free( iter );
        if ( !new_iter )
        {
            meldung( zond->app_window, "Fehler in kopieren nach BAUM_AUSWERTUNG:\n\n"
                    "Bei Aufruf db_baum_knoten:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }

        expand_row( zond, BAUM_AUSWERTUNG, new_iter );
        baum_setzen_cursor( zond, BAUM_AUSWERTUNG, new_iter );

        gtk_tree_iter_free( new_iter );

        anchor_id = new_node_id;
        child = FALSE;
    } while ( (list = list->next) );

    g_list_free( selected );

    return;
}


static void
cb_lb_row_activated( GtkWidget* listbox, GtkWidget* row, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = (Baum) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row),
            "baum" ));
    gint node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row),
            "node-id" ));

    gtk_tree_selection_unselect_all( zond->selection[BAUM_INHALT] );
    gtk_tree_selection_unselect_all( zond->selection[BAUM_AUSWERTUNG] );

    GtkTreePath* path = baum_abfragen_path( zond->treeview[baum], node_id );
    gtk_tree_view_expand_to_path( zond->treeview[baum], path );
    gtk_tree_view_set_cursor( zond->treeview[baum], path, NULL, FALSE );
    gtk_tree_path_free( path );

    return;
}


static GtkWidget*
suchen_oeffnen_ergebnisfenster( Projekt* zond, GArray* sql_array )
{
    //Fenster erzeugen
    GtkWidget* window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(window), 1000, 400 );
    gtk_window_set_transient_for( GTK_WINDOW(window), GTK_WINDOW(zond->app_window) );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    GtkWidget* list_box = gtk_list_box_new( );
    gtk_list_box_set_selection_mode( GTK_LIST_BOX(list_box),
            GTK_SELECTION_MULTIPLE );
    gtk_list_box_set_activate_on_single_click( GTK_LIST_BOX(list_box), FALSE );

    g_object_set_data( G_OBJECT(window), "list-box", list_box );

    gtk_container_add( GTK_CONTAINER(swindow), list_box );
    gtk_container_add( GTK_CONTAINER(window), swindow );

    //Headerbar erzeugen
    GtkWidget* headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar), ":minimize,close");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);

    //popover
    GtkWidget* button_sql = gtk_button_new_with_label( "SQL" );
    GtkWidget* popover = gtk_popover_new( button_sql );

    gchar* text = g_strdup( "" );
    for ( gint i = 0; i < sql_array->len; i++ )
    {
        SQLRow sql_row = { 0 };
        sql_row = g_array_index( sql_array, SQLRow, i );

        if ( i > 0 )
        {
            if ( sql_row.or ) text = add_string( text, g_strdup( " OR\n" ) );
            else text = add_string( text, g_strdup( " AND\n" ) );
        }

        if ( sql_row.case_sensitive ) text = add_string( text, g_strdup( "(sensitiv) " ) );
        else text = add_string( text, g_strdup( "(nicht-sensitiv) " ) );

        text = add_string( text, g_strdup( sql_row.key ) );

        if ( sql_row.not_like ) text = add_string( text, g_strdup( " NOT LIKE " ) );
        else text = add_string( text, g_strdup( " LIKE " ) );

        text = add_string( text, g_strdup( sql_row.value ) );
    }

    GtkWidget* label = gtk_label_new( text );
    g_free( text );
    gtk_widget_show_all( label );

    gtk_container_add( GTK_CONTAINER(popover), label );

    //Menu Button
    GtkWidget* suchen_menu_button = gtk_menu_button_new( );

    //Menu erzeugen
    GtkWidget* suchen_menu = gtk_menu_new( );

    //Items erzeugen
    GtkWidget* suchen_nach_auswertung =
            gtk_menu_item_new_with_label( "In Baum Auswertung kopieren" );

    //Füllen
    gtk_menu_shell_append( GTK_MENU_SHELL(suchen_menu), suchen_nach_auswertung );

    //Untermenu
    GtkWidget* suchen_nach_auswertung_ebene = gtk_menu_new( );

    GtkWidget* gleiche_ebene = gtk_menu_item_new_with_label( "Gleiche Ebene" );
    GtkWidget* unterpunkt = gtk_menu_item_new_with_label( "Unterpunkt" );

    //Füllen
    gtk_menu_shell_append( GTK_MENU_SHELL(suchen_nach_auswertung_ebene),
            gleiche_ebene );
    gtk_menu_shell_append( GTK_MENU_SHELL(suchen_nach_auswertung_ebene),
            unterpunkt );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(suchen_nach_auswertung),
            suchen_nach_auswertung_ebene );

    g_object_set_data( G_OBJECT(gleiche_ebene), "list-box", list_box );
    g_object_set_data( G_OBJECT(unterpunkt), "list-box", list_box );
    g_object_set_data( G_OBJECT(unterpunkt), "child", GINT_TO_POINTER(1) );

    //menu sichtbar machen
    gtk_widget_show_all( suchen_menu );

    //einfügen
    gtk_menu_button_set_popup( GTK_MENU_BUTTON(suchen_menu_button), suchen_menu );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), suchen_menu_button );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(headerbar), button_sql );
    gtk_window_set_titlebar( GTK_WINDOW(window), headerbar );

    gtk_widget_show_all( window );

    g_signal_connect( list_box, "row-activated", G_CALLBACK(cb_lb_row_activated),
            (gpointer) zond );
    g_signal_connect( gleiche_ebene, "activate",
            G_CALLBACK(cb_suchen_nach_auswertung), (gpointer) zond );
    g_signal_connect( unterpunkt, "activate",
            G_CALLBACK(cb_suchen_nach_auswertung), (gpointer) zond );

    return window;
}


static GArray*
info_abfrage_sql( Projekt* zond, sqlite3_stmt* stmt, gchar** errmsg )
{
    gint rc = 0;

    GArray* info_treffer = g_array_new( FALSE, FALSE, sizeof( gint ) );
    gint node_id = 0;

    do
    {
        rc = sqlite3_step( stmt );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_step:\n",
                    sqlite3_errmsg( zond->db ), NULL );
            g_array_free( info_treffer, TRUE );

            return NULL;
        }
        else if ( rc == SQLITE_DONE ) break;

        node_id = sqlite3_column_int( stmt, 0 );
        g_array_append_val( info_treffer, node_id );

    }
    while ( rc == SQLITE_ROW );

    return info_treffer;
}


static sqlite3_stmt*
info_prepare_stmt( Projekt* zond, GArray* sql_array, gchar** errmsg )
{
    sqlite3_stmt* stmt = NULL;
    SQLRow sql_row = { 0 };
    gint rc = 0;

    gchar* sql = g_strdup_printf( "WITH cte_0 (node_id) AS (SELECT node_id FROM infos WHERE " );

    gint i = 0;
    for ( i = 0; i < sql_array->len; i++ )
    {
        sql_row = g_array_index( sql_array, SQLRow, i );

        if ( i > 0 ) sql = add_string( sql, g_strdup_printf( ", cte_%i (node_id) AS (SELECT node_id FROM infos WHERE ", i ) );

        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( "LOWER(" ) );
        sql = add_string( sql, g_strdup( "element" ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( ") " ) );
        if ( sql_row.not_like ) sql = add_string( sql, g_strdup( "NOT " ) );
        sql = add_string( sql, g_strdup( "LIKE " ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( "LOWER( " ) );
        sql = add_string( sql, g_strdup( "?" ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( " )" ) );
        sql = add_string( sql, g_strdup( " AND " ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( "LOWER(" ) );
        sql = add_string( sql, g_strdup( "wert" ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( ") " ) );
        if ( sql_row.not_like ) sql = add_string( sql, g_strdup( "NOT " ) );
        sql = add_string( sql, g_strdup( "LIKE " ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( "LOWER( " ) );
        sql = add_string( sql, g_strdup( "?" ) );
        if ( !(sql_row.case_sensitive) ) sql = add_string( sql, g_strdup( " )" ) );
        sql = add_string( sql, g_strdup( ") " ) );
    }

    sql = add_string( sql, g_strdup( "SELECT DISTINCT cte_0.node_id FROM cte_0" ) );

    for ( i = 1; i < sql_array->len; i++ )
    {
        SQLRow sql_row = g_array_index( sql_array, SQLRow, i );

        if ( sql_row.or ) sql = add_string( sql, g_strdup_printf( " UNION SELECT cte_%i.node_id FROM cte_%i ", i, i ) );
        else sql = add_string( sql, g_strdup_printf( " INNER JOIN cte_%i ON cte_%i.node_id=cte_%i.node_id ", i, i, i - 1 ) );
    }

    sql = add_string( sql, g_strdup( ";" ) );

    rc = sqlite3_prepare_v2( zond->db, sql, -1, &stmt, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_vs:\n"
                "sql: ", sql, "\n", sqlite3_errmsg( zond->db ), NULL );
        g_free( sql );

        return NULL;
    }

    for ( i = 0; i < sql_array->len; i++ )
    {
        sql_row = g_array_index( sql_array, SQLRow, i );
        if ( !g_strcmp0( sql_row.key, "" ) || !g_strcmp0( sql_row.value, "" ) )
        {
            g_free( sql );
            sqlite3_finalize( stmt );

            return NULL;
        }

        rc = sqlite3_bind_text( stmt, (i * 2) + 1, sql_row.key, -1, NULL );
        if ( rc != SQLITE_OK )//falscher Fähler
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_bind_text:\n",
                    sqlite3_errmsg( zond->db ), NULL );
            sqlite3_finalize( stmt );
            g_free( sql );

            return NULL;
        }

        rc = sqlite3_bind_text( stmt, (i * 2) + 2, sql_row.value, -1, NULL );
        if ( rc != SQLITE_OK )//falscher Fähler
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_bind_text:\n",
                    sqlite3_errmsg( zond->db ), NULL );
            sqlite3_finalize( stmt );
            g_free( sql );

            return NULL;
        }
    }

    return stmt;
}


void
sql_row_free_func( gpointer data )
{
    SQLRow* sql_row = (SQLRow*) data;

    g_free( sql_row->key );
    g_free( sql_row->value );

    return;
}


static void
cb_entry_value( GtkEntry* entry, gpointer data )
{
    GtkWidget* vbox = (GtkWidget*) data;
    GList* selected = NULL;
    GList* list = NULL;

    selected = gtk_container_get_children( GTK_CONTAINER(vbox) );

    GtkWidget* hbox = NULL;
    GtkWidget* hbox_next = NULL;
    GtkWidget* entry_value = NULL;

    list = selected;
    do
    {
        hbox = list->data;

        entry_value = g_object_get_data( G_OBJECT(hbox), "entry-value" );
        if ( entry_value == GTK_WIDGET(entry) )
        {
            if ( list->next )
            {
                hbox_next = list->next->data;
                gtk_widget_grab_focus( (GtkWidget*) g_object_get_data( G_OBJECT(hbox_next), "entry-key" ) );
            }
            else gtk_dialog_response( g_object_get_data( G_OBJECT(vbox), "dialog" ), GTK_RESPONSE_OK );

            break;
        }
    }
    while ( (list = list->next) );

    g_list_free( selected );

    return;
}


static void
cb_entry_key( GtkEntry* entry, gpointer user_data )
{
    GtkWidget* entry_value = g_object_get_data( G_OBJECT(user_data),
            "entry-value" );

    gtk_widget_grab_focus( entry_value );

    return;
}


static void
suchen_erzeugen_abfragezeile( GtkWidget* vbox )
{
    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 1 );

    //Falls nicht die erste Reihe: weiteren Button (And/OR)
    GList* list = gtk_container_get_children( GTK_CONTAINER(vbox) );
    if ( list )
    {
        g_list_free( list );
        GtkWidget* toggle_button_or = gtk_toggle_button_new_with_label( "OR" );
        gtk_box_pack_start( GTK_BOX(hbox), toggle_button_or, FALSE, FALSE, 1 );
        g_object_set_data( G_OBJECT(hbox), "toggle-button-or", toggle_button_or );
    }

    //toggle-button für Beachtung Groß-/Kleinschreibung
    GtkWidget* check_button_case = gtk_check_button_new_with_label(
            "Groß-/Kleinschreibung beachten" );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(check_button_case), FALSE );

    GtkWidget* frame_key = gtk_frame_new( "Schlüssel:" );
    GtkWidget* entry_key = gtk_entry_new( );
    gtk_entry_set_text( GTK_ENTRY(entry_key), "%" );
    gtk_container_add( GTK_CONTAINER(frame_key), entry_key );

    //toggle-button für LIKE/NOT LIKE
    GtkWidget* check_button_like = gtk_check_button_new_with_label(
            "!=" );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(check_button_like), FALSE );

    GtkWidget* frame_value = gtk_frame_new( "Wert:" );
    GtkWidget* entry_value = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_value), entry_value );

    gtk_box_pack_start( GTK_BOX(hbox), check_button_case, FALSE, FALSE, 1 );
    gtk_box_pack_start( GTK_BOX(hbox), frame_key, FALSE, FALSE, 1 );
    gtk_box_pack_start( GTK_BOX(hbox), check_button_like, FALSE, FALSE, 1 );
    gtk_box_pack_start( GTK_BOX(hbox), frame_value, FALSE, FALSE, 1 );

    g_object_set_data( G_OBJECT(hbox), "check-button-case", check_button_case );
    g_object_set_data( G_OBJECT(hbox), "entry-key", entry_key );
    g_object_set_data( G_OBJECT(hbox), "check-button-like", check_button_like );
    g_object_set_data( G_OBJECT(hbox), "entry-value", entry_value );

    gtk_box_pack_start( GTK_BOX(vbox), hbox, FALSE, FALSE, 1 );

    g_signal_connect( entry_key, "activate", G_CALLBACK(cb_entry_key),
            (gpointer) hbox );
    g_signal_connect( entry_value, "activate", G_CALLBACK(cb_entry_value),
            (gpointer) vbox );

    gtk_widget_grab_focus( entry_key );

    gtk_widget_show_all( vbox );

    return;
}


static GArray*
info_suchfenster( Projekt* zond )
{
/*  Dialog erzeugen  */
    GtkDialog* dialog = GTK_DIALOG(gtk_dialog_new_with_buttons( "In Infos suchen",
            GTK_WINDOW(zond->app_window), GTK_DIALOG_MODAL, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL ));

    GtkWidget* content_area = gtk_dialog_get_content_area( dialog );

    GtkWidget* vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 1 );
    gtk_container_add( GTK_CONTAINER(content_area), vbox );

    //Button für Erzeugen neuer Abfragezeile
    GtkWidget* button_neue_zeile = gtk_button_new_with_label( "Neue Zeile" );
    GtkWidget* headerbar = gtk_header_bar_new( );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), button_neue_zeile );
    gtk_window_set_titlebar( GTK_WINDOW(dialog), headerbar );

    g_object_set_data( G_OBJECT(vbox), "dialog", dialog );

    g_signal_connect_swapped( button_neue_zeile, "clicked",
            G_CALLBACK(suchen_erzeugen_abfragezeile), vbox );

    suchen_erzeugen_abfragezeile( vbox );

    gtk_widget_show_all( GTK_WIDGET(dialog) );

    gint res = gtk_dialog_run( dialog );

    if ( res != GTK_RESPONSE_OK )
    {
        gtk_widget_destroy( GTK_WIDGET(dialog) );

        return NULL;
    }

    GList* selected = NULL;
    GList* list = NULL;

    selected = gtk_container_get_children( GTK_CONTAINER(vbox) );
    GArray* sql_array = g_array_new( FALSE, FALSE, sizeof( SQLRow ) );
    g_array_set_clear_func( sql_array, (GDestroyNotify) sql_row_free_func );

    GtkWidget* hbox = NULL;
    list = selected;
    do
    {
        hbox = list->data;

        SQLRow sql_row;

        GtkWidget* check_button_case = g_object_get_data( G_OBJECT(hbox),
                "check-button-case" );
        GtkWidget* entry_key = g_object_get_data( G_OBJECT(hbox), "entry-key" );
        GtkWidget* check_button_like = g_object_get_data( G_OBJECT(hbox),
                "check-button-like" );
        GtkWidget* entry_value = g_object_get_data( G_OBJECT(hbox), "entry-value" );
        GtkWidget* toggle_button_or = g_object_get_data( G_OBJECT(hbox),
                "toggle-button-or" );


        sql_row.case_sensitive =
                gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(check_button_case) );
        sql_row.key = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_key) ) );
        sql_row.not_like =
                gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(check_button_like) );
        sql_row.value= g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_value) ) );

        if ( toggle_button_or ) sql_row.or =
                gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(toggle_button_or) );
        else sql_row.or = FALSE;

        g_array_append_val( sql_array, sql_row );
    }
    while ( (list = list->next) );

    g_list_free( selected );

    gtk_widget_destroy( GTK_WIDGET(dialog) );

    return sql_array;
}


gint
info_suchen( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    gchar* errmsg_ii = NULL;

    //Key und value abfragen
    GArray* sql_array = info_suchfenster( zond );
    if ( !sql_array ) return 0;

    //SQL-String bilden
    sqlite3_stmt* stmt = NULL;
    stmt = info_prepare_stmt( zond, sql_array, &errmsg_ii );
    if ( !stmt && errmsg )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf info_prepare_stmt:\n",
                errmsg_ii, NULL );
        g_free( errmsg_ii );
        g_array_free( sql_array, TRUE );

        return -1;
    }
    else if ( !stmt )
    {
        if ( errmsg ) *errmsg = g_strdup( "Unzulässige Eingabe" );
        g_array_free( sql_array, TRUE );

        return -2;
    }

    //sql_abfrage
    GArray* info_treffer = info_abfrage_sql( zond, stmt, &errmsg_ii );
    sqlite3_finalize( stmt );
    if ( (!info_treffer) && errmsg_ii )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf info_abfrage_sql:\n",
                errmsg_ii, NULL );
        g_free( errmsg_ii );
        g_array_free( sql_array, TRUE );

        return -1;
    }

    if ( info_treffer->len == 0 )
    {
        if ( errmsg ) *errmsg = g_strdup( "Keine Treffer gefunden" );
        g_array_free( info_treffer, TRUE );
        g_array_free( sql_array, TRUE );

        return -2;
    }

    //Ergebnisfenster öffnen
    GtkWidget* ergebnisfenster = suchen_oeffnen_ergebnisfenster( zond, sql_array );
    g_array_free( sql_array, TRUE );

    //und füllen
    rc = suchen_fuellen_ergebnisfenster( zond, ergebnisfenster, info_treffer,
            &errmsg_ii );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf suchen_fuellen_"
                "ergebnisfenster:\n", errmsg_ii, NULL );
        g_free( errmsg_ii );
        g_array_free( info_treffer, TRUE );
        gtk_widget_destroy( ergebnisfenster );

        return -1;
    }

    g_array_free( info_treffer, TRUE );

    return 0;
}
