#ifndef VSYNTH_PICKER_H
#define VSYNTH_PICKER_H

/*
 * Capture-region picker: a translucent always-on-top window over the whole
 * desktop. The current region is drawn as a red rectangle. Left-drag draws a
 * new one; Esc, Enter or right-click keeps the old one.
 *
 * Modal: runs its own event loop and returns when the user is done. The main
 * window is covered anyway, so nothing is lost by pausing its event loop.
 */

typedef struct PickRect { int x, y, w, h; } PickRect;

/* Returns 1 and fills *out when a new rectangle was drawn, 0 when cancelled. */
int picker_run(const PickRect *prev, PickRect *out);

#endif
