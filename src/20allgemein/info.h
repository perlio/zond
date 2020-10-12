#ifndef INFO_H_INCLUDED
#define INFO_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;

typedef int gint;
typedef char gchar;
typedef int gboolean;


gint info_set_node_text( Projekt*, Baum, gint, gchar** );

gint info_bearbeiten( Projekt*, gboolean, gchar** );

#endif // INFO_H_INCLUDED
