/*
zond (seiten.c) - Akten, Beweisstücke, Unterlagen
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

#include "../99conv/general.h"
#include "../99conv/pdf.h"
#include "../99conv/mupdf.h"
#include "../99conv/db_read.h"
#include "../99conv/pdf_ocr.h"

#include "../20allgemein/ziele.h"

#include "viewer.h"
#include "render.h"
#include "annot.h"
#include "document.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>


static GPtrArray*
seiten_get_document_pages( PdfViewer* pv, GArray* arr_seiten )
{
    DocumentPage* document_page = NULL;

    GPtrArray* arr_document_page = g_ptr_array_new( );

    if ( arr_seiten )
    {
        for ( gint i = 0; i < arr_seiten->len; i++ )
        {
            document_get_dd( pv, g_array_index( arr_seiten, gint, i ),
                    &document_page, NULL, NULL );

            if ( !g_ptr_array_find( arr_document_page, document_page, NULL ) )
                    g_ptr_array_add( arr_document_page, document_page );
        }
    }

    return arr_document_page;
}


static GArray*
seiten_markierte_thumbs( PdfViewer* pv )
{
    GList* selected = NULL;
    GList* list = NULL;
    GArray* arr_page_pv = NULL;
    gint* index = NULL;

    selected = gtk_tree_selection_get_selected_rows(
            gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) ), NULL );

    if ( !selected ) return NULL;

    arr_page_pv = g_array_new( FALSE, FALSE, sizeof( gint ) );
    list = selected;
    do
    {
        index = gtk_tree_path_get_indices( list->data );
        g_array_append_val( arr_page_pv, index[0] );
    }
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return arr_page_pv;
}


static gint
compare_gint ( gconstpointer a, gconstpointer b )
{
  const gint *_a = a;
  const gint *_b = b;

  return *_a - *_b;
}


static GArray*
seiten_parse_text( PdfViewer* pv, gint max, const gchar* text )
{
    gint start = 0;
    gint end = 0;
    gchar* range = NULL;
    gint i = 0;
    gint page = 0;
    gint last_inserted = -1;

    if ( !fz_is_page_range( NULL, text ) ) return NULL;

    GArray* arr_tmp = g_array_new( FALSE, FALSE, sizeof( gint ) );
    GArray* arr_pages = g_array_new( FALSE, FALSE, sizeof( gint ) );

    range = (gchar*) text;
    while ((range = (gchar*) fz_parse_page_range( NULL, range, &start, &end, max )) )
    {
        if ( start < end )
        {
            for ( i = start; i <= end; i++ ) g_array_append_val( arr_tmp, i );
        }
        else if ( start > end )
        {
            for ( i = start; i >= end; i-- ) g_array_append_val( arr_tmp, i );
        }
        else g_array_append_val( arr_tmp, start );
    }

    g_array_sort( arr_tmp, (GCompareFunc) compare_gint );

    for ( i = 0; i < arr_tmp->len; i++ )
    {
        page = g_array_index( arr_tmp, gint, i ) - 1;
        if ( page != last_inserted )
        {
            g_array_append_val( arr_pages, page );
            last_inserted = page;
        }
    }

    g_array_free( arr_tmp, TRUE );

    return arr_pages;
}


static void
cb_seiten_drehen_entry( GtkEntry* entry, gpointer dialog )
{
    gtk_dialog_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK );

    return;
}


static void
cb_radio_auswahl_toggled( GtkToggleButton* button, gpointer data )
{
    gtk_widget_set_sensitive( (GtkWidget*) data,
            gtk_toggle_button_get_active( button ) );

    return;
}


static GPtrArray*
seiten_abfrage_seiten( PdfViewer* pv, const gchar* title, gint* winkel )
{
    gint rc = 0;
    GtkWidget* radio_90_UZS = NULL;
    GtkWidget* radio_180 = NULL;
    GtkWidget* radio_90_gegen_UZS = NULL;
    gchar* text = NULL;
    GArray* arr_seiten_pv = NULL;

    GtkWidget* dialog = gtk_dialog_new_with_buttons( title,
            GTK_WINDOW(pv->vf), GTK_DIALOG_MODAL,
            "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    if ( winkel )
    {
        radio_90_UZS = gtk_radio_button_new_with_label( NULL, "90° im Uhrzeigersinn" );
        radio_180 = gtk_radio_button_new_with_label( NULL, "180°" );
        radio_90_gegen_UZS = gtk_radio_button_new_with_label( NULL, "90° gegen Uhrzeigersinn" );

        gtk_radio_button_join_group (GTK_RADIO_BUTTON(radio_180),
                GTK_RADIO_BUTTON(radio_90_UZS) );
        gtk_radio_button_join_group( GTK_RADIO_BUTTON(radio_90_gegen_UZS),
                GTK_RADIO_BUTTON(radio_180) );

        gtk_box_pack_start( GTK_BOX(content_area), radio_90_UZS, FALSE, FALSE, 0 );
        gtk_box_pack_start( GTK_BOX(content_area), radio_180, FALSE, FALSE, 0 );
        gtk_box_pack_start( GTK_BOX(content_area), radio_90_gegen_UZS, FALSE, FALSE, 0 );
    }

    GtkWidget* radio_alle = gtk_radio_button_new_with_label( NULL, "Alle" );
    GtkWidget* radio_mark = gtk_radio_button_new_with_label( NULL, "Markierte" );
    GtkWidget* radio_auswahl =
            gtk_radio_button_new_with_label( NULL, "Seiten auswählen" );

    gtk_radio_button_join_group (GTK_RADIO_BUTTON(radio_alle),
            GTK_RADIO_BUTTON(radio_auswahl) );
    gtk_radio_button_join_group( GTK_RADIO_BUTTON(radio_mark),
            GTK_RADIO_BUTTON(radio_alle) );

    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), radio_auswahl, FALSE, FALSE, 0 );

    GtkWidget* entry = gtk_entry_new( );
    gtk_box_pack_start( GTK_BOX(hbox), entry, FALSE, FALSE, 0 );

    gtk_box_pack_start( GTK_BOX(content_area), radio_alle, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(content_area), radio_mark, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(content_area), hbox, FALSE, FALSE, 0 );

    g_signal_connect( radio_auswahl, "toggled",
            G_CALLBACK( cb_radio_auswahl_toggled), entry );

    if ( !gtk_tree_selection_count_selected_rows( gtk_tree_view_get_selection(
            GTK_TREE_VIEW(pv->tree_thumb) ) ) )
    {
        gtk_widget_set_sensitive( radio_mark, FALSE );
        gtk_widget_grab_focus( entry );
    }
    else gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(radio_mark), TRUE );

    g_signal_connect( entry, "activate", G_CALLBACK(cb_seiten_drehen_entry),
            (gpointer) dialog );

    gtk_widget_show_all( dialog );

    rc = gtk_dialog_run( GTK_DIALOG(dialog) );

    if ( rc == GTK_RESPONSE_OK )
    {
        if ( winkel )
        {
            if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_90_UZS) ) )
                    *winkel = 90;
            else if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_180) ) )
                    *winkel = 180;
            else *winkel = -90;
        }

        if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_auswahl) ) )
        {
            text = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry) ) );
            if ( text ) arr_seiten_pv =
                    seiten_parse_text( pv, pv->arr_pages->len, text );
            g_free( text );
        }
        else if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_alle) ) )
        {
            arr_seiten_pv = g_array_new( FALSE, FALSE, sizeof( gint ) );
            for ( gint i = 0; i < pv->arr_pages->len; i++ )
                    g_array_append_val( arr_seiten_pv, i );
        }
        else if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_mark) ) )
        {
            arr_seiten_pv = seiten_markierte_thumbs( pv );
        }
    }

    gtk_widget_destroy( dialog );

    GPtrArray* arr_document_pages = seiten_get_document_pages( pv, arr_seiten_pv );
    g_array_unref( arr_seiten_pv );

    return arr_document_pages;
}


/*
**  Seiten OCR
*/
gint
cb_foreach_ocr( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER(page_pv + 1), NULL );

    if ( viewer_page_ist_sichtbar( pv, page_pv ) )
            g_thread_pool_move_to_front( pv->thread_pool_page, GINT_TO_POINTER(page_pv + 1) );

    return 0;
}


void
cb_pv_seiten_ocr( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* title = NULL;
    GPtrArray* arr_document_page = NULL;
    InfoWindow* info_window = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //zu OCRende Seiten holen
    title = g_strdup_printf( "Seiten OCR (1 - %i):", pv->arr_pages->len );
    arr_document_page = seiten_abfrage_seiten( pv, title, NULL );
    g_free( title );

    info_window = info_window_open( pv->vf, "OCR" );

    rc = pdf_ocr_pages( pv->zond, info_window, arr_document_page, &errmsg );
    info_window_close( info_window );
    if ( rc == -1 )
    {
        meldung( pv->vf, "Fehler - OCR\n\nBei Aufruf pdf_ocr_doc:\n", errmsg,
                NULL );
        g_free( errmsg );
        g_ptr_array_unref( arr_document_page );

        return;
    }

    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        DocumentPage* document_page = g_ptr_array_index( arr_document_page, i );

        gint page_doc = document_get_index_of_document_page( document_page );

        //mit mutex sichern...
        rc = viewer_reload_document_page( pv, document_page->document,
                page_doc, 1, &errmsg );
        if ( rc )
        {
            meldung( pv->vf, "Fehler - OCR\n\nBei Aufruf viewer_reload_fz_page:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }

        rc = viewer_foreach( pv->zond->arr_pv, document_page->document, page_doc, cb_foreach_ocr, NULL, &errmsg );
        if ( rc )
        {
            meldung( pv->vf, "Fehler - OCR\n\nBei Aufruf viewer_foreach:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }
    }

    g_ptr_array_unref( arr_document_page );

    return;
}


/*
**      Seiten drehen
*/
static gint
seiten_cb_drehen( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    gint winkel = 0;
    gint rc = 0;
    GtkTreeIter iter;

    winkel = GPOINTER_TO_INT(data);
    if ( winkel == 90 || winkel == -90 ) g_object_set_data( G_OBJECT(pv->layout),
            "dirty", GINT_TO_POINTER(1) );

    ViewerPage* viewer_page = g_ptr_array_index( pv->arr_pages, page_pv );

    gtk_image_clear( GTK_IMAGE(viewer_page->image) );

    rc = viewer_get_iter_thumb( pv, page_pv, &iter, errmsg );
    if ( rc == -1 ) ERROR_PAO( "viewer_get_iter_thumb" )

    gtk_list_store_set( GTK_LIST_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) )), &iter, 1, FALSE, -1 );

    g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER(page_pv + 1), NULL );

    if ( viewer_page_ist_sichtbar( pv, page_pv ) ||
            viewer_thumb_ist_sichtbar( pv, page_pv ) )
            g_thread_pool_move_to_front( pv->thread_pool_page, GINT_TO_POINTER(page_pv + 1) );

    return 0;
}


static gint
seiten_drehen_pdf( DocumentPage* document_page, gint winkel, gchar** errmsg )
{
    pdf_page* page = NULL;
    pdf_obj* page_obj = NULL;
    pdf_obj* rotate_obj = NULL;
    gint rotate = 0;
    fz_context* ctx = document_page->document->ctx;

    page = pdf_page_from_fz_page( ctx, document_page->page );
    page_obj = page->obj;

    rotate_obj = pdf_dict_get_inheritable( ctx, page_obj, PDF_NAME(Rotate) );
    if ( !rotate_obj )
    {
        rotate_obj = pdf_new_int( ctx, (int64_t) winkel );
        fz_try( ctx ) pdf_dict_put_drop( ctx, page_obj, PDF_NAME(Rotate), rotate_obj );
        fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_put_drop" )
    }
    else
    {
        rotate = pdf_to_int( ctx, rotate_obj );
        rotate += winkel;
        if ( rotate < 0 ) rotate += 360;
        else if ( rotate > 360 ) rotate -= 360;
        else if ( rotate == 360 ) rotate = 0;

        pdf_set_int( ctx, rotate_obj, (int64_t) rotate );
    }

    return 0;
}


static gint
seiten_drehen( PdfViewer* pv, GPtrArray* arr_document_page, gint winkel, gchar** errmsg )
{
    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        gint rc = 0;

        DocumentPage* document_page = g_ptr_array_index( arr_document_page, i );
        gint page_doc = document_get_index_of_document_page( document_page );

        g_mutex_lock( &document_page->document->mutex_doc );

        rc = seiten_drehen_pdf( document_page, winkel, errmsg );
        if ( rc == -1 )
        {
            g_mutex_unlock( &document_page->document->mutex_doc );
            ERROR_PAO( "seiten_drehen_pdf" )
        }

        rc = viewer_reload_document_page( pv, document_page->document, page_doc, 3, errmsg );
        if ( rc )
        {
            g_mutex_unlock( &document_page->document->mutex_doc );
            ERROR_PAO( "viewer_reload_document_page" )
        }

        g_mutex_unlock( &document_page->document->mutex_doc );

        rc = viewer_foreach( pv->zond->arr_pv, document_page->document, page_doc,
                seiten_cb_drehen, GINT_TO_POINTER(winkel), errmsg );
        if ( rc )
        {
            viewer_schliessen( pv );

            ERROR_PAO( "viewer_foreach" )
        }
    }

    return 0;
}


void
cb_pv_seiten_drehen( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint winkel = 0;
    gchar* title = NULL;
    GPtrArray* arr_document_page = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //zu drehende Seiten holen
    title = g_strdup_printf( "Seiten drehen (1 - %i):",
            document_get_num_of_pages_of_pv( pv ) );
    arr_document_page = seiten_abfrage_seiten( pv, title, &winkel );
    g_free( title );

    rc = seiten_drehen( pv, arr_document_page, winkel, &errmsg );
    g_ptr_array_unref( arr_document_page );
    if ( rc )
    {
        meldung( pv->vf, "Fehler in Seiten drehen -\n\nBei Aufruf "
                "seiten_drehen\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    viewer_refresh_layouts( pv->zond->arr_pv );

    return;
}


/*
**      Seiten löschen
*/
static gint
seiten_cb_loesche_seite( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    GPtrArray** p_arr_pv = data;

    gboolean closed = FALSE;
    gchar* path_string = NULL;
    GtkTreeIter iter;

    //in Array suchen, ob pv schon vorhanden
    for ( gint i = 0; i < (*p_arr_pv)->len; i++ )
    {
        if ( g_ptr_array_index( (*p_arr_pv), i ) == pv )
        {
            closed = TRUE;
            break;
        }
    }

    //Falls noch nicht: erstmal thread_pool abstellen
    if ( closed == FALSE )
    {
        viewer_close_thread_pools( pv );
        g_ptr_array_add( (*p_arr_pv), pv );
    }

    //pv muß neues layout haben!
    g_object_set_data( G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1) );

    g_ptr_array_remove_index( pv->arr_pages, page_pv ); //viewer_page wird freed!

    path_string = g_strdup_printf( "%i", page_pv );
    if ( gtk_tree_model_get_iter_from_string( gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) ), &iter, path_string ) )
            gtk_list_store_remove( GTK_LIST_STORE( gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) ) ), &iter );

    g_free( path_string );

    return 0;
}


static gint
seiten_anbindung( PdfViewer* pv, GPtrArray* arr_document_page, gchar** errmsg )
{
    gint rc = 0;
    GPtrArray* arr_dests = NULL;
    DocumentPage* document_page = NULL;

    arr_dests = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    //Alle NamedDests der zu löschenden Seiten sammeln
    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        document_page = g_ptr_array_index( arr_document_page, i );

        gint page_doc = document_get_index_of_document_page( document_page );

        rc = pdf_document_get_dest( document_page->document->ctx,
                pdf_specifics( document_page->document->ctx,
                document_page->document->doc ), page_doc, (gpointer*) &arr_dests, FALSE, errmsg );
        if ( rc )
        {
            g_ptr_array_free( arr_dests, TRUE );

            ERROR_PAO( "pdf_document_get_dest" )
        }
    }
#ifdef VIEWER
    if ( arr_dests->len > 0 )
    {
        rc = abfrage_frage( pv->vf, "Zu löschende Seiten enthalten Ziele",
                "Trotzdem löschen?", NULL );
        if ( rc != GTK_RESPONSE_YES )
        {
            g_ptr_array_free( arr_dests, TRUE );
            return 1;
        }
    }
#else
    //Überprüfen, ob NamedDest in db als ziel
    for ( gint i = 0; i < arr_dests->len; i++ )
    {
        rc = db_check_id( pv->zond, g_ptr_array_index( arr_dests, i ), errmsg );
        if ( rc == -1 )
        {
            g_ptr_array_free( arr_dests, TRUE );
            ERROR_PAO( "db_check_id" )
        }
        if ( rc == 1 )
        {
            g_ptr_array_free( arr_dests, TRUE );
            return 1;
        }
    }
#endif // VIEWER

    g_ptr_array_free( arr_dests, TRUE );

    return 0;
}


static gint
seiten_loeschen( PdfViewer* pv, GPtrArray* arr_document_page, gchar** errmsg )
{
    gint rc = 0;

    //Abfrage, ob Anbindung mit Seite verknüpft
    rc = seiten_anbindung( pv, arr_document_page, errmsg );
    if ( rc )
    {
        if ( rc == -1 ) ERROR_PAO( "seiten_anbindung" );
        if ( rc == 1 ) return -2;
    }

    fz_context* ctx = pv->dd->document->ctx;

    GPtrArray* arr_pv = g_ptr_array_new( );

    for ( gint i = arr_document_page->len - 1; i >= 0; i-- )
    {
        gint page_pv = document_get_index_of_document_page( g_ptr_array_index( arr_document_page, i ) );

        //macht - sofern noch nicht geschehen - thread_pool des pv dicht, in dem Seite angezeigt wird
        //Dann wird Seite aus pv gelöscht
        //seiten_cb_loesche_seite gibt niemals Fehler zurück
        viewer_foreach( pv->zond->arr_pv, pv->dd->document, page_pv,
                seiten_cb_loesche_seite, (gpointer) &arr_pv, errmsg );

        //Seite aus document entfernen
        g_ptr_array_remove_index( pv->dd->document->pages, page_pv ); //ist gleich page_doc

        //Seite aus PDF entfernen
        fz_try( ctx ) pdf_delete_page( ctx, pdf_specifics( ctx,
                pv->dd->document->doc ), page_pv );
        fz_catch( ctx )
        {
            g_ptr_array_unref( arr_pv );
            ERROR_MUPDF( "pdf_delete_page" )
        }

        fz_try( ctx ) pdf_clean_document( ctx, pdf_specifics( ctx, pv->dd->document->doc ) );
        fz_catch( ctx )
        {
            g_ptr_array_unref( arr_pv );
            ERROR_MUPDF( "pdf_clean_document" )
        }
    }

    viewer_refresh_layouts( pv->zond->arr_pv );

    //abgeschaltete thread_pools wieder anschalten
    for ( gint i = 0; i < arr_pv->len; i++ )
            viewer_init_thread_pools( g_ptr_array_index( arr_pv, i ) );

    g_ptr_array_unref( arr_pv );

    gtk_tree_selection_unselect_all(
            gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) ) );

    return 0;
}


void
cb_pv_seiten_loeschen( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* title = NULL;
    GPtrArray* arr_document_page = NULL;
    gint count = 0;

    PdfViewer* pv = (PdfViewer*) data;

    //Seiten löschen nur in "ganzen" Pdfs
    if ( pv->dd->next != NULL || pv->dd->anbindung != NULL )
    {
        meldung( pv->vf, "Seiten aus Auszug löschen nicht möglich" , NULL );
        return;
    }
/*    if ( pv->dd->document->ref_count > 1 ) //document soll nur einmal (hier) geöffnet sein
    {
        meldung( pv->vf, "Dokument ist mehrfach geöffnet", NULL );
        return;
    }
*/
    count = document_get_num_of_pages_of_dd( pv->dd );

    //zu löschende Seiten holen
    title = g_strdup_printf( "Seiten löschen (1 - %i):", count );
    arr_document_page = seiten_abfrage_seiten( pv, title, NULL );
    g_free( title );

    rc = seiten_loeschen( pv, arr_document_page, &errmsg );
    if ( rc == -1 )
    {
        meldung( pv->vf, "Fehler in Seiten löschen -\n\nBei Aufruf "
                "seiten_loeschen:\n", errmsg, "\n\nViewer wird geschlossen", NULL );
        g_free( errmsg );

        viewer_schliessen( pv );

        return;
    }
#ifndef VIEWER
    else if ( rc == -2 ) meldung( pv->vf, "Fehler in Seiten löschen -\n\n"
            "Zu löschende Seiten enthalten Anbindungen", NULL );
#endif // VIEWER

//    viewer_init_thread_pools( pv );

    return;
}


/*
**      Seiten einfügen
*/
static gint
seiten_cb_einfuegen( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    DisplayedDocument* dd = NULL;
    gint page_dd = 0;
    gint page_doc = 0;

    gint count = GPOINTER_TO_INT(data);

    g_object_set_data( G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1) );

    dd = document_get_dd( pv, page_pv, NULL, &page_dd, &page_doc );
    if ( dd->anbindung )
    {
        if ( !(page_doc <= dd->anbindung->von.seite || page_doc > dd->anbindung->bis.seite) )
                dd->anbindung->bis.seite = dd->anbindung->bis.seite + count;
    }

    for ( gint u = 0; u < count; u++ )
    {
        ViewerPage* viewer_page = viewer_new_viewer_page( pv );
        g_ptr_array_insert( pv->arr_pages, page_pv + u, viewer_page );

        viewer_insert_thumb( pv, page_pv + u,
                viewer_get_displayed_rect_from_dd( dd, page_dd + u ) );
    }

    for ( gint i = page_pv; i < pv->arr_pages->len; i++ )
            g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER( i + 1 ), NULL );


    return 0;
}


static void
cb_pv_seiten_entry( GtkEntry* entry, gpointer button_datei )
{
    gtk_widget_grab_focus( (GtkWidget*) button_datei );

    return;
}


static gint
seiten_abfrage_seitenzahl( PdfViewer*pv, guint* num )
{
    gint res = 0;
    gint rc = 0;

    // Dialog erzeugen
    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Seiten einfügen:",
            GTK_WINDOW(pv->vf), GTK_DIALOG_MODAL, "Datei", 1, "Clipboard", 2,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    GtkWidget* frame = gtk_frame_new( "nach Seite:" );
    GtkWidget* entry = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame), entry );
    gtk_box_pack_start( GTK_BOX(content_area), frame, TRUE, TRUE, 0 );

    GtkWidget* button_clipboard =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), 2 );
    if ( !pv->zond->pv_clip ) gtk_widget_set_sensitive( button_clipboard, FALSE );

    GtkWidget* button_datei =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), 1 );
    g_signal_connect( entry, "activate", G_CALLBACK(cb_pv_seiten_entry),
            button_datei );

    gtk_widget_show_all( dialog );
    gtk_widget_grab_focus( entry );

    res = gtk_dialog_run( GTK_DIALOG(dialog) );
    rc = string_to_guint( gtk_entry_get_text( GTK_ENTRY(entry) ), num );
    if ( rc ) res = -1;

    gtk_widget_destroy( dialog );

    if ( res != 1 && res != 2 ) return -1;

    return res;
}


void
cb_pv_seiten_einfuegen( GtkMenuItem* item, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    //in Auszug soll nix eingefügt werden
    //ansonsten Zweideutigkeiten betr. Seite, nach der eingefügt werden soll, unvermeidlich
    if ( pv->dd->next != NULL || pv->dd->anbindung != NULL ) return;

    gint ret = 0;
    gint rc = 0;
    guint num = 0;
    DisplayedDocument* dd = NULL;
    pdf_document* doc_merge = NULL;
    gint count = 0;
    gint count_old = 0;
    fz_context* ctx = NULL;
    gchar* errmsg = NULL;

    ret = seiten_abfrage_seitenzahl( pv, &num );
    if ( ret == -1 ) return;

    count_old = pv->dd->document->pages->len;
    if ( num > count_old )
    {
        meldung( pv->vf, "Dokument hat ja gar \nnicht so viele Seiten", NULL );

        return;
    }

    //komplette Datei wird eingefügt
    if ( ret == 1 )
    {
        gchar* path_merge = NULL;

        //Datei auswählen
        path_merge = filename_oeffnen( GTK_WINDOW(pv->vf) );
        if ( !is_pdf( path_merge ) )
        {
            meldung( pv->vf, "Keine PDF-Datei", NULL );
            g_free( path_merge );

            return;
        }

        dd = document_new_displayed_document( pv->zond, path_merge, NULL, &errmsg );
        g_free( path_merge );
        if ( !dd )
        {
            meldung( pv->vf, "Fehler Datei einfügen -\n\nBei Aufruf "
                    "pdf_open_document:\n", fz_caught_message( ctx ), NULL );

            return;
        }
        ctx = dd->document->ctx;
        doc_merge = pdf_specifics( ctx, dd->document->doc );
    }
    else if ( ret == 2 )
    {
        ctx = pv->zond->ctx;
        doc_merge = pdf_keep_document( ctx, pv->zond->pv_clip ); //Clipboard
    }

    count = pdf_count_pages( ctx, doc_merge );
    if ( count == 0 )
    {
        meldung( pv->vf, "Fehler Einfügen-\n\nKeine Seiten!", NULL );
        if ( ret == 1 ) document_free_displayed_documents( pv->zond, dd );
        if ( ret == 2 ) pdf_drop_document( ctx, doc_merge );

        return;
    }

    g_mutex_lock( &pv->dd->document->mutex_doc );

    //einfügen in doc
    rc = pdf_copy_page( ctx, doc_merge, 0, count - 1,
            pdf_specifics( pv->dd->document->ctx,  pv->dd->document->doc ), num, &errmsg );

    //nur wenn Datei ausgewählt und dd erzeugt wurde:
    if ( ret == 1 ) document_free_displayed_documents( pv->zond, dd );
    if ( ret == 2 ) pdf_drop_document( ctx, doc_merge );

    if ( rc )
    {
        meldung( pv->vf, "Fehler Einfügen -\n\nBei Aufruf pdf_copy_page:\n",
                errmsg, NULL );
        g_free( errmsg );
        g_mutex_unlock( &pv->dd->document->mutex_doc );

        return;
    }

    rc = document_insert_pages( pv->dd->document, num, count, &errmsg );
    if ( rc )
    {
        meldung( pv->vf, "Fehler bei Einfügen\n\nBei Aufruf document_insert_"
                "pages:\n", errmsg, NULL );
        g_free( errmsg );
        g_mutex_unlock( &pv->dd->document->mutex_doc );

        return;
    }

    g_mutex_unlock( &pv->dd->document->mutex_doc );

    rc = viewer_foreach( pv->zond->arr_pv, pv->dd->document, num,
            seiten_cb_einfuegen, GINT_TO_POINTER(count), &errmsg );
    if ( rc )
    {
        meldung( pv->vf, "Fehler Einfügen -\n\nBei Aufruf viewer_foreach:\n",
                errmsg, "\n\nViewer wird geschlossen", NULL );
        g_free( errmsg );
        viewer_schliessen( pv );

        return;
    }

    viewer_refresh_layouts( pv->zond->arr_pv );

    return;
}


static pdf_document*
seiten_create_document( PdfViewer* pv, GArray* arr_page_pv, gchar** errmsg )
{
    fz_context* ctx = NULL;
    pdf_document* doc_dest = NULL;
    gint rc = 0;

    ctx = pv->zond->ctx;

    fz_try( ctx ) doc_dest = pdf_create_document( ctx );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_create_document", NULL )

    for ( gint i = 0; i < arr_page_pv->len; i++ )
    {
        DisplayedDocument* dd = NULL;
        gint page_doc = 0;

        gint page_pv = g_array_index( arr_page_pv, gint, i );

        dd = document_get_dd( pv, page_pv, NULL, NULL, &page_doc );

        g_mutex_lock( &dd->document->mutex_doc );
        rc = pdf_copy_page( ctx, pdf_specifics( dd->document->ctx,
                dd->document->doc ), page_doc, page_doc, doc_dest, -1, errmsg );
        g_mutex_unlock( &dd->document->mutex_doc );
        if ( rc )
        {
            pdf_drop_document( ctx, doc_dest );

            ERROR_PAO_R( "pdf_copy_page", NULL )
        }
    }

    return doc_dest;
}


static gint
seiten_set_clipboard( PdfViewer* pv, GArray* arr_page_pv, gchar** errmsg )
{
    pdf_drop_document( pv->zond->ctx, pv->zond->pv_clip );
    pv->zond->pv_clip = NULL;

    pv->zond->pv_clip = seiten_create_document( pv, arr_page_pv, errmsg );
    if ( !pv->zond->pv_clip ) ERROR_PAO( "seiten_create_document" )

    return 0;
}


void
cb_seiten_kopieren( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    GArray* arr_page_pv = seiten_markierte_thumbs( pv );
    if ( !arr_page_pv ) return;

    rc = seiten_set_clipboard( pv, arr_page_pv, &errmsg );
    if ( rc )
    {
        meldung( pv->vf, "Fehler Kopieren -\n\nBei Aufruf seiten_set_clipboard:\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    g_array_unref( arr_page_pv );

    return;
}


void
cb_seiten_ausschneiden( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //Nur aus ganzen PDFs ausschneiden
    if ( pv->dd->next != NULL || pv->dd->anbindung != NULL ) return;

    GArray* arr_page_pv = seiten_markierte_thumbs( pv );
    if ( !arr_page_pv ) return;

    rc = seiten_set_clipboard( pv, arr_page_pv, &errmsg );
    if ( rc )
    {
        meldung( pv->vf, "Fehler Kopieren -\n\nBei Aufruf seiten_set_clipboard:\n",
                errmsg, NULL );
        g_free( errmsg );
        g_array_unref( arr_page_pv );

        return;
    }

    GPtrArray* arr_document_page = seiten_get_document_pages( pv, arr_page_pv );
    g_array_unref( arr_page_pv );
    rc = seiten_loeschen( pv, arr_document_page, &errmsg );
    g_ptr_array_unref( arr_document_page );
    if ( rc )
    {
        meldung( pv->vf, "Fehler Ausschneiden -\n\nBei Aufruf seiten_loeschen:\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    return;
}
