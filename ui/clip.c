/* Impossible UI: rectangular and rounded-rectangle clip stacks. Portable. */
#include "clip.h"

static int floor_px(int64_t v, int limit) {
    int64_t q = v >= 0 ? (v >> 16) : -(((-v) + (IM_ONE - 1)) >> 16);
    if (q < 0) return 0;
    if (q > limit) return limit;
    return (int)q;
}

static int ceil_px(int64_t v, int limit) {
    int64_t q = v >= 0 ? ((v + (IM_ONE - 1)) >> 16) : -((-v) >> 16);
    if (q < 0) return 0;
    if (q > limit) return limit;
    return (int)q;
}

bool im_clip_init(im_clip_stack *stack, im_allocator alloc, int width, int height) {
    if (!stack || !alloc.alloc || !alloc.realloc || !alloc.free || width < 0 || height < 0) return false;
    stack->alloc = alloc;
    stack->entries = NULL;
    stack->count = 0;
    stack->cap = 0;
    stack->surface_w = width;
    stack->surface_h = height;
    return true;
}

void im_clip_destroy(im_clip_stack *stack) {
    if (!stack) return;
    if (stack->entries) {
        stack->alloc.free(stack->alloc.ctx, stack->entries, (size_t)stack->cap * sizeof(im_clip_entry));
        stack->entries = NULL;
    }
    stack->count = 0;
    stack->cap = 0;
}

static bool ensure_clip_cap(im_clip_stack *stack, uint32_t needed) {
    if (needed <= stack->cap) return true;
    uint32_t new_cap = stack->cap == 0 ? 8u : stack->cap;
    while (new_cap < needed) {
        if (new_cap > (UINT32_MAX / 2u)) return false;
        new_cap *= 2u;
    }
    if ((size_t)new_cap > (SIZE_MAX / sizeof(im_clip_entry))) return false;
    size_t old_b = (size_t)stack->cap * sizeof(im_clip_entry);
    size_t new_b = (size_t)new_cap * sizeof(im_clip_entry);
    im_clip_entry *buf = (im_clip_entry *)stack->alloc.realloc(stack->alloc.ctx, stack->entries, old_b, new_b);
    if (!buf) return false;
    stack->entries = buf;
    stack->cap = new_cap;
    return true;
}

static bool push_clip_wide_impl(im_clip_stack *stack,
                                int64_t l_fx, int64_t t_fx, int64_t r_fx, int64_t b_fx,
                                im_rect rect, im_radii radii, bool is_rounded) {
    if (!stack || stack->count == UINT32_MAX) return false;
    if (!ensure_clip_cap(stack, stack->count + 1u)) return false;

    int64_t prev_l = 0, prev_t = 0;
    int64_t prev_r = (int64_t)stack->surface_w * IM_ONE;
    int64_t prev_b = (int64_t)stack->surface_h * IM_ONE;
    int pl = 0, pt = 0, pr = stack->surface_w, pb = stack->surface_h;
    if (stack->count > 0) {
        const im_clip_entry *prev = &stack->entries[stack->count - 1u];
        prev_l = prev->eff_left;
        prev_t = prev->eff_top;
        prev_r = prev->eff_right;
        prev_b = prev->eff_bottom;
        pl = prev->int_left;
        pt = prev->int_top;
        pr = prev->int_right;
        pb = prev->int_bottom;
    }

    int64_t eff_l = l_fx > prev_l ? l_fx : prev_l;
    int64_t eff_t = t_fx > prev_t ? t_fx : prev_t;
    int64_t eff_r = r_fx < prev_r ? r_fx : prev_r;
    int64_t eff_b = b_fx < prev_b ? b_fx : prev_b;

    int cl, ct, cr, cb;
    if (eff_l >= eff_r || eff_t >= eff_b) {
        eff_r = eff_l;
        eff_b = eff_t;
        cl = pr; ct = pb; cr = pr; cb = pb;
    } else {
        cl = floor_px(eff_l, stack->surface_w);
        ct = floor_px(eff_t, stack->surface_h);
        cr = ceil_px(eff_r, stack->surface_w);
        cb = ceil_px(eff_b, stack->surface_h);
        if (cl < pl) cl = pl;
        if (ct < pt) ct = pt;
        if (cr > pr) cr = pr;
        if (cb > pb) cb = pb;
        if (cl > cr) cl = cr;
        if (ct > cb) ct = cb;
    }

    im_clip_entry *e = &stack->entries[stack->count++];
    e->rect = rect;
    e->own_left = l_fx;
    e->own_top = t_fx;
    e->own_right = r_fx;
    e->own_bottom = b_fx;
    e->eff_left = eff_l;
    e->eff_top = eff_t;
    e->eff_right = eff_r;
    e->eff_bottom = eff_b;
    e->radii = radii;
    e->is_rounded = is_rounded && (radii.top_left > 0 || radii.top_right > 0 || radii.bottom_right > 0 || radii.bottom_left > 0);
    e->int_left = cl;
    e->int_top = ct;
    e->int_right = cr;
    e->int_bottom = cb;
    return true;
}

bool im_clip_push_rect(im_clip_stack *stack, im_rect rect) {
    return push_clip_wide_impl(stack, rect.left, rect.top, rect.right, rect.bottom, rect, (im_radii){0, 0, 0, 0}, false);
}

bool im_clip_push_irect(im_clip_stack *stack, int left, int top, int right, int bottom) {
    return push_clip_wide_impl(stack,
                               (int64_t)left * IM_ONE, (int64_t)top * IM_ONE,
                               (int64_t)right * IM_ONE, (int64_t)bottom * IM_ONE,
                               (im_rect){0, 0, 0, 0}, (im_radii){0, 0, 0, 0}, false);
}

bool im_clip_push_rounded(im_clip_stack *stack, im_rect rect, im_radii radii) {
    return push_clip_wide_impl(stack, rect.left, rect.top, rect.right, rect.bottom, rect, radii, true);
}

void im_clip_pop(im_clip_stack *stack) {
    if (!stack || stack->count == 0) return;
    stack->count--;
}

void im_clip_bounds(const im_clip_stack *stack, int *left, int *top, int *right, int *bottom) {
    if (!stack || stack->count == 0) {
        *left = 0;
        *top = 0;
        *right = stack ? stack->surface_w : 0;
        *bottom = stack ? stack->surface_h : 0;
        return;
    }
    const im_clip_entry *e = &stack->entries[stack->count - 1u];
    *left = e->int_left;
    *top = e->int_top;
    *right = e->int_right;
    *bottom = e->int_bottom;
}

uint32_t im_clip_pixel_coverage_range(const im_clip_stack *stack, uint32_t from_idx, uint32_t to_idx, int x, int y) {
    if (!stack || from_idx >= to_idx || to_idx > stack->count) return IM_ONE;
    if (from_idx == 0u) {
        const im_clip_entry *top = &stack->entries[to_idx - 1u];
        if (x < top->int_left || x >= top->int_right || y < top->int_top || y >= top->int_bottom) return 0;
    }

    /* Intersect all axis-aligned bounds in [from_idx, to_idx) with pixel (x, y) first
       so touching or disjoint fractional clips yield exact zero coverage. */
    int64_t ix0 = (int64_t)x * IM_ONE, ix1 = ix0 + IM_ONE;
    int64_t iy0 = (int64_t)y * IM_ONE, iy1 = iy0 + IM_ONE;
    bool has_rounded = false;
    for (uint32_t i = from_idx; i < to_idx; i++) {
        const im_clip_entry *e = &stack->entries[i];
        int64_t el = e->own_left;
        int64_t er = e->own_right;
        int64_t et = e->own_top;
        int64_t eb = e->own_bottom;
        if (el > ix0) ix0 = el;
        if (er < ix1) ix1 = er;
        if (et > iy0) iy0 = et;
        if (eb < iy1) iy1 = eb;
        if (ix1 <= ix0 || iy1 <= iy0) return 0;
        if (e->is_rounded) has_rounded = true;
    }

    uint64_t dx = (uint64_t)(ix1 - ix0);
    uint64_t dy = (uint64_t)(iy1 - iy0);
    uint32_t rect_cov = (dx == IM_ONE && dy == IM_ONE) ? IM_ONE : (uint32_t)((dx * dy + IM_ONE / 2) >> 16);
    if (!has_rounded) return rect_cov;

    /* Combine non-rounded intersected box coverage with each rounded entry's exact shape coverage. */
    uint32_t cov = IM_ONE;
    bool any_non_rounded = false;
    int64_t rx0 = (int64_t)x * IM_ONE, rx1 = rx0 + IM_ONE;
    int64_t ry0 = (int64_t)y * IM_ONE, ry1 = ry0 + IM_ONE;
    for (uint32_t i = from_idx; i < to_idx; i++) {
        const im_clip_entry *e = &stack->entries[i];
        if (!e->is_rounded) {
            any_non_rounded = true;
            if (e->own_left > rx0) rx0 = e->own_left;
            if (e->own_right < rx1) rx1 = e->own_right;
            if (e->own_top > ry0) ry0 = e->own_top;
            if (e->own_bottom < ry1) ry1 = e->own_bottom;
        } else {
            uint32_t c = im_shape_coverage(e->rect, e->radii, x, y);
            if (c == 0) return 0;
            if (c != IM_ONE) {
                cov = (uint32_t)(((uint64_t)cov * c + IM_ONE / 2) >> 16);
                if (cov == 0) return 0;
            }
        }
    }
    if (any_non_rounded) {
        if (rx1 <= rx0 || ry1 <= ry0) return 0;
        uint64_t rdx = (uint64_t)(rx1 - rx0);
        uint64_t rdy = (uint64_t)(ry1 - ry0);
        if (rdx != IM_ONE || rdy != IM_ONE) {
            uint32_t rc = (uint32_t)((rdx * rdy + IM_ONE / 2) >> 16);
            cov = (uint32_t)(((uint64_t)cov * rc + IM_ONE / 2) >> 16);
        }
    }
    return cov;
}

uint32_t im_clip_pixel_coverage(const im_clip_stack *stack, int x, int y) {
    if (!stack || stack->count == 0) return IM_ONE;
    return im_clip_pixel_coverage_range(stack, 0u, stack->count, x, y);
}
