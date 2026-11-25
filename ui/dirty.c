/* Impossible UI: dirty-rectangle collection, bounded merging, and scaled damage conversion. Portable. */
#include "dirty.h"
#include "input.h"

static bool rect_empty(im_irect r) {
    return r.left >= r.right || r.top >= r.bottom;
}

static bool rect_contains(im_irect outer, im_irect inner) {
    return outer.left <= inner.left && outer.top <= inner.top &&
           outer.right >= inner.right && outer.bottom >= inner.bottom;
}

static im_irect rect_union(im_irect a, im_irect b) {
    return (im_irect){
        a.left < b.left ? a.left : b.left,
        a.top < b.top ? a.top : b.top,
        a.right > b.right ? a.right : b.right,
        a.bottom > b.bottom ? a.bottom : b.bottom
    };
}

static int64_t rect_area(im_irect r) {
    if (rect_empty(r)) return 0;
    return (int64_t)(r.right - r.left) * (int64_t)(r.bottom - r.top);
}

static int64_t rect_intersection_area(im_irect a, im_irect b) {
    im_irect inter = {
        a.left > b.left ? a.left : b.left,
        a.top > b.top ? a.top : b.top,
        a.right < b.right ? a.right : b.right,
        a.bottom < b.bottom ? a.bottom : b.bottom
    };
    return rect_area(inter);
}

static bool rect_touches_zero_waste(im_irect a, im_irect b) {
    im_irect u = rect_union(a, b);
    int64_t covered = rect_area(a) + rect_area(b) - rect_intersection_area(a, b);
    return rect_area(u) <= covered;
}

static im_irect clip_to_viewport(const im_dirty_list *list, im_irect r) {
    if (!list || list->width <= 0 || list->height <= 0) return (im_irect){0, 0, 0, 0};
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > list->width) r.right = list->width;
    if (r.bottom > list->height) r.bottom = list->height;
    if (r.left >= r.right || r.top >= r.bottom) return (im_irect){0, 0, 0, 0};
    return r;
}

void im_dirty_init(im_dirty_list *list, int32_t width, int32_t height) {
    if (!list) return;
    list->count = 0u;
    list->width = width > 0 ? width : 0;
    list->height = height > 0 ? height : 0;
    list->last_scale = IM_ONE;
    list->has_scale = false;
}

void im_dirty_clear(im_dirty_list *list) {
    if (!list) return;
    list->count = 0u;
}

void im_dirty_set_viewport(im_dirty_list *list, int32_t width, int32_t height) {
    if (!list) return;
    int32_t nw = width > 0 ? width : 0;
    int32_t nh = height > 0 ? height : 0;
    if (nw == list->width && nh == list->height) return;
    list->width = nw;
    list->height = nh;
    list->count = 0u;
    if (nw > 0 && nh > 0) {
        list->rects[0] = (im_irect){0, 0, nw, nh};
        list->count = 1u;
    }
}

bool im_dirty_add(im_dirty_list *list, im_irect rect) {
    if (!list) return false;
    im_irect r = clip_to_viewport(list, rect);
    if (rect_empty(r)) return false;

    /* Check containment and exact-fit merges until fixed point. */
    for (;;) {
        for (uint32_t i = 0; i < list->count; i++) {
            if (rect_contains(list->rects[i], r)) return true;
        }
        bool merged = false;
        for (uint32_t i = 0; i < list->count; i++) {
            if (rect_contains(r, list->rects[i]) || rect_touches_zero_waste(list->rects[i], r)) {
                r = rect_union(r, list->rects[i]);
                for (uint32_t j = i + 1u; j < list->count; j++) {
                    list->rects[j - 1u] = list->rects[j];
                }
                list->count--;
                merged = true;
                break;
            }
        }
        if (!merged) break;
    }

    if (list->count < IM_DIRTY_MAX_RECTS) {
        list->rects[list->count++] = r;
        return true;
    }

    /* Count is at IM_DIRTY_MAX_RECTS (16): merge the pair among the 17 candidates
       with minimum added waste area (deterministic lowest (i, j) tie-break). */
    im_irect cand[IM_DIRTY_MAX_RECTS + 1];
    for (uint32_t i = 0; i < IM_DIRTY_MAX_RECTS; i++) cand[i] = list->rects[i];
    cand[IM_DIRTY_MAX_RECTS] = r;

    uint32_t best_i = 0u;
    uint32_t best_j = 1u;
    int64_t best_waste = INT64_MAX;
    for (uint32_t i = 0; i < IM_DIRTY_MAX_RECTS; i++) {
        for (uint32_t j = i + 1u; j <= IM_DIRTY_MAX_RECTS; j++) {
            im_irect u = rect_union(cand[i], cand[j]);
            int64_t covered = rect_area(cand[i]) + rect_area(cand[j]) - rect_intersection_area(cand[i], cand[j]);
            int64_t waste = rect_area(u) - covered;
            if (waste < best_waste) {
                best_waste = waste;
                best_i = i;
                best_j = j;
            }
        }
    }

    cand[best_i] = rect_union(cand[best_i], cand[best_j]);
    for (uint32_t k = best_j + 1u; k <= IM_DIRTY_MAX_RECTS; k++) {
        cand[k - 1u] = cand[k];
    }
    for (uint32_t i = 0; i < IM_DIRTY_MAX_RECTS; i++) {
        list->rects[i] = cand[i];
    }
    list->count = IM_DIRTY_MAX_RECTS;
    return true;
}

static int32_t floor_32_32(int64_t v) {
    const int64_t one = 1LL << 32;
    int64_t q = (v >= 0) ? (v >> 32) : -(((-v) + one - 1) >> 32);
    if (q < INT32_MIN) return INT32_MIN;
    if (q > INT32_MAX) return INT32_MAX;
    return (int32_t)q;
}

static int32_t ceil_32_32(int64_t v) {
    const int64_t one = 1LL << 32;
    int64_t q = (v >= 0) ? ((v + one - 1) >> 32) : -((-v) >> 32);
    if (q < INT32_MIN) return INT32_MIN;
    if (q > INT32_MAX) return INT32_MAX;
    return (int32_t)q;
}

bool im_dirty_add_scaled(im_dirty_list *list, im_rect logical_rect, im_fixed scale) {
    if (!list || scale <= 0) return false;
    if (logical_rect.left >= logical_rect.right || logical_rect.top >= logical_rect.bottom) return false;
    im_irect dev = {
        floor_32_32((int64_t)logical_rect.left * (int64_t)scale),
        floor_32_32((int64_t)logical_rect.top * (int64_t)scale),
        ceil_32_32((int64_t)logical_rect.right * (int64_t)scale),
        ceil_32_32((int64_t)logical_rect.bottom * (int64_t)scale)
    };
    return im_dirty_add(list, dev);
}

static bool fixed_rect_eq(im_rect a, im_rect b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static void collect_node_recursive(im_dirty_list *list, im_node *node, im_fixed scale,
                                   bool ancestor_collapsed, bool ancestor_transformed) {
    if (!node) return;
    bool visible = !ancestor_collapsed && node->visibility != IM_VIS_COLLAPSED && !node->layout_overflowed;
    bool transformed = ancestor_transformed || node->has_render_transform || node->has_viewbox_transform || node->has_transition_transform;
    bool has_outset = node->paint_outset.left > 0 || node->paint_outset.top > 0 ||
                      node->paint_outset.right > 0 || node->paint_outset.bottom > 0;
    if (transformed || has_outset) {
        int dl = 0, dt = 0, dr = 0, db = 0;
        bool cur_dev_nonempty = false;
        if (visible) {
            int64_t w_q30 = im_fixed_to_q30(node->absolute_rect.right) - im_fixed_to_q30(node->absolute_rect.left);
            int64_t h_q30 = im_fixed_to_q30(node->absolute_rect.bottom) - im_fixed_to_q30(node->absolute_rect.top);
            int64_t l_q30 = -im_fixed_to_q30(node->paint_outset.left > 0 ? node->paint_outset.left : 0);
            int64_t t_q30 = -im_fixed_to_q30(node->paint_outset.top > 0 ? node->paint_outset.top : 0);
            int64_t r_q30 = w_q30 + im_fixed_to_q30(node->paint_outset.right > 0 ? node->paint_outset.right : 0);
            int64_t b_q30 = h_q30 + im_fixed_to_q30(node->paint_outset.bottom > 0 ? node->paint_outset.bottom : 0);
            im_transform dev_tr;
            if (w_q30 > 0 && h_q30 > 0 &&
                im_node_device_transform(node, scale, &dev_tr) &&
                im_transform_viewport_bounds_q30(&dev_tr, l_q30, t_q30, r_q30, b_q30,
                                                 0, 0, list->width, list->height,
                                                 &dl, &dt, &dr, &db)) {
                cur_dev_nonempty = true;
            }
        }
        bool changed = node->paint_dirty || node->had_prev_rect ||
                       (cur_dev_nonempty != node->had_prev_device_rect) ||
                       (cur_dev_nonempty &&
                        (dl != node->prev_dev_left || dt != node->prev_dev_top ||
                         dr != node->prev_dev_right || db != node->prev_dev_bottom));
        if (changed) {
            if (node->had_prev_rect) {
                im_dirty_add_scaled(list, node->prev_absolute_rect, scale);
            }
            if (node->had_prev_device_rect) {
                im_dirty_add(list, (im_irect){node->prev_dev_left, node->prev_dev_top,
                                              node->prev_dev_right, node->prev_dev_bottom});
            }
            if (cur_dev_nonempty) {
                im_dirty_add(list, (im_irect){dl, dt, dr, db});
            }
        }
        node->had_prev_rect = false;
        node->prev_absolute_rect = (im_rect){0, 0, 0, 0};
        node->had_prev_device_rect = cur_dev_nonempty;
        node->prev_dev_left = cur_dev_nonempty ? dl : 0;
        node->prev_dev_top = cur_dev_nonempty ? dt : 0;
        node->prev_dev_right = cur_dev_nonempty ? dr : 0;
        node->prev_dev_bottom = cur_dev_nonempty ? db : 0;
        node->paint_dirty = false;
    } else {
        im_rect cur = {0, 0, 0, 0};
        bool cur_nonempty = false;
        if (visible) {
            cur = node->absolute_rect;
            cur_nonempty = cur.left < cur.right && cur.top < cur.bottom;
        }
        bool changed = node->paint_dirty || node->had_prev_device_rect ||
                       (cur_nonempty != node->had_prev_rect) ||
                       (cur_nonempty && !fixed_rect_eq(cur, node->prev_absolute_rect));
        if (changed) {
            if (node->had_prev_device_rect) {
                im_dirty_add(list, (im_irect){node->prev_dev_left, node->prev_dev_top,
                                              node->prev_dev_right, node->prev_dev_bottom});
            }
            if (node->had_prev_rect) {
                im_dirty_add_scaled(list, node->prev_absolute_rect, scale);
            }
            if (cur_nonempty) {
                im_dirty_add_scaled(list, cur, scale);
            }
        }
        node->had_prev_device_rect = false;
        node->prev_dev_left = node->prev_dev_top = node->prev_dev_right = node->prev_dev_bottom = 0;
        node->had_prev_rect = cur_nonempty;
        node->prev_absolute_rect = cur_nonempty ? cur : (im_rect){0, 0, 0, 0};
        node->paint_dirty = false;
    }

    for (uint32_t i = 0; i < node->child_count; i++) {
        collect_node_recursive(list, node->children[i], scale, !visible, transformed);
    }
}

void im_dirty_collect_tree(im_dirty_list *list, im_tree *tree, im_node *root, im_fixed scale) {
    if (!list || scale <= 0) return;

    /* If scale changed since the last frame, the entire backing image must repaint. */
    if (list->has_scale && list->last_scale != scale) {
        im_dirty_add(list, (im_irect){0, 0, list->width, list->height});
    }
    list->last_scale = scale;
    list->has_scale = true;

    /* Drain detached/destroyed subtree bounds queued on the tree. */
    if (tree) {
        if (tree->pending_damage_overflow) {
            im_dirty_add(list, (im_irect){0, 0, list->width, list->height});
        } else {
            for (uint32_t i = 0; i < tree->pending_damage_count; i++) {
                im_dirty_add_scaled(list, tree->pending_damage[i], scale);
            }
            for (uint32_t i = 0; i < tree->pending_dev_damage_count; i++) {
                const int32_t *r = tree->pending_dev_damage[i];
                im_dirty_add(list, (im_irect){r[0], r[1], r[2], r[3]});
            }
        }
        tree->pending_damage_count = 0u;
        tree->pending_dev_damage_count = 0u;
        tree->pending_damage_overflow = false;
    }

    if (root) {
        collect_node_recursive(list, root, scale, false, false);
    }
}

bool im_dirty_covers(const im_dirty_list *list, im_irect rect) {
    if (!list) return false;
    im_irect target = clip_to_viewport(list, rect);
    if (rect_empty(target)) return true;
    /* Check if target is completely covered by a single rect first (fast path). */
    for (uint32_t i = 0; i < list->count; i++) {
        if (rect_contains(list->rects[i], target)) return true;
    }
    /* Exact sweep over X/Y coordinates of target and list rects (at most 34 coordinates per axis). */
    int32_t xs[2 * IM_DIRTY_MAX_RECTS + 2];
    int32_t ys[2 * IM_DIRTY_MAX_RECTS + 2];
    uint32_t nx = 0u, ny = 0u;
    xs[nx++] = target.left;
    xs[nx++] = target.right;
    ys[ny++] = target.top;
    ys[ny++] = target.bottom;
    for (uint32_t i = 0; i < list->count; i++) {
        im_irect r = list->rects[i];
        if (r.left > target.left && r.left < target.right) xs[nx++] = r.left;
        if (r.right > target.left && r.right < target.right) xs[nx++] = r.right;
        if (r.top > target.top && r.top < target.bottom) ys[ny++] = r.top;
        if (r.bottom > target.top && r.bottom < target.bottom) ys[ny++] = r.bottom;
    }
    for (uint32_t i = 0; i < nx; i++) {
        for (uint32_t j = i + 1u; j < nx; j++) {
            if (xs[j] < xs[i]) { int32_t t = xs[i]; xs[i] = xs[j]; xs[j] = t; }
        }
    }
    for (uint32_t i = 0; i < ny; i++) {
        for (uint32_t j = i + 1u; j < ny; j++) {
            if (ys[j] < ys[i]) { int32_t t = ys[i]; ys[i] = ys[j]; ys[j] = t; }
        }
    }
    for (uint32_t xi = 0; xi + 1u < nx; xi++) {
        if (xs[xi] == xs[xi + 1u]) continue;
        for (uint32_t yi = 0; yi + 1u < ny; yi++) {
            if (ys[yi] == ys[yi + 1u]) continue;
            im_irect cell = {xs[xi], ys[yi], xs[xi + 1u], ys[yi + 1u]};
            bool covered = false;
            for (uint32_t k = 0; k < list->count; k++) {
                if (rect_contains(list->rects[k], cell)) { covered = true; break; }
            }
            if (!covered) return false;
        }
    }
    return true;
}

bool im_dirty_push_clip(im_layer_ctx *ctx, im_irect rect) {
    if (!ctx || rect_empty(rect)) return false;
    return im_layer_push_clip_irect(ctx, rect.left, rect.top, rect.right, rect.bottom);
}
