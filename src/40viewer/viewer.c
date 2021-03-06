/*
zond (viewer.c) - Akten, Beweisstücke, Unterlagen
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
#include "../99conv/mupdf.h"
#include "../99conv/pdf.h"

#include "../20allgemein/ziele.h"

#include "document.h"
#include "annot.h"
#include "render.h"
#include "stand_alone.h"
#include "seiten.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>



static void
viewer_get_von_bis( DisplayedDocument* dd, gint* von, gint* bis )
{
    *von = 0;

    if ( dd->anbindung )
    {
        *von = dd->anbindung->von.seite;
        *bis = dd->anbindung->bis.seite;
    }
    else *bis = dd->document->pages->len - 1;

    return;
}


fz_rect
viewer_get_displayed_rect_from_dd( DisplayedDocument* dd, gint page_dd )
{
    gint von = 0;
    gint bis = 0;

    viewer_get_von_bis( dd, &von, &bis );

    fz_rect rect = ((DocumentPage*) g_ptr_array_index( dd->document->pages, page_dd + von ))->rect;

    if ( (dd->anbindung != NULL) && (page_dd == 0 || page_dd + von == bis) )
    {
        if ( dd->anbindung->von.index > rect.y0 ) rect.y0 = dd->anbindung->von.index;
        if ( dd->anbindung->bis.index < rect.y1 ) rect.y1 = dd->anbindung->bis.index;
    }

    return rect;
}


fz_rect
viewer_get_displayed_rect( PdfViewer* pv, gint page_pv )
{
    gint page_dd = 0;

    DisplayedDocument* dd = document_get_dd( pv, page_pv, NULL, &page_dd, NULL );

    return viewer_get_displayed_rect_from_dd( dd, page_dd );
}


static gdouble
viewer_abfragen_value_von_seite( PdfViewer* pv, gint page_num )
{
    gdouble value = 0.0;

    if ( !(pv->dd) || page_num < 0 ) return 0.0;

    for ( gint i = 0; i < page_num; i++ )
    {
        fz_rect rect = viewer_get_displayed_rect( pv, i );

        value += ((rect.y1 - rect.y0) *
            pv->zoom / 100) + 10;
    }

    return value;
}


void
viewer_springen_zu_pos_pdf( PdfViewer* pv, PdfPos pdf_pos, gdouble delta )
{
    gdouble value = 0.0;

    //zur zunächst anzuzeigenden Position springen
    gdouble value_seite = viewer_abfragen_value_von_seite( pv, pdf_pos.seite );

    //Länge aktueller Seite ermitteln
    fz_rect rect = viewer_get_displayed_rect( pv, pdf_pos.seite );
    gdouble page = rect.y1 - rect.y0;
    if ( pdf_pos.index <= page ) value = value_seite + (pdf_pos.index * pv->zoom / 100);
    else value = value_seite + (page * pv->zoom / 100);
#ifndef VIEWER
    if ( pv->zond->state & GDK_MOD1_MASK )
    {
        gdouble page_size = gtk_adjustment_get_page_size( pv->v_adj );
        value -= page_size;
    }
#endif // VIEWER
    gtk_adjustment_set_value( pv->v_adj, (value > delta) ? value - delta : 0 );

    return;
}


typedef struct _PV_Text_Treffer
{
    gint num;
    fz_quad quad;
} PVTextTreffer;


static void
viewer_highlight_treffer( PdfViewer* pv, gint index )
{
    PVTextTreffer pv_text_treffer = { 0 };

    if ( index >= pv->arr_text_treffer->len ) return;

    pv_text_treffer = g_array_index( pv->arr_text_treffer, PVTextTreffer, index );

    PdfPos pos_pdf = { 0 };
    pos_pdf.seite = pv_text_treffer.num;
    pos_pdf.index = pv_text_treffer.quad.ul.y;

    viewer_springen_zu_pos_pdf( pv, pos_pdf, 50.0 );

    //highlight = 1. sichtbarer Treffer
    pv->highlight[0] = pv_text_treffer.quad;

    //Sentinel setzen
    pv->highlight[1].ul.x = -1;

    //Trick, um richtige Seite anzusprechen
    pv->click_pdf_punkt.seite = pv_text_treffer.num;

    //anzeigen
    gtk_widget_queue_draw( ((ViewerPage*) g_ptr_array_index( pv->arr_pages,
            pv_text_treffer.num ))->image );

    return;
}


/*  punkt:      Koordinate im Layout (ScrolledWindow)
    pdf_punkt:  hier wird Ergebnis abgelegt
    gint:       0 wenn Punkt auf Seite liegt; -1 wenn außerhalb

    Wenn Punkt im Zwischenraum zwischen zwei Seiten oder unterhalb der letzten
    Seite liegt, wird pdf_punkt.seite die davorliegende Seite und
    pdf_punkt.punkt.y = EOP.
    Wenn punkt links oder rechts daneben liegt, ist pdf_punkt.punkt.x negativ
    oder größer als Seitenbreite
*/
static gint
viewer_abfragen_pdf_punkt( PdfViewer* pv, fz_point punkt, PdfPunkt* pdf_punkt )
{
    gint ret = 0;
    gdouble v_oben = 0.0;
    gdouble v_unten = 0.0;

    gint i = 0;

    for ( i = 0; i < pv->arr_pages->len; i++ )
    {
        fz_rect rect = viewer_get_displayed_rect( pv, i );

        pdf_punkt->delta_y = rect.y0;

        v_unten = v_oben +
                (rect.y1 - rect.y0) * pv->zoom / 100;

        if ( punkt.y >= v_oben && punkt.y <= v_unten )
        {
            pdf_punkt->seite = i;
            pdf_punkt->punkt.y = (punkt.y - v_oben) / pv->zoom * 100 + rect.y0;

            break;
        }
        else if ( punkt.y < v_unten )
        {
            pdf_punkt->seite = i - 1;
            pdf_punkt->punkt.y = EOP;

            break;
        }

        v_oben = v_unten + 10;
    }

    if ( i == pv->arr_pages->len )
    {
        ret = -1;
        pdf_punkt->seite = i - 1;
        pdf_punkt->punkt.y = EOP;
    }

    fz_rect rect = { 0 };
    gint x = 0;

    gtk_container_child_get( GTK_CONTAINER(pv->layout),
            ((ViewerPage*) g_ptr_array_index( pv->arr_pages,
            pdf_punkt->seite ))->image, "x", &x, NULL );

    if ( punkt.x < x ) ret = -1;
    rect = viewer_get_displayed_rect( pv, pdf_punkt->seite );
    if ( punkt.x > (((rect.x1 - rect.x0) * pv->zoom / 100) + x) ) ret = -1;

    pdf_punkt->punkt.x = (punkt.x - x) / pv->zoom * 100;

    return ret;
}


static gint
viewer_suchen_naechstes_vorkommen( PdfViewer* pv )
{
    PdfPunkt pdf_punkt = { 0 };
    fz_point punkt = { 0 };
    punkt.y = gtk_adjustment_get_value( pv->v_adj );

    viewer_abfragen_pdf_punkt( pv, punkt, &pdf_punkt );

    PVTextTreffer pv_text_treffer = { 0 };

    gint i = 0;
    for ( i = 0; i < pv->arr_text_treffer->len; i++ )
    {
        pv_text_treffer = g_array_index( pv->arr_text_treffer, PVTextTreffer, i );
        if ( (pv_text_treffer.num == pdf_punkt.seite &&
                pv_text_treffer.quad.ul.y > pdf_punkt.punkt.y) ||
                (pv_text_treffer.num > pdf_punkt.seite) ) break;
    }

    pv->highlight_index = i;

    return i;
}


void
viewer_abfragen_sichtbare_seiten( PdfViewer* pv, gint* von, gint* bis )
{
    gdouble value = gtk_adjustment_get_value( pv->v_adj );
    gdouble size = gtk_adjustment_get_page_size( pv->v_adj );

    *von = -1;
    *bis = -1;

    gdouble v_oben = 0.0;
    gdouble v_unten = -10.0;

    while ( ((value + size) > v_oben) && ((*bis) < ((gint) pv->arr_pages->len - 1)) )
    {
        (*bis)++;
        fz_rect rect = viewer_get_displayed_rect( pv, *bis );
        v_oben += ((rect.y1 -rect.y0) * pv->zoom / 100) + 10;

        if ( value > v_unten ) (*von)++;
        v_unten += ((rect.y1 - rect.y0) * pv->zoom / 100) + 10;
    }

    return;
}


gboolean
viewer_page_ist_sichtbar( PdfViewer* pv, gint page )
{
    gint von = 0;
    gint bis = 0;

    viewer_abfragen_sichtbare_seiten( pv, &von, &bis );

    if ( (page < von) || (page > bis) ) return FALSE;

    return TRUE;
}


static void
viewer_einrichten_layout( PdfViewer* pv )
{
    if ( pv->arr_pages->len == 0 ) return;

    gdouble h_sum = 0;
    gdouble w_max = 0;
    gdouble h = 0;
    gdouble w = 0;

    for ( gint u = 0; u < pv->arr_pages->len; u++ )
    {
        fz_rect rect = viewer_get_displayed_rect( pv, u );
        w = (rect.x1 - rect.x0) * pv->zoom / 100;

        if ( w > w_max ) w_max = w;
    }

    for ( gint i = 0; i < pv->arr_pages->len; i++ )
    {
        fz_rect rect = viewer_get_displayed_rect( pv, i );
        h = ((rect.y1 - rect.y0) * pv->zoom / 100);
        w = ((rect.x1 - rect.x0) * pv->zoom / 100);

        gtk_layout_move( GTK_LAYOUT(pv->layout), GTK_WIDGET(((ViewerPage*)
                (g_ptr_array_index( pv->arr_pages, i )))->image),
                (int) ((w_max - w) / 2 + 0.5),
                (int) (h_sum + 0.5) );

        h_sum += (h + 10);
    }

    h_sum -= 10;

    gint width = (gint) (w_max + 0.5);
    gint height = (gint) (h_sum + 0.5);

    gtk_layout_set_size( GTK_LAYOUT(pv->layout), width, height );

    gtk_adjustment_set_value( pv->h_adj, (gdouble) ((width - 950) / 2) );

    //label mit Gesamtseitenzahl erzeugen
    gchar* text = g_strdup_printf( "/ %i ", pv->arr_pages->len );
    gtk_label_set_text( GTK_LABEL(pv->label_anzahl), text );
    g_free( text );

    gtk_widget_set_size_request( pv->layout, width, height );

    return;
}


void
viewer_refresh_layouts( GPtrArray* arr_pv )
{
    for ( gint i = 0; i < arr_pv->len; i++ )
    {
        PdfViewer* pv = g_ptr_array_index( arr_pv, i );

        if ( g_object_get_data( G_OBJECT(pv->layout), "dirty" ) )
        {
            viewer_einrichten_layout( pv );
            g_object_set_data( G_OBJECT(pv->layout), "dirty", NULL );
        }
    }

    return;
}


void
viewer_close_thread_pools( PdfViewer* pv )
{
    if ( !pv->thread_pool_page ) return;

    g_thread_pool_free( pv->thread_pool_page, TRUE, TRUE );
    pv->thread_pool_page = NULL;

    return;
}


void
viewer_schliessen( PdfViewer* pv )
{
    DisplayedDocument* dd = pv->dd;

    viewer_close_thread_pools( pv );

    do
    {
        if ( dd->document->dirty && dd->document->ref_count == 1 )
        {
            gchar* text_frage = g_strconcat( "PDF-Datei ", dd->document->path,
                    " geändert", NULL );
            gint rc = abfrage_frage( pv->vf, text_frage, "Speichern?", NULL );
            g_free( text_frage );
            if ( rc == GTK_RESPONSE_YES )
            {
                gchar* errmsg = NULL;

                gint rc = mupdf_save_document( dd->document, &errmsg );
                dd->document->doc = NULL;
                if ( rc )
                {
                    meldung( pv->vf, "Dokument ", dd->document->path,
                            " konnte nicht gespeichert werden:\n", errmsg, NULL );

                    g_free( errmsg );
                }
            }
        }
    } while ( (dd = dd->next) );

    g_ptr_array_unref( pv->arr_pages );
    gtk_widget_destroy( pv->tree_thumb );

    document_free_displayed_documents( pv->zond, pv->dd );

    //pv aus Liste der geöffneten pvs entfernen
    g_ptr_array_remove_fast( pv->zond->arr_pv, pv );

    g_array_free( pv->arr_text_treffer, TRUE );

    gtk_widget_destroy( pv->vf );

    g_free( pv );

    return;
}


static gboolean
cb_viewer_delete_event( GtkWidget* window, GdkEvent* event, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    viewer_schliessen( pv );

    return TRUE;
}


static void
cb_thumb_sel_changed( GtkTreeSelection* sel, gpointer data )
{
    gboolean active = FALSE;

    PdfViewer* pv = (PdfViewer*) data;

    active = (gtk_tree_selection_count_selected_rows( sel ) == 0 ) ? FALSE : TRUE;

    gtk_widget_set_sensitive( pv->item_kopieren, active );
    gtk_widget_set_sensitive( pv->item_ausschneiden, active );

    return;
}


static void
cb_thumb_activated(GtkTreeView* tv, GtkTreePath* path, GtkTreeViewColumn* column,
        gpointer data )
{
    gint* indices = NULL;
    PdfPos pos = { 0 };

    PdfViewer* pv = (PdfViewer*) data;

    indices = gtk_tree_path_get_indices( path );

    pos.seite = indices[0];
    pos.index = 0;
    viewer_springen_zu_pos_pdf( pv, pos, 0.0 );

    return;
}


static void
cb_viewer_vadj_thumb( GtkAdjustment* vadj_thumb, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    render_sichtbare_thumbs( pv );

    return;
}


static void
cb_viewer_vadjustment_value_changed( GtkAdjustment* vadjustment, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    if ( pv->arr_pages->len == 0 ) return;

    render_sichtbare_seiten( pv );

    return;
}


#ifndef VIEWER
static void
cb_viewer_loeschen_anbindung_button_clicked( GtkButton* button, gpointer data )
{
   PdfViewer* pv = (PdfViewer*) data;

    //anbindung.von "löschen"
    pv->anbindung.von.index = -1;

    //Anzeige Beginn rückgängig machen
    gtk_widget_set_tooltip_text( pv->button_anbindung, "Anbindung Anfang löschen" );
    gtk_widget_set_sensitive( pv->button_anbindung, FALSE );

    return;
}
#endif // VIEWER


static gboolean
cb_viewer_draw_image( GtkWidget* image, cairo_t *cr, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    if ( image != VIEWER_PAGE(pv->click_pdf_punkt.seite)->image ) return FALSE;

    fz_matrix transform = fz_translate( 0.0, -pv->click_pdf_punkt.delta_y );
    transform = fz_post_scale( transform, pv->zoom / 100, pv->zoom / 100 );

    //wenn annot angeclickt wurde
    if ( pv->clicked_annot )
    {
        PVQuad* pv_quad = pv->clicked_annot->first;

        do
        {
            fz_quad quad = fz_transform_quad( pv_quad->quad, transform );
            cairo_move_to( cr, quad.ul.x, quad.ul.y );
            cairo_line_to( cr, quad.ur.x, quad.ur.y );
            cairo_line_to( cr, quad.lr.x, quad.lr.y );
            cairo_line_to( cr, quad.ll.x, quad.ll.y );
            cairo_line_to( cr, quad.ul.x, quad.ul.y );
            cairo_set_source_rgb(cr, 0, 1, 0 );
            cairo_stroke( cr );

            pv_quad = pv_quad->next;
        }
        while ( pv_quad );
    }
    else //ansonsten etwaige highlights zeichnen
    {
        gint i = 0;
        while ( pv->highlight[i].ul.x >= 0 )
        {
            fz_rect rect = fz_transform_rect( fz_rect_from_quad( pv->highlight[i] ), transform );

            float x = rect.x0;
            float y = rect.y0;
            float width = rect.x1 - x;
            float heigth = rect.y1 - y;

            cairo_rectangle( cr, x, y, width, heigth );
            cairo_set_source_rgba (cr, 0, .1, .8, 0.5);
            cairo_fill(cr);

            i++;
        }
    }

    return FALSE;
}


static void
viewer_free_viewer_page( ViewerPage* viewer_page )
{
    gtk_widget_destroy( viewer_page->image );

    g_free( viewer_page );

    return;
}


void
viewer_insert_thumb( PdfViewer* pv, gint page_pv, fz_rect rect )
{
    gint width = 0;
    gint height = 0;
    GdkPixbuf* pix = NULL;

    //tree mit thumbnails
    width = (gint) (rect.x1 - rect.x0) *.15;
    height = (gint) (rect.y1 - rect.y0) *.15;
    pix = gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, width, height );
    gdk_pixbuf_fill( pix, 0xffffffff );

    gtk_list_store_insert_with_values(
            GTK_LIST_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) )), NULL, page_pv,
            0, pix, 1, FALSE, -1 );
    g_object_unref( pix );

    return;
}


ViewerPage*
viewer_new_viewer_page( PdfViewer* pv )
{
    ViewerPage* viewer_page = g_malloc0( sizeof( ViewerPage ) );

    viewer_page->image = gtk_image_new( );
    gtk_widget_show( viewer_page->image );
    g_signal_connect_after( viewer_page->image, "draw", G_CALLBACK(cb_viewer_draw_image), pv );
    gtk_layout_put( GTK_LAYOUT(pv->layout), GTK_WIDGET(viewer_page->image), 0, 0 );

    return viewer_page;
}


static void
viewer_create_layout( PdfViewer* pv )
{
    DisplayedDocument* dd = pv->dd;

    do
    {
        gint seite_dd = 0;
        gint von = 0;
        gint bis = 0;

        if ( dd->anbindung )
        {
            von = dd->anbindung->von.seite;
            bis = dd->anbindung->bis.seite;
        }
        else bis = dd->document->pages->len - 1;

        while ( seite_dd  + von <= bis )
        {
            //ViewerPage erstellen und einfügen
            ViewerPage* viewer_page = viewer_new_viewer_page( pv );
            g_ptr_array_add( pv->arr_pages, viewer_page );

            viewer_insert_thumb( pv, -1,
                    viewer_get_displayed_rect_from_dd( dd, seite_dd ) );

            seite_dd++;
        }

    } while ( (dd = dd->next) );

    return;
}


static gboolean
cb_viewer_auswahlwerkzeug( GtkButton* button, GdkEvent* event, gpointer data )
{
    gint button_ID = 0;
    GtkToggleButton* button_other_1 = NULL;
    GtkToggleButton* button_other_2 = NULL;
    GtkToggleButton* button_other_3 = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    button_ID = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(button), "ID" ));
    button_other_1 = g_object_get_data( G_OBJECT(button), "button-other-1" );
    button_other_2 = g_object_get_data( G_OBJECT(button), "button-other-2" );
    button_other_3 = g_object_get_data( G_OBJECT(button), "button-other-3" );

    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(button), TRUE );
    gtk_toggle_button_set_active( button_other_1, FALSE );
    gtk_toggle_button_set_active( button_other_2, FALSE );
    gtk_toggle_button_set_active( button_other_3, FALSE );

    pv->state = button_ID;
    gtk_toggle_button_set_active( button_other_2, FALSE );

    return TRUE;
}


static void
viewer_set_clean( GPtrArray* arr_pv )
{
    for ( gint i = 0; i < arr_pv->len; i++ )
    {
        DisplayedDocument* dd = NULL;
        gboolean clean = TRUE;

        PdfViewer* pv_vergleich = g_ptr_array_index( arr_pv, i );

        dd = pv_vergleich->dd;

        do
        {
            if ( dd->document->dirty ) clean = FALSE;
        } while ( (dd = dd->next) );

        if ( clean == TRUE ) gtk_widget_set_sensitive( pv_vergleich->button_speichern, FALSE );
    }

    return;
}


void
viewer_init_thread_pools( PdfViewer* pv )
{
    pv->thread_pool_page = g_thread_pool_new( (GFunc) render_page_thread, pv, 1, FALSE, NULL );
//    pv->thread_pool_thumb = g_thread_pool_new( (GFunc) render_thumb_thread, pv, 1, FALSE, NULL );
    for ( gint i = 0; i < pv->arr_pages->len; i++ )
    {
        g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER(i + 1), NULL );
    }

    return;
}


static gint
viewer_dd_speichern( DisplayedDocument* dd, gchar** errmsg )
{
    gint rc = 0;

    rc = mupdf_save_document( dd->document, errmsg );
    if ( rc ) ERROR_PAO( "mupdf_save_document" )

    dd->document->dirty = FALSE;

    rc = mupdf_open_document( dd->document, errmsg );
    if ( rc ) ERROR_PAO( "mupdf_open_document" )

    return 0;

}


static void
cb_pv_speichern( GtkButton* button, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    DisplayedDocument* dd = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    dd = pv->dd;

    do
    {
        if ( !dd->document->dirty ) continue;

        g_mutex_lock( &dd->document->mutex_doc );

        rc = viewer_dd_speichern( dd, &errmsg );
        if ( rc )
        {
            meldung( pv->vf, "Dokument ", dd->document->path," konnte nicht "
                    "gespeichert werden:\n\nBei Aufruf viewer_dd_speichern:\n",
                    errmsg, "\n\nViewer wird geschlossen", NULL );
            g_free( errmsg );
            g_mutex_unlock( &dd->document->mutex_doc );
            viewer_schliessen( pv );
        }

        g_mutex_unlock( &dd->document->mutex_doc );
    }
    while ( (dd = dd->next) );

    viewer_set_clean( pv->zond->arr_pv );

    return;
}


static void
cb_tree_thumb( GtkToggleButton* button, gpointer data )
{
    GtkWidget* swindow = (GtkWidget*) data;

    if ( gtk_toggle_button_get_active( button ) ) gtk_widget_show( swindow );
    else
    {
        gtk_widget_hide( swindow );
        GtkTreeSelection* sel = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(gtk_bin_get_child( GTK_BIN(swindow) )) );
        gtk_tree_selection_unselect_all( sel );
    }

    return;
}


static void
cb_viewer_vor_button_clicked( GtkButton* button, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    if ( pv->highlight_index < (pv->arr_text_treffer->len - 1) ) pv->highlight_index++;
    else pv->highlight_index = 0;

    viewer_highlight_treffer( pv, pv->highlight_index );

    return;
}


static void
cb_viewer_zurueck_button_clicked( GtkButton* butxton, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    if ( pv->highlight_index > 0 ) pv->highlight_index--;
    else pv->highlight_index = pv->arr_text_treffer->len - 1;

    viewer_highlight_treffer( pv, pv->highlight_index );

    return;
}


static void
cb_viewer_text_search_entry_buffer_changed( gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    g_array_remove_range( pv->arr_text_treffer, 0, pv->arr_text_treffer->len );

    return;
}


static gint
viewer_durchsuchen_angezeigtes_doc( PdfViewer* pv, const gchar* search_text,
        gchar** errmsg )
{
    gint rc = 0;
    DisplayedDocument* dd = NULL;

    g_array_remove_range( pv->arr_text_treffer,
            0, pv->arr_text_treffer->len );

    dd = pv->dd;
    do
    {
        gint von = 0;
        gint bis = 0;
        fz_context* ctx = dd->document->ctx;

        g_mutex_lock( &dd->document->mutex_doc );

        viewer_get_von_bis( dd, &von, &bis );
        for ( gint i = von; i <= bis; i++ )
        {
            gint anzahl = 0;
            fz_quad quads[100] = { 0 };
            fz_stext_page* stext_page = NULL;

            stext_page = DOCUMENT_PAGE(i)->stext_page;

            if ( stext_page->first_block == NULL )
            {
                fz_display_list* dl = NULL;

                dl = DOCUMENT_PAGE(i)->display_list;

                if ( !fz_display_list_is_empty( ctx, dl ) )
                {
                    rc = render_display_list_to_stext_page( ctx, DOCUMENT_PAGE(i), errmsg );
                    if ( rc )
                    {
                        g_mutex_unlock( &dd->document->mutex_doc );
                        ERROR_PAO( "render_display_list_to_stext_page" )
                    }
                }
                else //wenn display_list noch nicht erzeugt, dann direkt aus page erzeugen
                {
                    rc = pdf_render_stext_page_direct( DOCUMENT_PAGE(i), errmsg );
                    if ( rc )
                    {
                        g_mutex_unlock( &dd->document->mutex_doc );
                        ERROR_PAO( "pdf_render_stext_page_direct" )
                    }
                }
            }

            anzahl = fz_search_stext_page( ctx,
                    DOCUMENT_PAGE(i)->stext_page, search_text, quads, 99 );

            for ( gint u = 0; u < anzahl; u++ )
            {
                PVTextTreffer pv_text_treffer = { 0 };

                fz_rect displayed_rect = viewer_get_displayed_rect_from_dd( dd, i - von );
                fz_rect text_rect = fz_rect_from_quad( quads[u] );
                fz_rect cropped_rect = fz_intersect_rect( displayed_rect, text_rect );

                if ( !fz_is_empty_rect( cropped_rect ) )
                {
                    cropped_rect = fz_translate_rect( cropped_rect, -displayed_rect.x0, -displayed_rect.y0 );

                    pv_text_treffer.num = document_get_page_pv( pv, dd, i - von );
                    pv_text_treffer.quad = fz_quad_from_rect( cropped_rect );
                    g_array_append_val( pv->arr_text_treffer, pv_text_treffer );
                }
            }
        }

        g_mutex_unlock( &dd->document->mutex_doc );

    } while ( (dd = dd->next) );

    return 0;
}


static void
cb_viewer_text_search_entry_activate( GtkEntry* entry, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    gint rc = 0;
    gchar* errmsg = NULL;

    const gchar* text_entry = gtk_entry_get_text( entry );

    if ( !g_strcmp0( text_entry, "" ) )
    {
        pv->highlight[0].ul.x = -1;
        gtk_widget_grab_focus( pv->layout);

        return;
    }

    rc = viewer_durchsuchen_angezeigtes_doc( pv, text_entry, &errmsg );
    if ( rc )
    {
        meldung( pv->vf, "Fehler - Textsuche:\n\nBei Aufruf viewer_durchsuchen_"
                "angezeigtes_doc", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    if ( pv->arr_text_treffer->len == 0 )
    {
        pv->highlight[0].ul.x = -1;
        meldung( pv->vf, "Kein Treffer", NULL );

        return;
    }

    gint index = viewer_suchen_naechstes_vorkommen( pv );

    viewer_highlight_treffer( pv, index );

    gtk_widget_grab_focus( pv->button_nachher );

    return;
}


static void
cb_viewer_spinbutton_value_changed( GtkSpinButton* spin_button, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    pv->zoom = gtk_spin_button_get_value( spin_button );

    for ( gint i = 0; i < pv->arr_pages->len; i++ )
    {
        if ( gtk_image_get_storage_type( GTK_IMAGE(VIEWER_PAGE(i)->image) ) ==
                GTK_IMAGE_PIXBUF ) gtk_image_clear( GTK_IMAGE(VIEWER_PAGE(i)->image) );
    }

    //Alte Position merken
    gdouble v_pos = gtk_adjustment_get_value( pv->v_adj ) /
            gtk_adjustment_get_upper( pv->v_adj );
    gdouble h_pos =  gtk_adjustment_get_value( pv->h_adj )/
            gtk_adjustment_get_upper( pv->h_adj );

    viewer_einrichten_layout( pv );

    gtk_adjustment_set_value( pv->v_adj, gtk_adjustment_get_upper( pv->v_adj ) *
            v_pos );
    gtk_adjustment_set_value( pv->h_adj, gtk_adjustment_get_upper( pv->h_adj ) *
            h_pos );

    gtk_widget_grab_focus( pv->layout );

    for ( gint i = 0; i < pv->arr_pages->len; i++ )
    {
        g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER(i + 1), NULL );
    }

    render_sichtbare_seiten( pv );

    return;
}


static void
cb_viewer_page_entry_activated( GtkEntry* entry, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    const gchar* text_entry = gtk_entry_get_text( entry );

    guint page_num = 0;
    gint rc = 0;
    gint erste = 0;
    gint letzte = 0;

    rc = string_to_guint( text_entry, &page_num );
    if ( rc || (page_num < 1) || (page_num > pv->arr_pages->len) )
    {
        viewer_abfragen_sichtbare_seiten( pv, &erste, &letzte );
        gchar* text = NULL;
        text = g_strdup_printf( "%i-%i", erste + 1, letzte + 1 );
        gtk_entry_set_text( entry, (const gchar*) text );
        g_free( text );
    }
    else
    {
        gdouble value = viewer_abfragen_value_von_seite( pv, page_num - 1 );
        gtk_adjustment_set_value( pv->v_adj, value );
    }

    gtk_widget_grab_focus( pv->layout );

    return;
}


static void
cb_pv_copy_text( GtkMenuItem* item, gpointer data )
{
    fz_context* ctx = NULL;
    gchar* text = NULL;
    gint i = 0;

    PdfViewer* pv = (PdfViewer*) data;
    DocumentPage* document_page = document_get_document_page_from_pv( pv,
            pv->click_pdf_punkt.seite );

    //highlights zählen
    while ( pv->highlight[i].ul.x != -1 ) i++;

    if ( i == 0 ) return;

    ctx = fz_clone_context( document_page->document->ctx );
    text = fz_copy_selection( ctx, document_page->stext_page,
            pv->click_pdf_punkt.punkt, pv->highlight[i].ll, FALSE );

    GtkClipboard* clipboard = gtk_clipboard_get( GDK_SELECTION_CLIPBOARD );
    gtk_clipboard_set_text( clipboard, text, -1 );

    fz_free( ctx, text );

    fz_drop_context( ctx );

    return;
}



static gint
viewer_on_text( PdfViewer* pv, PdfPunkt pdf_punkt )
{
    DocumentPage* document_page =
            document_get_document_page_from_pv( pv, pdf_punkt.seite );

    if ( !document_page->stext_page->first_block ) return FALSE;

	for ( fz_stext_block* block = document_page->stext_page->first_block; block;
            block = block->next)
	{
		if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

		for ( fz_stext_line* line = block->u.t.first_line; line; line = line->next)
		{
			fz_rect box = line->bbox;
			if ( pdf_punkt.punkt.x >= box.x0 && pdf_punkt.punkt.x <= box.x1 &&
                    pdf_punkt.punkt.y >= box.y0 && pdf_punkt.punkt.y <= box.y1 )
            {
                gboolean quer = FALSE;

                fz_context* ctx = fz_clone_context( document_page->document->ctx );
                if ( pdf_get_rotate( ctx, pdf_page_from_fz_page( ctx,
                        document_page->page )->obj ) == 90 ||
                        pdf_get_rotate( ctx, pdf_page_from_fz_page( ctx,
                        document_page->page )->obj ) == 180 ) quer = TRUE;
                fz_drop_context( ctx );

                if ( line->wmode == 0 && !quer ) return 1;
                else if ( line->wmode == 1 && quer ) return 1;
                else if ( line->wmode == 0 && quer ) return 2;
                if ( line->wmode == 1 && !quer ) return 2;
            }
		}
	}

	return 0;
}


static gboolean
inside_quad( fz_quad quad, fz_point punkt )
{
    fz_rect rect = fz_rect_from_quad( quad );

/*
    if ( (punkt.x < quad.ul.x) || (punkt.x > quad.ur.x) ) return FALSE;
    if ( (punkt.y < quad.ul.y) || (punkt.y > quad.ll.y) ) return FALSE;

    //nicht oberhalb Gerade ul-ur
    if ( punkt.y > (quad.ul.y + ((quad.ur.y - quad.ul.y) /
            (quad.ur.x - quad.ul.x) * (punkt.x - quad.ul.x))) ) return FALSE;

    //nicht unterhalb ll-lr
    if ( punkt.y < (quad.ll.y + ((quad.lr.y - quad.ll.y) /
            (quad.lr.x - quad.ll.x) * (punkt.x - quad.ll.x))) ) return FALSE;

    //nicht links von ul-ll
    if ( punkt.x < (quad.ul.x + ((quad.ll.x - quad.ul.x) /
            (quad.ll.y - quad.ul.y) * (punkt.y - quad.ul.y))) ) return FALSE;

    //nicht rechts von ur-lr
    if ( punkt.x > (quad.ur.x + ((quad.lr.x - quad.ur.x) /
            (quad.lr.y - quad.ur.y) * (punkt.y - quad.ur.y))) ) return FALSE;
*/
    return fz_is_point_inside_rect( punkt, rect );
}


static PVAnnot*
viewer_on_annot( PdfViewer* pv, PdfPunkt pdf_punkt )
{
    DocumentPage* document_page = NULL;

    document_page = document_get_document_page_from_pv( pv, pdf_punkt.seite );
    if ( !(document_page->pv_annot_page) ) return NULL;

    PVAnnot* pv_annot = NULL;
    PVQuad* pv_quad = NULL;

    for ( pv_annot = document_page->pv_annot_page->last; pv_annot;
            pv_annot = pv_annot->prev )
    {
        for ( pv_quad = pv_annot->first; pv_quad; pv_quad = pv_quad->next )
        {
            if ( inside_quad( pv_quad->quad, pdf_punkt.punkt ) ) return pv_annot;
        }
    }

    return NULL;
}


static void
viewer_set_cursor( PdfViewer* pv, gint rc, PdfPunkt pdf_punkt )
{
    PVAnnot* pv_annot = NULL;

    if ( rc ) gdk_window_set_cursor( pv->gdk_window, pv->cursor_default );
    else if ( (pv_annot = viewer_on_annot( pv, pdf_punkt )) )
    {
        if ( (pv->state == 1 && pv_annot->type == PDF_ANNOT_HIGHLIGHT ) ||
                (pv->state == 2 && pv_annot->type == PDF_ANNOT_UNDERLINE) )
        {
            gdk_window_set_cursor( pv->gdk_window, pv->cursor_annot );

            return;
        }
    }

    if ( viewer_on_text( pv, pdf_punkt ) == 1 )
            gdk_window_set_cursor( pv->gdk_window, pv->cursor_text );
    else if ( viewer_on_text( pv, pdf_punkt ) == 2 )
            gdk_window_set_cursor( pv->gdk_window, pv->cursor_vtext );
    else gdk_window_set_cursor( pv->gdk_window, pv->cursor_default );

    return;
}


gint
viewer_get_visible_thumbs( PdfViewer* pv, gint* start, gint* end )
{
    GtkTreePath* path_start = NULL;
    GtkTreePath* path_end = NULL;
    gint* index_start = NULL;
    gint* index_end = NULL;

    if ( pv->arr_pages->len == 0 ) return 1;

    if ( !gtk_tree_view_get_visible_range( GTK_TREE_VIEW(pv->tree_thumb),
            &path_start, &path_end ) ) return 1;

    index_start = gtk_tree_path_get_indices( path_start );
    index_end = gtk_tree_path_get_indices( path_end );

    *start = index_start[0];
    *end = index_end[0];

    gtk_tree_path_free( path_start );
    gtk_tree_path_free( path_end );

    return 0;
}


gboolean
viewer_thumb_ist_sichtbar( PdfViewer* pv, gint page_pv )
{
    gint rc = 0;
    gint start = 0;
    gint end = 0;

    rc = viewer_get_visible_thumbs( pv, &start, &end );

    if ( rc == 0 && page_pv >= start && page_pv <= end ) return TRUE;

    return FALSE;
}


gint
viewer_get_iter_thumb( PdfViewer* pv, gint page_pv, GtkTreeIter* iter,
        gchar** errmsg )
{
    GtkTreeModel* model = NULL;
    GtkTreePath* path = NULL;
    gboolean exists = FALSE;
    gboolean rendered = FALSE;

    path = gtk_tree_path_new_from_indices( page_pv, -1 );
    model = gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) );
    exists = gtk_tree_model_get_iter( model, iter, path );
    gtk_tree_path_free( path );
    if ( !exists ) ERROR_PAO( "gtk_tree_model_get_iter:\nSeite existiert nicht" );

    gtk_tree_model_get( model, iter, 1, &rendered, -1 );
    if ( rendered ) return 1;

    return 0;
}


static gint
viewer_cb_change_annot( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter;

    ViewerPage* viewer_page = g_ptr_array_index( pv->arr_pages, page_pv );

    gtk_image_clear( GTK_IMAGE(viewer_page->image) );
    rc = viewer_get_iter_thumb( pv, page_pv, &iter, errmsg );
    if ( rc == -1 ) ERROR_PAO( "viewer_get_iter_thumb" )
    gtk_list_store_set( GTK_LIST_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) )), &iter, 1, FALSE, -1 );

    g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER( page_pv + 1), NULL );

    if ( viewer_page_ist_sichtbar( pv, page_pv ) )
            g_thread_pool_move_to_front( pv->thread_pool_page, GINT_TO_POINTER(page_pv + 1) );

    return 0;
}


gint
viewer_reload_document_page( PdfViewer* pv, Document* document, gint page_doc,
        gint flags, gchar** errmsg )
{
    fz_context* ctx = document->ctx;
    DocumentPage* document_page = g_ptr_array_index( document->pages, page_doc );

    //Seite clearen
    fz_drop_page( ctx, document_page->page );
    document_page->page = NULL;

    fz_try( ctx ) document_page->page = fz_load_page( ctx, document->doc, page_doc );
    fz_catch( ctx ) ERROR_MUPDF( "fz_load_page" )

    document_page->rect = fz_bound_page( ctx, document_page->page );

    fz_drop_display_list( ctx, document_page->display_list );
    document_page->display_list = NULL;
    fz_try( ctx ) document_page->display_list = fz_new_display_list( ctx,
            document_page->rect );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_display_list" );

    if ( flags & 1 )
    {
        fz_drop_stext_page( ctx, document_page->stext_page );
        document_page->stext_page = NULL;
        fz_try( ctx ) document_page->stext_page = fz_new_stext_page( ctx,
                document_page->rect );
        fz_catch( ctx ) ERROR_MUPDF( "fz_new_stext_page" )
    }
    if ( flags & 2 )
    {
        annot_free_pv_annot_page( document_page->pv_annot_page );
        annot_load_pv_annot_page( document_page );
    }

    return 0;
}


gint
viewer_foreach( GPtrArray* arr_pv, Document* document, gint page_doc,
        gint (*cb_foreach_pv) (PdfViewer*, gint, gpointer, gchar**),
        gpointer data, gchar** errmsg )
{
    document->dirty = TRUE;

    for ( gint i = 0; i < arr_pv->len; i++ )
    {
        gboolean dirty = FALSE;

        PdfViewer* pv_vergleich = g_ptr_array_index( arr_pv, i );
        DisplayedDocument* dd_vergleich = pv_vergleich->dd;

        do
        {
            if ( document == dd_vergleich->document )
            {
                gint rc = 0;
                gint page_pv_vergleich = 0;

                page_pv_vergleich = document_get_page_pv( pv_vergleich,
                        dd_vergleich, document_get_page_dd( dd_vergleich, page_doc ) );
                if ( page_pv_vergleich == -1 ) continue;
                else dirty = TRUE;

                rc = cb_foreach_pv( pv_vergleich, page_pv_vergleich, data, errmsg );
                if ( rc ) ERROR_PAO( "cb_foreach_pv" )
            }
        } while ( (dd_vergleich = dd_vergleich->next) );

        if ( dirty ) gtk_widget_set_sensitive( pv_vergleich->button_speichern, TRUE );
    }

    return 0;
}


static gboolean
cb_viewer_swindow_key_press( GtkWidget* swindow, GdkEvent* event, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    if ( !(pv->clicked_annot) ) return FALSE;

    if ( event->key.keyval == GDK_KEY_Delete )
    {
        gchar* errmsg = NULL;
        gint rc = 0;
        DocumentPage* document_page = NULL;
        gint page_doc = 0;

        DisplayedDocument* dd = document_get_dd( pv, pv->click_pdf_punkt.seite,
                &document_page, NULL, &page_doc );

        g_mutex_lock( &dd->document->mutex_doc );
        rc = annot_delete( dd, page_doc, pv->clicked_annot,
                &errmsg );
        if ( rc )
        {
            meldung( pv->vf, "Fehler -Annotation löschen\n\n"
                    "Bei Aufruf annot_delete", errmsg, NULL );
            g_free( errmsg );
        }
        else
        {
            rc = viewer_reload_document_page( pv, dd->document, page_doc, 2, &errmsg );
            if ( rc )
            {
                meldung( pv->vf, "Fehler - Annotation löschen\n\n"
                        "Bei Aufruf viewer_reload_fz_page:\n", errmsg, NULL );
                g_free( errmsg );
            }

            rc = viewer_foreach( pv->zond->arr_pv, dd->document, page_doc,
                    viewer_cb_change_annot, NULL, &errmsg );
            if ( rc )
            {
                meldung( pv->vf, "Fehler -\n\n",
                        "Bei Aufruf viewer_refresh_changed_page:\n", errmsg, NULL );
                g_free( errmsg );

                g_mutex_unlock( &dd->document->mutex_doc );

                return -1;
            }
        }

        g_mutex_unlock( &dd->document->mutex_doc );

        pv->clicked_annot = NULL;
    }

    return FALSE;
}


static gboolean
cb_viewer_motion_notify( GtkWidget* window, GdkEvent* event, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    //Signal wird nur durchgelassen, wenn layout keines erhält,
    //also wenn Maus außerhalb layout
    //Ausnahme: Button wurde in layout gedrückt und wird gehalten
    //Vielleicht Fehler in GDK? Oder extra?
    gdk_window_set_cursor( pv->gdk_window, pv->cursor_default );

    return FALSE;
}


static gboolean
cb_viewer_layout_motion_notify( GtkWidget* layout, GdkEvent* event, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    if ( !(pv->dd) ) return TRUE;

    PdfPunkt pdf_punkt = { 0 };
    gint rc = viewer_abfragen_pdf_punkt( pv,
            fz_make_point( event->motion.x, event->motion.y ), &pdf_punkt );

    if ( event->motion.state == GDK_BUTTON1_MASK )
    {
        if ( pv->click_on_text && !pv->clicked_annot )
        {
            if ( pdf_punkt.seite == pv->click_pdf_punkt.seite )
            {
                DocumentPage* document_page =
                        document_get_document_page_from_pv( pv, pdf_punkt.seite );

                fz_context* ctx = fz_clone_context( document_page->document->ctx );
                gint n = fz_highlight_selection( ctx, document_page->stext_page,
                        pv->click_pdf_punkt.punkt, pdf_punkt.punkt, pv->highlight, 999 );
                fz_drop_context( ctx );

                pv->highlight[n].ul.x = -1;
                pv->highlight[n].ll = pdf_punkt.punkt;

                gtk_widget_queue_draw( ((ViewerPage*)
                        g_ptr_array_index( pv->arr_pages, pdf_punkt.seite ))->image );
            }
        }
        else if ( !pv->clicked_annot )
        {
            gdouble y = gtk_adjustment_get_value( pv->v_adj );
            gdouble x = gtk_adjustment_get_value(pv->h_adj );
            gtk_adjustment_set_value( pv->v_adj, y + pv->y - event->motion.y_root );
            gtk_adjustment_set_value( pv->h_adj, x+ pv->x - event->motion.x_root );
            pv->y = event->motion.y_root;
            pv->x = event->motion.x_root;
        }

    }
    else viewer_set_cursor( pv, rc, pdf_punkt ); //Kein Knopf gedrückt

    return TRUE;
}


static gboolean
cb_viewer_layout_release_button( GtkWidget* layout, GdkEvent* event, gpointer data )
{
    gchar* errmsg = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    if ( !(pv->dd) ) return TRUE;

    PdfPunkt pdf_punkt = { 0 };
    gint rc = viewer_abfragen_pdf_punkt( pv,
            fz_make_point( event->motion.x, event->motion.y ), &pdf_punkt );

    pv->click_on_text = FALSE;

    viewer_set_cursor( pv, rc, pdf_punkt );

    if ( pv->highlight[0].ul.x != -1 ) //es sind Markierungen gespeichert
    {
        if ( pv->state == 1 || pv->state == 2 )
        {
            DocumentPage* document_page = NULL;
            gint page_doc = 0;

            DisplayedDocument* dd = document_get_dd( pv, pv->click_pdf_punkt.seite,
                    &document_page, NULL, &page_doc );

            g_mutex_lock( &dd->document->mutex_doc );

            rc = annot_create( document_page, pv->highlight, pv->state, &errmsg );
            if ( rc )
            {
                meldung( pv->vf, "Fehler - Annotation einfügen:\n\nBei Aufruf "
                        "annot_create:\n", errmsg, NULL );
                g_free( errmsg );
            }
            else //Wenn annot eingefügt werden konnte
            {
                rc = viewer_reload_document_page( pv, dd->document, page_doc, 2, &errmsg );
                if ( rc )
                {
                    meldung( pv->vf, "Fehler - Annotation einfügen\n\n"
                            "Bei Aufruf viewer_reload_fz_page:\n", errmsg, NULL );
                    g_free( errmsg );
                }

                annot_load_pv_annot_page( document_page );

                rc = viewer_foreach( pv->zond->arr_pv, dd->document, page_doc,
                        viewer_cb_change_annot, NULL, &errmsg );
                if ( rc )
                {
                    meldung( pv->vf, "Fehler -\n\n",
                            "Bei Aufruf viewer_refresh_changed_page:\n", errmsg, NULL );
                    g_free( errmsg );

                    g_mutex_unlock( &dd->document->mutex_doc );

                    return -1;
                }
            }

            g_mutex_unlock( &dd->document->mutex_doc );

            pv->highlight[0].ul.x = -1;
        }
        else if ( pv->state == 0 ) gtk_widget_set_sensitive( pv->item_copy, TRUE );
    }

    return TRUE;
}


static gboolean
cb_viewer_layout_press_button( GtkWidget* layout, GdkEvent* event, gpointer
        user_data )
{
    gint rc = 0;
    PdfPunkt pdf_punkt = { 0 };

    PdfViewer* pv = (PdfViewer*) user_data;

    if ( !(pv->dd) ) return TRUE;

    gtk_widget_grab_focus( pv->layout );

    rc = viewer_abfragen_pdf_punkt( pv, fz_make_point( event->button.x,
            event->button.y ), &pdf_punkt );

//Einzelklick
    if ( event->button.type == GDK_BUTTON_PRESS &&
            event->button.button == 1 )
    {
        pv->click_pdf_punkt = pdf_punkt;
        pv->clicked_annot = NULL;
        pv->highlight[0].ul.x = -1;

        pv->y = event->button.y_root;
        pv->x = event->button.x_root;

        gtk_widget_set_sensitive( pv->item_copy, FALSE );

        if ( !rc )
        {
            PVAnnot* pv_annot = viewer_on_annot( pv, pdf_punkt );
            if ( pv_annot )
            {
                if ( (pv->state == 1 && pv_annot->type == PDF_ANNOT_HIGHLIGHT) ||
                    (pv->state == 2 && pv_annot->type == PDF_ANNOT_UNDERLINE) )
                    pv->clicked_annot = pv_annot;
            }

            if ( pv->clicked_annot );
            else if ( viewer_on_text( pv, pdf_punkt ) ) pv->click_on_text = TRUE;
            else
            {
                pv->click_on_text = FALSE;
                gdk_window_set_cursor( pv->gdk_window, pv->cursor_grab );
            }
        }
        gtk_widget_queue_draw( pv->layout ); //um ggf. Markierung der annot zu löschen
    }
#ifndef VIEWER
//Doppelklick - nur für Anbindung interessant
    else if ( event->button.type == GDK_2BUTTON_PRESS &&
            event->button.button == 1 )
    {
        gboolean punktgenau = FALSE;
        if ( event->button.state == GDK_SHIFT_MASK ) punktgenau = TRUE;
        if ( !rc && pv->anbindung.von.index == -1 )
        {
            pv->anbindung.von.seite = pdf_punkt.seite;
            if ( punktgenau ) pv->anbindung.von.index = pdf_punkt.punkt.y;
            else pv->anbindung.von.index = 0;

            //Wahl des Beginns irgendwie anzeigen
            gchar* button_label_text =
                    g_strdup_printf( "Anbindung Anfang löschen\nSeite: %i, Index: %i",
                    pv->anbindung.von.seite, pv->anbindung.von.index );
            gtk_widget_set_tooltip_text( pv->button_anbindung, button_label_text );
            gtk_widget_set_sensitive( pv->button_anbindung, TRUE );

            g_free( button_label_text );

            return FALSE;
        }

        //Wenn nicht zurückliegende Seite oder - wenn punktgenau - gleiche
        //Seite und zurückliegender Index
        if ( !rc && ((pdf_punkt.seite >= pv->anbindung.von.seite) ||
                    ((punktgenau) &&
                    (pdf_punkt.seite == pv->anbindung.von.seite) &&
                    (pdf_punkt.punkt.y >= pv->anbindung.von.index))) )
        {
            gchar* errmsg = NULL;

            pv->anbindung.bis.seite = pdf_punkt.seite;
            if ( punktgenau ) pv->anbindung.bis.index = pdf_punkt.punkt.y;
            else pv->anbindung.bis.index = EOP;

            DisplayedDocument* dd = document_get_dd( pv, pdf_punkt.seite, NULL, NULL, NULL );

            rc = ziele_erzeugen_anbindung( pv->zond, pv->vf, dd,
                    pv->anbindung, &errmsg );
            if ( rc == -2 )
            {
                meldung( pv->vf, "Fehler - Dokument konnte nicht gespeichert/"
                        "erneut geöffnet werden\n\nBei Aufruf ziele_erzeugen_"
                        "anbindung:\n", errmsg, "\n\nViewer wirdgeschlossen", NULL );
                g_free( errmsg );
                viewer_schliessen( pv );
            }
            else if ( rc == -1 )
            {
                meldung( pv->vf, "Fehler - Anbinden per Doppelklick nicht möglich:\n\n"
                        "Bei Aufruf ziele_erzeugen_anbindung:\n", errmsg, NULL );
                g_free( errmsg );
            }
            else if ( rc == 0 ) gtk_window_present( GTK_WINDOW(pv->zond->app_window) );
        }

        //anbindung.von "löschen"
        pv->anbindung.von.index = -1;

        //Anzeige Beginn rückgängig machen
        gtk_widget_set_tooltip_text( pv->button_anbindung, "Anbindung Anfang löschen" );
        gtk_widget_set_sensitive( pv->button_anbindung, FALSE );
    }
#endif

    return FALSE;
}


static void
cell_data_func_page( GtkTreeViewColumn* column, GtkCellRenderer* cell,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    GtkTreePath* path = NULL;
    gint* indices = NULL;
    gchar* text = NULL;

    path = gtk_tree_model_get_path( model, iter );
    indices = gtk_tree_path_get_indices( path );

    text = g_strdup_printf( "%i", indices[0] + 1 );
    gtk_tree_path_free( path );
    g_object_set( G_OBJECT(cell), "text", text, NULL );
    g_free( text );

    return;
}


static void
viewer_einrichten_fenster( PdfViewer* pv )
{
    pv->vf = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(pv->vf), 950, 1050 );

    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(pv->vf), accel_group);

//  Menu
#ifdef VIEWER
    GtkWidget* item_oeffnen = gtk_menu_item_new_with_label( "Datei öffnen" );
    pv->item_schliessen = gtk_menu_item_new_with_label( "Datei schließen" );
    GtkWidget* item_beenden = gtk_menu_item_new_with_label( "Beenden" );
    GtkWidget* item_sep1 = gtk_separator_menu_item_new( );
#endif // VIEWER
    pv->item_kopieren = gtk_menu_item_new_with_label( "Seiten kopieren" );
    gtk_widget_add_accelerator( pv->item_kopieren, "activate", accel_group,
            GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    pv->item_ausschneiden = gtk_menu_item_new_with_label( "Seiten ausschneiden" );
    gtk_widget_add_accelerator( pv->item_ausschneiden, "activate", accel_group,
            GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    //Drehen
    pv->item_drehen = gtk_menu_item_new_with_label( "Seiten drehen" );
    //Einfügen
    pv->item_einfuegen = gtk_menu_item_new_with_label( "Seiten einfügen" );
    //Löschen
    pv->item_loeschen = gtk_menu_item_new_with_label( "Seiten löschen" );
    //Löschen
    pv->item_entnehmen = gtk_menu_item_new_with_label( "Entnehmen" );
    //Löschen
    pv->item_ocr = gtk_menu_item_new_with_label( "OCR" );

    GtkWidget* sep0 = gtk_separator_menu_item_new( );

    pv->item_copy = gtk_menu_item_new_with_label( "Text kopieren" );

    gtk_widget_set_sensitive( pv->item_kopieren, FALSE );
    gtk_widget_set_sensitive( pv->item_ausschneiden, FALSE );
    gtk_widget_set_sensitive( pv->item_copy, FALSE );

    //Menu erzeugen
    GtkWidget* menu_viewer = gtk_menu_new( );

    //Füllen
#ifdef VIEWER
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), item_oeffnen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_schliessen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), item_beenden );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), item_sep1);
#endif // VIEWER
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_kopieren );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_ausschneiden );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_einfuegen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_drehen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_loeschen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_entnehmen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_ocr );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), sep0 );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_copy );

    //menu sichtbar machen
    gtk_widget_show_all( menu_viewer );

    //Menu Button
    GtkWidget* button_menu_viewer = gtk_menu_button_new( );

    //einfügen
    gtk_menu_button_set_popup( GTK_MENU_BUTTON(button_menu_viewer), menu_viewer );

//  Headerbar
    pv->headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(pv->headerbar),
            TRUE );
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(pv->headerbar), TRUE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(pv->headerbar),
            ":minimize,close" );
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(pv->headerbar),
            TRUE);
    gtk_window_set_titlebar( GTK_WINDOW(pv->vf), pv->headerbar );

//Toolbar
    //ToggleButton zum "Ausfahren der thumbnail-Leiste
    GtkWidget* button_thumb = gtk_toggle_button_new( );
    GtkWidget* image_thumb = gtk_image_new_from_icon_name( "go-next",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_thumb), image_thumb );

    //  Werkzeug Zeiger
    pv->button_speichern = gtk_toggle_button_new( );
    gtk_widget_set_sensitive( pv->button_speichern, FALSE );
    GtkWidget* button_zeiger = gtk_toggle_button_new( );
    GtkWidget* button_highlight = gtk_toggle_button_new( );
    GtkWidget* button_underline = gtk_toggle_button_new( );
    GtkWidget* button_paint = gtk_toggle_button_new( );

    GtkWidget* image_speichern = gtk_image_new_from_icon_name( "document-save",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(pv->button_speichern), image_speichern );
    GtkWidget* image_zeiger = gtk_image_new_from_icon_name( "accessories-text-editor",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_zeiger), image_zeiger );
    GtkWidget* image_highlight = gtk_image_new_from_icon_name( "edit-clear-all",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_highlight), image_highlight );
    GtkWidget* image_underline = gtk_image_new_from_icon_name( "format-text-underline",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_underline), image_underline );
    GtkWidget* image_paint = gtk_image_new_from_icon_name( "input-tablet",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_paint), image_paint );

    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(button_zeiger), TRUE );

    g_object_set_data( G_OBJECT(button_zeiger), "button-other-1", button_highlight );
    g_object_set_data( G_OBJECT(button_zeiger), "button-other-2", button_underline );
    g_object_set_data( G_OBJECT(button_zeiger), "button-other-3", button_paint );

    g_object_set_data( G_OBJECT(button_highlight), "ID", GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(button_highlight), "button-other-1", button_zeiger );
    g_object_set_data( G_OBJECT(button_highlight), "button-other-2", button_underline );
    g_object_set_data( G_OBJECT(button_highlight), "button-other-3", button_paint );

    g_object_set_data( G_OBJECT(button_underline), "ID", GINT_TO_POINTER(2) );
    g_object_set_data( G_OBJECT(button_underline), "button-other-1", button_zeiger );
    g_object_set_data( G_OBJECT(button_underline), "button-other-2", button_highlight );
    g_object_set_data( G_OBJECT(button_underline), "button-other-3", button_paint );

    g_object_set_data( G_OBJECT(button_paint), "ID", GINT_TO_POINTER(3) );
    g_object_set_data( G_OBJECT(button_paint), "button-other-1", button_zeiger );
    g_object_set_data( G_OBJECT(button_paint), "button-other-2", button_highlight );
    g_object_set_data( G_OBJECT(button_paint), "button-other-3", button_underline );

#ifndef VIEWER
    //button löschenAnbindung Anfangsposition
    pv->button_anbindung = gtk_button_new_from_icon_name( "edit-delete", GTK_ICON_SIZE_BUTTON );
    gtk_widget_set_sensitive( pv->button_anbindung, FALSE );
    gtk_widget_set_tooltip_text( pv->button_anbindung, "Anbindung Anfang löschen" );
#endif // VIEWERDisplayedDocument*

//vbox Tools
    GtkWidget* vbox_tools = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );

    //einfügen
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_menu_viewer, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), pv->button_speichern, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_thumb, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_zeiger, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_highlight, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_underline, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_paint, FALSE, FALSE, 0 );
#ifndef VIEWER
    gtk_box_pack_start( GTK_BOX(vbox_tools), pv->button_anbindung, FALSE, FALSE, 0 );
#endif // VIEWER

//Box mit Eingabemöglichkeiten
    //Eingagabefeld für Seitenzahlen erzeugen
    pv->entry = gtk_entry_new();
    gtk_entry_set_input_purpose( GTK_ENTRY(pv->entry), GTK_INPUT_PURPOSE_DIGITS );
    gtk_entry_set_width_chars( GTK_ENTRY(pv->entry), 9 );

    //label mit Gesamtseitenzahl erzeugen
    pv->label_anzahl = gtk_label_new( "" );

    //in box und dann in frame
    GtkWidget* box_seiten = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(box_seiten), pv->entry, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_seiten), pv->label_anzahl, FALSE, FALSE, 0 );
    GtkWidget* frame_seiten = gtk_frame_new( "Seiten" );
    gtk_container_add( GTK_CONTAINER(frame_seiten), box_seiten );

    //SpinButton für Zoom
    GtkWidget* spin_button = gtk_spin_button_new_with_range( ZOOM_MIN,
            ZOOM_MAX, 5.0 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(spin_button), (gdouble) pv->zoom );

    //frame
    GtkWidget* frame_spin = gtk_frame_new( "Zoom" );
    gtk_container_add( GTK_CONTAINER(frame_spin), spin_button );

    //Textsuche im geöffneten Dokument
    pv->button_vorher = gtk_button_new_from_icon_name( "go-previous",
            GTK_ICON_SIZE_SMALL_TOOLBAR );
    pv->entry_search = gtk_entry_new( );
    pv->button_nachher = gtk_button_new_from_icon_name( "go-next",
            GTK_ICON_SIZE_SMALL_TOOLBAR );

    GtkWidget* box_text = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(box_text), pv->button_vorher, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_text), pv->entry_search, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_text), pv->button_nachher, FALSE, FALSE, 0 );
    GtkWidget* frame_search = gtk_frame_new( "Text suchen" );
    gtk_container_add( GTK_CONTAINER(frame_search), box_text );

    gtk_header_bar_pack_start( GTK_HEADER_BAR(pv->headerbar), frame_seiten );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(pv->headerbar), frame_spin );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(pv->headerbar), frame_search );

//layout
    pv->layout = gtk_layout_new( NULL, NULL );
    gtk_widget_set_can_focus( pv->layout, TRUE );

    gtk_widget_add_events( pv->layout, GDK_POINTER_MOTION_MASK );
    gtk_widget_add_events( pv->layout, GDK_BUTTON_PRESS_MASK );
    gtk_widget_add_events( pv->layout, GDK_BUTTON_RELEASE_MASK );

//Scrolled window
    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    //Adjustments
    pv->v_adj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(swindow) );
    pv->h_adj = gtk_scrolled_window_get_hadjustment(
            GTK_SCROLLED_WINDOW(swindow) );

    gtk_container_add( GTK_CONTAINER(swindow), pv->layout );
    gtk_widget_set_halign( pv->layout, GTK_ALIGN_CENTER );

//Scrolled window für thumbnail_tree
    pv->tree_thumb = gtk_tree_view_new( );
    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(pv->tree_thumb), FALSE );
    GtkTreeSelection* sel = gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_MULTIPLE );

    GtkTreeViewColumn* column = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

    GtkCellRenderer* renderer_text = gtk_cell_renderer_text_new( );
    GtkCellRenderer* renderer_pixbuf = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( column, renderer_text, FALSE );
    gtk_tree_view_column_set_cell_data_func( column, renderer_text,
            cell_data_func_page, pv, NULL );
    gtk_tree_view_column_pack_start( column, renderer_pixbuf, TRUE );
    gtk_tree_view_column_set_attributes(column, renderer_pixbuf, "pixbuf", 0, NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(pv->tree_thumb), column);

    GtkListStore* store_thumbs = gtk_list_store_new( 2, GDK_TYPE_PIXBUF,
            G_TYPE_BOOLEAN );
    gtk_tree_view_set_model( GTK_TREE_VIEW(pv->tree_thumb),
            GTK_TREE_MODEL(store_thumbs) );
    g_object_unref( store_thumbs );

    GtkWidget* swindow_tree = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow_tree), pv->tree_thumb );
    GtkAdjustment* vadj_thumb =
            gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(swindow_tree) );

    GtkWidget* paned = gtk_paned_new( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_pack1( GTK_PANED(paned), swindow, TRUE, TRUE );
    gtk_paned_pack2( GTK_PANED(paned), swindow_tree, FALSE, FALSE );
    gtk_paned_set_position( GTK_PANED(paned), 760 );

//hbox erstellen
    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

    gtk_box_pack_start( GTK_BOX(hbox), vbox_tools, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), paned, TRUE, TRUE, 0 );

    gtk_container_add( GTK_CONTAINER(pv->vf), hbox );

    gtk_widget_show_all( pv->vf );
    gtk_widget_hide( swindow_tree );

    pv->gdk_window = gtk_widget_get_window( pv->vf );
    GdkDisplay* display = gdk_window_get_display( pv->gdk_window );
    pv->cursor_default = gdk_cursor_new_from_name( display, "default" );
    pv->cursor_text = gdk_cursor_new_from_name( display, "text" );
    pv->cursor_vtext = gdk_cursor_new_from_name( display, "vertical-text" );
    pv->cursor_grab = gdk_cursor_new_from_name( display, "grab" );
    pv->cursor_annot = gdk_cursor_new_from_name( display, "pointer" );

//Signale Menu
#ifdef VIEWER
    //öffnen
    g_signal_connect( item_oeffnen, "activate", G_CALLBACK(cb_datei_oeffnen), pv );
    //öffnen
    g_signal_connect( pv->item_schliessen, "activate", G_CALLBACK(cb_datei_schliessen), pv );
    //beenden
    g_signal_connect( item_beenden, "activate", G_CALLBACK(cb_pv_sa_beenden), pv );
#endif // VIEWER
    //Seiten kopieren
    g_signal_connect( pv->item_kopieren, "activate", G_CALLBACK(cb_seiten_kopieren),
            pv );
    //Seiten ausschneiden
    g_signal_connect( pv->item_ausschneiden, "activate",
            G_CALLBACK(cb_seiten_ausschneiden), pv );
    //löschen
    g_signal_connect( pv->item_loeschen, "activate", G_CALLBACK(cb_pv_seiten_loeschen), pv );
    //einfügen
    g_signal_connect( pv->item_einfuegen, "activate", G_CALLBACK(cb_pv_seiten_einfuegen), pv );
    //drehen
    g_signal_connect( pv->item_drehen, "activate", G_CALLBACK(cb_pv_seiten_drehen), pv );
    //OCR
    g_signal_connect( pv->item_ocr, "activate", G_CALLBACK(cb_pv_seiten_ocr), pv );
    //Text kopieren
    g_signal_connect( pv->item_copy, "activate", G_CALLBACK(cb_pv_copy_text), pv );

    //signale des entry
    g_signal_connect( pv->entry, "activate",
            G_CALLBACK(cb_viewer_page_entry_activated), (gpointer) pv );

    //signale des SpinButton
    g_signal_connect( spin_button, "value-changed",
            G_CALLBACK(cb_viewer_spinbutton_value_changed), (gpointer) pv );

    //Textsuche-entry
    g_signal_connect( pv->entry_search, "activate",
            G_CALLBACK(cb_viewer_text_search_entry_activate), pv );
    g_signal_connect_swapped( gtk_entry_get_buffer( GTK_ENTRY(pv->entry_search) ),
            "deleted-text", G_CALLBACK(cb_viewer_text_search_entry_buffer_changed), pv );
    g_signal_connect_swapped( gtk_entry_get_buffer( GTK_ENTRY(pv->entry_search) ),
            "inserted-text", G_CALLBACK(cb_viewer_text_search_entry_buffer_changed), pv );
    g_signal_connect( pv->button_vorher, "clicked",
            G_CALLBACK(cb_viewer_zurueck_button_clicked), pv );
    g_signal_connect( pv->button_nachher, "clicked",
            G_CALLBACK(cb_viewer_vor_button_clicked), pv );

// Signale Toolbox
    g_signal_connect( pv->button_speichern, "clicked", G_CALLBACK(cb_pv_speichern),
            pv );
    g_signal_connect( button_thumb, "toggled", G_CALLBACK(cb_tree_thumb),
            swindow_tree );
    g_signal_connect( button_zeiger, "button-press-event",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
    g_signal_connect( button_highlight, "button-press-event",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
    g_signal_connect( button_underline, "button-press-event",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
    g_signal_connect( button_paint, "button-press-event",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
#ifndef VIEWER
    //Anbindung löschen
    g_signal_connect( pv->button_anbindung, "clicked",
            G_CALLBACK(cb_viewer_loeschen_anbindung_button_clicked), pv );
#endif // VIEWER
    //und des vadjustments
    g_signal_connect( pv->v_adj, "value-changed",
            G_CALLBACK(cb_viewer_vadjustment_value_changed), pv );

    //vadjustment von thumbnail-leiste
    g_signal_connect( vadj_thumb, "value-changed",
            G_CALLBACK(cb_viewer_vadj_thumb), pv );

    //thumb-tree
    g_signal_connect( pv->tree_thumb, "row-activated",
            G_CALLBACK(cb_thumb_activated), pv );
    g_signal_connect( sel, "changed", G_CALLBACK(cb_thumb_sel_changed), pv );

    //Jetzt die Signale des layout verbinden
    g_signal_connect( pv->layout, "button-press-event",
            G_CALLBACK(cb_viewer_layout_press_button), (gpointer) pv );
    g_signal_connect( pv->layout, "button-release-event",
            G_CALLBACK(cb_viewer_layout_release_button), (gpointer) pv );
    g_signal_connect( pv->layout, "motion-notify-event",
            G_CALLBACK(cb_viewer_layout_motion_notify), (gpointer) pv );
    g_signal_connect( pv->layout, "key-press-event",
            G_CALLBACK(cb_viewer_swindow_key_press), (gpointer) pv );

    g_signal_connect( pv->vf, "motion-notify-event",
            G_CALLBACK(cb_viewer_motion_notify), (gpointer) pv );
#ifndef VIEWER //Stand-Alone hat anderen Callback zum schließen
    g_signal_connect( pv->vf, "delete-event",
            G_CALLBACK(cb_viewer_delete_event), (gpointer) pv );
#else
    g_signal_connect( pv->vf, "delete-event", G_CALLBACK(cb_pv_sa_beenden), pv );
#endif // VIEWER

    return;
}


void
viewer_display_document( PdfViewer* pv, DisplayedDocument* dd )
{
    pv->dd = dd;

    viewer_create_layout( pv );

    viewer_einrichten_layout( pv );

    viewer_init_thread_pools( pv );

    gtk_widget_grab_focus( pv->layout );

    return;
}


PdfViewer*
viewer_start_pv( Projekt* zond )
{
    PdfViewer* pv = g_malloc0( sizeof( PdfViewer ) );

    pv->zond = zond;
    pv->zoom = g_settings_get_double( zond->settings, "zoom" );

    g_ptr_array_add( zond->arr_pv, pv );

    pv->arr_pages = g_ptr_array_new_with_free_func( (GDestroyNotify) viewer_free_viewer_page );

    /*  Array für Text-Treffer  */
    pv->arr_text_treffer = g_array_new( FALSE, FALSE, sizeof( PVTextTreffer ) );

    //highlight Sentinel an den Anfang setzen
    pv->highlight[0].ul.x = -1;
    pv->anbindung.von.index = -1;
    pv->anbindung.bis.index = EOP + 1;

    //  Fenster erzeugen und anzeigen
    viewer_einrichten_fenster( pv );

    return pv;
}


