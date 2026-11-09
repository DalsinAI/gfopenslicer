/* Solids the way a small CAD does them: a box, a cylinder, and the three
 * ways of joining two solids. The triangles are a gs_mesh, so the slicer's
 * view and the chip's triangle op can draw them without a second scene.
 *
 * The operations are JSCAD's (cube, cylinder, union, subtract, intersect).
 * This file is not a translation of that source.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_MODEL_H
#define GS_MODEL_H

#include "gs.h"

/* Replaces m. 0, or -1. The box starts at (x, y, z) and measures sx, sy, sz. */
int gm_cube(gs_mesh *m, float x, float y, float z, float sx, float sy, float sz);

/* Axis along Z, centred on (x, y), from z up by h. seg is the number of
 * sides, at least 3. */
int gm_cylinder(gs_mesh *m, float x, float y, float z, float r, float h, int seg);

/* dst = a and b joined. a and b are not changed. 0, or -1. */
int gm_union(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b);

/* dst = a with b cut out. a and b are not changed. 0, or -1. */
int gm_subtract(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b);

/* dst = only the overlap of a and b. 0, or -1. An empty result has no triangles. */
int gm_intersect(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b);

/* Binary STL, little-endian, which is what a desktop slicer writes. */
int gm_stl_write(const gs_mesh *m, const char *path);

#endif
