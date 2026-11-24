#include "draw.h"

/* Corner geometry comes from the pinned WinUI source
   (dxaml/xcp/core/core/elements/geometry.cpp, CGeometryBuilder::ClampCornerRadii
   and CalculateRoundedCornersRectangle): outer/inner radii are offset by half the
   border thickness, and adjacent radii that exceed a shared edge are partitioned
   proportionally along that edge rather than clamped to half the shorter side.

   Corner coverage is inferred from measured WinUI 3 frames (probe-r4/r8/r16 at
   scales 1, 1.5 and 2, plus the button captures): Direct2D flattens each 90-degree
   cubic-bezier quarter-circle into N = 2^k inscribed chords by Wang's formula at
   D2D1_DEFAULT_FLATTENING_TOLERANCE = 0.25 px (N=2 for r<=3, N=4 for r<=11, N=8
   for r<=46, N=16 for r<=185). We integrate exact 16-row horizontal coverage over
   those chords so corners stay strictly 8-way symmetric without reproducing GPU
   per-primitive AA triangle asymmetry. */
enum { ROWS = 16 };

/* sin(pi * i / 32) in 0..65536 (16.16 fixed point) for i = 0..16. */
static const uint32_t SIN16[17] = {
    0, 6424, 12785, 19024, 25080, 30893, 36410, 41575,
    46341, 50660, 54491, 57798, 60547, 62714, 64277, 65220, 65536
};

static int64_t root(int64_t value) {
    if (value <= 0) return 0;
    int64_t result = 0, bit = (int64_t)1 << 62;
    while (bit > value) bit >>= 2;
    for (; bit; bit >>= 2) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
    }
    return result;
}

/* Coordinates may be any im_fixed, so their sums and differences are 64-bit. */
static int64_t smaller64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

static int64_t larger64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

/* The nearest value an im_fixed can hold. */
static im_fixed held(int64_t value) {
    return (im_fixed)larger64(INT32_MIN, smaller64(INT32_MAX, value));
}

typedef struct fitted_radii {
    int64_t tl_x, tl_y;
    int64_t tr_x, tr_y;
    int64_t br_x, br_y;
    int64_t bl_x, bl_y;
} fitted_radii;

static int64_t clamp_pair(int64_t a, int64_t b, int64_t edge) {
    int64_t total = a + b;
    if (total <= edge || total <= 0) return a;
    while (a > 0x7FFFFFFFLL || total > 0x7FFFFFFFLL) {
        a >>= 1;
        total >>= 1;
    }
    if (total <= 0) return edge / 2;
    return (int64_t)(((uint64_t)a * (uint64_t)edge) / (uint64_t)total);
}

static fitted_radii fit(im_rect shape, im_radii radii) {
    int64_t w = larger64(0, (int64_t)shape.right - shape.left);
    int64_t h = larger64(0, (int64_t)shape.bottom - shape.top);
    int64_t tl = larger64(0, radii.top_left);
    int64_t tr = larger64(0, radii.top_right);
    int64_t br = larger64(0, radii.bottom_right);
    int64_t bl = larger64(0, radii.bottom_left);
    fitted_radii out = {tl, tl, tr, tr, br, br, bl, bl};
    if (out.tl_x + out.tr_x > w) {
        out.tl_x = clamp_pair(out.tl_x, out.tr_x, w);
        out.tr_x = w - out.tl_x;
    }
    if (out.tr_y + out.br_y > h) {
        out.tr_y = clamp_pair(out.tr_y, out.br_y, h);
        out.br_y = h - out.tr_y;
    }
    if (out.bl_x + out.br_x > w) {
        out.bl_x = clamp_pair(out.bl_x, out.br_x, w);
        out.br_x = w - out.bl_x;
    }
    if (out.tl_y + out.bl_y > h) {
        out.tl_y = clamp_pair(out.tl_y, out.bl_y, h);
        out.bl_y = h - out.tl_y;
    }
    return out;
}

/* How far a corner pushes the edge in on this row, using D2D's chord flattening. */
static int64_t inset(int64_t rx, int64_t ry, int64_t distance) {
    if (rx <= 0 || ry <= 0 || distance >= ry) return 0;
    int64_t up = ry - distance;
    int64_t rmax = larger64(rx, ry);
    int step = 0;
    if (rmax <= 3 * IM_ONE) step = 8;                    /* N = 2 */
    else if (rmax <= 11 * IM_ONE) step = 4;              /* N = 4 */
    else if (rmax <= 46 * IM_ONE) step = 2;              /* N = 8 */
    else if (rmax <= 185 * IM_ONE) step = 1;             /* N = 16 */

    if (step > 0) {
        uint64_t u = (uint64_t)up, d = (uint64_t)ry;
        while (u > 0xFFFFFFFFULL || d > 0xFFFFFFFFULL) { u >>= 1; d >>= 1; }
        uint32_t v = d ? (uint32_t)((u * IM_ONE) / d) : IM_ONE;
        uint32_t cx = 0;
        for (int i = 0; i < 16; i += step) {
            uint32_t y0 = SIN16[i], y1 = SIN16[i + step];
            if (v <= y1) {
                uint32_t x0 = SIN16[16 - i], x1 = SIN16[16 - i - step];
                uint32_t dy = y1 - y0;
                uint32_t dx = x0 - x1;
                cx = dy ? x0 - (uint32_t)(((uint64_t)(v - y0) * dx + dy / 2) / dy) : x1;
                break;
            }
        }
        return (int64_t)(((uint64_t)rx * (uint64_t)(IM_ONE - cx) + IM_ONE / 2) >> 16);
    }

    /* Large radii (> 208 px): exact quarter-ellipse, sagitta < 0.25 px. */
    uint64_t u = (uint64_t)up, d = (uint64_t)ry;
    while (u > 0x7FFFFFFFULL || d > 0x7FFFFFFFULL) { u >>= 1; d >>= 1; }
    uint64_t v = d ? (u * IM_ONE) / d : IM_ONE;
    int64_t cx = root((int64_t)IM_ONE * IM_ONE - (int64_t)(v * v));
    return (int64_t)(((uint64_t)rx * (uint64_t)(IM_ONE - cx) + IM_ONE / 2) >> 16);
}

static bool extent(im_rect shape, const fitted_radii *radii, int64_t y, int64_t *left, int64_t *right) {
    if (y < shape.top || y >= shape.bottom) return false;
    int64_t above = y - shape.top, below = shape.bottom - y;
    *left = (int64_t)shape.left + larger64(inset(radii->tl_x, radii->tl_y, above), inset(radii->bl_x, radii->bl_y, below));
    *right = (int64_t)shape.right - larger64(inset(radii->tr_x, radii->tr_y, above), inset(radii->br_x, radii->br_y, below));
    return *left < *right;
}

/* The part of pixel column x between two edges, 0 to IM_ONE. */
static int64_t overlap(int x, int64_t left, int64_t right) {
    int64_t from = larger64(left, (int64_t)x * IM_ONE), to = smaller64(right, ((int64_t)x + 1) * IM_ONE);
    return to > from ? to - from : 0;
}

/* Source over, with the colour multiplied by its alpha and rounded to 8 bits
   first, which is how WinUI's surfaces are composed. */
static uint32_t blend(uint32_t below, uint32_t color, uint32_t coverage) {
    uint32_t alpha = (uint32_t)(((uint64_t)(color >> 24) * coverage + IM_ONE / 2) >> 16);
    uint32_t out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        uint32_t under = below >> shift & 255, over = color >> shift & 255;
        out |= ((((over * alpha + 127) / 255) * 255 + under * (255 - alpha) + 127) / 255) << shift;
    }
    return out;
}

/* The whole pixel at or above a coordinate, within 0 and the limit. */
static int pixel(int64_t value, int limit) {
    return (int)larger64(0, smaller64(limit, value >> 16));
}

static void shade(im_surface *surface, im_rect outer, im_radii outer_radii, const im_rect *hole, im_radii hole_radii, enum im_brush_id brush) {
    if (outer.left >= outer.right || outer.top >= outer.bottom) return;
    fitted_radii outer_fit = fit(outer, outer_radii);
    fitted_radii hole_fit = hole ? fit(*hole, hole_radii) : outer_fit;
    int top = pixel(outer.top, surface->height), bottom = pixel((int64_t)outer.bottom + IM_ONE - 1, surface->height);
    int from = pixel(outer.left, surface->width), to = pixel((int64_t)outer.right + IM_ONE - 1, surface->width);
    int64_t height = (int64_t)outer.bottom - outer.top;
    for (int y = top; y < bottom; y++) {
        int64_t left[ROWS], right[ROWS], hole_left[ROWS], hole_right[ROWS];
        for (int row = 0; row < ROWS; row++) {
            int64_t at = (int64_t)y * IM_ONE + row * (IM_ONE / ROWS) + IM_ONE / ROWS / 2;
            if (!extent(outer, &outer_fit, at, &left[row], &right[row])) left[row] = right[row] = 0;
            if (!hole || !extent(*hole, &hole_fit, at, &hole_left[row], &hole_right[row])) hole_left[row] = hole_right[row] = 0;
        }
        uint32_t color = im_brush_over(&surface->palette, brush, (int64_t)y * IM_ONE + IM_ONE / 2 - outer.top, height, surface->scale);
        if (!(color >> 24)) continue;
        uint32_t *line = surface->pixels + (size_t)y * (size_t)surface->stride;
        for (int x = from; x < to; x++) {
            int64_t covered = 0;
            for (int row = 0; row < ROWS; row++)
                covered += overlap(x, left[row], right[row]) - overlap(x, larger64(left[row], hole_left[row]), smaller64(right[row], hole_right[row]));
            if (covered > 0) line[x] = blend(line[x], color, (uint32_t)(covered / ROWS));
        }
    }
}

static im_fixed grown(im_fixed radius, im_fixed by) {
    return radius > 0 ? held(larger64(0, (int64_t)radius + by)) : 0;
}

im_rect im_inset(im_rect shape, im_fixed by) {
    return (im_rect){held((int64_t)shape.left + by), held((int64_t)shape.top + by), held((int64_t)shape.right - by), held((int64_t)shape.bottom - by)};
}

im_radii im_grow(im_radii radii, im_fixed by) {
    return (im_radii){grown(radii.top_left, by), grown(radii.top_right, by), grown(radii.bottom_right, by), grown(radii.bottom_left, by)};
}

void im_fill(im_surface *surface, im_rect shape, im_radii radii, enum im_brush_id brush) {
    shade(surface, shape, radii, NULL, radii, brush);
}

void im_stroke(im_surface *surface, im_rect shape, im_radii radii, im_fixed thickness, enum im_brush_id brush) {
    if (thickness <= 0) return;
    im_rect hole = im_inset(shape, thickness);
    shade(surface, shape, im_grow(radii, thickness / 2), &hole, im_grow(radii, -thickness / 2), brush);
}
