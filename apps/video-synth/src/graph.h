#ifndef VSYNTH_GRAPH_H
#define VSYNTH_GRAPH_H

#include <libavfilter/avfilter.h>

/*
 * Build a libavfilter graph from user-written filtergraph text (the exact
 * syntax of ffmpeg -filter_complex). This is the one place that knows how a
 * chain is wired into a source and its outputs:
 *
 *   - the first open input (or the one labelled [in]) is fed by a "buffer"
 *     source of the given size and pixel format;
 *   - the open output labelled [out], or the first open output when none is
 *     labelled so, is the main output: scaled back to the capture size and
 *     converted to BGRA;
 *   - every other open output is a tap: converted to BGRA and given its own
 *     buffersink, so the UI can show it as a preview thumbnail.
 *
 * Helper filters the builder adds are named with a "__" prefix so the knob
 * introspection can skip them.
 */

#define GRAPH_MAX_TAPS 8
#define GRAPH_ERR_CAP  512

typedef struct GraphSpec {
    const char *chain;        /* user text */
    int width, height;        /* source frame size */
    int pix_fmt;              /* source AVPixelFormat */
    AVRational time_base;
} GraphSpec;

typedef struct Graph {
    AVFilterGraph   *graph;
    AVFilterContext *src;
    AVFilterContext *sink;                     /* main output */
    AVFilterContext *taps[GRAPH_MAX_TAPS];
    char             tap_names[GRAPH_MAX_TAPS][32];
    int              ntaps;
} Graph;

/* Returns 0 on success. On failure returns <0 and writes a one-line, human
 * readable reason into err (libavfilter's own message where it has one). */
int  graph_build(const GraphSpec *spec, Graph *out, char *err, size_t err_cap);
void graph_free(Graph *g);

/* True for filters the builder added (source, sinks, format tails). */
int  graph_is_helper(const AVFilterContext *f);

#endif
