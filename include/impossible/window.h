/* Impossible SDK: one window whose program paints every pixel. Tier 1. */
#pragma once
#include <impossible/base.h>

/* The pixels of a window while it is painted. */
typedef struct im_canvas {
    uint32_t *pixels; /* 0x00RRGGBB, top row first, width pixels per row */
    int width, height;
    float scale; /* 1.0 at 96 DPI */
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
    void *native;
    void *cursor; /* active client HCURSOR set by im_host_set_cursor; NULL means arrow */
    uint32_t *buffer;
    size_t capacity;
    bool painted; /* a frame has reached the screen */
    bool failed;
} im_window;

/* Opens the window and runs it until it closes. False when it could not be
   opened or drawn. With one_frame it closes itself after the first frame. */
bool im_window_run(im_window *window, bool one_frame);
