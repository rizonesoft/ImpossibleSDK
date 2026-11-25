#include "draw.h"
#include "layer.h"

/* Corner geometry comes from the pinned WinUI source
   (dxaml/xcp/core/core/elements/geometry.cpp, CGeometryBuilder::ClampCornerRadii
   and CalculateRoundedCornersRectangle): outer/inner radii are offset by half the
   border thickness, and adjacent radii that exceed a shared edge are partitioned
   proportionally along that edge rather than clamped to half the shorter side.

   Corner coverage is inferred from measured WinUI 3 frames (probe-r4/r8/r16 at
   scales 1, 1.5 and 2, plus the button captures): each 90-degree corner arc is
   approximated as a standard quarter-ellipse cubic Bezier (kappa = 4*(sqrt(2)-1)/3)
   and flattened into N = 2^k chords by Wang's formula at Direct2D's default
   flattening tolerance of 0.25 px (N^2 >= 3 * M / 1 px, where M is the maximum
   second difference of the cubic control polygon). We integrate 16-row horizontal
   coverage over those flattened chords, preserving 4-way (left-right and top-bottom
   reflection) symmetry without reproducing GPU per-primitive AA triangle asymmetry
   across corners. */
enum { ROWS = 16 };

/* Quarter-circle cubic Bezier handle length: round(4*(sqrt(2)-1)/3 * 65536). */
enum { BEZ_KAPPA = 36195 };
/* Control-polygon second-difference weights: (1 - kappa) and (2*kappa - 1) in Q16. */
enum { WANG_A = 29341, WANG_B = 6853 };

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

/* Normalized cubic Bezier y(t) for quarter-circle from (1,0) to (0,1), t in [0..IM_ONE]. */
static uint32_t bezier_coord(uint32_t t) {
    uint64_t mt = (uint64_t)IM_ONE - t;
    uint64_t t2 = ((uint64_t)t * t + IM_ONE / 2) >> 16;
    uint64_t mt2 = (mt * mt + IM_ONE / 2) >> 16;
    uint64_t c1 = (3 * mt2 * t + IM_ONE / 2) >> 16;
    uint64_t y1 = (c1 * BEZ_KAPPA + IM_ONE / 2) >> 16;
    uint64_t y2 = (3 * mt * t2 + IM_ONE / 2) >> 16;
    uint64_t y3 = (t2 * t + IM_ONE / 2) >> 16;
    uint64_t sum = y1 + y2 + y3;
    return sum > IM_ONE ? IM_ONE : (uint32_t)sum;
}

/* Power-of-two chord count N from Wang's formula for a quarter-ellipse cubic at tol = 0.25 px. */
static uint32_t wang_segments(int64_t rx, int64_t ry) {
    int shift = 0;
    uint64_t ux = (uint64_t)rx, uy = (uint64_t)ry;
    while (ux > 0x3FFFFFFFULL || uy > 0x3FFFFFFFULL) {
        ux >>= 1;
        uy >>= 1;
        shift++;
    }
    int64_t ax = (int64_t)((ux * WANG_A) >> 16);
    int64_t ay = (int64_t)((uy * WANG_B) >> 16);
    int64_t bx = (int64_t)((ux * WANG_B) >> 16);
    int64_t by = (int64_t)((uy * WANG_A) >> 16);
    int64_t m1 = root(ax * ax + ay * ay);
    int64_t m2 = root(bx * bx + by * by);
    uint64_t m = (uint64_t)larger64(m1, m2);
    /* Wang's formula at tol = 0.25 px (IM_ONE / 4): N^2 >= (3/4) * M / 0.25 = 3 * M / IM_ONE. */
    uint64_t need = 3 * m;
    if (shift > 0) need = (shift >= 40) ? (1ULL << 40) : (need << shift);
    uint32_t n = 2;
    while (n < 1024 && (uint64_t)n * n * IM_ONE < need) n <<= 1;
    return n;
}

/* How far a corner pushes the edge in on this row, using Wang-formula cubic Bezier flattening. */
static int64_t inset(int64_t rx, int64_t ry, int64_t distance) {
    if (rx <= 0 || ry <= 0 || distance >= ry) return 0;
    int64_t up = ry - distance;
    uint64_t u = (uint64_t)up, d = (uint64_t)ry;
    while (u > 0xFFFFFFFFULL || d > 0xFFFFFFFFULL) { u >>= 1; d >>= 1; }
    uint32_t v = d ? (uint32_t)((u * IM_ONE) / d) : IM_ONE;
    if (v > IM_ONE) v = IM_ONE;

    uint32_t n = wang_segments(rx, ry);
    uint32_t lo = 0, hi = n;
    while (lo + 1 < hi) {
        uint32_t mid = (lo + hi) >> 1;
        uint32_t t = (uint32_t)(((uint64_t)mid * IM_ONE) / n);
        if (bezier_coord(t) <= v) lo = mid;
        else hi = mid;
    }
    uint32_t t0 = (uint32_t)(((uint64_t)lo * IM_ONE) / n);
    uint32_t t1 = (uint32_t)(((uint64_t)(lo + 1) * IM_ONE) / n);
    uint32_t y0 = bezier_coord(t0), y1 = bezier_coord(t1);
    uint32_t x0 = bezier_coord(IM_ONE - t0), x1 = bezier_coord(IM_ONE - t1);
    uint32_t dy = y1 > y0 ? y1 - y0 : 0;
    uint32_t dx = x0 > x1 ? x0 - x1 : 0;
    uint32_t cx = dy ? x0 - (uint32_t)(((uint64_t)(v > y0 ? v - y0 : 0) * dx + dy / 2) / dy) : x1;
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

static void shade_color(im_surface *surface, im_rect outer, im_radii outer_radii, const im_rect *hole, im_radii hole_radii, enum im_brush_id brush, bool use_argb, uint32_t solid_argb) {
    if (outer.left >= outer.right || outer.top >= outer.bottom) return;
    fitted_radii outer_fit = fit(outer, outer_radii);
    fitted_radii hole_fit = hole ? fit(*hole, hole_radii) : outer_fit;
    int bound_l = 0, bound_t = 0, bound_r = surface->width, bound_b = surface->height;
    if (surface->layers) {
        im_layer_draw_bounds(surface->layers, &bound_l, &bound_t, &bound_r, &bound_b);
        if (bound_l >= bound_r || bound_t >= bound_b) return;
    }
    int top = pixel(outer.top, surface->height), bottom = pixel((int64_t)outer.bottom + IM_ONE - 1, surface->height);
    int from = pixel(outer.left, surface->width), to = pixel((int64_t)outer.right + IM_ONE - 1, surface->width);
    if (top < bound_t) top = bound_t;
    if (bottom > bound_b) bottom = bound_b;
    if (from < bound_l) from = bound_l;
    if (to > bound_r) to = bound_r;
    if (top >= bottom || from >= to) return;

    int64_t height = (int64_t)outer.bottom - outer.top;
    for (int y = top; y < bottom; y++) {
        int64_t left[ROWS], right[ROWS], hole_left[ROWS], hole_right[ROWS];
        for (int row = 0; row < ROWS; row++) {
            int64_t at = (int64_t)y * IM_ONE + row * (IM_ONE / ROWS) + IM_ONE / ROWS / 2;
            if (!extent(outer, &outer_fit, at, &left[row], &right[row])) left[row] = right[row] = 0;
            if (!hole || !extent(*hole, &hole_fit, at, &hole_left[row], &hole_right[row])) hole_left[row] = hole_right[row] = 0;
        }
        uint32_t color = use_argb ? solid_argb : im_brush_over(&surface->palette, brush, (int64_t)y * IM_ONE + IM_ONE / 2 - outer.top, height, surface->scale);
        if (!(color >> 24)) continue;
        if (surface->layers) {
            for (int x = from; x < to; x++) {
                int64_t covered = 0;
                for (int row = 0; row < ROWS; row++)
                    covered += overlap(x, left[row], right[row]) - overlap(x, larger64(left[row], hole_left[row]), smaller64(right[row], hole_right[row]));
                if (covered > 0) im_layer_blend_pixel(surface->layers, x, y, color, (uint32_t)(covered / ROWS));
            }
        } else {
            uint32_t *line = surface->pixels + (size_t)y * (size_t)surface->stride;
            for (int x = from; x < to; x++) {
                int64_t covered = 0;
                for (int row = 0; row < ROWS; row++)
                    covered += overlap(x, left[row], right[row]) - overlap(x, larger64(left[row], hole_left[row]), smaller64(right[row], hole_right[row]));
                if (covered > 0) line[x] = blend(line[x], color, (uint32_t)(covered / ROWS));
            }
        }
    }
}

uint32_t im_shape_coverage(im_rect shape, im_radii radii, int x, int y) {
    if (shape.left >= shape.right || shape.top >= shape.bottom) return 0;
    int64_t px0 = (int64_t)x * IM_ONE, px1 = px0 + IM_ONE;
    int64_t py0 = (int64_t)y * IM_ONE, py1 = py0 + IM_ONE;
    if (px1 <= shape.left || px0 >= shape.right || py1 <= shape.top || py0 >= shape.bottom) return 0;
    fitted_radii f = fit(shape, radii);
    int64_t covered = 0;
    for (int row = 0; row < ROWS; row++) {
        int64_t at = py0 + row * (IM_ONE / ROWS) + IM_ONE / ROWS / 2;
        int64_t l = 0, r = 0;
        if (extent(shape, &f, at, &l, &r)) covered += overlap(x, l, r);
    }
    return (uint32_t)(covered / ROWS);
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
    shade_color(surface, shape, radii, NULL, radii, brush, false, 0);
}

void im_fill_argb(im_surface *surface, im_rect shape, im_radii radii, uint32_t argb) {
    shade_color(surface, shape, radii, NULL, radii, (enum im_brush_id)0, true, argb);
}

void im_stroke(im_surface *surface, im_rect shape, im_radii radii, im_fixed thickness, enum im_brush_id brush) {
    if (thickness <= 0) return;
    im_rect hole = im_inset(shape, thickness);
    shade_color(surface, shape, im_grow(radii, thickness / 2), &hole, im_grow(radii, -thickness / 2), brush, false, 0);
}
