/* Draws every case of the reference program (16 button/accent cases and 10
   radius probes) and writes the raw BGRA pixels to standard output. Seven
   8-hex-digit ARGB accent shades may be passed on the command line (matching
   cases.json["accents"]); otherwise accents.h supplies the defaults. */
#include <impossible/base.h>
#include <windows.h>
#include "button.h"
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

int im_main(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t accents[7] = ACCENTS;
    if (parse_accents(accents) < 0) return 2;
    static const im_fixed pad = IM_ONE;
    for (int theme = 0; theme < 2; theme++)
        for (int style = 0; style < 2; style++)
            for (int state = 0; state < 4; state++) {
                enum { W = 160, H = 64 };
                im_surface surface = {pixels, W, H, W, {theme ? IM_LIGHT : IM_DARK, {0}}, IM_ONE};
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
        im_surface surface = {pixels, w, h, w, {IM_DARK, {0}}, scale};
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
