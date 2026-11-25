/* Impossible UI: fixed-point vector paths, arcs, strokes and analytic fill rasterization. Portable. */
#include "path.h"
#include "layer.h"

/* ---------- Freestanding integer math helpers ---------- */

static uint64_t isqrt64(uint64_t n) {
    uint64_t x = 0, bit = (uint64_t)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= x + bit) {
            n -= x + bit;
            x = (x >> 1) + bit;
        } else {
            x >>= 1;
        }
        bit >>= 2;
    }
    return x;
}

static uint64_t isqrt128(unsigned __int128 n) {
    if (n <= UINT64_MAX) return isqrt64((uint64_t)n);
    unsigned __int128 x = 0;
    unsigned __int128 bit = ((unsigned __int128)1) << 126;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= x + bit) {
            n -= x + bit;
            x = (x >> 1) + bit;
        } else {
            x >>= 1;
        }
        bit >>= 2;
    }
    return (uint64_t)x;
}

static int64_t i64_abs(int64_t v) {
    return (v < 0) ? -v : v;
}

/* Compute atan2(dy, dx) in 16.16 degrees in [-180*IM_ONE, 180*IM_ONE] via exact monotone bisection. */
static im_fixed atan2_deg_fx(int64_t dy, int64_t dx) {
    if (dx == 0 && dy == 0) return 0;
    if (dx == 0) return (dy > 0) ? (90 * IM_ONE) : (-90 * IM_ONE);
    if (dy == 0) return (dx > 0) ? 0 : (180 * IM_ONE);

    int64_t ax = i64_abs(dx);
    int64_t ay = i64_abs(dy);
    /* Normalize (ax, ay) so max <= 2^30 to prevent 128-bit overflow during cross-product comparison. */
    while (ax > IM_TR_ONE || ay > IM_TR_ONE) {
        ax >>= 1;
        ay >>= 1;
    }
    int32_t lo = 0, hi = 90 * IM_ONE;
    for (int i = 0; i < 24; i++) {
        int32_t mid = lo + ((hi - lo) >> 1);
        int64_t s = im_sin_deg(mid);
        int64_t c = im_cos_deg(mid);
        /* Compare ay/ax vs s/c -> ay*c vs ax*s */
        __int128 lhs = (__int128)ay * c;
        __int128 rhs = (__int128)ax * s;
        if (lhs > rhs) lo = mid;
        else hi = mid;
    }
    im_fixed ang = (im_fixed)((lo + hi) >> 1);
    if (dx > 0 && dy >= 0) return ang;
    if (dx < 0 && dy >= 0) return (180 * IM_ONE) - ang;
    if (dx < 0 && dy < 0) return -(180 * IM_ONE) + ang;
    return -ang;
}

static int64_t q30_mul(int64_t a, int64_t b) {
    __int128 p = (__int128)a * b;
    __int128 half = (__int128)1 << (IM_TR_SHIFT - 1);
    if (p >= 0) return (int64_t)((p + half) >> IM_TR_SHIFT);
    return -(int64_t)(((-p) + half) >> IM_TR_SHIFT);
}

static int64_t hypot_q30(int64_t dx, int64_t dy) {
    uint64_t ax = (uint64_t)i64_abs(dx);
    uint64_t ay = (uint64_t)i64_abs(dy);
    if (ax == 0) return (int64_t)ay;
    if (ay == 0) return (int64_t)ax;
    int shift = 0;
    while (ax > ((uint64_t)1 << 60) || ay > ((uint64_t)1 << 60)) {
        ax >>= 1;
        ay >>= 1;
        shift++;
    }
    unsigned __int128 sq = (unsigned __int128)ax * ax + (unsigned __int128)ay * ay;
    uint64_t r = isqrt128(sq);
    return (int64_t)(r << shift);
}

/* Blend straight 0xAARRGGBB with coverage (0..IM_ONE) onto an opaque 0x00RRGGBB pixel. */
static uint32_t blend_direct(uint32_t bg, uint32_t argb, uint32_t coverage) {
    uint32_t a = (argb >> 24) & 0xFFu;
    uint32_t eff = (a * coverage + (IM_ONE / 2)) >> 16;
    if (eff == 0u) return bg;
    if (eff >= 255u) return argb & 0x00FFFFFFu;
    uint32_t inv = 255u - eff;
    uint32_t sr = (argb >> 16) & 0xFFu, sg = (argb >> 8) & 0xFFu, sb = argb & 0xFFu;
    uint32_t dr = (bg >> 16) & 0xFFu, dg = (bg >> 8) & 0xFFu, db = bg & 0xFFu;
    uint32_t r = (sr * eff + dr * inv + 127u) / 255u;
    uint32_t g = (sg * eff + dg * inv + 127u) / 255u;
    uint32_t b = (sb * eff + db * inv + 127u) / 255u;
    return (r << 16) | (g << 8) | b;
}

/* ---------- Path builder ---------- */

bool im_path_init(im_path *path, im_allocator alloc, im_fill_rule fill_rule) {
    if (!path || !alloc.alloc || !alloc.realloc || !alloc.free) return false;
    path->alloc = alloc;
    path->segs = NULL;
    path->count = 0u;
    path->cap = 0u;
    path->fill_rule = fill_rule;
    return true;
}

void im_path_destroy(im_path *path) {
    if (!path) return;
    if (path->segs && path->alloc.free) {
        path->alloc.free(path->alloc.ctx, path->segs, (size_t)path->cap * sizeof(im_path_seg));
    }
    path->segs = NULL;
    path->count = 0u;
    path->cap = 0u;
}

void im_path_clear(im_path *path) {
    if (!path) return;
    path->count = 0u;
}

im_path_data im_path_view(const im_path *path) {
    im_path_data d = { NULL, 0u, IM_FILL_EVEN_ODD };
    if (!path) return d;
    d.segs = path->segs;
    d.count = path->count;
    d.fill_rule = path->fill_rule;
    return d;
}

static bool path_push(im_path *path, im_path_seg seg) {
    if (!path || !path->alloc.realloc) return false;
    if (path->count == UINT32_MAX) return false;
    if (path->count >= path->cap) {
        uint32_t new_cap = (path->cap == 0u) ? 16u : path->cap * 2u;
        if (new_cap <= path->cap || (size_t)new_cap > (SIZE_MAX / sizeof(im_path_seg))) return false;
        size_t old_b = (size_t)path->cap * sizeof(im_path_seg);
        size_t new_b = (size_t)new_cap * sizeof(im_path_seg);
        im_path_seg *nb = (im_path_seg *)path->alloc.realloc(path->alloc.ctx, path->segs, old_b, new_b);
        if (!nb) return false;
        path->segs = nb;
        path->cap = new_cap;
    }
    path->segs[path->count++] = seg;
    return true;
}

bool im_path_move_to(im_path *path, im_fixed x, im_fixed y) {
    im_path_seg s = { IM_PATH_MOVE, x, y, 0, 0, 0, 0, 0, false, false };
    return path_push(path, s);
}

bool im_path_line_to(im_path *path, im_fixed x, im_fixed y) {
    im_path_seg s = { IM_PATH_LINE, x, y, 0, 0, 0, 0, 0, false, false };
    return path_push(path, s);
}

bool im_path_quad_to(im_path *path, im_fixed cx, im_fixed cy, im_fixed x, im_fixed y) {
    im_path_seg s = { IM_PATH_QUAD, cx, cy, x, y, 0, 0, 0, false, false };
    return path_push(path, s);
}

bool im_path_cubic_to(im_path *path, im_fixed c1x, im_fixed c1y, im_fixed c2x, im_fixed c2y, im_fixed x, im_fixed y) {
    im_path_seg s = { IM_PATH_CUBIC, c1x, c1y, c2x, c2y, x, y, 0, false, false };
    return path_push(path, s);
}

bool im_path_arc_to(im_path *path, im_fixed rx, im_fixed ry, im_fixed x_axis_rot_deg,
                    bool large_arc, bool sweep, im_fixed x, im_fixed y) {
    im_path_seg s = { IM_PATH_ARC, rx, ry, x, y, 0, 0, x_axis_rot_deg, large_arc, sweep };
    return path_push(path, s);
}

bool im_path_close(im_path *path) {
    im_path_seg s = { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false };
    return path_push(path, s);
}

static im_fixed q30_to_fixed_round(int64_t q) {
    const int shift = IM_TR_SHIFT - 16;
    const int64_t half = (int64_t)1 << (shift - 1);
    int64_t r = (q >= 0) ? ((q + half) >> shift) : -(((-q) + half) >> shift);
    if (r < INT32_MIN) return INT32_MIN;
    if (r > INT32_MAX) return INT32_MAX;
    return (im_fixed)r;
}

bool im_path_arc_angles(im_path *path, im_fixed cx, im_fixed cy, im_fixed rx, im_fixed ry,
                        im_fixed start_deg, im_fixed sweep_deg, bool move_to_start) {
    if (!path) return false;
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx == 0 || ry == 0) return false;

    int64_t cx_q = im_fixed_to_q30(cx);
    int64_t cy_q = im_fixed_to_q30(cy);
    int64_t rx_q = im_fixed_to_q30(rx);
    int64_t ry_q = im_fixed_to_q30(ry);

    int64_t sx0 = cx_q + q30_mul(rx_q, im_cos_deg(start_deg));
    int64_t sy0 = cy_q + q30_mul(ry_q, im_sin_deg(start_deg));
    if (move_to_start) {
        if (!im_path_move_to(path, q30_to_fixed_round(sx0), q30_to_fixed_round(sy0))) return false;
    } else {
        if (!im_path_line_to(path, q30_to_fixed_round(sx0), q30_to_fixed_round(sy0))) return false;
    }
    if (sweep_deg == 0) return true;

    int64_t abs_sweep = i64_abs((int64_t)sweep_deg);
    int64_t segs = (abs_sweep + (45LL * IM_ONE) - 1) / (45LL * IM_ONE);
    if (segs < 1) segs = 1;
    if (segs > 64) segs = 64;

    for (int64_t i = 0; i < segs; i++) {
        im_fixed a0 = (im_fixed)((int64_t)start_deg + ((int64_t)sweep_deg * i) / segs);
        im_fixed a1 = (im_fixed)((int64_t)start_deg + ((int64_t)sweep_deg * (i + 1)) / segs);
        im_fixed delta = (im_fixed)((int64_t)a1 - (int64_t)a0);
        int64_t tan_q = 0;
        if (!im_tan_deg(delta / 4, &tan_q)) return false;
        int64_t k_q = (tan_q * 4) / 3;

        int64_t ca0 = im_cos_deg(a0), sa0 = im_sin_deg(a0);
        int64_t ca1 = im_cos_deg(a1), sa1 = im_sin_deg(a1);

        int64_t c1x = cx_q + q30_mul(rx_q, ca0 - q30_mul(k_q, sa0));
        int64_t c1y = cy_q + q30_mul(ry_q, sa0 + q30_mul(k_q, ca0));
        int64_t c2x = cx_q + q30_mul(rx_q, ca1 + q30_mul(k_q, sa1));
        int64_t c2y = cy_q + q30_mul(ry_q, sa1 - q30_mul(k_q, ca1));
        int64_t ex = cx_q + q30_mul(rx_q, ca1);
        int64_t ey = cy_q + q30_mul(ry_q, sa1);
        if (!im_path_cubic_to(path,
                              q30_to_fixed_round(c1x), q30_to_fixed_round(c1y),
                              q30_to_fixed_round(c2x), q30_to_fixed_round(c2y),
                              q30_to_fixed_round(ex), q30_to_fixed_round(ey))) {
            return false;
        }
    }
    return true;
}

/* SDK-owned geometric checkmark in [0, 16] x [0, 16] (clean original geometry, never Microsoft coordinates). */
static const im_path_seg SDK_CHECKMARK_SEGS[7] = {
    { IM_PATH_MOVE, 2 * IM_ONE, 8 * IM_ONE, 0, 0, 0, 0, 0, false, false },
    { IM_PATH_LINE, 4 * IM_ONE, 6 * IM_ONE, 0, 0, 0, 0, 0, false, false },
    { IM_PATH_LINE, 6 * IM_ONE + IM_ONE / 2, 9 * IM_ONE, 0, 0, 0, 0, 0, false, false },
    { IM_PATH_LINE, 12 * IM_ONE + IM_ONE / 2, 3 * IM_ONE, 0, 0, 0, 0, 0, false, false },
    { IM_PATH_LINE, 14 * IM_ONE, 4 * IM_ONE + IM_ONE / 2, 0, 0, 0, 0, 0, false, false },
    { IM_PATH_LINE, 6 * IM_ONE + IM_ONE / 2, 13 * IM_ONE, 0, 0, 0, 0, 0, false, false },
    { IM_PATH_CLOSE, 0, 0, 0, 0, 0, 0, 0, false, false }
};

im_path_data im_path_sdk_checkmark(void) {
    im_path_data d = { SDK_CHECKMARK_SEGS, 7u, IM_FILL_NON_ZERO };
    return d;
}

/* ---------- Polyline & Edge buffers (all-or-nothing allocation) ---------- */

typedef struct pt_q30 {
    int64_t x, y;
} pt_q30;

typedef struct pt_buf {
    im_allocator alloc;
    pt_q30 *pts;
    uint32_t count;
    uint32_t cap;
    bool oom;
} pt_buf;

static void pt_buf_init(pt_buf *b, im_allocator alloc) {
    b->alloc = alloc;
    b->pts = NULL;
    b->count = 0u;
    b->cap = 0u;
    b->oom = false;
}

static void pt_buf_destroy(pt_buf *b) {
    if (b->pts && b->alloc.free) {
        b->alloc.free(b->alloc.ctx, b->pts, (size_t)b->cap * sizeof(pt_q30));
    }
    b->pts = NULL;
    b->count = 0u;
    b->cap = 0u;
}

static bool pt_buf_push(pt_buf *b, int64_t x, int64_t y) {
    if (b->oom) return false;
    if (b->count >= b->cap) {
        uint32_t nc = (b->cap == 0u) ? 64u : b->cap * 2u;
        if (nc <= b->cap || (size_t)nc > (SIZE_MAX / sizeof(pt_q30))) { b->oom = true; return false; }
        pt_q30 *np = (pt_q30 *)b->alloc.realloc(b->alloc.ctx, b->pts,
                                                (size_t)b->cap * sizeof(pt_q30),
                                                (size_t)nc * sizeof(pt_q30));
        if (!np) { b->oom = true; return false; }
        b->pts = np;
        b->cap = nc;
    }
    b->pts[b->count++] = (pt_q30){ x, y };
    return true;
}

typedef struct subpath_rec {
    uint32_t start;
    uint32_t count;
    bool closed;
} subpath_rec;

typedef struct subpath_list {
    im_allocator alloc;
    pt_buf pts;
    subpath_rec *subs;
    uint32_t count;
    uint32_t cap;
    bool oom;
} subpath_list;

static void subpaths_init(subpath_list *sl, im_allocator alloc) {
    sl->alloc = alloc;
    pt_buf_init(&sl->pts, alloc);
    sl->subs = NULL;
    sl->count = 0u;
    sl->cap = 0u;
    sl->oom = false;
}

static void subpaths_destroy(subpath_list *sl) {
    pt_buf_destroy(&sl->pts);
    if (sl->subs && sl->alloc.free) {
        sl->alloc.free(sl->alloc.ctx, sl->subs, (size_t)sl->cap * sizeof(subpath_rec));
    }
    sl->subs = NULL;
    sl->count = 0u;
    sl->cap = 0u;
}

static bool subpaths_begin(subpath_list *sl, int64_t x, int64_t y) {
    if (sl->oom) return false;
    if (sl->count >= sl->cap) {
        uint32_t nc = (sl->cap == 0u) ? 8u : sl->cap * 2u;
        if (nc <= sl->cap || (size_t)nc > (SIZE_MAX / sizeof(subpath_rec))) { sl->oom = true; return false; }
        subpath_rec *ns = (subpath_rec *)sl->alloc.realloc(sl->alloc.ctx, sl->subs,
                                                           (size_t)sl->cap * sizeof(subpath_rec),
                                                           (size_t)nc * sizeof(subpath_rec));
        if (!ns) { sl->oom = true; return false; }
        sl->subs = ns;
        sl->cap = nc;
    }
    uint32_t idx = sl->pts.count;
    if (!pt_buf_push(&sl->pts, x, y)) { sl->oom = true; return false; }
    sl->subs[sl->count++] = (subpath_rec){ idx, 1u, false };
    return true;
}

static bool subpaths_add_pt(subpath_list *sl, int64_t x, int64_t y) {
    if (sl->oom || sl->count == 0u) return false;
    if (!pt_buf_push(&sl->pts, x, y)) { sl->oom = true; return false; }
    sl->subs[sl->count - 1u].count++;
    return true;
}

/* ---------- Curve & SVG-Arc Flattening ---------- */

static bool flatten_quad(subpath_list *sl,
                         int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t x2, int64_t y2,
                         int64_t tol_q30, int depth) {
    /* Flatness metric: max(|2*x1 - x0 - x2|, |2*y1 - y0 - y2|) <= 4 * tol_q30 */
    int64_t dx = i64_abs(2 * x1 - x0 - x2);
    int64_t dy = i64_abs(2 * y1 - y0 - y2);
    if (depth >= 12 || (dx <= 4 * tol_q30 && dy <= 4 * tol_q30)) {
        return subpaths_add_pt(sl, x2, y2);
    }
    int64_t x01 = (x0 + x1) >> 1, y01 = (y0 + y1) >> 1;
    int64_t x12 = (x1 + x2) >> 1, y12 = (y1 + y2) >> 1;
    int64_t xm = (x01 + x12) >> 1, ym = (y01 + y12) >> 1;
    if (!flatten_quad(sl, x0, y0, x01, y01, xm, ym, tol_q30, depth + 1)) return false;
    return flatten_quad(sl, xm, ym, x12, y12, x2, y2, tol_q30, depth + 1);
}

static bool flatten_cubic(subpath_list *sl,
                          int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                          int64_t x2, int64_t y2, int64_t x3, int64_t y3,
                          int64_t tol_q30, int depth) {
    /* Flatness metric (Wang/cubic chord bound):
       max(|3*x1 - 2*x0 - x3|, |3*x2 - x0 - 2*x3|, same for y) <= 16 * tol_q30 / 3 */
    int64_t d1x = i64_abs(3 * x1 - 2 * x0 - x3);
    int64_t d1y = i64_abs(3 * y1 - 2 * y0 - y3);
    int64_t d2x = i64_abs(3 * x2 - x0 - 2 * x3);
    int64_t d2y = i64_abs(3 * y2 - y0 - 2 * y3);
    int64_t mx = (d1x > d2x) ? d1x : d2x;
    int64_t my = (d1y > d2y) ? d1y : d2y;
    int64_t lim = tol_q30 * 4;
    if (depth >= 12 || (mx <= lim && my <= lim)) {
        return subpaths_add_pt(sl, x3, y3);
    }
    int64_t x01 = (x0 + x1) >> 1, y01 = (y0 + y1) >> 1;
    int64_t x12 = (x1 + x2) >> 1, y12 = (y1 + y2) >> 1;
    int64_t x23 = (x2 + x3) >> 1, y23 = (y2 + y3) >> 1;
    int64_t x012 = (x01 + x12) >> 1, y012 = (y01 + y12) >> 1;
    int64_t x123 = (x12 + x23) >> 1, y123 = (y12 + y23) >> 1;
    int64_t xm = (x012 + x123) >> 1, ym = (y012 + y123) >> 1;
    if (!flatten_cubic(sl, x0, y0, x01, y01, x012, y012, xm, ym, tol_q30, depth + 1)) return false;
    return flatten_cubic(sl, xm, ym, x123, y123, x23, y23, x3, y3, tol_q30, depth + 1);
}

static bool transform_pt_opt(const im_transform *tr, int64_t lx, int64_t ly, int64_t *ox, int64_t *oy) {
    if (!tr) { *ox = lx; *oy = ly; return true; }
    return im_transform_point_q30(tr, lx, ly, ox, oy);
}

/* Convert W3C SVG/WinUI endpoint elliptical arc in local Q30 to cubics, transforming each control point by tr. */
static bool flatten_svg_arc(subpath_list *sl, const im_transform *tr,
                            int64_t x0, int64_t y0,
                            int64_t rx, int64_t ry, im_fixed phi_deg,
                            bool large_arc, bool sweep,
                            int64_t x1, int64_t y1, int64_t tol_q30) {
    if (x0 == x1 && y0 == y1) return true;
    rx = i64_abs(rx);
    ry = i64_abs(ry);
    if (rx == 0 || ry == 0) {
        int64_t tx = 0, ty = 0;
        if (!transform_pt_opt(tr, x1, y1, &tx, &ty)) return false;
        return subpaths_add_pt(sl, tx, ty);
    }

    int64_t cos_p = im_cos_deg(phi_deg);
    int64_t sin_p = im_sin_deg(phi_deg);
    int64_t dx2 = (x0 - x1) >> 1;
    int64_t dy2 = (y0 - y1) >> 1;
    int64_t x1p = q30_mul(dx2, cos_p) + q30_mul(dy2, sin_p);
    int64_t y1p = -q30_mul(dx2, sin_p) + q30_mul(dy2, cos_p);

    /* Compute u = x1p / rx, v = y1p / ry in Q30. */
    int64_t u = im_sdiv128_i64((__int128)x1p * IM_TR_ONE, rx);
    int64_t v = im_sdiv128_i64((__int128)y1p * IM_TR_ONE, ry);

    /* Normalize (u, v) if huge to keep u^2 + v^2 within __int128. */
    int shift = 0;
    int64_t us = u, vs = v;
    while (i64_abs(us) > ((int64_t)1 << 45) || i64_abs(vs) > ((int64_t)1 << 45)) {
        us >>= 1;
        vs >>= 1;
        shift++;
    }
    unsigned __int128 lam_q60 = (unsigned __int128)i64_abs(us) * (unsigned __int128)i64_abs(us) +
                                (unsigned __int128)i64_abs(vs) * (unsigned __int128)i64_abs(vs);
    unsigned __int128 one_q60 = ((unsigned __int128)IM_TR_ONE * IM_TR_ONE) >> (2 * shift);

    int64_t coef_q30 = 0;
    if (shift > 0 || lam_q60 >= one_q60) {
        /* Scale radii up by sqrt(lambda) so ellipse just reaches endpoints; center is midpoint. */
        uint64_t s_q30 = isqrt128(lam_q60) << shift;
        rx = q30_mul(rx, (int64_t)s_q30);
        ry = q30_mul(ry, (int64_t)s_q30);
        if (rx == 0) rx = 1;
        if (ry == 0) ry = 1;
        coef_q30 = 0;
    } else if (lam_q60 > 0) {
        /* sq = (1 - lam) / lam in Q60 -> sqrt(sq) in Q30 */
        unsigned __int128 diff_q60 = one_q60 - lam_q60;
        /* Shift down to fit denominator in int64_t for im_sdiv128_i64. */
        unsigned __int128 num = diff_q60;
        unsigned __int128 den = lam_q60;
        while (den > (unsigned __int128)INT64_MAX) {
            num >>= 1;
            den >>= 1;
        }
        if (den == 0) den = 1;
        /* Want sqrt(num / den) * 2^30 = sqrt((num * 2^60) / den). Keep num*2^60 inside 128 bits. */
        int extra = 60;
        while (extra > 0 && num > (((unsigned __int128)1 << 127) >> extra)) {
            num >>= 1;
            den >>= 1;
            if (den == 0) den = 1;
            extra--;
            if (extra > 0) extra--;
        }
        int64_t sq_q = im_sdiv128_i64((__int128)(num << extra), (int64_t)den);
        if (sq_q < 0) sq_q = 0;
        int64_t root = (int64_t)isqrt64((uint64_t)sq_q);
        int rem = 30 - (extra / 2);
        coef_q30 = (rem >= 0) ? (root << rem) : (root >> (-rem));
        if (large_arc == sweep) coef_q30 = -coef_q30;
    }

    int64_t cxp = q30_mul(coef_q30, im_sdiv128_i64((__int128)rx * y1p, ry));
    int64_t cyp = -q30_mul(coef_q30, im_sdiv128_i64((__int128)ry * x1p, rx));

    int64_t mx = (x0 + x1) >> 1;
    int64_t my = (y0 + y1) >> 1;
    int64_t cx = q30_mul(cxp, cos_p) - q30_mul(cyp, sin_p) + mx;
    int64_t cy = q30_mul(cxp, sin_p) + q30_mul(cyp, cos_p) + my;

    int64_t ux0 = im_sdiv128_i64((__int128)(x1p - cxp) * IM_TR_ONE, rx);
    int64_t uy0 = im_sdiv128_i64((__int128)(y1p - cyp) * IM_TR_ONE, ry);
    int64_t ux1 = im_sdiv128_i64((__int128)(-x1p - cxp) * IM_TR_ONE, rx);
    int64_t uy1 = im_sdiv128_i64((__int128)(-y1p - cyp) * IM_TR_ONE, ry);

    im_fixed a_start = atan2_deg_fx(uy0, ux0);
    im_fixed a_end = atan2_deg_fx(uy1, ux1);
    int64_t delta = (int64_t)a_end - (int64_t)a_start;
    if (sweep && delta < 0) delta += 360LL * IM_ONE;
    if (!sweep && delta > 0) delta -= 360LL * IM_ONE;
    if (delta == 0) {
        int64_t tx = 0, ty = 0;
        if (!transform_pt_opt(tr, x1, y1, &tx, &ty)) return false;
        return subpaths_add_pt(sl, tx, ty);
    }

    int64_t segs = (i64_abs(delta) + (45LL * IM_ONE) - 1) / (45LL * IM_ONE);
    if (segs < 1) segs = 1;
    if (segs > 64) segs = 64;

    int64_t cur_x = 0, cur_y = 0;
    if (!transform_pt_opt(tr, x0, y0, &cur_x, &cur_y)) return false;

    for (int64_t i = 0; i < segs; i++) {
        im_fixed a0 = (im_fixed)((int64_t)a_start + (delta * i) / segs);
        im_fixed a1 = (im_fixed)((int64_t)a_start + (delta * (i + 1)) / segs);
        im_fixed d = (im_fixed)((int64_t)a1 - (int64_t)a0);
        int64_t tan_q = 0;
        if (!im_tan_deg(d / 4, &tan_q)) return false;
        int64_t k_q = (tan_q * 4) / 3;

        int64_t ca0 = im_cos_deg(a0), sa0 = im_sin_deg(a0);
        int64_t ca1 = im_cos_deg(a1), sa1 = im_sin_deg(a1);

        int64_t lc1x = q30_mul(rx, ca0 - q30_mul(k_q, sa0));
        int64_t lc1y = q30_mul(ry, sa0 + q30_mul(k_q, ca0));
        int64_t lc2x = q30_mul(rx, ca1 + q30_mul(k_q, sa1));
        int64_t lc2y = q30_mul(ry, sa1 - q30_mul(k_q, ca1));
        int64_t lex = q30_mul(rx, ca1);
        int64_t ley = q30_mul(ry, sa1);

        int64_t rc1x = cx + q30_mul(lc1x, cos_p) - q30_mul(lc1y, sin_p);
        int64_t rc1y = cy + q30_mul(lc1x, sin_p) + q30_mul(lc1y, cos_p);
        int64_t rc2x = cx + q30_mul(lc2x, cos_p) - q30_mul(lc2y, sin_p);
        int64_t rc2y = cy + q30_mul(lc2x, sin_p) + q30_mul(lc2y, cos_p);
        int64_t rex = (i + 1 == segs) ? x1 : (cx + q30_mul(lex, cos_p) - q30_mul(ley, sin_p));
        int64_t rey = (i + 1 == segs) ? y1 : (cy + q30_mul(lex, sin_p) + q30_mul(ley, cos_p));

        int64_t tc1x = 0, tc1y = 0, tc2x = 0, tc2y = 0, tex = 0, tey = 0;
        if (!transform_pt_opt(tr, rc1x, rc1y, &tc1x, &tc1y) ||
            !transform_pt_opt(tr, rc2x, rc2y, &tc2x, &tc2y) ||
            !transform_pt_opt(tr, rex, rey, &tex, &tey)) {
            return false;
        }
        if (!flatten_cubic(sl, cur_x, cur_y, tc1x, tc1y, tc2x, tc2y, tex, tey, tol_q30, 0)) return false;
        cur_x = tex;
        cur_y = tey;
    }
    return true;
}

static bool flatten_path_to_subpaths(const im_path_data *path, const im_transform *tr,
                                     int64_t tol_q30, subpath_list *sl) {
    if (!path || !sl) return false;
    bool has_cur = false;
    int64_t cur_lx = 0, cur_ly = 0;
    int64_t start_lx = 0, start_ly = 0;
    int64_t cur_tx = 0, cur_ty = 0;

    for (uint32_t i = 0; i < path->count; i++) {
        const im_path_seg *s = &path->segs[i];
        switch (s->cmd) {
        case IM_PATH_MOVE: {
            cur_lx = im_fixed_to_q30(s->x1);
            cur_ly = im_fixed_to_q30(s->y1);
            start_lx = cur_lx;
            start_ly = cur_ly;
            if (!transform_pt_opt(tr, cur_lx, cur_ly, &cur_tx, &cur_ty)) return false;
            if (!subpaths_begin(sl, cur_tx, cur_ty)) return false;
            has_cur = true;
            break;
        }
        case IM_PATH_LINE: {
            if (!has_cur) {
                cur_lx = im_fixed_to_q30(s->x1);
                cur_ly = im_fixed_to_q30(s->y1);
                start_lx = cur_lx;
                start_ly = cur_ly;
                if (!transform_pt_opt(tr, cur_lx, cur_ly, &cur_tx, &cur_ty)) return false;
                if (!subpaths_begin(sl, cur_tx, cur_ty)) return false;
                has_cur = true;
            } else {
                cur_lx = im_fixed_to_q30(s->x1);
                cur_ly = im_fixed_to_q30(s->y1);
                if (!transform_pt_opt(tr, cur_lx, cur_ly, &cur_tx, &cur_ty)) return false;
                if (!subpaths_add_pt(sl, cur_tx, cur_ty)) return false;
            }
            break;
        }
        case IM_PATH_QUAD: {
            if (!has_cur) return false;
            int64_t c1x = 0, c1y = 0, ex = 0, ey = 0;
            int64_t lx1 = im_fixed_to_q30(s->x1), ly1 = im_fixed_to_q30(s->y1);
            int64_t lx2 = im_fixed_to_q30(s->x2), ly2 = im_fixed_to_q30(s->y2);
            if (!transform_pt_opt(tr, lx1, ly1, &c1x, &c1y) ||
                !transform_pt_opt(tr, lx2, ly2, &ex, &ey)) return false;
            if (!flatten_quad(sl, cur_tx, cur_ty, c1x, c1y, ex, ey, tol_q30, 0)) return false;
            cur_lx = lx2; cur_ly = ly2;
            cur_tx = ex; cur_ty = ey;
            break;
        }
        case IM_PATH_CUBIC: {
            if (!has_cur) return false;
            int64_t c1x = 0, c1y = 0, c2x = 0, c2y = 0, ex = 0, ey = 0;
            int64_t lx1 = im_fixed_to_q30(s->x1), ly1 = im_fixed_to_q30(s->y1);
            int64_t lx2 = im_fixed_to_q30(s->x2), ly2 = im_fixed_to_q30(s->y2);
            int64_t lx3 = im_fixed_to_q30(s->x3), ly3 = im_fixed_to_q30(s->y3);
            if (!transform_pt_opt(tr, lx1, ly1, &c1x, &c1y) ||
                !transform_pt_opt(tr, lx2, ly2, &c2x, &c2y) ||
                !transform_pt_opt(tr, lx3, ly3, &ex, &ey)) return false;
            if (!flatten_cubic(sl, cur_tx, cur_ty, c1x, c1y, c2x, c2y, ex, ey, tol_q30, 0)) return false;
            cur_lx = lx3; cur_ly = ly3;
            cur_tx = ex; cur_ty = ey;
            break;
        }
        case IM_PATH_ARC: {
            if (!has_cur) return false;
            int64_t lx2 = im_fixed_to_q30(s->x2), ly2 = im_fixed_to_q30(s->y2);
            if (!flatten_svg_arc(sl, tr, cur_lx, cur_ly,
                                 im_fixed_to_q30(s->x1), im_fixed_to_q30(s->y1),
                                 s->angle_deg, s->large_arc, s->sweep,
                                 lx2, ly2, tol_q30)) {
                return false;
            }
            cur_lx = lx2; cur_ly = ly2;
            if (!transform_pt_opt(tr, cur_lx, cur_ly, &cur_tx, &cur_ty)) return false;
            break;
        }
        case IM_PATH_CLOSE: {
            if (sl->count > 0u) {
                sl->subs[sl->count - 1u].closed = true;
            }
            cur_lx = start_lx;
            cur_ly = start_ly;
            if (!transform_pt_opt(tr, cur_lx, cur_ly, &cur_tx, &cur_ty)) return false;
            break;
        }
        default:
            return false;
        }
    }
    return !sl->oom && !sl->pts.oom;
}

bool im_path_bounds(const im_path_data *path, im_rect *out_bounds) {
    if (!path || !out_bounds || path->count == 0u) return false;
    bool has = false;
    int64_t min_x = INT64_MAX, min_y = INT64_MAX;
    int64_t max_x = INT64_MIN, max_y = INT64_MIN;
    /* Track control/endpoint bounds; for arcs also include center +/- radius conservatism via endpoint + rx/ry. */
    int64_t cur_x = 0, cur_y = 0;
    for (uint32_t i = 0; i < path->count; i++) {
        const im_path_seg *s = &path->segs[i];
        int64_t xs[3], ys[3];
        int n = 0;
        switch (s->cmd) {
        case IM_PATH_MOVE:
        case IM_PATH_LINE:
            xs[0] = s->x1; ys[0] = s->y1; n = 1;
            cur_x = s->x1; cur_y = s->y1;
            break;
        case IM_PATH_QUAD:
            xs[0] = s->x1; ys[0] = s->y1;
            xs[1] = s->x2; ys[1] = s->y2; n = 2;
            cur_x = s->x2; cur_y = s->y2;
            break;
        case IM_PATH_CUBIC:
            xs[0] = s->x1; ys[0] = s->y1;
            xs[1] = s->x2; ys[1] = s->y2;
            xs[2] = s->x3; ys[2] = s->y3; n = 3;
            cur_x = s->x3; cur_y = s->y3;
            break;
        case IM_PATH_ARC: {
            int64_t rx = i64_abs(s->x1), ry = i64_abs(s->y1);
            int64_t r = (rx > ry) ? rx : ry;
            int64_t lo_x = (cur_x < s->x2 ? cur_x : s->x2) - r;
            int64_t hi_x = (cur_x > s->x2 ? cur_x : s->x2) + r;
            int64_t lo_y = (cur_y < s->y2 ? cur_y : s->y2) - r;
            int64_t hi_y = (cur_y > s->y2 ? cur_y : s->y2) + r;
            xs[0] = lo_x; ys[0] = lo_y;
            xs[1] = hi_x; ys[1] = hi_y; n = 2;
            cur_x = s->x2; cur_y = s->y2;
            break;
        }
        case IM_PATH_CLOSE:
            break;
        }
        for (int k = 0; k < n; k++) {
            if (!has || xs[k] < min_x) min_x = xs[k];
            if (!has || xs[k] > max_x) max_x = xs[k];
            if (!has || ys[k] < min_y) min_y = ys[k];
            if (!has || ys[k] > max_y) max_y = ys[k];
            has = true;
        }
    }
    if (!has) return false;
    if (min_x < INT32_MIN) min_x = INT32_MIN;
    if (min_y < INT32_MIN) min_y = INT32_MIN;
    if (max_x > INT32_MAX) max_x = INT32_MAX;
    if (max_y > INT32_MAX) max_y = INT32_MAX;
    *out_bounds = (im_rect){ (im_fixed)min_x, (im_fixed)min_y, (im_fixed)max_x, (im_fixed)max_y };
    return true;
}

/* ---------- 16-Subscanline Analytic Horizontal Coverage Rasterizer ---------- */

typedef struct scan_edge {
    int64_t x0, y0, x1, y1;
    int8_t dir;
} scan_edge;

typedef struct scan_x {
    int64_t x;
    int8_t dir;
} scan_x;

static void sort_scan_x(scan_x *arr, uint32_t n) {
    /* Shell sort: O(n log n) worst-ish, fast on small n, zero recursion/alloc. */
    for (uint32_t gap = n >> 1; gap > 0u; gap >>= 1) {
        for (uint32_t i = gap; i < n; i++) {
            scan_x tmp = arr[i];
            uint32_t j = i;
            while (j >= gap && arr[j - gap].x > tmp.x) {
                arr[j] = arr[j - gap];
                j -= gap;
            }
            arr[j] = tmp;
        }
    }
}

static bool rasterize_subpaths(im_surface *surface, im_allocator alloc,
                               const subpath_list *sl, im_fill_rule fill_rule,
                               uint32_t solid_argb, im_path_shader_fn shader, void *shader_ctx) {
    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0) return true;
    if (!alloc.alloc || !alloc.realloc || !alloc.free) return false;

    int bound_l = 0, bound_t = 0, bound_r = surface->width, bound_b = surface->height;
    if (surface->layers) {
        im_layer_draw_bounds(surface->layers, &bound_l, &bound_t, &bound_r, &bound_b);
    }
    if (bound_l >= bound_r || bound_t >= bound_b) return true;

    im_transform inv;
    bool has_tr = (surface->transform != NULL);
    if (has_tr && !im_transform_invert(surface->transform, &inv)) return true;

    /* Count max possible edges across all subpaths (implicit closure for fill!). */
    uint32_t max_edges = 0u;
    for (uint32_t si = 0; si < sl->count; si++) {
        if (sl->subs[si].count >= 2u) {
            if (max_edges > UINT32_MAX - sl->subs[si].count) return false;
            max_edges += sl->subs[si].count;
        }
    }
    if (max_edges == 0u) return true;

    if ((size_t)max_edges > (SIZE_MAX / sizeof(scan_edge))) return false;
    size_t edges_bytes = (size_t)max_edges * sizeof(scan_edge);
    scan_edge *edges = (scan_edge *)alloc.alloc(alloc.ctx, edges_bytes);
    if (!edges) return false;

    uint32_t edge_count = 0u;
    int64_t min_x = INT64_MAX, max_x = INT64_MIN;
    int64_t min_y = INT64_MAX, max_y = INT64_MIN;

    for (uint32_t si = 0; si < sl->count; si++) {
        const subpath_rec *sr = &sl->subs[si];
        if (sr->count < 2u) continue;
        for (uint32_t k = 0; k < sr->count; k++) {
            pt_q30 p0 = sl->pts.pts[sr->start + k];
            pt_q30 p1 = sl->pts.pts[sr->start + ((k + 1u < sr->count) ? (k + 1u) : 0u)];
            if (p0.y == p1.y) continue;
            scan_edge e;
            if (p0.y < p1.y) {
                e.x0 = p0.x; e.y0 = p0.y; e.x1 = p1.x; e.y1 = p1.y; e.dir = 1;
            } else {
                e.x0 = p1.x; e.y0 = p1.y; e.x1 = p0.x; e.y1 = p0.y; e.dir = -1;
            }
            edges[edge_count++] = e;
            int64_t ex_lo = (e.x0 < e.x1) ? e.x0 : e.x1;
            int64_t ex_hi = (e.x0 > e.x1) ? e.x0 : e.x1;
            if (ex_lo < min_x) min_x = ex_lo;
            if (ex_hi > max_x) max_x = ex_hi;
            if (e.y0 < min_y) min_y = e.y0;
            if (e.y1 > max_y) max_y = e.y1;
        }
    }

    if (edge_count == 0u) {
        alloc.free(alloc.ctx, edges, edges_bytes);
        return true;
    }

    /* Clip bounding box to viewport [bound_l, bound_r) x [bound_t, bound_b). */
    int64_t f_from = (min_x >= 0) ? (min_x >> IM_TR_SHIFT) : -(((-min_x) + IM_TR_ONE - 1) >> IM_TR_SHIFT);
    int64_t f_to = (max_x >= 0) ? ((max_x + IM_TR_ONE - 1) >> IM_TR_SHIFT) : -((-max_x) >> IM_TR_SHIFT);
    int64_t f_top = (min_y >= 0) ? (min_y >> IM_TR_SHIFT) : -(((-min_y) + IM_TR_ONE - 1) >> IM_TR_SHIFT);
    int64_t f_bot = (max_y >= 0) ? ((max_y + IM_TR_ONE - 1) >> IM_TR_SHIFT) : -((-max_y) >> IM_TR_SHIFT);

    int from = (f_from < bound_l) ? bound_l : (f_from > bound_r ? bound_r : (int)f_from);
    int to = (f_to > bound_r) ? bound_r : (f_to < bound_l ? bound_l : (int)f_to);
    int top = (f_top < bound_t) ? bound_t : (f_top > bound_b ? bound_b : (int)f_top);
    int bottom = (f_bot > bound_b) ? bound_b : (f_bot < bound_t ? bound_t : (int)f_bot);

    if (from >= to || top >= bottom) {
        alloc.free(alloc.ctx, edges, edges_bytes);
        return true;
    }

    int row_w = to - from;
    size_t cov_bytes = (size_t)row_w * sizeof(uint32_t);
    size_t xs_bytes = (size_t)edge_count * sizeof(scan_x);
    uint32_t *cov = (uint32_t *)alloc.alloc(alloc.ctx, cov_bytes);
    scan_x *xs = (scan_x *)alloc.alloc(alloc.ctx, xs_bytes);
    if (!cov || !xs) {
        if (cov) alloc.free(alloc.ctx, cov, cov_bytes);
        if (xs) alloc.free(alloc.ctx, xs, xs_bytes);
        alloc.free(alloc.ctx, edges, edges_bytes);
        return false;
    }

    int64_t row_l_q30 = (int64_t)from * IM_TR_ONE;
    int64_t row_r_q30 = (int64_t)to * IM_TR_ONE;
    const int64_t sub_step = IM_TR_ONE / 16;
    const int64_t sub_half = IM_TR_ONE / 32;

    for (int y = top; y < bottom; y++) {
        for (int i = 0; i < row_w; i++) cov[i] = 0u;
        int64_t y_base = (int64_t)y * IM_TR_ONE;

        for (int s = 0; s < 16; s++) {
            int64_t sy = y_base + (int64_t)s * sub_step + sub_half;
            uint32_t nx = 0u;
            for (uint32_t ei = 0; ei < edge_count; ei++) {
                const scan_edge *e = &edges[ei];
                if (sy < e->y0 || sy >= e->y1) continue;
                int64_t ix = e->x0 + im_sdiv128_i64((__int128)(e->x1 - e->x0) * (sy - e->y0), e->y1 - e->y0);
                xs[nx++] = (scan_x){ ix, e->dir };
            }
            if (nx < 2u) continue;
            sort_scan_x(xs, nx);

            int winding = 0;
            bool inside = false;
            int64_t span_start = 0;
            for (uint32_t i = 0; i < nx; i++) {
                winding += (int)xs[i].dir;
                bool now_in = (fill_rule == IM_FILL_NON_ZERO) ? (winding != 0) : ((winding & 1) != 0);
                if (!inside && now_in) {
                    span_start = xs[i].x;
                    inside = true;
                } else if (inside && !now_in) {
                    int64_t sl_q = span_start;
                    int64_t sr_q = xs[i].x;
                    inside = false;
                    if (sr_q <= row_l_q30 || sl_q >= row_r_q30 || sl_q >= sr_q) continue;
                    if (sl_q < row_l_q30) sl_q = row_l_q30;
                    if (sr_q > row_r_q30) sr_q = row_r_q30;
                    int px0 = (int)(sl_q >> IM_TR_SHIFT);
                    int px1 = (int)((sr_q - 1) >> IM_TR_SHIFT);
                    if (px0 < from) px0 = from;
                    if (px1 >= to) px1 = to - 1;
                    for (int px = px0; px <= px1; px++) {
                        int64_t p_l = (int64_t)px * IM_TR_ONE;
                        int64_t p_r = p_l + IM_TR_ONE;
                        int64_t ol = (sl_q > p_l) ? sl_q : p_l;
                        int64_t or_ = (sr_q < p_r) ? sr_q : p_r;
                        if (or_ > ol) {
                            uint32_t frac = (uint32_t)(((or_ - ol) + ((int64_t)1 << (IM_TR_SHIFT - 17))) >> (IM_TR_SHIFT - 16));
                            if (frac > (uint32_t)IM_ONE) frac = (uint32_t)IM_ONE;
                            cov[px - from] += frac;
                        }
                    }
                }
            }
        }

        uint32_t *line = surface->pixels + (size_t)y * (size_t)surface->stride;
        int64_t py_c = y_base + (IM_TR_ONE >> 1);
        for (int px = from; px < to; px++) {
            uint32_t c = (cov[px - from] + 8u) >> 4;
            if (c == 0u) continue;
            if (c > (uint32_t)IM_ONE) c = (uint32_t)IM_ONE;
            uint32_t col = solid_argb;
            if (shader) {
                int64_t px_c = (int64_t)px * IM_TR_ONE + (IM_TR_ONE >> 1);
                int64_t lx = px_c, ly = py_c;
                if (has_tr && !im_transform_point_q30(&inv, px_c, py_c, &lx, &ly)) continue;
                col = shader(shader_ctx, lx, ly);
            }
            if ((col >> 24) == 0u) continue;
            if (surface->layers) {
                im_layer_blend_pixel(surface->layers, px, y, col, c);
            } else {
                line[px] = blend_direct(line[px], col, c);
            }
        }
    }

    alloc.free(alloc.ctx, cov, cov_bytes);
    alloc.free(alloc.ctx, xs, xs_bytes);
    alloc.free(alloc.ctx, edges, edges_bytes);
    return true;
}

bool im_path_fill_shaded(im_surface *surface, im_allocator alloc, const im_path_data *path,
                         im_path_shader_fn shader, void *shader_ctx) {
    if (!surface || !path || path->count == 0u) return true;
    if (!alloc.alloc || !alloc.realloc || !alloc.free) return false;
    im_transform inv;
    if (surface->transform && !im_transform_invert(surface->transform, &inv)) return true;

    subpath_list sl;
    subpaths_init(&sl, alloc);
    bool ok = flatten_path_to_subpaths(path, surface->transform, IM_TR_ONE / 128, &sl);
    if (ok) {
        ok = rasterize_subpaths(surface, alloc, &sl, path->fill_rule, 0xFFFFFFFFu, shader, shader_ctx);
    }
    subpaths_destroy(&sl);
    return ok;
}

bool im_path_fill_argb(im_surface *surface, im_allocator alloc, const im_path_data *path, uint32_t argb) {
    if (!surface || !path || path->count == 0u || (argb >> 24) == 0u) return true;
    if (!alloc.alloc || !alloc.realloc || !alloc.free) return false;
    im_transform inv;
    if (surface->transform && !im_transform_invert(surface->transform, &inv)) return true;

    subpath_list sl;
    subpaths_init(&sl, alloc);
    bool ok = flatten_path_to_subpaths(path, surface->transform, IM_TR_ONE / 128, &sl);
    if (ok) {
        ok = rasterize_subpaths(surface, alloc, &sl, path->fill_rule, argb, NULL, NULL);
    }
    subpaths_destroy(&sl);
    return ok;
}

/* ---------- Stroke Expansion (caps, joins, miter limit, dashes) ---------- */

/* Append a circular arc of radius r around (cx, cy) from angle a0 to a1 (in Q30) as flattened vertices. */
static bool append_arc_pts(pt_buf *out, int64_t cx, int64_t cy, int64_t r,
                           im_fixed a0, int64_t sweep, int64_t tol_q30) {
    if (r <= 0 || sweep == 0) return true;
    /* Choose steps so sagitta r*(1 - cos(d/2)) <= tol_q30; conservative 16 steps per 90 deg or finer for large r. */
    int64_t abs_sw = i64_abs(sweep);
    int64_t steps = (abs_sw + (6LL * IM_ONE) - 1) / (6LL * IM_ONE);
    if (r > 32 * IM_TR_ONE) {
        int64_t extra = (r / (16 * IM_TR_ONE));
        if (extra > 4) extra = 4;
        steps *= extra;
    }
    if (steps < 4) steps = 4;
    if (steps > 64) steps = 64;
    (void)tol_q30;
    for (int64_t i = 1; i <= steps; i++) {
        im_fixed a = (im_fixed)((int64_t)a0 + (sweep * i) / steps);
        int64_t px = cx + q30_mul(r, im_cos_deg(a));
        int64_t py = cy + q30_mul(r, im_sin_deg(a));
        if (!pt_buf_push(out, px, py)) return false;
    }
    return true;
}

static bool add_cap(pt_buf *out, pt_q30 p, int64_t tx, int64_t ty, int64_t half_w,
                    im_line_cap cap, int64_t tol_q30) {
    /* Outward tangent is (tx, ty) unit Q30; left normal is (-ty, tx), right normal is (ty, -tx).
       Outline walks from left normal (+(-ty, tx)) around end to right normal (+(ty, -tx)). */
    int64_t nlx = -q30_mul(ty, half_w), nly = q30_mul(tx, half_w);
    int64_t nrx = -nlx, nry = -nly;
    int64_t dx = q30_mul(tx, half_w), dy = q30_mul(ty, half_w);

    switch (cap) {
    case IM_CAP_SQUARE:
        if (!pt_buf_push(out, p.x + nlx + dx, p.y + nly + dy)) return false;
        if (!pt_buf_push(out, p.x + nrx + dx, p.y + nry + dy)) return false;
        return true;
    case IM_CAP_TRIANGLE:
        if (!pt_buf_push(out, p.x + nlx, p.y + nly)) return false;
        if (!pt_buf_push(out, p.x + dx, p.y + dy)) return false;
        if (!pt_buf_push(out, p.x + nrx, p.y + nry)) return false;
        return true;
    case IM_CAP_ROUND: {
        im_fixed a0 = atan2_deg_fx(nly, nlx);
        if (!pt_buf_push(out, p.x + nlx, p.y + nly)) return false;
        /* In screen coords (+X right, +Y down), left normal (-ty, tx) -> tangent (tx, ty) -> right normal (ty, -tx)
           is -180 degrees (counter-clockwise). */
        return append_arc_pts(out, p.x, p.y, half_w, a0, -180LL * IM_ONE, tol_q30);
    }
    case IM_CAP_FLAT:
    default:
        if (!pt_buf_push(out, p.x + nlx, p.y + nly)) return false;
        if (!pt_buf_push(out, p.x + nrx, p.y + nry)) return false;
        return true;
    }
}

/* Add a join on the outer side of a vertex P between incoming unit tangent (t0x,t0y) and outgoing (t1x,t1y). */
static bool add_join_side(pt_buf *out, pt_q30 p,
                          int64_t n0x, int64_t n0y, int64_t n1x, int64_t n1y,
                          int64_t half_w, im_line_join join, int64_t miter_lim_q30,
                          int64_t tol_q30) {
    /* Cross product n0 x n1 determines whether this side is outer (convex) or inner (concave). */
    int64_t p0x = p.x + q30_mul(n0x, half_w), p0y = p.y + q30_mul(n0y, half_w);
    int64_t p1x = p.x + q30_mul(n1x, half_w), p1y = p.y + q30_mul(n1y, half_w);
    if (!pt_buf_push(out, p0x, p0y)) return false;

    int64_t dot = q30_mul(n0x, n1x) + q30_mul(n0y, n1y);
    int64_t cross = q30_mul(n0x, n1y) - q30_mul(n0y, n1x);
    if (dot >= IM_TR_ONE - 1024) {
        return pt_buf_push(out, p1x, p1y);
    }

    if (join == IM_JOIN_BEVEL) {
        return pt_buf_push(out, p1x, p1y);
    }
    if (join == IM_JOIN_ROUND) {
        im_fixed a0 = atan2_deg_fx(n0y, n0x);
        im_fixed a1 = atan2_deg_fx(n1y, n1x);
        int64_t sw = (int64_t)a1 - (int64_t)a0;
        if (cross > 0 && sw < 0) sw += 360LL * IM_ONE;
        if (cross < 0 && sw > 0) sw -= 360LL * IM_ONE;
        if (i64_abs(sw) > 180LL * IM_ONE) {
            sw = (sw > 0) ? (sw - 360LL * IM_ONE) : (sw + 360LL * IM_ONE);
        }
        return append_arc_pts(out, p.x, p.y, half_w, a0, sw, tol_q30);
    }
    /* IM_JOIN_MITER: miter_len / half_w = 1 / cos(theta/2) = sqrt(2 / (1 + dot)). */
    int64_t denom = IM_TR_ONE + dot;
    if (denom <= 1024) {
        /* Nearly 180-degree reversal: bevel fallback. */
        return pt_buf_push(out, p1x, p1y);
    }
    /* Bisector direction b = n0 + n1; scale so b . n0 = half_w -> factor = half_w / (1 + dot) in Q30. */
    int64_t bx = n0x + n1x, by = n0y + n1y;
    int64_t b_len = hypot_q30(bx, by);
    /* Miter ratio = 2 * IM_TR_ONE / b_len (since |n0+n1| = 2*cos(theta/2)). */
    if (b_len <= 1024) return pt_buf_push(out, p1x, p1y);
    int64_t miter_ratio_q30 = im_sdiv128_i64((__int128)(2LL * IM_TR_ONE) * IM_TR_ONE, b_len);
    if (miter_ratio_q30 > miter_lim_q30) {
        /* Exceeds miter limit: bevel fallback (WinUI/Direct2D). */
        return pt_buf_push(out, p1x, p1y);
    }
    int64_t scale_q30 = im_sdiv128_i64((__int128)half_w * IM_TR_ONE, denom);
    int64_t mx = p.x + q30_mul(bx, scale_q30);
    int64_t my = p.y + q30_mul(by, scale_q30);
    if (!pt_buf_push(out, mx, my)) return false;
    return pt_buf_push(out, p1x, p1y);
}

static bool emit_transformed_contour(subpath_list *dst, const pt_buf *local_poly, const im_transform *tr) {
    if (local_poly->count < 3u) return true;
    int64_t tx = 0, ty = 0;
    if (!transform_pt_opt(tr, local_poly->pts[0].x, local_poly->pts[0].y, &tx, &ty)) return false;
    if (!subpaths_begin(dst, tx, ty)) return false;
    for (uint32_t i = 1; i < local_poly->count; i++) {
        if (!transform_pt_opt(tr, local_poly->pts[i].x, local_poly->pts[i].y, &tx, &ty)) return false;
        if (!subpaths_add_pt(dst, tx, ty)) return false;
    }
    dst->subs[dst->count - 1u].closed = true;
    return true;
}

/* Expand one open or closed polyline (in local Q30) into stroke outline contour(s) in dst (transformed by tr). */
static bool expand_polyline_stroke(subpath_list *dst, im_allocator alloc,
                                   const pt_q30 *pts, uint32_t n, bool closed,
                                   int64_t half_w, im_line_cap start_cap, im_line_cap end_cap,
                                   im_line_join join, int64_t miter_lim_q30,
                                   const im_transform *tr, int64_t tol_q30) {
    if (n == 0u) return true;
    /* Deduplicate consecutive identical points. */
    pt_buf clean;
    pt_buf_init(&clean, alloc);
    if (!pt_buf_push(&clean, pts[0].x, pts[0].y)) { pt_buf_destroy(&clean); return false; }
    for (uint32_t i = 1; i < n; i++) {
        pt_q30 prev = clean.pts[clean.count - 1u];
        if (pts[i].x != prev.x || pts[i].y != prev.y) {
            if (!pt_buf_push(&clean, pts[i].x, pts[i].y)) { pt_buf_destroy(&clean); return false; }
        }
    }
    if (closed && clean.count > 1u &&
        clean.pts[clean.count - 1u].x == clean.pts[0].x &&
        clean.pts[clean.count - 1u].y == clean.pts[0].y) {
        clean.count--;
    }

    if (clean.count == 1u) {
        /* Zero-length segment (dot): draw start/end caps if Round/Square/Triangle. */
        bool ok = true;
        if (start_cap != IM_CAP_FLAT || end_cap != IM_CAP_FLAT) {
            pt_buf dot;
            pt_buf_init(&dot, alloc);
            ok = add_cap(&dot, clean.pts[0], IM_TR_ONE, 0, half_w, end_cap, tol_q30) &&
                 add_cap(&dot, clean.pts[0], -IM_TR_ONE, 0, half_w, start_cap, tol_q30) &&
                 emit_transformed_contour(dst, &dot, tr);
            pt_buf_destroy(&dot);
        }
        pt_buf_destroy(&clean);
        return ok;
    }

    uint32_t seg_count = closed ? clean.count : (clean.count - 1u);
    pt_buf tangents;
    pt_buf_init(&tangents, alloc);
    for (uint32_t i = 0; i < seg_count; i++) {
        pt_q30 a = clean.pts[i];
        pt_q30 b = clean.pts[(i + 1u) % clean.count];
        int64_t dx = b.x - a.x, dy = b.y - a.y;
        int64_t len = hypot_q30(dx, dy);
        if (len == 0) len = 1;
        int64_t tx = im_sdiv128_i64((__int128)dx * IM_TR_ONE, len);
        int64_t ty = im_sdiv128_i64((__int128)dy * IM_TR_ONE, len);
        if (!pt_buf_push(&tangents, tx, ty)) {
            pt_buf_destroy(&tangents);
            pt_buf_destroy(&clean);
            return false;
        }
    }

    bool ok = true;
    if (closed) {
        /* Closed polyline: emit two contours (left loop + reversed right loop) for clean NonZero fill. */
        pt_buf left_loop, right_loop;
        pt_buf_init(&left_loop, alloc);
        pt_buf_init(&right_loop, alloc);
        for (uint32_t i = 0; i < clean.count && ok; i++) {
            uint32_t prev_s = (i == 0u) ? (seg_count - 1u) : (i - 1u);
            uint32_t next_s = i;
            int64_t t0x = tangents.pts[prev_s].x, t0y = tangents.pts[prev_s].y;
            int64_t t1x = tangents.pts[next_s].x, t1y = tangents.pts[next_s].y;
            /* Left normals: (-ty, tx). */
            int64_t ln0x = -t0y, ln0y = t0x;
            int64_t ln1x = -t1y, ln1y = t1x;
            /* Right normals: (ty, -tx). */
            int64_t rn0x = t0y, rn0y = -t0x;
            int64_t rn1x = t1y, rn1y = -t1x;
            /* In screen coords (+X right, +Y down), nl = (-ty, tx) is clockwise (+90 deg) from t,
               so turn = cross(t0, t1) > 0 (clockwise turn) has nr = (ty, -tx) on the OUTER convex side. */
            int64_t turn = q30_mul(t0x, t1y) - q30_mul(t0y, t1x);
            if (turn >= 0) {
                ok = add_join_side(&left_loop, clean.pts[i], ln0x, ln0y, ln1x, ln1y, half_w, IM_JOIN_BEVEL, miter_lim_q30, tol_q30) &&
                     add_join_side(&right_loop, clean.pts[i], rn0x, rn0y, rn1x, rn1y, half_w, join, miter_lim_q30, tol_q30);
            } else {
                ok = add_join_side(&left_loop, clean.pts[i], ln0x, ln0y, ln1x, ln1y, half_w, join, miter_lim_q30, tol_q30) &&
                     add_join_side(&right_loop, clean.pts[i], rn0x, rn0y, rn1x, rn1y, half_w, IM_JOIN_BEVEL, miter_lim_q30, tol_q30);
            }
        }
        /* Reverse right_loop so its winding is opposite to left_loop (hollow interior under NonZero). */
        for (uint32_t i = 0; i < right_loop.count / 2u; i++) {
            pt_q30 tmp = right_loop.pts[i];
            right_loop.pts[i] = right_loop.pts[right_loop.count - 1u - i];
            right_loop.pts[right_loop.count - 1u - i] = tmp;
        }
        if (ok) ok = emit_transformed_contour(dst, &left_loop, tr);
        if (ok) ok = emit_transformed_contour(dst, &right_loop, tr);
        pt_buf_destroy(&left_loop);
        pt_buf_destroy(&right_loop);
    } else {
        /* Open polyline: walk forward along left side, end cap, backward along right side, start cap. */
        pt_buf poly;
        pt_buf_init(&poly, alloc);
        /* Forward left side (vertices 1 .. n-2). */
        for (uint32_t i = 1; i + 1u < clean.count && ok; i++) {
            int64_t t0x = tangents.pts[i - 1u].x, t0y = tangents.pts[i - 1u].y;
            int64_t t1x = tangents.pts[i].x, t1y = tangents.pts[i].y;
            int64_t turn = q30_mul(t0x, t1y) - q30_mul(t0y, t1x);
            im_line_join lj = (turn < 0) ? join : IM_JOIN_BEVEL;
            ok = add_join_side(&poly, clean.pts[i], -t0y, t0x, -t1y, t1x, half_w, lj, miter_lim_q30, tol_q30);
        }
        /* End cap at clean.pts[n-1]. */
        if (ok) {
            pt_q30 et = tangents.pts[seg_count - 1u];
            ok = add_cap(&poly, clean.pts[clean.count - 1u], et.x, et.y, half_w, end_cap, tol_q30);
        }
        /* Backward right side (vertices n-2 .. 1). */
        for (uint32_t i = clean.count - 1u; i > 1u && ok; i--) {
            uint32_t v = i - 1u;
            int64_t t0x = -tangents.pts[v].x, t0y = -tangents.pts[v].y;
            int64_t t1x = -tangents.pts[v - 1u].x, t1y = -tangents.pts[v - 1u].y;
            int64_t turn = q30_mul(t0x, t1y) - q30_mul(t0y, t1x);
            im_line_join rj = (turn < 0) ? join : IM_JOIN_BEVEL;
            ok = add_join_side(&poly, clean.pts[v], -t0y, t0x, -t1y, t1x, half_w, rj, miter_lim_q30, tol_q30);
        }
        /* Start cap at clean.pts[0] (outward tangent = -tangents[0]). */
        if (ok) {
            pt_q30 st = tangents.pts[0];
            ok = add_cap(&poly, clean.pts[0], -st.x, -st.y, half_w, start_cap, tol_q30);
        }
        if (ok) ok = emit_transformed_contour(dst, &poly, tr);
        pt_buf_destroy(&poly);
    }

    pt_buf_destroy(&tangents);
    pt_buf_destroy(&clean);
    return ok;
}

static bool expand_dashed_subpath(subpath_list *dst, im_allocator alloc,
                                  const pt_q30 *pts, uint32_t n, bool closed,
                                  const im_stroke_style *style, int64_t half_w,
                                  int64_t miter_lim_q30, const im_transform *tr, int64_t tol_q30) {
    if (n < 2u) return true;
    /* Build doubled pattern in Q30 units if odd count (WinUI/SVG convention). */
    uint32_t pat_n = (style->dash_count & 1u) ? (style->dash_count * 2u) : style->dash_count;
    size_t pat_bytes = (size_t)pat_n * sizeof(int64_t);
    int64_t *pat = (int64_t *)alloc.alloc(alloc.ctx, pat_bytes);
    if (!pat) return false;
    int64_t cycle = 0;
    int64_t thick_q30 = half_w * 2;
    for (uint32_t i = 0; i < pat_n; i++) {
        im_fixed v = style->dash_array[i % style->dash_count];
        int64_t len = (v > 0) ? q30_mul(im_fixed_to_q30(v), thick_q30) : 0;
        pat[i] = len;
        cycle += len;
    }
    if (cycle <= 0) {
        /* All-zero dash cycle: fallback to solid stroke (avoid infinite loop). */
        alloc.free(alloc.ctx, pat, pat_bytes);
        return expand_polyline_stroke(dst, alloc, pts, n, closed, half_w,
                                      style->start_cap, style->end_cap,
                                      style->line_join, miter_lim_q30, tr, tol_q30);
    }

    /* Normalize dash_offset * thickness modulo cycle. */
    int64_t off = q30_mul(im_fixed_to_q30(style->dash_offset), thick_q30);
    off %= cycle;
    if (off < 0) off += cycle;
    uint32_t pat_idx = 0u;
    while (off >= pat[pat_idx] && pat[pat_idx] > 0) {
        off -= pat[pat_idx];
        pat_idx = (pat_idx + 1u) % pat_n;
    }
    int64_t rem = pat[pat_idx] - off;
    bool drawing = ((pat_idx & 1u) == 0u);

    uint32_t seg_count = closed ? n : (n - 1u);
    pt_buf cur_dash;
    pt_buf_init(&cur_dash, alloc);
    bool ok = true;

    if (drawing) {
        ok = pt_buf_push(&cur_dash, pts[0].x, pts[0].y);
    }

    for (uint32_t si = 0; si < seg_count && ok; si++) {
        pt_q30 a = pts[si];
        pt_q30 b = pts[(si + 1u) % n];
        int64_t seg_len = hypot_q30(b.x - a.x, b.y - a.y);
        if (seg_len == 0) continue;
        int64_t pos = 0;
        while (pos < seg_len && ok) {
            if (rem == 0) {
                /* Zero-length dash/gap entry. */
                if (drawing) {
                    int64_t px = a.x + im_sdiv128_i64((__int128)(b.x - a.x) * pos, seg_len);
                    int64_t py = a.y + im_sdiv128_i64((__int128)(b.y - a.y) * pos, seg_len);
                    pt_q30 dot_pt = { px, py };
                    ok = expand_polyline_stroke(dst, alloc, &dot_pt, 1u, false, half_w,
                                                style->dash_cap, style->dash_cap,
                                                style->line_join, miter_lim_q30, tr, tol_q30);
                    cur_dash.count = 0u;
                }
                pat_idx = (pat_idx + 1u) % pat_n;
                drawing = ((pat_idx & 1u) == 0u);
                rem = pat[pat_idx];
                if (drawing) {
                    int64_t px = a.x + im_sdiv128_i64((__int128)(b.x - a.x) * pos, seg_len);
                    int64_t py = a.y + im_sdiv128_i64((__int128)(b.y - a.y) * pos, seg_len);
                    cur_dash.count = 0u;
                    ok = pt_buf_push(&cur_dash, px, py);
                }
                continue;
            }
            int64_t step = (seg_len - pos < rem) ? (seg_len - pos) : rem;
            pos += step;
            rem -= step;
            int64_t px = (pos == seg_len) ? b.x : (a.x + im_sdiv128_i64((__int128)(b.x - a.x) * pos, seg_len));
            int64_t py = (pos == seg_len) ? b.y : (a.y + im_sdiv128_i64((__int128)(b.y - a.y) * pos, seg_len));
            if (drawing) {
                ok = pt_buf_push(&cur_dash, px, py);
            }
            if (rem == 0) {
                if (drawing && cur_dash.count > 0u) {
                    ok = expand_polyline_stroke(dst, alloc, cur_dash.pts, cur_dash.count, false, half_w,
                                                style->dash_cap, style->dash_cap,
                                                style->line_join, miter_lim_q30, tr, tol_q30);
                    cur_dash.count = 0u;
                }
                pat_idx = (pat_idx + 1u) % pat_n;
                drawing = ((pat_idx & 1u) == 0u);
                rem = pat[pat_idx];
                if (drawing && pos < seg_len) {
                    ok = pt_buf_push(&cur_dash, px, py);
                }
            }
        }
    }

    if (ok && drawing && cur_dash.count > 0u) {
        ok = expand_polyline_stroke(dst, alloc, cur_dash.pts, cur_dash.count, false, half_w,
                                    style->dash_cap, style->dash_cap,
                                    style->line_join, miter_lim_q30, tr, tol_q30);
    }

    pt_buf_destroy(&cur_dash);
    alloc.free(alloc.ctx, pat, pat_bytes);
    return ok;
}

static bool stroke_path_impl(im_surface *surface, im_allocator alloc, const im_path_data *path,
                             const im_stroke_style *style, uint32_t solid_argb,
                             im_path_shader_fn shader, void *shader_ctx) {
    if (!surface || !path || !style || path->count == 0u || style->thickness <= 0) return true;
    if (!shader && (solid_argb >> 24) == 0u) return true;
    if (!alloc.alloc || !alloc.realloc || !alloc.free) return false;
    im_transform inv;
    if (surface->transform && !im_transform_invert(surface->transform, &inv)) return true;

    /* Scale local flatness tolerance down by max transform scale so device flatness stays <= 1/16 px. */
    int64_t local_tol = IM_TR_ONE / 16;
    if (surface->transform) {
        int64_t sx = hypot_q30(surface->transform->m11, surface->transform->m12);
        int64_t sy = hypot_q30(surface->transform->m21, surface->transform->m22);
        int64_t max_s = (sx > sy) ? sx : sy;
        if (max_s > IM_TR_ONE) {
            local_tol = im_sdiv128_i64((__int128)(IM_TR_ONE / 16) * IM_TR_ONE, max_s);
            if (local_tol < 1024) local_tol = 1024;
        }
    }

    subpath_list local_sl, outline_sl;
    subpaths_init(&local_sl, alloc);
    subpaths_init(&outline_sl, alloc);

    bool ok = flatten_path_to_subpaths(path, NULL, local_tol, &local_sl);
    int64_t half_w = im_fixed_to_q30(style->thickness) >> 1;
    int64_t miter_lim_q30 = (style->miter_limit > 0) ? im_fixed_to_q30(style->miter_limit) : (10LL * IM_TR_ONE);

    for (uint32_t si = 0; si < local_sl.count && ok; si++) {
        const subpath_rec *sr = &local_sl.subs[si];
        if (sr->count == 0u) continue;
        if (style->dash_array && style->dash_count > 0u) {
            ok = expand_dashed_subpath(&outline_sl, alloc, &local_sl.pts.pts[sr->start], sr->count,
                                       sr->closed, style, half_w, miter_lim_q30, surface->transform, local_tol);
        } else {
            ok = expand_polyline_stroke(&outline_sl, alloc, &local_sl.pts.pts[sr->start], sr->count,
                                        sr->closed, half_w, style->start_cap, style->end_cap,
                                        style->line_join, miter_lim_q30, surface->transform, local_tol);
        }
    }

    if (ok) {
        ok = rasterize_subpaths(surface, alloc, &outline_sl, IM_FILL_NON_ZERO, solid_argb, shader, shader_ctx);
    }

    subpaths_destroy(&outline_sl);
    subpaths_destroy(&local_sl);
    return ok;
}

bool im_path_stroke_argb(im_surface *surface, im_allocator alloc, const im_path_data *path,
                         const im_stroke_style *style, uint32_t argb) {
    return stroke_path_impl(surface, alloc, path, style, argb, NULL, NULL);
}

bool im_path_stroke_shaded(im_surface *surface, im_allocator alloc, const im_path_data *path,
                           const im_stroke_style *style, im_path_shader_fn shader, void *shader_ctx) {
    return stroke_path_impl(surface, alloc, path, style, 0xFFFFFFFFu, shader, shader_ctx);
}

bool im_path_draw_stretched(im_surface *surface, im_allocator alloc, const im_path_data *path,
                            im_rect dst_rect, int stretch_mode, uint32_t fill_argb,
                            const im_stroke_style *stroke_style, uint32_t stroke_argb) {
    if (!surface || !path || path->count == 0u) return true;
    int64_t dw = (int64_t)dst_rect.right - dst_rect.left;
    int64_t dh = (int64_t)dst_rect.bottom - dst_rect.top;
    if (dw <= 0 || dh <= 0) return true;

    im_rect pb;
    if (!im_path_bounds(path, &pb)) return true;
    int64_t pw = (int64_t)pb.right - pb.left;
    int64_t ph = (int64_t)pb.bottom - pb.top;

    im_transform fit = im_transform_identity();
    if (stretch_mode == 0 || pw <= 0 || ph <= 0) {
        /* Stretch=None */
        fit = im_transform_translate(dst_rect.left, dst_rect.top);
    } else {
        int64_t sx_q = im_sdiv128_i64((__int128)dw * IM_TR_ONE, pw);
        int64_t sy_q = im_sdiv128_i64((__int128)dh * IM_TR_ONE, ph);
        if (stretch_mode == 2) {
            /* Uniform */
            int64_t s = (sx_q < sy_q) ? sx_q : sy_q;
            sx_q = sy_q = s;
        } else if (stretch_mode == 3) {
            /* UniformToFill */
            int64_t s = (sx_q > sy_q) ? sx_q : sy_q;
            sx_q = sy_q = s;
        }
        int64_t scaled_w = q30_mul(im_fixed_to_q30((im_fixed)pw), sx_q);
        int64_t scaled_h = q30_mul(im_fixed_to_q30((im_fixed)ph), sy_q);
        int64_t ox = im_fixed_to_q30(dst_rect.left) + ((im_fixed_to_q30((im_fixed)dw) - scaled_w) >> 1) -
                     q30_mul(im_fixed_to_q30(pb.left), sx_q);
        int64_t oy = im_fixed_to_q30(dst_rect.top) + ((im_fixed_to_q30((im_fixed)dh) - scaled_h) >> 1) -
                     q30_mul(im_fixed_to_q30(pb.top), sy_q);
        fit = (im_transform){ sx_q, 0, 0, sy_q, ox, oy };
    }

    im_transform combined = fit;
    if (surface->transform) {
        if (!im_transform_multiply(&fit, surface->transform, &combined)) return false;
    }

    const im_transform *saved = surface->transform;
    surface->transform = &combined;
    bool ok = true;
    if ((fill_argb >> 24) != 0u) {
        ok = im_path_fill_argb(surface, alloc, path, fill_argb);
    }
    if (ok && stroke_style && stroke_style->thickness > 0 && (stroke_argb >> 24) != 0u) {
        ok = im_path_stroke_argb(surface, alloc, path, stroke_style, stroke_argb);
    }
    surface->transform = saved;
    return ok;
}
