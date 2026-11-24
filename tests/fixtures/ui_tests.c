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

    clear(); /* oversized radii clamp to fit; a D2D-flattened N=8 (32-gon) circle of radius 16 has area 799.09 */
    im_fill(&surface, box(10, 10, 42, 42), (im_radii){99 * IM_ONE, 99 * IM_ONE, 99 * IM_ONE, 99 * IM_ONE}, white);
    if (at(10, 10) != 0 || (at(25, 10) & 255) < 230 || (at(10, 25) & 255) < 230 || at(25, 11) != 0xFFFFFF) return 6;
    uint32_t sum = 0;
    for (int i = 0; i < W * H; i++) sum += pixels[i] & 255;
    if (sum < 798 * 255 || sum > 800 * 255) return 6;

    clear();
    im_stroke(&surface, box(10, 10, 42, 42), round, IM_ONE, white);
    if (at(20, 10) != 0xFFFFFF || at(20, 11) || at(10, 20) != 0xFFFFFF || at(41, 20) != 0xFFFFFF || at(11, 20)) return 7;

    clear(); /* nothing outside the buffer is touched */
    im_fill(&surface, box(-100, -100, 200, 200), round, white);
    im_fill(&surface, box(200, 200, 300, 300), round, white);
    im_fill(&surface, box(20, 20, 10, 10), round, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 8;

    if (im_brush_over(&surface.palette, IM_BRUSH_ButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0x0FFFFFFF) return 9;
    surface.palette.theme = IM_LIGHT;
    if (im_brush_over(&surface.palette, IM_BRUSH_ButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0xB3FFFFFF) return 10;
    /* The light border is darker at the bottom, the dark one at the top. */
    uint32_t top = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE / 2, 32 * IM_ONE, IM_ONE);
    uint32_t low = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, 32 * IM_ONE - IM_ONE / 2, 32 * IM_ONE, IM_ONE);
    if (top >> 24 != 0x0F || low >> 24 != 0x29) return 11;

    surface.palette.slot[IM_SLOT_ACCENT_COLOR_DARK_1] = 0xFF005FB8;
    if (im_brush_over(&surface.palette, IM_BRUSH_AccentButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0xFF005FB8) return 12;
    if (im_brush_over(&surface.palette, IM_BRUSH_AccentButtonBackgroundPressed, 0, 32 * IM_ONE, IM_ONE) != 0xCC005FB8) return 13;
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
    uint32_t high = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE / 2, (int64_t)32768 * IM_ONE, IM_ONE);
    if (high >> 24 != 0x0F) return 19;
    clear();
    im_fill(&surface, (im_rect){0, 0, 8 * IM_ONE, 3 * IM_ONE + IM_ONE / 4}, square, IM_BRUSH_ButtonBorderBrush);
    uint32_t exact = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE + IM_ONE / 2, 3 * IM_ONE + IM_ONE / 4, IM_ONE);
    uint32_t white_by = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE + IM_ONE / 2, 3 * IM_ONE, IM_ONE);
    for (int i = 0; i < W * H; i++) pixels[i] = 0xFFFFFF;
    im_fill(&surface, (im_rect){0, 0, 8 * IM_ONE, 3 * IM_ONE + IM_ONE / 4}, square, IM_BRUSH_ButtonBorderBrush);
    uint32_t alpha = exact >> 24;
    if (alpha == white_by >> 24) return 20; /* the two heights give different opacities here */
    if ((at(2, 1) & 255) != (255 * (255 - alpha) + 127) / 255) return 21;

    /* The subtle button's background ends inside its border (InnerBorderEdge), and its corner is rounded. */
    surface.palette.theme = IM_DARK;
    clear();
    im_draw_button(&surface, box(0, 0, 60, 32), IM_BUTTON_SUBTLE, IM_HOVER);
    if (at(30, 0) != 0x0F0F0F || at(30, 16) != 0x0F0F0F) return 22;
    if (at(0, 0) != 0 || at(2, 0) == 0 || at(2, 0) == 0x0F0F0F) return 23;

    /* Asymmetric corner fitting matches CGeometryBuilder::CalculateRoundedCornersRectangle:
       a 40x20 box with top-left radius 16 and three square corners keeps radius 16 (not clamped to 10). */
    clear();
    im_fill(&surface, box(12, 22, 52, 42), (im_radii){16 * IM_ONE, 0, 0, 0}, white);
    if (at(12, 22) != 0 || at(12, 30) != 0) return 24; /* outside the r=16 corner; would be lit if clamped to 10 */
    if (at(12, 38) != 0xFFFFFF || at(51, 22) != 0xFFFFFF || at(51, 41) != 0xFFFFFF || at(12, 41) != 0xFFFFFF) return 25;

    /* Competing unequal radii: 40x20 with top_left=16, bottom_left=12 splits the 20px left edge
       proportionally (tl_y = 16*20/28 = 11.43, bl_y = 8.57) while keeping tl_x=16, bl_x=12. */
    clear();
    im_fill(&surface, box(10, 20, 50, 40), (im_radii){16 * IM_ONE, 0, 0, 12 * IM_ONE}, white);
    /* Left edge meets at y = 20 + 11.43 = 31.43: pixel (10, 31) is near the tangent, (10, 20) is empty,
       and along the top edge (25, 20) is still partial because tl_x remains 16 (not clamped to 11.43). */
    if (at(10, 20) != 0 || at(10, 39) != 0) return 26;
    if ((at(10, 31) & 255) < 200) return 27;
    if (at(25, 20) == 0xFFFFFF) return 28; /* tl_x = 16 leaves x=25 partial; clamping to 11.43 would fill it */
    if (at(26, 20) != 0xFFFFFF || at(22, 39) != 0xFFFFFF) return 29;
    /* Exercise im_stroke with competing unequal radii: outer radii are {17,0,0,13} on 40x20
       (tl_y = 17*20/30 = 11.33, tl_x = 17 unclamped) and inner radii are {15,0,0,11} on 36x16. */
    clear();
    im_stroke(&surface, box(10, 20, 50, 40), (im_radii){16 * IM_ONE, 0, 0, 12 * IM_ONE}, 2 * IM_ONE, white);
    if (at(10, 20) != 0 || at(30, 20) != 0xFFFFFF || at(30, 22) != 0 || at(49, 30) != 0xFFFFFF) return 30;
    /* Outer arc at x=25, y=20 is partial because outer tl_x=17 > 15 (half-short-side clamping to 10 would make it 0xFFFFFF). */
    if (at(25, 20) == 0 || at(25, 20) == 0xFFFFFF) return 31;
    /* Inner contour at x=25, y=22 is outside the inner arc (partial/lit); half-short-side clamping would leave it empty. */
    if (at(25, 22) == 0) return 32;

    /* Large radius (> 185 px) uses Wang's formula cubic flattening (N = 32 for r = 256) without overflow. */
    clear();
    im_fill(&surface, box(0, 0, 600, 600), (im_radii){256 * IM_ONE, 0, 0, 0}, white);
    if (at(0, 0) != 0 || at(63, 63) != 0) return 33; /* top-left 64x64 viewport lies outside the r=256 corner */
    clear();
    im_fill(&surface, box(-200, 0, 400, 600), (im_radii){256 * IM_ONE, 0, 0, 0}, white);
    if (at(63, 63) != 0xFFFFFF || at(0, 0) != 0) return 34; /* spans across the r=256 flattened cubic */

    /* Cubic Bezier subdivision at r=8 (N=4, t=0.25 vertex at (0.92142, 0.38925)*8) gives 37 at (8,12)
       and 126 at (8,13), distinct from equal-angle chords (40, 128) and exact circle (49, 151). */
    clear();
    im_fill(&surface, box(8, 8, 56, 56), (im_radii){8 * IM_ONE, 8 * IM_ONE, 8 * IM_ONE, 8 * IM_ONE}, white);
    if ((at(8, 12) & 255) != 37 || (at(8, 13) & 255) != 126) return 35;
    return 0;
}
