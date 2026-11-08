/* The layer as a picture. The window's view and the host preview both
 * call this. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_VIEW_H
#define GS_VIEW_H

/* Paints one layer's G-code into rgb, w*h*3, top to bottom. The bed is
 * dark. A travel is not drawn. An extrusion is. Returns how many extrusion
 * lines were drawn, or -1. */
int gs_view_paint(const char *gcode, int len, unsigned char *rgb, int w, int h);

typedef struct {
    float x0, y0, x1, y1;
} gs_line;

/* The extrusion lines only. *out is allocated. 0, or -1. */
int gs_view_lines(const char *gcode, int len, gs_line **out, int *n,
                  float *minx, float *miny, float *maxx, float *maxy);

#endif
