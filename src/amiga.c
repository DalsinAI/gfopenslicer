/* GFOpenSlicer on the Amiga. The window is GFOpenLayout: this file opens
 * it, asks ASL for a file, and fills the list from the slice.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_job.h"
#include "gs_view.h"
#include "markup.h"

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
static int view_i = -1;
static struct Window *win;

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
    view_i = index;
    olui_repaint(ui, olui_find(ui, "view"), NULL);
}

/* The view is drawn by us, into the window, over the dark bed GFOpenLayout
 * left. The lines are the same ones the host picture paints. */
static void draw_view(void *userdata, const olui_el *el, int x, int y, int w, int h)
{
    char *text;
    gs_line *ls = NULL;
    int n = 0, i;
    float minx, miny, maxx, maxy, dx, dy, s, ox, oy;
    (void)userdata;
    (void)el;
    SetAPen(win->RPort, 1);
    RectFill(win->RPort, (WORD)x, (WORD)y, (WORD)(x + w - 1), (WORD)(y + h - 1));
    if (view_i < 0 || !job.gcode) return;
    text = (char *)malloc(48 * 1024);
    if (!text || gs_job_layer(&job, view_i, text, 48 * 1024)) {
        free(text);
        return;
    }
    if (gs_view_lines(text, (int)strlen(text), &ls, &n, &minx, &miny, &maxx, &maxy) || !n) {
        free(ls);
        free(text);
        return;
    }
    dx = maxx - minx;
    dy = maxy - miny;
    if (dx < 0.1f) dx = 0.1f;
    if (dy < 0.1f) dy = 0.1f;
    s = (float)(w - 24) / dx;
    if ((float)(h - 24) / dy < s) s = (float)(h - 24) / dy;
    ox = (float)x + ((float)w - dx * s) * 0.5f;
    oy = (float)y + ((float)h - dy * s) * 0.5f;
    SetAPen(win->RPort, 2);
    for (i = 0; i < n; i++) {
        int x0 = (int)(ox + (ls[i].x0 - minx) * s);
        int y0 = (int)((float)(y + h) - (oy - (float)y + (ls[i].y0 - miny) * s));
        int x1 = (int)(ox + (ls[i].x1 - minx) * s);
        int y1 = (int)((float)(y + h) - (oy - (float)y + (ls[i].y1 - miny) * s));
        Move(win->RPort, (WORD)x0, (WORD)y0);
        Draw(win->RPort, (WORD)x1, (WORD)y1);
    }
    free(ls);
    free(text);
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
    snprintf(status, sizeof status, "%d triangles, %d layers, wrote %s", job.triangles, job.layers, job.out_path);
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
            r = olui_amiga_event(a, m);
            ReplyMsg((struct Message *)m);
            if (r == OLUI_AMIGA_CLOSE) goto done;
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
