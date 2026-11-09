/* A 20 mm cube with a hole through it. The hole is a cylinder cut out.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_model.h"
#include "gs_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        fails++;
    }
}

static float volume(const gs_mesh *m)
{
    double s = 0;
    int i;
    for (i = 0; i < m->n; i++) {
        gs_v a = m->t[i].a, b = m->t[i].b, c = m->t[i].c;
        s += (double)a.x * ((double)b.y * c.z - (double)c.y * b.z);
        s += (double)b.x * ((double)c.y * a.z - (double)a.y * c.z);
        s += (double)c.x * ((double)a.y * b.z - (double)b.y * a.z);
    }
    if (s < 0) s = -s;
    return (float)(s / 6.0);
}

/* Odd crossings of a ray along +X means the point is inside. */
static int inside(const gs_mesh *m, float x, float y, float z)
{
    int i, hits = 0;
    for (i = 0; i < m->n; i++) {
        gs_v a = m->t[i].a, b = m->t[i].b, c = m->t[i].c;
        float ay = a.y - y, by = b.y - y, cy = c.y - y;
        float az = a.z - z, bz = b.z - z, cz = c.z - z;
        float area = ay * (bz - cz) + by * (cz - az) + cy * (az - bz);
        float u, v, w, t;
        if (area > -1e-8f && area < 1e-8f) continue;
        u = (y * (bz - cz) + b.y * (cz - z) + c.y * (z - bz) - y * 0) ;
        /* barycentric of (x is free) on the yz projection */
        {
            float den = (b.y - a.y) * (c.z - a.z) - (c.y - a.y) * (b.z - a.z);
            if (den > -1e-8f && den < 1e-8f) continue;
            v = ((y - a.y) * (c.z - a.z) - (c.y - a.y) * (z - a.z)) / den;
            w = ((b.y - a.y) * (z - a.z) - (y - a.y) * (b.z - a.z)) / den;
            u = 1.f - v - w;
            if (u < -1e-5f || v < -1e-5f || w < -1e-5f) continue;
            t = u * a.x + v * b.x + w * c.x;
            if (t > x + 1e-4f) hits++;
        }
    }
    return hits & 1;
}

int main(void)
{
    gs_mesh box, peg, hole;
    gs_cam cam;
    unsigned char *rgb;
    float vol;
    int i, coloured = 0;
    memset(&box, 0, sizeof box);
    memset(&peg, 0, sizeof peg);
    memset(&hole, 0, sizeof hole);
    expect(gm_cube(&box, 0, 0, 0, 20, 20, 20) == 0, "cube");
    expect(gm_cylinder(&peg, 10, 10, -1, 6, 22, 32) == 0, "cylinder");
    expect(gm_subtract(&hole, &box, &peg) == 0, "subtract");
    expect(hole.n > 12, "the hole added triangles");
    vol = volume(&hole);
    fprintf(stderr, "triangles %d  volume %.1f\n", hole.n, vol);
    expect(vol > 5700.f && vol < 5800.f, "volume is the cube minus the peg");
    expect(!inside(&hole, 10.f, 10.f, 10.f), "the middle is the hole");
    expect(inside(&hole, 1.3f, 1.7f, 10.2f), "a corner of the cube is solid");
    {
        gs_mesh left, right, joined, shared, miss;
        float jv, sv;
        memset(&left, 0, sizeof left);
        memset(&right, 0, sizeof right);
        memset(&joined, 0, sizeof joined);
        memset(&shared, 0, sizeof shared);
        memset(&miss, 0, sizeof miss);
        expect(gm_cube(&left, 0, 0, 0, 20, 20, 20) == 0, "left cube");
        expect(gm_cube(&right, 10, 0, 0, 20, 20, 20) == 0, "right cube");
        expect(gm_union(&joined, &left, &right) == 0, "union");
        jv = volume(&joined);
        fprintf(stderr, "union %.1f\n", jv);
        expect(jv > 11900.f && jv < 12100.f, "union is both cubes, overlap once");
        expect(inside(&joined, 4.f, 7.f, 9.f), "union keeps the left cube");
        expect(inside(&joined, 26.f, 7.f, 9.f), "union keeps the right cube");
        expect(gm_intersect(&shared, &left, &right) == 0, "intersect");
        sv = volume(&shared);
        fprintf(stderr, "intersect %.1f\n", sv);
        expect(sv > 3900.f && sv < 4100.f, "intersect is the overlap");
        expect(inside(&shared, 15.f, 8.f, 12.f), "the overlap is solid");
        expect(!inside(&shared, 4.f, 7.f, 9.f), "outside the overlap is empty");
        expect(gm_cube(&miss, 40, 40, 40, 10, 10, 10) == 0, "a cube that misses");
        expect(gm_intersect(&shared, &left, &miss) == 0, "intersect of a miss");
        expect(shared.n == 0, "a miss leaves nothing");
        gs_mesh_free(&left);
        gs_mesh_free(&right);
        gs_mesh_free(&joined);
        gs_mesh_free(&shared);
        gs_mesh_free(&miss);
    }
    expect(gm_stl_write(&hole, "/tmp/gfopenslicer/build/hole.stl") == 0, "stl");
    rgb = (unsigned char *)malloc(480 * 360 * 3);
    expect(rgb != NULL, "picture");
    if (rgb) {
        gs_cam_home(&cam);
        expect(gs_view_solid(&hole, &cam, rgb, 480, 360) == 0, "view");
        for (i = 0; i < 480 * 360 * 3; i += 3)
            if (rgb[i] > 40 || rgb[i + 2] > 40) coloured++;
        expect(coloured > 2000, "the solid covers the picture");
        {
            FILE *f = fopen("/tmp/gfopenslicer/build/hole.ppm", "wb");
            expect(f != NULL, "ppm");
            if (f) {
                fprintf(f, "P6\n480 360\n255\n");
                fwrite(rgb, 1, 480 * 360 * 3, f);
                fclose(f);
            }
        }
        free(rgb);
    }
    gs_mesh_free(&box);
    gs_mesh_free(&peg);
    gs_mesh_free(&hole);
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("model ok\n");
    return 0;
}
