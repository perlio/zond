/*
zond (ziele.c) - Akten, Beweisstücke, Unterlagen
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

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../99conv/db_write.h"
#include "../99conv/pdf.h"
#include "../99conv/db_zu_baum.h"
#include "../99conv/baum.h"
#include "../99conv/mupdf.h"

#include "../40viewer/document.h"

#include "zieleplus.h"

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gtk/gtk.h>


gboolean
ziele_1_gleich_2( const Anbindung anbindung1, const Anbindung anbindung2 )
{
    if ( (anbindung1.von.seite == anbindung2.von.seite) &&
            (anbindung1.bis.seite == anbindung2.bis.seite) &&
            (anbindung1.von.index == anbindung2.von.index) &&
            (anbindung1.bis.index == anbindung2.bis.index) ) return TRUE;
    else return FALSE;
}


static gboolean
ziele_1_vor_2( Anbindung anbindung1, Anbindung anbindung2 )
{
    if ( (anbindung1.bis.seite < anbindung2.von.seite) ||
            ((anbindung1.bis.seite == anbindung2.von.seite) &&
            (anbindung1.bis.index < anbindung2.von.index)) ) return TRUE;
    else return FALSE;
}


static gboolean
ziele_1_eltern_von_2( Anbindung anbindung1, Anbindung anbindung2 )
{
    if ( ((anbindung1.von.seite < anbindung2.von.seite) ||
            ((anbindung1.von.seite == anbindung2.von.seite) &&
            (anbindung1.von.index <= anbindung2.von.index)))
            &&
            //hinten:
            ((anbindung1.bis.seite > anbindung2.bis.seite) ||
            ((anbindung1.bis.seite == anbindung2.bis.seite) &&
            (anbindung1.bis.index >= anbindung2.bis.index))) ) return TRUE;
    else return FALSE;
}



static gint
ziele_verschieben_kinder( Projekt* zond,
                          gint node_id,
                          Anbindung anbindung,
                          gchar** errmsg )
{
    gint rc = 0;

/*  kinder an node_id anhängen  */
    //younger_sibling ermittelm
    gint younger_sibling = 0;
    gint older_sibling = 0;

    while ( (younger_sibling = db_get_younger_sibling( zond, BAUM_INHALT,
            node_id, errmsg )) )
    {
        if ( younger_sibling < 0 ) ERROR_PAO( "db_get_younger_siblings" )

        Anbindung* anbindung_younger_sibling = NULL;
        rc = abfragen_rel_path_and_anbindung( zond, BAUM_INHALT, younger_sibling,
                NULL, &anbindung_younger_sibling, errmsg );
        if ( rc == -1 ) ERROR_PAO( "abfragen_rel_path_and_anbindung" )
        else if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup( "Keine Anbindung gespeichert" );

            return -1;
        }
        else
        {
            gboolean kind = ziele_1_eltern_von_2( anbindung,
                    *anbindung_younger_sibling );
            g_free( anbindung_younger_sibling );
            if ( !kind ) break;
        }

        rc = knoten_verschieben( zond, BAUM_INHALT, younger_sibling, node_id,
                older_sibling, errmsg );
        if ( rc ) ERROR_PAO( "knoten_verschieben" )
        older_sibling = younger_sibling;
    }

    return 0;
}


static gint
ziele_einfuegen_db( Projekt* zond, gint anchor_id, gboolean kind, Ziel* ziel,
        const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    gint new_node = 0;

    //zuerst in baum_inhalt-Tabelle (weil FK in Tabelle "ziele" darauf Bezug nimmt)
    new_node = db_insert_node( zond, BAUM_INHALT, anchor_id, kind,
            zond->icon[ICON_ANBINDUNG].icon_name, node_text, errmsg );
    if ( new_node == -1 ) ERROR_PAO( "db_insert_node" )

    sqlite3_reset( zond->stmts.ziele_einfuegen[0] );
    sqlite3_clear_bindings( zond->stmts.ziele_einfuegen[0] );

    rc = sqlite3_bind_text( zond->stmts.ziele_einfuegen[0], 1,
            ziel->ziel_id_von, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text [0,1]" )

    rc = sqlite3_bind_int( zond->stmts.ziele_einfuegen[0], 2, ziel->index_von );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [0,2]" )

    rc = sqlite3_bind_text( zond->stmts.ziele_einfuegen[0], 3, ziel->ziel_id_bis,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text [0,3]" )

    rc = sqlite3_bind_int( zond->stmts.ziele_einfuegen[0], 4, ziel->index_bis );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text [0,4]" )

    rc = sqlite3_bind_int( zond->stmts.ziele_einfuegen[0], 5, anchor_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [0,5]" )

    rc = sqlite3_step( zond->stmts.ziele_einfuegen[0] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0]" )

    return new_node;
}


static gint
ziele_einfuegen_anbindung( Projekt* zond, const gchar* rel_path, gint anchor_id,
        gboolean kind, Anbindung anbindung, Ziel* ziel, gchar** errmsg )
{
    gint rc = 0;
    gint new_node = 0;

    gchar* node_text = NULL;
    if ( anbindung.bis.seite > anbindung.von.seite) node_text =
            g_strdup_printf( "S. %i - %i, %s", anbindung.von.seite + 1,
            anbindung.bis.seite + 1, rel_path );
    else node_text = g_strdup_printf( "S. %i, %s", anbindung.von.seite + 1,
            rel_path );

    rc = db_begin( zond, errmsg );
    if ( rc )
    {
        g_free( node_text );

        ERROR_PAO( "db_begin" )
    }

    new_node = ziele_einfuegen_db( zond, anchor_id, kind, ziel, node_text, errmsg );
    g_free( node_text );
    if ( new_node == -1) ERROR_PAO_ROLLBACK( "ziele_einfuegen_db" )

    //eingefügtes ziel in Baum
    GtkTreeIter* iter = baum_abfragen_iter( zond->treeview[BAUM_INHALT], anchor_id );
    GtkTreeIter* new_iter = db_baum_knoten( zond, BAUM_INHALT, new_node, iter,
            kind, errmsg );
    gtk_tree_iter_free( iter );
    if ( !new_iter ) ERROR_PAO_ROLLBACK( "db_baum_knoten_mit_kindern" )

    rc = ziele_verschieben_kinder( zond, new_node, anbindung, errmsg );
    if ( rc ) ERROR_PAO_ROLLBACK( "ziele_verschieben_kinder" )

    rc = db_commit( zond, errmsg );
    if ( rc ) ERROR_PAO_ROLLBACK( "db_commit" )

    expand_row( zond, BAUM_INHALT, new_iter );
    baum_setzen_cursor( zond, BAUM_INHALT, new_iter );

    gtk_tree_iter_free( new_iter );

    return 0;
}


static gint
ziele_erzeugen_ziel( GtkWidget* window, const DisplayedDocument* dd,
        Anbindung anbindung, Ziel* ziel, gchar** errmsg )
{
    gint rc = 0;

    g_mutex_lock( &dd->document->mutex_doc );

    //schon nameddest zur Seite?
    rc = pdf_document_get_dest( dd->document->ctx,
            pdf_specifics( dd->document->ctx, dd->document->doc ),
            anbindung.von.seite, (gpointer*) &ziel->ziel_id_von, TRUE, errmsg );
    if ( rc )
    {
        g_mutex_unlock( &dd->document->mutex_doc );

        ERROR_PAO( "pdf_document_get_dest" )
    }

    //nameddest herausfinden bzw. einfügen
    rc = pdf_document_get_dest( dd->document->ctx,
            pdf_specifics( dd->document->ctx, dd->document->doc ),
            anbindung.bis.seite, (gpointer*) &ziel->ziel_id_bis, TRUE, errmsg );
    if ( rc )
    {
        g_mutex_unlock( &dd->document->mutex_doc );

        ERROR_PAO( "pdf_document_get_dest" )
    }

    gint page_number1 = -1;
    gint page_number2 = -1;

    if ( !ziel->ziel_id_von ) page_number1 = anbindung.von.seite;
    if ( !ziel->ziel_id_bis ) page_number2 = anbindung.bis.seite;

    if ( page_number1 >= 0 || page_number2 >= 0 )
    {
        if ( dd->document->dirty )
        {
            rc = abfrage_frage( window, "Änderungen müssen vor Einfügen von "
                    "Anbindungen gespeichert werden", "Änderungen speichern?", NULL );
            if ( rc == GTK_RESPONSE_YES )
            {
                rc = mupdf_save_document( dd->document, errmsg );
                if ( rc )
                {
                    g_mutex_unlock( &dd->document->mutex_doc );
                    ERROR_PAO_R( "mupdf_save_document", -2 )
                }
            }
            else
            {
                rc = mupdf_open_document( dd->document, errmsg );
                g_mutex_unlock( &dd->document->mutex_doc );
                if ( rc ) ERROR_PAO_R( "mupdf_open_document", -2 )

                return 1;
            }
        }
        else mupdf_close_document( dd->document );

        //namedDest einfügen
        rc = SetDestPage( dd, page_number1, page_number2,
                &ziel->ziel_id_von, &ziel->ziel_id_bis, errmsg );
        if ( rc )
        {
            meldung( window, "Anbindung konnte nicht erzeugt "
                    "werden\n\nBei Aufruf SetDestPage:\n", *errmsg, NULL );
            g_free( *errmsg );

            rc = mupdf_open_document( dd->document, errmsg );
            g_mutex_unlock( &dd->document->mutex_doc );
            if ( rc ) ERROR_PAO_R( "mupdf_open_document", -2 )

            return 1;
        }

        rc = mupdf_open_document( dd->document, errmsg );
        if ( rc )
        {
            g_mutex_unlock( &dd->document->mutex_doc );
            ERROR_PAO_R( "mupdf_open_document", -2 )
        }
    }

    g_mutex_unlock( &dd->document->mutex_doc );

    ziel->index_von = anbindung.von.index;
    ziel->index_bis = anbindung.bis.index;

    return 0;
}


gint
ziele_abfragen_anker_rek( Projekt* zond, gint node_id, Anbindung anbindung,
        gboolean* kind, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;
    gint first_child_id = 0;

    if ( node_id == 0 ) return 0;

    Anbindung* anbindung_node_id = NULL;
    rc = abfragen_rel_path_and_anbindung( zond, BAUM_INHALT, node_id, NULL,
            &anbindung_node_id, errmsg );
    if ( rc == -1 ) ERROR_PAO( "abfragen_rel_path_and_anbindung" )
    else if ( rc ) //Datei, hat ja kein ziel gespeichert
    {
        *kind = TRUE;
        new_node_id = node_id;

        first_child_id = db_get_first_child( zond, BAUM_INHALT,
                node_id, errmsg );
        if ( first_child_id < 0 ) ERROR_PAO( "db_get_first_child" )
        else if ( first_child_id > 0 ) //hat kind
        {
            gint res = ziele_abfragen_anker_rek( zond, first_child_id, anbindung,
                    kind, errmsg );
            if ( res == -1 ) return -1; //bei rekursivem Aufruf Fehlermeldung nicht "aufblasen"

            if ( res > 0 ) new_node_id = res;
        }
    }
    else //anbindung zurück
    {
        //ziele auf Identität prüfen
        if ( ziele_1_gleich_2( *anbindung_node_id, anbindung ) )
        {
            g_free( anbindung_node_id );

            ERROR_PAO( "Einzufügendes Ziel mit bestehendem Ziel identisch" )
        }

        //Knoten kommt als parent in Beracht
        if ( ziele_1_eltern_von_2( *anbindung_node_id, anbindung ) )
        {
            *kind = TRUE;
            new_node_id = node_id;

            first_child_id = db_get_first_child( zond, BAUM_INHALT,
                    node_id, errmsg );
            if ( first_child_id < 0 )
            {
                g_free( anbindung_node_id );

                ERROR_PAO( "db_get_first_child" )
            }
            else if ( first_child_id > 0 ) //hat kind
            {
                gint res = ziele_abfragen_anker_rek( zond, first_child_id, anbindung,
                        kind, errmsg );
                if ( res == -1 )
                {
                    g_free( anbindung_node_id );

                    return -1;
                }

                if ( res > 0 ) new_node_id = res;
            }
        }
        //Seiten oder Punkte vor den einzufügenden Punkt
        else if ( ziele_1_vor_2( *anbindung_node_id, anbindung ) )
        {
            *kind = FALSE;
            new_node_id = node_id;

            gint younger_sibling_id = db_get_younger_sibling( zond, BAUM_INHALT,
                    node_id, errmsg );
            if ( younger_sibling_id < 0 )
            {
                g_free( anbindung_node_id );

                ERROR_PAO( "db_get_younger_sibling" )
            }

            if ( younger_sibling_id != 0 )
            {
                gint res = ziele_abfragen_anker_rek( zond, younger_sibling_id, anbindung,
                        kind, errmsg );
                if ( res == -1 )
                {
                    g_free( anbindung_node_id );

                    return -1;
                }

                if ( res > 0 ) new_node_id = res;
            }
        }
        // wenn nicht danach, bleibt ja nur Überschneidung!!
        else if ( !ziele_1_vor_2( anbindung, *anbindung_node_id ) &&
                !ziele_1_eltern_von_2( anbindung, *anbindung_node_id ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Eingegebenes Ziel überschneidet "
                    "sich mit bereits bestehendem Ziel", NULL );
            g_free( anbindung_node_id );

            return -1;
        }
    }

    return new_node_id;
}


gint
ziele_erzeugen_anbindung( Projekt* zond, GtkWidget* window, const DisplayedDocument* dd,
        Anbindung anbindung, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    gint anchor_id = 0;
    gboolean kind = FALSE;
    Ziel* ziel = NULL;

    node_id = db_get_node_id_from_rel_path( zond, dd->document->path, errmsg );
    if ( node_id == -1 ) ERROR_PAO( "db_get_node_id_from_rel_path" )
    else if ( node_id == 0 ) ERROR_PAO( "Datei nicht vorhanden" )

    //Kinder von Knoten mit DateiID=datei_id durchgehen
    anchor_id = ziele_abfragen_anker_rek( zond, node_id, anbindung, &kind, errmsg );
    if ( anchor_id == -1 ) ERROR_PAO( "ziele_abfragen_anker_rek" )

    ziel = g_malloc0( sizeof( Ziel ) );

    rc = ziele_erzeugen_ziel( window, dd, anbindung, ziel, errmsg );
    if ( rc ) ziele_free( ziel );
    if ( rc == 1 ) return 1;
    else if ( rc == -1 ) ERROR_PAO( "ziele_erzeugen_ziel" )
    else if ( rc == -2 ) ERROR_PAO_R( "ziele_erzeugen_ziel", -2 )

    rc = ziele_einfuegen_anbindung( zond, dd->document->path, anchor_id, kind,
            anbindung, ziel, errmsg );
    ziele_free( ziel );
    if ( rc ) ERROR_PAO( "ziele_einfuegen_anbindung" )

    return 0;
}


