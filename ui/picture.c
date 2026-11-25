/* Impossible UI: premultiplied picture drawing with bilinear filtering and nine-slice. Portable. */
#include "picture.h"
#include "layer.h"

static bool picture_valid(const im_picture *pic) {
    return pic && pic->pixels && pic->width > 0 && pic->height > 0 && pic->stride_pixels >= pic->width;
}

static uint32_t blend_premul_opaque(uint32_t dst_rgb, uint32_t premul_argb) {
    uint32_t sa = (premul_argb >> 24) & 255u;
    uint32_t sr = (premul_argb >> 16) & 255u;
    uint32_t sg = (premul_argb >> 8) & 255u;
    uint32_t sb = premul_argb & 255u;
    if (sa == 0 && sr == 0 && sg == 0 && sb == 0) return dst_rgb;
    if (sa == 255u) return (sr << 16) | (sg << 8) | sb;
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
    int64_t lo_x = (int64_t)min_x * IM_ONE;
    int64_t hi_x = (int64_t)max_x * IM_ONE;
    int64_t lo_y = (int64_t)min_y * IM_ONE;
    int64_t hi_y = (int64_t)max_y * IM_ONE;
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
    const uint32_t *row0 = pic->pixels + (size_t)y0 * (size_t)pic->stride_pixels;
    if (fx == 0u && fy == 0u) return row0[x0];

    const uint32_t *row1 = pic->pixels + (size_t)y1 * (size_t)pic->stride_pixels;
    uint32_t wx0 = (uint32_t)IM_ONE - fx, wx1 = fx;
    uint32_t wy0 = (uint32_t)IM_ONE - fy, wy1 = fy;
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

/* Draw source sub-rect [src_l..src_r) x [src_t..src_b) mapped onto wide Q30 map rect
   [map_l_q30, map_l_q30 + map_w_q30) x [map_t_q30, map_t_q30 + map_h_q30), clipped to clip_rect.
   Uses precomputed linear stepping (O(1) additions per pixel, zero per-pixel 128-bit divisions). */
static bool draw_subrect_q30(im_surface *surface, const im_picture *pic,
                             im_rect clip_rect,
                             int64_t map_l_q30, int64_t map_t_q30, int64_t map_w_q30, int64_t map_h_q30,
                             int32_t src_l, int32_t src_t, int32_t src_r, int32_t src_b) {
    if (clip_rect.left >= clip_rect.right || clip_rect.top >= clip_rect.bottom) return true;
    if (map_w_q30 <= 0 || map_h_q30 <= 0) return true;
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

    int64_t src_w = (int64_t)(src_r - src_l);
    int64_t src_h = (int64_t)(src_b - src_t);
    int64_t clip_l_q30 = im_fixed_to_q30(clip_rect.left);
    int64_t clip_t_q30 = im_fixed_to_q30(clip_rect.top);
    int64_t clip_r_q30 = im_fixed_to_q30(clip_rect.right);
    int64_t clip_b_q30 = im_fixed_to_q30(clip_rect.bottom);

    /* Linear step vectors in Q30 for both local coordinates and source UV coordinates. */
    int64_t m11 = has_tr ? inv.m11 : IM_TR_ONE;
    int64_t m12 = has_tr ? inv.m12 : 0;
    int64_t m21 = has_tr ? inv.m21 : 0;
    int64_t m22 = has_tr ? inv.m22 : IM_TR_ONE;

    int64_t du_dx_q30 = im_sdiv128_i64((__int128)m11 * (src_w * IM_TR_ONE), map_w_q30);
    int64_t dv_dx_q30 = im_sdiv128_i64((__int128)m12 * (src_h * IM_TR_ONE), map_h_q30);
    int64_t du_dy_q30 = im_sdiv128_i64((__int128)m21 * (src_w * IM_TR_ONE), map_w_q30);
    int64_t dv_dy_q30 = im_sdiv128_i64((__int128)m22 * (src_h * IM_TR_ONE), map_h_q30);

    int64_t start_px = fl * IM_TR_ONE + (IM_TR_ONE >> 1);
    int64_t start_py = ft * IM_TR_ONE + (IM_TR_ONE >> 1);
    int64_t row_lx = start_px, row_ly = start_py;
    if (has_tr) {
        if (!im_transform_point_q30(&inv, start_px, start_py, &row_lx, &row_ly)) return false;
    }

    int64_t row_u_q30 = (int64_t)src_l * IM_TR_ONE +
        im_sdiv128_i64((__int128)(row_lx - map_l_q30) * (src_w * IM_TR_ONE), map_w_q30) - (IM_TR_ONE >> 1);
    int64_t row_v_q30 = (int64_t)src_t * IM_TR_ONE +
        im_sdiv128_i64((__int128)(row_ly - map_t_q30) * (src_h * IM_TR_ONE), map_h_q30) - (IM_TR_ONE >> 1);

    const int q30_to_16 = IM_TR_SHIFT - 16;
    for (int y = (int)ft; y < (int)fb; y++) {
        int64_t lx = row_lx, ly = row_ly;
        int64_t u_q30 = row_u_q30, v_q30 = row_v_q30;
        uint32_t *line = surface->pixels + (size_t)y * (size_t)surface->stride;
        for (int x = (int)fl; x < (int)fr; x++) {
            if (lx >= clip_l_q30 && lx < clip_r_q30 && ly >= clip_t_q30 && ly < clip_b_q30) {
                uint32_t sample = sample_bilinear_clamped(pic, u_q30 >> q30_to_16, v_q30 >> q30_to_16,
                                                          src_l, src_r - 1, src_t, src_b - 1);
                if (surface->layers) {
                    im_layer_blend_premul_pixel(surface->layers, x, y, sample, IM_ONE);
                } else {
                    line[x] = blend_premul_opaque(line[x], sample);
                }
            }
            lx += m11;
            ly += m12;
            u_q30 += du_dx_q30;
            v_q30 += dv_dx_q30;
        }
        row_lx += m21;
        row_ly += m22;
        row_u_q30 += du_dy_q30;
        row_v_q30 += dv_dy_q30;
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

    int64_t dl_q30 = im_fixed_to_q30(dst_rect.left);
    int64_t dt_q30 = im_fixed_to_q30(dst_rect.top);
    int64_t dw_q30 = im_fixed_to_q30(dst_rect.right) - dl_q30;
    int64_t dh_q30 = im_fixed_to_q30(dst_rect.bottom) - dt_q30;
    int64_t sw = (int64_t)pic->width;
    int64_t sh = (int64_t)pic->height;
    /* When surface->transform is set (e.g. im_node_device_transform), dst_rect is in logical units
       and the transform already applies DPI scaling; avoid double-scaling IM_STRETCH_NONE. */
    int64_t unit_q30 = (surface->transform != NULL)
        ? IM_TR_ONE
        : im_fixed_to_q30(surface->scale > 0 ? surface->scale : IM_ONE);

    int64_t map_w_q30 = dw_q30, map_h_q30 = dh_q30;
    if (stretch == IM_STRETCH_NONE) {
        map_w_q30 = sw * unit_q30;
        map_h_q30 = sh * unit_q30;
    } else if (stretch == IM_STRETCH_UNIFORM) {
        if ((__int128)dw_q30 * sh <= (__int128)dh_q30 * sw) {
            map_w_q30 = dw_q30;
            map_h_q30 = im_sdiv128_i64((__int128)dw_q30 * sh + (sw / 2), sw);
        } else {
            map_h_q30 = dh_q30;
            map_w_q30 = im_sdiv128_i64((__int128)dh_q30 * sw + (sh / 2), sh);
        }
    } else if (stretch == IM_STRETCH_UNIFORM_TO_FILL) {
        if ((__int128)dw_q30 * sh >= (__int128)dh_q30 * sw) {
            map_w_q30 = dw_q30;
            map_h_q30 = im_sdiv128_i64((__int128)dw_q30 * sh + (sw / 2), sw);
        } else {
            map_h_q30 = dh_q30;
            map_w_q30 = im_sdiv128_i64((__int128)dh_q30 * sw + (sh / 2), sh);
        }
    }

    int64_t ml_q30 = dl_q30 + (dw_q30 - map_w_q30) / 2;
    int64_t mt_q30 = dt_q30 + (dh_q30 - map_h_q30) / 2;
    int64_t mr_q30 = ml_q30 + map_w_q30;
    int64_t mb_q30 = mt_q30 + map_h_q30;

    int64_t cl_q30 = ml_q30 > dl_q30 ? ml_q30 : dl_q30;
    int64_t ct_q30 = mt_q30 > dt_q30 ? mt_q30 : dt_q30;
    int64_t cr_q30 = mr_q30 < (dl_q30 + dw_q30) ? mr_q30 : (dl_q30 + dw_q30);
    int64_t cb_q30 = mb_q30 < (dt_q30 + dh_q30) ? mb_q30 : (dt_q30 + dh_q30);
    if (cl_q30 >= cr_q30 || ct_q30 >= cb_q30) return true;

    const int q30_to_16 = IM_TR_SHIFT - 16;
    im_rect clip_rect = {
        (im_fixed)(cl_q30 >> q30_to_16),
        (im_fixed)(ct_q30 >> q30_to_16),
        (im_fixed)((cr_q30 + ((1LL << q30_to_16) - 1)) >> q30_to_16),
        (im_fixed)((cb_q30 + ((1LL << q30_to_16) - 1)) >> q30_to_16)
    };
    return draw_subrect_q30(surface, pic, clip_rect, ml_q30, mt_q30, map_w_q30, map_h_q30,
                            0, 0, pic->width, pic->height);
}

static int64_t round_scale_px(int32_t src_px, int64_t scale_fx) {
    return (((int64_t)src_px * scale_fx + (IM_ONE / 2)) >> 16) * (int64_t)IM_ONE;
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

    /* Avoid double-scaling corners when surface->transform already maps logical coords to device DPI. */
    int64_t scale = (surface->transform != NULL)
        ? (int64_t)IM_ONE
        : (surface->scale > 0 ? (int64_t)surface->scale : (int64_t)IM_ONE);
    int64_t dw = (int64_t)dst_rect.right - dst_rect.left;
    int64_t dh = (int64_t)dst_rect.bottom - dst_rect.top;

    int64_t dl = round_scale_px(sl, scale);
    int64_t dr = round_scale_px(sr, scale);
    int64_t dt = round_scale_px(st, scale);
    int64_t db = round_scale_px(sb, scale);

    if (dl + dr > dw && (dl + dr) > 0) {
        int64_t sum = dl + dr;
        dl = im_sdiv128_i64((__int128)dl * dw, sum);
        dr = dw - dl;
    }
    if (dt + db > dh && (dt + db) > 0) {
        int64_t sum = dt + db;
        dt = im_sdiv128_i64((__int128)dt * dh, sum);
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
            int64_t ml = im_fixed_to_q30(cell.left);
            int64_t mt = im_fixed_to_q30(cell.top);
            int64_t mw = im_fixed_to_q30(cell.right) - ml;
            int64_t mh = im_fixed_to_q30(cell.bottom) - mt;
            if (!draw_subrect_q30(surface, pic, cell, ml, mt, mw, mh,
                                  sx[gx], sy[gy], sx[gx + 1], sy[gy + 1])) {
                return false;
            }
        }
    }
    return true;
}
