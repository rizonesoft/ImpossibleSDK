/* Impossible UI: bounded offscreen layers, group opacity and nested clip/layer stacks. Portable. */
#include "layer.h"

static void zero_u32(uint32_t *p, size_t count) {
    for (size_t i = 0; i < count; i++) p[i] = 0;
}

static int floor_px_clamped(int64_t v, int limit) {
    int64_t q = v >= 0 ? (v >> 16) : -(((-v) + (IM_ONE - 1)) >> 16);
    if (q < 0) return 0;
    if (q > limit) return limit;
    return (int)q;
}

static int ceil_px_clamped(int64_t v, int limit) {
    int64_t q = v >= 0 ? ((v + (IM_ONE - 1)) >> 16) : -((-v) >> 16);
    if (q < 0) return 0;
    if (q > limit) return limit;
    return (int)q;
}

/* Blend premultiplied (sa, sr, sg, sb) over premultiplied dst (0xAARRGGBB). */
static uint32_t blend_premul_over_premul(uint32_t dst, uint32_t sa, uint32_t sr, uint32_t sg, uint32_t sb) {
    if (sa == 0) return dst;
    if (sa == 255) return (255u << 24) | (sr << 16) | (sg << 8) | sb;
    uint32_t inv = 255u - sa;
    uint32_t da = (dst >> 24) & 255u;
    uint32_t dr = (dst >> 16) & 255u;
    uint32_t dg = (dst >> 8) & 255u;
    uint32_t db = dst & 255u;
    uint32_t oa = sa + (da * inv + 127u) / 255u;
    uint32_t or_ = sr + (dr * inv + 127u) / 255u;
    uint32_t og = sg + (dg * inv + 127u) / 255u;
    uint32_t ob = sb + (db * inv + 127u) / 255u;
    if (oa > 255u) oa = 255u;
    if (or_ > 255u) or_ = 255u;
    if (og > 255u) og = 255u;
    if (ob > 255u) ob = 255u;
    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

/* Blend premultiplied (sa, sr, sg, sb) over opaque 0x00RRGGBB target pixel. */
static uint32_t blend_premul_over_opaque(uint32_t dst, uint32_t sa, uint32_t sr, uint32_t sg, uint32_t sb) {
    if (sa == 0) return dst;
    uint32_t inv = 255u - sa;
    uint32_t dr = (dst >> 16) & 255u;
    uint32_t dg = (dst >> 8) & 255u;
    uint32_t db = dst & 255u;
    uint32_t or_ = sr + (dr * inv + 127u) / 255u;
    uint32_t og = sg + (dg * inv + 127u) / 255u;
    uint32_t ob = sb + (db * inv + 127u) / 255u;
    if (or_ > 255u) or_ = 255u;
    if (og > 255u) og = 255u;
    if (ob > 255u) ob = 255u;
    return (or_ << 16) | (og << 8) | ob;
}

/* Find the innermost offscreen (non-fallback) layer frame index, or -1 if drawing onto target. */
static int32_t active_offscreen_index(const im_layer_ctx *ctx) {
    for (int32_t i = (int32_t)ctx->frame_count - 1; i >= 0; i--) {
        if (!ctx->frames[i].is_fallback) return i;
    }
    return -1;
}

bool im_layer_init(im_layer_ctx *ctx, im_surface *target, im_allocator alloc, size_t memory_limit) {
    if (!ctx || !target || !alloc.alloc || !alloc.realloc || !alloc.free) return false;
    if (!im_clip_init(&ctx->clips, alloc, target->width, target->height)) return false;
    ctx->target = target;
    ctx->alloc = alloc;
    ctx->memory_limit = memory_limit;
    ctx->bytes_used = 0;
    ctx->frames = NULL;
    ctx->frame_count = 0;
    ctx->frame_cap = 0;
    target->layers = ctx;
    return true;
}

void im_layer_destroy(im_layer_ctx *ctx) {
    if (!ctx) return;
    while (ctx->frame_count > 0) {
        im_layer_frame *f = &ctx->frames[--ctx->frame_count];
        if (f->pixels) {
            ctx->alloc.free(ctx->alloc.ctx, f->pixels, f->buffer_bytes);
            if (ctx->bytes_used >= f->buffer_bytes) ctx->bytes_used -= f->buffer_bytes;
            else ctx->bytes_used = 0;
        }
    }
    if (ctx->frames) {
        ctx->alloc.free(ctx->alloc.ctx, ctx->frames, (size_t)ctx->frame_cap * sizeof(im_layer_frame));
        ctx->frames = NULL;
    }
    ctx->frame_cap = 0;
    im_clip_destroy(&ctx->clips);
    if (ctx->target && ctx->target->layers == ctx) {
        ctx->target->layers = NULL;
    }
    ctx->target = NULL;
}

bool im_layer_push_clip_rect(im_layer_ctx *ctx, im_rect rect) {
    if (!ctx) return false;
    return im_clip_push_rect(&ctx->clips, rect);
}

bool im_layer_push_clip_rounded(im_layer_ctx *ctx, im_rect rect, im_radii radii) {
    if (!ctx) return false;
    return im_clip_push_rounded(&ctx->clips, rect, radii);
}

void im_layer_pop_clip(im_layer_ctx *ctx) {
    if (!ctx) return;
    uint32_t min_depth = (ctx->frame_count > 0) ? ctx->frames[ctx->frame_count - 1u].clip_base : 0u;
    if (ctx->clips.count > min_depth) {
        im_clip_pop(&ctx->clips);
    }
}

static bool ensure_frame_cap(im_layer_ctx *ctx, uint32_t needed) {
    if (needed <= ctx->frame_cap) return true;
    uint32_t new_cap = ctx->frame_cap == 0 ? 8u : ctx->frame_cap;
    while (new_cap < needed) {
        if (new_cap > (UINT32_MAX / 2u)) return false;
        new_cap *= 2u;
    }
    if ((size_t)new_cap > (SIZE_MAX / sizeof(im_layer_frame))) return false;
    size_t old_b = (size_t)ctx->frame_cap * sizeof(im_layer_frame);
    size_t new_b = (size_t)new_cap * sizeof(im_layer_frame);
    im_layer_frame *buf = (im_layer_frame *)ctx->alloc.realloc(ctx->alloc.ctx, ctx->frames, old_b, new_b);
    if (!buf) return false;
    ctx->frames = buf;
    ctx->frame_cap = new_cap;
    return true;
}

im_layer_result im_layer_push(im_layer_ctx *ctx, im_rect bounds, uint8_t opacity, bool has_clip, im_radii clip_radii) {
    if (!ctx || !ctx->target || ctx->frame_count == UINT32_MAX) return IM_LAYER_ERR;
    if (!ensure_frame_cap(ctx, ctx->frame_count + 1u)) return IM_LAYER_ERR;

    /* Intersect requested bounds with parent bounds (offscreen or fallback) and active clip stack BEFORE calculating buffer size. */
    int pl = 0, pt = 0, pr = ctx->target->width, pb = ctx->target->height;
    if (ctx->frame_count > 0) {
        const im_layer_frame *pf = &ctx->frames[ctx->frame_count - 1u];
        pl = pf->x;
        pt = pf->y;
        pr = pf->x + pf->width;
        pb = pf->y + pf->height;
    }
    int cl, ct, cr, cb;
    im_clip_bounds(&ctx->clips, &cl, &ct, &cr, &cb);
    if (cl > pl) pl = cl;
    if (ct > pt) pt = ct;
    if (cr < pr) pr = cr;
    if (cb < pb) pb = cb;

    int lx = 0, ly = 0, lw = 0, lh = 0;
    if (bounds.left < bounds.right && bounds.top < bounds.bottom) {
        int bl = floor_px_clamped(bounds.left, ctx->target->width);
        int bt = floor_px_clamped(bounds.top, ctx->target->height);
        int br = ceil_px_clamped(bounds.right, ctx->target->width);
        int bb = ceil_px_clamped(bounds.bottom, ctx->target->height);
        if (bl < pl) bl = pl;
        if (bt < pt) bt = pt;
        if (br > pr) br = pr;
        if (bb > pb) bb = pb;
        if (bl < br && bt < bb) {
            lx = bl;
            ly = bt;
            lw = br - bl;
            lh = bb - bt;
        }
    }

    uint32_t saved_clips = ctx->clips.count;
    bool pushed_clip = false;
    if (has_clip) {
        bool clip_ok = (clip_radii.top_left > 0 || clip_radii.top_right > 0 ||
                        clip_radii.bottom_right > 0 || clip_radii.bottom_left > 0)
            ? im_clip_push_rounded(&ctx->clips, bounds, clip_radii)
            : im_clip_push_rect(&ctx->clips, bounds);
        if (!clip_ok) return IM_LAYER_ERR;
        pushed_clip = true;
    }

    uint8_t parent_fb_op = (ctx->frame_count > 0 && ctx->frames[ctx->frame_count - 1u].is_fallback)
        ? ctx->frames[ctx->frame_count - 1u].fallback_opacity
        : 255u;

    /* Empty intersected bounds: no buffer needed, nothing drawn inside. */
    if (lw <= 0 || lh <= 0) {
        im_layer_frame *f = &ctx->frames[ctx->frame_count++];
        f->pixels = NULL;
        f->buffer_bytes = 0;
        f->x = 0;
        f->y = 0;
        f->width = 0;
        f->height = 0;
        f->opacity = opacity;
        f->fallback_opacity = 255u;
        f->is_fallback = false;
        f->has_clip = pushed_clip;
        f->empty_bounds = true;
        f->clip_base = ctx->clips.count;
        return IM_LAYER_OK;
    }

    size_t pixel_count = (size_t)lw * (size_t)lh;
    bool overflow = (pixel_count > (SIZE_MAX / sizeof(uint32_t)));
    size_t needed = overflow ? SIZE_MAX : pixel_count * sizeof(uint32_t);
    bool within_budget = !overflow && needed <= ctx->memory_limit && (ctx->bytes_used <= ctx->memory_limit - needed);

    uint32_t *buf = NULL;
    if (within_budget) {
        buf = (uint32_t *)ctx->alloc.alloc(ctx->alloc.ctx, needed);
    }

    if (!buf) {
        /* Fallback direct-draw frame: multiply group opacity into child draws and keep clips active. */
        im_layer_frame *f = &ctx->frames[ctx->frame_count++];
        f->pixels = NULL;
        f->buffer_bytes = 0;
        f->x = lx;
        f->y = ly;
        f->width = lw;
        f->height = lh;
        f->opacity = opacity;
        f->fallback_opacity = (uint8_t)(((uint32_t)parent_fb_op * (uint32_t)opacity + 127u) / 255u);
        f->is_fallback = true;
        f->has_clip = pushed_clip;
        f->empty_bounds = false;
        f->clip_base = saved_clips + (pushed_clip ? 1u : 0u);
        return IM_LAYER_FALLBACK;
    }

    zero_u32(buf, pixel_count);
    ctx->bytes_used += needed;
    im_layer_frame *f = &ctx->frames[ctx->frame_count++];
    f->pixels = buf;
    f->buffer_bytes = needed;
    f->x = lx;
    f->y = ly;
    f->width = lw;
    f->height = lh;
    f->opacity = opacity;
    f->fallback_opacity = 255u;
    f->is_fallback = false;
    f->has_clip = pushed_clip;
    f->empty_bounds = false;
    f->clip_base = ctx->clips.count;
    return IM_LAYER_OK;
}

void im_layer_pop(im_layer_ctx *ctx) {
    if (!ctx || ctx->frame_count == 0) return;
    im_layer_frame f = ctx->frames[--ctx->frame_count];

    /* Unwind any inner clips pushed inside this layer back to its clip_base. */
    while (ctx->clips.count > f.clip_base) {
        im_clip_pop(&ctx->clips);
    }

    if (f.is_fallback || f.empty_bounds) {
        if (f.has_clip) im_clip_pop(&ctx->clips);
        return;
    }

    /* Determine parent destination (innermost remaining offscreen layer, or target surface). */
    int32_t parent_off = active_offscreen_index(ctx);
    uint32_t parent_clip_from = (parent_off >= 0) ? ctx->frames[parent_off].clip_base : 0u;
    uint32_t parent_clip_to = ctx->clips.count; /* Includes f.has_clip if pushed! */
    uint8_t parent_fb_op = (ctx->frame_count > 0 && ctx->frames[ctx->frame_count - 1u].is_fallback)
        ? ctx->frames[ctx->frame_count - 1u].fallback_opacity
        : 255u;
    uint32_t eff_layer_op = ((uint32_t)f.opacity * (uint32_t)parent_fb_op + 127u) / 255u;

    if (f.pixels && eff_layer_op > 0) {
        for (int dy = 0; dy < f.height; dy++) {
            int ty = f.y + dy;
            const uint32_t *src_row = f.pixels + (size_t)dy * (size_t)f.width;
            for (int dx = 0; dx < f.width; dx++) {
                uint32_t sp = src_row[dx];
                if (sp == 0) continue;
                int tx = f.x + dx;
                uint32_t clip_cov = im_clip_pixel_coverage_range(&ctx->clips, parent_clip_from, parent_clip_to, tx, ty);
                if (clip_cov == 0) continue;
                uint32_t w = (uint32_t)(((uint64_t)eff_layer_op * clip_cov + IM_ONE / 2) >> 16);
                if (w == 0) continue;
                uint32_t sa = (((sp >> 24) & 255u) * w + 127u) / 255u;
                uint32_t sr = (((sp >> 16) & 255u) * w + 127u) / 255u;
                uint32_t sg = (((sp >> 8) & 255u) * w + 127u) / 255u;
                uint32_t sb = ((sp & 255u) * w + 127u) / 255u;
                if (parent_off >= 0) {
                    im_layer_frame *pf = &ctx->frames[parent_off];
                    if (pf->pixels && tx >= pf->x && tx < pf->x + pf->width && ty >= pf->y && ty < pf->y + pf->height) {
                        uint32_t *dp = pf->pixels + (size_t)(ty - pf->y) * (size_t)pf->width + (size_t)(tx - pf->x);
                        *dp = blend_premul_over_premul(*dp, sa, sr, sg, sb);
                    }
                } else {
                    uint32_t *dp = ctx->target->pixels + (size_t)ty * (size_t)ctx->target->stride + (size_t)tx;
                    *dp = blend_premul_over_opaque(*dp, sa, sr, sg, sb);
                }
            }
        }
    }

    if (f.pixels) {
        ctx->alloc.free(ctx->alloc.ctx, f.pixels, f.buffer_bytes);
        if (ctx->bytes_used >= f.buffer_bytes) ctx->bytes_used -= f.buffer_bytes;
        else ctx->bytes_used = 0;
    }
    if (f.has_clip) {
        im_clip_pop(&ctx->clips);
    }
}

void im_layer_draw_bounds(const im_layer_ctx *ctx, int *left, int *top, int *right, int *bottom) {
    if (!ctx || !ctx->target) {
        *left = *top = *right = *bottom = 0;
        return;
    }
    if (ctx->frame_count > 0) {
        const im_layer_frame *top_f = &ctx->frames[ctx->frame_count - 1u];
        if (top_f->empty_bounds || top_f->width <= 0 || top_f->height <= 0) {
            *left = *top = *right = *bottom = 0;
            return;
        }
    }
    im_clip_bounds(&ctx->clips, left, top, right, bottom);
    if (ctx->frame_count > 0) {
        const im_layer_frame *f = &ctx->frames[ctx->frame_count - 1u];
        if (*left < f->x) *left = f->x;
        if (*top < f->y) *top = f->y;
        if (*right > f->x + f->width) *right = f->x + f->width;
        if (*bottom > f->y + f->height) *bottom = f->y + f->height;
        if (*left > *right) *left = *right;
        if (*top > *bottom) *top = *bottom;
    }
}

void im_layer_blend_pixel(im_layer_ctx *ctx, int x, int y, uint32_t argb, uint32_t coverage) {
    if (!ctx || !ctx->target || coverage == 0) return;
    if (ctx->frame_count > 0) {
        const im_layer_frame *top_f = &ctx->frames[ctx->frame_count - 1u];
        if (top_f->empty_bounds || x < top_f->x || x >= top_f->x + top_f->width ||
            y < top_f->y || y >= top_f->y + top_f->height) {
            return;
        }
    }
    int32_t off = active_offscreen_index(ctx);
    if (off >= 0 && ctx->frames[off].empty_bounds) return;

    uint32_t clip_from = (off >= 0) ? ctx->frames[off].clip_base : 0u;
    uint32_t clip_cov = im_clip_pixel_coverage_range(&ctx->clips, clip_from, ctx->clips.count, x, y);
    if (clip_cov == 0) return;
    uint32_t eff_cov = (clip_cov == IM_ONE) ? coverage : (uint32_t)(((uint64_t)coverage * clip_cov + IM_ONE / 2) >> 16);
    if (eff_cov == 0) return;

    uint32_t src_a = (argb >> 24) & 255u;
    if (ctx->frame_count > 0 && ctx->frames[ctx->frame_count - 1u].is_fallback) {
        src_a = (src_a * (uint32_t)ctx->frames[ctx->frame_count - 1u].fallback_opacity + 127u) / 255u;
    }
    uint32_t sa = (uint32_t)(((uint64_t)src_a * eff_cov + IM_ONE / 2) >> 16);
    if (sa == 0) return;
    uint32_t sr = (((argb >> 16) & 255u) * sa + 127u) / 255u;
    uint32_t sg = (((argb >> 8) & 255u) * sa + 127u) / 255u;
    uint32_t sb = ((argb & 255u) * sa + 127u) / 255u;

    if (off >= 0) {
        im_layer_frame *f = &ctx->frames[off];
        if (!f->pixels || x < f->x || x >= f->x + f->width || y < f->y || y >= f->y + f->height) return;
        uint32_t *dp = f->pixels + (size_t)(y - f->y) * (size_t)f->width + (size_t)(x - f->x);
        *dp = blend_premul_over_premul(*dp, sa, sr, sg, sb);
    } else {
        if (x < 0 || x >= ctx->target->width || y < 0 || y >= ctx->target->height) return;
        uint32_t *dp = ctx->target->pixels + (size_t)y * (size_t)ctx->target->stride + (size_t)x;
        *dp = blend_premul_over_opaque(*dp, sa, sr, sg, sb);
    }
}
