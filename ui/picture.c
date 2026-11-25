/* Impossible UI: premultiplied picture drawing with bilinear filtering and nine-slice. Portable. */
#include "picture.h"
#include "layer.h"

static int64_t sdiv128_i64(__int128 num, int64_t den) {
    if (den == 0) return 0;
    bool neg = false;
    unsigned __int128 un, ud;
    if (num < 0) { un = (unsigned __int128)(-(num + 1)) + 1u; neg = !neg; } else { un = (unsigned __int128)num; }
    if (den < 0) { ud = (unsigned __int128)(-(den + 1)) + 1u; neg = !neg; } else { ud = (unsigned __int128)den; }
    unsigned __int128 q = 0;
    if (un <= UINT64_MAX && ud <= UINT64_MAX) {
        q = (uint64_t)un / (uint64_t)ud;
    } else {
        unsigned __int128 r = 0;
        for (int i = 127; i >= 0; i--) {
            r = (r << 1) | ((un >> i) & 1u);
            if (r >= ud) { r -= ud; q |= ((unsigned __int128)1 << i); }
        }
    }
    return neg ? -(int64_t)q : (int64_t)q;
}

static bool picture_valid(const im_picture *pic) {
    return pic && pic->pixels && pic->width > 0 && pic->height > 0 && pic->stride_pixels >= pic->width;
}

static uint32_t blend_premul_opaque(uint32_t dst_rgb, uint32_t premul_argb) {
    uint32_t sa = (premul_argb >> 24) & 255u;
    uint32_t sr = (premul_argb >> 16) & 255u;
    uint32_t sg = (premul_argb >> 8) & 255u;
    uint32_t sb = premul_argb & 255u;
    if (sa == 0 && sr == 0 && sg == 0 && sb == 0) return dst_rgb;
    uint32_t inv_a = 255u - sa;
    uint32_t dr = (dst_rgb >> 16) & 255u;
    uint32_t dg = (dst_rgb >> 8) & 255u;
    uint32_t db = dst_rgb & 255u;
    uint32_t out_r = sr + (dr * inv_a + 127u) / 255u;
    uint32_t out_g = sg + (dg * inv_a + 127u) / 255u;
    uint32_t out_b = sb + (db * inv_a + 127u) / 255u;
    if (out_r > 255u) out_r = 255u;
    if (out_g > 255u) out_g = 255u;
    if (out_b > 255u) out_b = 255u;
    return (out_r << 16) | (out_g << 8) | out_b;
}

/* Sample premultiplied 0xAARRGGBB at Q16 coordinates (u_fx, v_fx) clamped to [min_x..max_x] x [min_y..max_y]. */
static uint32_t sample_bilinear_clamped(const im_picture *pic, int64_t u_fx, int64_t v_fx,
                                        int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y) {
    int64_t lo_x = (int64_t)min_x << 16;
    int64_t hi_x = (int64_t)max_x << 16;
    int64_t lo_y = (int64_t)min_y << 16;
    int64_t hi_y = (int64_t)max_y << 16;
    if (u_fx < lo_x) u_fx = lo_x;
    if (u_fx > hi_x) u_fx = hi_x;
    if (v_fx < lo_y) v_fx = lo_y;
    if (v_fx > hi_y) v_fx = hi_y;

    int32_t x0 = (int32_t)(u_fx >> 16);
    int32_t y0 = (int32_t)(v_fx >> 16);
    int32_t x1 = x0 + 1;
    int32_t y1 = y0 + 1;
    if (x1 > max_x) x1 = max_x;
    if (y1 > max_y) y1 = max_y;

    uint32_t fx = (uint32_t)(u_fx & 0xFFFF);
    uint32_t fy = (uint32_t)(v_fx & 0xFFFF);
    uint32_t wx0 = (uint32_t)IM_ONE - fx;
    uint32_t wx1 = fx;
    uint32_t wy0 = (uint32_t)IM_ONE - fy;
    uint32_t wy1 = fy;

    const uint32_t *row0 = pic->pixels + (size_t)y0 * (size_t)pic->stride_pixels;
    const uint32_t *row1 = pic->pixels + (size_t)y1 * (size_t)pic->stride_pixels;
    uint32_t p00 = row0[x0], p10 = row0[x1];
    uint32_t p01 = row1[x0], p11 = row1[x1];

    uint32_t out = 0u;
    for (int shift = 0; shift < 32; shift += 8) {
        uint64_t top = (uint64_t)((p00 >> shift) & 255u) * wx0 + (uint64_t)((p10 >> shift) & 255u) * wx1;
        uint64_t bot = (uint64_t)((p01 >> shift) & 255u) * wx0 + (uint64_t)((p11 >> shift) & 255u) * wx1;
        uint64_t val = (top * wy0 + bot * wy1 + ((uint64_t)1 << 31)) >> 32;
        if (val > 255u) val = 255u;
        out |= ((uint32_t)val << shift);
    }
    return out;
}

static void put_premul_pixel(im_surface *surface, int x, int y, uint32_t premul_argb) {
    if (surface->layers) {
        im_layer_blend_premul_pixel(surface->layers, x, y, premul_argb, IM_ONE);
    } else {
        uint32_t *dp = surface->pixels + (size_t)y * (size_t)surface->stride + (size_t)x;
        *dp = blend_premul_opaque(*dp, premul_argb);
    }
}

static bool draw_subrect(im_surface *surface, const im_picture *pic,
                         im_rect clip_rect, im_rect map_rect,
                         int32_t src_l, int32_t src_t, int32_t src_r, int32_t src_b) {
    if (clip_rect.left >= clip_rect.right || clip_rect.top >= clip_rect.bottom) return true;
    if (map_rect.left >= map_rect.right || map_rect.top >= map_rect.bottom) return true;
    if (src_l >= src_r || src_t >= src_b) return true;

    int bound_l = 0, bound_t = 0, bound_r = surface->width, bound_b = surface->height;
    if (surface->layers) {
        im_layer_draw_bounds(surface->layers, &bound_l, &bound_t, &bound_r, &bound_b);
        if (bound_l >= bound_r || bound_t >= bound_b) return true;
    }

    im_transform inv;
    bool has_tr = (surface->transform != NULL);
    im_rect dev_bounds = clip_rect;
    if (has_tr) {
        if (!im_transform_invert(surface->transform, &inv)) return false;
        if (!im_transform_rect_bounds(surface->transform, clip_rect, &dev_bounds)) return false;
    }

    int64_t fl = (int64_t)dev_bounds.left >> 16;
    int64_t ft = (int64_t)dev_bounds.top >> 16;
    int64_t fr = ((int64_t)dev_bounds.right + IM_ONE - 1) >> 16;
    int64_t fb = ((int64_t)dev_bounds.bottom + IM_ONE - 1) >> 16;
    if (fl < bound_l) fl = bound_l;
    if (ft < bound_t) ft = bound_t;
    if (fr > bound_r) fr = bound_r;
    if (fb > bound_b) fb = bound_b;
    if (fl >= fr || ft >= fb) return true;

    int64_t map_w = (int64_t)map_rect.right - map_rect.left;
    int64_t map_h = (int64_t)map_rect.bottom - map_rect.top;
    int64_t src_w = (int64_t)(src_r - src_l);
    int64_t src_h = (int64_t)(src_b - src_t);
    int64_t clip_l_q30 = im_fixed_to_q30(clip_rect.left);
    int64_t clip_t_q30 = im_fixed_to_q30(clip_rect.top);
    int64_t clip_r_q30 = im_fixed_to_q30(clip_rect.right);
    int64_t clip_b_q30 = im_fixed_to_q30(clip_rect.bottom);
    int64_t map_l_q30 = im_fixed_to_q30(map_rect.left);
    int64_t map_t_q30 = im_fixed_to_q30(map_rect.top);
    int64_t map_w_q30 = map_w << (IM_TR_SHIFT - 16);
    int64_t map_h_q30 = map_h << (IM_TR_SHIFT - 16);

    for (int y = (int)ft; y < (int)fb; y++) {
        int64_t py_q30 = ((int64_t)y << IM_TR_SHIFT) + (IM_TR_ONE >> 1);
        for (int x = (int)fl; x < (int)fr; x++) {
            int64_t px_q30 = ((int64_t)x << IM_TR_SHIFT) + (IM_TR_ONE >> 1);
            int64_t lx_q30 = px_q30, ly_q30 = py_q30;
            if (has_tr) {
                if (!im_transform_point_q30(&inv, px_q30, py_q30, &lx_q30, &ly_q30)) continue;
            }
            if (lx_q30 < clip_l_q30 || lx_q30 >= clip_r_q30 ||
                ly_q30 < clip_t_q30 || ly_q30 >= clip_b_q30) {
                continue;
            }
            /* Half-pixel center mapping: u = src_l + (lx - map_l) * src_w / map_w - 0.5 */
            __int128 rel_x = (__int128)(lx_q30 - map_l_q30) * (src_w << 16);
            __int128 rel_y = (__int128)(ly_q30 - map_t_q30) * (src_h << 16);
            int64_t u_fx = ((int64_t)src_l << 16) + sdiv128_i64(rel_x, map_w_q30) - (IM_ONE / 2);
            int64_t v_fx = ((int64_t)src_t << 16) + sdiv128_i64(rel_y, map_h_q30) - (IM_ONE / 2);
            uint32_t sample = sample_bilinear_clamped(pic, u_fx, v_fx, src_l, src_r - 1, src_t, src_b - 1);
            put_premul_pixel(surface, x, y, sample);
        }
    }
    return true;
}

bool im_picture_premultiply_straight(const uint8_t *src_bytes, int32_t width, int32_t height,
                                     uint32_t src_stride_bytes, bool is_rgba,
                                     uint32_t *dst_pixels, int32_t dst_stride_pixels) {
    if (!src_bytes || !dst_pixels || width <= 0 || height <= 0) return false;
    if ((uint64_t)src_stride_bytes < (uint64_t)width * 4u) return false;
    if (dst_stride_pixels < width) return false;
    for (int32_t y = 0; y < height; y++) {
        const uint8_t *sr = src_bytes + (size_t)y * (size_t)src_stride_bytes;
        uint32_t *dr = dst_pixels + (size_t)y * (size_t)dst_stride_pixels;
        for (int32_t x = 0; x < width; x++) {
            uint32_t r = is_rgba ? sr[x * 4 + 0] : sr[x * 4 + 2];
            uint32_t g = sr[x * 4 + 1];
            uint32_t b = is_rgba ? sr[x * 4 + 2] : sr[x * 4 + 0];
            uint32_t a = sr[x * 4 + 3];
            uint32_t pr = (r * a + 127u) / 255u;
            uint32_t pg = (g * a + 127u) / 255u;
            uint32_t pb = (b * a + 127u) / 255u;
            dr[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
        }
    }
    return true;
}

bool im_picture_draw(im_surface *surface, const im_picture *pic, im_rect dst_rect, im_stretch stretch) {
    if (!surface || !surface->pixels || !picture_valid(pic)) return false;
    if (dst_rect.left >= dst_rect.right || dst_rect.top >= dst_rect.bottom) return false;

    int64_t dw = (int64_t)dst_rect.right - dst_rect.left;
    int64_t dh = (int64_t)dst_rect.bottom - dst_rect.top;
    int64_t sw = (int64_t)pic->width;
    int64_t sh = (int64_t)pic->height;
    int64_t scale = surface->scale > 0 ? (int64_t)surface->scale : (int64_t)IM_ONE;

    int64_t map_w = dw, map_h = dh;
    if (stretch == IM_STRETCH_NONE) {
        map_w = sw * scale;
        map_h = sh * scale;
    } else if (stretch == IM_STRETCH_UNIFORM) {
        /* Compare dw / sw vs dh / sh via cross-multiplication dw * sh vs dh * sw. */
        if ((__int128)dw * sh <= (__int128)dh * sw) {
            map_w = dw;
            map_h = sdiv128_i64((__int128)dw * sh + (sw / 2), sw);
        } else {
            map_h = dh;
            map_w = sdiv128_i64((__int128)dh * sw + (sh / 2), sh);
        }
    } else if (stretch == IM_STRETCH_UNIFORM_TO_FILL) {
        if ((__int128)dw * sh >= (__int128)dh * sw) {
            map_w = dw;
            map_h = sdiv128_i64((__int128)dw * sh + (sw / 2), sw);
        } else {
            map_h = dh;
            map_w = sdiv128_i64((__int128)dh * sw + (sh / 2), sh);
        }
    }

    int64_t ml = (int64_t)dst_rect.left + (dw - map_w) / 2;
    int64_t mt = (int64_t)dst_rect.top + (dh - map_h) / 2;
    int64_t mr = ml + map_w;
    int64_t mb = mt + map_h;

    int64_t cl = ml > dst_rect.left ? ml : dst_rect.left;
    int64_t ct = mt > dst_rect.top ? mt : dst_rect.top;
    int64_t cr = mr < dst_rect.right ? mr : dst_rect.right;
    int64_t cb = mb < dst_rect.bottom ? mb : dst_rect.bottom;
    if (cl < INT32_MIN || ct < INT32_MIN || cr > INT32_MAX || cb > INT32_MAX) return false;
    if (ml < INT32_MIN || mt < INT32_MIN || mr > INT32_MAX || mb > INT32_MAX) return false;

    im_rect clip_rect = {(im_fixed)cl, (im_fixed)ct, (im_fixed)cr, (im_fixed)cb};
    im_rect map_rect = {(im_fixed)ml, (im_fixed)mt, (im_fixed)mr, (im_fixed)mb};
    return draw_subrect(surface, pic, clip_rect, map_rect, 0, 0, pic->width, pic->height);
}

static int64_t round_scale_px(int32_t src_px, int64_t scale_fx) {
    return (((int64_t)src_px * scale_fx + (IM_ONE / 2)) >> 16) << 16;
}

bool im_picture_draw_nine_slice(im_surface *surface, const im_picture *pic, im_rect dst_rect, im_nine_slice slice) {
    if (!surface || !surface->pixels || !picture_valid(pic)) return false;
    if (dst_rect.left >= dst_rect.right || dst_rect.top >= dst_rect.bottom) return false;

    int32_t sl = slice.left < 0 ? 0 : slice.left;
    int32_t st = slice.top < 0 ? 0 : slice.top;
    int32_t sr = slice.right < 0 ? 0 : slice.right;
    int32_t sb = slice.bottom < 0 ? 0 : slice.bottom;

    if ((int64_t)sl + sr > pic->width) {
        int64_t sum = (int64_t)sl + sr;
        sl = (int32_t)(((int64_t)sl * pic->width) / sum);
        sr = pic->width - sl;
    }
    if ((int64_t)st + sb > pic->height) {
        int64_t sum = (int64_t)st + sb;
        st = (int32_t)(((int64_t)st * pic->height) / sum);
        sb = pic->height - st;
    }

    int64_t scale = surface->scale > 0 ? (int64_t)surface->scale : (int64_t)IM_ONE;
    int64_t dw = (int64_t)dst_rect.right - dst_rect.left;
    int64_t dh = (int64_t)dst_rect.bottom - dst_rect.top;

    int64_t dl = round_scale_px(sl, scale);
    int64_t dr = round_scale_px(sr, scale);
    int64_t dt = round_scale_px(st, scale);
    int64_t db = round_scale_px(sb, scale);

    if (dl + dr > dw && (dl + dr) > 0) {
        int64_t sum = dl + dr;
        dl = sdiv128_i64((__int128)dl * dw, sum);
        dr = dw - dl;
    }
    if (dt + db > dh && (dt + db) > 0) {
        int64_t sum = dt + db;
        dt = sdiv128_i64((__int128)dt * dh, sum);
        db = dh - dt;
    }

    int32_t sx[4] = {0, sl, pic->width - sr, pic->width};
    int32_t sy[4] = {0, st, pic->height - sb, pic->height};
    im_fixed dx[4] = {
        dst_rect.left,
        (im_fixed)((int64_t)dst_rect.left + dl),
        (im_fixed)((int64_t)dst_rect.right - dr),
        dst_rect.right
    };
    im_fixed dy[4] = {
        dst_rect.top,
        (im_fixed)((int64_t)dst_rect.top + dt),
        (im_fixed)((int64_t)dst_rect.bottom - db),
        dst_rect.bottom
    };

    for (int gy = 0; gy < 3; gy++) {
        for (int gx = 0; gx < 3; gx++) {
            im_rect cell = {dx[gx], dy[gy], dx[gx + 1], dy[gy + 1]};
            if (!draw_subrect(surface, pic, cell, cell, sx[gx], sy[gy], sx[gx + 1], sy[gy + 1])) {
                return false;
            }
        }
    }
    return true;
}
