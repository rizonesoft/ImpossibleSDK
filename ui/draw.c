#include "draw.h"

/* Coverage is counted on a grid of 16 rows by 1/256 pixel columns: the x
   extent of the shape is exact on each row, so straight edges have no error. */
enum { ROWS = 16 };

static int64_t root(int64_t value) {
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

static im_fixed smaller(im_fixed a, im_fixed b) {
    return a < b ? a : b;
}

static im_fixed larger(im_fixed a, im_fixed b) {
    return a > b ? a : b;
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

static void fit(im_rect shape, im_radii *radii) {
    im_fixed most = held(larger64(0, smaller64((int64_t)shape.right - shape.left, (int64_t)shape.bottom - shape.top) / 2));
    im_fixed *each[] = {&radii->top_left, &radii->top_right, &radii->bottom_right, &radii->bottom_left};
    for (int i = 0; i < 4; i++) *each[i] = larger(0, smaller(*each[i], most));
}

/* How far a corner pushes the edge in on this row. */
static im_fixed inset(im_fixed radius, int64_t distance) {
    if (distance >= radius) return 0;
    int64_t up = radius - distance;
    return radius - (im_fixed)root((int64_t)radius * radius - up * up);
}

static bool extent(im_rect shape, const im_radii *radii, int64_t y, int64_t *left, int64_t *right) {
    if (y < shape.top || y >= shape.bottom) return false;
    int64_t above = y - shape.top, below = shape.bottom - y;
    *left = (int64_t)shape.left + larger(inset(radii->top_left, above), inset(radii->bottom_left, below));
    *right = (int64_t)shape.right - larger(inset(radii->top_right, above), inset(radii->bottom_right, below));
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
    fit(outer, &outer_radii);
    if (hole) fit(*hole, &hole_radii);
    int top = pixel(outer.top, surface->height), bottom = pixel((int64_t)outer.bottom + IM_ONE - 1, surface->height);
    int from = pixel(outer.left, surface->width), to = pixel((int64_t)outer.right + IM_ONE - 1, surface->width);
    int64_t height = (int64_t)outer.bottom - outer.top;
    for (int y = top; y < bottom; y++) {
        int64_t left[ROWS], right[ROWS], hole_left[ROWS], hole_right[ROWS];
        for (int row = 0; row < ROWS; row++) {
            int64_t at = (int64_t)y * IM_ONE + row * (IM_ONE / ROWS) + IM_ONE / ROWS / 2;
            if (!extent(outer, &outer_radii, at, &left[row], &right[row])) left[row] = right[row] = 0;
            if (!hole || !extent(*hole, &hole_radii, at, &hole_left[row], &hole_right[row])) hole_left[row] = hole_right[row] = 0;
        }
        uint32_t color = im_brush_at(&surface->palette, brush, (int64_t)y * IM_ONE + IM_ONE / 2 - outer.top, height, surface->scale);
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
