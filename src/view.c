#include "gs_view.h"

#include <stdlib.h>
#include <string.h>

static int field(const char *s, const char *end, char key, float *o)
{
    const char *p;
    for (p = s; p + 1 < end; p++) {
        if ((p == s || p[-1] == ' ') && *p == key) {
            char *e;
            float v = strtof(p + 1, &e);
            if (e == p + 1) return 0;
            *o = v;
            return 1;
        }
    }
    return 0;
}

int gs_view_lines(const char *gcode, int len, gs_line **out, int *n,
                  float *minx, float *miny, float *maxx, float *maxy)
{
    const char *s, *end, *line;
    float x = 0, y = 0, nx, ny;
    int have = 0, cap = 0, count = 0;
    gs_line *ls = NULL;
    if (!gcode || len < 0 || !out || !n) return -1;
    *out = NULL;
    *n = 0;
    *minx = *miny = 1e30f;
    *maxx = *maxy = -1e30f;
    s = gcode;
    end = gcode + len;
    for (line = s; line < end; line = s) {
        const char *nl = line;
        int extrude;
        while (nl < end && *nl != '\n') nl++;
        s = nl < end ? nl + 1 : end;
        if (nl - line < 2 || line[0] != 'G') continue;
        extrude = line[1] == '1';
        if (line[1] != '0' && !extrude) continue;
        nx = x;
        ny = y;
        field(line, nl, 'X', &nx);
        field(line, nl, 'Y', &ny);
        if (!have) {
            x = nx;
            y = ny;
            have = 1;
            continue;
        }
        if (extrude && (nx != x || ny != y)) {
            gs_line L;
            L.x0 = x;
            L.y0 = y;
            L.x1 = nx;
            L.y1 = ny;
            if (count == cap) {
                int c = cap ? cap * 2 : 64;
                gs_line *p = (gs_line *)realloc(ls, (size_t)c * sizeof *p);
                if (!p) {
                    free(ls);
                    return -1;
                }
                ls = p;
                cap = c;
            }
            ls[count++] = L;
            if (x < *minx) *minx = x;
            if (nx < *minx) *minx = nx;
            if (y < *miny) *miny = y;
            if (ny < *miny) *miny = ny;
            if (x > *maxx) *maxx = x;
            if (nx > *maxx) *maxx = nx;
            if (y > *maxy) *maxy = y;
            if (ny > *maxy) *maxy = ny;
        }
        x = nx;
        y = ny;
    }
    *out = ls;
    *n = count;
    return 0;
}

static void put(unsigned char *rgb, int w, int h, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char *p;
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    p = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

static void stroke(unsigned char *rgb, int w, int h, int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, err, i;
    dx = ax;
    dy = ay;
    err = dx - dy;
    for (i = 0; i < ax + ay + 1; i++) {
        int e2;
        put(rgb, w, h, x0, y0, 232, 184, 74);
        put(rgb, w, h, x0 + 1, y0, 232, 184, 74);
        put(rgb, w, h, x0, y0 + 1, 232, 184, 74);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

int gs_view_paint(const char *gcode, int len, unsigned char *rgb, int w, int h)
{
    gs_line *ls = NULL;
    int n = 0, i;
    float minx, miny, maxx, maxy, dx, dy, s, ox, oy;
    if (!rgb || w < 4 || h < 4) return -1;
    memset(rgb, 0, (size_t)w * (size_t)h * 3);
    for (i = 0; i < w * h; i++) {
        rgb[i * 3] = 20;
        rgb[i * 3 + 1] = 22;
        rgb[i * 3 + 2] = 28;
    }
    if (gs_view_lines(gcode, len, &ls, &n, &minx, &miny, &maxx, &maxy)) return -1;
    if (!n) {
        free(ls);
        return 0;
    }
    dx = maxx - minx;
    dy = maxy - miny;
    if (dx < 0.1f) dx = 0.1f;
    if (dy < 0.1f) dy = 0.1f;
    s = (float)(w - 24) / dx;
    if ((float)(h - 24) / dy < s) s = (float)(h - 24) / dy;
    ox = ((float)w - dx * s) * 0.5f;
    oy = ((float)h - dy * s) * 0.5f;
    for (i = 0; i < n; i++) {
        int x0 = (int)(ox + (ls[i].x0 - minx) * s);
        int y0 = (int)((float)h - (oy + (ls[i].y0 - miny) * s));
        int x1 = (int)(ox + (ls[i].x1 - minx) * s);
        int y1 = (int)((float)h - (oy + (ls[i].y1 - miny) * s));
        stroke(rgb, w, h, x0, y0, x1, y1);
    }
    free(ls);
    return n;
}
