/* OpenGPU words for the solid. The library is not linked here: the Amiga
 * program submits the buffer this writes.
 *
 * RENDER3D's compare word is Warp3D's W3D_Z_* , which stream3d.h says to
 * pass through unchanged. The published header numbers them
 * NEVER 1, LESS 2, GEQUAL 3, LEQUAL 4, GREATER 5, NOTEQUAL 6, EQUAL 7,
 * ALWAYS 8. The painter keeps the larger depth, so the test is GREATER
 * and a cleared depth of 0 loses to every fragment.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_gpu.h"

#include <gfopengpu/stream.h>
#include <gfopengpu/stream3d.h>

#define GS_Z_GREATER 5u

static uint32_t fix16(float v)
{
    double x = (double)v * 65536.0;
    if (x > 2147483647.0) x = 2147483647.0;
    if (x < -2147483648.0) x = -2147483648.0;
    return (uint32_t)(int32_t)x;
}

static uint32_t depth_of(const gs_frame *f, float z)
{
    double span = (double)f->zmax - (double)f->zmin;
    double t = span > 1e-8 ? ((double)z - (double)f->zmin) / span : 1.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return 1u + (uint32_t)(t * 4294967294.0);
}

static uint32_t argb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int verts_frame(const gs_mesh *m, const gs_cam *cam, const gs_frame *f, uint32_t *verts, int cap)
{
    int i, n = 0;
    if (!m) return -1;
    for (i = 0; i < m->n; i++) {
        gs_v p[3];
        float x, y, z;
        int r, g, b, k;
        uint32_t colour;
        p[0] = m->t[i].a;
        p[1] = m->t[i].b;
        p[2] = m->t[i].c;
        if (gs_view_colour(cam, f->origin, p[0], p[1], p[2], &r, &g, &b)) continue;
        if (n + 3 > cap) return -1;
        colour = argb(r, g, b);
        for (k = 0; k < 3; k++) {
            gs_view_map(f, cam, p[k], &x, &y, &z);
            verts[(n + k) * 6] = fix16(x);
            verts[(n + k) * 6 + 1] = fix16(y);
            verts[(n + k) * 6 + 2] = colour;
            verts[(n + k) * 6 + 3] = 0;
            verts[(n + k) * 6 + 4] = 0;
            verts[(n + k) * 6 + 5] = depth_of(f, z);
        }
        n += 3;
    }
    return n;
}

int gs_gpu_vertices(const gs_mesh *m, const gs_cam *cam, int w, int h, uint32_t *verts, int cap)
{
    gs_frame f;
    if (!verts || cap < 3 || !cam) return -1;
    if (gs_view_frame(m, cam, w, h, &f)) return -1;
    return verts_frame(m, cam, &f, verts, cap);
}

int gs_gpu_vertices_plate(const gs_mesh *m, const gs_cam *cam, int w, int h, uint32_t *verts, int cap)
{
    gs_frame f;
    if (!verts || cap < 3 || !cam) return -1;
    if (gs_view_frame_bed(m, cam, w, h, 0.f, 0.f, GS_BED, GS_BED, &f)) return -1;
    return verts_frame(m, cam, &f, verts, cap);
}

static int put(uint32_t *s, int *n, int cap, uint32_t w)
{
    if (*n >= cap) return -1;
    s[(*n)++] = w;
    return 0;
}

static int grid(uint32_t *s, int *n, int cap, const gs_mesh *m, const gs_cam *cam, int w, int h, int plate)
{
    gs_frame f;
    int g;
    float x0, x1, y0, y1, z, dx, dy;
    if (plate) {
        if (gs_view_frame_bed(m, cam, w, h, 0.f, 0.f, GS_BED, GS_BED, &f)) return 0;
        x0 = 0.f;
        x1 = GS_BED;
        y0 = 0.f;
        y1 = GS_BED;
        z = 0.f;
        dx = 0.f;
        dy = 0.f;
    } else {
        if (!m || gs_view_frame(m, cam, w, h, &f)) return 0;
        x0 = m->min.x;
        x1 = m->max.x;
        y0 = m->min.y;
        y1 = m->max.y;
        z = m->min.z;
        dx = (x1 - x0) * 0.2f;
        dy = (y1 - y0) * 0.2f;
    }
    x0 -= dx;
    x1 += dx;
    y0 -= dy;
    y1 += dy;
    for (g = 0; g <= 4; g++) {
        float u = x0 + (x1 - x0) * (float)g / 4.f;
        float v = y0 + (y1 - y0) * (float)g / 4.f;
        float sx[2], sy[2];
        gs_v a, b;
        float ax, ay, az, bx, by, bz;
        int k;
        a = (gs_v){u, y0, z};
        b = (gs_v){u, y1, z};
        gs_view_map(&f, cam, a, &ax, &ay, &az);
        gs_view_map(&f, cam, b, &bx, &by, &bz);
        sx[0] = ax;
        sy[0] = ay;
        sx[1] = bx;
        sy[1] = by;
        a = (gs_v){x0, v, z};
        b = (gs_v){x1, v, z};
        gs_view_map(&f, cam, a, &ax, &ay, &az);
        gs_view_map(&f, cam, b, &bx, &by, &bz);
        (void)az;
        (void)bz;
        for (k = 0; k < 2; k++) {
            float x0s, y0s, x1s, y1s;
            if (k == 0) {
                x0s = sx[0];
                y0s = sy[0];
                x1s = sx[1];
                y1s = sy[1];
            } else {
                x0s = ax;
                y0s = ay;
                x1s = bx;
                y1s = by;
            }
            if (put(s, n, cap, OGPU_HDR(OGPU_OP_LINE, 5)) ||
                put(s, n, cap, OGPU_XY((int)x0s, (int)y0s)) ||
                put(s, n, cap, OGPU_XY((int)x1s, (int)y1s)) ||
                put(s, n, cap, 0xFF464E5Cu) ||
                put(s, n, cap, OGPU_JAM1))
                return -1;
        }
    }
    return 0;
}

int gs_gpu_stream(uint32_t *stream, int cap, uint32_t vert_addr, int nverts,
                  uint32_t picture, int picture_bpr, uint32_t depth, int w, int h,
                  const gs_mesh *m, const gs_cam *cam, int grid_on)
{
    return gs_gpu_stream_plate(stream, cap, vert_addr, nverts, picture, picture_bpr, depth, w, h, m, cam, grid_on, 0);
}

int gs_gpu_stream_plate(uint32_t *stream, int cap, uint32_t vert_addr, int nverts,
                        uint32_t picture, int picture_bpr, uint32_t depth, int w, int h,
                        const gs_mesh *m, const gs_cam *cam, int grid_on, int plate)
{
    uint32_t args[OGPU_R3D_ARGS];
    int n = 0, i;
    if (!stream || cap < 48 || nverts < 3 || w < 4 || h < 4 || picture_bpr < w * 4) return -1;
    if (put(stream, &n, cap, OGPU_HDR(OGPU_OP_SURFACE, 6)) ||
        put(stream, &n, cap, 0) ||
        put(stream, &n, cap, picture) ||
        put(stream, &n, cap, (uint32_t)picture_bpr) ||
        put(stream, &n, cap, OGPU_XY(w, h)) ||
        put(stream, &n, cap, OGPU_FMT_ARGB32) ||
        put(stream, &n, cap, OGPU_HDR(OGPU_OP_TARGET, 2)) ||
        put(stream, &n, cap, 0) ||
        put(stream, &n, cap, OGPU_HDR(OGPU_OP_DEPTH, 6)) ||
        put(stream, &n, cap, OGPU_KIND_DEPTH) ||
        put(stream, &n, cap, depth) ||
        put(stream, &n, cap, (uint32_t)w * 4u) ||
        put(stream, &n, cap, OGPU_XY(w, h)) ||
        put(stream, &n, cap, OGPU_FMT_Z32) ||
        put(stream, &n, cap, OGPU_HDR(OGPU_OP_CLEAR3D, 5)) ||
        put(stream, &n, cap, OGPU_KIND_COLOUR) ||
        put(stream, &n, cap, OGPU_XY(0, 0)) ||
        put(stream, &n, cap, OGPU_XY(w, h)) ||
        put(stream, &n, cap, 0xFF14161Cu) ||
        put(stream, &n, cap, OGPU_HDR(OGPU_OP_CLEAR3D, 5)) ||
        put(stream, &n, cap, OGPU_KIND_DEPTH) ||
        put(stream, &n, cap, OGPU_XY(0, 0)) ||
        put(stream, &n, cap, OGPU_XY(w, h)) ||
        put(stream, &n, cap, 0))
        return -1;
    if (grid_on && cam && grid(stream, &n, cap, m, cam, w, h, plate)) return -1;
    for (i = 0; i < OGPU_R3D_ARGS; i++) args[i] = 0;
    args[0] = OGPU_R3D_GOURAUD | OGPU_R3D_ZTEST | OGPU_R3D_ZWRITE;
    args[1] = GS_Z_GREATER;
    args[5] = 0xF; /* write R, G, B and A */
    if (put(stream, &n, cap, OGPU_HDR(OGPU_OP_RENDER3D, 1 + OGPU_R3D_ARGS))) return -1;
    for (i = 0; i < OGPU_R3D_ARGS; i++)
        if (put(stream, &n, cap, args[i])) return -1;
    if (put(stream, &n, cap, OGPU_HDR(OGPU_OP_TRIANGLES, 10)) ||
        put(stream, &n, cap, vert_addr) ||
        put(stream, &n, cap, (uint32_t)nverts) ||
        put(stream, &n, cap, 24) ||
        put(stream, &n, cap, 0) ||
        put(stream, &n, cap, 0) ||
        put(stream, &n, cap, 0) ||
        put(stream, &n, cap, OGPU_TRI_NOTEX) ||
        put(stream, &n, cap, 0) ||
        put(stream, &n, cap, OGPU_LAY_LIST | OGPU_LAY_Z))
        return -1;
    return n;
}
