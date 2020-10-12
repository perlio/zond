/*
zond (info.c) - Akten, Beweisstücke, Unterlagen
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
#include "../99conv/db_write.h"
#include "../99conv/baum.h"
#include "../99conv/general.h"

#include <gtk/gtk.h>
#include <sqlite3.h>


gint
info_set_node_text( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gchar* node_text = NULL;
    gint rc = 0;
    GtkTreeIter* iter = NULL;

    rc = db_create_node_text_from_info( zond, baum, node_id, &node_text, errmsg );
    if ( rc ) ERROR_PAO( "db_create_node_text_from_info" )

    if ( !node_text ) return 0;

    //neue icon_id in db speichern
    rc = db_set_node_text( zond, baum, node_id, node_text, errmsg );
    if ( rc )
    {
        g_free( node_text );
        ERROR_PAO( "db_set_node_text" )
    }

    iter = baum_abfragen_iter( zond->treeview[baum], node_id );
    //neuen text im tree speichern
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[baum] )), iter, 1, node_text, -1 );
    gtk_tree_iter_free( iter );
    g_free( node_text );

    return 0;
}


static gchar*
info_element_zusammensetzen( GtkTreeModel* treemodel, GtkTreeIter* iter )
{
    gchar* element = NULL;
    gtk_tree_model_get( treemodel, iter, 0, &element, -1 );

    //Spitzlammern
    gchar* element_temp = g_strconcat( "<", element, ">", NULL );
    g_free( element );
    element = g_strdup( element_temp );
    g_free( element_temp );

    GtkTreeIter* iter_child = gtk_tree_iter_copy( iter );
    GtkTreeIter iter_parent;
    while ( gtk_tree_model_iter_parent( treemodel, &iter_parent, iter_child ) )
    {
        gtk_tree_iter_free( iter_child );

        gchar* parent_element = NULL;
        gtk_tree_model_get( treemodel, &iter_parent, 0, &parent_element, -1 );

        //iter tauschen
        iter_child = gtk_tree_iter_copy( &iter_parent );

        //strings zusammensetzen und tauschen
        gchar* element_new = g_strconcat( "<", parent_element, ">", element, NULL );
        g_free( element );
        element = g_strdup( element_new );
        g_free( element_new );
        g_free( parent_element );
    }
    gtk_tree_iter_free( iter_child );

    return element;
}


static gboolean
info_foreach_path_from_element( GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
        gpointer data )
{
    ForeachPipe* fp = data;
    gchar* element = fp->question;

    gchar* element_tree = info_element_zusammensetzen( model, iter );

    if ( !g_strcmp0( element_tree, element ) )
    {
        fp->answer = gtk_tree_path_copy( path );
        g_free( element_tree );

        return TRUE;
    }

    g_free( element_tree );

    return FALSE;
}


static void
info_springen_zu_element( GtkWidget* treeview, gchar* element )
{
    ForeachPipe fp = { 0 };
    fp.question = element;
    gtk_tree_model_foreach( gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) ),
            (GtkTreeModelForeachFunc) info_foreach_path_from_element, &fp );

    GtkTreePath* path = fp.answer;

    if ( !path ) return;

    GtkTreeViewColumn* column =
            gtk_tree_view_get_column( GTK_TREE_VIEW(treeview), 1 );

    gtk_tree_view_expand_to_path( GTK_TREE_VIEW(treeview), path );

    gtk_tree_view_set_cursor( GTK_TREE_VIEW(treeview), path, column, TRUE );

    gtk_tree_path_free( path );

}


static gint
info_element_enthalten( gchar* element, GPtrArray* elemente )
{
    for ( gint i = 0; i < (gint) elemente->len; i++ )
            if ( !g_strcmp0( element, g_ptr_array_index( elemente, i ) ) )
            return i;

    return -1;
}


static gchar*
info_element_laenge_n( gchar* element, gint laenge )
{
    gchar** keys = g_strsplit_set( element, "<>", -1 );

    //Elemente in Array umkopieren
    GPtrArray* elemente = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    for ( gint i = 0; i < g_strv_length( keys ); i++ ) if ( g_strcmp0( keys[i],
            "" ) ) g_ptr_array_add( elemente, g_strdup( keys[i] ) );
    g_strfreev( keys );

    if ( (!elemente->len) || (laenge > (gint) elemente->len) )
    {
        g_ptr_array_free( elemente, TRUE );

        return NULL;
    }

    gchar* elem = NULL;
    gchar* elem_alt = g_strdup( "" );

    for ( gint i = 0; i < laenge; i++ )
    {
        elem = g_strconcat( elem_alt, "<", g_ptr_array_index( elemente, i ),
                ">", NULL );
        g_free( elem_alt );
        elem_alt = g_strdup( elem );
        g_free( elem );
    }

    g_ptr_array_free( elemente, TRUE );

    return elem_alt;
}


static gint
info_element_tiefe( gchar* element )
{
    gint zaehler = 0;
    gchar** keys = g_strsplit_set( element, "<>", -1 );
    guint tiefe = g_strv_length( keys );
    for ( gint i = 0; i < tiefe; i++ ) if ( g_strcmp0( keys[i], "" ) ) zaehler++;
    g_strfreev( keys );

    return zaehler;
}


static gint
info_suche_eltern( gchar* element, GPtrArray* info_elemente )
{
    gint tiefe = info_element_tiefe( element );

    if ( tiefe == 1 ) return -1;

    gchar* element_parent = info_element_laenge_n( element, tiefe - 1 );

    gint stelle = info_element_enthalten( element_parent, info_elemente );

    g_free( element_parent );

    return stelle;
}


static gboolean
info_row_wird_blatt( GtkTreeView* treeview, GtkTreeIter* iter, const gchar* text )
{
    GtkTreeModel* model = gtk_tree_view_get_model( treeview );

    gint geschwister = 0;
    GtkTreeIter* iter_parent = g_malloc( sizeof( GtkTreeIter ) );
    if ( gtk_tree_model_iter_parent( model, iter_parent, iter ) )
            geschwister = gtk_tree_model_iter_n_children( model, iter_parent );
    else
    {
        g_free( iter_parent );
        iter_parent = NULL;
        geschwister = gtk_tree_model_iter_n_children( model, iter_parent );
    }

    gchar* key = NULL;
    GtkTreeIter iter_vergleich;
    for ( gint i = 0; i < geschwister; i++ )
    {
        gtk_tree_model_iter_nth_child( model, &iter_vergleich, iter_parent, i );

        gtk_tree_model_get( model, &iter_vergleich, 0, &key, -1 );
        if ( !g_strcmp0( text, key ) )
        {
            if ( gtk_tree_model_iter_has_child( model, &iter_vergleich ) )
            {
                g_free( key );

                return FALSE;
            }
        }

        g_free( key );
    }

    if ( iter_parent ) g_free( iter_parent );
    return TRUE;
}


static void
info_tree_view_anzeigen( GtkWidget* infofenster )
{
    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data( G_OBJECT(infofenster),
            "treeview" );

    gtk_tree_view_expand_all( treeview );

    gtk_widget_show_all( infofenster );
    gtk_tree_view_columns_autosize( treeview );

    return;
}


static gchar*
info_letzter_key( gchar* element )
{
    gchar** keys = g_strsplit_set( element, "<>", -1 );

    gint zaehler = g_strv_length( keys );
    if ( !zaehler ) return NULL;

    while ( !g_strcmp0( keys[zaehler - 1], "" ) ) zaehler--;

    gchar* letzter_key = g_strdup( keys[zaehler - 1] );

    g_strfreev( keys );

    return letzter_key;
}


static void
info_store_fuellen( GtkWidget* infofenster, GPtrArray* info_elemente,
        GPtrArray* info_werte )
{
    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data(
            G_OBJECT(infofenster), "treeview" );
    GtkTreeStore* treestore = GTK_TREE_STORE(gtk_tree_view_get_model( treeview ));

    GPtrArray* array_objekte = g_object_get_data( G_OBJECT(infofenster),
            "array-objekte" );

    GtkTreeIter iter;
    GtkTreeIter* iter_eltern = NULL;

    GPtrArray* iter_array =
            g_ptr_array_new_with_free_func( (GDestroyNotify) gtk_tree_iter_free );

    for ( gint i = 0; i < info_elemente->len; i++ )
    {
        gchar* element = g_ptr_array_index( info_elemente, i );
        gchar* letzter_key = info_letzter_key( element );

        gchar* wert = g_ptr_array_index( info_werte, i );

        if ( (array_objekte->len > 1) && wert ) wert = "<...>";

        gint z = info_suche_eltern( element, info_elemente );
        if ( z == -1 ) iter_eltern = NULL;
        else iter_eltern = g_ptr_array_index( iter_array, z );

        gtk_tree_store_insert( treestore, &iter, iter_eltern, -1 );
        gtk_tree_store_set( treestore, &iter, 0, letzter_key, 1,
                wert, -1 );

        g_ptr_array_add( iter_array, gtk_tree_iter_copy( &iter ) );

        g_free( letzter_key );
    }

    g_ptr_array_free( iter_array, TRUE );

    return;
}


static void
info_erzeugen_fehlende_vorfahren( GPtrArray* elemente, GPtrArray* werte )
{
    gint i = 0;
    for ( i = 0; i < (gint) elemente->len; i++ )
    {
        gchar* element = g_ptr_array_index( elemente, i );

        gint z = 0;
        gint tiefe = 0;

        z = info_suche_eltern( element, elemente );
        if ( z != -1 ) continue; //Eltern da - alles gut

        tiefe = info_element_tiefe( element );
        if ( tiefe == 1 ) continue; //Keine Eltern, nie welche gehabt - alles gut

        //Lücke im Stammbaum
        gint stelle = 0;

        gint letzte_vorfahren = tiefe;
        do
        {
            letzte_vorfahren--;
            gchar* element_n = info_element_laenge_n( element, letzte_vorfahren );
            stelle = info_suche_eltern( element_n, elemente );
            g_free( element_n );
        }
        while ( letzte_vorfahren > 1 && stelle == -1 );

        gint k = 0;
        for ( k = letzte_vorfahren; k < tiefe; k++ )
        {
            gchar* element_n = info_element_laenge_n( element, k );

            if ( stelle >= 0 )
            {
                g_ptr_array_insert( elemente, stelle + 1, element_n );
                g_ptr_array_insert( werte, stelle + 1, NULL );

                stelle++;
            }
            else
            {
                g_ptr_array_add( elemente, element_n );
                g_ptr_array_add( werte, NULL );
            }

            g_free( element_n );
        }
    }

    return;
}


static gint
info_gemeinsame_keys( gchar* vorlage, gchar* objekt )
{
    gchar* vorlage_keys = NULL;
    gchar* objekt_keys = NULL;
    gboolean weiter = FALSE;

    gint gemeinsame_keys = -1;
    do
    {
        gemeinsame_keys++;

        vorlage_keys = info_element_laenge_n( vorlage, gemeinsame_keys + 1 );
        objekt_keys = info_element_laenge_n( objekt, gemeinsame_keys + 1 );

        weiter = (!g_strcmp0( vorlage_keys, objekt_keys )) && vorlage_keys && objekt_keys;
        g_free( vorlage_keys );
        g_free( objekt_keys );
    }
    while ( weiter );

    return gemeinsame_keys;
}


static void
info_zusammenfuehren( GPtrArray* vorlage_elemente, GPtrArray* vorlage_werte,
        GPtrArray* objekt_elemente, GPtrArray* objekt_werte, gboolean einzelobjekt )
{
    gchar* objekt_element = NULL;
    gchar* objekt_wert = NULL;

    for ( gint i = 0; i < objekt_elemente->len; i++ )
    {
        gchar* vorlage_element = NULL;

        objekt_element = g_ptr_array_index( objekt_elemente, i );
        objekt_wert = g_ptr_array_index( objekt_werte, i );

        gint u = -1;
        gint gemeinsame_keys_max = 0;
        while ( u < (gint) (vorlage_elemente->len) )
        {
            u++;

            vorlage_element = g_ptr_array_index( vorlage_elemente, u );

            gint gemeinsame_keys = info_gemeinsame_keys( objekt_element,
                    vorlage_element );
            if ( gemeinsame_keys > gemeinsame_keys_max ) gemeinsame_keys_max =
                    gemeinsame_keys;

            if ( (!g_strcmp0( vorlage_element, objekt_element )) && (!g_ptr_array_index( vorlage_werte, u )) )
            {
                g_ptr_array_remove_index( vorlage_werte, u );
                g_ptr_array_insert( vorlage_werte, u, g_strdup( objekt_wert ) );

                break;
            }

            else if ( (gemeinsame_keys < gemeinsame_keys_max) ||
                    (u == (gint) vorlage_elemente->len) ||
                    (!g_strcmp0( vorlage_element, objekt_element )) )
            {
                if ( !einzelobjekt && (!g_strcmp0( vorlage_element, objekt_element )) ) break;

                g_ptr_array_insert( vorlage_elemente, u, g_strdup( objekt_element ) );
                g_ptr_array_insert( vorlage_werte, u, g_strdup( objekt_wert ) );

                break;
            }
        }
    }

    return;
}


static gint
info_objekt_elemente_und_werte_holen( Projekt* zond, gint node_id, GPtrArray*
        objekt_elemente, GPtrArray* objekt_werte, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_stmt* stmt = zond->stmts.info_objekt_elemente_und_werte_holen[0];

    sqlite3_reset( stmt );
    sqlite3_clear_bindings( stmt );

    rc = sqlite3_bind_int( stmt, 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    while ( (rc = sqlite3_step( stmt )) == SQLITE_ROW )
    {
        gchar* buf1 = (gchar*) sqlite3_column_text( stmt, 1 );
        if ( !buf1 ) buf1 = "";
        g_ptr_array_add( objekt_elemente, g_strdup( buf1 ) );

        gchar* buf2 = (gchar*) sqlite3_column_text( stmt, 2 );
        if ( !buf2 ) buf2 = "";
        g_ptr_array_add( objekt_werte, g_strdup( buf2 ) );
    }

    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" );

    return 0;
}


static GPtrArray*
info_elemente_vorlage_holen( void )
{
    gchar* vorlage =
            "<Herkunft>,"
            "<Herkunft><Fremd>,"
            "<Herkunft><Fremd><Absender>," // (LG Köln, Mandant, StA Buxtehude, ...)
            "<Herkunft><Fremd><Absendedatum>,"
            "<Herkunft><Fremd><Adressat>,"
            "<Herkunft><Fremd><Eingangsdatum>,"
            "<Herkunft><Fremd><Transportweg>,"
            "<Herkunft><Fremd><Medium>,"
            "<Herkunft><Fremd><Übergabeort>,"

            "<Herkunft><Eigen>,"
            "<Herkunft><Eigen><Status>," //Entwurf, abschließende Fassung
            "<Herkunft><Eigen><Ausgang>,"
            "<Herkunft><Eigen><Ausgang><Datum>,"
            "<Herkunft><Eigen><Ausgang><Adressat>,"
            "<Herkunft><Eigen><Ausgang><Transportweg>,"
            "<Herkunft><Eigen><Ausgang><Ankunftsdatum>,"

            "<Container>,"
            "<Container><Art>," //Datei, Aktenordner, Loseblatt, Konvolut
            "<Container><Bezeichnung>," //xxx.pdf, SLV I, ...

            "<Fundstelle>,"
            "<Fundstelle><Akte>,"
            "<Fundstelle><Akte><Behörde und Aktenzeichen>,"
            "<Fundstelle><Akte><Band>,"
            "<Fundstelle><Akte><Band><Bezeichnung>,"
            "<Fundstelle><Akte><Band><Nr.>,"
            "<Fundstelle><Akte><Band><Blatt von>,"
            "<Fundstelle><Akte><Band><Blatt bis>,"
            "<Fundstelle><Akte><Blatt von>,"
            "<Fundstelle><Akte><Blatt bis>,"
            "<Fundstelle><sonstige>,"

            "<Originalität>,"

            "<Datum>,"

            "<Bezeichnung>," //Vermerk, Observationsbericht, LiBi-Mappe

            "<Schlagwort/Titel>,"

            "<Urheber>,"
            "<Urheber><Kollektiv>,"
            "<Urheber><Person>,"

            "<Vernehmung>,"
            "<Vernehmung><Art>," //BES, ZEG, GES, VER
            "<Vernehmung><Auskunftsperson>,"
            "<Vernehmung><Auskunftsperson><Name>,"
            "<Vernehmung><Datum>,"
            "<Vernehmung><Uhrzeit Beginn>,"
            "<Vernehmung><Uhrzeit Ende>,"
            "<Vernehmung><Ort>,"

            "<Entscheidung>,"
            "<Entscheidung><Adressat>,"
            "<Entscheidung><Adressat><Name>,"
            "<Entscheidung><Adressat><Rolle>,"
            "<Entscheidung><Verfahren>,"
            "<Entscheidung><Verfahren><Aktenzeichen>,"
            "<Entscheidung><Verfahren><Gegenstand>,"
            "<Entscheidung><Bekanntmachung>,"
            "<Entscheidung><Bekanntmachung><Art>," //mündl. Verkündung, Zustellung, etc.
            "<Entscheidung><Bekanntmachung><Zeitpunkt>,"
            "<Entscheidung><Bekanntmachung><Zeitpunkt><Datum>,"
            "<Entscheidung><Bekanntmachung><Zeitpunkt><HV-Tag>,"
            "<Entscheidung><Inhalt>,"
            "<Entscheidung><Inhalt><Bezug>,"
            "<Entscheidung><Inhalt><Beschreibung>,"

            "<Eingabe>,"
            "<Eingabe><Anbringung>,"
            "<Eingabe><Anbringung><Datum>,"
            "<Eingabe><Anbringung><HV-Tag>,"
            "<Eingabe><Anbringung><Art>," //Verlesung, schriftlich, etc.
            "<Eingabe><Anbringender>,"
            "<Eingabe><Bezug>,"
            "<Eingabe><Ziel>,"
            "<Eingabe><Beweismittel>,"

            "<Sicherstellung>,"
            "<Sicherstellung><Datum>,"
            "<Sicherstellung><Asservatennr.>,"
            "<Sicherstellung><Anordnung>,"
            "<Sicherstellung><Ort>,"
            "<Sicherstellung><Gewahrsamsinhaber>,"
            "<Sicherstellung><Sicherstellender>,"
            "<Sicherstellung><Sicherstellender><Behörde>,"
            "<Sicherstellung><Sicherstellender><Person>,"

            "<Kommunikationsüberwachung>,"

            "<Lesezeichen>,"
            "<Lesezeichen><Stelle>,"
            "<Lesezeichen><Bezeichnung>,"

            "<tag>";

    gchar** elemente_zeilen = g_strsplit( vorlage, ",", -1 );

    GPtrArray* vorlage_elemente = g_ptr_array_new( );
    g_ptr_array_set_free_func( vorlage_elemente, (GDestroyNotify) g_free );

    gint i = 0;
    while ( elemente_zeilen[i] )
    {
        g_ptr_array_add( vorlage_elemente, (gpointer) g_strdup( elemente_zeilen[i] ) );
        i++;
    }

    g_strfreev( elemente_zeilen );

    return vorlage_elemente;
}


/*
<Bezeichnung>
    - Trennblatt
    - Fach
    - Vermerk
    - TKÜ-Protokoll
    - Vernehmungsprotokoll
    - Verfügung
    - Schreiben
    - Beschluß
    - Urteil
    - Lichtbildmappe
    - Wahllichtbildvorlage
    - Observationsbericht
    - Schreiben
    - Antrag
    - Beschluß
    - handschriftliche Notiz
    - Tabelle
    - TKÜ-Gesprächsaufzeichnung
    - IRÜ-Aufzeichnung
    - sonst. Audio
    - UFED-Datei
    - Video (nicht bei Medium=Papier)>
    - Bild
        <Aufnehmender>
        <Gegenstand>
        <...>


    <Observation>
        <Anordnung>
        <Beginn>
        <Ende>
        <Zielperson>

    <TKÜ-Ereignis>
        <Art> {Telefongespräch, SMS, FAX, WA, telegram, ...}
        <Bearbeitung>
            <Bearbeiter>
            <Bearbeitungsdatum>
        <Sprecher ZÜA>
        <Gegenstelle>
            <Rufnummer>
            <Sprecher>
        <Anfangszeitpunkt>
        <Endzeitpunkt>
        <Ltg.-Nr.>
        <Prod.-Nr.>
        <Korr.-Nr.>
        <Geodaten>

    <TKÜ-Maßnahme>
        <ZÜA>
            <Rufnummer>
            <IMEI>
            <Benutzerkonto>
            <Nutzer>
        <Anordnung>
            <wie> (Beschluß etc.)
            <wann>
            <durch>
        <Aufschaltung>
        <Abschaltung>

    <Lichtbild>
        <Aufnehmender>
        <Objekt>

    <Video>
        <Aufnehmender>
        <Objekt>

    <elektronische Auswertung>
        <Ausgewerteter Gegenstand>
            <Bezeichnung>
            <Asservatennr.>

*/


static gboolean
info_treemodel_foreach( GtkTreeModel* treemodel, GtkTreePath* path, GtkTreeIter*
        iter, gpointer user_data )
{
    struct Elemente_Werte
    {
        GPtrArray* elemente;
        GPtrArray* werte;
    };

    struct Elemente_Werte* elemente_werte;

    elemente_werte = (struct Elemente_Werte*) user_data;

    GPtrArray* array_elemente = elemente_werte->elemente;
    GPtrArray* array_werte = elemente_werte->werte;

    gchar* wert = NULL;
    gtk_tree_model_get( treemodel, iter, 1, &wert, -1 );

    if ( g_strcmp0( wert, "" ) && g_strcmp0( wert, "<...>" ) && wert != NULL )
    {
        gchar* element = info_element_zusammensetzen( treemodel, iter );
        g_ptr_array_add( array_elemente, (gpointer) element );

        g_ptr_array_add( array_werte, (gpointer) g_strdup( wert ) );
    }

    return FALSE;
}


static void
info_elemente_einlesen( GtkWidget* infofenster, GPtrArray* array_elemente,
        GPtrArray* array_werte )
{
    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data(
            G_OBJECT(infofenster), "treeview" );
    GtkTreeModel* treemodel = gtk_tree_view_get_model( treeview );

    struct Elemente_Werte
    {
        GPtrArray* elemente;
        GPtrArray* werte;
    } elemente_werte;

    elemente_werte.elemente = array_elemente;
    elemente_werte.werte = array_werte;

    gtk_tree_model_foreach( treemodel, (GtkTreeModelForeachFunc)
            info_treemodel_foreach, &elemente_werte );

    return;
}


gint
info_speichern( Projekt* zond, GtkWidget* infofenster, gchar** errmsg )
{
    GArray* array_objekte = (GArray*) g_object_get_data( G_OBJECT(infofenster),
            "array-objekte" );

    gint rc = 0;

/*  Beginn  */
    rc = db_begin( zond, errmsg );
    if ( rc ) ERROR_PAO( "db_begin" );

    //Wenn nur ein Knoten bearbeitet wurde: alles löschen
    if ( array_objekte->len == 1 )
    {
        sqlite3_reset( zond->stmts.info_speichern[0] );
        sqlite3_clear_bindings( zond->stmts.info_speichern[0] );

        rc = sqlite3_bind_int( zond->stmts.info_speichern[0], 1,
                g_array_index( array_objekte, gint, 0 ) );
        if ( rc != SQLITE_OK ) ERROR_SQL_ROLLBACK( "sqlite3_bind_int ([0])" )

        rc = sqlite3_step( zond->stmts.info_speichern[0] );
        if ( rc != SQLITE_DONE ) ERROR_SQL_ROLLBACK( "sqlite3_step ([0])" )
    }
    else //Sonst nur die ausdrücklich gelöschten Knoten
    {
        GPtrArray* array_geloeschte = g_object_get_data( G_OBJECT(infofenster),
                "array-geloeschte" );

        gchar* text = NULL;
        for ( gint u = 0; u < (gint) array_objekte->len; u++ )
        {
            for ( gint i = 0; i < (gint) array_geloeschte->len; i++ )
            {
                sqlite3_reset( zond->stmts.info_speichern[1] );
                sqlite3_clear_bindings( zond->stmts.info_speichern[1] );

                rc = sqlite3_bind_int( zond->stmts.info_speichern[1], 1,
                        g_array_index( array_objekte, gint, u ) );
                if ( rc != SQLITE_OK ) ERROR_SQL_ROLLBACK( "sqlite3_bind_int ([1])" )

                text = g_strconcat( (gchar*) g_ptr_array_index( array_geloeschte, i ), "%", NULL );
                rc = sqlite3_bind_text( zond->stmts.info_speichern[1], 2, text, -1, NULL );
                g_free( text );
                if ( rc != SQLITE_OK ) ERROR_SQL_ROLLBACK( "sqlite3_bind_text ([1])" )

                rc = sqlite3_step( zond->stmts.info_speichern[1] );
                if ( rc != SQLITE_DONE ) ERROR_SQL_ROLLBACK( "sqlite3_step ([1])" )
            }
        }
    }

    //Arrays für zu speichernde Wert-Paare erzeugen
    GPtrArray* array_elemente = g_ptr_array_new_with_free_func(
            (GDestroyNotify) g_free );
    GPtrArray* array_werte = g_ptr_array_new_with_free_func(
            (GDestroyNotify) g_free );

    info_elemente_einlesen( infofenster, array_elemente, array_werte );

    for ( gint i = 0; i < array_objekte->len; i++ )
    {
        for ( gint z = 0; z < array_elemente->len; z++ )
        {
            sqlite3_reset( zond->stmts.info_speichern[2] );
            sqlite3_clear_bindings( zond->stmts.info_speichern[2] );

            rc = sqlite3_bind_int( zond->stmts.info_speichern[2], 1,
                    g_array_index( array_objekte, gint, i ) );
            if ( rc != SQLITE_OK )
            {
                g_ptr_array_free( array_elemente, TRUE );
                g_ptr_array_free( array_werte, TRUE );

                ERROR_SQL_ROLLBACK( "sqlite3_bind_int ([2])" )
            }

            rc = sqlite3_bind_text( zond->stmts.info_speichern[2], 2,
                    (gchar*) g_ptr_array_index( array_elemente, z ), -1, NULL );
            if ( rc != SQLITE_OK )
            {
                g_ptr_array_free( array_elemente, TRUE );
                g_ptr_array_free( array_werte, TRUE );

                ERROR_SQL_ROLLBACK( "sqlite3_bind_text ([2])" )
            }

            rc = sqlite3_bind_text( zond->stmts.info_speichern[2], 3,
                    (gchar*) g_ptr_array_index( array_werte, z ), -1, NULL );
            if ( rc != SQLITE_OK )
            {
                g_ptr_array_free( array_elemente, TRUE );
                g_ptr_array_free( array_werte, TRUE );

                ERROR_SQL_ROLLBACK( "sqlite3_bind_text ([2])" )
            }

            rc = sqlite3_step( zond->stmts.info_speichern[2] );
            if ( rc != SQLITE_DONE )
            {
                g_ptr_array_free( array_elemente, TRUE );
                g_ptr_array_free( array_werte, TRUE );

                ERROR_SQL_ROLLBACK( "sqlite3_step ([2])" )
            }
        }
    }

    rc = db_commit( zond, errmsg );
    if ( rc )
    {
        g_ptr_array_free( array_elemente, TRUE );
        g_ptr_array_free( array_werte, TRUE );

        ERROR_PAO_ROLLBACK( "db_commit" );
    }

    //button_speichern ausgrauen
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(infofenster),
            "button_speichern" );
    gtk_widget_set_sensitive( button_speichern, FALSE );

    g_ptr_array_free( array_elemente, TRUE );
    g_ptr_array_free( array_werte, TRUE );

    return 0;
}


static gboolean
cb_window_delete_event( GtkWidget* window, GdkEvent* event, gpointer data )
{
    Projekt* zond = (Projekt*) g_object_get_data( G_OBJECT(window), "zond" );

    gchar* errmsg = NULL;

    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(window),
            "button_speichern" );
    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        rc = abfrage_frage( window, "Infofenster schließen", "Änderungen übernehmen?" );

        if ( rc == GTK_RESPONSE_YES )
        {
            rc = info_speichern( zond, window, &errmsg );
            if ( rc )
            {
                meldung( window, "Fehler in Schließen -\n\nBei Aufruf "
                        "info_speichern:\n", errmsg, NULL );
                g_free( errmsg );
                //ToDo: Abfrage "Trotzdem schließen"
            }
        }
        else if ( rc != GTK_RESPONSE_NO) return TRUE;
    }

    //array_geloeschte kaputt machen
    GPtrArray* array_geloeschte = g_object_get_data( G_OBJECT(window),
            "array-geloeschte" );
    g_ptr_array_free( array_geloeschte, TRUE );

    //node_id des infofensters aus open_info-array löschen
    GArray* array_objekte = g_object_get_data( G_OBJECT(window),
            "array-objekte" );

    for ( gint u = 0; u < array_objekte->len; u++ )
            for ( gint i = 0; i < zond->arr_info_nodes->len; i++ )
            if ( g_array_index( array_objekte, gint, u ) ==
            g_array_index( zond->arr_info_nodes, gint, i ) ) g_array_remove_index_fast(
            zond->arr_info_nodes, i );

    g_array_free( array_objekte, TRUE );

    return FALSE;
}


static void
cb_info_button_speichern( GtkButton* button, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    GtkWidget* infofenster = (GtkWidget*) user_data;

    Projekt* zond = (Projekt*) g_object_get_data( G_OBJECT(infofenster), "zond" );

    rc = info_speichern( zond, infofenster, &errmsg );
    if ( rc )
    {
        meldung( infofenster, "Fehler in Speichern info -\n\nBei Aufruf info_"
                "speichern", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
cb_button_abbrechen_clicked( GtkButton* button, gpointer infofenster )
{
    gboolean rc = cb_window_delete_event( (GtkWidget*) infofenster, NULL, NULL );

    if ( !rc ) gtk_widget_destroy( infofenster );

    return;
}


static void
cb_button_ok_clicked( GtkButton* button, gpointer infofenster )
{
    Projekt* zond = (Projekt*) g_object_get_data( G_OBJECT(infofenster), "zond" );

    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(infofenster),
            "button_speichern" );

    gint rc = 0;
    gchar* errmsg = NULL;

    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        rc = info_speichern( zond, (GtkWidget*) infofenster, &errmsg );
        if ( rc )
        {
            meldung( (GtkWidget*) infofenster, "Fehler in Ok -\n\nBei Aufruf "
                    "info_speichern:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }
    }

    //Nach (gelungenem) speichern ist button_speichern jedenfalls nicht sensitiv
    cb_window_delete_event( (GtkWidget*) infofenster, NULL, NULL );

    gtk_widget_destroy( GTK_WIDGET(infofenster) );

    return;
}


static void
cb_button_loeschen_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* infofenster = (GtkWidget*) user_data;

    GPtrArray* array_geloeschte = g_object_get_data( G_OBJECT(infofenster),
            "array-geloeschte" );

    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data( G_OBJECT(infofenster),
            "treeview" );
    GtkTreeModel* treemodel = gtk_tree_view_get_model( treeview );

    GtkTreeIter* iter = baum_abfragen_aktuellen_cursor( treeview );
    if ( !iter )
    {
        meldung( infofenster, "Kein Element ausgewählt", NULL );

        return;
    }

    gchar* element = NULL;
    element = info_element_zusammensetzen( treemodel, iter );

    g_ptr_array_add( array_geloeschte, (gpointer) g_strdup( element ) );
    g_free( element );

    gtk_tree_store_remove( GTK_TREE_STORE(treemodel), iter );

    gtk_tree_iter_free( iter );

    //button_speichern aktivieren
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(infofenster),
            "button_speichern" );
    gtk_widget_set_sensitive( button_speichern, TRUE );


    return;
}


static void
cb_button_duplizieren_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* infofenster = (GtkWidget*) user_data;

    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data( G_OBJECT(infofenster),
            "treeview" );
    GtkTreeStore* treestore = GTK_TREE_STORE(gtk_tree_view_get_model( treeview ));

    GtkTreeIter* iter_anchor = baum_abfragen_aktuellen_cursor( treeview );
    if ( !iter_anchor )
    {
        meldung( infofenster, "Kein Element ausgewählt", NULL );

        return;
    }

    gchar* element = NULL;
    gtk_tree_model_get( GTK_TREE_MODEL(treestore), iter_anchor, 0, &element, -1 );

    gboolean blatt = FALSE;
    blatt = info_row_wird_blatt( treeview, iter_anchor, element );
    if ( !blatt )
    {
        meldung( infofenster, "Element kann nicht eingefügt werden:\n\n"
            "Zu duplizierendes Element hat Kinder", NULL );
        g_free( element );
        gtk_tree_iter_free( iter_anchor );

        return;
    }

    GtkTreeIter* iter_new = baum_einfuegen_knoten(treeview, iter_anchor, FALSE );
    gtk_tree_iter_free( iter_anchor );

    gtk_tree_store_set( treestore, iter_new, 0, element, -1 );
    g_free( element );

    GtkTreePath* path = NULL;
    path = gtk_tree_model_get_path( GTK_TREE_MODEL(treestore), iter_new );
    gtk_tree_view_set_cursor( treeview, path, gtk_tree_view_get_column(
            treeview, 1 ), TRUE );
    gtk_tree_path_free( path );
    gtk_tree_iter_free( iter_new );

    return;
}


static gboolean
info_row_hat_zwilling( GtkTreeView* treeview, GtkTreeIter* iter, gchar*
        element )
{
    GtkTreeModel* model = gtk_tree_view_get_model( treeview );

    GtkTreeIter iter_parent;
    GtkTreeIter* p_iter_parent = NULL;
    if ( gtk_tree_model_iter_parent( model, &iter_parent, iter ) )
            p_iter_parent = &iter_parent;

    gint geschwister = 0;
    geschwister = gtk_tree_model_iter_n_children( model, p_iter_parent );

    if ( geschwister == 1 ) return FALSE; //Einzelkind

    gchar* element_vergleich = NULL;
    gint zaehler = 0;
    GtkTreeIter iter_vergleich;
    for ( gint i = 0; i < geschwister; i++ )
    {
        gtk_tree_model_iter_nth_child( model, &iter_vergleich, p_iter_parent, i );

        gtk_tree_model_get( model, &iter_vergleich, 0, &element_vergleich, -1 );
        if ( !g_strcmp0( element, element_vergleich ) )
        {
            zaehler++;
            if ( zaehler == 2 )
            {
                g_free( element_vergleich );

                return TRUE;
            }
        }

        g_free( element_vergleich );
    }

    return FALSE;
}


static gboolean
info_row_hat_vorfahren_mit_wert( GtkTreeModel* treemodel, GtkTreeIter* iter )
{
    GtkTreeIter iter_parent;
    GtkTreeIter* iter_temp = NULL;
    gchar* wert = NULL;

    iter_temp = gtk_tree_iter_copy( iter );

    while ( gtk_tree_model_iter_parent( treemodel, &iter_parent, iter_temp ) )
    {
        gtk_tree_iter_free( iter_temp );

        gtk_tree_model_get( treemodel, &iter_parent, 1, &wert, -1 );
        if ( wert ) return TRUE;

        iter_temp = gtk_tree_iter_copy( &iter_parent );
    }

    gtk_tree_iter_free( iter_temp );

    return FALSE;
}


static void
cb_button_element_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* infofenster = (GtkWidget*) user_data;

    GtkWidget* button_element = g_object_get_data( G_OBJECT(infofenster),
            "button_element" );
    GtkWidget* button_unterelement = g_object_get_data( G_OBJECT(infofenster),
            "button_unterelement" );

    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data( G_OBJECT(infofenster),
            "treeview" );
    GtkTreeStore* treestore = GTK_TREE_STORE( gtk_tree_view_get_model( treeview ));

    gboolean kind = (gboolean) GPOINTER_TO_INT(g_object_get_data(
            G_OBJECT(button), "kind" ));

    GtkTreeIter* iter_anchor = baum_abfragen_aktuellen_cursor( treeview );
    if ( !iter_anchor )
    {
        meldung( infofenster, "Kein Element ausgewählt", NULL );

        return;
    }

    GtkTreeIter iter_new;

    if ( info_row_hat_vorfahren_mit_wert( GTK_TREE_MODEL(treestore), iter_anchor ) )
    {
        meldung( infofenster, "Element einfügen nicht möglich - "
                "Zu Vorfahren Wert gespeichert", NULL );

        return;
    }

    if ( kind )
    {
        gchar* eltern_wert = NULL;
        gchar* eltern_element = NULL;
        gtk_tree_model_get( GTK_TREE_MODEL(treestore), iter_anchor, 0,
                &eltern_element, 1, &eltern_wert, -1 );
        if ( eltern_wert != NULL || info_row_hat_zwilling( treeview, iter_anchor,
                eltern_element ) )
        {
            if ( eltern_wert != NULL ) meldung( infofenster,
                    "Einfügen Unterelement nicht möglich:\n\nElement darf "
                    "keinen Wert haben", NULL );
            else meldung(infofenster, "Einfügen Unterelement nicht möglich:\n\n"
                    "Element darf keinen Zwilling haben", NULL );

            g_free( eltern_wert );
            g_free( eltern_element );
            gtk_tree_iter_free( iter_anchor );

            return;
        }

        gtk_tree_store_insert( treestore, &iter_new, iter_anchor, -1 );
    }
    else gtk_tree_store_insert_after( treestore, &iter_new, NULL, iter_anchor );

    gtk_tree_iter_free( iter_anchor );

    gtk_widget_set_sensitive( button_element, FALSE );
    gtk_widget_set_sensitive( button_unterelement, FALSE );

    GtkCellRenderer* renderer = (GtkCellRenderer*) g_object_get_data(
            G_OBJECT(infofenster), "renderer_text1" );

    g_object_set( G_OBJECT(renderer), "editable", TRUE, NULL );

    GtkTreePath* path = NULL;
    path = gtk_tree_model_get_path( GTK_TREE_MODEL(treestore), &iter_new );
    if ( kind ) gtk_tree_view_expand_to_path( treeview, path );
    gtk_tree_view_set_cursor( treeview, path, gtk_tree_view_get_column(
            treeview, 0 ), TRUE );
    gtk_tree_path_free( path );

    return;
}


static void
cb_cell_element_edited( GtkCellRendererText* renderer, gchar* path_string,
        gchar* text, gpointer user_data )
{
    GtkWidget* infofenster = (GtkWidget*) user_data;

    //Editierbarkeit wieder ausschalten
    g_object_set( G_OBJECT(renderer), "editable", FALSE, NULL );

    //buttons wieder einschalten
    GtkWidget* button_element = g_object_get_data( G_OBJECT(infofenster),
            "button_element" );
    GtkWidget* button_unterelement = g_object_get_data( G_OBJECT(infofenster),
            "button_unterelement" );

    gtk_widget_set_sensitive( button_element, TRUE );
    gtk_widget_set_sensitive( button_unterelement, TRUE );

    //Eingabe üverprüfen
    GtkTreeView* treeview = (GtkTreeView*) g_object_get_data(
            G_OBJECT(infofenster), "treeview" );

    GtkTreeModel* treemodel = gtk_tree_view_get_model( treeview );

    GtkTreeIter iter;
    if ( !gtk_tree_model_get_iter_from_string( treemodel, &iter, path_string) )
    {
        meldung( infofenster, "Element kann nicht eingefügt werden:\n\n"
                "Fehler bei Aufruf gtk_tree_model_get_iter_from_string", NULL );

        return;
    }

    gboolean blatt = FALSE;
    blatt = info_row_wird_blatt( treeview, &iter, text );
    if ( g_strcmp0( text, "" ) && blatt )
    {
        //Eingabe abspeichern
        gtk_tree_store_set( GTK_TREE_STORE(treemodel), &iter, 0, text, -1 );
        gtk_tree_view_columns_autosize( treeview );

        //button_speichern aktivieren
        GtkWidget* button_speichern = g_object_get_data( G_OBJECT(infofenster),
                "button_speichern" );
        gtk_widget_set_sensitive( button_speichern, TRUE );

        //zum info-Feld in gleichr Zeile springen
        GtkTreePath* path = gtk_tree_path_new_from_string( (const gchar*)
                path_string );
        gtk_tree_view_set_cursor( treeview, path, gtk_tree_view_get_column(
                treeview, 1 ), TRUE );
        gtk_tree_path_free( path );
    }
    else gtk_tree_store_remove( GTK_TREE_STORE(treemodel), &iter );

    if ( !blatt ) meldung( infofenster, "Element kann nicht eingefügt werden:\n\n"
            "Gleichnamiges Element mit Kindern existiert bereits", NULL );

    return;
}


static void
cb_cell_wert_edited( GtkCellRendererText* renderer, gchar* path, gchar* text,
        gpointer user_data )
{
    GtkWidget* infofenster = (GtkWidget*) user_data;
    GtkWidget* treeview = (GtkWidget*) g_object_get_data( G_OBJECT(infofenster),
            "treeview" );

    GtkTreeModel* treemodel = gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) );

    //Eingabe abspeichern
    GtkTreeIter iter;
    gtk_tree_model_get_iter_from_string( treemodel, &iter, path);

    if ( !g_strcmp0( text, "" ) ) text = NULL;
    gtk_tree_store_set( GTK_TREE_STORE(treemodel), &iter, 1, text, -1 );
    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(treeview) );

    //button_speichern aktivieren
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(infofenster),
            "button_speichern" );
    gtk_widget_set_sensitive( button_speichern, TRUE );

    //zum nächsten Element springen
    gchar* element = info_element_zusammensetzen( treemodel, &iter );

    //In Liste?
    if ( !g_strcmp0( element, "<Fundstelle><Akte><Blatt von>" ) )
            info_springen_zu_element( treeview, "<Fundstelle><Akte><Blatt bis>" );
    if ( !g_strcmp0( element, "<Fundstelle><Akte><Blatt bis>" ) )
            info_springen_zu_element( treeview, "<Datum>" );
    if ( !g_strcmp0( element, "<Datum>" ) )
            info_springen_zu_element( treeview, "<Bezeichnung>" );
    if ( !g_strcmp0( element, "<Bezeichnung>" ) )
            info_springen_zu_element( treeview, "<Schlagwort/Titel>" );
    if ( !g_strcmp0( element, "<Schlagwort/Titel>" ) )
            info_springen_zu_element( treeview, "<Urheber><Kollektiv>" );
    if ( !g_strcmp0( element, "<Urheber><Kollektiv>" ) )
            info_springen_zu_element( treeview, "<Urheber><Person>" );
    if ( !g_strcmp0( element, "<Urheber><Person>" ) )
            info_springen_zu_element( treeview, "<tag>" );

    g_free( element );

    return;
}


static void
gtk_tree_cell_data_func( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* treemodel, GtkTreeIter* iter, gpointer user_data )
{
    if ( !gtk_tree_model_iter_has_child( treemodel, iter ) ) g_object_set( renderer,
            "editable", TRUE, NULL );
    else g_object_set( renderer, "editable", FALSE, NULL );

    gchar* wert = NULL;
    gtk_tree_model_get( treemodel, iter, 1, &wert, -1 );
    if ( !g_strcmp0( wert, "<...>" ) ) g_object_set( renderer, "sensitive",
        FALSE, NULL );
    else g_object_set( renderer, "sensitive", TRUE, NULL );

    return;
}


static GtkWidget*
info_fenster_oeffnen( Projekt* zond )
{
    //Fenster erzeugen
    GtkWidget* infofenster = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(infofenster), 500, 950 );

    GtkAccelGroup* accel_group = gtk_accel_group_new( );
    gtk_window_add_accel_group( GTK_WINDOW(infofenster), accel_group );

    //Headerbar
    GtkWidget* infofenster_headerbar = gtk_header_bar_new( );

    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(infofenster_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(infofenster_headerbar), "Info-Fenster" );

    gtk_window_set_titlebar( GTK_WINDOW(infofenster), infofenster_headerbar );

    // Fenster teilen
    GtkWidget* vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(infofenster), vbox );
    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), swindow, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), hbox, FALSE, FALSE, 0 );

    //Treeview-Widget
    GtkWidget* treeview = gtk_tree_view_new( );

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(treeview), FALSE );
    gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW(treeview), TRUE );
    gtk_tree_view_set_reorderable( GTK_TREE_VIEW(treeview), FALSE );
    gtk_tree_view_set_enable_tree_lines( GTK_TREE_VIEW(treeview), TRUE );

    //die renderer
    GtkCellRenderer* renderer_text1 = gtk_cell_renderer_text_new();
    GtkCellRenderer* renderer_text2 = gtk_cell_renderer_text_new();

    //die column
    GtkTreeViewColumn* column1 = gtk_tree_view_column_new();
    GtkTreeViewColumn* column2 = gtk_tree_view_column_new();
    gtk_tree_view_column_set_resizable( column1, TRUE );
    gtk_tree_view_column_set_sizing( column1, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_resizable( column2, TRUE );
    gtk_tree_view_column_set_sizing(column2, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_cell_data_func( column2, renderer_text2,
            (GtkTreeCellDataFunc) gtk_tree_cell_data_func, NULL, NULL );

    gtk_tree_view_column_pack_start( column1, renderer_text1, TRUE );
    gtk_tree_view_column_pack_start( column2, renderer_text2, TRUE );

    gtk_tree_view_column_set_attributes(column1, renderer_text1, "text", 0, NULL);
    gtk_tree_view_column_set_attributes(column2, renderer_text2, "text", 1, NULL);

    gtk_tree_view_append_column( GTK_TREE_VIEW(treeview), column1 );
    gtk_tree_view_append_column( GTK_TREE_VIEW(treeview), column2 );

    gtk_container_add( GTK_CONTAINER(swindow), treeview );

    //buttons in hbox in unterem Teil der vbox
    GtkWidget* button_element = gtk_button_new_with_label( "Neues Element" );
    GtkWidget* button_unterelement = gtk_button_new_with_label( "Neues Unterelement" );
    g_object_set_data( G_OBJECT(button_unterelement), "kind", GINT_TO_POINTER(1) );
    GtkWidget* button_duplizieren = gtk_button_new_with_label( "Element duplizieren" );
    GtkWidget* button_loeschen = gtk_button_new_with_label( "Element löschen" );
    GtkWidget* button_speichern = gtk_button_new_with_label( "Übernehmen" );
    gtk_widget_set_sensitive( button_speichern, FALSE );

    GtkWidget* button_ok = gtk_button_new_with_label( "Ok" );
    gtk_widget_add_accelerator(button_ok, "clicked", accel_group, GDK_KEY_Return,
            GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);

    GtkWidget* button_abbrechen = gtk_button_new_with_label( "Abbrechen" );

    gtk_box_pack_start( GTK_BOX(hbox), button_element, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), button_unterelement, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), button_duplizieren, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), button_loeschen, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), button_speichern, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), button_ok, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), button_abbrechen, FALSE, FALSE, 0 );

    //treestore erzeugen
    GtkTreeStore* treestore = gtk_tree_store_new( 2, G_TYPE_STRING, G_TYPE_STRING );

    //treestore zu treeview hinzufügen
    gtk_tree_view_set_model( GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(treestore) );

    //zond in Fenster schreiben
    g_object_set_data( G_OBJECT(infofenster), "zond", (gpointer) zond );

    //button_speichern in Fenster schreiben
    g_object_set_data( G_OBJECT(infofenster), "button_speichern", (gpointer)
            button_speichern );

    //treeview in Fenster schreiben
    g_object_set_data( G_OBJECT(infofenster), "treeview", (gpointer) treeview );

    //renderer_text1 in infofenster
    g_object_set_data( G_OBJECT(infofenster), "renderer_text1", (gpointer)
            renderer_text1 );

    //Buttons für neue ELemente in Fenster
    g_object_set_data( G_OBJECT(infofenster), "button_element", (gpointer)
            button_element );
    g_object_set_data( G_OBJECT(infofenster), "button_unterelement", (gpointer)
            button_unterelement );

/*
**  Signale verknüpfen  */
    //Zelle editiert
    g_signal_connect( renderer_text2, "edited", G_CALLBACK(cb_cell_wert_edited),
            (gpointer) infofenster );

    g_signal_connect( renderer_text1, "edited", G_CALLBACK(cb_cell_element_edited),
            (gpointer) infofenster );

    //Button hinzufügen
    g_signal_connect( button_element, "clicked",
            G_CALLBACK(cb_button_element_clicked), (gpointer) infofenster );
    g_signal_connect( button_unterelement, "clicked",
            G_CALLBACK(cb_button_element_clicked), (gpointer) infofenster );

    //button duplizieren
    g_signal_connect( button_duplizieren, "clicked",
            G_CALLBACK(cb_button_duplizieren_clicked), (gpointer) infofenster );

    //button loeschen
    g_signal_connect( button_loeschen, "clicked",
            G_CALLBACK(cb_button_loeschen_clicked), (gpointer) infofenster );

    //Button speichern
    g_signal_connect( button_speichern, "clicked",
            G_CALLBACK(cb_info_button_speichern), (gpointer) infofenster );

    //Button ok
    g_signal_connect( button_ok, "clicked",
            G_CALLBACK(cb_button_ok_clicked), (gpointer) infofenster );

    //Button abbrechen
    g_signal_connect( button_abbrechen, "clicked",
            G_CALLBACK(cb_button_abbrechen_clicked), (gpointer) infofenster );

    //X angeclickt
    g_signal_connect( infofenster, "delete-event",
            G_CALLBACK(cb_window_delete_event), NULL );

    return infofenster;
}


static gboolean
info_objekte_bereits_geoeffnet( Projekt* zond, GArray* array_objekte )
{
    for ( gint u = 0; u < array_objekte->len; u++ )
        for ( gint i = 0; i < zond->arr_info_nodes->len; i++ )
        {
            if ( g_array_index( array_objekte, gint, u ) ==
                    g_array_index( zond->arr_info_nodes, gint, i ) ) return TRUE;
        }
    return FALSE;
}


static GArray*
info_auslesen_selection( Projekt* zond, gboolean einzelobjekt, gchar** errmsg )
{
    GList* selected = NULL;
    GList* list = NULL;
    gint node_id = 0;

    Baum baum = KEIN_BAUM;
    baum = baum_abfragen_aktiver_treeview( zond );

    selected = gtk_tree_selection_get_selected_rows(
            zond->selection[baum], NULL );
    if ( !selected )
    {
        if ( errmsg ) *errmsg = g_strdup( "Keine Punkte ausgewählt" );

        return NULL;
    }

    //array_objekte = Zeiger auf die objekt_ids, zu denen gespeichert werden soll
    GArray* array_objekte = g_array_new( FALSE, FALSE, sizeof ( gint ) );

    //Selection auslesen
    list = selected;
    do //alle rows aus der Liste
    {
        if ( einzelobjekt ) node_id =
                baum_abfragen_aktuelle_node_id( zond->treeview[baum] );
        else node_id = baum_abfragen_node_id( zond->treeview[baum],
                list->data, errmsg );

        if ( node_id == -1 )
        {
            g_array_free( array_objekte, TRUE );
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

            ERROR_PAO_R( "baum_abfragen_(aktuelle)node_id", NULL );
        }

        if ( baum == BAUM_AUSWERTUNG )
        {
            node_id = db_get_ref_id( zond, node_id, errmsg );
            if ( node_id < 0 )
            {
                g_array_free( array_objekte, TRUE );
                g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

                ERROR_PAO_R( "db_get_ref_id", NULL )
            }
        }

        //Falls noch nicht: überprüfen, ob Datei oder Ziel
                                            //!BAUM_INHALT weil node_id auf BAUM_INHALT umgerechnet wurde!
        gint typ = db_knotentyp_abfragen( zond, BAUM_INHALT, node_id, errmsg );
        if ( typ == -1 )
        {
            g_array_free( array_objekte, TRUE );
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

            ERROR_PAO_R( "db_knotentyp_abfragen", NULL )
        }
        if ( typ == 1 || typ == 2 ) g_array_append_val( array_objekte,
                node_id );

        list = list->next;
    }
    while ( (list = list->next) && (!einzelobjekt) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return array_objekte;
}


gint
info_bearbeiten( Projekt* zond, gboolean einzelobjekt, gchar** errmsg )
{
    gint rc = 0;
    gchar* errmsg_ii = NULL;

    GArray* array_objekte = info_auslesen_selection( zond, einzelobjekt, &errmsg_ii );
    if ( !array_objekte )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf info_auslesen_"
                "selection:\n", errmsg_ii, NULL );
        g_free( errmsg_ii );

        return -1;
    }

    if ( array_objekte->len == 0 )
    {
        meldung( zond->app_window, "Fehler - Info bearbeiten\n\n"
                "Keine Knoten mit Anbindung ausgewählt", NULL );

        return 0;
    }

    if ( info_objekte_bereits_geoeffnet( zond, array_objekte ) )
    {
        meldung( zond->app_window, "Fehler - Info bearbeiten\n\n"
                "Infos zu ausgewähltem Objekt bereits in Bearbeitung", NULL );

        return 0;
    }

    GtkWidget* infofenster = info_fenster_oeffnen( zond );

    g_object_set_data( G_OBJECT(infofenster), "array-objekte", array_objekte );

    //Auswahl in geöffnete Objekte schreiben
    for ( gint i = 0; i < array_objekte->len; i++ )
            g_array_append_val( zond->arr_info_nodes, g_array_index( array_objekte,
            gint, i ) );

    //Array für gelöschte Zeilen erzeugen und an Fenster hängen
    GPtrArray* array_geloeschte =
            g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    g_object_set_data( G_OBJECT(infofenster), "array-geloeschte",
            (gpointer) array_geloeschte );

    //Vorlage einlesen
    GPtrArray* vorlage_elemente = info_elemente_vorlage_holen( );

    //vorlage_werte erzeugen und mit NULL auffüllen
    GPtrArray* vorlage_werte = g_ptr_array_new_with_free_func(
            (GDestroyNotify) g_free );
    for ( gint i = 0; i < (gint) vorlage_elemente->len; i++ )
            g_ptr_array_add( vorlage_werte, NULL );

//in tabelle infos zur objekt_id gespeicherte elemente und werte holen
    //zunächst arrays erzeugen
    GPtrArray* objekt_elemente = g_ptr_array_new_with_free_func(
            (GDestroyNotify) g_free );
    GPtrArray* objekt_werte = g_ptr_array_new_with_free_func(
            (GDestroyNotify) g_free );

    //Dann zu allen objekten die in info-Tabelle gespeicherten Daten holen
    for ( gint i = 0; i < array_objekte->len; i++ )
    {
        rc = info_objekt_elemente_und_werte_holen( zond,
                g_array_index( array_objekte, gint, i ), objekt_elemente,
                objekt_werte, &errmsg_ii );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf info_objekt_"
                    "elemente_und_werte_holen:\n", errmsg_ii, NULL );
            g_free( errmsg_ii );

            //alles kaputtmachen
            gtk_widget_destroy( infofenster );

            //zond->open_info bereinigen
            gint other_node = 0;
            gint this_node = 0;
            for ( gint i = 0; i < zond->arr_info_nodes->len; i++ )
            {
                other_node = g_array_index( zond->arr_info_nodes, gint, i );
                for ( gint u = 0; u < array_objekte->len; u++ )
                {
                    this_node = g_array_index( array_objekte, gint, u );
                    if ( other_node == this_node ) g_array_remove_index_fast(
                            zond->arr_info_nodes, i );
                }
            }

            //arrays löschen
            g_array_free( array_objekte, TRUE );
            g_ptr_array_free( objekt_elemente, TRUE );
            g_ptr_array_free( objekt_werte, TRUE );

            return -1;
        }
    }

    info_zusammenfuehren( vorlage_elemente, vorlage_werte,
            objekt_elemente, objekt_werte, einzelobjekt );

    g_ptr_array_free( objekt_elemente, TRUE );
    g_ptr_array_free( objekt_werte, TRUE );

    info_erzeugen_fehlende_vorfahren( vorlage_elemente, vorlage_werte );

    info_store_fuellen( infofenster, vorlage_elemente, vorlage_werte );

    g_ptr_array_free( vorlage_elemente, TRUE );
    g_ptr_array_free( vorlage_werte, TRUE );

    info_tree_view_anzeigen( infofenster );

    info_springen_zu_element( g_object_get_data( G_OBJECT(infofenster),
            "treeview" ), "<Fundstelle><Akte><Blatt von>" );

    return 0;
}

