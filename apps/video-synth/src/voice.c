#include "voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

#define CMD_QUEUE_CAP 256

typedef struct Command {
    char target[64];
    char cmd[64];
    char arg[256];
} Command;

struct Voice {
    VoiceConfig cfg;
    char       *filters;

    SDL_Thread *thread;
    SDL_atomic_t quit;
    SDL_atomic_t failed;

    /* mailbox */
    SDL_mutex  *mbx_lock;
    AVFrame    *latest;          /* newest frame, owned by mailbox */
    int         latest_fresh;

    /* command queue (ring) */
    SDL_mutex  *cmd_lock;
    Command     cmds[CMD_QUEUE_CAP];
    int         cmd_head, cmd_tail;

    char        graph_desc[4096];
};

/* ---------- source ---------- */

static int open_source(const VoiceConfig *cfg, AVFormatContext **out_fmt,
                       AVCodecContext **out_dec, int *out_stream)
{
    AVDictionary *opts = NULL;
    char buf[64];
    const AVInputFormat *ifmt;
    const char *url;

    snprintf(buf, sizeof buf, "%dx%d", cfg->cap_w, cfg->cap_h);
    av_dict_set(&opts, "video_size", buf, 0);
    snprintf(buf, sizeof buf, "%d", cfg->cap_fps);
    av_dict_set(&opts, "framerate", buf, 0);
    av_dict_set(&opts, "draw_mouse", "0", 0);

#ifdef _WIN32
    ifmt = av_find_input_format("gdigrab");
    url = "desktop";
    snprintf(buf, sizeof buf, "%d", cfg->cap_x); av_dict_set(&opts, "offset_x", buf, 0);
    snprintf(buf, sizeof buf, "%d", cfg->cap_y); av_dict_set(&opts, "offset_y", buf, 0);
#else
    ifmt = av_find_input_format("x11grab");
    static char x11url[64];
    const char *display = getenv("DISPLAY");
    snprintf(x11url, sizeof x11url, "%s+%d,%d", display ? display : ":0.0", cfg->cap_x, cfg->cap_y);
    url = x11url;
#endif
    if (!ifmt) {
        fprintf(stderr, "voice: capture input format not available in this libavdevice\n");
        av_dict_free(&opts);
        return -1;
    }

    AVFormatContext *fmt = NULL;
    int ret = avformat_open_input(&fmt, url, ifmt, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        fprintf(stderr, "voice: avformat_open_input(%s): %s\n", ifmt->name, av_err2str(ret));
        return ret;
    }
    fmt->flags |= AVFMT_FLAG_NOBUFFER;

    ret = avformat_find_stream_info(fmt, NULL);
    if (ret < 0) {
        fprintf(stderr, "voice: find_stream_info: %s\n", av_err2str(ret));
        avformat_close_input(&fmt);
        return ret;
    }

    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si < 0) {
        fprintf(stderr, "voice: no video stream from capture device\n");
        avformat_close_input(&fmt);
        return si;
    }

    AVCodecParameters *par = fmt->streams[si]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        fprintf(stderr, "voice: no decoder for codec id %d\n", par->codec_id);
        avformat_close_input(&fmt);
        return -1;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, par);
    dec->pkt_timebase = fmt->streams[si]->time_base;
    dec->thread_count = 1;
    ret = avcodec_open2(dec, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "voice: avcodec_open2: %s\n", av_err2str(ret));
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return ret;
    }

    fprintf(stderr, "voice: source %s %dx%d %s via %s\n", ifmt->name, par->width, par->height,
            av_get_pix_fmt_name(par->format), codec->name);

    *out_fmt = fmt;
    *out_dec = dec;
    *out_stream = si;
    return 0;
}

/* ---------- graph ---------- */

static int build_graph(Voice *v, const AVCodecContext *dec, AVRational tb,
                       AVFilterGraph **out_graph, AVFilterContext **out_src,
                       AVFilterContext **out_sink)
{
    AVFilterGraph *graph = avfilter_graph_alloc();
    if (!graph) return AVERROR(ENOMEM);
    graph->nb_threads = 0;   /* auto */

    char args[256];
    snprintf(args, sizeof args,
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             dec->width, dec->height, dec->pix_fmt, tb.num, tb.den);

    AVFilterContext *src = NULL, *sink = NULL;
    int ret = avfilter_graph_create_filter(&src, avfilter_get_by_name("buffer"), "in", args, NULL, graph);
    if (ret < 0) { fprintf(stderr, "voice: buffer: %s\n", av_err2str(ret)); goto fail; }
    ret = avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out", NULL, NULL, graph);
    if (ret < 0) { fprintf(stderr, "voice: buffersink: %s\n", av_err2str(ret)); goto fail; }

    /* Output format is pinned by the trailing format=bgra filter (av_opt_set_int_list
     * was removed in ffmpeg 8; the filter is the portable way). */

    /* Rack runs at capture resolution. Filters that change size (crop) get
     * scaled back so the graph output stays constant; SDL scales to the window. */
    char chain[4096];
    snprintf(chain, sizeof chain, "%s%sscale@out=%d:%d,format=bgra",
             v->filters && *v->filters ? v->filters : "",
             v->filters && *v->filters ? "," : "",
             dec->width, dec->height);

    AVFilterInOut *inputs = avfilter_inout_alloc(), *outputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");  outputs->filter_ctx = src;  outputs->pad_idx = 0; outputs->next = NULL;
    inputs->name  = av_strdup("out"); inputs->filter_ctx  = sink; inputs->pad_idx = 0;  inputs->next = NULL;

    ret = avfilter_graph_parse_ptr(graph, chain, &inputs, &outputs, NULL);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) { fprintf(stderr, "voice: parse '%s': %s\n", chain, av_err2str(ret)); goto fail; }

    ret = avfilter_graph_config(graph, NULL);
    if (ret < 0) { fprintf(stderr, "voice: graph_config: %s\n", av_err2str(ret)); goto fail; }

    snprintf(v->graph_desc, sizeof v->graph_desc, "%s", chain);
    fprintf(stderr, "voice: graph %s\n", chain);

    *out_graph = graph;
    *out_src = src;
    *out_sink = sink;
    return 0;

fail:
    avfilter_graph_free(&graph);
    return ret;
}

/* ---------- mailbox & commands ---------- */

static void publish(Voice *v, AVFrame *f)
{
    SDL_LockMutex(v->mbx_lock);
    if (!v->latest) v->latest = av_frame_alloc();
    av_frame_unref(v->latest);
    av_frame_move_ref(v->latest, f);
    v->latest_fresh = 1;
    SDL_UnlockMutex(v->mbx_lock);
}

AVFrame *voice_take_frame(Voice *v)
{
    AVFrame *out = NULL;
    SDL_LockMutex(v->mbx_lock);
    if (v->latest_fresh && v->latest) {
        out = av_frame_clone(v->latest);   /* ref-counted, cheap */
        v->latest_fresh = 0;
    }
    SDL_UnlockMutex(v->mbx_lock);
    return out;
}

int voice_send_command(Voice *v, const char *target, const char *cmd, const char *arg)
{
    int ret = 0;
    SDL_LockMutex(v->cmd_lock);
    int next = (v->cmd_tail + 1) % CMD_QUEUE_CAP;
    if (next == v->cmd_head) {
        ret = -1;   /* full; drop */
    } else {
        Command *c = &v->cmds[v->cmd_tail];
        snprintf(c->target, sizeof c->target, "%s", target);
        snprintf(c->cmd, sizeof c->cmd, "%s", cmd);
        snprintf(c->arg, sizeof c->arg, "%s", arg ? arg : "");
        v->cmd_tail = next;
    }
    SDL_UnlockMutex(v->cmd_lock);
    return ret;
}

static void drain_commands(Voice *v, AVFilterGraph *graph)
{
    for (;;) {
        Command c;
        SDL_LockMutex(v->cmd_lock);
        if (v->cmd_head == v->cmd_tail) { SDL_UnlockMutex(v->cmd_lock); return; }
        c = v->cmds[v->cmd_head];
        v->cmd_head = (v->cmd_head + 1) % CMD_QUEUE_CAP;
        SDL_UnlockMutex(v->cmd_lock);

        char res[256] = {0};
        int ret = avfilter_graph_send_command(graph, c.target, c.cmd, c.arg, res, sizeof res, 0);
        if (ret < 0)
            fprintf(stderr, "voice: command %s %s %s failed: %s %s\n",
                    c.target, c.cmd, c.arg, av_err2str(ret), res);
    }
}

/* ---------- thread ---------- */

static int voice_thread(void *arg)
{
    Voice *v = arg;
    AVFormatContext *fmt = NULL;
    AVCodecContext *dec = NULL;
    AVFilterGraph *graph = NULL;
    AVFilterContext *src = NULL, *sink = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc(), *filt = av_frame_alloc();
    int stream = -1, ret;

    if (open_source(&v->cfg, &fmt, &dec, &stream) < 0) goto fail;
    if (build_graph(v, dec, fmt->streams[stream]->time_base, &graph, &src, &sink) < 0) goto fail;

    while (!SDL_AtomicGet(&v->quit)) {
        drain_commands(v, graph);

        ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) { SDL_Delay(1); continue; }
            fprintf(stderr, "voice: read_frame: %s\n", av_err2str(ret));
            goto fail;
        }
        if (pkt->stream_index != stream) { av_packet_unref(pkt); continue; }

        ret = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (ret < 0) { fprintf(stderr, "voice: send_packet: %s\n", av_err2str(ret)); continue; }

        while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
            frame->pts = frame->best_effort_timestamp;
            ret = av_buffersrc_add_frame_flags(src, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
            av_frame_unref(frame);
            if (ret < 0) { fprintf(stderr, "voice: buffersrc: %s\n", av_err2str(ret)); break; }

            while ((ret = av_buffersink_get_frame(sink, filt)) >= 0)
                publish(v, filt);      /* moves the ref out of filt */
        }
    }

    goto out;
fail:
    SDL_AtomicSet(&v->failed, 1);
out:
    av_frame_free(&frame);
    av_frame_free(&filt);
    av_packet_free(&pkt);
    avfilter_graph_free(&graph);
    avcodec_free_context(&dec);
    if (fmt) avformat_close_input(&fmt);
    return 0;
}

/* ---------- lifecycle ---------- */

Voice *voice_start(const VoiceConfig *cfg)
{
    static int registered;
    if (!registered) { avdevice_register_all(); registered = 1; }

    Voice *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->cfg = *cfg;
    v->filters = cfg->filters ? strdup(cfg->filters) : NULL;
    v->cfg.filters = v->filters;
    v->mbx_lock = SDL_CreateMutex();
    v->cmd_lock = SDL_CreateMutex();
    SDL_AtomicSet(&v->quit, 0);
    SDL_AtomicSet(&v->failed, 0);

    v->thread = SDL_CreateThread(voice_thread, "voice", v);
    if (!v->thread) {
        fprintf(stderr, "voice: SDL_CreateThread: %s\n", SDL_GetError());
        voice_stop(v);
        return NULL;
    }
    return v;
}

void voice_stop(Voice *v)
{
    if (!v) return;
    SDL_AtomicSet(&v->quit, 1);
    if (v->thread) SDL_WaitThread(v->thread, NULL);
    if (v->latest) av_frame_free(&v->latest);
    if (v->mbx_lock) SDL_DestroyMutex(v->mbx_lock);
    if (v->cmd_lock) SDL_DestroyMutex(v->cmd_lock);
    free(v->filters);
    free(v);
}

const char *voice_graph_description(const Voice *v) { return v->graph_desc; }
int voice_failed(const Voice *v) { return SDL_AtomicGet((SDL_atomic_t *)&v->failed); }
