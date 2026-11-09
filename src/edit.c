/* Move, turn, scale, and lay a face on the bed. The copies for undo are
 * ours. No other editor's source is in here.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_edit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HIST 32

struct ge_hist {
    gs_mesh slot[HIST];
    int n;
};

static void bound(gs_mesh *m)
{
    int i, k;
    if (!m->n || !m->t) {
        m->min.x = m->min.y = m->min.z = 0.f;
        m->max = m->min;
        return;
    }
    m->min = m->max = m->t[0].a;
    for (i = 0; i < m->n; i++) {
        gs_v p[3];
        p[0] = m->t[i].a;
        p[1] = m->t[i].b;
        p[2] = m->t[i].c;
        for (k = 0; k < 3; k++) {
            if (p[k].x < m->min.x) m->min.x = p[k].x;
            if (p[k].y < m->min.y) m->min.y = p[k].y;
            if (p[k].z < m->min.z) m->min.z = p[k].z;
            if (p[k].x > m->max.x) m->max.x = p[k].x;
            if (p[k].y > m->max.y) m->max.y = p[k].y;
            if (p[k].z > m->max.z) m->max.z = p[k].z;
        }
    }
}

static gs_v mid(const gs_mesh *m)
{
    gs_v c;
    c.x = (m->min.x + m->max.x) * 0.5f;
    c.y = (m->min.y + m->max.y) * 0.5f;
    c.z = (m->min.z + m->max.z) * 0.5f;
    return c;
}

static void spin_about(gs_mesh *m, gs_v c, float ax, float ay, float az, float ang)
{
    float co, si, len;
    int i, k;
    len = sqrtf(ax * ax + ay * ay + az * az);
    if (len < 1e-8f || (ang > -1e-8f && ang < 1e-8f)) return;
    ax /= len;
    ay /= len;
    az /= len;
    co = cosf(ang);
    si = sinf(ang);
    for (i = 0; i < m->n; i++) {
        gs_v *p[3];
        p[0] = &m->t[i].a;
        p[1] = &m->t[i].b;
        p[2] = &m->t[i].c;
        for (k = 0; k < 3; k++) {
            float vx = p[k]->x - c.x, vy = p[k]->y - c.y, vz = p[k]->z - c.z;
            float cx = ay * vz - az * vy;
            float cy = az * vx - ax * vz;
            float cz = ax * vy - ay * vx;
            float d = ax * vx + ay * vy + az * vz;
            p[k]->x = c.x + vx * co + cx * si + ax * d * (1.f - co);
            p[k]->y = c.y + vy * co + cy * si + ay * d * (1.f - co);
            p[k]->z = c.z + vz * co + cz * si + az * d * (1.f - co);
        }
    }
    bound(m);
}

static int face_nrm(const gs_tri *t, gs_v *n)
{
    float ux = t->b.x - t->a.x, uy = t->b.y - t->a.y, uz = t->b.z - t->a.z;
    float vx = t->c.x - t->a.x, vy = t->c.y - t->a.y, vz = t->c.z - t->a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float L = sqrtf(nx * nx + ny * ny + nz * nz);
    if (L < 1e-12f) return -1;
    n->x = nx / L;
    n->y = ny / L;
    n->z = nz / L;
    return 0;
}

int ge_copy(gs_mesh *dst, const gs_mesh *src)
{
    gs_tri *t = NULL;
    int n = 0;
    if (!dst) return -1;
    if (src && src->n > 0 && src->t) {
        n = src->n;
        t = (gs_tri *)malloc((size_t)n * sizeof *t);
        if (!t) return -1;
        memcpy(t, src->t, (size_t)n * sizeof *t);
    }
    free(dst->t);
    memset(dst, 0, sizeof *dst);
    dst->t = t;
    dst->n = dst->cap = n;
    bound(dst);
    return 0;
}

void ge_move(gs_mesh *m, float dx, float dy, float dz)
{
    int i;
    if (!m || !m->t) return;
    if (dx == 0.f && dy == 0.f && dz == 0.f) return;
    for (i = 0; i < m->n; i++) {
        m->t[i].a.x += dx;
        m->t[i].a.y += dy;
        m->t[i].a.z += dz;
        m->t[i].b.x += dx;
        m->t[i].b.y += dy;
        m->t[i].b.z += dz;
        m->t[i].c.x += dx;
        m->t[i].c.y += dy;
        m->t[i].c.z += dz;
    }
    m->min.x += dx;
    m->min.y += dy;
    m->min.z += dz;
    m->max.x += dx;
    m->max.y += dy;
    m->max.z += dz;
}

void ge_rotate(gs_mesh *m, float rx, float ry, float rz)
{
    gs_v c;
    if (!m || !m->n) return;
    c = mid(m);
    if (rx) spin_about(m, c, 1.f, 0.f, 0.f, rx);
    if (ry) {
        c = mid(m);
        spin_about(m, c, 0.f, 1.f, 0.f, ry);
    }
    if (rz) {
        c = mid(m);
        spin_about(m, c, 0.f, 0.f, 1.f, rz);
    }
}

void ge_scale(gs_mesh *m, float sx, float sy, float sz)
{
    gs_v c;
    int i, k;
    if (!m || !m->t || !m->n) return;
    c = mid(m);
    for (i = 0; i < m->n; i++) {
        gs_v *p[3];
        p[0] = &m->t[i].a;
        p[1] = &m->t[i].b;
        p[2] = &m->t[i].c;
        for (k = 0; k < 3; k++) {
            p[k]->x = c.x + (p[k]->x - c.x) * sx;
            p[k]->y = c.y + (p[k]->y - c.y) * sy;
            p[k]->z = c.z + (p[k]->z - c.z) * sz;
        }
    }
    bound(m);
}

void ge_drop(gs_mesh *m)
{
    if (!m || !m->n) return;
    ge_move(m, 0.f, 0.f, -m->min.z);
}

void ge_centre(gs_mesh *m)
{
    float cx, cy;
    if (!m || !m->n) return;
    cx = (m->min.x + m->max.x) * 0.5f;
    cy = (m->min.y + m->max.y) * 0.5f;
    ge_move(m, GS_BED * 0.5f - cx, GS_BED * 0.5f - cy, 0.f);
}

int ge_largest_face(const gs_mesh *m)
{
    int i, best = -1;
    float besta = 0.f;
    if (!m) return -1;
    for (i = 0; i < m->n; i++) {
        gs_v n;
        float ux, uy, uz, vx, vy, vz, area;
        if (face_nrm(&m->t[i], &n)) continue;
        ux = m->t[i].b.x - m->t[i].a.x;
        uy = m->t[i].b.y - m->t[i].a.y;
        uz = m->t[i].b.z - m->t[i].a.z;
        vx = m->t[i].c.x - m->t[i].a.x;
        vy = m->t[i].c.y - m->t[i].a.y;
        vz = m->t[i].c.z - m->t[i].a.z;
        area = sqrtf((uy * vz - uz * vy) * (uy * vz - uz * vy) +
                     (uz * vx - ux * vz) * (uz * vx - ux * vz) +
                     (ux * vy - uy * vx) * (ux * vy - uy * vx));
        if (area > besta) {
            besta = area;
            best = i;
        }
    }
    return best;
}

int ge_lay_face(gs_mesh *m, int face)
{
    gs_v n, c;
    float d, ang, fz;
    if (!m || face < 0 || face >= m->n) return -1;
    if (face_nrm(&m->t[face], &n)) return -1;
    d = -n.z;
    c = mid(m);
    if (d < 0.9999f) {
        if (d <= -0.9999f) {
            ang = 3.14159265f;
            spin_about(m, c, 1.f, 0.f, 0.f, ang);
        } else {
            float ax = n.y * -1.f - n.z * 0.f;
            float ay = n.z * 0.f - n.x * -1.f;
            float az = n.x * 0.f - n.y * 0.f;
            if (d < -1.f) d = -1.f;
            if (d > 1.f) d = 1.f;
            ang = acosf(d);
            spin_about(m, c, ax, ay, az, ang);
        }
    }
    fz = (m->t[face].a.z + m->t[face].b.z + m->t[face].c.z) / 3.f;
    ge_move(m, 0.f, 0.f, -fz);
    return 0;
}

int ge_pick(const gs_mesh *m, const gs_cam *cam, int w, int h, int x, int y)
{
    gs_frame f;
    int i, best = -1;
    float bestz = -1e30f;
    float px, py;
    if (!m || !cam || gs_view_frame_bed(m, cam, w, h, 0.f, 0.f, GS_BED, GS_BED, &f)) return -1;
    px = (float)x + 0.5f;
    py = (float)y + 0.5f;
    for (i = 0; i < m->n; i++) {
        float ax, ay, az, bx, by, bz, cx, cy, cz;
        float area, inv, w0, w1, w2, z;
        gs_view_map(&f, cam, m->t[i].a, &ax, &ay, &az);
        gs_view_map(&f, cam, m->t[i].b, &bx, &by, &bz);
        gs_view_map(&f, cam, m->t[i].c, &cx, &cy, &cz);
        area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (area > -0.5f && area < 0.5f) continue;
        inv = 1.f / area;
        w0 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) * inv;
        w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) * inv;
        w2 = 1.f - w0 - w1;
        if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
        z = w0 * az + w1 * bz + w2 * cz;
        if (z <= bestz) continue;
        bestz = z;
        best = i;
    }
    return best;
}

ge_hist *ge_hist_new(void)
{
    ge_hist *h = (ge_hist *)calloc(1, sizeof *h);
    return h;
}

void ge_hist_free(ge_hist *h)
{
    int i;
    if (!h) return;
    for (i = 0; i < h->n; i++) gs_mesh_free(&h->slot[i]);
    free(h);
}

int ge_hist_push(ge_hist *h, const gs_mesh *m)
{
    if (!h || !m) return -1;
    if (h->n == HIST) {
        gs_mesh_free(&h->slot[0]);
        memmove(&h->slot[0], &h->slot[1], (size_t)(HIST - 1) * sizeof h->slot[0]);
        memset(&h->slot[HIST - 1], 0, sizeof h->slot[0]);
        h->n = HIST - 1;
    }
    if (ge_copy(&h->slot[h->n], m)) return -1;
    h->n++;
    return 0;
}

int ge_hist_pop(ge_hist *h, gs_mesh *m)
{
    if (!h || !m || h->n < 1) return -1;
    h->n--;
    gs_mesh_free(m);
    *m = h->slot[h->n];
    memset(&h->slot[h->n], 0, sizeof h->slot[h->n]);
    return 0;
}

int ge_hist_depth(const ge_hist *h)
{
    return h ? h->n : 0;
}
