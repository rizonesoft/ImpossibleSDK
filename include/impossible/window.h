/* Impossible SDK: one window whose program paints every pixel. Tier 1. */
#pragma once
#include <impossible/base.h>

/* An integer device-pixel update rectangle [left, right) x [top, bottom). */
typedef struct im_dirty_rect {
    int left, top, right, bottom;
} im_dirty_rect;

/* The pixels of a window while it is painted. */
typedef struct im_canvas {
    uint32_t *pixels; /* 0x00RRGGBB, top row first, width pixels per row */
    int width, height;
    float scale; /* 1.0 at 96 DPI */
    im_dirty_rect dirty; /* exact update rectangle from Windows WM_PAINT */
    bool full_redraw; /* true on first paint, resize, scale change, or full-client invalidation */
} im_canvas;

/* What the user did. */
typedef enum im_event_kind { IM_POINTER_MOVE, IM_POINTER_DOWN, IM_POINTER_UP, IM_KEY_DOWN } im_event_kind;

/* One thing the user did, where, or with which key. */
typedef struct im_event {
    im_event_kind kind;
    int x, y; /* pixels */
    int key;  /* Windows virtual key, for IM_KEY_DOWN */
} im_event;

/* Fill in from title to event; the rest belongs to the kit and starts as zero. */
typedef struct im_window {
    const wchar_t *title;
    int width, height; /* client size at 96 DPI, 1 to 32767 */
    void *user;
    void (*paint)(struct im_window *window, im_canvas *canvas);
    bool (*event)(struct im_window *window, const im_event *event); /* true repaints; may be NULL */
    void (*tick)(struct im_window *window); /* one-shot frame timer callback; may be NULL */
    void *native;
    void *cursor; /* active client HCURSOR set by im_host_set_cursor; NULL means arrow */
    uint32_t *buffer;
    size_t capacity;
    int last_width, last_height;
    float last_scale;
    bool event_damaged;
    bool frame_timer_active;
    bool painted; /* a frame has reached the screen */
    bool failed;
} im_window;

/* Opens the window and runs it until it closes. False when it could not be
   opened or drawn. With one_frame it closes itself after the first frame. */
bool im_window_run(im_window *window, bool one_frame);

/* Invalidates one device-pixel rectangle on an open window without erasing background.
   Returns false before the window opens, after it closes, or if the rectangle is empty. */
bool im_window_invalidate(im_window *window, int left, int top, int right, int bottom);

/* Invalidates a list of device-pixel rectangles on an open window. Returns true if at least one succeeded. */
bool im_window_invalidate_rects(im_window *window, const im_dirty_rect *rects, int count);

/* Arms a one-shot frame wake-up for an open window (delay_ms == 0 requests immediately). */
bool im_window_request_frame(im_window *window, uint32_t delay_ms);

/* Cancels any pending one-shot frame wake-up on an open window. */
void im_window_cancel_frame(im_window *window);
