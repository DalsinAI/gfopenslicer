/* Host checks for the engine. A cube, a pyramid, an ASCII file, a mesh
 * with welded duplicates. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs.h"
#include "gs_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        fails++;
    }
}

static void tri(FILE *f, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz)
{
    float r[12];
    unsigned char attr[2] = {0, 0};
    memset(r, 0, sizeof r);
    r[3] = ax; r[4] = ay; r[5] = az;
    r[6] = bx; r[7] = by; r[8] = bz;
    r[9] = cx; r[10] = cy; r[11] = cz;
    fwrite(r, 4, 12, f);
    fwrite(attr, 1, 2, f);
}

static void cube_stl(const char *path)
{
    FILE *f = fopen(path, "wb");
    unsigned char hdr[84];
    unsigned n = 12;
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "gfopenslicer", 12);
    hdr[80] = 12;
    fwrite(hdr, 1, 84, f);
    /* outward-enough; repair turns it out if not */
    tri(f, 0, 0, 0, 10, 0, 0, 0, 10, 0);
    tri(f, 10, 0, 0, 10, 10, 0, 0, 10, 0);
    tri(f, 0, 0, 10, 0, 10, 10, 10, 0, 10);
    tri(f, 10, 0, 10, 0, 10, 10, 10, 10, 10);
    tri(f, 0, 0, 0, 0, 0, 10, 10, 0, 0);
    tri(f, 10, 0, 0, 0, 0, 10, 10, 0, 10);
    tri(f, 0, 10, 0, 10, 10, 0, 0, 10, 10);
    tri(f, 10, 10, 0, 10, 10, 10, 0, 10, 10);
    tri(f, 0, 0, 0, 0, 10, 0, 0, 0, 10);
    tri(f, 0, 10, 0, 0, 10, 10, 0, 0, 10);
    tri(f, 10, 0, 0, 10, 0, 10, 10, 10, 0);
    tri(f, 10, 10, 0, 10, 0, 10, 10, 10, 10);
    (void)n;
    fclose(f);
}

static void pyramid_stl(const char *path)
{
    FILE *f = fopen(path, "w");
    fprintf(f, "solid pyramid\n");
    fprintf(f, "facet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 10 0 0\nvertex 0 10 0\nendloop\nendfacet\n");
    fprintf(f, "facet normal 0 0 -1\nouter loop\nvertex 10 0 0\nvertex 10 10 0\nvertex 0 10 0\nendloop\nendfacet\n");
    fprintf(f, "facet normal 0 -1 0\nouter loop\nvertex 0 0 0\nvertex 5 5 10\nvertex 10 0 0\nendloop\nendfacet\n");
    fprintf(f, "facet normal 1 0 0\nouter loop\nvertex 10 0 0\nvertex 5 5 10\nvertex 10 10 0\nendloop\nendfacet\n");
    fprintf(f, "facet normal 0 1 0\nouter loop\nvertex 10 10 0\nvertex 5 5 10\nvertex 0 10 0\nendloop\nendfacet\n");
    fprintf(f, "facet normal -1 0 0\nouter loop\nvertex 0 10 0\nvertex 5 5 10\nvertex 0 0 0\nendloop\nendfacet\n");
    fprintf(f, "endsolid\n");
    fclose(f);
}

typedef struct {
    int layers;
    int g1;
    float emax;
    float xmin, xmax, ymin, ymax;
    float z0, zlast;
    int skirt_out;
    float first_span, last_span;
    int in_first, in_last;
} stats;

static void span_add(float *mn, float *mx, float x)
{
    if (x < *mn) *mn = x;
    if (x > *mx) *mx = x;
}

static stats read_gcode(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    stats s;
    int layer = -1, first = -1, last = -1;
    float fx0 = 1e9f, fx1 = -1e9f, lx0 = 1e9f, lx1 = -1e9f;
    memset(&s, 0, sizeof s);
    s.xmin = 1e9f;
    s.ymin = 1e9f;
    s.xmax = -1e9f;
    s.ymax = -1e9f;
    while (fgets(line, sizeof line, f)) {
        float x, y, z, e;
        if (sscanf(line, "; LAYER %d Z%f", &layer, &z) == 2) {
            s.layers++;
            if (s.layers == 1) s.z0 = z;
            s.zlast = z;
            if (first < 0) first = layer;
            last = layer;
            lx0 = 1e9f;
            lx1 = -1e9f;
            s.in_last = 0;
        } else if (sscanf(line, "G1 X%f Y%f E%f", &x, &y, &e) == 3) {
            s.g1++;
            if (e > s.emax) s.emax = e;
            span_add(&s.xmin, &s.xmax, x);
            span_add(&s.ymin, &s.ymax, y);
            if (layer == first) {
                span_add(&fx0, &fx1, x);
                s.in_first++;
            }
            if (layer == last) {
                span_add(&lx0, &lx1, x);
                s.in_last++;
            }
            if (layer == 0 && (x < -0.5f || x > 10.5f || y < -0.5f || y > 10.5f)) s.skirt_out = 1;
        }
    }
    s.first_span = fx1 - fx0;
    s.last_span = lx1 - lx0;
    fclose(f);
    return s;
}

static int slice_file(const char *in, const char *out, float infill)
{
    gs_mesh m;
    gs_cfg c;
    FILE *f;
    char err[160];
    if (gs_stl_load(&m, in, err, (int)sizeof err)) {
        fprintf(stderr, "load %s\n", err);
        return -1;
    }
    gs_mesh_repair(&m);
    gs_cfg_default(&c);
    c.infill = infill;
    f = fopen(out, "w");
    if (gs_slice(&m, &c, f, err, (int)sizeof err)) {
        fprintf(stderr, "slice %s\n", err);
        fclose(f);
        gs_mesh_free(&m);
        return -1;
    }
    fclose(f);
    gs_mesh_free(&m);
    return 0;
}

int main(void)
{
    stats s;
    cube_stl("/tmp/gs-cube.stl");
    pyramid_stl("/tmp/gs-pyr.stl");
    expect(slice_file("/tmp/gs-cube.stl", "/tmp/gs-cube.gcode", 20.f) == 0, "cube slices");
    s = read_gcode("/tmp/gs-cube.gcode");
    expect(s.layers == 50, "cube has 50 layers");
    expect(fabsf(s.z0 - 0.1f) < 0.001f, "first layer at 0.1");
    expect(fabsf(s.zlast - 9.9f) < 0.02f, "last layer at 9.9");
    expect(s.g1 > 50, "cube extrudes");
    expect(s.emax > 10.f && s.emax < 400.f, "filament used is in range");
    expect(s.skirt_out, "the skirt stands outside the cube");
    expect(s.xmin > -8.f && s.xmax < 18.f, "nothing is far from the cube");

    expect(slice_file("/tmp/gs-pyr.stl", "/tmp/gs-pyr.gcode", 0.f) == 0, "pyramid slices");
    s = read_gcode("/tmp/gs-pyr.gcode");
    expect(s.layers == 50, "pyramid has 50 layers");
    expect(s.first_span > s.last_span + 2.f, "the pyramid narrows");
    expect(s.in_last > 0, "the top still has a wall");

    {
        FILE *f = fopen("/tmp/gs-cube.gcode", "r");
        char *buf;
        const char *a, *b;
        unsigned char *rgb;
        int segs, brass = 0, i, n;
        long sz;
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        rewind(f);
        buf = (char *)malloc((size_t)sz + 1);
        n = (int)fread(buf, 1, (size_t)sz, f);
        buf[n] = 0;
        fclose(f);
        a = strstr(buf, "; LAYER 0 ");
        b = strstr(a, "; LAYER 1 ");
        rgb = (unsigned char *)malloc(180 * 140 * 3);
        segs = gs_view_paint(a, (int)(b - a), rgb, 180, 140);
        expect(segs > 8, "the view draws the first layer");
        for (i = 0; i < 180 * 140; i++)
            if (rgb[i * 3] == 232 && rgb[i * 3 + 1] == 184) brass++;
        expect(brass > 40, "the lines show on the bed");
        expect(rgb[0] == 20 && rgb[2] == 28, "the corner of the bed stays empty");
        free(rgb);
        free(buf);
    }

    {
        gs_mesh m;
        gs_cam cam;
        unsigned char *a, *b;
        int shades[8], i, used = 0, diff = 0;
        char err[80];
        expect(gs_stl_load(&m, "/tmp/gs-cube.stl", err, (int)sizeof err) == 0, "cube loads for the solid");
        gs_mesh_repair(&m);
        a = (unsigned char *)malloc(160 * 120 * 3);
        b = (unsigned char *)malloc(160 * 120 * 3);
        gs_cam_home(&cam);
        expect(gs_view_solid(&m, &cam, a, 160, 120) == 0, "the solid draws");
        expect(gs_view_on_gpu() == 0, "this machine has no chip");
        memset(shades, 0, sizeof shades);
        for (i = 0; i < 160 * 120; i++) {
            if (a[i * 3] == 20) continue;
            shades[a[i * 3] >> 5]++;
        }
        for (i = 0; i < 8; i++)
            if (shades[i] > 40) used++;
        expect(used >= 2, "the cube shows more than one face");
        gs_cam_orbit(&cam, 0.8f, 0.f);
        gs_view_solid(&m, &cam, b, 160, 120);
        for (i = 0; i < 160 * 120 * 3; i++) diff += a[i] != b[i];
        expect(diff > 500, "orbiting the camera moves the solid");
        free(a);
        free(b);
        gs_mesh_free(&m);
    }

    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
