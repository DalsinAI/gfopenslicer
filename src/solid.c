/* CPU drawer for the solid. The same triangles a chip draws through
 * gfopengpu.library when OGPU_ONGPU is set. There is no chip here.
 *
 * The camera is a turntable: yaw around Z, pitch to tilt, distance to
 * zoom. Pan moves the look-at in the screen plane, scaled by the distance,
 * which is the rule in Mikola Lysenko's orbit-camera (MIT, 2013), written
 * again here. Cura and PrusaSlicer do the same turntable; their code is
 * AGPL and is not in this file.
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
    /* dx, dy are fractions of the window. Dividing by dist makes the same
     * drag move fewer millimetres when the camera is closer. */
    c->panx += dx / c->dist;
    c->pany += dy / c->dist;
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

static void put_rgb(unsigned char *rgb, int w, int h, int x, int y, int r, int g, int b)
{
    unsigned char *p;
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
    p[0] = (unsigned char)r;
    p[1] = (unsigned char)g;
    p[2] = (unsigned char)b;
}

static void fill(unsigned char *rgb, float *zb, int w, int h, v3 a, v3 b, v3 c, int cr, int cg, int cb)
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
            put_rgb(rgb, w, h, x, y, cr, cg, cb);
        }
    }
}

static void hair(unsigned char *rgb, float *zb, int w, int h, float x0, float y0, float z0, float x1, float y1, float z1)
{
    int steps, i;
    float dx = x1 - x0, dy = y1 - y0;
    steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    if (steps < 1) steps = 1;
    for (i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        int x = (int)(x0 + dx * t);
        int y = (int)(y0 + dy * t);
        float z = z0 + (z1 - z0) * t;
        int k;
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        k = y * w + x;
        if (z <= zb[k]) continue;
        zb[k] = z;
        put_rgb(rgb, w, h, x, y, 70, 78, 92);
    }
}

int gs_view_frame(const gs_mesh *m, const gs_cam *cam, int w, int h, gs_frame *f)
{
    float minx, miny, maxx, maxy, zmin, zmax;
    int i, n;
    if (!f || !cam || w < 4 || h < 4) return -1;
    f->w = w;
    f->h = h;
    f->ox = f->oy = f->s = 0.f;
    f->zmin = 0.f;
    f->zmax = 1.f;
    f->origin.x = f->origin.y = f->origin.z = 0.f;
    if (!m || m->n < 1 || !m->t) return -1;
    f->origin.x = (m->min.x + m->max.x) * 0.5f;
    f->origin.y = (m->min.y + m->max.y) * 0.5f;
    f->origin.z = (m->min.z + m->max.z) * 0.5f;
    minx = miny = zmin = 1e30f;
    maxx = maxy = zmax = -1e30f;
    for (i = 0; i < m->n; i++) {
        gs_v p[3];
        p[0] = m->t[i].a;
        p[1] = m->t[i].b;
        p[2] = m->t[i].c;
        for (n = 0; n < 3; n++) {
            v3 r = xform(p[n], cam, f->origin);
            if (r.x < minx) minx = r.x;
            if (r.y < miny) miny = r.y;
            if (r.z < zmin) zmin = r.z;
            if (r.x > maxx) maxx = r.x;
            if (r.y > maxy) maxy = r.y;
            if (r.z > zmax) zmax = r.z;
        }
    }
    if (maxx - minx < 0.01f) maxx = minx + 0.01f;
    if (maxy - miny < 0.01f) maxy = miny + 0.01f;
    f->s = (float)(w - 36) / (maxx - minx);
    if ((float)(h - 36) / (maxy - miny) < f->s) f->s = (float)(h - 36) / (maxy - miny);
    f->s *= cam->dist;
    /* Pan is a fraction of the fitted model, so a closer view moves less
     * of the bed. Applied after the fit, or the fit would cancel it. */
    f->ox = ((float)w - (maxx - minx) * f->s) * 0.5f - minx * f->s + cam->panx * (maxx - minx) * f->s;
    f->oy = ((float)h - (maxy - miny) * f->s) * 0.5f - miny * f->s + cam->pany * (maxy - miny) * f->s;
    f->zmin = zmin;
    f->zmax = zmax;
    return 0;
}

int gs_view_frame_bed(const gs_mesh *m, const gs_cam *cam, int w, int h,
                      float x0, float y0, float x1, float y1, gs_frame *f)
{
    float minx, miny, maxx, maxy, zmin, zmax, zh, xs[2], ys[2];
    int i, a, b;
    if (!f || !cam || w < 4 || h < 4) return -1;
    if (x1 < x0) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0) {
        float t = y0;
        y0 = y1;
        y1 = t;
    }
    f->w = w;
    f->h = h;
    f->origin.x = (x0 + x1) * 0.5f;
    f->origin.y = (y0 + y1) * 0.5f;
    f->origin.z = 0.f;
    zh = (x1 - x0) > (y1 - y0) ? (x1 - x0) : (y1 - y0);
    if (zh < 1.f) zh = 1.f;
    minx = miny = zmin = 1e30f;
    maxx = maxy = zmax = -1e30f;
    xs[0] = x0;
    xs[1] = x1;
    ys[0] = y0;
    ys[1] = y1;
    for (a = 0; a < 2; a++) {
        for (b = 0; b < 2; b++) {
            float zz;
            for (i = 0; i < 2; i++) {
                gs_v p;
                v3 r;
                zz = i ? zh : 0.f;
                p.x = xs[a];
                p.y = ys[b];
                p.z = zz;
                r = xform(p, cam, f->origin);
                if (r.x < minx) minx = r.x;
                if (r.y < miny) miny = r.y;
                if (r.z < zmin) zmin = r.z;
                if (r.x > maxx) maxx = r.x;
                if (r.y > maxy) maxy = r.y;
                if (r.z > zmax) zmax = r.z;
            }
        }
    }
    if (m && m->t) {
        for (i = 0; i < m->n; i++) {
            gs_v p[3];
            int n;
            p[0] = m->t[i].a;
            p[1] = m->t[i].b;
            p[2] = m->t[i].c;
            for (n = 0; n < 3; n++) {
                v3 r = xform(p[n], cam, f->origin);
                if (r.z < zmin) zmin = r.z;
                if (r.z > zmax) zmax = r.z;
            }
        }
    }
    if (maxx - minx < 0.01f) maxx = minx + 0.01f;
    if (maxy - miny < 0.01f) maxy = miny + 0.01f;
    f->s = (float)(w - 36) / (maxx - minx);
    if ((float)(h - 36) / (maxy - miny) < f->s) f->s = (float)(h - 36) / (maxy - miny);
    f->s *= cam->dist;
    f->ox = ((float)w - (maxx - minx) * f->s) * 0.5f - minx * f->s + cam->panx * (maxx - minx) * f->s;
    f->oy = ((float)h - (maxy - miny) * f->s) * 0.5f - miny * f->s + cam->pany * (maxy - miny) * f->s;
    f->zmin = zmin;
    f->zmax = zmax;
    return 0;
}

void gs_view_map(const gs_frame *f, const gs_cam *cam, gs_v p, float *x, float *y, float *z)
{
    v3 r = xform(p, cam, f->origin);
    *x = f->ox + r.x * f->s;
    *y = (float)f->h - (f->oy + r.y * f->s);
    *z = r.z;
}

int gs_view_colour(const gs_cam *cam, gs_v origin, gs_v a, gs_v b, gs_v c, int *r, int *g, int *bl)
{
    v3 pa = xform(a, cam, origin);
    v3 pb = xform(b, cam, origin);
    v3 pc = xform(c, cam, origin);
    float ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z;
    float vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float len, nd, lit;
    float ax, ay, az;
    int cr, cg, cb;
    len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len < 1e-8f) return -1;
    nx /= len;
    ny /= len;
    nz /= len;
    nd = nx * -0.35f + ny * 0.82f + nz * 0.45f;
    if (nd < 0.f) nd = -nd;
    lit = 0.45f + 0.55f * nd;
    ax = fabsf(nx);
    ay = fabsf(ny);
    az = fabsf(nz);
    if (az >= ax && az >= ay) {
        cr = 230;
        cg = 190;
        cb = 80;
    } else if (ax >= ay) {
        cr = 196;
        cg = 84;
        cb = 62;
    } else {
        cr = 64;
        cg = 150;
        cb = 168;
    }
    *r = (int)(cr * lit);
    *g = (int)(cg * lit);
    *bl = (int)(cb * lit);
    return 0;
}

int gs_view_solid(const gs_mesh *m, const gs_cam *cam, unsigned char *rgb, int w, int h)
{
    gs_cam home;
    const gs_cam *c = cam;
    gs_frame f;
    float *zb;
    int i;
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
    if (gs_view_frame(m, c, w, h, &f)) return 0;
    zb = (float *)malloc((size_t)w * (size_t)h * sizeof *zb);
    if (!zb) return -1;
    for (i = 0; i < w * h; i++) zb[i] = -1e30f;
    {
        int g;
        float x0 = m->min.x, x1 = m->max.x, y0 = m->min.y, y1 = m->max.y, z = m->min.z;
        float dx = (x1 - x0) * 0.2f, dy = (y1 - y0) * 0.2f;
        x0 -= dx;
        x1 += dx;
        y0 -= dy;
        y1 += dy;
        for (g = 0; g <= 4; g++) {
            float u = x0 + (x1 - x0) * (float)g / 4.f;
            float v = y0 + (y1 - y0) * (float)g / 4.f;
            gs_v wa, wb;
            float ax, ay, az, bx, by, bz;
            wa = (gs_v){u, y0, z};
            wb = (gs_v){u, y1, z};
            gs_view_map(&f, c, wa, &ax, &ay, &az);
            gs_view_map(&f, c, wb, &bx, &by, &bz);
            hair(rgb, zb, w, h, ax, ay, az, bx, by, bz);
            wa = (gs_v){x0, v, z};
            wb = (gs_v){x1, v, z};
            gs_view_map(&f, c, wa, &ax, &ay, &az);
            gs_view_map(&f, c, wb, &bx, &by, &bz);
            hair(rgb, zb, w, h, ax, ay, az, bx, by, bz);
        }
    }
    for (i = 0; i < m->n; i++) {
        float ax, ay, az, bx, by, bz, cx, cy, cz;
        int cr, cg, cb;
        v3 a, b, d;
        if (gs_view_colour(c, f.origin, m->t[i].a, m->t[i].b, m->t[i].c, &cr, &cg, &cb)) continue;
        gs_view_map(&f, c, m->t[i].a, &ax, &ay, &az);
        gs_view_map(&f, c, m->t[i].b, &bx, &by, &bz);
        gs_view_map(&f, c, m->t[i].c, &cx, &cy, &cz);
        a.x = ax;
        a.y = ay;
        a.z = az;
        b.x = bx;
        b.y = by;
        b.z = bz;
        d.x = cx;
        d.y = cy;
        d.z = cz;
        fill(rgb, zb, w, h, a, b, d, cr, cg, cb);
    }
    free(zb);
    return 0;
}

int gs_view_bed(const gs_mesh *m, const gs_cam *cam, unsigned char *rgb, int w, int h)
{
    gs_cam home;
    const gs_cam *c = cam;
    gs_frame f;
    float *zb;
    int i;
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
    if (gs_view_frame_bed(m, c, w, h, 0.f, 0.f, GS_BED, GS_BED, &f)) return -1;
    zb = (float *)malloc((size_t)w * (size_t)h * sizeof *zb);
    if (!zb) return -1;
    for (i = 0; i < w * h; i++) zb[i] = -1e30f;
    {
        int g;
        for (g = 0; g <= 4; g++) {
            float u = GS_BED * (float)g / 4.f;
            float v = GS_BED * (float)g / 4.f;
            gs_v wa, wb;
            float ax, ay, az, bx, by, bz;
            wa = (gs_v){u, 0.f, 0.f};
            wb = (gs_v){u, GS_BED, 0.f};
            gs_view_map(&f, c, wa, &ax, &ay, &az);
            gs_view_map(&f, c, wb, &bx, &by, &bz);
            hair(rgb, zb, w, h, ax, ay, az, bx, by, bz);
            wa = (gs_v){0.f, v, 0.f};
            wb = (gs_v){GS_BED, v, 0.f};
            gs_view_map(&f, c, wa, &ax, &ay, &az);
            gs_view_map(&f, c, wb, &bx, &by, &bz);
            hair(rgb, zb, w, h, ax, ay, az, bx, by, bz);
        }
    }
    if (m && m->t) {
        for (i = 0; i < m->n; i++) {
            float ax, ay, az, bx, by, bz, cx, cy, cz;
            int cr, cg, cb;
            v3 a, b, d;
            if (gs_view_colour(c, f.origin, m->t[i].a, m->t[i].b, m->t[i].c, &cr, &cg, &cb)) continue;
            gs_view_map(&f, c, m->t[i].a, &ax, &ay, &az);
            gs_view_map(&f, c, m->t[i].b, &bx, &by, &bz);
            gs_view_map(&f, c, m->t[i].c, &cx, &cy, &cz);
            a.x = ax;
            a.y = ay;
            a.z = az;
            b.x = bx;
            b.y = by;
            b.z = bz;
            d.x = cx;
            d.y = cy;
            d.z = cz;
            fill(rgb, zb, w, h, a, b, d, cr, cg, cb);
        }
    }
    free(zb);
    return 0;
}
