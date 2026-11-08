/* The layer as a picture, and the solid the way Cura shows it.
 * The triangles are the scene. A chip draws them when gfopengpu.library
 * says so (OGPU_ONGPU). This file's painter is the CPU drawer, the same
 * scene, used when there is no chip.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_VIEW_H
#define GS_VIEW_H

#include "gs.h"

int gs_view_paint(const char *gcode, int len, unsigned char *rgb, int w, int h);

typedef struct {
    float x0, y0, x1, y1;
} gs_line;

int gs_view_lines(const char *gcode, int len, gs_line **out, int *n,
                  float *minx, float *miny, float *maxx, float *maxy);

/* Orbit like a slicer. Yaw turns the model. Pitch tilts it. Distance
 * zooms. Pan is in pixels of the view. */
typedef struct {
    float yaw, pitch, dist, panx, pany;
} gs_cam;

void gs_cam_home(gs_cam *c);
void gs_cam_orbit(gs_cam *c, float dx, float dy);
void gs_cam_pan(gs_cam *c, float dx, float dy);
void gs_cam_zoom(gs_cam *c, float factor);

/* Shaded mesh into rgb, w*h*3. 0, or -1. The bed stays dark. */
int gs_view_solid(const gs_mesh *m, const gs_cam *cam, unsigned char *rgb, int w, int h);

/* 1 when a graphics chip would draw this scene. 0 on a machine with none,
 * where gs_view_solid is what the window shows. */
int gs_view_on_gpu(void);

#endif
