#ifndef GENERAL_H_INCLUDED
#define GENERAL_H_INCLUDED

#include "../global_types.h"

#include <stdio.h>

typedef struct _GSList GSList;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkWidget GtkWidget;
typedef struct TessBaseAPI TessBaseAPI;
typedef struct TessResultRenderer TessResultRenderer;

typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef const void* gconstpointer;


/* String-Utils */
gchar* full_path_to_rel_path( Projekt*, const gchar* );

gchar* utf8_to_local_filename( const gchar* );

gint string_to_guint( const gchar*, guint* );

gchar* add_string( gchar*, gchar* );

gchar* prepend_string( gchar*, gchar* );

/* Sonst. */
GSList* choose_files( GtkWindow*, const gchar*, gchar*, gint, gboolean );

gchar* filename_speichern( GtkWindow*, const gchar* );

gchar* filename_oeffnen( GtkWindow* );

void meldung( GtkWidget*, const gchar*, ... );

gint abfrage_frage(GtkWidget*, const gchar*, const gchar*, gchar** );

gint dialog_with_buttons( GtkWidget*, const gchar*, const gchar*, gchar**,
        gchar*, ... );

gint hat_vorfahre_datei( Projekt*, Baum, gint, gboolean, gchar** );

gint knoten_verschieben( Projekt*, Baum, gint, gint, gint, gchar** );

void ziele_free( Ziel* );

gint abfragen_rel_path_and_anbindung( Projekt*, Baum, gint, gchar**, Anbindung**, gchar** );

gboolean is_pdf( const gchar* );

gboolean is_white( const char* );

/*  info_window  */
void info_window_scroll( InfoWindow* );

void info_window_close( InfoWindow* );

void info_window_set_message( InfoWindow*, const gchar* );

InfoWindow* info_window_open( GtkWidget*, const gchar* );

#endif // GENERAL_H_INCLUDED

