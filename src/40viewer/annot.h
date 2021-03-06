#ifndef ANNOT_H_INCLUDED
#define ANNOT_H_INCLUDED

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _PV_Annot_Page PVAnnotPage;
typedef struct _PV_Annot PVAnnot;

typedef int gint;
typedef char gchar;


void annot_free_pv_annot_page( PVAnnotPage* );

void annot_load_pv_annot_page( DocumentPage* );

gint annot_create( DocumentPage*, fz_quad*, gint, gchar** );

gint annot_delete( DisplayedDocument*, gint, PVAnnot*, gchar** );

#endif //ANNOT_H
