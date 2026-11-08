/* A slice the window and the preview both keep: the G-code, and where
 * each layer starts in it. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_JOB_H
#define GS_JOB_H

#include "gs.h"

typedef struct {
    char in_path[256];
    char out_path[256];
    char *gcode;
    int layers;
    long *at;
    long *len;
    int triangles;
} gs_job;

void gs_job_init(gs_job *j);
void gs_job_free(gs_job *j);

/* out NULL: the STL's name with .gcode. 0, or -1 with why in err. */
int gs_job_slice(gs_job *j, const char *in, const char *out, const gs_cfg *cfg, char *err, int errlen);

/* The layer's G-code, and a short name ("12  Z2.500"). */
int gs_job_layer(const gs_job *j, int i, char *buf, int buflen);
void gs_job_layer_name(const gs_job *j, int i, char *buf, int buflen);

#endif
