/* Adjust a mesh the way a small STL editor does: move, turn, scale,
 * drop it on the bed, centre it, lay a face flat. Undo keeps a copy from
 * before each change. This is not a translation of any editor's source.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_EDIT_H
#define GS_EDIT_H

#include "gs_view.h"

/* Replace dst with a copy of src. 0, or -1. */
int ge_copy(gs_mesh *dst, const gs_mesh *src);

void ge_move(gs_mesh *m, float dx, float dy, float dz);

/* About the centre of the box. Radians, X then Y then Z. */
void ge_rotate(gs_mesh *m, float rx, float ry, float rz);

/* About the centre of the box. 1 leaves it. */
void ge_scale(gs_mesh *m, float sx, float sy, float sz);

/* The lowest point onto z = 0. */
void ge_drop(gs_mesh *m);

/* The middle of the box onto the middle of the plate. */
void ge_centre(gs_mesh *m);

/* The biggest triangle, or -1. */
int ge_largest_face(const gs_mesh *m);

/* That face's outside points down, and the face sits on z = 0. 0, or -1. */
int ge_lay_face(gs_mesh *m, int face);

/* The front triangle under a pixel of the plate view, or -1. */
int ge_pick(const gs_mesh *m, const gs_cam *cam, int w, int h, int x, int y);

/* Up to 32 copies. */
typedef struct ge_hist ge_hist;

ge_hist *ge_hist_new(void);
void ge_hist_free(ge_hist *h);
int ge_hist_push(ge_hist *h, const gs_mesh *m);
int ge_hist_pop(ge_hist *h, gs_mesh *m);
int ge_hist_depth(const ge_hist *h);

#endif
