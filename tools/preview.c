/* Host picture of the GFOpenLayout window. The layout is the real one;
 * this only records the draw operations.
 *   preview empty out.ops
 *   preview sliced stl out.ops
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs_job.h"
#include "markup.h"

#include <gfopenlayout_ui.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *ops;

static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *metrics)
{
    (void)userdata;
    (void)utf8;
    metrics->width = (ol_unit)(length * (size_t)style->font_size * 60 / 100);
    metrics->ascent = style->font_size * 80 / 100;
    metrics->descent = style->font_size * 22 / 100;
    if (metrics->width < 0) metrics->width = 0;
    return 1;
}

static void draw(void *userdata, const olui_draw_op *op)
{
    int i;
    (void)userdata;
    if (op->alpha == 0) return;
    if (op->kind == OLUI_DRAW_BOX) {
        fprintf(ops, "B %d %d %d %d %d", op->box.x, op->box.y, op->box.w, op->box.h, op->stops);
        for (i = 0; i < op->stops && i < OLUI_MAX_STOPS; i++)
            fprintf(ops, " %08x %d", op->stop_argb[i], op->stop_pos[i]);
        fprintf(ops, " %08x %d %d %d\n", op->border_argb, op->border, op->radius, op->alpha);
    } else if (op->kind == OLUI_DRAW_TEXT && op->text && op->length) {
        fprintf(ops, "T %d %d %d %08x %d ", op->box.x, op->baseline, op->font_pixels, op->argb, op->alpha);
        fwrite(op->text, 1, op->length, ops);
        fputc('\n', ops);
    } else if (op->kind == OLUI_DRAW_MARK) {
        fprintf(ops, "M %d %d %d %d %d %08x %d\n", op->box.x, op->box.y, op->box.w, op->box.h, op->mark, op->argb, op->alpha);
    } else if (op->kind == OLUI_DRAW_CLIP) {
        fprintf(ops, "C %d %d %d %d\n", op->clip.x, op->clip.y, op->clip.w, op->clip.h);
    }
}

static float number(olui *ui, const char *id)
{
    olui_el *el = olui_find(ui, id);
    int dec = olui_number_decimals(el), v = olui_number_value(el);
    float d = 1.f;
    while (dec-- > 0) d *= 10.f;
    return (float)v / d;
}

int main(int argc, char **argv)
{
    olui *ui;
    char err[160];
    int w = 720, h = 390;
    if (argc < 3) return 2;
    ui = olui_new();
    olui_set_measure(ui, measure, NULL);
    if (!olui_load(ui, gs_markup, gs_css, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (strcmp(argv[1], "sliced") == 0) {
        gs_job job;
        gs_cfg cfg;
        char name[32], value[16], status[160], *layer;
        int i;
        size_t cap = 64 * 1024;
        gs_job_init(&job);
        gs_cfg_default(&cfg);
        cfg.layer_h = number(ui, "layer");
        cfg.infill = number(ui, "infill");
        cfg.line_w = number(ui, "line");
        cfg.filament = number(ui, "filament");
        cfg.hotend = number(ui, "hotend");
        cfg.bed = number(ui, "bed");
        cfg.skirt = olui_get_checked(olui_find(ui, "skirt"));
        if (argc < 4 || gs_job_slice(&job, argv[3], "/tmp/gs-preview.gcode", &cfg, err, (int)sizeof err)) {
            fprintf(stderr, "%s\n", argc < 4 ? "need an stl" : err);
            return 1;
        }
        olui_set_text(ui, olui_find(ui, "path"), "Work:Models/cube.stl");
        for (i = 0; i < job.layers; i++) {
            gs_job_layer_name(&job, i, name, (int)sizeof name);
            snprintf(value, sizeof value, "%d", i);
            olui_list_add(ui, olui_find(ui, "layers"), NULL, name, value);
        }
        layer = (char *)malloc(cap);
        if (layer && !gs_job_layer(&job, 0, layer, (int)cap))
            olui_set_text(ui, olui_find(ui, "moves"), layer);
        free(layer);
        snprintf(status, sizeof status, "%d triangles, %d layers, wrote Work:Models/cube.gcode", job.triangles, job.layers);
        olui_set_text(ui, olui_find(ui, "status"), status);
        olui_set_progress(ui, olui_find(ui, "progress"), 100);
        gs_job_free(&job);
    }
    if (!olui_layout(ui, w, h)) return 1;
    ops = fopen(argv[2], "w");
    if (!ops) return 1;
    fprintf(ops, "W %d %d\n", w, h);
    olui_draw(ui, NULL, draw, NULL);
    fclose(ops);
    olui_free(ui);
    return 0;
}
