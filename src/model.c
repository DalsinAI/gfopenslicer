/* A box, a cylinder, and the three boolean joins: union, cut, overlap.
 * Each boolean is a BSP: a polygon is split by a plane, the pieces that
 * fall on the wrong side are dropped, and the two surfaces are joined.
 * That is the public method for mesh booleans. No other project's source
 * is in here.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VMAX 16
#define EPS 1e-5f

typedef struct {
    float x, y, z;
} mv;

typedef struct {
    mv v[VMAX];
    int n;
} poly;

typedef struct {
    poly *p;
    int n, cap;
} bag;

typedef struct node {
    mv nrm;
    float w;
    int has;
    bag co;
    struct node *front, *back;
} node;

static mv sub(mv a, mv b)
{
    mv r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}

static mv cross(mv a, mv b)
{
    mv r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

static float dot(mv a, mv b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static int unit(mv *n)
{
    float L = sqrtf(dot(*n, *n));
    if (L < 1e-12f) return 0;
    n->x /= L;
    n->y /= L;
    n->z /= L;
    return 1;
}

static void bag_free(bag *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

static int bag_add(bag *b, const poly *p)
{
    if (p->n < 3 || p->n > VMAX) return 0;
    if (b->n >= b->cap) {
        int c = b->cap ? b->cap * 2 : 32;
        poly *np = (poly *)realloc(b->p, (size_t)c * sizeof *np);
        if (!np) return -1;
        b->p = np;
        b->cap = c;
    }
    b->p[b->n++] = *p;
    return 0;
}

static int plane_of(const poly *p, mv *nrm, float *w)
{
    mv e1, e2, n;
    if (p->n < 3) return 0;
    e1 = sub(p->v[1], p->v[0]);
    e2 = sub(p->v[2], p->v[0]);
    n = cross(e1, e2);
    if (!unit(&n)) return 0;
    *nrm = n;
    *w = dot(n, p->v[0]);
    return 1;
}

static float side(const node *n, mv v)
{
    return n->nrm.x * v.x + n->nrm.y * v.y + n->nrm.z * v.z - n->w;
}

static void flip_poly(poly *p)
{
    int i, h = p->n / 2;
    for (i = 0; i < h; i++) {
        mv t = p->v[i];
        p->v[i] = p->v[p->n - 1 - i];
        p->v[p->n - 1 - i] = t;
    }
}

/* Front keeps what is outside the plane, back what is inside. */
static int split_poly(const node *nd, const poly *in, bag *front, bag *back)
{
    int i, nf = 0, nb = 0;
    float d[VMAX];
    int c[VMAX];
    poly f, b;
    mv pn;
    float pw;
    for (i = 0; i < in->n; i++) {
        d[i] = side(nd, in->v[i]);
        if (d[i] > EPS) {
            c[i] = 1;
            nf++;
        } else if (d[i] < -EPS) {
            c[i] = -1;
            nb++;
        } else {
            c[i] = 0;
        }
    }
    if (!nf && !nb) {
        if (!plane_of(in, &pn, &pw)) return 0;
        if (dot(pn, nd->nrm) > 0.f) return bag_add(front, in);
        return bag_add(back, in);
    }
    if (!nb) return bag_add(front, in);
    if (!nf) return bag_add(back, in);
    f.n = b.n = 0;
    for (i = 0; i < in->n; i++) {
        int j = (i + 1) % in->n;
        if (c[i] != -1 && f.n < VMAX) f.v[f.n++] = in->v[i];
        if (c[i] != 1 && b.n < VMAX) b.v[b.n++] = in->v[i];
        if ((c[i] == 1 && c[j] == -1) || (c[i] == -1 && c[j] == 1)) {
            float t = d[i] / (d[i] - d[j]);
            mv p;
            p.x = in->v[i].x + (in->v[j].x - in->v[i].x) * t;
            p.y = in->v[i].y + (in->v[j].y - in->v[i].y) * t;
            p.z = in->v[i].z + (in->v[j].z - in->v[i].z) * t;
            if (f.n < VMAX) f.v[f.n++] = p;
            if (b.n < VMAX) b.v[b.n++] = p;
        }
    }
    if (bag_add(front, &f) || bag_add(back, &b)) return -1;
    return 0;
}

static void node_free(node *n)
{
    if (!n) return;
    node_free(n->front);
    node_free(n->back);
    bag_free(&n->co);
    free(n);
}

static int node_build(node *n, const bag *src)
{
    bag front, back;
    int i;
    memset(&front, 0, sizeof front);
    memset(&back, 0, sizeof back);
    if (!src->n) return 0;
    if (!n->has) {
        if (!plane_of(&src->p[0], &n->nrm, &n->w)) return 0;
        n->has = 1;
    }
    for (i = 0; i < src->n; i++) {
        const poly *p = &src->p[i];
        int vi, on = 1, out = 0, inn = 0;
        float d;
        for (vi = 0; vi < p->n; vi++) {
            d = side(n, p->v[vi]);
            if (d > EPS) {
                out = 1;
                on = 0;
            } else if (d < -EPS) {
                inn = 1;
                on = 0;
            }
        }
        if (on) {
            if (bag_add(&n->co, p)) goto fail;
            continue;
        }
        if (out && !inn) {
            if (bag_add(&front, p)) goto fail;
            continue;
        }
        if (inn && !out) {
            if (bag_add(&back, p)) goto fail;
            continue;
        }
        if (split_poly(n, p, &front, &back)) goto fail;
    }
    if (front.n) {
        if (!n->front) {
            n->front = (node *)calloc(1, sizeof *n->front);
            if (!n->front) goto fail;
        }
        if (node_build(n->front, &front)) goto fail;
    }
    if (back.n) {
        if (!n->back) {
            n->back = (node *)calloc(1, sizeof *n->back);
            if (!n->back) goto fail;
        }
        if (node_build(n->back, &back)) goto fail;
    }
    bag_free(&front);
    bag_free(&back);
    return 0;
fail:
    bag_free(&front);
    bag_free(&back);
    return -1;
}

static int node_clip(const node *n, const bag *src, bag *out)
{
    bag front, back;
    int i;
    memset(&front, 0, sizeof front);
    memset(&back, 0, sizeof back);
    if (!n || !n->has) {
        for (i = 0; i < src->n; i++)
            if (bag_add(out, &src->p[i])) return -1;
        return 0;
    }
    for (i = 0; i < src->n; i++) {
        if (split_poly(n, &src->p[i], &front, &back)) {
            bag_free(&front);
            bag_free(&back);
            return -1;
        }
    }
    if (n->front) {
        if (node_clip(n->front, &front, out)) {
            bag_free(&front);
            bag_free(&back);
            return -1;
        }
    } else {
        for (i = 0; i < front.n; i++)
            if (bag_add(out, &front.p[i])) {
                bag_free(&front);
                bag_free(&back);
                return -1;
            }
    }
    if (n->back) {
        if (node_clip(n->back, &back, out)) {
            bag_free(&front);
            bag_free(&back);
            return -1;
        }
    }
    bag_free(&front);
    bag_free(&back);
    return 0;
}

static int node_clip_to(node *n, const node *bsp)
{
    bag next;
    int i;
    if (!n) return 0;
    memset(&next, 0, sizeof next);
    if (node_clip(bsp, &n->co, &next)) return -1;
    bag_free(&n->co);
    n->co = next;
    if (node_clip_to(n->front, bsp)) return -1;
    if (node_clip_to(n->back, bsp)) return -1;
    (void)i;
    return 0;
}

static void node_invert(node *n)
{
    node *t;
    int i;
    if (!n) return;
    for (i = 0; i < n->co.n; i++) flip_poly(&n->co.p[i]);
    if (n->has) {
        n->nrm.x = -n->nrm.x;
        n->nrm.y = -n->nrm.y;
        n->nrm.z = -n->nrm.z;
        n->w = -n->w;
    }
    t = n->front;
    n->front = n->back;
    n->back = t;
    node_invert(n->front);
    node_invert(n->back);
}

static int node_all(const node *n, bag *out)
{
    int i;
    if (!n) return 0;
    for (i = 0; i < n->co.n; i++)
        if (bag_add(out, &n->co.p[i])) return -1;
    if (node_all(n->front, out)) return -1;
    if (node_all(n->back, out)) return -1;
    return 0;
}

static int mesh_from(gs_mesh *m, const bag *b)
{
    int i, k, count = 0;
    for (i = 0; i < b->n; i++)
        if (b->p[i].n >= 3) count += b->p[i].n - 2;
    m->t = count ? (gs_tri *)malloc((size_t)count * sizeof *m->t) : NULL;
    if (count && !m->t) return -1;
    m->n = 0;
    m->cap = count;
    m->min.x = m->min.y = m->min.z = 1e30f;
    m->max.x = m->max.y = m->max.z = -1e30f;
    for (i = 0; i < b->n; i++) {
        const poly *p = &b->p[i];
        for (k = 1; k + 1 < p->n; k++) {
            gs_tri t;
            int e;
            mv vs[3];
            t.a.x = p->v[0].x;
            t.a.y = p->v[0].y;
            t.a.z = p->v[0].z;
            t.b.x = p->v[k].x;
            t.b.y = p->v[k].y;
            t.b.z = p->v[k].z;
            t.c.x = p->v[k + 1].x;
            t.c.y = p->v[k + 1].y;
            t.c.z = p->v[k + 1].z;
            m->t[m->n++] = t;
            vs[0] = p->v[0];
            vs[1] = p->v[k];
            vs[2] = p->v[k + 1];
            for (e = 0; e < 3; e++) {
                if (vs[e].x < m->min.x) m->min.x = vs[e].x;
                if (vs[e].y < m->min.y) m->min.y = vs[e].y;
                if (vs[e].z < m->min.z) m->min.z = vs[e].z;
                if (vs[e].x > m->max.x) m->max.x = vs[e].x;
                if (vs[e].y > m->max.y) m->max.y = vs[e].y;
                if (vs[e].z > m->max.z) m->max.z = vs[e].z;
            }
        }
    }
    return 0;
}

static int from_mesh(bag *b, const gs_mesh *m)
{
    int i;
    memset(b, 0, sizeof *b);
    for (i = 0; i < m->n; i++) {
        poly p;
        p.n = 3;
        p.v[0].x = m->t[i].a.x;
        p.v[0].y = m->t[i].a.y;
        p.v[0].z = m->t[i].a.z;
        p.v[1].x = m->t[i].b.x;
        p.v[1].y = m->t[i].b.y;
        p.v[1].z = m->t[i].b.z;
        p.v[2].x = m->t[i].c.x;
        p.v[2].y = m->t[i].c.y;
        p.v[2].z = m->t[i].c.z;
        if (bag_add(b, &p)) return -1;
    }
    return 0;
}

static int tri(gs_mesh *m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz)
{
    gs_tri t;
    gs_v vs[3];
    int i;
    if (m->n >= m->cap) {
        int c = m->cap ? m->cap * 2 : 16;
        gs_tri *nt = (gs_tri *)realloc(m->t, (size_t)c * sizeof *nt);
        if (!nt) return -1;
        m->t = nt;
        m->cap = c;
    }
    t.a.x = ax;
    t.a.y = ay;
    t.a.z = az;
    t.b.x = bx;
    t.b.y = by;
    t.b.z = bz;
    t.c.x = cx;
    t.c.y = cy;
    t.c.z = cz;
    m->t[m->n++] = t;
    vs[0] = t.a;
    vs[1] = t.b;
    vs[2] = t.c;
    for (i = 0; i < 3; i++) {
        if (vs[i].x < m->min.x) m->min.x = vs[i].x;
        if (vs[i].y < m->min.y) m->min.y = vs[i].y;
        if (vs[i].z < m->min.z) m->min.z = vs[i].z;
        if (vs[i].x > m->max.x) m->max.x = vs[i].x;
        if (vs[i].y > m->max.y) m->max.y = vs[i].y;
        if (vs[i].z > m->max.z) m->max.z = vs[i].z;
    }
    return 0;
}

static void mesh_clear(gs_mesh *m)
{
    free(m->t);
    memset(m, 0, sizeof *m);
    m->min.x = m->min.y = m->min.z = 1e30f;
    m->max.x = m->max.y = m->max.z = -1e30f;
}

int gm_cube(gs_mesh *m, float x, float y, float z, float sx, float sy, float sz)
{
    float x1 = x + sx, y1 = y + sy, z1 = z + sz;
    mesh_clear(m);
    /* Outward faces, counter-clockwise from the outside. */
    if (tri(m, x, y, z1, x1, y, z1, x1, y1, z1) || tri(m, x, y, z1, x1, y1, z1, x, y1, z1)) return -1;
    if (tri(m, x, y, z, x, y1, z, x1, y1, z) || tri(m, x, y, z, x1, y1, z, x1, y, z)) return -1;
    if (tri(m, x, y, z, x, y, z1, x, y1, z1) || tri(m, x, y, z, x, y1, z1, x, y1, z)) return -1;
    if (tri(m, x1, y, z, x1, y1, z, x1, y1, z1) || tri(m, x1, y, z, x1, y1, z1, x1, y, z1)) return -1;
    if (tri(m, x, y, z, x1, y, z, x1, y, z1) || tri(m, x, y, z, x1, y, z1, x, y, z1)) return -1;
    if (tri(m, x, y1, z, x, y1, z1, x1, y1, z1) || tri(m, x, y1, z, x1, y1, z1, x1, y1, z)) return -1;
    return 0;
}

int gm_cylinder(gs_mesh *m, float x, float y, float z, float r, float h, int seg)
{
    int i;
    float z1 = z + h;
    if (seg < 3) seg = 3;
    mesh_clear(m);
    for (i = 0; i < seg; i++) {
        float a0 = (float)i / (float)seg * 6.2831853f;
        float a1 = (float)(i + 1) / (float)seg * 6.2831853f;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        float x0 = x + r * c0, y0 = y + r * s0, x2 = x + r * c1, y2 = y + r * s1;
        if (tri(m, x0, y0, z, x2, y2, z, x2, y2, z1) || tri(m, x0, y0, z, x2, y2, z1, x0, y0, z1)) return -1;
        if (tri(m, x, y, z1, x0, y0, z1, x2, y2, z1)) return -1;
        if (tri(m, x, y, z, x2, y2, z, x0, y0, z)) return -1;
    }
    return 0;
}

/* Join 0, cut 1, overlap 2. The same clips; only the turns of the
 * surface change, which is what picks inside from outside. */
static int gm_csg(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b, int op)
{
    bag pa, pb, all;
    node *A, *B;
    int rc;
    memset(&pa, 0, sizeof pa);
    memset(&pb, 0, sizeof pb);
    memset(&all, 0, sizeof all);
    A = (node *)calloc(1, sizeof *A);
    B = (node *)calloc(1, sizeof *B);
    if (!A || !B || from_mesh(&pa, a) || from_mesh(&pb, b)) {
        bag_free(&pa);
        bag_free(&pb);
        node_free(A);
        node_free(B);
        return -1;
    }
    rc = node_build(A, &pa) || node_build(B, &pb);
    bag_free(&pa);
    bag_free(&pb);
    if (rc) {
        node_free(A);
        node_free(B);
        return -1;
    }
    if (op != 0) node_invert(A);
    if (op == 2) node_invert(B);
    rc = node_clip_to(A, B) || node_clip_to(B, A);
    if (!rc) {
        node_invert(B);
        rc = node_clip_to(B, A);
    }
    if (!rc) {
        bag got;
        /* Turn the second solid back so its faces meet the first, then
         * build them in. A cut and an overlap turn A once more, which
         * puts the outside back out. */
        node_invert(B);
        memset(&got, 0, sizeof got);
        rc = node_all(B, &got) || node_build(A, &got);
        bag_free(&got);
    }
    if (!rc && op != 0) node_invert(A);
    if (!rc) rc = node_all(A, &all);
    node_free(A);
    node_free(B);
    if (rc) {
        bag_free(&all);
        return -1;
    }
    mesh_clear(dst);
    rc = mesh_from(dst, &all);
    bag_free(&all);
    return rc;
}

int gm_union(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b)
{
    return gm_csg(dst, a, b, 0);
}

int gm_subtract(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b)
{
    return gm_csg(dst, a, b, 1);
}

int gm_intersect(gs_mesh *dst, const gs_mesh *a, const gs_mesh *b)
{
    return gm_csg(dst, a, b, 2);
}

int gm_stl_write(const gs_mesh *m, const char *path)
{
    FILE *f;
    unsigned char hdr[80];
    unsigned long n;
    int i;
    if (!m || !path) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "GFOpenMake hole", 15);
    if (fwrite(hdr, 1, 80, f) != 80) {
        fclose(f);
        return -1;
    }
    n = (unsigned long)m->n;
    if (fwrite(&n, 4, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    for (i = 0; i < m->n; i++) {
        float v[12];
        unsigned short attr = 0;
        mv e1, e2, nn;
        e1.x = m->t[i].b.x - m->t[i].a.x;
        e1.y = m->t[i].b.y - m->t[i].a.y;
        e1.z = m->t[i].b.z - m->t[i].a.z;
        e2.x = m->t[i].c.x - m->t[i].a.x;
        e2.y = m->t[i].c.y - m->t[i].a.y;
        e2.z = m->t[i].c.z - m->t[i].a.z;
        nn = cross(e1, e2);
        unit(&nn);
        v[0] = nn.x;
        v[1] = nn.y;
        v[2] = nn.z;
        v[3] = m->t[i].a.x;
        v[4] = m->t[i].a.y;
        v[5] = m->t[i].a.z;
        v[6] = m->t[i].b.x;
        v[7] = m->t[i].b.y;
        v[8] = m->t[i].b.z;
        v[9] = m->t[i].c.x;
        v[10] = m->t[i].c.y;
        v[11] = m->t[i].c.z;
        if (fwrite(v, 4, 12, f) != 12 || fwrite(&attr, 2, 1, f) != 1) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}
