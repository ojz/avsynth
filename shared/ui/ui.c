#include "ui.h"

#include <math.h>
#include <stdio.h>

/* One palette for every app: a neutral panel and a single amber accent.
 * Sections are told apart by their titles and their spacing, not by giving
 * each one its own colour. */
static const UiTheme THEME = {
    .bg         = { 17,  18,  17, 255 },
    .panel      = { 27,  29,  28, 255 },
    .panel_head = { 38,  41,  39, 255 },
    .ink        = { 235, 231, 216, 255 },
    .ink_dim    = { 156, 159, 150, 255 },
    .ink_faint  = { 104, 108, 102, 255 },
    .accent     = { 232, 168,  56, 255 },
    .track      = { 12,  13,  13, 255 },
    .fill       = { 118, 138, 128, 255 },
    .fill_off   = { 62,  66,  64, 255 },
    .handle     = { 226, 222, 205, 255 },
    .rule       = { 58,  62,  59, 255 },
    .ok         = { 108, 200, 138, 255 },
    .warn       = { 214,  92,  78, 255 },
    .track_h    = 10.0f,
    .handle_w   = 5.0f,
    .handle_over = 5.0f,
};

const UiTheme *ui_theme(void) { return &THEME; }

int ui_hit(const SDL_FRect *box, float x, float y)
{
    return x >= box->x && x < box->x + box->w && y >= box->y && y < box->y + box->h;
}

void ui_fill(SDL_Renderer *r, SDL_FRect box, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &box);
}

void ui_rect(SDL_Renderer *r, SDL_FRect box, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderRect(r, &box);
}

static float text_h(const UiText *t)
{
    return (t && t->height) ? t->height(t->ud) : 10.0f;
}

static float text_w(const UiText *t, const char *s)
{
    return (t && t->width) ? t->width(t->ud, s) : 0.0f;
}

static void text_at(const UiText *t, float x, float y, const char *s, SDL_Color c)
{
    if (t && t->draw) t->draw(t->ud, x, y, s, c);
}

SDL_FRect ui_panel(SDL_Renderer *r, const UiText *t, SDL_FRect box, const char *title)
{
    const float head = text_h(t) + 12.0f;
    ui_fill(r, box, THEME.panel);
    ui_fill(r, (SDL_FRect){ box.x, box.y, box.w, head }, THEME.panel_head);
    ui_rect(r, box, THEME.rule);
    text_at(t, box.x + 10.0f, box.y + 6.0f, title, THEME.accent);
    return (SDL_FRect){ box.x + 12.0f, box.y + head + 10.0f,
                        box.w - 24.0f, box.h - head - 22.0f };
}

void ui_fader_layout(UiFader *f, SDL_FRect box, UiOrient o, const UiText *t)
{
    const float lh = text_h(t);
    f->box = box;
    f->orient = o;
    f->label = (SDL_FRect){ box.x, box.y, box.w, lh };
    f->value = (SDL_FRect){ box.x, box.y + box.h - lh, box.w, lh };

    if (o == UI_H) {
        float mid = box.y + lh + (box.h - 2.0f * lh) * 0.5f;
        f->track = (SDL_FRect){ box.x, mid - THEME.track_h * 0.5f, box.w, THEME.track_h };
    } else {
        float cx = box.x + box.w * 0.5f;
        float top = box.y + lh + 4.0f;
        float bottom = box.y + box.h - lh - 4.0f;
        f->track = (SDL_FRect){ cx - THEME.track_h * 0.5f, top,
                                THEME.track_h, bottom - top };
    }
}

void ui_fader_layout_row(UiFader *f, SDL_FRect box, float label_w, float value_w,
                         const UiText *t)
{
    const float lh = text_h(t);
    const float gap = 8.0f;
    const float cy = box.y + box.h * 0.5f;
    f->box = box;
    f->orient = UI_H;
    f->label = (SDL_FRect){ box.x, cy - lh * 0.5f, label_w, lh };
    f->value = (SDL_FRect){ box.x + box.w - value_w, cy - lh * 0.5f, value_w, lh };
    /* The track keeps its thickness unless the row is too short for the
     * handle's overhang, then it thins so nothing draws outside the row. */
    float th = THEME.track_h;
    if (th + 2.0f * THEME.handle_over > box.h) th = box.h - 2.0f * THEME.handle_over;
    if (th < 2.0f) th = 2.0f;
    float tx = box.x + label_w + gap;
    float tw = box.w - label_w - value_w - 2.0f * gap;
    if (tw < 8.0f) tw = 8.0f;
    f->track = (SDL_FRect){ tx, cy - th * 0.5f, tw, th };
}

/* Where along the track a normalised position sits, in pixels. */
static float track_pos(const UiFader *f, double t)
{
    if (f->orient == UI_H) return f->track.x + (float)t * f->track.w;
    /* Vertical faders read bottom-up: 0 at the bottom. */
    return f->track.y + f->track.h - (float)t * f->track.h;
}

double ui_fader_value_at(const UiFader *f, const Param *p, float x, float y)
{
    double t;
    if (f->orient == UI_H) {
        t = f->track.w > 0.0f ? (double)(x - f->track.x) / f->track.w : 0.0;
    } else {
        t = f->track.h > 0.0f ? (double)(f->track.y + f->track.h - y) / f->track.h : 0.0;
    }
    return param_from_norm(p, t);
}

void ui_fader_draw(SDL_Renderer *r, const UiText *t, const UiFader *f,
                   const Param *p, double value, int selected)
{
    const float lh = text_h(t);
    SDL_Color ink = selected ? THEME.accent : THEME.ink;
    SDL_Color fill = selected ? THEME.accent : THEME.fill;

    if (selected) {
        SDL_Color wash = { THEME.accent.r, THEME.accent.g, THEME.accent.b, 34 };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        ui_fill(r, f->box, wash);
    }

    ui_fill(r, f->track, THEME.track);

    double tv = param_to_norm(p, value);
    double tn = param_to_norm(p, p->neutral);
    /* A bipolar fader fills outward from its neutral so an inverted setting
     * reads at a glance; every other fader fills from its minimum. */
    double t0 = p->taper == PARAM_BIPOLAR ? tn : 0.0;

    float a = track_pos(f, t0 < tv ? t0 : tv);
    float b = track_pos(f, t0 < tv ? tv : t0);
    if (f->orient == UI_H) {
        ui_fill(r, (SDL_FRect){ a, f->track.y, b - a, f->track.h }, fill);
    } else {
        ui_fill(r, (SDL_FRect){ f->track.x, b, f->track.w, a - b }, fill);
    }

    /* The neutral tick: where reset goes. */
    float np = track_pos(f, tn);
    if (f->orient == UI_H)
        ui_fill(r, (SDL_FRect){ np - 0.5f, f->track.y - 3.0f, 1.0f, f->track.h + 6.0f },
                THEME.ink_faint);
    else
        ui_fill(r, (SDL_FRect){ f->track.x - 3.0f, np - 0.5f, f->track.w + 6.0f, 1.0f },
                THEME.ink_faint);

    /* The handle. */
    float hp = track_pos(f, tv);
    if (f->orient == UI_H)
        ui_fill(r, (SDL_FRect){ hp - THEME.handle_w * 0.5f, f->track.y - THEME.handle_over,
                                THEME.handle_w, f->track.h + 2.0f * THEME.handle_over },
                THEME.handle);
    else
        ui_fill(r, (SDL_FRect){ f->track.x - THEME.handle_over, hp - THEME.handle_w * 0.5f,
                                f->track.w + 2.0f * THEME.handle_over, THEME.handle_w },
                THEME.handle);

    /* Label on the left, readout on the right, both on their own line, so
     * nothing has to be centred and nothing collides with a neighbour. */
    char readout[64];
    param_format(p, value, readout, sizeof readout);
    text_at(t, f->label.x, f->label.y, p->label, selected ? THEME.accent : THEME.ink_dim);
    float rw = text_w(t, readout);
    text_at(t, f->value.x + f->value.w - rw, f->value.y, readout, ink);
    (void)lh;
}

void ui_stepper_draw(SDL_Renderer *r, const UiText *t, SDL_FRect box,
                     const Param *p, double value, int selected)
{
    const float lh = text_h(t);
    SDL_Color ink = selected ? THEME.accent : THEME.ink;
    char readout[64];
    param_format_value(p, value, readout, sizeof readout);

    if (selected) {
        SDL_Color wash = { THEME.accent.r, THEME.accent.g, THEME.accent.b, 34 };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        ui_fill(r, box, wash);
    }

    SDL_FRect well = { box.x, box.y + lh + 2.0f, box.w, box.h - lh - 2.0f };
    int on = value >= 0.5;
    ui_fill(r, well, THEME.track);
    ui_rect(r, well, selected ? THEME.accent : (on ? THEME.fill : THEME.fill_off));
    text_at(t, box.x, box.y, p->label, selected ? THEME.accent : THEME.ink_dim);
    float rw = text_w(t, readout);
    text_at(t, well.x + (well.w - rw) * 0.5f, well.y + (well.h - lh) * 0.5f, readout, ink);
}

void ui_stepper_draw_row(SDL_Renderer *r, const UiText *t, SDL_FRect box,
                         float label_w, float value_w,
                         const Param *p, double value, int selected)
{
    const float lh = text_h(t);
    const float gap = 8.0f;
    const float cy = box.y + box.h * 0.5f;
    SDL_Color ink = selected ? THEME.accent : THEME.ink;
    char readout[64];
    param_format_value(p, value, readout, sizeof readout);

    if (selected) {
        SDL_Color wash = { THEME.accent.r, THEME.accent.g, THEME.accent.b, 34 };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        ui_fill(r, box, wash);
    }

    float wh = lh + 4.0f;
    if (wh > box.h) wh = box.h;
    SDL_FRect well = { box.x + label_w + gap, cy - wh * 0.5f,
                       box.w - label_w - value_w - 2.0f * gap, wh };
    if (well.w < 8.0f) well.w = 8.0f;
    int on = value >= 0.5;
    ui_fill(r, well, THEME.track);
    ui_rect(r, well, selected ? THEME.accent : (on ? THEME.fill : THEME.fill_off));
    text_at(t, box.x, cy - lh * 0.5f, p->label, selected ? THEME.accent : THEME.ink_dim);
    float rw = text_w(t, readout);
    text_at(t, well.x + (well.w - rw) * 0.5f, well.y + (well.h - lh) * 0.5f, readout, ink);
}

ParamGrain ui_grain(SDL_Keymod mod)
{
    if (mod & SDL_KMOD_SHIFT) return PARAM_ULTRA;
    if (mod & SDL_KMOD_CTRL)  return PARAM_COARSE;
    return PARAM_FINE;
}

int ui_is_reset_click(const SDL_Event *ev)
{
    if (ev->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return 0;
    if (ev->button.button == SDL_BUTTON_MIDDLE) return 1;
    return ev->button.button == SDL_BUTTON_LEFT && ev->button.clicks >= 2;
}
