/* Impossible SDK: Windows Tier 0/1 implementation of the portable im_host callback table. */
#include <impossible/win_host.h>
#include "host.h"
#include <windows.h>

static void *win_alloc(void *ctx, size_t size) {
    (void)ctx;
    if (size == 0u) return NULL;
    HANDLE heap = GetProcessHeap();
    if (!heap) return NULL;
    return HeapAlloc(heap, HEAP_ZERO_MEMORY, size);
}

static void *win_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    (void)ctx;
    (void)old_size;
    HANDLE heap = GetProcessHeap();
    if (!heap) return NULL;
    if (new_size == 0u) {
        if (ptr) HeapFree(heap, 0, ptr);
        return NULL;
    }
    if (!ptr) {
        return HeapAlloc(heap, HEAP_ZERO_MEMORY, new_size);
    }
    return HeapReAlloc(heap, HEAP_ZERO_MEMORY, ptr, new_size);
}

static void win_free(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    if (!ptr) return;
    HANDLE heap = GetProcessHeap();
    if (heap) HeapFree(heap, 0, ptr);
}

static uint32_t win_now_ms(void *ctx) {
    (void)ctx;
    LARGE_INTEGER freq, count;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) return 0u;
    if (!QueryPerformanceCounter(&count) || count.QuadPart < 0) return 0u;
    uint64_t c = (uint64_t)count.QuadPart;
    uint64_t f = (uint64_t)freq.QuadPart;
    uint64_t sec = c / f;
    uint64_t rem = c % f;
    uint64_t ms = sec * 1000ULL + (rem * 1000ULL) / f;
    return (uint32_t)ms;
}

static im_host_result win_set_cursor(void *ctx, im_cursor cursor) {
    LPCWSTR id = (LPCWSTR)IDC_ARROW;
    switch (cursor) {
    case IM_CURSOR_ARROW: id = (LPCWSTR)IDC_ARROW; break;
    case IM_CURSOR_IBEAM: id = (LPCWSTR)IDC_IBEAM; break;
    case IM_CURSOR_HAND: id = (LPCWSTR)IDC_HAND; break;
    case IM_CURSOR_SIZE_WE: id = (LPCWSTR)IDC_SIZEWE; break;
    case IM_CURSOR_SIZE_NS: id = (LPCWSTR)IDC_SIZENS; break;
    case IM_CURSOR_WAIT: id = (LPCWSTR)IDC_WAIT; break;
    default: return IM_HOST_INVALID;
    }
    HCURSOR hcur = LoadCursorW(NULL, id);
    if (!hcur) return IM_HOST_FAILED;
    if (ctx) {
        im_window *window = (im_window *)ctx;
        window->cursor = hcur;
    }
    SetCursor(hcur);
    return IM_HOST_OK;
}

void im_win_host_init(im_host *host, im_window *window) {
    if (!host) return;
    memset(host, 0, sizeof(*host));
    host->version = IM_HOST_VERSION;
    host->size = (uint32_t)sizeof(*host);
    host->ctx = window;
    host->alloc = win_alloc;
    host->realloc = win_realloc;
    host->free = win_free;
    host->now_ms = win_now_ms;
    host->set_cursor = win_set_cursor;
}
