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

/* A turntable, which is how a slicer looks at a bed. Yaw turns around Z.
 * Pitch tilts. Distance zooms. Pan is a fraction of the view, and it moves
 * the point we look at in the plane of the screen, by more when the camera
 * is further away: that rule is orbit-camera by Mikola Lysenko, MIT, 2013,
 * written again here. A free tumble is not used, so the bed stays up. */
typedef struct {
    float yaw, pitch, dist, panx, pany;
} gs_cam;

void gs_cam_home(gs_cam *c);
void gs_cam_orbit(gs_cam *c, float dx, float dy);
void gs_cam_pan(gs_cam *c, float dx, float dy);
void gs_cam_zoom(gs_cam *c, float factor);

/* How gs_view_solid fitted this mesh into the picture. Depth is the
 * painter's: a larger z is closer. */
typedef struct {
    float ox, oy, s, zmin, zmax;
    gs_v origin;
    int w, h;
} gs_frame;

/* 0, or -1 when there is nothing to fit. cam is not NULL. */
int gs_view_frame(const gs_mesh *m, const gs_cam *cam, int w, int h, gs_frame *f);

/* Screen x, y (y down) and the painter's depth. */
void gs_view_map(const gs_frame *f, const gs_cam *cam, gs_v p, float *x, float *y, float *z);

/* The face colour gs_view_solid uses. 0, or -1 for an empty triangle. */
int gs_view_colour(const gs_cam *cam, gs_v origin, gs_v a, gs_v b, gs_v c, int *r, int *g, int *blue);

/* Shaded mesh into rgb, w*h*3. 0, or -1. The bed stays dark. */
int gs_view_solid(const gs_mesh *m, const gs_cam *cam, unsigned char *rgb, int w, int h);

/* The editor's plate, in millimetres, from (0, 0) to (GS_BED, GS_BED).
 * The picture is fitted to the plate, not the part, so a move shows. */
#define GS_BED 60.f

/* Fit x0,y0 .. x1,y1. The mesh may be empty. 0, or -1. */
int gs_view_frame_bed(const gs_mesh *m, const gs_cam *cam, int w, int h,
                      float x0, float y0, float x1, float y1, gs_frame *f);

/* The plate and the mesh. 0, or -1. */
int gs_view_bed(const gs_mesh *m, const gs_cam *cam, unsigned char *rgb, int w, int h);

/* 1 when a graphics chip would draw this scene. 0 on a machine with none,
 * where gs_view_solid is what the window shows. */
int gs_view_on_gpu(void);

#endif
