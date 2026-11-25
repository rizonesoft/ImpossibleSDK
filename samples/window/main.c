/* A gradient with a spot of light under the pointer. --one-frame draws once and
   leaves. */
#include <impossible/win_host.h>
#include <impossible/window.h>
#include "host.h"

typedef struct state {
    int x, y;
    uint32_t last_ms;
    im_host host;
} state;

static uint32_t lift(int base, int light) {
    int value = base + light;
    return (uint32_t)(value > 255 ? 255 : value);
}

static void paint(im_window *window, im_canvas *canvas) {
    state *at = window->user;
    at->last_ms = im_host_now_ms(&at->host);
    int reach = (int)(180.0f * canvas->scale);
    for (int y = 0; y < canvas->height; y++) {
        for (int x = 0; x < canvas->width; x++) {
            long long dx = x - at->x, dy = y - at->y, limit = (long long)reach * reach;
            long long near = limit - dx * dx - dy * dy;
            int light = near > 0 && limit ? (int)(near * 110 / limit) : 0;
            uint32_t r = lift(18 + 40 * x / canvas->width, light);
            uint32_t g = lift(22 + 30 * y / canvas->height, light);
            uint32_t b = lift(48 + 70 * x / canvas->width, light);
            canvas->pixels[(size_t)y * (size_t)canvas->width + (size_t)x] = r << 16 | g << 8 | b;
        }
    }
}

static bool event(im_window *window, const im_event *event) {
    if (event->kind != IM_POINTER_MOVE) return false;
    state *at = window->user;
    at->x = event->x;
    at->y = event->y;
    im_host_set_cursor(&at->host, IM_CURSOR_HAND);
    return true;
}

int im_main(void) {
    state at = {.x = 240, .y = 160};
    im_window window = {.title = L"Impossible SDK", .width = 640, .height = 400, .user = &at, .paint = paint, .event = event};
    im_win_host_init(&at.host, &window);
    return im_window_run(&window, im_has_argument(L"--one-frame")) ? 0 : 1;
}
