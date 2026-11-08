/* CPU drawer for the solid. The same triangles a chip draws through
 * gfopengpu.library when OGPU_ONGPU is set. There is no chip here.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void gs_cam_home(gs_cam *c)
{
    c->yaw = 0.9f;
    c->pitch = 0.55f;
    c->dist = 1.f;
    c->panx = 0.f;
    c->pany = 0.f;
}

void gs_cam_orbit(gs_cam *c, float dx, float dy)
{
    c->yaw += dx;
    c->pitch += dy;
    if (c->pitch > 1.4f) c->pitch = 1.4f;
    if (c->pitch < -1.4f) c->pitch = -1.4f;
}

void gs_cam_pan(gs_cam *c, float dx, float dy)
{
    c->panx += dx;
    c->pany += dy;
}

void gs_cam_zoom(gs_cam *c, float factor)
{
    c->dist *= factor;
    if (c->dist < 0.25f) c->dist = 0.25f;
    if (c->dist > 6.f) c->dist = 6.f;
}

int gs_view_on_gpu(void)
{
    return 0;
}

typedef struct {
    float x, y, z;
} v3;

static v3 xform(gs_v p, const gs_cam *c, gs_v o)
{
    float x = p.x - o.x, y = p.y - o.y, z = p.z - o.z;
    float cy = cosf(c->yaw), sy = sinf(c->yaw);
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    float x1 = x * cy - y * sy;
    float y1 = x * sy + y * cy;
    float y2 = y1 * cp - z * sp;
    float z2 = y1 * sp + z * cp;
    v3 r;
    r.x = x1;
    r.y = z2;
    r.z = y2;
    return r;
}

static void put(unsigned char *rgb, int w, int h, int x, int y, int lit)
{
    unsigned char *p;
    if (lit < 0) lit = 0;
    if (lit > 255) lit = 255;
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
    p[0] = (unsigned char)(242 * lit / 255);
    p[1] = (unsigned char)(176 * lit / 255);
    p[2] = (unsigned char)(66 * lit / 255);
}

static void fill(unsigned char *rgb, float *zb, int w, int h, v3 a, v3 b, v3 c, int lit)
{
    int minx, miny, maxx, maxy, x, y;
    float area, inv;
    minx = (int)a.x;
    if ((int)b.x < minx) minx = (int)b.x;
    if ((int)c.x < minx) minx = (int)c.x;
    miny = (int)a.y;
    if ((int)b.y < miny) miny = (int)b.y;
    if ((int)c.y < miny) miny = (int)c.y;
    maxx = (int)a.x;
    if ((int)b.x > maxx) maxx = (int)b.x;
    if ((int)c.x > maxx) maxx = (int)c.x;
    maxy = (int)a.y;
    if ((int)b.y > maxy) maxy = (int)b.y;
    if ((int)c.y > maxy) maxy = (int)c.y;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= w) maxx = w - 1;
    if (maxy >= h) maxy = h - 1;
    area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area > -0.5f && area < 0.5f) return;
    inv = 1.f / area;
    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) * inv;
            float w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) * inv;
            float w2 = 1.f - w0 - w1;
            float z;
            int i;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
            z = w0 * a.z + w1 * b.z + w2 * c.z;
            i = y * w + x;
            if (z <= zb[i]) continue;
            zb[i] = z;
            put(rgb, w, h, x, y, lit);
        }
    }
}

int gs_view_solid(const gs_mesh *m, const gs_cam *cam, unsigned char *rgb, int w, int h)
{
    gs_cam home;
    const gs_cam *c = cam;
    gs_v o;
    v3 *p;
    float *zb;
    float minx, miny, maxx, maxy, s, ox, oy;
    int i, n;
    if (!rgb || w < 4 || h < 4) return -1;
    if (!c) {
        gs_cam_home(&home);
        c = &home;
    }
    for (i = 0; i < w * h; i++) {
        rgb[i * 3] = 20;
        rgb[i * 3 + 1] = 22;
        rgb[i * 3 + 2] = 28;
    }
    if (!m || m->n < 1 || !m->t) return 0;
    p = (v3 *)malloc((size_t)m->n * 3 * sizeof *p);
    zb = (float *)malloc((size_t)w * (size_t)h * sizeof *zb);
    if (!p || !zb) {
        free(p);
        free(zb);
        return -1;
    }
    for (i = 0; i < w * h; i++) zb[i] = -1e30f;
    o.x = (m->min.x + m->max.x) * 0.5f;
    o.y = (m->min.y + m->max.y) * 0.5f;
    o.z = (m->min.z + m->max.z) * 0.5f;
    minx = miny = 1e30f;
    maxx = maxy = -1e30f;
    for (i = 0; i < m->n; i++) {
        p[i * 3] = xform(m->t[i].a, c, o);
        p[i * 3 + 1] = xform(m->t[i].b, c, o);
        p[i * 3 + 2] = xform(m->t[i].c, c, o);
        for (n = 0; n < 3; n++) {
            if (p[i * 3 + n].x < minx) minx = p[i * 3 + n].x;
            if (p[i * 3 + n].y < miny) miny = p[i * 3 + n].y;
            if (p[i * 3 + n].x > maxx) maxx = p[i * 3 + n].x;
            if (p[i * 3 + n].y > maxy) maxy = p[i * 3 + n].y;
        }
    }
    if (maxx - minx < 0.01f) maxx = minx + 0.01f;
    if (maxy - miny < 0.01f) maxy = miny + 0.01f;
    s = (float)(w - 36) / (maxx - minx);
    if ((float)(h - 36) / (maxy - miny) < s) s = (float)(h - 36) / (maxy - miny);
    s *= c->dist;
    ox = ((float)w - (maxx - minx) * s) * 0.5f - minx * s + c->panx;
    oy = ((float)h - (maxy - miny) * s) * 0.5f - miny * s + c->pany;
    for (i = 0; i < m->n; i++) {
        v3 a = p[i * 3], b = p[i * 3 + 1], d = p[i * 3 + 2];
        float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        float vx = d.x - a.x, vy = d.y - a.y, vz = d.z - a.z;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float len, nd, lit;
        a.x = ox + a.x * s;
        a.y = (float)h - (oy + a.y * s);
        b.x = ox + b.x * s;
        b.y = (float)h - (oy + b.y * s);
        d.x = ox + d.x * s;
        d.y = (float)h - (oy + d.y * s);
        len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len < 1e-8f) continue;
        nx /= len;
        ny /= len;
        nz /= len;
        nd = nx * -0.35f + ny * 0.82f + nz * 0.45f;
        if (nd < 0.f) nd = -nd;
        lit = 70.f + 185.f * nd;
        fill(rgb, zb, w, h, a, b, d, (int)lit);
    }
    free(p);
    free(zb);
    return 0;
}
