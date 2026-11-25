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

static bool push_clip_impl(im_clip_stack *stack, im_rect rect, im_radii radii, bool is_rounded) {
    if (!stack || stack->count == UINT32_MAX) return false;
    if (!ensure_clip_cap(stack, stack->count + 1u)) return false;

    int pl = 0, pt = 0, pr = stack->surface_w, pb = stack->surface_h;
    if (stack->count > 0) {
        const im_clip_entry *prev = &stack->entries[stack->count - 1u];
        pl = prev->int_left;
        pt = prev->int_top;
        pr = prev->int_right;
        pb = prev->int_bottom;
    }

    int cl, ct, cr, cb;
    if (rect.left >= rect.right || rect.top >= rect.bottom) {
        cl = pr; ct = pb; cr = pr; cb = pb;
    } else {
        cl = floor_px(rect.left, stack->surface_w);
        ct = floor_px(rect.top, stack->surface_h);
        cr = ceil_px(rect.right, stack->surface_w);
        cb = ceil_px(rect.bottom, stack->surface_h);
        if (cl < pl) cl = pl;
        if (ct < pt) ct = pt;
        if (cr > pr) cr = pr;
        if (cb > pb) cb = pb;
        if (cl > cr) cl = cr;
        if (ct > cb) ct = cb;
    }

    im_clip_entry *e = &stack->entries[stack->count++];
    e->rect = rect;
    e->radii = radii;
    e->is_rounded = is_rounded && (radii.top_left > 0 || radii.top_right > 0 || radii.bottom_right > 0 || radii.bottom_left > 0);
    e->int_left = cl;
    e->int_top = ct;
    e->int_right = cr;
    e->int_bottom = cb;
    return true;
}

bool im_clip_push_rect(im_clip_stack *stack, im_rect rect) {
    return push_clip_impl(stack, rect, (im_radii){0, 0, 0, 0}, false);
}

bool im_clip_push_rounded(im_clip_stack *stack, im_rect rect, im_radii radii) {
    return push_clip_impl(stack, rect, radii, true);
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

static uint32_t entry_coverage(const im_clip_entry *e, int x, int y) {
    if (x < e->int_left || x >= e->int_right || y < e->int_top || y >= e->int_bottom) return 0;
    int64_t px0 = (int64_t)x * IM_ONE, px1 = px0 + IM_ONE;
    int64_t py0 = (int64_t)y * IM_ONE, py1 = py0 + IM_ONE;
    if (!e->is_rounded) {
        int64_t x0 = px0 > e->rect.left ? px0 : e->rect.left;
        int64_t x1 = px1 < e->rect.right ? px1 : e->rect.right;
        int64_t y0 = py0 > e->rect.top ? py0 : e->rect.top;
        int64_t y1 = py1 < e->rect.bottom ? py1 : e->rect.bottom;
        if (x1 <= x0 || y1 <= y0) return 0;
        uint64_t dx = (uint64_t)(x1 - x0);
        uint64_t dy = (uint64_t)(y1 - y0);
        if (dx == IM_ONE && dy == IM_ONE) return IM_ONE;
        return (uint32_t)((dx * dy + IM_ONE / 2) >> 16);
    }
    return im_shape_coverage(e->rect, e->radii, x, y);
}

uint32_t im_clip_pixel_coverage(const im_clip_stack *stack, int x, int y) {
    if (!stack || stack->count == 0) return IM_ONE;
    const im_clip_entry *top = &stack->entries[stack->count - 1u];
    if (x < top->int_left || x >= top->int_right || y < top->int_top || y >= top->int_bottom) return 0;
    uint32_t cov = IM_ONE;
    for (uint32_t i = 0; i < stack->count; i++) {
        uint32_t c = entry_coverage(&stack->entries[i], x, y);
        if (c == 0) return 0;
        if (c != IM_ONE) {
            cov = (uint32_t)(((uint64_t)cov * c + IM_ONE / 2) >> 16);
            if (cov == 0) return 0;
        }
    }
    return cov;
}
