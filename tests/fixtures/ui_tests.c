/* Exits 0, or with the number of the first check that failed. */
#include <impossible/base.h>
#include "button.h"

enum { W = 64, H = 64 };
static uint32_t pixels[W * H];
static im_surface surface = {pixels, W, H, W, {IM_DARK, {0}}, IM_ONE};

static void clear(void) {
    for (int i = 0; i < W * H; i++) pixels[i] = 0;
}

static im_rect box(int l, int t, int r, int b) {
    return (im_rect){l * IM_ONE, t * IM_ONE, r * IM_ONE, b * IM_ONE};
}

static uint32_t at(int x, int y) {
    return pixels[y * W + x];
}

int im_main(void) {
    const im_radii square = {0}, round = {4 * IM_ONE, 4 * IM_ONE, 4 * IM_ONE, 4 * IM_ONE};
    const enum im_brush_id white = IM_BRUSH_TextFillColorPrimaryBrush; /* #FFFFFF in the dark theme */

    clear();
    im_fill(&surface, box(10, 10, 20, 20), square, white);
    if (at(9, 15) || at(20, 15) || at(15, 9) || at(15, 20)) return 1;
    if (at(10, 10) != 0xFFFFFF || at(19, 19) != 0xFFFFFF) return 2;

    clear();
    im_fill(&surface, (im_rect){10 * IM_ONE + IM_ONE / 2, 10 * IM_ONE, 20 * IM_ONE, 20 * IM_ONE}, square, white);
    if (at(10, 15) != 0x808080) return 3; /* half covered */

    clear();
    im_fill(&surface, box(10, 10, 42, 42), round, white);
    if (at(10, 10) || at(41, 41) || at(14, 10) != 0xFFFFFF || at(26, 26) != 0xFFFFFF) return 4;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (at(10 + x, 10 + y) != at(41 - x, 10 + y) || at(10 + x, 10 + y) != at(10 + x, 41 - y)) return 5;

    clear(); /* the area of a circle of radius 16 is 804.25 */
    im_fill(&surface, box(10, 10, 42, 42), (im_radii){99 * IM_ONE, 99 * IM_ONE, 99 * IM_ONE, 99 * IM_ONE}, white);
    uint32_t sum = 0;
    for (int i = 0; i < W * H; i++) sum += pixels[i] & 255;
    if (sum < 804 * 255 || sum > 805 * 255) return 6;

    clear();
    im_stroke(&surface, box(10, 10, 42, 42), round, IM_ONE, white);
    if (at(20, 10) != 0xFFFFFF || at(20, 11) || at(10, 20) != 0xFFFFFF || at(41, 20) != 0xFFFFFF || at(11, 20)) return 7;

    clear(); /* nothing outside the buffer is touched */
    im_fill(&surface, box(-100, -100, 200, 200), round, white);
    im_fill(&surface, box(200, 200, 300, 300), round, white);
    im_fill(&surface, box(20, 20, 10, 10), round, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 8;

    if (im_brush_at(&surface.palette, IM_BRUSH_ButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0x0FFFFFFF) return 9;
    surface.palette.theme = IM_LIGHT;
    if (im_brush_at(&surface.palette, IM_BRUSH_ButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0xB3FFFFFF) return 10;
    /* The light border is darker at the bottom, the dark one at the top. */
    uint32_t top = im_brush_at(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE / 2, 32 * IM_ONE, IM_ONE);
    uint32_t low = im_brush_at(&surface.palette, IM_BRUSH_ButtonBorderBrush, 32 * IM_ONE - IM_ONE / 2, 32 * IM_ONE, IM_ONE);
    if (top >> 24 != 0x0F || low >> 24 != 0x29) return 11;

    surface.palette.slot[IM_SLOT_ACCENT_COLOR_DARK_1] = 0xFF005FB8;
    if (im_brush_at(&surface.palette, IM_BRUSH_AccentButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0xFF005FB8) return 12;
    if (im_brush_at(&surface.palette, IM_BRUSH_AccentButtonBackgroundPressed, 0, 32 * IM_ONE, IM_ONE) != 0xCC005FB8) return 13;
    const im_fixed *padding = im_value(&surface.palette, IM_VALUE_ButtonPadding);
    if (padding[0] != 11 * IM_ONE || padding[1] != 5 * IM_ONE || padding[2] != 11 * IM_ONE || padding[3] != 6 * IM_ONE) return 14;

    /* Coordinates at the limits of an im_fixed fill the buffer, and nothing wraps. */
    surface.palette.theme = IM_DARK;
    clear();
    im_fill(&surface, (im_rect){-20000 * IM_ONE, -20000 * IM_ONE, 20000 * IM_ONE, 20000 * IM_ONE}, square, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 15;
    clear();
    im_fill(&surface, (im_rect){0, 0, INT32_MAX, INT32_MAX}, square, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 16;
    clear();
    im_fill(&surface, (im_rect){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX}, (im_radii){INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}, white);
    if (at(32, 32) != 0xFFFFFF) return 17;
    clear();
    im_stroke(&surface, (im_rect){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX}, round, 40 * IM_ONE, white);
    if (at(32, 32) != 0) return 18;

    /* A gradient runs over the shape's exact height, however high it is. */
    surface.palette.theme = IM_LIGHT;
    uint32_t high = im_brush_at(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE / 2, (int64_t)32768 * IM_ONE, IM_ONE);
    if (high >> 24 != 0x0F) return 19;
    clear();
    im_fill(&surface, (im_rect){0, 0, 8 * IM_ONE, 3 * IM_ONE + IM_ONE / 4}, square, IM_BRUSH_ButtonBorderBrush);
    uint32_t exact = im_brush_at(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE + IM_ONE / 2, 3 * IM_ONE + IM_ONE / 4, IM_ONE);
    uint32_t white_by = im_brush_at(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE + IM_ONE / 2, 3 * IM_ONE, IM_ONE);
    for (int i = 0; i < W * H; i++) pixels[i] = 0xFFFFFF;
    im_fill(&surface, (im_rect){0, 0, 8 * IM_ONE, 3 * IM_ONE + IM_ONE / 4}, square, IM_BRUSH_ButtonBorderBrush);
    uint32_t alpha = exact >> 24;
    if (alpha == white_by >> 24) return 20; /* the two heights give different opacities here */
    if ((at(2, 1) & 255) != (255 * (255 - alpha) + 127) / 255) return 21;

    /* The subtle button's background ends inside its border (InnerBorderEdge). */
    surface.palette.theme = IM_DARK;
    clear();
    im_draw_button(&surface, box(0, 0, 60, 32), IM_BUTTON_SUBTLE, IM_HOVER);
    if (at(30, 0) != 0x0F0F0F || at(30, 16) != 0x0F0F0F) return 22;
    return 0;
}
