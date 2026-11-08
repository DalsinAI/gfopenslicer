/* GFOpenSlicer on the Amiga. The window is GFOpenLayout: this file opens
 * it, asks ASL for a file, and fills the list from the slice.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_job.h"
#include "gs_view.h"
#include "markup.h"

#include <devices/inputevent.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <libraries/asl.h>
#include <intuition/intuition.h>

#include <gfopenlayout_ui.h>
#include <gfopenlayout_ui_amiga.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = GS_VERSTR;
static gs_job job;
static gs_cam cam;
static struct Window *win;
static int dragging;
static int drag_x, drag_y;
static int drag_shift;

static void say(olui *ui, const char *text)
{
    olui_set_text(ui, olui_find(ui, "status"), text);
}

static float number(olui *ui, const char *id)
{
    olui_el *el = olui_find(ui, id);
    int dec = olui_number_decimals(el);
    int v = olui_number_value(el);
    float d = 1.f;
    while (dec-- > 0) d *= 10.f;
    return (float)v / d;
}

static void show_layer(olui *ui, int index)
{
    (void)index;
    olui_repaint(ui, olui_find(ui, "view"), NULL);
}

/* Drag turns the solid. Shift-drag pans. The wheel zooms. The picture is
 * the CPU drawer; a chip takes the same triangles when gfopengpu.library
 * says OGPU_ONGPU. */
static void draw_view(void *userdata, const olui_el *el, int x, int y, int w, int h)
{
    unsigned char *rgb;
    int row, col;
    (void)userdata;
    (void)el;
    rgb = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (!rgb) return;
    if (gs_view_solid(&job.mesh, &cam, rgb, w, h)) {
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

static void fill_layers(olui *ui)
{
    olui_el *list = olui_find(ui, "layers");
    char name[32], value[16];
    int i;
    olui_list_clear(ui, list);
    for (i = 0; i < job.layers; i++) {
        gs_job_layer_name(&job, i, name, (int)sizeof name);
        snprintf(value, sizeof value, "%d", i);
        olui_list_add(ui, list, NULL, name, value);
    }
    if (job.layers) show_layer(ui, 0);
}

static void on_open(olui *ui, const olui_event *ev, void *userdata)
{
    struct FileRequester *fr;
    char path[256];
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
    if (AslRequest(fr, NULL)) {
        if (fr->fr_Drawer && fr->fr_File) {
            size_t n = strlen(fr->fr_Drawer);
            int slash = n && (fr->fr_Drawer[n - 1] == '/' || fr->fr_Drawer[n - 1] == ':');
            snprintf(path, sizeof path, slash ? "%s%s" : "%s/%s", fr->fr_Drawer, fr->fr_File);
            olui_set_text(ui, olui_find(ui, "path"), path);
            say(ui, "Ready to slice");
        }
    }
    FreeAslRequest(fr);
}

static void on_slice(olui *ui, const olui_event *ev, void *userdata)
{
    gs_cfg cfg;
    char err[160], status[160];
    const char *path = olui_get_text(olui_find(ui, "path"));
    (void)ev;
    (void)userdata;
    gs_cfg_default(&cfg);
    cfg.layer_h = number(ui, "layer");
    cfg.infill = number(ui, "infill");
    cfg.line_w = number(ui, "line");
    cfg.filament = number(ui, "filament");
    cfg.hotend = number(ui, "hotend");
    cfg.bed = number(ui, "bed");
    cfg.skirt = olui_get_checked(olui_find(ui, "skirt"));
    olui_set_progress(ui, olui_find(ui, "progress"), 10);
    if (gs_job_slice(&job, path ? path : "", NULL, &cfg, err, (int)sizeof err)) {
        say(ui, err);
        olui_set_progress(ui, olui_find(ui, "progress"), 0);
        return;
    }
    fill_layers(ui);
    snprintf(status, sizeof status, "%d triangles, %d layers, %s drawer", job.triangles, job.layers,
             gs_view_on_gpu() ? "GPU" : "CPU");
    say(ui, status);
    olui_set_progress(ui, olui_find(ui, "progress"), 100);
}

static void on_layer(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    if (ev->type != OLUI_SELECT) return;
    show_layer(ui, ev->index);
}

int main(void)
{
    olui *ui;
    olui_amiga *a;
    struct Window *w;
    struct IntuiMessage *m;
    char err[160];
    (void)version;
    ui = olui_new();
    if (!ui || !olui_load(ui, gs_markup, gs_css, err, sizeof err)) {
        if (ui) olui_free(ui);
        fprintf(stderr, "GFOpenSlicer: %s\n", ui ? err : "GFOpenLayout is not open");
        return 20;
    }
    olui_on(ui, "open", OLUI_CLICK, on_open, NULL);
    olui_on(ui, "slice", OLUI_CLICK, on_slice, NULL);
    olui_on(ui, "layers", OLUI_SELECT, on_layer, NULL);
    gs_job_init(&job);
    gs_cam_home(&cam);
    w = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenSlicer",
        WA_InnerWidth, 640,
        WA_InnerHeight, 400,
        WA_MinWidth, 360,
        WA_MinHeight, 240,
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
        gs_job_free(&job);
        olui_free(ui);
        fprintf(stderr, "GFOpenSlicer: the window did not open\n");
        return 20;
    }
    win = w;
    a = olui_amiga_new(ui, w);
    if (!a) {
        CloseWindow(w);
        gs_job_free(&job);
        olui_free(ui);
        fprintf(stderr, "GFOpenSlicer: GFOpenLayout could not draw the window\n");
        return 20;
    }
    olui_amiga_theme(a, NULL);
    olui_amiga_own(a, olui_find(ui, "view"), draw_view, NULL);
    olui_amiga_layout(a);
    for (;;) {
        int r;
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
                olui_rect box;
                if (olui_box(ui, olui_find(ui, "view"), &box) &&
                    mx >= w->BorderLeft + box.x && mx < w->BorderLeft + box.x + box.w &&
                    my >= w->BorderTop + box.y && my < w->BorderTop + box.y + box.h) {
                    dragging = 1;
                    drag_x = mx;
                    drag_y = my;
                    drag_shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
                }
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTUP) {
                dragging = 0;
            } else if (cls == IDCMP_MOUSEMOVE && dragging) {
                float dx = (float)(mx - drag_x), dy = (float)(my - drag_y);
                if (drag_shift) gs_cam_pan(&cam, dx, dy);
                else gs_cam_orbit(&cam, dx * 0.01f, dy * 0.01f);
                drag_x = mx;
                drag_y = my;
                olui_repaint(ui, olui_find(ui, "view"), NULL);
            }
        }
        olui_amiga_draw(a);
    }
done:
    olui_amiga_free(a);
    CloseWindow(w);
    gs_job_free(&job);
    olui_free(ui);
    return 0;
}
