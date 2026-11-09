/* The solid as an OpenGPU batch. The triangles are the mesh's. A chip
 * draws them with OGPU_OP_TRIANGLES; the CPU back end draws the same
 * words. This file does not call the library.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_GPU_H
#define GS_GPU_H

#include "gs_view.h"

#include <stdint.h>

/* Six words a vertex, the 3D form with OGPU_LAY_Z:
 * x, y as 16.16, ARGB32, u, v as 16.16, z as u32 (1 is far, all-ones is
 * near). cap is how many vertices fit. Returns how many were written, or -1. */
int gs_gpu_vertices(const gs_mesh *m, const gs_cam *cam, int w, int h,
                    uint32_t *verts, int cap);

/* The same vertices, fitted to the plate instead of the part. */
int gs_gpu_vertices_plate(const gs_mesh *m, const gs_cam *cam, int w, int h,
                          uint32_t *verts, int cap);

/* The stream that clears an ARGB32 picture and a Z32 depth, draws the bed
 * grid (grid != 0), then the vertices. picture_bpr is the picture's row
 * stride in bytes. Addresses are the Amiga's, already 32-bit.
 * Returns the number of words, or -1. */
int gs_gpu_stream(uint32_t *stream, int cap,
                  uint32_t vert_addr, int nverts,
                  uint32_t picture, int picture_bpr,
                  uint32_t depth, int w, int h,
                  const gs_mesh *m, const gs_cam *cam, int grid);

/* plate is not 0: the grid is the editor's plate, and the vertices must
 * be the ones from gs_gpu_vertices_plate. */
int gs_gpu_stream_plate(uint32_t *stream, int cap,
                        uint32_t vert_addr, int nverts,
                        uint32_t picture, int picture_bpr,
                        uint32_t depth, int w, int h,
                        const gs_mesh *m, const gs_cam *cam, int grid, int plate);

#endif
