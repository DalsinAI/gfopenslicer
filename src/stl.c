#include "gs.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void bounds(gs_mesh *m)
{
    int i;
    if (!m->n) {
        m->min = m->max = (gs_v){0, 0, 0};
        return;
    }
    m->min = m->max = m->t[0].a;
    for (i = 0; i < m->n; i++) {
        gs_v p[3];
        int k;
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

static int push(gs_mesh *m, gs_tri t)
{
    if (m->n == m->cap) {
        int cap = m->cap ? m->cap * 2 : 64;
        gs_tri *p = (gs_tri *)realloc(m->t, (size_t)cap * sizeof *p);
        if (!p) return -1;
        m->t = p;
        m->cap = cap;
    }
    m->t[m->n++] = t;
    return 0;
}

static int load_ascii(gs_mesh *m, FILE *f, char *err, int errlen)
{
    char line[256];
    gs_v v[3];
    int nv = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        float x, y, z;
        while (*s && isspace((unsigned char)*s)) s++;
        if (strncmp(s, "vertex", 6) != 0 || !isspace((unsigned char)s[6])) continue;
        if (sscanf(s + 6, "%f %f %f", &x, &y, &z) != 3) {
            snprintf(err, (size_t)errlen, "a vertex line is not three numbers");
            return -1;
        }
        v[nv++] = (gs_v){x, y, z};
        if (nv == 3) {
            if (push(m, (gs_tri){v[0], v[1], v[2]})) {
                snprintf(err, (size_t)errlen, "out of memory");
                return -1;
            }
            nv = 0;
        }
    }
    if (!m->n) {
        snprintf(err, (size_t)errlen, "no triangles");
        return -1;
    }
    return 0;
}

static float lef32(const unsigned char *p)
{
    unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int load_bin(gs_mesh *m, FILE *f, char *err, int errlen)
{
    unsigned char hdr[84], rec[50];
    unsigned n = 0, i;
    if (fread(hdr, 1, 84, f) != 84) {
        snprintf(err, (size_t)errlen, "not an STL");
        return -1;
    }
    n = (unsigned)hdr[80] | ((unsigned)hdr[81] << 8) | ((unsigned)hdr[82] << 16) | ((unsigned)hdr[83] << 24);
    if (n > 2000000u) {
        snprintf(err, (size_t)errlen, "too many triangles");
        return -1;
    }
    for (i = 0; i < n; i++) {
        gs_tri t;
        if (fread(rec, 1, 50, f) != 50) {
            snprintf(err, (size_t)errlen, "the file ended among the triangles");
            return -1;
        }
        t.a = (gs_v){lef32(rec + 12), lef32(rec + 16), lef32(rec + 20)};
        t.b = (gs_v){lef32(rec + 24), lef32(rec + 28), lef32(rec + 32)};
        t.c = (gs_v){lef32(rec + 36), lef32(rec + 40), lef32(rec + 44)};
        if (push(m, t)) {
            snprintf(err, (size_t)errlen, "out of memory");
            return -1;
        }
    }
    if (!m->n) {
        snprintf(err, (size_t)errlen, "no triangles");
        return -1;
    }
    return 0;
}

int gs_stl_load(gs_mesh *m, const char *path, char *err, int errlen)
{
    FILE *f;
    unsigned char probe[256];
    size_t got;
    int ascii = 0, i;
    memset(m, 0, sizeof *m);
    f = fopen(path, "rb");
    if (!f) {
        snprintf(err, (size_t)errlen, "cannot open %s", path);
        return -1;
    }
    got = fread(probe, 1, sizeof probe, f);
    for (i = 0; i + 5 < (int)got; i++) {
        if (memcmp(probe + i, "facet", 5) == 0) ascii = 1;
    }
    if (!ascii && got >= 5 && memcmp(probe, "solid", 5) == 0) ascii = 1;
    rewind(f);
    if ((ascii ? load_ascii(m, f, err, errlen) : load_bin(m, f, err, errlen))) {
        fclose(f);
        gs_mesh_free(m);
        return -1;
    }
    fclose(f);
    bounds(m);
    return 0;
}

void gs_mesh_free(gs_mesh *m)
{
    free(m->t);
    memset(m, 0, sizeof *m);
}

typedef struct {
    int x, y, z, at;
} gs_key;

static int cmp_key(const void *a, const void *b)
{
    const gs_key *p = (const gs_key *)a, *q = (const gs_key *)b;
    if (p->x != q->x) return p->x < q->x ? -1 : 1;
    if (p->y != q->y) return p->y < q->y ? -1 : 1;
    if (p->z != q->z) return p->z < q->z ? -1 : 1;
    return 0;
}

static int qcoord(float v)
{
    return (int)lroundf(v * 1000.f);
}

void gs_mesh_repair(gs_mesh *m)
{
    gs_key *k;
    gs_v *orig, *uniq;
    int *id, i, nv, w, nk;
    double vol = 0;
    if (!m->n) return;
    nk = m->n * 3;
    k = (gs_key *)malloc((size_t)nk * sizeof *k);
    orig = (gs_v *)malloc((size_t)nk * sizeof *orig);
    uniq = (gs_v *)malloc((size_t)nk * sizeof *uniq);
    id = (int *)malloc((size_t)nk * sizeof *id);
    if (!k || !orig || !uniq || !id) {
        free(k);
        free(orig);
        free(uniq);
        free(id);
        return;
    }
    for (i = 0; i < m->n; i++) {
        gs_v p[3];
        int e;
        p[0] = m->t[i].a;
        p[1] = m->t[i].b;
        p[2] = m->t[i].c;
        for (e = 0; e < 3; e++) {
            k[i * 3 + e] = (gs_key){qcoord(p[e].x), qcoord(p[e].y), qcoord(p[e].z), i * 3 + e};
            orig[i * 3 + e] = p[e];
        }
    }
    qsort(k, (size_t)nk, sizeof *k, cmp_key);
    nv = 0;
    for (i = 0; i < nk; i++) {
        if (i && k[i].x == k[i - 1].x && k[i].y == k[i - 1].y && k[i].z == k[i - 1].z)
            id[k[i].at] = id[k[i - 1].at];
        else {
            id[k[i].at] = nv;
            uniq[nv++] = orig[k[i].at];
        }
    }
    w = 0;
    for (i = 0; i < m->n; i++) {
        int ia = id[i * 3], ib = id[i * 3 + 1], ic = id[i * 3 + 2];
        gs_v a, b, c;
        float cx, cy, cz;
        if (ia == ib || ib == ic || ia == ic) continue;
        a = uniq[ia];
        b = uniq[ib];
        c = uniq[ic];
        cx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
        cy = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        cz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (cx * cx + cy * cy + cz * cz < 1e-12f) continue;
        m->t[w].a = a;
        m->t[w].b = b;
        m->t[w].c = c;
        vol += (double)a.x * ((double)b.y * c.z - (double)b.z * c.y) +
               (double)b.x * ((double)c.y * a.z - (double)c.z * a.y) +
               (double)c.x * ((double)a.y * b.z - (double)a.z * b.y);
        w++;
    }
    m->n = w;
    if (vol < 0) {
        for (i = 0; i < m->n; i++) {
            gs_v s = m->t[i].b;
            m->t[i].b = m->t[i].c;
            m->t[i].c = s;
        }
    }
    free(k);
    free(orig);
    free(uniq);
    free(id);
    bounds(m);
}
