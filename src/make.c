/* GFOpenMake on the Amiga. A 20 mm cube with a hole through it, on a 60 mm
 * plate, in a GFOpenLayout window. Orbit turns the view. Move, rotate and
 * scale change the solid. Drop, Centre and Lay flat put it on the bed.
 *
 * The triangles are submitted as OGPU_OP_TRIANGLES, with a depth buffer,
 * whenever gfopengpu.library can draw them. The picture is shown through
 * a retained surface, so a chip and the library's CPU back end take the
 * same batch. The pen drawer is only the fallback.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_edit.h"
#include "gs_gpu.h"
#include "gs_model.h"

#include <devices/inputevent.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <exec/libraries.h>
#include <exec/memory.h>

#include <gfopenlayout_ui.h>
#include <gfopenlayout_ui_amiga.h>
#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/retained.h>
#include <proto/gfopengpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = "$VER: GFOpenMake 0.1.0 (8.11.2026) AmiCradle Team";

static const char markup[] =
    "<window>"
    "<row><h2>GFOpenMake</h2><spacer></spacer><span id=\"ver\">0.1.0</span></row>"
    "<row>"
    "<button id=\"open\">Open</button>"
    "<button id=\"save\">Save</button>"
    "<button id=\"undo\">Undo</button>"
    "<spacer></spacer>"
    "<button id=\"orbit\">Orbit</button>"
    "<button id=\"move\">Move</button>"
    "<button id=\"rot\">Rotate</button>"
    "<button id=\"scale\">Scale</button>"
    "</row>"
    "<row>"
    "<button id=\"addcube\">Cube</button>"
    "<button id=\"addcyl\">Cylinder</button>"
    "<button id=\"join\">Join</button>"
    "<button id=\"cut\">Cut</button>"
    "<button id=\"meet\">Intersect</button>"
    "</row>"
    "<row>"
    "<button id=\"drop\">Drop</button>"
    "<button id=\"centre\">Centre</button>"
    "<button id=\"lay\">Lay flat</button>"
    "<spacer></spacer>"
    "<span id=\"face\">no face</span>"
    "</row>"
    "<div id=\"view\" style=\"flex:1\"></div>"
    "<span id=\"status\">A 20 mm cube, with a hole.</span>"
    "</window>";

static const char css[] =
    "window{height:100%}"
    "#view{flex:1;background-color:#14161c}";

static gs_mesh hole;
static gs_mesh added;
static gs_mesh shown;
static int has_added, on_added;
static gs_cam cam;
static struct Window *win;
static int dragging, drag_x, drag_y, drag_shift, drag_moved;
static int gpu_on, on_chip, draw_grid, presented;
static int tool, picked = -1, pushed, vw = 640, vh = 320, said_chip;
static ge_hist *hist;
static char stl_path[256] = "RAM:hole.stl";
struct Library *OpenGPUBase;

static struct OGPURetained *rs;
static uint32_t *depth, *verts, *batch;
static ULONG depth_bytes, vert_bytes, batch_bytes;
static int pic_w, pic_h, vert_cap;

static gs_mesh *work(void)
{
    return (has_added && on_added) ? &added : &hole;
}

static void mesh_bounds(gs_mesh *m)
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

static void rebuild_shown(void)
{
    int n = hole.n + (has_added ? added.n : 0);
    int i, k = 0;
    gs_tri *t = n ? (gs_tri *)malloc((size_t)n * sizeof *t) : NULL;
    if (n && !t) return;
    for (i = 0; i < hole.n; i++) t[k++] = hole.t[i];
    if (has_added) {
        for (i = 0; i < added.n; i++) t[k++] = added.t[i];
    }
    free(shown.t);
    memset(&shown, 0, sizeof shown);
    shown.t = t;
    shown.n = shown.cap = n;
    mesh_bounds(&shown);
}

static void say(olui *ui, const char *text)
{
    olui_set_text(ui, olui_find(ui, "status"), text);
}

static void gpu_free(void)
{
    if (rs && OpenGPUBase) OGPU_RetainedDestroy(rs);
    rs = NULL;
    if (depth) FreeMem(depth, depth_bytes);
    depth = NULL;
    depth_bytes = 0;
    if (verts) FreeMem(verts, vert_bytes);
    verts = NULL;
    vert_bytes = 0;
    if (batch) FreeMem(batch, batch_bytes);
    batch = NULL;
    batch_bytes = 0;
    pic_w = pic_h = 0;
}

static int gpu_ensure(int w, int h)
{
    ULONG bytes;
    if (w < 4 || h < 4 || w > 2048 || h > 2048) return 0;
    if (rs && depth && pic_w == w && pic_h == h && rs->rs_Pixels) return 1;
    if (rs && OpenGPUBase) OGPU_RetainedDestroy(rs);
    rs = NULL;
    if (depth) FreeMem(depth, depth_bytes);
    depth = NULL;
    depth_bytes = 0;
    rs = OGPU_RetainedCreate((ULONG)w, (ULONG)h, OGPU_FMT_ARGB32, 0);
    bytes = (ULONG)w * (ULONG)h * 4UL;
    if (rs && rs->rs_Pixels) depth = (uint32_t *)AllocMem(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rs || !rs->rs_Pixels || !depth || rs->rs_BytesPerRow < (ULONG)w * 4UL) {
        if (rs && OpenGPUBase) OGPU_RetainedDestroy(rs);
        rs = NULL;
        if (depth) FreeMem(depth, bytes);
        depth = NULL;
        return 0;
    }
    depth_bytes = bytes;
    pic_w = w;
    pic_h = h;
    return 1;
}

static int draw_gpu(int x, int y, int w, int h)
{
    int nv, nw;
    ULONG fence = 0;
    if (!gpu_on || !verts || !batch) return 0;
    if (!gpu_ensure(w, h)) return 0;
    rebuild_shown();
    nv = gs_gpu_vertices_plate(&shown, &cam, w, h, verts, vert_cap);
    if (nv < 3) return 0;
    nw = gs_gpu_stream_plate(batch, (int)(batch_bytes / 4),
                       (uint32_t)(uintptr_t)verts, nv,
                       (uint32_t)(uintptr_t)rs->rs_Pixels, (int)rs->rs_BytesPerRow,
                       (uint32_t)(uintptr_t)depth, w, h, &shown, &cam, draw_grid, 1);
    if (nw < 1) return 0;
    if (OGPU_Submit(batch, (ULONG)nw, &fence) != OGPU_OK) return 0;
    if (OGPU_Wait(fence) != OGPU_OK) return 0;
    OGPU_RetainedMark(rs, 0, 0, w, h);
    if (OGPU_RetainedPresent(rs, win->RPort, x, y, w, h) < 0) return 0;
    presented = 1;
    return 1;
}

static void draw_pens(int x, int y, int w, int h)
{
    unsigned char *rgb;
    int row, col;
    rgb = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (!rgb) return;
    vw = w;
    vh = h;
    rebuild_shown();
    if (gs_view_bed(&shown, &cam, rgb, w, h)) {
        free(rgb);
        return;
    }
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            unsigned char *p = rgb + ((size_t)row * (size_t)w + (size_t)col) * 3;
            int pen = p[0] < 40 ? 0 : p[0] < 140 ? 1 : 2;
            SetAPen(win->RPort, (UBYTE)pen);
            WritePixel(win->RPort, (WORD)(x + col), (WORD)(y + row));
        }
    }
    free(rgb);
}

static void draw_view(void *userdata, const olui_el *el, int x, int y, int w, int h)
{
    (void)userdata;
    (void)el;
    vw = w;
    vh = h;
    if (!draw_gpu(x, y, w, h)) draw_pens(x, y, w, h);
}

enum { TOOL_ORBIT, TOOL_MOVE, TOOL_ROT, TOOL_SCALE };

static void face_line(olui *ui)
{
    char buf[32];
    if (picked >= 0) snprintf(buf, sizeof buf, "face %d", picked);
    else snprintf(buf, sizeof buf, "no face");
    olui_set_text(ui, olui_find(ui, "face"), buf);
}

static const char *tool_label(void)
{
    if (tool == TOOL_MOVE) return "Move. Drag slides. Shift-drag lifts.";
    if (tool == TOOL_ROT) return "Rotate. Drag turns the part.";
    if (tool == TOOL_SCALE) return "Scale. Drag up grows it.";
    return "Orbit. Drag turns the view. A click picks a face.";
}

static void refresh(olui *ui)
{
    char status[180];
    snprintf(status, sizeof status, "%s, %d triangles%s. %s",
             (has_added && on_added) ? "added part" : "stock",
             work()->n,
             presented ? (on_chip ? ", chip" : ", OpenGPU") : "", tool_label());
    say(ui, status);
    face_line(ui);
    olui_repaint(ui, olui_find(ui, "view"), NULL);
}

static void remember(void)
{
    if (hist && work()->n) ge_hist_push(hist, work());
}

static void verts_fit(void)
{
    int need = hole.n + (has_added ? added.n : 0);
    need *= 3;
    uint32_t *p;
    ULONG bytes;
    if (!verts || need <= vert_cap) return;
    bytes = (ULONG)need * 6UL * 4UL;
    p = (uint32_t *)AllocMem(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!p) return;
    FreeMem(verts, vert_bytes);
    verts = p;
    vert_bytes = bytes;
    vert_cap = need;
}

static void path_join(char *path, int n, const char *drawer, const char *file)
{
    size_t len = drawer ? strlen(drawer) : 0;
    int slash = len && (drawer[len - 1] == '/' || drawer[len - 1] == ':');
    snprintf(path, (size_t)n, slash ? "%s%s" : "%s/%s", drawer ? drawer : "", file ? file : "");
}

static void on_open(olui *ui, const olui_event *ev, void *userdata)
{
    struct FileRequester *fr;
    char path[256], err[160];
    gs_mesh next;
    (void)ev;
    (void)userdata;
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)"Open an STL",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, (ULONG)"#?.stl",
        TAG_END);
    if (!fr) {
        say(ui, "The file requester did not open");
        return;
    }
    if (AslRequest(fr, NULL) && fr->fr_Drawer && fr->fr_File) {
        path_join(path, (int)sizeof path, fr->fr_Drawer, fr->fr_File);
        memset(&next, 0, sizeof next);
        if (gs_stl_load(&next, path, err, (int)sizeof err)) {
            say(ui, err[0] ? err : "Could not open that STL");
        } else {
            remember();
            gs_mesh_free(work());
            *work() = next;
            if (work() == &added) has_added = 1;
            picked = -1;
            strncpy(stl_path, path, sizeof stl_path - 1);
            stl_path[sizeof stl_path - 1] = 0;
            verts_fit();
            refresh(ui);
        }
    }
    FreeAslRequest(fr);
}

static void on_save(olui *ui, const olui_event *ev, void *userdata)
{
    struct FileRequester *fr;
    char path[256];
    (void)ev;
    (void)userdata;
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)"Save an STL",
        ASLFR_DoSaveMode, TRUE,
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, (ULONG)"#?.stl",
        TAG_END);
    if (!fr) {
        say(ui, "The file requester did not open");
        return;
    }
    if (AslRequest(fr, NULL) && fr->fr_Drawer && fr->fr_File) {
        path_join(path, (int)sizeof path, fr->fr_Drawer, fr->fr_File);
        if (gm_stl_write(work(), path)) say(ui, "Could not save that STL");
        else {
            strncpy(stl_path, path, sizeof stl_path - 1);
            stl_path[sizeof stl_path - 1] = 0;
            say(ui, path);
        }
    }
    FreeAslRequest(fr);
}

static void on_undo(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ev;
    (void)userdata;
    if (!hist || ge_hist_pop(hist, work())) {
        say(ui, "Nothing to undo");
        return;
    }
    picked = -1;
    verts_fit();
    refresh(ui);
}

static void on_tool(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ev;
    tool = (int)(intptr_t)userdata;
    refresh(ui);
}

static void on_drop(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ev;
    (void)userdata;
    remember();
    ge_drop(work());
    refresh(ui);
}

static void on_centre(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ev;
    (void)userdata;
    remember();
    ge_centre(work());
    refresh(ui);
}

static void on_lay(olui *ui, const olui_event *ev, void *userdata)
{
    int face = picked >= 0 ? picked : ge_largest_face(work());
    gs_v was_min, was_max;
    int same;
    gs_mesh *m;
    (void)ev;
    (void)userdata;
    if (face < 0) {
        say(ui, "No face to lay");
        return;
    }
    m = work();
    was_min = m->min;
    was_max = m->max;
    remember();
    if (ge_lay_face(m, face)) {
        if (hist) ge_hist_pop(hist, m);
        say(ui, "That face will not lie flat");
        return;
    }
    same = fabsf(m->min.x - was_min.x) < 1e-3f && fabsf(m->min.y - was_min.y) < 1e-3f &&
           fabsf(m->min.z - was_min.z) < 1e-3f && fabsf(m->max.x - was_max.x) < 1e-3f &&
           fabsf(m->max.y - was_max.y) < 1e-3f && fabsf(m->max.z - was_max.z) < 1e-3f;
    if (same) {
        if (hist) ge_hist_pop(hist, m);
        say(ui, "That face is already flat on the bed");
        return;
    }
    picked = face;
    refresh(ui);
}

static int axes(float b[3][2])
{
    gs_frame f;
    float ox, oy, oz, px, py, pz;
    gs_v o, p;
    int k;
    static const float step[3][3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
    if (vw < 4 || vh < 4) return -1;
    if (gs_view_frame_bed(&hole, &cam, vw, vh, 0.f, 0.f, GS_BED, GS_BED, &f)) return -1;
    o = f.origin;
    gs_view_map(&f, &cam, o, &ox, &oy, &oz);
    for (k = 0; k < 3; k++) {
        p.x = o.x + step[k][0];
        p.y = o.y + step[k][1];
        p.z = o.z + step[k][2];
        gs_view_map(&f, &cam, p, &px, &py, &pz);
        b[k][0] = px - ox;
        b[k][1] = py - oy;
    }
    return 0;
}

static void drag_edit(olui *ui, float dx, float dy)
{
    if (dx == 0.f && dy == 0.f) return;
    if (!pushed) {
        remember();
        pushed = 1;
    }
    if (tool == TOOL_MOVE) {
        float b[3][2];
        if (axes(b)) return;
        if (drag_shift) {
            float len2 = b[2][0] * b[2][0] + b[2][1] * b[2][1];
            float mz = len2 < 1e-4f ? 0.f : (dx * b[2][0] + dy * b[2][1]) / len2;
            ge_move(work(), 0.f, 0.f, mz);
        } else {
            float a00 = b[0][0] * b[0][0] + b[0][1] * b[0][1];
            float a01 = b[0][0] * b[1][0] + b[0][1] * b[1][1];
            float a11 = b[1][0] * b[1][0] + b[1][1] * b[1][1];
            float rx = dx * b[0][0] + dy * b[0][1];
            float ry = dx * b[1][0] + dy * b[1][1];
            float det = a00 * a11 - a01 * a01;
            float mx, my;
            if (det < 1e-4f && det > -1e-4f) return;
            mx = (rx * a11 - ry * a01) / det;
            my = (a00 * ry - a01 * rx) / det;
            ge_move(work(), mx, my, 0.f);
        }
    } else if (tool == TOOL_ROT) {
        ge_rotate(work(), dy * 0.01f, 0.f, dx * 0.01f);
    } else if (tool == TOOL_SCALE) {
        float s = expf(-dy * 0.005f);
        ge_scale(work(), s, s, s);
    }
    refresh(ui);
}

static int in_view(olui *ui, struct Window *w, int mx, int my, int *lx, int *ly)
{
    olui_rect box;
    int x0, y0;
    if (!olui_box(ui, olui_find(ui, "view"), &box)) return 0;
    x0 = w->BorderLeft + box.x;
    y0 = w->BorderTop + box.y;
    if (mx < x0 || my < y0 || mx >= x0 + box.w || my >= y0 + box.h) return 0;
    if (lx) *lx = mx - x0;
    if (ly) *ly = my - y0;
    vw = box.w;
    vh = box.h;
    return 1;
}

static void take_added(olui *ui, gs_mesh box)
{
    if (has_added) gs_mesh_free(&added);
    added = box;
    has_added = 1;
    on_added = 1;
    picked = -1;
    verts_fit();
    refresh(ui);
}

static void on_add_cube(olui *ui, const olui_event *ev, void *userdata)
{
    gs_mesh box;
    (void)ev;
    (void)userdata;
    memset(&box, 0, sizeof box);
    if (gm_cube(&box, 12.f, 4.f, 0.f, 16.f, 16.f, 16.f)) {
        say(ui, "The cube did not build");
        return;
    }
    take_added(ui, box);
}

static void on_add_cyl(olui *ui, const olui_event *ev, void *userdata)
{
    gs_mesh peg;
    float cx, cy, h, r;
    (void)ev;
    (void)userdata;
    memset(&peg, 0, sizeof peg);
    cx = (hole.min.x + hole.max.x) * 0.5f;
    cy = (hole.min.y + hole.max.y) * 0.5f;
    h = (hole.max.z - hole.min.z) + 4.f;
    if (h < 8.f) h = 8.f;
    r = (hole.max.x - hole.min.x) * 0.22f;
    if (r < 3.f) r = 3.f;
    if (gm_cylinder(&peg, cx, cy, hole.min.z - 2.f, r, h, 24)) {
        say(ui, "The cylinder did not build");
        return;
    }
    take_added(ui, peg);
}

static void on_bool(olui *ui, const olui_event *ev, void *userdata)
{
    int op = (int)(intptr_t)userdata;
    gs_mesh out;
    gs_mesh *a;
    gs_mesh *b;
    int rc;
    (void)ev;
    memset(&out, 0, sizeof out);
    if (!has_added) {
        say(ui, "Add a second part first");
        return;
    }
    /* Cut removes the selected part from the other one. */
    if (op == 1 && on_added) {
        a = &hole;
        b = &added;
    } else if (op == 1) {
        a = &added;
        b = &hole;
    } else {
        a = &hole;
        b = &added;
    }
    if (hist) ge_hist_push(hist, &hole);
    if (op == 0) rc = gm_union(&out, a, b);
    else if (op == 1) rc = gm_subtract(&out, a, b);
    else rc = gm_intersect(&out, a, b);
    if (rc || out.n < 1) {
        gs_mesh_free(&out);
        if (hist) ge_hist_pop(hist, &hole);
        say(ui, "Those two do not meet");
        return;
    }
    gs_mesh_free(&hole);
    gs_mesh_free(&added);
    memset(&added, 0, sizeof added);
    hole = out;
    has_added = 0;
    on_added = 0;
    picked = -1;
    verts_fit();
    refresh(ui);
}

static int takes(unsigned long op, unsigned long fmt)
{
    return OGPU_ANSWER(OGPU_Query(op, fmt)) != OGPU_NONE;
}

int main(int argc, char **argv)
{
    olui *ui;
    olui_amiga *a;
    struct Window *w;
    struct IntuiMessage *m;
    gs_mesh box, peg;
    char err[160];
    (void)version;
    memset(&box, 0, sizeof box);
    memset(&peg, 0, sizeof peg);
    memset(&hole, 0, sizeof hole);
    if (argc > 1 && argv[1] && argv[1][0]) {
        strncpy(stl_path, argv[1], sizeof stl_path - 1);
        stl_path[sizeof stl_path - 1] = 0;
    }
    if (gm_cube(&box, 0, 0, 0, 20, 20, 20) || gm_cylinder(&peg, 10, 10, -1, 6, 22, 32) ||
        gm_subtract(&hole, &box, &peg)) {
        fprintf(stderr, "GFOpenMake: the solid did not build\n");
        return 20;
    }
    gs_mesh_free(&box);
    gs_mesh_free(&peg);
    /* Off the plate, so Drop is a visible move. The cut itself sits on z = 0. */
    ge_move(&hole, 0.f, 0.f, 8.f);
    hist = ge_hist_new();
    if (gm_stl_write(&hole, stl_path)) {
        fprintf(stderr, "GFOpenMake: could not write %s\n", stl_path);
    }
    OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
    gpu_on = 0;
    on_chip = 0;
    draw_grid = 0;
    if (OpenGPUBase && OpenGPUBase->lib_Revision >= OGPU_RETAINED_REVISION &&
        takes(OGPU_OP_TRIANGLES, OGPU_FMT_ARGB32) &&
        takes(OGPU_OP_RENDER3D, OGPU_FMT_ARGB32) &&
        (takes(OGPU_OP_DEPTH, OGPU_FMT_ARGB32) || takes(OGPU_OP_DEPTH, OGPU_FMT_Z32)) &&
        (takes(OGPU_OP_CLEAR3D, OGPU_FMT_ARGB32) || takes(OGPU_OP_CLEAR3D, 0))) {
        unsigned long q = OGPU_Query(OGPU_OP_TRIANGLES, OGPU_FMT_ARGB32);
        on_chip = OGPU_ONGPU(q);
        draw_grid = takes(OGPU_OP_LINE, OGPU_FMT_ARGB32);
        vert_cap = hole.n * 3;
        vert_bytes = (ULONG)vert_cap * 6UL * 4UL;
        batch_bytes = 512UL * 4UL;
        verts = (uint32_t *)AllocMem(vert_bytes, MEMF_PUBLIC | MEMF_CLEAR);
        batch = (uint32_t *)AllocMem(batch_bytes, MEMF_PUBLIC | MEMF_CLEAR);
        gpu_on = verts && batch;
        if (!gpu_on) gpu_free();
    }
    ui = olui_new();
    if (!ui || !olui_load(ui, markup, css, err, sizeof err)) {
        if (ui) olui_free(ui);
        ge_hist_free(hist);
        gs_mesh_free(&hole);
        gs_mesh_free(&added);
        gs_mesh_free(&shown);
        gpu_free();
        if (OpenGPUBase) CloseLibrary(OpenGPUBase);
        fprintf(stderr, "GFOpenMake: %s\n", ui ? err : "GFOpenLayout is not open");
        return 20;
    }
    olui_on(ui, "open", OLUI_CLICK, on_open, NULL);
    olui_on(ui, "save", OLUI_CLICK, on_save, NULL);
    olui_on(ui, "undo", OLUI_CLICK, on_undo, NULL);
    olui_on(ui, "orbit", OLUI_CLICK, on_tool, (void *)(intptr_t)TOOL_ORBIT);
    olui_on(ui, "move", OLUI_CLICK, on_tool, (void *)(intptr_t)TOOL_MOVE);
    olui_on(ui, "rot", OLUI_CLICK, on_tool, (void *)(intptr_t)TOOL_ROT);
    olui_on(ui, "scale", OLUI_CLICK, on_tool, (void *)(intptr_t)TOOL_SCALE);
    olui_on(ui, "drop", OLUI_CLICK, on_drop, NULL);
    olui_on(ui, "centre", OLUI_CLICK, on_centre, NULL);
    olui_on(ui, "lay", OLUI_CLICK, on_lay, NULL);
    olui_on(ui, "addcube", OLUI_CLICK, on_add_cube, NULL);
    olui_on(ui, "addcyl", OLUI_CLICK, on_add_cyl, NULL);
    olui_on(ui, "join", OLUI_CLICK, on_bool, (void *)(intptr_t)0);
    olui_on(ui, "cut", OLUI_CLICK, on_bool, (void *)(intptr_t)1);
    olui_on(ui, "meet", OLUI_CLICK, on_bool, (void *)(intptr_t)2);
    gs_cam_home(&cam);
    w = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenMake",
        WA_InnerWidth, 640,
        WA_InnerHeight, 520,
        WA_MinWidth, 320,
        WA_MinHeight, 280,
        WA_MaxWidth, (ULONG)~0,
        WA_MaxHeight, (ULONG)~0,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_SizeBBottom, TRUE,
        WA_IDCMP, olui_amiga_idcmp(),
        WA_ReportMouse, TRUE,
        WA_Activate, TRUE,
        TAG_END);
    if (!w) {
        ge_hist_free(hist);
        gs_mesh_free(&hole);
        gs_mesh_free(&added);
        gs_mesh_free(&shown);
        olui_free(ui);
        gpu_free();
        if (OpenGPUBase) CloseLibrary(OpenGPUBase);
        fprintf(stderr, "GFOpenMake: the window did not open\n");
        return 20;
    }
    win = w;
    a = olui_amiga_new(ui, w);
    if (!a) {
        CloseWindow(w);
        ge_hist_free(hist);
        gs_mesh_free(&hole);
        gs_mesh_free(&added);
        gs_mesh_free(&shown);
        olui_free(ui);
        gpu_free();
        if (OpenGPUBase) CloseLibrary(OpenGPUBase);
        fprintf(stderr, "GFOpenMake: GFOpenLayout could not draw the window\n");
        return 20;
    }
    olui_amiga_theme(a, NULL);
    olui_amiga_own(a, olui_find(ui, "view"), draw_view, NULL);
    olui_amiga_layout(a);
    refresh(ui);
    for (;;) {
        int r, lx, ly;
        WaitPort(w->UserPort);
        while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            WORD mx = m->MouseX, my = m->MouseY;
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD qual = m->Qualifier;
            r = olui_amiga_event(a, m);
            ReplyMsg((struct Message *)m);
            if (r == OLUI_AMIGA_CLOSE) goto done;
            if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                if (in_view(ui, w, mx, my, NULL, NULL)) {
                    dragging = 1;
                    drag_moved = 0;
                    pushed = 0;
                    drag_x = mx;
                    drag_y = my;
                    drag_shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
                }
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTUP) {
                if (dragging && tool == TOOL_ORBIT && drag_moved < 4 &&
                    in_view(ui, w, mx, my, &lx, &ly)) {
                    int face = -1;
                    if (has_added) face = ge_pick(&added, &cam, vw, vh, lx, ly);
                    if (face >= 0) on_added = 1;
                    else {
                        face = ge_pick(&hole, &cam, vw, vh, lx, ly);
                        on_added = 0;
                    }
                    picked = face;
                    face_line(ui);
                }
                dragging = 0;
                pushed = 0;
            } else if (cls == IDCMP_MOUSEMOVE && dragging) {
                float dx = (float)(mx - drag_x), dy = (float)(my - drag_y);
                int adx = mx - drag_x, ady = my - drag_y;
                if (adx < 0) adx = -adx;
                if (ady < 0) ady = -ady;
                drag_moved += adx + ady;
                drag_shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
                if (tool == TOOL_ORBIT) {
                    if (drag_shift) gs_cam_pan(&cam, dx / 400.f, -dy / 400.f);
                    else gs_cam_orbit(&cam, dx * 0.01f, dy * 0.01f);
                    olui_repaint(ui, olui_find(ui, "view"), NULL);
                } else drag_edit(ui, dx, dy);
                drag_x = mx;
                drag_y = my;
            }
        }
        olui_amiga_draw(a);
        if (presented && !said_chip) {
            said_chip = 1;
            refresh(ui);
        }
    }
done:
    olui_amiga_free(a);
    CloseWindow(w);
    ge_hist_free(hist);
    gs_mesh_free(&hole);
    gs_mesh_free(&added);
    gs_mesh_free(&shown);
    olui_free(ui);
    gpu_free();
    if (OpenGPUBase) CloseLibrary(OpenGPUBase);
    return 0;
}
