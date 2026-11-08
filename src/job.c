#include "gs_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gs_job_init(gs_job *j)
{
    memset(j, 0, sizeof *j);
}

void gs_job_free(gs_job *j)
{
    free(j->gcode);
    free(j->at);
    free(j->len);
    gs_mesh_free(&j->mesh);
    memset(j, 0, sizeof *j);
}

static int out_name(char *dst, int n, const char *in)
{
    const char *base, *dot;
    int len;
    base = in;
    for (dot = in; *dot; dot++) if (*dot == '/' || *dot == ':' || *dot == '\\') base = dot + 1;
    dot = strrchr(base, '.');
    len = dot ? (int)(dot - in) : (int)strlen(in);
    if (len + 7 >= n) return -1;
    memcpy(dst, in, (size_t)len);
    memcpy(dst + len, ".gcode", 7);
    return 0;
}

static int index_layers(gs_job *j)
{
    const char *s = j->gcode;
    long n = 0, cap = 0, i;
    long *at = NULL, *len = NULL;
    if (!s) return -1;
    for (i = 0; s[i]; i++) {
        if ((i == 0 || s[i - 1] == '\n') && strncmp(s + i, "; LAYER ", 8) == 0) {
            if (n == cap) {
                long c = cap ? cap * 2 : 16;
                long *a2 = (long *)realloc(at, (size_t)c * sizeof *at);
                long *l2;
                if (!a2) {
                    free(at);
                    free(len);
                    return -1;
                }
                at = a2;
                l2 = (long *)realloc(len, (size_t)c * sizeof *len);
                if (!l2) {
                    free(at);
                    free(len);
                    return -1;
                }
                len = l2;
                cap = c;
            }
            if (n) len[n - 1] = i - at[n - 1];
            at[n++] = i;
        }
    }
    if (n) len[n - 1] = (long)strlen(s) - at[n - 1];
    j->at = at;
    j->len = len;
    j->layers = (int)n;
    return 0;
}

int gs_job_slice(gs_job *j, const char *in, const char *out, const gs_cfg *cfg, char *err, int errlen)
{
    gs_mesh mesh;
    FILE *f;
    long sz;
    char *buf;
    gs_job_free(j);
    if (!in || !in[0]) {
        snprintf(err, (size_t)errlen, "Open an STL first");
        return -1;
    }
    snprintf(j->in_path, sizeof j->in_path, "%s", in);
    if (out && out[0]) snprintf(j->out_path, sizeof j->out_path, "%s", out);
    else if (out_name(j->out_path, (int)sizeof j->out_path, in)) {
        snprintf(err, (size_t)errlen, "that path is too long");
        return -1;
    }
    if (gs_stl_load(&mesh, in, err, errlen)) return -1;
    gs_mesh_repair(&mesh);
    j->triangles = mesh.n;
    f = fopen(j->out_path, "w+");
    if (!f) {
        snprintf(err, (size_t)errlen, "cannot write %s", j->out_path);
        gs_mesh_free(&mesh);
        return -1;
    }
    if (gs_slice(&mesh, cfg, f, err, errlen)) {
        fclose(f);
        gs_mesh_free(&mesh);
        return -1;
    }
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        gs_mesh_free(&mesh);
        snprintf(err, (size_t)errlen, "cannot read the G-code back");
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        gs_mesh_free(&mesh);
        snprintf(err, (size_t)errlen, "cannot read the G-code back");
        return -1;
    }
    rewind(f);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf || (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz)) {
        free(buf);
        fclose(f);
        gs_mesh_free(&mesh);
        snprintf(err, (size_t)errlen, "out of memory");
        return -1;
    }
    buf[sz] = 0;
    fclose(f);
    j->gcode = buf;
    j->mesh = mesh;
    if (index_layers(j)) {
        snprintf(err, (size_t)errlen, "out of memory");
        return -1;
    }
    return 0;
}

int gs_job_layer(const gs_job *j, int i, char *buf, int buflen)
{
    long n;
    if (!j || i < 0 || i >= j->layers || buflen < 2) return -1;
    n = j->len[i];
    if (n >= buflen) n = buflen - 1;
    memcpy(buf, j->gcode + j->at[i], (size_t)n);
    buf[n] = 0;
    return 0;
}

void gs_job_layer_name(const gs_job *j, int i, char *buf, int buflen)
{
    int layer = i;
    float z = 0.f;
    const char *s;
    if (!j || i < 0 || i >= j->layers) {
        buf[0] = 0;
        return;
    }
    s = j->gcode + j->at[i];
    sscanf(s, "; LAYER %d Z%f", &layer, &z);
    snprintf(buf, (size_t)buflen, "%d  Z%.3f", layer, z);
}
