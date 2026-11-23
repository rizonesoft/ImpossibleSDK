/* One im_window opened twice: the second run must paint again. */
#include <impossible/window.h>

static void paint(im_window *window, im_canvas *canvas) {
    (void)canvas;
    ++*(int *)window->user;
}

int im_main(void) {
    int frames = 0;
    im_window window = {.title = L"Reopen", .width = 160, .height = 100, .user = &frames, .paint = paint};
    im_window huge = {.title = L"Huge", .width = 0x7FFFFFFF, .height = 100, .user = &frames, .paint = paint};
    if (im_window_run(&huge, true)) return 3;
    if (!im_window_run(&window, true)) return 1;
    int first = frames;
    return im_window_run(&window, true) && first && frames > first ? 0 : 2;
}
