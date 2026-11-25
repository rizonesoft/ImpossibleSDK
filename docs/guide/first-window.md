<!-- page: group="Start here" order=2 sources=include/impossible/window.h,include/impossible/win_host.h,samples/window/main.c reviewed=2026-11-25 -->
# Your first window

How do you open a window and paint it? Fill in one structure, give it a function that writes pixels, and run it.

## What does the smallest window look like?

This is a whole program. Windows asks for the picture, and `paint` writes it. Each pixel is `0x00RRGGBB`, top row first.

```c program=first_window window tier=1
#include <impossible/window.h>

static void paint(im_window *window, im_canvas *canvas) {
    (void)window;
    for (int i = 0; i < canvas->width * canvas->height; i++)
        canvas->pixels[i] = 0x00102040;
}

int im_main(void) {
    im_window window = {.title = L"First window", .width = 640, .height = 400, .paint = paint};
    return im_window_run(&window, false) ? 0 : 1;
}
```

> [!NOTE]
> A window needs `WINDOW TIER 1` in `CMakeLists.txt`. At tier 0 the build stops and names the functions that are too many.

## How does it answer the pointer?

Add an `event` function. Return `true` and the window is painted again. This is the one of the window sample, which keeps the pointer's place in its own structure:

```c from=samples/window/main.c
static bool event(im_window *window, const im_event *event) {
    if (event->kind == IM_THEME_CHANGED) return true;
    if (event->kind != IM_POINTER_MOVE) return false;
    state *at = window->user;
    at->x = event->x;
    at->y = event->y;
    im_host_set_cursor(&at->host, IM_CURSOR_HAND);
    return true;
}
```

The kinds of event are pointer move, down and up, and a key going down. [window.h](site:api-window.html) lists them, provides `im_window_invalidate`, `im_window_invalidate_rects`, `im_window_request_frame`, and `im_window_cancel_frame` for disjoint partial repaints and one-shot animation ticks (`canvas->dirty`, `canvas->dirty_rects`, `canvas->dirty_count`, and `canvas->full_redraw`), and [win_host.h](site:api-win_host.html) initializes the Windows host callback table for clock (`now_ms`) and cursor updates.

## What happens on a screen at 150 percent?

The width and height you give are for 100 percent. The canvas you get is already in real pixels, and `canvas->scale` is 1.5, so multiply your own sizes by it. Moving the window to a screen with another scale paints it again at that scale.

## How do you draw more than a colour?

Impossible UI draws rounded shapes and buttons into the same pixels. [Drawing](drawing.md) shows how.
