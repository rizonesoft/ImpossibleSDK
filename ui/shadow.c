/* Impossible UI: integer elevation shadows and LRU shadow mask cache (pinned DropShadowRecipe.h). Portable. */
#include "shadow.h"
#include "layer.h"
#include "transform.h"

#define FX(n) ((im_fixed)((int64_t)(n) * IM_ONE))

static im_fixed ceil_fx(im_fixed v) {
    if (v <= 0) return 0;
    return (v + (IM_ONE - 1)) & ~(IM_ONE - 1);
}

static im_fixed max_fx(im_fixed a, im_fixed b) { return a > b ? a : b; }

void im_shadow_recipe_for(im_fixed translation_z, im_theme theme, im_shadow_recipe *out) {
    if (!out) return;
    im_fixed elev = translation_z / 2;
    if (elev < 0) elev = 0;
    if (elev > FX(64)) elev = FX(64);
    out->elevation = elev;
    out->ambient_y_offset = 0;
    out->directional_blur_radius = elev;
    out->directional_y_offset = elev / 2;

    if (elev < FX(2) || theme == IM_CONTRAST) {
        out->ambient_blur_radius = FX(2);
        out->ambient_alpha = 0u;
        out->directional_alpha = 0u;
    } else if (elev <= FX(16)) {
        out->ambient_blur_radius = FX(2);
        out->ambient_alpha = 0u;
        if (theme == IM_LIGHT) {
            /* Exact integer equivalent of static_cast<uint8_t>(min(elev / 100 + 0.06, 0.14) * 255). */
            int64_t scaled = ((int64_t)elev * 255LL + 6LL * 255LL * (int64_t)IM_ONE) / (100LL * (int64_t)IM_ONE);
            int64_t cap = (14LL * 255LL) / 100LL; /* 35 */
            if (scaled > cap) scaled = cap;
            if (scaled < 0) scaled = 0;
            out->directional_alpha = (uint8_t)scaled;
        } else {
            out->directional_alpha = (uint8_t)((17039LL * 255LL) >> 16); /* 0.26 * 255 = 66 */
        }
    } else {
        out->ambient_blur_radius = elev / 3;
        out->ambient_y_offset = FX(2);
        if (theme == IM_LIGHT) {
            out->ambient_alpha = (uint8_t)((9830LL * 255LL) >> 16);     /* 0.15 * 255 = 38 */
            out->directional_alpha = (uint8_t)((12452LL * 255LL) >> 16); /* 0.19 * 255 = 48 */
        } else {
            out->ambient_alpha = (uint8_t)((24248LL * 255LL) >> 16);     /* 0.37 * 255 = 94 */
            out->directional_alpha = (uint8_t)((24248LL * 255LL) >> 16); /* 0.37 * 255 = 94 */
        }
    }

    /* Full 3-sigma Gaussian support (sigma = blur_radius / 2 -> 3*sigma = 3*blur_radius/2 + 1 px margin). */
    im_fixed dir_sup = ceil_fx((im_fixed)(((int64_t)out->directional_blur_radius * 3 + 1) / 2)) + IM_ONE;
    im_fixed amb_sup = (out->ambient_alpha > 0u)
        ? (ceil_fx((im_fixed)(((int64_t)out->ambient_blur_radius * 3 + 1) / 2)) + IM_ONE) : 0;
    im_fixed max_sup = max_fx(dir_sup, amb_sup);
    out->insets.left = max_sup;
    out->insets.right = max_sup;
    out->insets.top = ceil_fx(max_fx(amb_sup - out->ambient_y_offset, dir_sup - out->directional_y_offset));
    if (out->insets.top < 0) out->insets.top = 0;
    out->insets.bottom = ceil_fx(max_fx(amb_sup + out->ambient_y_offset, dir_sup + out->directional_y_offset));
}

im_thickness im_shadow_outset(im_fixed translation_z, im_theme theme) {
    im_shadow_recipe r;
    im_shadow_recipe_for(translation_z, theme, &r);
    if (r.ambient_alpha == 0u && r.directional_alpha == 0u) {
        return (im_thickness){ 0, 0, 0, 0 };
    }
    return r.insets;
}

void im_shadow_cache_init(im_shadow_cache *cache, im_allocator alloc, size_t memory_limit) {
    if (!cache) return;
    for (size_t i = 0; i < sizeof(*cache); i++) ((uint8_t *)cache)[i] = 0u;
    cache->alloc = alloc;
    cache->memory_limit = memory_limit;
}

static void free_entry(im_shadow_cache *cache, im_shadow_cache_entry *e) {
    if (!cache || !e) return;
    size_t sz = (size_t)e->mask_w * (size_t)e->mask_h;
    if (e->dir_mask && cache->alloc.free) {
        cache->alloc.free(cache->alloc.ctx, e->dir_mask, sz);
        e->dir_mask = NULL;
    }
    if (e->amb_mask && cache->alloc.free) {
        cache->alloc.free(cache->alloc.ctx, e->amb_mask, sz);
        e->amb_mask = NULL;
    }
    if (cache->used_bytes >= e->bytes) cache->used_bytes -= e->bytes;
    else cache->used_bytes = 0u;
    e->bytes = 0u;
}

void im_shadow_cache_destroy(im_shadow_cache *cache) {
    if (!cache) return;
    for (uint32_t i = 0; i < cache->entry_count; i++) {
        free_entry(cache, &cache->entries[i]);
    }
    cache->entry_count = 0u;
    cache->used_bytes = 0u;
}

/* Pure integer Q20 approximation of exp(-x) for x >= 0 (Q20 input, Q20 output, error < 2e-5). */
static uint32_t exp_neg_q20(int64_t x_q20) {
    if (x_q20 <= 0) return (1u << 20);
    if (x_q20 >= (14LL << 20)) return 0u;
    /* Reduce x = n * ln(2) + r where ln(2) in Q20 = 726817. */
    const int64_t LN2_Q20 = 726817LL;
    int64_t n = x_q20 / LN2_Q20;
    int64_t r = x_q20 - n * LN2_Q20; /* 0 <= r < ln(2) */
    if (n >= 20) return 0u;
    /* exp(-r) ~= 1 - r + r^2/2 - r^3/6 + r^4/24 - r^5/120 + r^6/720 in Q20 */
    int64_t r2 = (r * r) >> 20;
    int64_t r3 = (r2 * r) >> 20;
    int64_t r4 = (r3 * r) >> 20;
    int64_t r5 = (r4 * r) >> 20;
    int64_t r6 = (r5 * r) >> 20;
    int64_t val = (1LL << 20) - r + (r2 / 2) - (r3 / 6) + (r4 / 24) - (r5 / 120) + (r6 / 720);
    if (val < 0) val = 0;
    return (uint32_t)(val >> n);
}

/* Exact separable 1D integer Gaussian blur with sigma = radius_px / 2 (matches double Gaussian <= 1/255). */
static void blur_gaussian(uint8_t *buf, uint8_t *tmp, int w, int h, int radius_px) {
    if (radius_px <= 0) return;
    int kmax = (3 * radius_px + 1) / 2;
    if (kmax < 1) kmax = 1;
    if (kmax > 96) kmax = 96;
    uint32_t weights[97];
    /* 2 * sigma^2 = 2 * (radius_px / 2)^2 = radius_px^2 / 2 => k^2 / (2*sigma^2) = 2 * k^2 / radius_px^2 */
    int64_t den = (int64_t)radius_px * (int64_t)radius_px;
    uint64_t sum = 0;
    for (int k = 0; k <= kmax; k++) {
        int64_t x_q20 = (2LL * (int64_t)k * (int64_t)k * (1LL << 20)) / den;
        uint32_t wk = exp_neg_q20(x_q20);
        weights[k] = wk;
        sum += (k == 0) ? (uint64_t)wk : (2ULL * (uint64_t)wk);
    }
    if (sum == 0) return;
    /* Normalize weights to Q16 (sum == 65536). */
    uint32_t q16[97];
    uint32_t qsum = 0;
    for (int k = 0; k <= kmax; k++) {
        q16[k] = (uint32_t)(((uint64_t)weights[k] * 65536ULL + sum / 2ULL) / sum);
        qsum += (k == 0) ? q16[k] : (2u * q16[k]);
    }
    if (qsum < 65536u) q16[0] += (65536u - qsum);
    else if (qsum > 65536u && q16[0] > (qsum - 65536u)) q16[0] -= (qsum - 65536u);

    /* Horizontal pass: buf -> tmp */
    for (int y = 0; y < h; y++) {
        const uint8_t *row = buf + (size_t)y * (size_t)w;
        uint8_t *out = tmp + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            uint64_t acc = (uint64_t)row[x] * (uint64_t)q16[0];
            for (int k = 1; k <= kmax; k++) {
                uint32_t wk = q16[k];
                if (wk == 0u) continue;
                int xl = x - k;
                int xr = x + k;
                uint32_t vl = (xl >= 0) ? row[xl] : 0u;
                uint32_t vr = (xr < w) ? row[xr] : 0u;
                acc += (uint64_t)(vl + vr) * (uint64_t)wk;
            }
            uint32_t v = (uint32_t)((acc + 32768ULL) >> 16);
            out[x] = (uint8_t)(v > 255u ? 255u : v);
        }
    }

    /* Vertical pass: tmp -> buf */
    for (int y = 0; y < h; y++) {
        uint8_t *out = buf + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            uint64_t acc = (uint64_t)tmp[(size_t)y * (size_t)w + (size_t)x] * (uint64_t)q16[0];
            for (int k = 1; k <= kmax; k++) {
                uint32_t wk = q16[k];
                if (wk == 0u) continue;
                int yl = y - k;
                int yr = y + k;
                uint32_t vl = (yl >= 0) ? tmp[(size_t)yl * (size_t)w + (size_t)x] : 0u;
                uint32_t vr = (yr < h) ? tmp[(size_t)yr * (size_t)w + (size_t)x] : 0u;
                acc += (uint64_t)(vl + vr) * (uint64_t)wk;
            }
            uint32_t v = (uint32_t)((acc + 32768ULL) >> 16);
            out[x] = (uint8_t)(v > 255u ? 255u : v);
        }
    }
}

static void rasterize_caster_mask(uint8_t *mask, int mw, int mh, int ox, int oy, int cw, int ch, im_radii rad) {
    im_rect r = { FX(ox), FX(oy), FX(ox + cw), FX(oy + ch) };
    for (int y = 0; y < mh; y++) {
        im_fixed fy = FX(y) + (IM_ONE / 2);
        for (int x = 0; x < mw; x++) {
            im_fixed fx = FX(x) + (IM_ONE / 2);
            mask[(size_t)y * (size_t)mw + (size_t)x] = im_shape_contains(r, rad, fx, fy) ? 255u : 0u;
        }
    }
}

static bool radii_eq(im_radii a, im_radii b) {
    return a.top_left == b.top_left && a.top_right == b.top_right &&
           a.bottom_right == b.bottom_right && a.bottom_left == b.bottom_left;
}

static void evict_lru(im_shadow_cache *cache) {
    if (!cache || cache->entry_count == 0u) return;
    uint32_t victim = 0u;
    for (uint32_t i = 1u; i < cache->entry_count; i++) {
        if (cache->entries[i].last_use < cache->entries[victim].last_use) victim = i;
    }
    free_entry(cache, &cache->entries[victim]);
    for (uint32_t i = victim + 1u; i < cache->entry_count; i++) {
        cache->entries[i - 1u] = cache->entries[i];
    }
    cache->entry_count--;
}

bool im_shadow_draw(im_surface *surface, im_shadow_cache *cache, im_rect caster_bounds,
                    im_radii caster_radii, im_fixed translation_z) {
    if (!surface || !surface->pixels) return false;
    im_fixed scale = surface->scale > 0 ? surface->scale : IM_ONE;
    im_transform inv = im_transform_identity();
    bool has_xform = false;
    if (surface->transform) {
        if (!im_transform_invert(surface->transform, &inv)) return true; /* singular transform = no-op */
        has_xform = true;
    }

    im_shadow_recipe rec;
    im_shadow_recipe_for(translation_z, surface->palette.theme, &rec);
    if (rec.ambient_alpha == 0u && rec.directional_alpha == 0u) return true;

    int64_t cx0 = ((int64_t)caster_bounds.left + (IM_ONE / 2)) >> 16;
    int64_t cy0 = ((int64_t)caster_bounds.top + (IM_ONE / 2)) >> 16;
    int64_t cx1 = ((int64_t)caster_bounds.right + (IM_ONE / 2)) >> 16;
    int64_t cy1 = ((int64_t)caster_bounds.bottom + (IM_ONE / 2)) >> 16;
    int64_t cw64 = cx1 - cx0;
    int64_t ch64 = cy1 - cy0;
    if (cw64 <= 0 || ch64 <= 0 || cw64 > INT32_MAX / 2 || ch64 > INT32_MAX / 2) return true;
    int32_t cw = (int32_t)cw64;
    int32_t ch = (int32_t)ch64;

    int32_t dir_r_px = (int32_t)((((int64_t)rec.directional_blur_radius * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t amb_r_px = (int32_t)((((int64_t)rec.ambient_blur_radius * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t dir_dy_px = (int32_t)((((int64_t)rec.directional_y_offset * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t amb_dy_px = (int32_t)((((int64_t)rec.ambient_y_offset * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t max_r_px = dir_r_px > amb_r_px ? dir_r_px : amb_r_px;
    int32_t kmax = (3 * max_r_px + 1) / 2;
    if (kmax > 96) kmax = 96;
    int32_t pad = kmax + (dir_dy_px > amb_dy_px ? dir_dy_px : amb_dy_px) + 1;
    if (pad < 2) pad = 2;

    /* Early offscreen rejection before allocation or blurring. */
    int32_t vx0 = 0, vy0 = 0, vx1 = surface->width, vy1 = surface->height;
    if (has_xform) {
        int64_t lx0 = ((int64_t)(cx0 - pad)) << 30;
        int64_t ly0 = ((int64_t)(cy0 - pad)) << 30;
        int64_t lx1 = ((int64_t)(cx1 + pad)) << 30;
        int64_t ly1 = ((int64_t)(cy1 + pad)) << 30;
        if (!im_transform_viewport_bounds_q30(surface->transform, lx0, ly0, lx1, ly1,
                                              0, 0, surface->width, surface->height, &vx0, &vy0, &vx1, &vy1)) {
            return true;
        }
    } else {
        int64_t ox64 = cx0 - pad;
        int64_t oy64 = cy0 - pad;
        int64_t ex64 = cx1 + pad;
        int64_t ey64 = cy1 + pad;
        if (ex64 <= 0 || ey64 <= 0 || ox64 >= surface->width || oy64 >= surface->height) return true;
        vx0 = (ox64 > 0) ? (int32_t)ox64 : 0;
        vy0 = (oy64 > 0) ? (int32_t)oy64 : 0;
        vx1 = (ex64 < surface->width) ? (int32_t)ex64 : surface->width;
        vy1 = (ey64 < surface->height) ? (int32_t)ey64 : surface->height;
    }

    int64_t mw64 = cw64 + 2LL * (int64_t)pad;
    int64_t mh64 = ch64 + 2LL * (int64_t)pad;
    if (mw64 <= 0 || mh64 <= 0 || mw64 > 16384 || mh64 > 16384) return false;
    int32_t mw = (int32_t)mw64;
    int32_t mh = (int32_t)mh64;
    size_t plane_bytes = (size_t)mw * (size_t)mh;
    bool need_amb = (rec.elevation > FX(16));
    size_t entry_bytes = plane_bytes * (need_amb ? 2u : 1u);

    im_shadow_cache_entry *hit = NULL;
    if (cache) {
        for (uint32_t i = 0; i < cache->entry_count; i++) {
            im_shadow_cache_entry *e = &cache->entries[i];
            if (e->caster_w == cw && e->caster_h == ch && radii_eq(e->caster_radii, caster_radii) &&
                e->elevation == rec.elevation && e->scale == scale) {
                hit = e;
                hit->last_use = ++cache->use_clock;
                cache->cache_hits++;
                break;
            }
        }
    }

    uint8_t *dir_mask = hit ? hit->dir_mask : NULL;
    uint8_t *amb_mask = hit ? hit->amb_mask : NULL;
    im_allocator alloc = cache ? cache->alloc : (surface->layers ? surface->layers->alloc : (im_allocator){0});
    uint8_t stack_dir[8192];
    uint8_t stack_amb[8192];
    uint8_t stack_tmp[8192];
    bool temp_owned = false;

    if (!hit) {
        uint8_t *scratch = NULL;
        if (alloc.alloc && alloc.free) {
            if (cache && cache->memory_limit > 0u) {
                if (entry_bytes > cache->memory_limit) return false;
                while (cache->entry_count > 0u &&
                       (cache->used_bytes + entry_bytes > cache->memory_limit ||
                        cache->entry_count >= IM_SHADOW_CACHE_MAX_ENTRIES)) {
                    evict_lru(cache);
                }
            } else if (cache && cache->entry_count >= IM_SHADOW_CACHE_MAX_ENTRIES) {
                evict_lru(cache);
            }
            scratch = (uint8_t *)alloc.alloc(alloc.ctx, plane_bytes);
            dir_mask = (uint8_t *)alloc.alloc(alloc.ctx, plane_bytes);
            if (need_amb) amb_mask = (uint8_t *)alloc.alloc(alloc.ctx, plane_bytes);
            if (!scratch || !dir_mask || (need_amb && !amb_mask)) {
                if (scratch) alloc.free(alloc.ctx, scratch, plane_bytes);
                if (dir_mask) alloc.free(alloc.ctx, dir_mask, plane_bytes);
                if (amb_mask) alloc.free(alloc.ctx, amb_mask, plane_bytes);
                return false;
            }
            temp_owned = (cache == NULL);
        } else if (!cache && plane_bytes <= sizeof(stack_dir)) {
            dir_mask = stack_dir;
            amb_mask = need_amb ? stack_amb : NULL;
            scratch = stack_tmp;
        } else {
            return false;
        }

        rasterize_caster_mask(dir_mask, mw, mh, pad, pad + dir_dy_px, cw, ch, caster_radii);
        blur_gaussian(dir_mask, scratch, mw, mh, dir_r_px);
        if (need_amb) {
            rasterize_caster_mask(amb_mask, mw, mh, pad, pad + amb_dy_px, cw, ch, caster_radii);
            blur_gaussian(amb_mask, scratch, mw, mh, amb_r_px);
        }
        if (alloc.free && scratch != stack_tmp) alloc.free(alloc.ctx, scratch, plane_bytes);

        if (cache) {
            cache->blur_computations++;
            im_shadow_cache_entry *slot = &cache->entries[cache->entry_count++];
            slot->caster_w = cw;
            slot->caster_h = ch;
            slot->caster_radii = caster_radii;
            slot->elevation = rec.elevation;
            slot->scale = scale;
            slot->mask_w = mw;
            slot->mask_h = mh;
            slot->pad_left = pad;
            slot->pad_top = pad;
            slot->dir_mask = dir_mask;
            slot->amb_mask = amb_mask;
            slot->bytes = entry_bytes;
            slot->last_use = ++cache->use_clock;
            cache->used_bytes += entry_bytes;
        }
    }

    /* Composite directional + ambient shadow masks onto surface, cutting out the hollow caster interior! */
    int64_t ox = cx0 - pad;
    int64_t oy = cy0 - pad;
    for (int32_t sy = vy0; sy < vy1; sy++) {
        for (int32_t sx = vx0; sx < vx1; sx++) {
            im_fixed lx, ly;
            int64_t mx, my;
            if (has_xform) {
                im_fixed dx_fx = FX(sx) + (IM_ONE / 2);
                im_fixed dy_fx = FX(sy) + (IM_ONE / 2);
                im_transform_point(&inv, dx_fx, dy_fx, &lx, &ly);
                mx = ((int64_t)lx >> 16) - ox;
                my = ((int64_t)ly >> 16) - oy;
                if (mx < 0 || mx >= mw || my < 0 || my >= mh) continue;
            } else {
                lx = FX(sx) + (IM_ONE / 2);
                ly = FX(sy) + (IM_ONE / 2);
                mx = (int64_t)sx - ox;
                my = (int64_t)sy - oy;
                if (mx < 0 || mx >= mw || my < 0 || my >= mh) continue;
            }
            /* Pinned DropShadowRecipe.h: hollow element interior receives zero shadow. */
            if (im_shape_contains(caster_bounds, caster_radii, lx, ly)) continue;

            uint32_t da = (uint32_t)(((uint32_t)dir_mask[(size_t)my * (size_t)mw + (size_t)mx] * rec.directional_alpha + 127u) / 255u);
            uint32_t aa = (amb_mask && rec.ambient_alpha > 0u)
                ? (uint32_t)(((uint32_t)amb_mask[(size_t)my * (size_t)mw + (size_t)mx] * rec.ambient_alpha + 127u) / 255u)
                : 0u;
            /* Source-over composition of black ambient + black directional alpha: 1 - (1 - aa)*(1 - da). */
            uint32_t alpha = aa + da - (aa * da + 127u) / 255u;
            if (alpha == 0u) continue;
            if (alpha > 255u) alpha = 255u;

            if (surface->layers) {
                im_layer_blend_pixel(surface->layers, sx, sy, alpha << 24, IM_ONE);
            } else {
                uint32_t dst = surface->pixels[(size_t)sy * (size_t)surface->stride + (size_t)sx];
                uint32_t inv_a = 255u - alpha;
                uint32_t r = (((dst >> 16) & 255u) * inv_a + 127u) / 255u;
                uint32_t g = (((dst >> 8) & 255u) * inv_a + 127u) / 255u;
                uint32_t b = ((dst & 255u) * inv_a + 127u) / 255u;
                surface->pixels[(size_t)sy * (size_t)surface->stride + (size_t)sx] = (r << 16) | (g << 8) | b;
            }
        }
    }

    if (temp_owned) {
        alloc.free(alloc.ctx, dir_mask, plane_bytes);
        if (amb_mask) alloc.free(alloc.ctx, amb_mask, plane_bytes);
    }
    return true;
}
