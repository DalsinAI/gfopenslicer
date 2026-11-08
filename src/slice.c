#include "gs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void gs_cfg_default(gs_cfg *c)
{
    c->layer_h = 0.2f;
    c->line_w = 0.4f;
    c->filament = 1.75f;
    c->infill = 20.f;
    c->hotend = 200.f;
    c->bed = 60.f;
    c->skirt = 1;
}

typedef struct {
    float x, y;
} p2;

typedef struct {
    p2 a, b;
} seg;

typedef struct {
    p2 *p;
    int n, cap, closed;
} loop;

static int near2(p2 a, p2 b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy < 0.02f * 0.02f;
}

static int cut_edge(gs_v p, gs_v q, float z, p2 *o)
{
    float t;
    if ((p.z - z) * (q.z - z) > 0.f) return 0;
    if (p.z == q.z) return 0;
    t = (z - p.z) / (q.z - p.z);
    if (t < 0.f || t > 1.f) return 0;
    o->x = p.x + t * (q.x - p.x);
    o->y = p.y + t * (q.y - p.y);
    return 1;
}

static int add_seg(seg **s, int *n, int *cap, p2 a, p2 b)
{
    int i;
    if (near2(a, b)) return 0;
    for (i = 0; i < *n; i++) {
        if ((near2((*s)[i].a, a) && near2((*s)[i].b, b)) || (near2((*s)[i].a, b) && near2((*s)[i].b, a)))
            return 0;
    }
    if (*n == *cap) {
        int c = *cap ? *cap * 2 : 64;
        seg *p = (seg *)realloc(*s, (size_t)c * sizeof *p);
        if (!p) return -1;
        *s = p;
        *cap = c;
    }
    (*s)[(*n)++] = (seg){a, b};
    return 0;
}

static int loop_add(loop *L, p2 q)
{
    if (L->n && near2(L->p[L->n - 1], q)) return 0;
    if (L->n == L->cap) {
        int c = L->cap ? L->cap * 2 : 16;
        p2 *p = (p2 *)realloc(L->p, (size_t)c * sizeof *p);
        if (!p) return -1;
        L->p = p;
        L->cap = c;
    }
    L->p[L->n++] = q;
    return 0;
}

static int grow(loop *L, seg *s, int ns, unsigned char *used, int tail)
{
    int guard;
    for (guard = 0; guard < ns + 2; guard++) {
        p2 end = tail ? L->p[L->n - 1] : L->p[0];
        int i, hit = 0;
        for (i = 0; i < ns; i++) {
            p2 nxt;
            if (used[i]) continue;
            if (near2(s[i].a, end)) nxt = s[i].b;
            else if (near2(s[i].b, end)) nxt = s[i].a;
            else continue;
            used[i] = 1;
            if (tail) {
                if (loop_add(L, nxt)) return -1;
            } else {
                /* insert at the front */
                if (L->n == L->cap) {
                    int c = L->cap ? L->cap * 2 : 16;
                    p2 *p = (p2 *)realloc(L->p, (size_t)c * sizeof *p);
                    if (!p) return -1;
                    L->p = p;
                    L->cap = c;
                }
                memmove(L->p + 1, L->p, (size_t)L->n * sizeof *L->p);
                L->p[0] = nxt;
                L->n++;
            }
            hit = 1;
            break;
        }
        if (!hit) return 0;
        if (L->n >= 3 && near2(L->p[0], L->p[L->n - 1])) {
            L->n--;
            L->closed = 1;
            return 0;
        }
    }
    return 0;
}

static int stitch(seg *s, int ns, loop **out, int *nl)
{
    unsigned char *used = (unsigned char *)calloc((size_t)ns, 1);
    int i;
    *out = NULL;
    *nl = 0;
    if (!used) return -1;
    for (i = 0; i < ns; i++) {
        loop L;
        loop *p;
        if (used[i]) continue;
        memset(&L, 0, sizeof L);
        used[i] = 1;
        if (loop_add(&L, s[i].a) || loop_add(&L, s[i].b)) {
            free(L.p);
            free(used);
            return -1;
        }
        if (grow(&L, s, ns, used, 1) || grow(&L, s, ns, used, 0)) {
            free(L.p);
            free(used);
            return -1;
        }
        if (L.n >= 3 && near2(L.p[0], L.p[L.n - 1])) {
            L.n--;
            L.closed = 1;
        }
        if (L.n < 2) {
            free(L.p);
            continue;
        }
        p = (loop *)realloc(*out, (size_t)(*nl + 1) * sizeof *p);
        if (!p) {
            free(L.p);
            free(used);
            return -1;
        }
        *out = p;
        (*out)[(*nl)++] = L;
    }
    free(used);
    return 0;
}

static float filament_per_mm(const gs_cfg *c)
{
    float r = c->filament * 0.5f;
    return (c->line_w * c->layer_h) / (3.14159265f * r * r);
}

static void emit_line(FILE *o, float *e, float *ex, float *ey, p2 a, p2 b, float fpm, int travel)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    if (travel || !near2((p2){*ex, *ey}, a))
        fprintf(o, "G0 X%.3f Y%.3f F6000\n", a.x, a.y);
    if (!travel) {
        *e += len * fpm;
        fprintf(o, "G1 X%.3f Y%.3f E%.5f F1200\n", b.x, b.y, *e);
    }
    *ex = b.x;
    *ey = b.y;
}

static void emit_loop(FILE *o, float *e, float *ex, float *ey, const loop *L, float fpm)
{
    int i;
    if (L->n < 2) return;
    emit_line(o, e, ex, ey, L->p[0], L->p[1], fpm, 0);
    for (i = 1; i < L->n - 1; i++)
        emit_line(o, e, ex, ey, L->p[i], L->p[i + 1], fpm, 0);
    if (L->closed)
        emit_line(o, e, ex, ey, L->p[L->n - 1], L->p[0], fpm, 0);
}

/* Horizontal chords of closed loops, in a frame rotated by -45 degrees,
 * rotated back. Even-odd across every loop. */
static int emit_infill(FILE *o, float *e, float *ex, float *ey, loop *ls, int nl, const gs_cfg *c, float fpm)
{
    const float ang = 0.78539816f;
    float ca = cosf(ang), sa = sinf(ang);
    float ymin = 1e30f, ymax = -1e30f, spacing, y;
    int li;
    if (c->infill <= 0.f) return 0;
    spacing = c->line_w * 100.f / c->infill;
    if (spacing < c->line_w) spacing = c->line_w;
    for (li = 0; li < nl; li++) {
        int i;
        if (!ls[li].closed) continue;
        for (i = 0; i < ls[li].n; i++) {
            float ry = -ls[li].p[i].x * sa + ls[li].p[i].y * ca;
            if (ry < ymin) ymin = ry;
            if (ry > ymax) ymax = ry;
        }
    }
    if (ymin > ymax) return 0;
    for (y = ymin + spacing * 0.5f; y < ymax; y += spacing) {
        float xs[256];
        int nx = 0, i, a, b;
        for (li = 0; li < nl && nx < 250; li++) {
            const loop *L = &ls[li];
            if (!L->closed || L->n < 3) continue;
            for (i = 0; i < L->n && nx < 250; i++) {
                p2 p = L->p[i], q = L->p[(i + 1) % L->n];
                float py = -p.x * sa + p.y * ca, qy = -q.x * sa + q.y * ca;
                float px = p.x * ca + p.y * sa, qx = q.x * ca + q.y * sa;
                float t;
                if ((py - y) * (qy - y) > 0.f || py == qy) continue;
                t = (y - py) / (qy - py);
                if (t < 0.f || t >= 1.f) continue;
                xs[nx++] = px + t * (qx - px);
            }
        }
        for (a = 1; a < nx; a++) {
            float v = xs[a];
            b = a;
            while (b > 0 && xs[b - 1] > v) {
                xs[b] = xs[b - 1];
                b--;
            }
            xs[b] = v;
        }
        for (a = 0; a + 1 < nx; a += 2) {
            p2 u, v;
            if (xs[a + 1] - xs[a] < 0.05f) continue;
            u.x = xs[a] * ca - y * sa;
            u.y = xs[a] * sa + y * ca;
            v.x = xs[a + 1] * ca - y * sa;
            v.y = xs[a + 1] * sa + y * ca;
            emit_line(o, e, ex, ey, u, v, fpm, 0);
        }
    }
    return 0;
}

int gs_slice(const gs_mesh *m, const gs_cfg *c, FILE *out, char *err, int errlen)
{
    float z, fpm, e = 0, ex = 0, ey = 0;
    int layer = 0, any = 0;
    if (!m->n) {
        snprintf(err, (size_t)errlen, "nothing to slice");
        return -1;
    }
    if (c->layer_h < 0.05f || c->layer_h > 1.f || c->line_w < 0.1f || c->filament < 0.5f) {
        snprintf(err, (size_t)errlen, "layer, line or filament is out of range");
        return -1;
    }
    fpm = filament_per_mm(c);
    fprintf(out, "; GFOpenSlicer %s (%s)\n", GS_VERSION, GS_DATE);
    fprintf(out, "; layer %.3f mm, line %.3f mm, infill %.0f%%\n", c->layer_h, c->line_w, c->infill);
    fprintf(out, "G21\nG90\nM82\n");
    if (c->hotend > 0.f) {
        fprintf(out, "M104 S%.0f\nM109 S%.0f\n", c->hotend, c->hotend);
    }
    if (c->bed > 0.f) {
        fprintf(out, "M140 S%.0f\nM190 S%.0f\n", c->bed, c->bed);
    }
    fprintf(out, "G28\nG92 E0\n");
    for (z = m->min.z + c->layer_h * 0.5f; z < m->max.z - 1e-4f; z += c->layer_h) {
        seg *ss = NULL;
        int ns = 0, scap = 0, ti, nl = 0, li;
        loop *ls = NULL;
        fprintf(out, "; LAYER %d Z%.3f\n", layer, z);
        fprintf(out, "G0 Z%.3f F300\n", z);
        for (ti = 0; ti < m->n; ti++) {
            p2 h[3];
            int nh = 0, e1;
            gs_v p[3];
            p[0] = m->t[ti].a;
            p[1] = m->t[ti].b;
            p[2] = m->t[ti].c;
            for (e1 = 0; e1 < 3; e1++) {
                p2 o;
                int k, seen = 0;
                if (!cut_edge(p[e1], p[(e1 + 1) % 3], z, &o)) continue;
                for (k = 0; k < nh; k++)
                    if (near2(h[k], o)) seen = 1;
                if (!seen && nh < 3) h[nh++] = o;
            }
            if (nh == 2 && add_seg(&ss, &ns, &scap, h[0], h[1])) {
                snprintf(err, (size_t)errlen, "out of memory");
                free(ss);
                return -1;
            }
        }
        if (stitch(ss, ns, &ls, &nl)) {
            snprintf(err, (size_t)errlen, "out of memory");
            free(ss);
            return -1;
        }
        free(ss);
        if (layer == 0 && c->skirt > 0 && nl > 0) {
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            int s;
            for (li = 0; li < nl; li++) {
                int i;
                for (i = 0; i < ls[li].n; i++) {
                    if (ls[li].p[i].x < x0) x0 = ls[li].p[i].x;
                    if (ls[li].p[i].y < y0) y0 = ls[li].p[i].y;
                    if (ls[li].p[i].x > x1) x1 = ls[li].p[i].x;
                    if (ls[li].p[i].y > y1) y1 = ls[li].p[i].y;
                }
            }
            for (s = 1; s <= c->skirt; s++) {
                float mrg = c->line_w * (float)s + 2.f;
                p2 r[4];
                loop sk;
                r[0] = (p2){x0 - mrg, y0 - mrg};
                r[1] = (p2){x1 + mrg, y0 - mrg};
                r[2] = (p2){x1 + mrg, y1 + mrg};
                r[3] = (p2){x0 - mrg, y1 + mrg};
                memset(&sk, 0, sizeof sk);
                sk.p = r;
                sk.n = 4;
                sk.closed = 1;
                fprintf(out, "; SKIRT %d\n", s);
                emit_loop(out, &e, &ex, &ey, &sk, fpm);
            }
        }
        for (li = 0; li < nl; li++) {
            if (ls[li].n >= 2) any = 1;
            emit_loop(out, &e, &ex, &ey, &ls[li], fpm);
        }
        emit_infill(out, &e, &ex, &ey, ls, nl, c, fpm);
        for (li = 0; li < nl; li++) free(ls[li].p);
        free(ls);
        layer++;
    }
    fprintf(out, "; END layers %d\n", layer);
    fprintf(out, "M104 S0\nM140 S0\nG28 X0\nM84\n");
    if (!any) {
        snprintf(err, (size_t)errlen, "no layer had a wall");
        return -1;
    }
    return 0;
}
