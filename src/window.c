#include <impossible/window.h>
#include <windows.h>
#include <windowsx.h>

#define IM_CLASS L"ImpossibleSDK.Window"

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
    im_canvas canvas = {.width = area.right, .height = area.bottom, .scale = (float)GetDpiForWindow(handle) / 96.0f};
    size_t need = (size_t)canvas.width * (size_t)canvas.height * sizeof(uint32_t);
    if (need > window->capacity) {
        if (window->buffer) VirtualFree(window->buffer, 0, MEM_RELEASE);
        window->buffer = VirtualAlloc(NULL, need, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        window->capacity = window->buffer ? need : 0;
    }
    if (need && !window->buffer) {
        fail(window);
    } else if (need) {
        canvas.pixels = window->buffer;
        window->paint(window, &canvas);
        BITMAPINFO format = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = canvas.width,
                                           .biHeight = -canvas.height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
        int rows = SetDIBitsToDevice(screen, 0, 0, (DWORD)canvas.width, (DWORD)canvas.height, 0, 0, 0,
                                     (UINT)canvas.height, canvas.pixels, &format, DIB_RGB_COLORS);
        if (rows == canvas.height) window->painted = true;
        else fail(window);
    }
    EndPaint(handle, &state);
}

static void send(im_window *window, HWND handle, im_event event) {
    if (window->event && window->event(window, &event)) InvalidateRect(handle, NULL, FALSE);
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
    if (window->buffer) VirtualFree(window->buffer, 0, MEM_RELEASE);
    window->buffer = NULL;
    window->capacity = 0;
    while (PeekMessageW(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    return got >= 0 && window->painted && !window->failed;
}
