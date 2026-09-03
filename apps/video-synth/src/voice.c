#include "voice.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

#define CMD_QUEUE_CAP 1024
#define NBOX (1 + GRAPH_MAX_TAPS)   /* mailbox 0 is the main output */

typedef struct Command {
    char target[64];
    char cmd[64];
    char arg[256];
} Command;

typedef struct Mailbox {
    AVFrame *latest;
    int      fresh;
} Mailbox;

struct Voice {
    VoiceConfig cfg;
    char       *chain;

    SDL_Thread *thread;
    SDL_atomic_t quit;
    SDL_atomic_t failed;
    SDL_atomic_t ntaps;

    SDL_mutex  *mbx_lock;
    Mailbox     box[NBOX];

    /* command queue (ring) */
    SDL_mutex  *cmd_lock;
    Command     cmds[CMD_QUEUE_CAP];
    int         cmd_head, cmd_tail;

    char        error[GRAPH_ERR_CAP];
};

/* ---------- source ---------- */

static int open_source(Voice *v, AVFormatContext **out_fmt, AVCodecContext **out_dec, int *out_stream)
{
    const VoiceConfig *cfg = &v->cfg;
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
        snprintf(v->error, sizeof v->error, "capture input format not available in this libavdevice");
        av_dict_free(&opts);
        return -1;
    }

    AVFormatContext *fmt = NULL;
    int ret = avformat_open_input(&fmt, url, ifmt, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        snprintf(v->error, sizeof v->error, "open %s: %s", ifmt->name, av_err2str(ret));
        return ret;
    }
    fmt->flags |= AVFMT_FLAG_NOBUFFER;

    ret = avformat_find_stream_info(fmt, NULL);
    if (ret < 0) {
        snprintf(v->error, sizeof v->error, "find_stream_info: %s", av_err2str(ret));
        avformat_close_input(&fmt);
        return ret;
    }

    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si < 0) {
        snprintf(v->error, sizeof v->error, "no video stream from capture device");
        avformat_close_input(&fmt);
        return si;
    }

    AVCodecParameters *par = fmt->streams[si]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        snprintf(v->error, sizeof v->error, "no decoder for codec id %d", par->codec_id);
        avformat_close_input(&fmt);
        return -1;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, par);
    dec->pkt_timebase = fmt->streams[si]->time_base;
    dec->thread_count = 1;
    ret = avcodec_open2(dec, codec, NULL);
    if (ret < 0) {
        snprintf(v->error, sizeof v->error, "avcodec_open2: %s", av_err2str(ret));
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

/* ---------- mailbox & commands ---------- */

static void publish(Voice *v, int box, AVFrame *f)
{
    SDL_LockMutex(v->mbx_lock);
    Mailbox *b = &v->box[box];
    if (!b->latest) b->latest = av_frame_alloc();
    av_frame_unref(b->latest);
    av_frame_move_ref(b->latest, f);
    b->fresh = 1;
    SDL_UnlockMutex(v->mbx_lock);
}

static AVFrame *take(Voice *v, int box)
{
    AVFrame *out = NULL;
    SDL_LockMutex(v->mbx_lock);
    Mailbox *b = &v->box[box];
    if (b->fresh && b->latest) {
        out = av_frame_clone(b->latest);   /* ref-counted, cheap */
        b->fresh = 0;
    }
    SDL_UnlockMutex(v->mbx_lock);
    return out;
}

AVFrame *voice_take_frame(Voice *v) { return take(v, 0); }
AVFrame *voice_take_tap(Voice *v, int tap)
{
    if (tap < 0 || tap >= GRAPH_MAX_TAPS) return NULL;
    return take(v, 1 + tap);
}
int voice_tap_count(const Voice *v) { return SDL_AtomicGet((SDL_atomic_t *)&v->ntaps); }

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
    Graph g = {0};
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc(), *filt = av_frame_alloc();
    int stream = -1, ret;

    if (open_source(v, &fmt, &dec, &stream) < 0) goto fail;

    GraphSpec spec = { v->chain, dec->width, dec->height, dec->pix_fmt, fmt->streams[stream]->time_base };
    if (graph_build(&spec, &g, v->error, sizeof v->error) < 0) goto fail;
    SDL_AtomicSet(&v->ntaps, g.ntaps);
    fprintf(stderr, "voice: graph up, %d tap%s\n", g.ntaps, g.ntaps == 1 ? "" : "s");

    while (!SDL_AtomicGet(&v->quit)) {
        drain_commands(v, g.graph);

        ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) { SDL_Delay(1); continue; }
            snprintf(v->error, sizeof v->error, "read_frame: %s", av_err2str(ret));
            goto fail;
        }
        if (pkt->stream_index != stream) { av_packet_unref(pkt); continue; }

        ret = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (ret < 0) { fprintf(stderr, "voice: send_packet: %s\n", av_err2str(ret)); continue; }

        while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
            frame->pts = frame->best_effort_timestamp;
            ret = av_buffersrc_add_frame_flags(g.src, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
            av_frame_unref(frame);
            if (ret < 0) { fprintf(stderr, "voice: buffersrc: %s\n", av_err2str(ret)); break; }

            while ((ret = av_buffersink_get_frame(g.sink, filt)) >= 0)
                publish(v, 0, filt);      /* moves the ref out of filt */
            for (int t = 0; t < g.ntaps; t++)
                while ((ret = av_buffersink_get_frame(g.taps[t], filt)) >= 0)
                    publish(v, 1 + t, filt);
        }
    }

    goto out;
fail:
    fprintf(stderr, "voice: %s\n", v->error);
    SDL_AtomicSet(&v->failed, 1);
out:
    av_frame_free(&frame);
    av_frame_free(&filt);
    av_packet_free(&pkt);
    graph_free(&g);
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
    v->chain = cfg->chain ? strdup(cfg->chain) : strdup("");
    v->cfg.chain = v->chain;
    v->mbx_lock = SDL_CreateMutex();
    v->cmd_lock = SDL_CreateMutex();
    SDL_AtomicSet(&v->quit, 0);
    SDL_AtomicSet(&v->failed, 0);
    SDL_AtomicSet(&v->ntaps, 0);

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
    for (int i = 0; i < NBOX; i++)
        if (v->box[i].latest) av_frame_free(&v->box[i].latest);
    if (v->mbx_lock) SDL_DestroyMutex(v->mbx_lock);
    if (v->cmd_lock) SDL_DestroyMutex(v->cmd_lock);
    free(v->chain);
    free(v);
}

int         voice_failed(const Voice *v) { return SDL_AtomicGet((SDL_atomic_t *)&v->failed); }
const char *voice_error(const Voice *v)  { return v->error; }
