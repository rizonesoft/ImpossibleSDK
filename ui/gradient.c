/* Impossible UI: fixed-point linear and radial gradients matching WinUI 3 brushes. Portable. */
#include "gradient.h"
#include "layer.h"

/* ---------- Freestanding integer math helpers ---------- */

static uint64_t isqrt64_g(uint64_t n) {
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

static uint64_t isqrt128_g(unsigned __int128 n) {
    if (n <= UINT64_MAX) return isqrt64_g((uint64_t)n);
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

static int64_t i64_abs_g(int64_t v) {
    return (v < 0) ? -v : v;
}

static int64_t q30_mul_g(int64_t a, int64_t b) {
    __int128 p = (__int128)a * b;
    __int128 half = (__int128)1 << (IM_TR_SHIFT - 1);
    if (p >= 0) return (int64_t)((p + half) >> IM_TR_SHIFT);
    return -(int64_t)(((-p) + half) >> IM_TR_SHIFT);
}

static uint32_t blend_direct_g(uint32_t bg, uint32_t argb, uint32_t coverage) {
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

/* ---------- Stop sampling (sRGB premultiplied interpolation matching WinUI Composition) ---------- */

static int64_t apply_spread(int64_t t_q30, im_spread_method spread) {
    if (spread == IM_SPREAD_PAD) {
        if (t_q30 < 0) return 0;
        if (t_q30 > IM_TR_ONE) return IM_TR_ONE;
        return t_q30;
    }
    if (spread == IM_SPREAD_REPEAT) {
        int64_t r = t_q30 % IM_TR_ONE;
        if (r < 0) r += IM_TR_ONE;
        return r;
    }
    /* IM_SPREAD_REFLECT */
    int64_t period = 2LL * IM_TR_ONE;
    int64_t r = t_q30 % period;
    if (r < 0) r += period;
    if (r > IM_TR_ONE) r = period - r;
    return r;
}

static uint32_t lerp_premul_srgb(uint32_t c0, uint32_t c1, uint32_t w_q16) {
    uint32_t inv = (uint32_t)IM_ONE - w_q16;
    uint32_t a0 = (c0 >> 24) & 0xFFu, a1 = (c1 >> 24) & 0xFFu;
    uint32_t r0 = ((c0 >> 16) & 0xFFu) * a0, r1 = ((c1 >> 16) & 0xFFu) * a1;
    uint32_t g0 = ((c0 >> 8) & 0xFFu) * a0, g1 = ((c1 >> 8) & 0xFFu) * a1;
    uint32_t b0 = (c0 & 0xFFu) * a0, b1 = (c1 & 0xFFu) * a1;

    uint32_t a_255 = (uint32_t)(((uint64_t)a0 * inv + (uint64_t)a1 * w_q16 + (IM_ONE / 2)) >> 16);
    if (a_255 == 0u) return 0x00000000u;

    /* Unpremultiply cleanly: pr = (r0*inv + r1*w) / 2^16, then straight = round(pr / a_255). */
    uint64_t a_q16 = (uint64_t)a0 * inv + (uint64_t)a1 * w_q16;
    uint64_t pr = (uint64_t)r0 * inv + (uint64_t)r1 * w_q16;
    uint64_t pg = (uint64_t)g0 * inv + (uint64_t)g1 * w_q16;
    uint64_t pb = (uint64_t)b0 * inv + (uint64_t)b1 * w_q16;

    uint32_t sr = (uint32_t)((pr + (a_q16 >> 1)) / a_q16);
    uint32_t sg = (uint32_t)((pg + (a_q16 >> 1)) / a_q16);
    uint32_t sb = (uint32_t)((pb + (a_q16 >> 1)) / a_q16);
    if (sr > 255u) sr = 255u;
    if (sg > 255u) sg = 255u;
    if (sb > 255u) sb = 255u;
    if (a_255 > 255u) a_255 = 255u;
    return (a_255 << 24) | (sr << 16) | (sg << 8) | sb;
}

uint32_t im_gradient_sample_stops(const im_gradient_stop *stops, uint32_t stop_count,
                                  im_spread_method spread, int64_t t_q30) {
    if (!stops || stop_count == 0u) return 0x00000000u;
    if (stop_count == 1u) return stops[0].argb;

    int64_t t = apply_spread(t_q30, spread);

    /* Find the bracketing stops in stable offset order without mutating or allocating.
       For unsorted stops or repeated offsets:
       - lo: largest stop offset <= t (breaking ties by LAST index so hard steps work)
       - hi: smallest stop offset >= t (breaking ties by FIRST index after lo). */
    int64_t lo_off = INT64_MIN, hi_off = INT64_MAX;
    uint32_t lo_idx = 0u, hi_idx = 0u;
    bool has_lo = false, has_hi = false;

    /* Also track global min (first) and global max (last) for clamping outside stop range. */
    int64_t min_off = INT64_MAX, max_off = INT64_MIN;
    uint32_t min_idx = 0u, max_idx = 0u;

    for (uint32_t i = 0; i < stop_count; i++) {
        int64_t o = im_fixed_to_q30(stops[i].offset);
        if (o < min_off) { min_off = o; min_idx = i; }
        if (o >= max_off) { max_off = o; max_idx = i; }
        if (o <= t) {
            if (!has_lo || o >= lo_off) { lo_off = o; lo_idx = i; has_lo = true; }
        }
        if (o >= t) {
            if (!has_hi || o < hi_off) { hi_off = o; hi_idx = i; has_hi = true; }
        }
    }

    if (!has_lo) return stops[min_idx].argb;
    if (!has_hi) return stops[max_idx].argb;
    if (lo_off == hi_off) return stops[lo_idx].argb;

    int64_t span = hi_off - lo_off;
    int64_t w = im_sdiv128_i64((__int128)(t - lo_off) * IM_ONE, span);
    if (w <= 0) return stops[lo_idx].argb;
    if (w >= IM_ONE) return stops[hi_idx].argb;
    return lerp_premul_srgb(stops[lo_idx].argb, stops[hi_idx].argb, (uint32_t)w);
}

/* ---------- Linear & Radial evaluation ---------- */

im_radial_gradient im_radial_gradient_default(const im_gradient_stop *stops, uint32_t stop_count) {
    im_radial_gradient g;
    g.center_x = IM_ONE / 2;
    g.center_y = IM_ONE / 2;
    g.radius_x = IM_ONE / 2;
    g.radius_y = IM_ONE / 2;
    g.origin_x = IM_ONE / 2;
    g.origin_y = IM_ONE / 2;
    g.mapping = IM_MAPPING_RELATIVE;
    g.spread = IM_SPREAD_PAD;
    g.space = IM_GRAD_SPACE_SRGB;
    g.stops = stops;
    g.stop_count = stop_count;
    return g;
}

static void map_point_to_local_q30(im_fixed px, im_fixed py, im_mapping_mode mapping,
                                   im_rect local_bounds, im_fixed scale,
                                   int64_t *ox_q30, int64_t *oy_q30) {
    if (mapping == IM_MAPPING_RELATIVE) {
        int64_t bl = im_fixed_to_q30(local_bounds.left);
        int64_t bt = im_fixed_to_q30(local_bounds.top);
        int64_t bw = im_fixed_to_q30(local_bounds.right) - bl;
        int64_t bh = im_fixed_to_q30(local_bounds.bottom) - bt;
        *ox_q30 = bl + q30_mul_g(bw, im_fixed_to_q30(px));
        *oy_q30 = bt + q30_mul_g(bh, im_fixed_to_q30(py));
    } else {
        int64_t sc = (scale > 0) ? im_fixed_to_q30(scale) : IM_TR_ONE;
        *ox_q30 = q30_mul_g(im_fixed_to_q30(px), sc);
        *oy_q30 = q30_mul_g(im_fixed_to_q30(py), sc);
    }
}

static void map_radius_to_local_q30(im_fixed rx, im_fixed ry, im_mapping_mode mapping,
                                    im_rect local_bounds, im_fixed scale,
                                    int64_t *orx_q30, int64_t *ory_q30) {
    if (mapping == IM_MAPPING_RELATIVE) {
        int64_t bw = im_fixed_to_q30(local_bounds.right) - im_fixed_to_q30(local_bounds.left);
        int64_t bh = im_fixed_to_q30(local_bounds.bottom) - im_fixed_to_q30(local_bounds.top);
        *orx_q30 = q30_mul_g(bw, im_fixed_to_q30(rx));
        *ory_q30 = q30_mul_g(bh, im_fixed_to_q30(ry));
    } else {
        int64_t sc = (scale > 0) ? im_fixed_to_q30(scale) : IM_TR_ONE;
        *orx_q30 = q30_mul_g(im_fixed_to_q30(rx), sc);
        *ory_q30 = q30_mul_g(im_fixed_to_q30(ry), sc);
    }
}

uint32_t im_linear_gradient_eval(const im_linear_gradient *grad, im_rect local_bounds, im_fixed scale,
                                 int64_t lx_q30, int64_t ly_q30) {
    if (!grad || !grad->stops || grad->stop_count == 0u) return 0x00000000u;
    if (grad->stop_count == 1u) return grad->stops[0].argb;

    int64_t sx = 0, sy = 0, ex = 0, ey = 0;
    map_point_to_local_q30(grad->start_x, grad->start_y, grad->mapping, local_bounds, scale, &sx, &sy);
    map_point_to_local_q30(grad->end_x, grad->end_y, grad->mapping, local_bounds, scale, &ex, &ey);

    int64_t vx = ex - sx;
    int64_t vy = ey - sy;
    if (vx == 0 && vy == 0) {
        /* Coincident linear endpoints: t = 1 (last stop). */
        return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, IM_TR_ONE);
    }

    int64_t dx = lx_q30 - sx;
    int64_t dy = ly_q30 - sy;
    /* Normalize (vx, vy, dx, dy) so dot products never overflow int64_t denominator or __int128 numerator. */
    while (i64_abs_g(vx) > ((int64_t)1 << 30) || i64_abs_g(vy) > ((int64_t)1 << 30) ||
           i64_abs_g(dx) > ((int64_t)1 << 40) || i64_abs_g(dy) > ((int64_t)1 << 40)) {
        vx >>= 1;
        vy >>= 1;
        dx >>= 1;
        dy >>= 1;
    }
    int64_t len2 = (int64_t)((__int128)vx * vx + (__int128)vy * vy);
    if (len2 == 0) {
        return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, IM_TR_ONE);
    }
    __int128 dot = (__int128)dx * vx + (__int128)dy * vy;
    int64_t t_q30 = im_sdiv128_i64(dot * IM_TR_ONE, len2);
    return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, t_q30);
}

uint32_t im_radial_gradient_eval(const im_radial_gradient *grad, im_rect local_bounds, im_fixed scale,
                                 int64_t lx_q30, int64_t ly_q30) {
    if (!grad || !grad->stops || grad->stop_count == 0u) return 0x00000000u;
    if (grad->stop_count == 1u) return grad->stops[0].argb;

    int64_t cx = 0, cy = 0, ox = 0, oy = 0, rx = 0, ry = 0;
    map_point_to_local_q30(grad->center_x, grad->center_y, grad->mapping, local_bounds, scale, &cx, &cy);
    map_point_to_local_q30(grad->origin_x, grad->origin_y, grad->mapping, local_bounds, scale, &ox, &oy);
    map_radius_to_local_q30(grad->radius_x, grad->radius_y, grad->mapping, local_bounds, scale, &rx, &ry);

    if (rx <= 0 || ry <= 0) {
        /* Degenerate zero radius: t = 1 (last stop). */
        return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, IM_TR_ONE);
    }

    /* Transform into unit-circle coordinates centered at (cx, cy):
       P = ((lx - cx) / rx, (ly - cy) / ry) in Q30
       F = ((ox - cx) / rx, (oy - cy) / ry) in Q30 (WinUI GradientOrigin - Center offset). */
    int64_t px = im_sdiv128_i64((__int128)(lx_q30 - cx) * IM_TR_ONE, rx);
    int64_t py = im_sdiv128_i64((__int128)(ly_q30 - cy) * IM_TR_ONE, ry);
    int64_t fx = im_sdiv128_i64((__int128)(ox - cx) * IM_TR_ONE, rx);
    int64_t fy = im_sdiv128_i64((__int128)(oy - cy) * IM_TR_ONE, ry);

    /* Concentric fast path (F == 0): t = sqrt(px^2 + py^2). */
    if (fx == 0 && fy == 0) {
        int shift = 0;
        int64_t ux = i64_abs_g(px), uy = i64_abs_g(py);
        while (ux > ((int64_t)1 << 58) || uy > ((int64_t)1 << 58)) {
            ux >>= 1;
            uy >>= 1;
            shift++;
        }
        unsigned __int128 sq = (unsigned __int128)ux * ux + (unsigned __int128)uy * uy;
        int64_t t_q30 = (int64_t)(isqrt128_g(sq) << shift);
        return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, t_q30);
    }

    /* Clamp focus F strictly inside unit circle (|F| <= 0.999) for Direct2D/WinUI stability. */
    {
        int shift = 0;
        int64_t afx = i64_abs_g(fx), afy = i64_abs_g(fy);
        while (afx > ((int64_t)1 << 58) || afy > ((int64_t)1 << 58)) {
            afx >>= 1;
            afy >>= 1;
            shift++;
        }
        unsigned __int128 fsq = (unsigned __int128)afx * afx + (unsigned __int128)afy * afy;
        int64_t flen = (int64_t)(isqrt128_g(fsq) << shift);
        const int64_t max_f = (IM_TR_ONE * 999LL) / 1000LL;
        if (flen > max_f) {
            fx = im_sdiv128_i64((__int128)fx * max_f, flen);
            fy = im_sdiv128_i64((__int128)fy * max_f, flen);
        }
    }

    /* Solve |(F + (P - F)/t)|^2 = 1 for t > 0:
       Let D = P - F.
       |F*t + D|^2 = t^2  =>  (1 - |F|^2)*t^2 - 2*(F.D)*t - |D|^2 = 0
       Wait: with Q(u) = F + u*D on unit circle (u = 1/t):
       |F + u*D|^2 = 1  =>  |D|^2*u^2 + 2*(F.D)*u + (|F|^2 - 1) = 0
       Since |F| < 1, C = 1 - |F|^2 > 0, and:
       t = |D|^2 / (sqrt((F.D)^2 + |D|^2 * (1 - |F|^2)) - (F.D))
       (rationalized: t = (sqrt((F.D)^2 + |D|^2 * (1 - |F|^2)) + (F.D)) / (1 - |F|^2) is ALWAYS positive and exact!) */
    int64_t dx = px - fx;
    int64_t dy = py - fy;
    if (dx == 0 && dy == 0) {
        return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, 0);
    }

    /* Normalize D = (dx, dy) by power-of-2 shift so |D| <= 2^28 (zero overflow in __int128!). */
    int d_shift = 0;
    int64_t dsx = dx, dsy = dy;
    while (i64_abs_g(dsx) > ((int64_t)1 << 28) || i64_abs_g(dsy) > ((int64_t)1 << 28)) {
        dsx >>= 1;
        dsy >>= 1;
        d_shift++;
    }
    /* All terms below are in Q30 (|F| < 1 -> <= 2^30, |Ds| <= 2^28):
       f2_q30 = |F|^2 in Q30 (< IM_TR_ONE)
       one_minus_f2 = IM_TR_ONE - f2_q30 (> 0)
       fd_q30 = (fx*dsx + fy*dsy) >> 30 in Q30
       d2_q30 = (dsx*dsx + dsy*dsy) >> 30 in Q30
       disc_q60 = fd_q30^2 + d2_q30 * one_minus_f2 (fits well inside unsigned __int128!) */
    int64_t f2_q30 = q30_mul_g(fx, fx) + q30_mul_g(fy, fy);
    int64_t one_m_f2 = IM_TR_ONE - f2_q30;
    if (one_m_f2 <= 0) one_m_f2 = 1;

    int64_t fd_q30 = q30_mul_g(fx, dsx) + q30_mul_g(fy, dsy);
    int64_t d2_q30 = q30_mul_g(dsx, dsx) + q30_mul_g(dsy, dsy);

    unsigned __int128 disc_q60 = (unsigned __int128)i64_abs_g(fd_q30) * (unsigned __int128)i64_abs_g(fd_q30) +
                                 (unsigned __int128)d2_q30 * (unsigned __int128)one_m_f2;
    int64_t root_q30 = (int64_t)isqrt128_g(disc_q60);
    int64_t num_q30 = root_q30 + fd_q30;
    if (num_q30 < 0) num_q30 = 0;

    int64_t t_q30 = im_sdiv128_i64((__int128)num_q30 * IM_TR_ONE, one_m_f2);
    if (d_shift > 0) {
        if (t_q30 > (INT64_MAX >> d_shift)) t_q30 = INT64_MAX;
        else t_q30 <<= d_shift;
    }
    return im_gradient_sample_stops(grad->stops, grad->stop_count, grad->spread, t_q30);
}

/* ---------- Rounded-rect and path fill/stroke entry points ---------- */

typedef struct grad_shader_ctx {
    const im_linear_gradient *lin;
    const im_radial_gradient *rad;
    im_rect local_bounds;
    im_fixed scale;
} grad_shader_ctx;

static uint32_t grad_shader_cb(void *vctx, int64_t lx_q30, int64_t ly_q30) {
    const grad_shader_ctx *c = (const grad_shader_ctx *)vctx;
    if (c->lin) return im_linear_gradient_eval(c->lin, c->local_bounds, c->scale, lx_q30, ly_q30);
    if (c->rad) return im_radial_gradient_eval(c->rad, c->local_bounds, c->scale, lx_q30, ly_q30);
    return 0x00000000u;
}

static void fill_rect_gradient_impl(im_surface *surface, im_rect rect, im_radii radii,
                                    const im_linear_gradient *lin, const im_radial_gradient *rad) {
    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0) return;
    if (rect.left >= rect.right || rect.top >= rect.bottom) return;

    int bound_l = 0, bound_t = 0, bound_r = surface->width, bound_b = surface->height;
    if (surface->layers) {
        im_layer_draw_bounds(surface->layers, &bound_l, &bound_t, &bound_r, &bound_b);
    }
    if (bound_l >= bound_r || bound_t >= bound_b) return;

    im_transform inv;
    bool has_tr = (surface->transform != NULL);
    if (has_tr && !im_transform_invert(surface->transform, &inv)) return;

    int from = 0, top = 0, to = 0, bottom = 0;
    if (!im_transform_viewport_bounds_q30(surface->transform,
            im_fixed_to_q30(rect.left), im_fixed_to_q30(rect.top),
            im_fixed_to_q30(rect.right), im_fixed_to_q30(rect.bottom),
            bound_l, bound_t, bound_r, bound_b, &from, &top, &to, &bottom)) {
        return;
    }

    im_fixed sc = (surface->scale > 0) ? surface->scale : IM_ONE;
    for (int y = top; y < bottom; y++) {
        uint32_t *line = surface->pixels + (size_t)y * (size_t)surface->stride;
        int64_t py_c = (int64_t)y * IM_TR_ONE + (IM_TR_ONE >> 1);
        for (int x = from; x < to; x++) {
            int64_t px_c = (int64_t)x * IM_TR_ONE + (IM_TR_ONE >> 1);
            uint32_t cov = 0u;
            int64_t lx = px_c, ly = py_c;
            if (!has_tr) {
                cov = im_shape_coverage(rect, radii, x, y);
            } else {
                if (!im_transform_point_q30(&inv, px_c, py_c, &lx, &ly)) continue;
                /* 4x4 supersampled coverage under transform. */
                uint32_t hits = 0u;
                for (int sy = 0; sy < 4; sy++) {
                    int64_t spy = (int64_t)y * IM_TR_ONE + ((int64_t)(2 * sy + 1) * (IM_TR_ONE / 8));
                    for (int sx = 0; sx < 4; sx++) {
                        int64_t spx = (int64_t)x * IM_TR_ONE + ((int64_t)(2 * sx + 1) * (IM_TR_ONE / 8));
                        int64_t slx = 0, sly = 0;
                        if (!im_transform_point_q30(&inv, spx, spy, &slx, &sly)) continue;
                        if (slx >= im_fixed_to_q30(rect.left) && slx < im_fixed_to_q30(rect.right) &&
                            sly >= im_fixed_to_q30(rect.top) && sly < im_fixed_to_q30(rect.bottom)) {
                            hits++;
                        }
                    }
                }
                cov = (hits * (uint32_t)IM_ONE) / 16u;
            }
            if (cov == 0u) continue;
            uint32_t col = lin ? im_linear_gradient_eval(lin, rect, sc, lx, ly)
                               : im_radial_gradient_eval(rad, rect, sc, lx, ly);
            if ((col >> 24) == 0u) continue;
            if (surface->layers) {
                im_layer_blend_pixel(surface->layers, x, y, col, cov);
            } else {
                line[x] = blend_direct_g(line[x], col, cov);
            }
        }
    }
}

void im_gradient_fill_rect_linear(im_surface *surface, im_rect rect, im_radii radii,
                                  const im_linear_gradient *grad) {
    if (!grad) return;
    fill_rect_gradient_impl(surface, rect, radii, grad, NULL);
}

void im_gradient_fill_rect_radial(im_surface *surface, im_rect rect, im_radii radii,
                                  const im_radial_gradient *grad) {
    if (!grad) return;
    fill_rect_gradient_impl(surface, rect, radii, NULL, grad);
}

bool im_gradient_fill_path_linear(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                  const im_linear_gradient *grad) {
    if (!surface || !path || !grad || path->count == 0u) return true;
    im_rect bounds;
    if (!im_path_bounds(path, &bounds)) return true;
    grad_shader_ctx ctx = { grad, NULL, bounds, (surface->scale > 0) ? surface->scale : IM_ONE };
    return im_path_fill_shaded(surface, alloc, path, grad_shader_cb, &ctx);
}

bool im_gradient_fill_path_radial(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                  const im_radial_gradient *grad) {
    if (!surface || !path || !grad || path->count == 0u) return true;
    im_rect bounds;
    if (!im_path_bounds(path, &bounds)) return true;
    grad_shader_ctx ctx = { NULL, grad, bounds, (surface->scale > 0) ? surface->scale : IM_ONE };
    return im_path_fill_shaded(surface, alloc, path, grad_shader_cb, &ctx);
}

bool im_gradient_stroke_path_linear(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                    const im_stroke_style *style, const im_linear_gradient *grad) {
    if (!surface || !path || !style || !grad || path->count == 0u) return true;
    im_rect bounds;
    if (!im_path_bounds(path, &bounds)) return true;
    grad_shader_ctx ctx = { grad, NULL, bounds, (surface->scale > 0) ? surface->scale : IM_ONE };
    return im_path_stroke_shaded(surface, alloc, path, style, grad_shader_cb, &ctx);
}

bool im_gradient_stroke_path_radial(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                    const im_stroke_style *style, const im_radial_gradient *grad) {
    if (!surface || !path || !style || !grad || path->count == 0u) return true;
    im_rect bounds;
    if (!im_path_bounds(path, &bounds)) return true;
    grad_shader_ctx ctx = { NULL, grad, bounds, (surface->scale > 0) ? surface->scale : IM_ONE };
    return im_path_stroke_shaded(surface, alloc, path, style, grad_shader_cb, &ctx);
}
