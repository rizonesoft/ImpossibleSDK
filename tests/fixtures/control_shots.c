/* Draws every case of the reference program (16 button/accent cases and 10
   radius probes) and writes the raw BGRA pixels to standard output. Seven
   8-hex-digit ARGB accent shades may be passed on the command line (matching
   cases.json["accents"]); otherwise accents.h supplies the defaults. */
#include <impossible/base.h>
#include <windows.h>
#include "button.h"
#include "layer.h"
#include "accents.h"

enum { MAX_W = 160, MAX_H = 128 };
static uint32_t pixels[MAX_W * MAX_H];

static int hex_digit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    return -1;
}

/* Returns 1 if 7 valid accent args were parsed, 0 if no args were passed, -1 if malformed. */
static int parse_accents(uint32_t out[7]) {
    const wchar_t *p = GetCommandLineW();
    if (!p) return 0;
    /* Skip executable name (quoted or unquoted). */
    while (*p == L' ' || *p == L'\t') p++;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') p++;
        if (*p == L'"') p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') p++;
    }
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return 0;
    uint32_t parsed[7];
    for (int i = 0; i < 7; i++) {
        while (*p == L' ' || *p == L'\t') p++;
        uint32_t value = 0;
        for (int j = 0; j < 8; j++) {
            int d = hex_digit(p[j]);
            if (d < 0) return -1;
            value = (value << 4) | (uint32_t)d;
        }
        p += 8;
        if (*p && *p != L' ' && *p != L'\t') return -1;
        parsed[i] = value;
    }
    while (*p == L' ' || *p == L'\t') p++;
    if (*p) return -1;
    for (int i = 0; i < 7; i++) out[i] = parsed[i];
    return 1;
}

static bool write_all(HANDLE out, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    while (size > 0) {
        DWORD chunk = size > 65536 ? 65536 : (DWORD)size;
        DWORD done = 0;
        if (!WriteFile(out, bytes, chunk, &done, NULL) || done == 0) return false;
        bytes += done;
        size -= done;
    }
    return true;
}

static uint8_t shot_arena[512 * 1024];
static size_t shot_arena_used = 0;

static void *sa_alloc(void *ctx, size_t size) {
    (void)ctx;
    size = (size + 7u) & ~(size_t)7u;
    if (shot_arena_used + size > sizeof(shot_arena)) return NULL;
    void *p = &shot_arena[shot_arena_used];
    shot_arena_used += size;
    return p;
}

static void *sa_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    void *n = sa_alloc(ctx, new_size);
    if (!n) return NULL;
    if (ptr && old_size > 0) {
        size_t c = old_size < new_size ? old_size : new_size;
        uint8_t *d = (uint8_t *)n;
        const uint8_t *s = (const uint8_t *)ptr;
        for (size_t i = 0; i < c; i++) d[i] = s[i];
    }
    return n;
}

static void sa_free(void *ctx, void *ptr, size_t size) {
    (void)ctx; (void)ptr; (void)size;
}

static int emit_clip_shots(HANDLE out) {
    static uint32_t big[192 * 192];
    static const struct { int num, den; } scales[4] = {{1, 1}, {3, 2}, {2, 1}, {3, 1}};
    im_allocator alloc = {sa_alloc, sa_realloc, sa_free, NULL};
    im_radii sq = {0, 0, 0, 0};

    /* 1. Oversized white fill clipped to rounded 48x48 r=8 centered in 64x64 at 100%, 150%, 200%, 300%. */
    for (int i = 0; i < 4; i++) {
        int w = 64 * scales[i].num / scales[i].den;
        int h = 64 * scales[i].num / scales[i].den;
        int bw = 48 * scales[i].num / scales[i].den;
        int bh = 48 * scales[i].num / scales[i].den;
        int left = (w - bw) / 2, top = (h - bh) / 2;
        im_fixed sc = (im_fixed)((int64_t)IM_ONE * scales[i].num / scales[i].den);
        im_fixed rad = (im_fixed)(8LL * sc);
        im_surface s = {big, w, h, w, {IM_DARK, {0}}, sc, NULL};
        for (int p = 0; p < w * h; p++) big[p] = 0;
        shot_arena_used = 0;
        im_layer_ctx ctx;
        if (!im_layer_init(&ctx, &s, alloc, sizeof(shot_arena))) return 1;
        if (!im_layer_push_clip_rounded(&ctx, (im_rect){left * IM_ONE, top * IM_ONE, (left + bw) * IM_ONE, (top + bh) * IM_ONE}, (im_radii){rad, rad, rad, rad})) return 1;
        im_fill_argb(&s, (im_rect){0, 0, w * IM_ONE, h * IM_ONE}, sq, 0xFFFFFFFF);
        im_layer_pop_clip(&ctx);
        im_layer_destroy(&ctx);
        if (!write_all(out, big, (size_t)w * (size_t)h * 4)) return 1;
    }

    /* 2. Clip + group-opacity scene at 100%, 150%, 200%, 300% (64x64 logical). */
    for (int i = 0; i < 4; i++) {
        int w = 64 * scales[i].num / scales[i].den;
        int h = 64 * scales[i].num / scales[i].den;
        im_fixed sc = (im_fixed)((int64_t)IM_ONE * scales[i].num / scales[i].den);
        im_fixed rad = (im_fixed)(8LL * sc);
        im_surface s = {big, w, h, w, {IM_DARK, {0}}, sc, NULL};
        for (int p = 0; p < w * h; p++) big[p] = 0x00202020;
        shot_arena_used = 0;
        im_layer_ctx ctx;
        if (!im_layer_init(&ctx, &s, alloc, sizeof(shot_arena))) return 1;
        im_rect clip_r = {(im_fixed)(12LL * sc), (im_fixed)(12LL * sc), (im_fixed)(52LL * sc), (im_fixed)(52LL * sc)};
        if (!im_layer_push_clip_rounded(&ctx, clip_r, (im_radii){rad, rad, rad, rad})) return 1;
        im_fill_argb(&s, (im_rect){0, 0, w * IM_ONE, h * IM_ONE}, sq, 0xFF183048);
        if (im_layer_push(&ctx, clip_r, 128u, false, sq) != IM_LAYER_OK) return 1;
        im_fill_argb(&s, (im_rect){(im_fixed)(16LL * sc), (im_fixed)(16LL * sc), (im_fixed)(40LL * sc), (im_fixed)(40LL * sc)}, sq, 0xFFFF0000);
        im_fill_argb(&s, (im_rect){(im_fixed)(24LL * sc), (im_fixed)(24LL * sc), (im_fixed)(48LL * sc), (im_fixed)(48LL * sc)}, sq, 0xFF0000FF);
        im_layer_pop(&ctx);
        im_layer_pop_clip(&ctx);
        im_layer_destroy(&ctx);
        if (!write_all(out, big, (size_t)w * (size_t)h * 4)) return 1;
    }

    /* 3. WinUI-matched rounded-clip + 0.5 opacity scene (oversized left-red + right-blue halves clipped by r=8 Border)
          at 100%, 150%, 200%, 300%, compared pixel-by-pixel against live WinUI in tests/test_live.py. */
    for (int i = 0; i < 4; i++) {
        int w = 64 * scales[i].num / scales[i].den;
        int h = 64 * scales[i].num / scales[i].den;
        im_fixed sc = (im_fixed)((int64_t)IM_ONE * scales[i].num / scales[i].den);
        im_fixed rad = (im_fixed)(8LL * sc);
        im_surface s = {big, w, h, w, {IM_DARK, {0}}, sc, NULL};
        for (int p = 0; p < w * h; p++) big[p] = 0x00202020;
        shot_arena_used = 0;
        im_layer_ctx ctx;
        if (!im_layer_init(&ctx, &s, alloc, sizeof(shot_arena))) return 1;
        im_rect clip_r = {(im_fixed)(12LL * sc), (im_fixed)(12LL * sc), (im_fixed)(52LL * sc), (im_fixed)(52LL * sc)};
        if (!im_layer_push_clip_rounded(&ctx, clip_r, (im_radii){rad, rad, rad, rad})) return 1;
        im_fill_argb(&s, (im_rect){0, 0, w * IM_ONE, h * IM_ONE}, sq, 0xFF183048);
        if (im_layer_push(&ctx, clip_r, 128u, false, sq) != IM_LAYER_OK) return 1;
        im_fill_argb(&s, (im_rect){(im_fixed)(16LL * sc), (im_fixed)(16LL * sc), (im_fixed)(32LL * sc), (im_fixed)(48LL * sc)}, sq, 0xFFFF0000);
        im_fill_argb(&s, (im_rect){(im_fixed)(32LL * sc), (im_fixed)(16LL * sc), (im_fixed)(48LL * sc), (im_fixed)(48LL * sc)}, sq, 0xFF0000FF);
        im_layer_pop(&ctx);
        im_layer_pop_clip(&ctx);
        im_layer_destroy(&ctx);
        if (!write_all(out, big, (size_t)w * (size_t)h * 4)) return 1;
    }
    return 0;
}

int im_main(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (im_has_argument(L"--clip-shots")) return emit_clip_shots(out);
    uint32_t accents[7] = ACCENTS;
    if (parse_accents(accents) < 0) return 2;
    static const im_fixed pad = IM_ONE;
    for (int theme = 0; theme < 2; theme++)
        for (int style = 0; style < 2; style++)
            for (int state = 0; state < 4; state++) {
                enum { W = 160, H = 64 };
                im_surface surface = {pixels, W, H, W, {theme ? IM_LIGHT : IM_DARK, {0}}, IM_ONE, NULL};
                for (int k = 0; k < 7; k++) surface.palette.slot[k] = accents[k];
                for (int i = 0; i < W * H; i++) pixels[i] = theme ? 0xF3F3F3 : 0x202020;
                im_draw_button(&surface, (im_rect){20 * pad, 16 * pad, 140 * pad, 48 * pad}, (im_button_style)style, (im_button_state)state);
                if (!write_all(out, pixels, (size_t)W * H * 4)) return 1;
            }

    static const struct {
        int logical_w, logical_h, box_w, box_h;
        int radii[4];
        int scale_num, scale_den;
    } probes[] = {
        {64, 64, 48, 48, {4, 4, 4, 4}, 1, 1},
        {64, 64, 48, 48, {4, 4, 4, 4}, 3, 2},
        {64, 64, 48, 48, {4, 4, 4, 4}, 2, 1},
        {64, 64, 48, 48, {8, 8, 8, 8}, 1, 1},
        {64, 64, 48, 48, {8, 8, 8, 8}, 3, 2},
        {64, 64, 48, 48, {8, 8, 8, 8}, 2, 1},
        {64, 64, 48, 48, {16, 16, 16, 16}, 1, 1},
        {64, 64, 48, 48, {16, 16, 16, 16}, 3, 2},
        {64, 64, 48, 48, {16, 16, 16, 16}, 2, 1},
        {64, 64, 40, 20, {16, 0, 0, 0}, 1, 1},
    };
    const enum im_brush_id white = IM_BRUSH_TextFillColorPrimaryBrush;
    for (size_t idx = 0; idx < sizeof probes / sizeof probes[0]; idx++) {
        int w = probes[idx].logical_w * probes[idx].scale_num / probes[idx].scale_den;
        int h = probes[idx].logical_h * probes[idx].scale_num / probes[idx].scale_den;
        int bw = probes[idx].box_w * probes[idx].scale_num / probes[idx].scale_den;
        int bh = probes[idx].box_h * probes[idx].scale_num / probes[idx].scale_den;
        int left = (w - bw) / 2;
        int top = (h - bh) / 2;
        im_fixed scale = (im_fixed)((int64_t)IM_ONE * probes[idx].scale_num / probes[idx].scale_den);
        im_surface surface = {pixels, w, h, w, {IM_DARK, {0}}, scale, NULL};
        for (int i = 0; i < w * h; i++) pixels[i] = 0;
        im_radii radii = {
            (im_fixed)((int64_t)probes[idx].radii[0] * scale),
            (im_fixed)((int64_t)probes[idx].radii[1] * scale),
            (im_fixed)((int64_t)probes[idx].radii[2] * scale),
            (im_fixed)((int64_t)probes[idx].radii[3] * scale),
        };
        im_fill(&surface, (im_rect){left * IM_ONE, top * IM_ONE, (left + bw) * IM_ONE, (top + bh) * IM_ONE}, radii, white);
        if (!write_all(out, pixels, (size_t)w * (size_t)h * 4)) return 1;
    }
    return 0;
}
