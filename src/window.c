#include <impossible/window.h>
#include <windows.h>
#include <windowsx.h>

#define IM_CLASS L"ImpossibleSDK.Window"
#define IM_MSG_FRAME_WAKE (WM_APP + 1)

static void fail(im_window *window) {
    window->failed = true;
    PostQuitMessage(1);
}

static bool blit_rect(HDC screen, const im_canvas *canvas, im_dirty_rect r) {
    int rw = r.right - r.left;
    int rh = r.bottom - r.top;
    if (rw <= 0 || rh <= 0) return true;
    BITMAPINFO format = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = canvas->width,
                                       .biHeight = -rh, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    const uint32_t *band = canvas->pixels + (size_t)r.top * (size_t)canvas->width;
    int rows = SetDIBitsToDevice(screen, r.left, r.top, (DWORD)rw, (DWORD)rh,
                                 r.left, 0, 0, (UINT)rh, band, &format, DIB_RGB_COLORS);
    return rows == rh;
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

    im_dirty_rect local_rects[IM_WINDOW_MAX_DIRTY_RECTS];
    int local_count = 0;
    bool use_pending = (!resized_or_new && !window->pending_overflow && window->pending_count > 0);
    if (use_pending) {
        int ul = area.right, ut = area.bottom, ur = 0, ub = 0;
        for (int i = 0; i < window->pending_count; i++) {
            im_dirty_rect r = window->pending_rects[i];
            if (r.left < 0) r.left = 0;
            if (r.top < 0) r.top = 0;
            if (r.right > area.right) r.right = area.right;
            if (r.bottom > area.bottom) r.bottom = area.bottom;
            if (r.left < r.right && r.top < r.bottom) {
                local_rects[local_count++] = r;
                if (r.left < ul) ul = r.left;
                if (r.top < ut) ut = r.top;
                if (r.right > ur) ur = r.right;
                if (r.bottom > ub) ub = r.bottom;
            }
        }
        /* If Windows reported external exposure outside our queued rects, fall back to rcPaint. */
        if (local_count == 0 || dl < ul || dt < ut || dr > ur || db > ub) {
            use_pending = false;
            local_count = 0;
        }
    }
    window->pending_count = 0;
    window->pending_overflow = false;

    im_dirty_rect single_rect = {dl, dt, dr, db};
    const im_dirty_rect *rects_ptr = use_pending ? local_rects : &single_rect;
    int rects_cnt = use_pending ? local_count : 1;

    bool full = resized_or_new;
    if (!full && !use_pending && dl <= 0 && dt <= 0 && dr >= area.right && db >= area.bottom) {
        full = true;
    } else if (!full && use_pending) {
        for (int i = 0; i < rects_cnt; i++) {
            if (rects_ptr[i].left <= 0 && rects_ptr[i].top <= 0 &&
                rects_ptr[i].right >= area.right && rects_ptr[i].bottom >= area.bottom) {
                full = true;
                break;
            }
        }
    }

    im_canvas canvas = {
        .width = area.right, .height = area.bottom, .scale = scale,
        .dirty = single_rect, .dirty_rects = rects_ptr, .dirty_count = rects_cnt,
        .full_redraw = full
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
        bool ok = blit_rect(screen, &canvas, single_rect);
        if (ok) window->painted = true;
        else fail(window);
    }
    EndPaint(handle, &state);
}

static void send(im_window *window, HWND handle, im_event event) {
    bool prev_damaged = window->event_damaged;
    window->event_damaged = false;
    bool handled = window->event && window->event(window, &event);
    bool damaged_here = window->event_damaged;
    window->event_damaged = prev_damaged;
    if (handled && !damaged_here) {
        window->pending_count = 0;
        window->pending_overflow = true;
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
    case WM_SIZE:
        window->pending_count = 0;
        window->pending_overflow = true;
        InvalidateRect(handle, NULL, FALSE);
        return 0;
    case WM_TIMER:
        KillTimer(handle, wide);
        if (window->frame_timer_active && !window->frame_timer_is_posted &&
            wide == (WPARAM)window->frame_token) {
            window->frame_timer_active = false;
            if (window->tick) window->tick(window);
        }
        return 0;
    case IM_MSG_FRAME_WAKE:
        if (window->frame_timer_active && window->frame_timer_is_posted &&
            wide == (WPARAM)window->frame_token) {
            window->frame_timer_active = false;
            window->frame_timer_is_posted = false;
            if (window->tick) window->tick(window);
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
    window->pending_count = 0;
    window->pending_overflow = false;
    window->frame_timer_active = false;
    window->frame_timer_is_posted = false;
    window->frame_token = 0u;
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
    window->frame_timer_is_posted = false;
    window->pending_count = 0;
    window->pending_overflow = false;
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
    if (!window->pending_overflow) {
        if (window->pending_count < IM_WINDOW_MAX_DIRTY_RECTS) {
            window->pending_rects[window->pending_count++] = (im_dirty_rect){left, top, right, bottom};
        } else {
            window->pending_overflow = true;
        }
    }
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

static uint32_t next_frame_token(im_window *window) {
    window->frame_token++;
    if (window->frame_token == 0u) window->frame_token = 1u;
    return window->frame_token;
}

bool im_window_request_frame(im_window *window, uint32_t delay_ms) {
    if (!window || !window->native) return false;
    if (window->frame_timer_active && !window->frame_timer_is_posted) {
        KillTimer((HWND)window->native, (UINT_PTR)window->frame_token);
    }
    uint32_t tok = next_frame_token(window);
    if (delay_ms == 0u) {
        if (!PostMessageW((HWND)window->native, IM_MSG_FRAME_WAKE, (WPARAM)tok, 0)) {
            window->frame_timer_active = false;
            window->frame_timer_is_posted = false;
            return false;
        }
        window->frame_timer_active = true;
        window->frame_timer_is_posted = true;
        return true;
    }
    if (!SetTimer((HWND)window->native, (UINT_PTR)tok, (UINT)delay_ms, NULL)) {
        window->frame_timer_active = false;
        window->frame_timer_is_posted = false;
        return false;
    }
    window->frame_timer_active = true;
    window->frame_timer_is_posted = false;
    return true;
}

bool im_window_cancel_frame(im_window *window) {
    if (!window || !window->native) return false;
    if (window->frame_timer_active && !window->frame_timer_is_posted) {
        KillTimer((HWND)window->native, (UINT_PTR)window->frame_token);
    }
    next_frame_token(window);
    window->frame_timer_active = false;
    window->frame_timer_is_posted = false;
    return true;
}
