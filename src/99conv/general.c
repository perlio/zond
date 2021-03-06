#include "../global_types.h"
#include "../error.h"

#include "../99conv/baum.h"
#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/db_zu_baum.h"
#include "../99conv/pdf.h"

#include "../40viewer/document.h"

#include <gtk/gtk.h>
#include <ctype.h>


/** String Utilities **/
#ifndef VIEWER
gchar*
full_path_to_rel_path( Projekt* zond, const gchar* full_path )
{
    if ( !zond->project_dir ) return NULL;

    gchar* rel_path = g_strdup( full_path + strlen( zond->project_dir) + 1 );

    return rel_path;
}
#endif // VIEWER


gchar*
utf8_to_local_filename( const gchar* utf8 )
{
    //utf8 in filename konvertieren
    gsize written;
    gchar* charset = g_get_codeset();
    gchar* local_filename = g_convert( utf8, -1, charset, "UTF-8", NULL, &written,
            NULL );
    g_free( charset );

    return local_filename; //muß g_freed werden!
}


gint
string_to_guint( const gchar* string, guint* zahl )
{
    gboolean is_guint = TRUE;
    if ( !strlen( string ) ) is_guint = FALSE;
    gint i = 0;
    while ( i < (gint) strlen( string ) && is_guint )
    {
        if ( !isdigit( (int) *(string + i) ) ) is_guint = FALSE;
        i++;
    }

    if ( is_guint )
    {
        *zahl = atoi( string );
        return 0;
    }
    else return -1;
}


gchar*
add_string( gchar* old_string, gchar* add_string )
{
    gchar* new_string = NULL;
    if ( old_string ) new_string = g_strconcat( old_string, add_string, NULL );
    else new_string = g_strdup( add_string );
    g_free( old_string );
    g_free( add_string );

    return new_string;
}


gchar*
prepend_string( gchar* old_string, gchar* prepend )
{
    gchar* new_string = g_strconcat( prepend, old_string, NULL );
    g_free( old_string );
    g_free( prepend );

    return new_string;
}



/** Andere Sachen **/
GSList*
choose_files( GtkWindow* window, const gchar* title_text, gchar* accept_text,
        gint action, gboolean multiple )
{
    GtkWidget *dialog = NULL;
    gint rc = 0;
    GSList* list = NULL;

    dialog = gtk_file_chooser_dialog_new( title_text,
            window, action, "_Abbrechen",
            GTK_RESPONSE_CANCEL, accept_text, GTK_RESPONSE_ACCEPT, NULL);

    gchar* current_dir = g_get_current_dir( );
    gtk_file_chooser_set_current_folder(
            GTK_FILE_CHOOSER(dialog), current_dir );
    g_free( current_dir );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER(dialog), multiple );
    gtk_file_chooser_set_do_overwrite_confirmation( GTK_FILE_CHOOSER(dialog),
            TRUE );
    if ( action == GTK_FILE_CHOOSER_ACTION_SAVE )
            gtk_file_chooser_set_current_name( GTK_FILE_CHOOSER(dialog),
            ".ZND" );

    rc = gtk_dialog_run( GTK_DIALOG(dialog) );
    if ( rc == GTK_RESPONSE_ACCEPT )
            list = gtk_file_chooser_get_uris( GTK_FILE_CHOOSER(dialog) );

    //Dialog schließen
    gtk_widget_destroy(dialog);

    return list;
}


gchar*
filename_speichern( GtkWindow* window, const gchar* titel )
{
    GSList* list = choose_files( window, titel, "Speichern",
            GTK_FILE_CHOOSER_ACTION_SAVE, FALSE );

    if ( !list ) return NULL;

    gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );
    g_free( list->data );
    g_slist_free( list );

    gchar* abs_path = g_strdup( uri_unescaped + 8 );
    g_free( uri_unescaped );

    return abs_path; //muß g_freed werden
}


gchar*
filename_oeffnen( GtkWindow* window )
{
    GSList* list = choose_files( window, "Datei auswählen", "Öffnen",
            GTK_FILE_CHOOSER_ACTION_OPEN, FALSE );

    if ( !list ) return NULL;

    gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );
    g_free( list->data );
    g_slist_free( list );

    gchar* abs_path = g_strdup( uri_unescaped + 8);
    g_free( uri_unescaped );

    return abs_path; //muß g_freed werden
}


/*
**  Zeigt Fenster mit Liste übergebener Strings.
**  Muß mit NULL abgeschlossen werden.
**  text1 darf nicht NULL sein!  */
void
meldung( GtkWidget* window, const gchar* text1, ... )
{
    va_list ap;
    gchar* message = g_strdup( "" );
    gchar* str = NULL;

    str = (gchar*) text1;
    va_start( ap, text1 );
    while ( str )
    {
        message = add_string( message, g_strdup( str ) );
        str = va_arg( ap, gchar* );
    }

    GtkWidget* dialog = gtk_message_dialog_new( GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE, message );
    gtk_dialog_run ( GTK_DIALOG (dialog) );
    gtk_widget_destroy( dialog );

    g_free( message );

    return;
}


static void
cb_entry_text( GtkEntry* entry, gpointer data )
{
    gtk_widget_grab_focus( (GtkWidget*) data );

    return;
}


/** ... response_id, next_button_text, next_response_id, ..., NULL
**/
gint
dialog_with_buttons( GtkWidget* window, const gchar* message,
        const gchar* secondary, gchar** text, gchar* first_button_text, ... )
{
    gint res;
    GtkWidget* entry = NULL;
    va_list arg_pointer;
    gchar* button_text = NULL;
    GtkWidget* button = NULL;
    gboolean first_button = TRUE;

    va_start( arg_pointer, first_button_text );

   GtkWidget* dialog = gtk_message_dialog_new( GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE, message, NULL );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG(dialog), "%s",
            secondary );

    //buttons einfügen
    button_text = first_button_text;
    while ( button_text )
    {
        GtkWidget* tmp = NULL;
        gint response_id = 0;

        response_id = va_arg( arg_pointer, gint );

        tmp = gtk_dialog_add_button( GTK_DIALOG(dialog), button_text, response_id );
        if ( first_button )
        {
            button = tmp;
            first_button = FALSE;
        }

        button_text = va_arg( arg_pointer, gchar* );
    }

    if ( text )
    {
        GtkWidget* content = gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG(dialog) );
        entry = gtk_entry_new( );
        gtk_container_add( GTK_CONTAINER(content), entry);
        gtk_entry_set_text( GTK_ENTRY(entry), *text );

        gtk_widget_show_all( content );

        g_signal_connect( entry, "activate", G_CALLBACK(cb_entry_text),
                (gpointer) button );
    }

    res = gtk_dialog_run( GTK_DIALOG(dialog) );

    if ( text )
    {
        g_free( *text );
        *text = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry) ) );
    }

    gtk_widget_destroy( dialog );

    return res;
}


/** wrapper
**/
gint
abfrage_frage( GtkWidget* window, const gchar* message, const gchar* secondary, gchar** text )
{
    gint res;

    res = dialog_with_buttons( window, message, secondary, text, "Ja",
            GTK_RESPONSE_YES, "Nein", GTK_RESPONSE_NO, NULL );

    return res;
}


#ifndef VIEWER
/** Wenn Fehler: -1
    Wenn Vorfahre Datei ist: 1
    ansonsten: 0 **/
gint
hat_vorfahre_datei( Projekt* zond, Baum baum, gint anchor_id, gboolean child, gchar** errmsg )
{
    if ( !child )
    {
        anchor_id = db_get_parent( zond, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_PAO( "db_get_parent" )
    }

    gint rc = 0;
    while ( anchor_id != 0 )
    {
        rc = db_knotentyp_abfragen( zond, baum, anchor_id, errmsg );
        if ( rc == -1 ) ERROR_PAO( "db_knotentyp_abfragen" )

        if ( rc > 0 ) return 1;

        anchor_id = db_get_parent( zond, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_PAO( "db_get_parent" )
    }

    return 0;
}


gint
knoten_verschieben( Projekt* zond, Baum baum, gint node_id, gint new_parent,
        gint new_older_sibling, gchar** errmsg )
{
    gint rc = 0;

    //kind verschieben
    rc = db_verschieben_knoten( zond, baum, node_id, new_parent,
            new_older_sibling, errmsg );
    if ( rc ) ERROR_PAO (" db_verschieben_knoten" )

    //Knoten im tree löschen
    //hierzu iter des verschobenen Kindknotens herausfinden
    GtkTreeIter* iter = NULL;
    iter = baum_abfragen_iter( zond->treeview[baum], node_id );

    gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[baum] ) ), iter );
    gtk_tree_iter_free( iter );

    //jetzt neuen kindknoten aus db erzeugen
    //hierzu zunächst iter des Anker-Knotens ermitteln
    gboolean kind = FALSE;
    if ( new_older_sibling )
    {
        iter = baum_abfragen_iter( zond->treeview[baum], new_older_sibling );
        kind = FALSE;
    }
    else
    {
        iter = baum_abfragen_iter( zond->treeview[baum], new_parent );
        kind = TRUE;
    }

    //Knoten erzeugen
    GtkTreeIter* iter_anker = db_baum_knoten_mit_kindern( zond, FALSE,
            baum, node_id, iter, kind, errmsg );
    gtk_tree_iter_free( iter );

    if ( !iter_anker ) ERROR_PAO( "db_baum_knoten_mit_kindern" )

    gtk_tree_iter_free( iter_anker );

    return 0;
}
#endif // VIEWER


/** Gibt nur bei Fehler NULL zurück, sonst immer Zeiger auf Anbindung **/
static Anbindung*
ziel_zu_anbindung( fz_context* ctx, const gchar* rel_path, Ziel* ziel, gchar** errmsg )
{
    gint page_num = 0;

    Anbindung* anbindung = g_malloc0( sizeof( Anbindung ) );

    page_num = pdf_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_von, errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );
        ERROR_PAO_R( "pdf_get_page_num_from_dest", NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->von.seite = page_num;

    page_num = pdf_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_bis,
            errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );

        ERROR_PAO_R( "pdf_get_page_num_from_dest", NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->bis.seite = page_num;

    anbindung->von.index = ziel->index_von;
    anbindung->bis.index = ziel->index_bis;

    return anbindung;
}


void
ziele_free( Ziel* ziel )
{
    if ( !ziel ) return;

    g_free( ziel->ziel_id_von );
    g_free( ziel->ziel_id_bis );

    g_free( ziel );

    return;
}


#ifndef VIEWER
/** Keine Datei mit node_id verknüpft: 2
    Kein ziel mit node_id verknüpft: 1
    Datei und ziel verknüpft: 0
    Fehler (inkl. node_id existiert nicht): -1

    Funktion sollte thread-safe sein! **/
gint
abfragen_rel_path_and_anbindung( Projekt* zond, Baum baum, gint node_id,
        gchar** rel_path, Anbindung** anbindung, gchar** errmsg )
{
    gint rc = 0;
    Ziel* ziel = NULL;
    gchar* rel_path_intern = NULL;
    Anbindung* anbindung_intern = NULL;

    rc = db_get_rel_path( zond, baum, node_id, &rel_path_intern, errmsg );
    if ( rc == -1 ) ERROR_PAO( "db_get_rel_path" )
    else if ( rc == -2 ) return 2;

    rc = db_get_ziel( zond, baum, node_id, &ziel, errmsg );
    if ( rc )
    {
        g_free( rel_path_intern );
        ERROR_PAO( "db_get_ziel" )
    }

    if ( !ziel )
    {
        if ( rel_path ) *rel_path = rel_path_intern;
        else g_free( rel_path_intern );

        return 1;
    }

    Document* document = document_geoeffnet( zond, rel_path_intern );
    if ( document ) g_mutex_lock( &document->mutex_doc );

    anbindung_intern = ziel_zu_anbindung( zond->ctx, rel_path_intern, ziel, errmsg );

    if ( document ) g_mutex_unlock( &document->mutex_doc );

    ziele_free( ziel );

    if ( !anbindung_intern )
    {
        g_free( rel_path_intern );
        ERROR_PAO( "ziel_zu_anbindung" )
    }

    if ( rel_path ) *rel_path = rel_path_intern;
    else g_free( rel_path_intern );

    if ( anbindung ) *anbindung = anbindung_intern;
    else g_free( anbindung_intern );

    return 0;
}
#endif // VIEWER


gboolean
is_pdf( const gchar* path )
{
    gchar* content_type = NULL;
    gboolean res = FALSE;

    content_type = g_content_type_guess( path, NULL, 0, NULL );

    //Sonderbehandung, falls pdf-Datei
    if ( (!g_strcmp0( content_type, ".pdf" ) || !g_strcmp0( content_type,
            "application/pdf" )) ) res = TRUE;
    g_free( content_type );

    return res;
}


gboolean
is_white( const char* s )
{
    if ( *s == 0 || *s == 9 || *s == 10 || *s == 12 || *s == 13 || *s == 32 )
            return TRUE;

    return FALSE;
}


void
info_window_close( InfoWindow* info_window )
{
    GtkWidget* button =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(info_window->dialog),
            GTK_RESPONSE_CANCEL );
    gtk_button_set_label( GTK_BUTTON(button), "Schließen" );

    gtk_dialog_run( GTK_DIALOG(info_window->dialog) );

    gtk_widget_destroy( info_window->dialog );

    g_free( info_window );

    return;
}


void
info_window_scroll( InfoWindow* info_window )
{
    GtkWidget* viewport = NULL;
    GtkWidget* swindow = NULL;
    GtkAdjustment* adj = NULL;

    viewport = gtk_widget_get_parent( info_window->content);
    swindow = gtk_widget_get_parent( viewport );
    adj = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(swindow) );
    gtk_adjustment_set_value( adj, gtk_adjustment_get_upper( adj ) );

    return;
}


void
info_window_set_message( InfoWindow* info_window, const gchar* message )
{
    GtkWidget* label = NULL;

    label = gtk_label_new( message );
    gtk_widget_set_halign( label, GTK_ALIGN_START );
    gtk_box_pack_start( GTK_BOX(info_window->content), label, FALSE, FALSE, 0 );
    gtk_widget_show_all( label );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    info_window_scroll( info_window );

    return;
}


static gboolean
cb_info_window_delete_event( GtkWidget* widget, gpointer data )
{
    gtk_dialog_response( GTK_DIALOG(widget), GTK_RESPONSE_CANCEL );

    return TRUE;
}


static void
cb_info_window_response( GtkDialog* dialog, gint id, gpointer data )
{
    InfoWindow* info_window = (InfoWindow*) data;

    if ( info_window->cancel ) return;

    info_window_set_message( info_window, "...abbrechen" );
    info_window->cancel = TRUE;

    return;
}


InfoWindow*
info_window_open( GtkWidget* window, const gchar* title )
{
    GtkWidget* content = NULL;
    GtkWidget* swindow = NULL;

    InfoWindow* info_window = g_malloc0( sizeof( InfoWindow ) );

    info_window->dialog = gtk_dialog_new_with_buttons( title, GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "Abbrechen",
            GTK_RESPONSE_CANCEL, NULL );

    gtk_window_set_default_size( GTK_WINDOW(info_window->dialog), 450, 110 );

    content = gtk_dialog_get_content_area( GTK_DIALOG(info_window->dialog) );
    swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(content), swindow, TRUE, TRUE, 0 );

    info_window->content = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(swindow), info_window->content );

    gtk_widget_show_all( info_window->dialog );

    g_signal_connect( GTK_DIALOG(info_window->dialog), "response",
            G_CALLBACK(cb_info_window_response), info_window );
    g_signal_connect( info_window->dialog, "delete-event",
            G_CALLBACK(cb_info_window_delete_event), NULL );

    return info_window;
}

