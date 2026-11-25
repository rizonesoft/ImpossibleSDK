#include <impossible/window.h>
#include <windows.h>
#include <windowsx.h>

#define IM_CLASS L"ImpossibleSDK.Window"
#define IM_FRAME_TIMER_ID 1u

static void fail(im_window *window) {
    window->failed = true;
    PostQuitMessage(1);
}

static void paint(im_window *window, HWND handle) {
    PAINTSTRUCT state;
    RECT area;
    HDC screen = BeginPaint(handle, &state);
    if (!screen) {
        fail(window);
        return;
    }
    GetClientRect(handle, &area);
    float scale = (float)GetDpiForWindow(handle) / 96.0f;
    bool resized_or_new = (!window->buffer || area.right != window->last_width ||
                           area.bottom != window->last_height || scale != window->last_scale);
    int dl = state.rcPaint.left < 0 ? 0 : state.rcPaint.left;
    int dt = state.rcPaint.top < 0 ? 0 : state.rcPaint.top;
    int dr = state.rcPaint.right > area.right ? area.right : state.rcPaint.right;
    int db = state.rcPaint.bottom > area.bottom ? area.bottom : state.rcPaint.bottom;
    if (resized_or_new) {
        dl = 0; dt = 0; dr = area.right; db = area.bottom;
    }
    bool full = resized_or_new || (dl <= 0 && dt <= 0 && dr >= area.right && db >= area.bottom);
    im_canvas canvas = {
        .width = area.right, .height = area.bottom, .scale = scale,
        .dirty = {dl, dt, dr, db}, .full_redraw = full
    };
    size_t need = (size_t)canvas.width * (size_t)canvas.height * sizeof(uint32_t);
    if (need > window->capacity) {
        if (window->buffer) VirtualFree(window->buffer, 0, MEM_RELEASE);
        window->buffer = VirtualAlloc(NULL, need, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        window->capacity = window->buffer ? need : 0;
    }
    if (need && !window->buffer) {
        fail(window);
    } else if (need && dr > dl && db > dt) {
        canvas.pixels = window->buffer;
        window->paint(window, &canvas);
        window->last_width = canvas.width;
        window->last_height = canvas.height;
        window->last_scale = scale;
        int dh = db - dt;
        BITMAPINFO format = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = canvas.width,
                                           .biHeight = -dh, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
        const uint32_t *band = canvas.pixels + (size_t)dt * (size_t)canvas.width;
        int rows = SetDIBitsToDevice(screen, 0, dt, (DWORD)canvas.width, (DWORD)dh, 0, 0, 0,
                                     (UINT)dh, band, &format, DIB_RGB_COLORS);
        if (rows == dh) window->painted = true;
        else fail(window);
    }
    EndPaint(handle, &state);
}

static void send(im_window *window, HWND handle, im_event event) {
    window->event_damaged = false;
    if (window->event && window->event(window, &event) && !window->event_damaged) {
        InvalidateRect(handle, NULL, FALSE);
    }
}

static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wide, LPARAM longer) {
    if (message == WM_NCCREATE) {
        im_window *created = ((CREATESTRUCTW *)longer)->lpCreateParams;
        created->native = handle;
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)created);
    }
    im_window *window = (im_window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    if (!window) return DefWindowProcW(handle, message, wide, longer);

    im_event event = {.x = GET_X_LPARAM(longer), .y = GET_Y_LPARAM(longer)};
    switch (message) {
    case WM_PAINT: paint(window, handle); return 0;
    case WM_ERASEBKGND: return 1; /* every pixel is painted in WM_PAINT */
    case WM_SIZE: InvalidateRect(handle, NULL, FALSE); return 0;
    case WM_TIMER:
        if (wide == IM_FRAME_TIMER_ID) {
            KillTimer(handle, IM_FRAME_TIMER_ID);
            window->frame_timer_active = false;
            if (window->tick) window->tick(window);
            return 0;
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(longer) == HTCLIENT) {
            HCURSOR active = window->cursor ? (HCURSOR)window->cursor : LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            SetCursor(active);
            return TRUE;
        }
        return DefWindowProcW(handle, message, wide, longer);
    case WM_MOUSEMOVE: event.kind = IM_POINTER_MOVE; send(window, handle, event); return 0;
    case WM_LBUTTONDOWN: SetCapture(handle); event.kind = IM_POINTER_DOWN; send(window, handle, event); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); event.kind = IM_POINTER_UP; send(window, handle, event); return 0;
    case WM_KEYDOWN: send(window, handle, (im_event){.kind = IM_KEY_DOWN, .key = (int)wide}); return 0;
    case WM_DPICHANGED: {
        const RECT *best = (const RECT *)longer;
        SetWindowPos(handle, NULL, best->left, best->top, best->right - best->left, best->bottom - best->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(window->failed ? 1 : 0); return 0;
    default: return DefWindowProcW(handle, message, wide, longer);
    }
}

/* Grows the window until its client area is the asked size at its own DPI. */
static void fit(HWND handle, int width, int height) {
    RECT outer, inner;
    int dpi = (int)GetDpiForWindow(handle);
    GetWindowRect(handle, &outer);
    GetClientRect(handle, &inner);
    SetWindowPos(handle, NULL, 0, 0, width * dpi / 96 + (outer.right - outer.left) - inner.right,
                 height * dpi / 96 + (outer.bottom - outer.top) - inner.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool im_window_run(im_window *window, bool one_frame) {
    window->painted = window->failed = false;
    if (window->width < 1 || window->height < 1 || window->width > 32767 || window->height > 32767) return false;
    HINSTANCE self = GetModuleHandleW(NULL);
    WNDCLASSEXW type = {.cbSize = sizeof type, .lpfnWndProc = procedure, .hInstance = self,
                        .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW), .lpszClassName = IM_CLASS};
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    HWND handle = CreateWindowExW(0, IM_CLASS, window->title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  window->width, window->height, NULL, NULL, self, window);
    if (!handle) return false;
    fit(handle, window->width, window->height);
    ShowWindow(handle, one_frame ? SW_SHOWNOACTIVATE : SW_SHOW);

    MSG message;
    BOOL got = 1;
    while (!(one_frame && window->painted) && (got = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyWindow(handle);
    window->native = NULL;
    window->frame_timer_active = false;
    if (window->buffer) VirtualFree(window->buffer, 0, MEM_RELEASE);
    window->buffer = NULL;
    window->capacity = 0;
    while (PeekMessageW(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    return got >= 0 && window->painted && !window->failed;
}

bool im_window_invalidate(im_window *window, int left, int top, int right, int bottom) {
    if (!window || !window->native || left >= right || top >= bottom) return false;
    RECT r = {left, top, right, bottom};
    if (!InvalidateRect((HWND)window->native, &r, FALSE)) return false;
    window->event_damaged = true;
    return true;
}

bool im_window_invalidate_rects(im_window *window, const im_dirty_rect *rects, int count) {
    if (!window || !window->native || !rects || count <= 0) return false;
    bool any = false;
    for (int i = 0; i < count; i++) {
        if (im_window_invalidate(window, rects[i].left, rects[i].top, rects[i].right, rects[i].bottom)) {
            any = true;
        }
    }
    return any;
}

bool im_window_request_frame(im_window *window, uint32_t delay_ms) {
    if (!window || !window->native) return false;
    UINT ms = delay_ms == 0u ? 1u : (UINT)delay_ms;
    if (!SetTimer((HWND)window->native, IM_FRAME_TIMER_ID, ms, NULL)) return false;
    window->frame_timer_active = true;
    return true;
}

void im_window_cancel_frame(im_window *window) {
    if (!window || !window->native || !window->frame_timer_active) return;
    KillTimer((HWND)window->native, IM_FRAME_TIMER_ID);
    window->frame_timer_active = false;
}
