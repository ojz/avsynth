#include "graph.h"

#include <stdio.h>
#include <string.h>

#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>

/* ---------- error capture ----------
 * libavfilter reports the useful part of a parse or config failure through
 * av_log, not the return code. While a build runs we copy AV_LOG_ERROR lines
 * into a buffer so the editor can show them verbatim. */

static char  cap_buf[GRAPH_ERR_CAP];
static int   cap_on;

static void log_cb(void *ptr, int level, const char *fmt, va_list vl)
{
    if (cap_on && level <= AV_LOG_ERROR) {
        char line[512];
        va_list copy;
        va_copy(copy, vl);
        vsnprintf(line, sizeof line, fmt, copy);
        va_end(copy);
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n) {
            size_t have = strlen(cap_buf);
            snprintf(cap_buf + have, sizeof cap_buf - have, "%s%s", have ? " | " : "", line);
        }
    }
    av_log_default_callback(ptr, level, fmt, vl);
}

static void capture_begin(void)
{
    static int installed;
    if (!installed) { av_log_set_callback(log_cb); installed = 1; }
    cap_buf[0] = 0;
    cap_on = 1;
}

static void capture_end(char *err, size_t cap, const char *fallback)
{
    cap_on = 0;
    if (err && cap)
        snprintf(err, cap, "%s", cap_buf[0] ? cap_buf : fallback);
}

/* ---------- build ---------- */

int graph_is_helper(const AVFilterContext *f)
{
    return f->name && (!strncmp(f->name, "__", 2) || !strcmp(f->name, "in") || !strcmp(f->name, "out"));
}

static AVFilterContext *add(AVFilterGraph *g, const char *filter, const char *name, const char *args)
{
    AVFilterContext *ctx = NULL;
    int ret = avfilter_graph_create_filter(&ctx, avfilter_get_by_name(filter), name, args, NULL, g);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "cannot create %s (%s): %s\n", filter, name, av_err2str(ret));
        return NULL;
    }
    return ctx;
}

int graph_build(const GraphSpec *spec, Graph *out, char *err, size_t err_cap)
{
    memset(out, 0, sizeof *out);
    if (!spec->chain || !*spec->chain) {
        if (err) snprintf(err, err_cap, "empty chain");
        return -1;
    }

    capture_begin();

    AVFilterGraph *g = avfilter_graph_alloc();
    if (!g) { capture_end(err, err_cap, "out of memory"); return -1; }
    g->nb_threads = 0;

    char args[256];
    snprintf(args, sizeof args, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             spec->width, spec->height, spec->pix_fmt, spec->time_base.num, spec->time_base.den);
    AVFilterContext *src = add(g, "buffer", "in", args);
    if (!src) goto fail;

    /* {W} and {H} -> capture size. The only thing we add to ffmpeg's syntax;
     * it lets a chain scale back to the capture size after a crop without
     * hard-coding numbers that break when the region changes. */
    char expanded[8192];
    {
        size_t n = 0;
        for (const char *p = spec->chain; *p && n + 16 < sizeof expanded; p++) {
            if (p[0] == '{' && (p[1] == 'W' || p[1] == 'H') && p[2] == '}') {
                n += snprintf(expanded + n, sizeof expanded - n, "%d", p[1] == 'W' ? spec->width : spec->height);
                p += 2;
            } else if (*p == '\n' || *p == '\r' || *p == '\t') {
                continue;    /* the editor lets you break lines; the parser does not */
            } else {
                expanded[n++] = *p;
            }
        }
        expanded[n] = 0;
    }

    AVFilterInOut *inputs = NULL, *outputs = NULL;
    int ret = avfilter_graph_parse2(g, expanded, &inputs, &outputs);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "parse: %s\n", av_err2str(ret));
        goto fail;
    }

    /* source: exactly one open input */
    int nin = 0;
    for (AVFilterInOut *p = inputs; p; p = p->next) nin++;
    if (nin != 1) {
        av_log(NULL, AV_LOG_ERROR, "chain has %d open inputs, needs exactly one\n", nin);
        avfilter_inout_free(&inputs); avfilter_inout_free(&outputs);
        goto fail;
    }
    ret = avfilter_link(src, 0, inputs->filter_ctx, inputs->pad_idx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "link source: %s\n", av_err2str(ret));
        avfilter_inout_free(&inputs); avfilter_inout_free(&outputs);
        goto fail;
    }
    avfilter_inout_free(&inputs);

    /* main output: [out] if labelled, else the unlabelled chain end, else the
     * first open output. Labelled leftovers are taps. */
    AVFilterInOut *main = NULL;
    for (AVFilterInOut *p = outputs; p; p = p->next)
        if (p->name && !strcmp(p->name, "out")) { main = p; break; }
    if (!main)
        for (AVFilterInOut *p = outputs; p; p = p->next)
            if (!p->name) { main = p; break; }
    if (!main) main = outputs;
    if (!main) {
        av_log(NULL, AV_LOG_ERROR, "chain has no open output\n");
        goto fail;
    }

    snprintf(args, sizeof args, "%d:%d", spec->width, spec->height);
    AVFilterContext *osc = add(g, "scale", "__out_scale", args);
    AVFilterContext *ofm = add(g, "format", "__out_fmt", "bgra");
    AVFilterContext *sink = add(g, "buffersink", "out", NULL);
    if (!osc || !ofm || !sink) { avfilter_inout_free(&outputs); goto fail; }
    if (avfilter_link(main->filter_ctx, main->pad_idx, osc, 0) < 0 ||
        avfilter_link(osc, 0, ofm, 0) < 0 || avfilter_link(ofm, 0, sink, 0) < 0) {
        av_log(NULL, AV_LOG_ERROR, "link main output\n");
        avfilter_inout_free(&outputs); goto fail;
    }

    /* taps: every other open output */
    int ntaps = 0;
    for (AVFilterInOut *p = outputs; p; p = p->next) {
        if (p == main) continue;
        if (ntaps >= GRAPH_MAX_TAPS) {
            av_log(NULL, AV_LOG_ERROR, "more than %d tap outputs\n", GRAPH_MAX_TAPS);
            avfilter_inout_free(&outputs); goto fail;
        }
        char nm[32];
        snprintf(nm, sizeof nm, "__tap%d_fmt", ntaps);
        AVFilterContext *tfm = add(g, "format", nm, "bgra");
        snprintf(nm, sizeof nm, "__tap%d", ntaps);
        AVFilterContext *tsk = add(g, "buffersink", nm, NULL);
        if (!tfm || !tsk) { avfilter_inout_free(&outputs); goto fail; }
        if (avfilter_link(p->filter_ctx, p->pad_idx, tfm, 0) < 0 || avfilter_link(tfm, 0, tsk, 0) < 0) {
            av_log(NULL, AV_LOG_ERROR, "link tap %s\n", p->name ? p->name : "?");
            avfilter_inout_free(&outputs); goto fail;
        }
        snprintf(out->tap_names[ntaps], sizeof out->tap_names[ntaps], "%s",
                 p->name ? p->name : p->filter_ctx->name);
        out->taps[ntaps++] = tsk;
    }
    avfilter_inout_free(&outputs);

    ret = avfilter_graph_config(g, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "config: %s\n", av_err2str(ret));
        goto fail;
    }

    capture_end(err, err_cap, "");
    out->graph = g;
    out->src = src;
    out->sink = sink;
    out->ntaps = ntaps;
    return 0;

fail:
    capture_end(err, err_cap, "graph build failed");
    avfilter_graph_free(&g);
    return -1;
}

void graph_free(Graph *g)
{
    if (!g) return;
    avfilter_graph_free(&g->graph);
    memset(g, 0, sizeof *g);
}
