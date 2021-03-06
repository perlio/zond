#ifndef ZIELE_H_INCLUDED
#define ZIELE_H_INCLUDED

#include "../global_types.h"

typedef int gint;
typedef char gchar;
typedef int gboolean;


gboolean ziele_1_gleich_2( const Anbindung, const Anbindung );

gint ziele_abfragen_anker_rek( Projekt*, gint, Anbindung, gboolean*, gchar** );

gint ziele_erzeugen_anbindung( Projekt*, GtkWidget*, const DisplayedDocument*, Anbindung, gchar** );

#endif // ZIELE_H_INCLUDED
