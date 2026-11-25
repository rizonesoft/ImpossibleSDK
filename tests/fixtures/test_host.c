/* Exits 0, or with the number of the first check that failed. */
#include <impossible/base.h>
#include <impossible/win_host.h>
#include <impossible/window.h>
#include <windows.h>
#include "host.h"
#include "layer.h"
#include "layout.h"
#include "states.h"
#include "tree.h"

static bool str_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

/* Genuinely shorter v1 backing struct (no v2 fields exist in memory after now_ms). */
typedef struct host_v1_table {
    uint32_t version;
    uint32_t size;
    void *ctx;
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t new_size);
    void (*free)(void *ctx, void *ptr, size_t size);
    uint32_t (*now_ms)(void *ctx);
} host_v1_table;

static uint32_t v1_clock(void *ctx) {
    return *(const uint32_t *)ctx;
}

typedef enum rec_op {
    OP_ALLOC = 1,
    OP_REALLOC,
    OP_FREE,
    OP_NOW_MS,
    OP_CLIP_GET,
    OP_CLIP_SET,
    OP_CURSOR,
    OP_CARET,
    OP_IME,
    OP_OPEN_URL,
    OP_SOUND,
    OP_FILE_PICK,
    OP_ANNOUNCE
} rec_op;

typedef struct rec_event {
    rec_op op;
    void *ctx;
    int32_t arg;
} rec_event;

enum { REC_POOL_CAP = 256 * 1024, REC_LOG_CAP = 128 };
static uint8_t rec_pool[REC_POOL_CAP];

typedef struct rec_host {
    im_host host;
    size_t pool_used;
    uint32_t live_allocs;
    size_t live_bytes;
    bool fail_next_realloc;
    uint32_t clock_ms;
    bool cancel_next_pick;
    bool fail_next_clip_get;
    char clip_buf[64];
    size_t clip_len;
    im_rect last_ime_rect;
    char last_url[64];
    char last_announce[64];
    size_t last_pick_cap;
    rec_event log[REC_LOG_CAP];
    uint32_t log_count;
} rec_host;

static void rec_push(rec_host *r, rec_op op, void *ctx, int32_t arg) {
    if (r->log_count < REC_LOG_CAP) {
        r->log[r->log_count++] = (rec_event){op, ctx, arg};
    }
}

static void *rec_alloc(void *ctx, size_t size) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_ALLOC, ctx, (int32_t)size);
    size_t aligned = (size + 7u) & ~(size_t)7u;
    if (size == 0u || r->pool_used + aligned > REC_POOL_CAP) return NULL;
    void *p = &rec_pool[r->pool_used];
    r->pool_used += aligned;
    memset(p, 0, size);
    r->live_allocs++;
    r->live_bytes += size;
    return p;
}

static void *rec_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_REALLOC, ctx, (int32_t)new_size);
    if (r->fail_next_realloc) {
        r->fail_next_realloc = false;
        return NULL;
    }
    if (new_size == 0u) {
        if (ptr) {
            if (r->live_allocs > 0u) r->live_allocs--;
            r->live_bytes -= old_size;
        }
        return NULL;
    }
    size_t aligned = (new_size + 7u) & ~(size_t)7u;
    if (r->pool_used + aligned > REC_POOL_CAP) return NULL;
    void *n = &rec_pool[r->pool_used];
    r->pool_used += aligned;
    memset(n, 0, new_size);
    if (ptr && old_size > 0u) {
        memcpy(n, ptr, old_size < new_size ? old_size : new_size);
        r->live_bytes -= old_size;
    } else {
        r->live_allocs++;
    }
    r->live_bytes += new_size;
    return n;
}

static void rec_free(void *ctx, void *ptr, size_t size) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_FREE, ctx, (int32_t)size);
    if (!ptr) return;
    if (r->live_allocs > 0u) r->live_allocs--;
    r->live_bytes -= size;
}

static uint32_t rec_now_ms(void *ctx) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_NOW_MS, ctx, (int32_t)r->clock_ms);
    return r->clock_ms;
}

static im_host_result rec_clip_get(void *ctx, char *buf, size_t cap, size_t *out_len) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_CLIP_GET, ctx, (int32_t)cap);
    if (r->fail_next_clip_get) {
        r->fail_next_clip_get = false;
        /* Deliberately write partial output before failing to prove wrapper clears it. */
        if (buf && cap > 1u) { buf[0] = 'y'; buf[1] = '\0'; }
        if (out_len) *out_len = 1u;
        return IM_HOST_FAILED;
    }
    size_t n = r->clip_len;
    if (buf && cap > 0u) {
        size_t copy = n < cap - 1u ? n : cap - 1u;
        if (copy > 0u) memcpy(buf, r->clip_buf, copy);
        buf[copy] = '\0';
    }
    if (out_len) *out_len = n;
    return IM_HOST_OK;
}

static im_host_result rec_clip_set(void *ctx, const char *utf8, size_t len) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_CLIP_SET, ctx, (int32_t)len);
    size_t copy = len < sizeof(r->clip_buf) - 1u ? len : sizeof(r->clip_buf) - 1u;
    if (copy > 0u) memcpy(r->clip_buf, utf8, copy);
    r->clip_buf[copy] = '\0';
    r->clip_len = copy;
    return IM_HOST_OK;
}

static im_host_result rec_set_cursor(void *ctx, im_cursor cursor) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_CURSOR, ctx, (int32_t)cursor);
    return IM_HOST_OK;
}

static im_host_result rec_caret_blink(void *ctx, uint32_t *out_ms) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_CARET, ctx, 600);
    if (out_ms) *out_ms = 600u;
    return IM_HOST_OK;
}

static im_host_result rec_ime(void *ctx, im_rect rect) {
    rec_host *r = (rec_host *)ctx;
    r->last_ime_rect = rect;
    rec_push(r, OP_IME, ctx, rect.left);
    return IM_HOST_OK;
}

static void copy_str_cap(char *dst, size_t cap, const char *src) {
    size_t i = 0u;
    while (src && src[i] && i + 1u < cap) { dst[i] = src[i]; i++; }
    if (cap > 0u) dst[i] = '\0';
}

static im_host_result rec_open_url(void *ctx, const char *utf8_url) {
    rec_host *r = (rec_host *)ctx;
    copy_str_cap(r->last_url, sizeof(r->last_url), utf8_url);
    rec_push(r, OP_OPEN_URL, ctx, (int32_t)utf8_url[0]);
    return IM_HOST_OK;
}

static im_host_result rec_play_sound(void *ctx, im_sound_id sound) {
    rec_host *r = (rec_host *)ctx;
    rec_push(r, OP_SOUND, ctx, (int32_t)sound);
    return IM_HOST_OK;
}

static im_host_result rec_file_pick(void *ctx, im_file_pick_kind kind, char *utf8_path, size_t cap, size_t *out_len) {
    rec_host *r = (rec_host *)ctx;
    r->last_pick_cap = cap;
    rec_push(r, OP_FILE_PICK, ctx, (int32_t)kind);
    if (r->cancel_next_pick) {
        r->cancel_next_pick = false;
        /* Deliberately write partial output before cancelling to prove wrapper clears it. */
        if (utf8_path && cap > 1u) { utf8_path[0] = 'x'; utf8_path[1] = '\0'; }
        if (out_len) *out_len = 1u;
        return IM_HOST_CANCELLED;
    }
    const char *sample = "C:/demo.txt";
    size_t n = 11u;
    if (utf8_path && cap > 0u) {
        size_t copy = n < cap - 1u ? n : cap - 1u;
        memcpy(utf8_path, sample, copy);
        utf8_path[copy] = '\0';
    }
    if (out_len) *out_len = n;
    return IM_HOST_OK;
}

static im_host_result rec_announce(void *ctx, const char *utf8_text) {
    rec_host *r = (rec_host *)ctx;
    copy_str_cap(r->last_announce, sizeof(r->last_announce), utf8_text);
    rec_push(r, OP_ANNOUNCE, ctx, (int32_t)utf8_text[0]);
    return IM_HOST_OK;
}

static void rec_host_init(rec_host *r) {
    memset(r, 0, sizeof(*r));
    r->host.version = IM_HOST_VERSION;
    r->host.size = (uint32_t)sizeof(im_host);
    r->host.ctx = r;
    r->host.alloc = rec_alloc;
    r->host.realloc = rec_realloc;
    r->host.free = rec_free;
    r->host.now_ms = rec_now_ms;
    r->host.clipboard_get_utf8 = rec_clip_get;
    r->host.clipboard_set_utf8 = rec_clip_set;
    r->host.set_cursor = rec_set_cursor;
    r->host.caret_blink_ms = rec_caret_blink;
    r->host.ime_set_rect = rec_ime;
    r->host.open_url = rec_open_url;
    r->host.play_sound = rec_play_sound;
    r->host.file_pick = rec_file_pick;
    r->host.announce = rec_announce;
}

typedef struct cursor_win_ctx {
    im_host host;
    int status;
} cursor_win_ctx;

static void cursor_test_paint(im_window *window, im_canvas *canvas) {
    cursor_win_ctx *c = (cursor_win_ctx *)window->user;
    for (int i = 0; i < canvas->width * canvas->height; i++) canvas->pixels[i] = 0x00202020u;
    HCURSOR ibeam = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM);
    HCURSOR hand = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    HCURSOR arrow = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    if (im_host_set_cursor(&c->host, IM_CURSOR_HAND) != IM_HOST_OK) { c->status = 71; return; }
    if (window->cursor != hand) { c->status = 72; return; }
    HWND hwnd = (HWND)window->native;
    WNDPROC proc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    /* Install a different cursor first, then prove WM_SETCURSOR actually calls SetCursor(hand). */
    SetCursor(ibeam);
    LRESULT handled = proc(hwnd, WM_SETCURSOR, (WPARAM)hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    if (handled != TRUE || SetCursor(ibeam) != hand) { c->status = 73; return; }
    if (im_host_set_cursor(&c->host, IM_CURSOR_ARROW) != IM_HOST_OK) { c->status = 74; return; }
    if (window->cursor != arrow) { c->status = 75; return; }
    SetCursor(ibeam);
    handled = proc(hwnd, WM_SETCURSOR, (WPARAM)hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    if (handled != TRUE || SetCursor(ibeam) != arrow) { c->status = 76; return; }
    c->status = 0;
}

int im_main(void) {
    /* 1. ABI version/size compatibility: genuinely shorter v1 struct, present NULL slot,
          partial memory set refused as a unit, and incompatible versions (0 and 99). */
    {
        uint32_t v1_t = 4321u;
        host_v1_table v1 = {
            .version = 1u, .size = (uint32_t)sizeof(host_v1_table), .ctx = &v1_t,
            .alloc = NULL, .realloc = NULL, .free = NULL, .now_ms = v1_clock
        };
        const im_host *v1_as_host = (const im_host *)&v1;
        const char *reason = NULL;
        const char *missing = NULL;
        if (im_host_accept(v1_as_host, &reason) != IM_HOST_OK || reason != NULL) return 1;
        if (im_host_now_ms(v1_as_host) != 4321u) return 2;
        if (im_host_require(v1_as_host, "now_ms", &missing) != IM_HOST_OK || missing != NULL) return 3;
        if (im_host_require(v1_as_host, "set_cursor", &missing) != IM_HOST_UNSUPPORTED || !str_eq(missing, "set_cursor")) return 4;
        if (im_host_set_cursor(v1_as_host, IM_CURSOR_HAND) != IM_HOST_UNSUPPORTED) return 5;
        char buf[16] = "dirty";
        size_t len = 99u;
        if (im_host_clipboard_get_utf8(v1_as_host, buf, sizeof(buf), &len) != IM_HOST_UNSUPPORTED || len != 0u || buf[0] != '\0') return 6;
        uint32_t blink = 0u;
        if (im_host_caret_blink_ms(v1_as_host, &blink) != IM_HOST_UNSUPPORTED || blink != IM_HOST_DEFAULT_CARET_BLINK_MS) return 7;

        /* Present NULL slot in full v2 table. */
        rec_host partial;
        rec_host_init(&partial);
        partial.host.open_url = NULL;
        if (im_host_require(&partial.host, "open_url", &missing) != IM_HOST_UNSUPPORTED || !str_eq(missing, "open_url")) return 8;
        if (im_host_open_url(&partial.host, "https://impossibleos.co") != IM_HOST_UNSUPPORTED) return 9;

        /* Partial memory callback set (alloc + realloc without free) must be refused as a unit. */
        partial.host.free = NULL;
        if (im_host_require(&partial.host, "memory", &missing) != IM_HOST_UNSUPPORTED || !str_eq(missing, "free")) return 10;
        if (im_host_alloc(&partial.host, 32u) != NULL) return 11;
        im_allocator bad_alloc = im_host_allocator(&partial.host);
        if (bad_alloc.alloc(bad_alloc.ctx, 32u) != NULL) return 12;

        /* Larger storage advertising version = 1 MUST still cap readable extent at IM_HOST_V1_SIZE. */
        rec_host v1_big;
        rec_host_init(&v1_big);
        v1_big.host.version = 1u;
        v1_big.host.size = (uint32_t)sizeof(im_host);
        if (im_host_accept(&v1_big.host, &reason) != IM_HOST_OK || reason != NULL) return 17;
        if (im_host_require(&v1_big.host, "set_cursor", &missing) != IM_HOST_UNSUPPORTED || !str_eq(missing, "set_cursor")) return 18;
        if (im_host_set_cursor(&v1_big.host, IM_CURSOR_HAND) != IM_HOST_UNSUPPORTED) return 19;

        /* Incompatible versions (0 and 99) and undersized header must each provide a fresh refusal reason. */
        im_host bad_v0 = {.version = 0u, .size = (uint32_t)sizeof(im_host)};
        im_host bad_v99 = {.version = 99u, .size = (uint32_t)sizeof(im_host)};
        im_host bad_sz = {.version = 1u, .size = 4u};
        reason = NULL;
        if (im_host_accept(NULL, &reason) != IM_HOST_INVALID || !reason) return 13;
        reason = NULL;
        if (im_host_accept(&bad_v0, &reason) != IM_HOST_INVALID || !reason) return 14;
        reason = NULL;
        if (im_host_accept(&bad_v99, &reason) != IM_HOST_INVALID || !reason) return 15;
        reason = NULL;
        if (im_host_accept(&bad_sz, &reason) != IM_HOST_INVALID || !reason) return 16;
    }

    /* 2. Empty and NULL table safe defaults across all wrappers plus real ui/ consumers. */
    {
        im_host empty = {.version = IM_HOST_VERSION, .size = (uint32_t)sizeof(im_host)};
        const im_host *cases[2] = {&empty, NULL};
        for (int ci = 0; ci < 2; ci++) {
            const im_host *h = cases[ci];
            im_host_result expect = (ci == 0) ? IM_HOST_UNSUPPORTED : IM_HOST_INVALID;
            char buf[8] = "abc";
            size_t len = 5u;
            uint32_t blink = 0u;
            if (im_host_alloc(h, 16u) != NULL) return 20;
            if (im_host_realloc(h, NULL, 0u, 16u) != NULL) return 21;
            im_host_free(h, buf, 8u);
            if (im_host_now_ms(h) != 0u) return 22;
            if (im_host_clipboard_get_utf8(h, buf, sizeof(buf), &len) != expect || len != 0u || buf[0] != '\0') return 23;
            if (im_host_clipboard_set_utf8(h, "hi", 2u) != expect) return 24;
            if (im_host_set_cursor(h, IM_CURSOR_IBEAM) != expect) return 25;
            if (im_host_caret_blink_ms(h, &blink) != expect || blink != IM_HOST_DEFAULT_CARET_BLINK_MS) return 26;
            if (im_host_ime_set_rect(h, (im_rect){0, 0, IM_ONE, IM_ONE}) != expect) return 27;
            if (im_host_open_url(h, "https://sdk.impossibleos.co") != expect) return 28;
            if (im_host_play_sound(h, IM_SOUND_INVOKE) != expect) return 29;
            if (im_host_file_pick(h, IM_FILE_PICK_OPEN, buf, sizeof(buf), &len) != expect || len != 0u || buf[0] != '\0') return 30;
            if (im_host_announce(h, "Ready") != expect) return 31;

            /* Real ui/ consumers with empty/NULL host allocator must fail cleanly without crashing. */
            im_tree t;
            if (!im_tree_init(&t, im_host_allocator(h))) return 32;
            if (im_tree_create_node(&t, IM_ELEM_GRID, "R") != NULL) return 33;
            im_tree_destroy(&t);

            uint32_t px[16] = {0};
            im_surface s = {px, 4, 4, 4, {IM_DARK, {0}}, IM_ONE, NULL, NULL};
            im_layer_ctx lctx;
            if (!im_layer_init(&lctx, &s, im_host_allocator(h), 1024u)) return 34;
            if (im_layer_push(&lctx, (im_rect){0, 0, 4 * IM_ONE, 4 * IM_ONE}, 128u, false, (im_radii){0}) != IM_LAYER_ERR) return 35;
            im_layer_destroy(&lctx);

            im_state_machine sm;
            im_states_init(&sm, IM_TEMPLATE_BUTTON, &s.palette);
            im_states_go_to(&sm, "PointerOver", im_host_now_ms(h));
            im_states_draw_button(&sm, &s, (im_rect){0, 0, 4 * IM_ONE, 4 * IM_ONE}, im_host_now_ms(h));
        }
    }

    /* 3. Recording host: real consumers (im_tree first append via realloc(NULL), growth, refusal, teardown;
          im_layer_ctx; im_state_machine driven by advancing clock) + all 10 wrapper domains in exact order. */
    {
        rec_host rec;
        rec_host_init(&rec);
        im_allocator alloc = im_host_allocator(&rec.host);

        /* Tree: first child append calls realloc(NULL, 0, ...), growth preserves exact child order,
           refusal preserves all 8 children intact, teardown leaks 0 bytes. */
        im_tree tree;
        if (!im_tree_init(&tree, alloc)) return 40;
        im_node *root = im_tree_create_node(&tree, IM_ELEM_STACK_PANEL, "Root");
        if (!root) return 41;
        tree.root = root;
        im_node *kids[8] = {0};
        for (int i = 0; i < 8; i++) {
            kids[i] = im_tree_create_node(&tree, IM_ELEM_BORDER, "Child");
            if (!kids[i] || !im_tree_append_child(&tree, root, kids[i])) return 42;
        }
        for (uint32_t i = 0; i < 8u; i++) {
            if (root->children[i] != kids[i]) return 43;
        }
        im_node *extra = im_tree_create_node(&tree, IM_ELEM_BORDER, "Extra");
        if (!extra) return 44;
        rec.fail_next_realloc = true;
        if (im_tree_append_child(&tree, root, extra)) return 45;
        if (root->child_count != 8u) return 46;
        for (uint32_t i = 0; i < 8u; i++) {
            if (root->children[i] != kids[i]) return 46;
        }
        im_tree_destroy_subtree(&tree, extra);
        im_tree_destroy(&tree);
        if (rec.live_allocs != 0u || rec.live_bytes != 0u) return 47;

        /* Layer consumer backed by recording host allocator: push IM_LAYER_OK, fill, pop, zero leaks. */
        uint32_t lpx[64] = {0};
        im_surface lsurf = {lpx, 8, 8, 8, {IM_DARK, {0}}, IM_ONE, NULL, NULL};
        im_layer_ctx lctx;
        if (!im_layer_init(&lctx, &lsurf, alloc, 4096u)) return 47;
        if (im_layer_push(&lctx, (im_rect){0, 0, 8 * IM_ONE, 8 * IM_ONE}, 128u, false, (im_radii){0}) != IM_LAYER_OK) return 47;
        im_fill_argb(&lsurf, (im_rect){0, 0, 8 * IM_ONE, 8 * IM_ONE}, (im_radii){0}, 0xFFFF0000u);
        im_layer_pop(&lctx);
        im_layer_destroy(&lctx);
        if (lpx[0] == 0u || rec.live_allocs != 0u || rec.live_bytes != 0u) return 47;

        /* State machine driven by recording host clock advancing 1000 -> 1021 -> 1083 ms. */
        im_palette pal = {IM_DARK, {0}};
        im_state_machine sm;
        im_states_init(&sm, IM_TEMPLATE_BUTTON, &pal);
        rec.clock_ms = 1000u;
        im_states_go_to(&sm, "Normal", im_host_now_ms(&rec.host));
        im_states_go_to(&sm, "PointerOver", im_host_now_ms(&rec.host));
        rec.clock_ms = 1021u;
        if (!im_states_is_animating(&sm, im_host_now_ms(&rec.host))) return 48;
        rec.clock_ms = 1083u;
        if (im_states_is_animating(&sm, im_host_now_ms(&rec.host))) return 49;

        /* Reset log and invoke all 10 callback families in exact known order, verifying forwarded ctx, args, outputs. */
        rec.log_count = 0u;
        void *p = im_host_alloc(&rec.host, 16u);
        p = im_host_realloc(&rec.host, p, 16u, 32u);
        im_host_free(&rec.host, p, 32u);
        rec.clock_ms = 777u;
        if (im_host_now_ms(&rec.host) != 777u) return 50;
        if (im_host_clipboard_set_utf8(&rec.host, "hello", 5u) != IM_HOST_OK) return 51;
        char cbuf[32] = {0};
        size_t clen = 0u;
        if (im_host_clipboard_get_utf8(&rec.host, cbuf, sizeof(cbuf), &clen) != IM_HOST_OK || clen != 5u || !str_eq(cbuf, "hello")) return 52;
        /* Clipboard failure after partial write must clear caller outputs. */
        rec.fail_next_clip_get = true;
        if (im_host_clipboard_get_utf8(&rec.host, cbuf, sizeof(cbuf), &clen) != IM_HOST_FAILED || clen != 0u || cbuf[0] != '\0') return 52;
        if (im_host_set_cursor(&rec.host, IM_CURSOR_SIZE_WE) != IM_HOST_OK) return 53;
        uint32_t blink = 0u;
        if (im_host_caret_blink_ms(&rec.host, &blink) != IM_HOST_OK || blink != 600u) return 54;
        im_rect ime_r = {12 * IM_ONE, 4 * IM_ONE, 20 * IM_ONE, 16 * IM_ONE};
        if (im_host_ime_set_rect(&rec.host, ime_r) != IM_HOST_OK) return 55;
        if (rec.last_ime_rect.left != ime_r.left || rec.last_ime_rect.top != ime_r.top ||
            rec.last_ime_rect.right != ime_r.right || rec.last_ime_rect.bottom != ime_r.bottom) return 55;
        if (im_host_open_url(&rec.host, "https://sdk.impossibleos.co") != IM_HOST_OK ||
            !str_eq(rec.last_url, "https://sdk.impossibleos.co")) return 56;
        if (im_host_play_sound(&rec.host, IM_SOUND_FOCUS) != IM_HOST_OK) return 57;
        char pbuf[32] = {0};
        size_t plen = 0u;
        if (im_host_file_pick(&rec.host, IM_FILE_PICK_OPEN, pbuf, sizeof(pbuf), &plen) != IM_HOST_OK ||
            rec.last_pick_cap != sizeof(pbuf) || plen != 11u || !str_eq(pbuf, "C:/demo.txt")) return 58;
        rec.cancel_next_pick = true;
        rec.last_pick_cap = 0u;
        if (im_host_file_pick(&rec.host, IM_FILE_PICK_SAVE, pbuf, sizeof(pbuf), &plen) != IM_HOST_CANCELLED ||
            rec.last_pick_cap != sizeof(pbuf) || plen != 0u || pbuf[0] != '\0') return 59;
        if (im_host_announce(&rec.host, "Saved") != IM_HOST_OK || !str_eq(rec.last_announce, "Saved")) return 60;

        const rec_op want_ops[15] = {
            OP_ALLOC, OP_REALLOC, OP_FREE, OP_NOW_MS, OP_CLIP_SET, OP_CLIP_GET, OP_CLIP_GET,
            OP_CURSOR, OP_CARET, OP_IME, OP_OPEN_URL, OP_SOUND, OP_FILE_PICK, OP_FILE_PICK, OP_ANNOUNCE
        };
        const int32_t want_args[15] = {
            16, 32, 32, 777, 5, 32, 32,
            (int32_t)IM_CURSOR_SIZE_WE, 600, 12 * IM_ONE, (int32_t)'h', (int32_t)IM_SOUND_FOCUS,
            (int32_t)IM_FILE_PICK_OPEN, (int32_t)IM_FILE_PICK_SAVE, (int32_t)'S'
        };
        if (rec.log_count != 15u) return 61;
        for (uint32_t i = 0; i < 15u; i++) {
            if (rec.log[i].op != want_ops[i] || rec.log[i].ctx != &rec || rec.log[i].arg != want_args[i]) return 62;
        }
    }

    /* 4. Live Windows host (im_win_host_init): HeapAlloc/HeapReAlloc(NULL)/HeapFree via im_tree,
          advancing monotonic QPC clock, and WM_SETCURSOR cursor retention + arrow restoration. */
    {
        im_host wh;
        im_win_host_init(&wh, NULL);
        if (im_host_accept(&wh, NULL) != IM_HOST_OK) return 63;

        /* Tree backed directly by Windows host allocator (exercises win_realloc(NULL, 0, ...) on first append!). */
        im_tree wtree;
        if (!im_tree_init(&wtree, im_host_allocator(&wh))) return 64;
        im_node *wroot = im_tree_create_node(&wtree, IM_ELEM_STACK_PANEL, "WRoot");
        if (!wroot) return 65;
        wtree.root = wroot;
        for (int i = 0; i < 10; i++) {
            im_node *ch = im_tree_create_node(&wtree, IM_ELEM_BORDER, "WChild");
            if (!ch || !im_tree_append_child(&wtree, wroot, ch)) return 66;
        }
        if (wroot->child_count != 10u) return 67;
        im_tree_destroy(&wtree);

        /* Monotonic clock actually advances across Sleep(20). */
        uint32_t t0 = im_host_now_ms(&wh);
        Sleep(20);
        uint32_t t1 = im_host_now_ms(&wh);
        if ((uint32_t)(t1 - t0) == 0u || (uint32_t)(t1 - t0) >= 0x80000000u) return 68;

        /* Window cursor selection retained across WM_SETCURSOR and restored to arrow. */
        cursor_win_ctx cctx = {.status = 70};
        im_window win = {
            .title = L"Host cursor check", .width = 64, .height = 64,
            .user = &cctx, .paint = cursor_test_paint
        };
        im_win_host_init(&cctx.host, &win);
        if (!im_window_run(&win, true)) return 69;
        if (cctx.status != 0) return cctx.status;
    }

    return 0;
}
