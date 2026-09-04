#ifndef VSYNTH_VOICE_H
#define VSYNTH_VOICE_H

#include <libavutil/frame.h>

/*
 * A Voice is one running signal path: screen-capture source -> decoder ->
 * libavfilter graph -> latest-frame mailboxes. It runs on its own thread.
 *
 * The main thread pulls frames with voice_take_frame() (main output) and
 * voice_take_tap() (preview taps), and steers the graph with
 * voice_send_command(), which is queued and applied by the voice thread
 * between frames (avfilter_graph_send_command is not safe to call
 * concurrently with filtering). Commands queued before the graph exists are
 * applied as soon as it is up, so a restart followed by rack_send_all() lands.
 */

typedef struct VoiceConfig {
    int cap_x, cap_y, cap_w, cap_h;   /* capture region in screen pixels */
    int cap_fps;
    const char *chain;                /* filtergraph text, see graph.h */
} VoiceConfig;

typedef struct Voice Voice;

Voice *voice_start(const VoiceConfig *cfg);
void   voice_stop(Voice *v);

/* Returns a new reference to the newest frame if one arrived since the last
 * call, else NULL. Caller owns the reference (av_frame_free). Format is BGRA. */
AVFrame *voice_take_frame(Voice *v);
AVFrame *voice_take_tap(Voice *v, int tap);
int      voice_tap_count(const Voice *v);

/* Queue a libavfilter command. target is an instance name ("rotate@rot") or a
 * filter name ("rotate", all instances) or "all". Returns 0 if queued. */
int voice_send_command(Voice *v, const char *target, const char *cmd, const char *arg);

/* Non-zero if the voice thread has exited on error; voice_error() says why. */
int         voice_failed(const Voice *v);
const char *voice_error(const Voice *v);

#endif
