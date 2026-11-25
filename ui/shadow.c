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
            /* min(elev / 100 + 0.06, 0.14) * 255, rounded truncating like static_cast<uint8_t>(op * 255). */
            int64_t op_num = ((int64_t)elev * 255LL) / (100LL * IM_ONE) + (1530LL / 100LL);
            /* Exact integer: op_q16 = min(elev/100 + 0.06*65536, 0.14*65536). */
            int64_t op_q16 = (int64_t)elev / 100LL + 3932LL;
            if (op_q16 > 9175LL) op_q16 = 9175LL;
            (void)op_num;
            out->directional_alpha = (uint8_t)((op_q16 * 255LL) >> 16);
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

    im_fixed max_r = ceil_fx(max_fx(out->ambient_blur_radius, out->directional_blur_radius));
    out->insets.left = max_r;
    out->insets.right = max_r;
    out->insets.top = ceil_fx(max_fx(out->ambient_blur_radius - out->ambient_y_offset,
                                     out->directional_blur_radius - out->directional_y_offset));
    out->insets.bottom = ceil_fx(max_fx(out->ambient_blur_radius + out->ambient_y_offset,
                                        out->directional_blur_radius + out->directional_y_offset));
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

/* 3-pass separable integer box blur approximating Gaussian with blur_radius in pixels (sigma ~= radius / 2). */
static void box_blur_h(const uint8_t *src, uint8_t *dst, int w, int h, int rad) {
    if (rad <= 0) {
        for (int i = 0; i < w * h; i++) dst[i] = src[i];
        return;
    }
    int diam = 2 * rad + 1;
    int half = diam / 2;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * (size_t)w;
        uint8_t *out = dst + (size_t)y * (size_t)w;
        int sum = 0;
        for (int k = -rad; k <= rad; k++) {
            int sx = (k < 0) ? 0 : (k >= w ? w - 1 : k);
            sum += row[sx];
        }
        for (int x = 0; x < w; x++) {
            out[x] = (uint8_t)((sum + half) / diam);
            int sub_x = x - rad;
            int add_x = x + rad + 1;
            int s0 = (sub_x < 0) ? 0 : (sub_x >= w ? w - 1 : sub_x);
            int s1 = (add_x < 0) ? 0 : (add_x >= w ? w - 1 : add_x);
            sum += (int)row[s1] - (int)row[s0];
        }
    }
}

static void box_blur_v(const uint8_t *src, uint8_t *dst, int w, int h, int rad) {
    if (rad <= 0) {
        for (int i = 0; i < w * h; i++) dst[i] = src[i];
        return;
    }
    int diam = 2 * rad + 1;
    int half = diam / 2;
    for (int x = 0; x < w; x++) {
        int sum = 0;
        for (int k = -rad; k <= rad; k++) {
            int sy = (k < 0) ? 0 : (k >= h ? h - 1 : k);
            sum += src[(size_t)sy * (size_t)w + (size_t)x];
        }
        for (int y = 0; y < h; y++) {
            dst[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)((sum + half) / diam);
            int sub_y = y - rad;
            int add_y = y + rad + 1;
            int s0 = (sub_y < 0) ? 0 : (sub_y >= h ? h - 1 : sub_y);
            int s1 = (add_y < 0) ? 0 : (add_y >= h ? h - 1 : add_y);
            sum += (int)src[(size_t)s1 * (size_t)w + (size_t)x] - (int)src[(size_t)s0 * (size_t)w + (size_t)x];
        }
    }
}

/* Compute exact integer 3-pass box radii for Gaussian sigma (radius_px / 2) using W3C/Kovesi formula. */
static void blur_3pass(uint8_t *buf, uint8_t *tmp, int w, int h, int radius_px) {
    if (radius_px <= 0) return;
    /* 3-pass box blur variance = 3 * ((2r+1)^2 - 1)/12 = r*(r+1). Target sigma ~= radius_px / 2 -> var = radius_px^2 / 4.
       Solve r*(r+1) ~= radius_px^2 / 4 -> r ~= max(1, (radius_px - 1) / 2). */
    int r0 = radius_px / 2;
    int r1 = (radius_px + 1) / 2;
    int r2 = r0;
    if (r0 <= 0) r0 = 1;
    if (r1 <= 0) r1 = 1;
    if (r2 <= 0) r2 = 1;
    int passes[3] = { r0, r1, r2 };
    for (int p = 0; p < 3; p++) {
        box_blur_h(buf, tmp, w, h, passes[p]);
        box_blur_v(tmp, buf, w, h, passes[p]);
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
    if (surface->transform) {
        im_transform inv;
        if (!im_transform_invert(surface->transform, &inv)) return true; /* singular transform = no-op */
    }

    im_shadow_recipe rec;
    im_shadow_recipe_for(translation_z, surface->palette.theme, &rec);
    if (rec.ambient_alpha == 0u && rec.directional_alpha == 0u) return true;

    int32_t cx0 = (int32_t)((caster_bounds.left + (IM_ONE / 2)) >> 16);
    int32_t cy0 = (int32_t)((caster_bounds.top + (IM_ONE / 2)) >> 16);
    int32_t cx1 = (int32_t)((caster_bounds.right + (IM_ONE / 2)) >> 16);
    int32_t cy1 = (int32_t)((caster_bounds.bottom + (IM_ONE / 2)) >> 16);
    int32_t cw = cx1 - cx0;
    int32_t ch = cy1 - cy0;
    if (cw <= 0 || ch <= 0) return true;

    int32_t dir_r_px = (int32_t)((((int64_t)rec.directional_blur_radius * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t amb_r_px = (int32_t)((((int64_t)rec.ambient_blur_radius * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t dir_dy_px = (int32_t)((((int64_t)rec.directional_y_offset * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t amb_dy_px = (int32_t)((((int64_t)rec.ambient_y_offset * (int64_t)scale) + (1LL << 31)) >> 32);
    int32_t pad = (dir_r_px > amb_r_px ? dir_r_px : amb_r_px) + (dir_dy_px > amb_dy_px ? dir_dy_px : amb_dy_px) + 2;
    if (pad < 2) pad = 2;

    int64_t mw64 = (int64_t)cw + 2LL * (int64_t)pad;
    int64_t mh64 = (int64_t)ch + 2LL * (int64_t)pad;
    if (mw64 <= 0 || mh64 <= 0 || mw64 > 4096 || mh64 > 4096) return false;
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
    im_allocator alloc = cache ? cache->alloc : (im_allocator){0};
    bool temp_owned = false;

    if (!hit) {
        if (!alloc.alloc || !alloc.free) return false;
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

        uint8_t *scratch = (uint8_t *)alloc.alloc(alloc.ctx, plane_bytes);
        dir_mask = (uint8_t *)alloc.alloc(alloc.ctx, plane_bytes);
        if (need_amb) amb_mask = (uint8_t *)alloc.alloc(alloc.ctx, plane_bytes);
        if (!scratch || !dir_mask || (need_amb && !amb_mask)) {
            if (scratch) alloc.free(alloc.ctx, scratch, plane_bytes);
            if (dir_mask) alloc.free(alloc.ctx, dir_mask, plane_bytes);
            if (amb_mask) alloc.free(alloc.ctx, amb_mask, plane_bytes);
            return false;
        }

        rasterize_caster_mask(dir_mask, mw, mh, pad, pad + dir_dy_px, cw, ch, caster_radii);
        blur_3pass(dir_mask, scratch, mw, mh, dir_r_px);
        if (need_amb) {
            rasterize_caster_mask(amb_mask, mw, mh, pad, pad + amb_dy_px, cw, ch, caster_radii);
            blur_3pass(amb_mask, scratch, mw, mh, amb_r_px);
        }
        alloc.free(alloc.ctx, scratch, plane_bytes);

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
        } else {
            temp_owned = true;
        }
    }

    /* Composite directional + ambient shadow masks onto surface, cutting out the hollow caster interior! */
    int32_t ox = cx0 - pad;
    int32_t oy = cy0 - pad;
    for (int32_t my = 0; my < mh; my++) {
        int32_t sy = oy + my;
        if (sy < 0 || sy >= surface->height) continue;
        im_fixed fy = FX(sy) + (IM_ONE / 2);
        for (int32_t mx = 0; mx < mw; mx++) {
            int32_t sx = ox + mx;
            if (sx < 0 || sx >= surface->width) continue;
            im_fixed fx = FX(sx) + (IM_ONE / 2);
            /* Pinned DropShadowRecipe.h: hollow element interior receives zero shadow. */
            if (im_shape_contains(caster_bounds, caster_radii, fx, fy)) continue;

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
