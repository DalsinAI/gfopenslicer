/* GFOpenSlicer, from the shell.
 *   gfopenslicer in.stl out.gcode [LAYER=0.2] [INFILL=20] [LINE=0.4]
 *       [FILAMENT=1.75] [HOTEND=200] [BED=60] [SKIRT=1]
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = GS_VERSTR;

static int opt(int argc, char **argv, const char *name, float *out)
{
    int i;
    size_t n = strlen(name);
    for (i = 3; i < argc; i++) {
        if (strncmp(argv[i], name, n) == 0 && argv[i][n] == '=') {
            *out = (float)atof(argv[i] + n + 1);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    gs_mesh mesh;
    gs_cfg cfg;
    FILE *out;
    char err[160];
    float skirt = 1.f;
    (void)version;
    if (argc < 3) {
        fprintf(stderr, "GFOpenSlicer %s (%s)\n", GS_VERSION, GS_DATE);
        fprintf(stderr, "usage: gfopenslicer in.stl out.gcode [LAYER=0.2] [INFILL=20] [LINE=0.4] [FILAMENT=1.75] [HOTEND=200] [BED=60] [SKIRT=1]\n");
        return 1;
    }
    gs_cfg_default(&cfg);
    opt(argc, argv, "LAYER", &cfg.layer_h);
    opt(argc, argv, "INFILL", &cfg.infill);
    opt(argc, argv, "LINE", &cfg.line_w);
    opt(argc, argv, "FILAMENT", &cfg.filament);
    opt(argc, argv, "HOTEND", &cfg.hotend);
    opt(argc, argv, "BED", &cfg.bed);
    if (opt(argc, argv, "SKIRT", &skirt)) cfg.skirt = (int)skirt;
    if (gs_stl_load(&mesh, argv[1], err, (int)sizeof err)) {
        fprintf(stderr, "GFOpenSlicer: %s\n", err);
        return 1;
    }
    gs_mesh_repair(&mesh);
    out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "GFOpenSlicer: cannot write %s\n", argv[2]);
        gs_mesh_free(&mesh);
        return 1;
    }
    if (gs_slice(&mesh, &cfg, out, err, (int)sizeof err)) {
        fprintf(stderr, "GFOpenSlicer: %s\n", err);
        fclose(out);
        gs_mesh_free(&mesh);
        return 1;
    }
    fclose(out);
    gs_mesh_free(&mesh);
    printf("GFOpenSlicer: wrote %s\n", argv[2]);
    return 0;
}
