/* Impossible UI: shared node-to-device transform mapping and pointer hit-testing. Portable. */
#include "input.h"

static bool apply_origin_transform(const im_node *node, const im_transform *tr,
                                   im_fixed origin_x, im_fixed origin_y, im_transform *out) {
    int64_t w = (int64_t)node->absolute_rect.right - node->absolute_rect.left;
    int64_t h = (int64_t)node->absolute_rect.bottom - node->absolute_rect.top;
    im_fixed ox = (im_fixed)(((int64_t)w * (int64_t)origin_x + (IM_ONE / 2)) >> 16);
    im_fixed oy = (im_fixed)(((int64_t)h * (int64_t)origin_y + (IM_ONE / 2)) >> 16);
    im_transform to_orig = im_transform_translate(-ox, -oy);
    im_transform from_orig = im_transform_translate(ox, oy);
    im_transform tmp;
    if (!im_transform_multiply(&to_orig, tr, &tmp)) return false;
    return im_transform_multiply(&tmp, &from_orig, out);
}

static bool node_local_render_transform(const im_node *node, im_transform *out) {
    if (!node || !out) return false;
    im_transform acc = im_transform_identity();
    if (node->has_render_transform) {
        if (!apply_origin_transform(node, &node->render_transform,
                                    node->render_transform_origin_x, node->render_transform_origin_y, &acc)) {
            return false;
        }
    }
    if (node->has_transition_transform) {
        im_transform ttr;
        if (!apply_origin_transform(node, &node->transition_transform,
                                    node->transition_origin_x, node->transition_origin_y, &ttr)) {
            return false;
        }
        im_transform comb;
        if (!im_transform_multiply(&acc, &ttr, &comb)) return false;
        acc = comb;
    }
    *out = acc;
    return true;
}

static bool node_logical_transform(const im_node *node, im_transform *out) {
    if (!node || !out) return false;
    for (const im_node *cur = node; cur != NULL; cur = cur->parent) {
        if (cur->visibility == IM_VIS_COLLAPSED) return false;
    }

    /* Start at node-local coordinates (0, 0) -> apply node's render_transform around node origin ->
       translate to root-space absolute_rect -> for each ancestor (child-to-root), apply ancestor's
       render_transform around that ancestor's origin in root coordinates. */
    im_transform acc;
    if (!node_local_render_transform(node, &acc)) return false;
    im_transform to_root = im_transform_translate(node->absolute_rect.left, node->absolute_rect.top);
    im_transform tmp;
    if (!im_transform_multiply(&acc, &to_root, &tmp)) return false;
    acc = tmp;

    for (const im_node *anc = node->parent; anc != NULL; anc = anc->parent) {
        if (anc->has_viewbox_transform) {
            /* Viewbox scales its child subtree around the Viewbox's own top-left corner,
               independently of both the child's and the Viewbox's RenderTransform. */
            im_transform vt1 = im_transform_translate(-anc->absolute_rect.left, -anc->absolute_rect.top);
            im_transform vt2 = im_transform_translate(anc->absolute_rect.left, anc->absolute_rect.top);
            im_transform vm1, vm2;
            if (!im_transform_multiply(&acc, &vt1, &vm1)) return false;
            if (!im_transform_multiply(&vm1, &anc->viewbox_transform, &vm2)) return false;
            if (!im_transform_multiply(&vm2, &vt2, &acc)) return false;
        }
        if (anc->has_render_transform) {
            int64_t aw = (int64_t)anc->absolute_rect.right - anc->absolute_rect.left;
            int64_t ah = (int64_t)anc->absolute_rect.bottom - anc->absolute_rect.top;
            int64_t aox = (int64_t)anc->absolute_rect.left + (((int64_t)aw * (int64_t)anc->render_transform_origin_x + (IM_ONE / 2)) >> 16);
            int64_t aoy = (int64_t)anc->absolute_rect.top + (((int64_t)ah * (int64_t)anc->render_transform_origin_y + (IM_ONE / 2)) >> 16);
            if (aox < INT32_MIN || aox > INT32_MAX || aoy < INT32_MIN || aoy > INT32_MAX) return false;
            im_transform t1 = im_transform_translate(-(im_fixed)aox, -(im_fixed)aoy);
            im_transform t2 = im_transform_translate((im_fixed)aox, (im_fixed)aoy);
            im_transform m1, m2;
            if (!im_transform_multiply(&acc, &t1, &m1)) return false;
            if (!im_transform_multiply(&m1, &anc->render_transform, &m2)) return false;
            if (!im_transform_multiply(&m2, &t2, &acc)) return false;
        }
        if (anc->has_transition_transform) {
            int64_t aw = (int64_t)anc->absolute_rect.right - anc->absolute_rect.left;
            int64_t ah = (int64_t)anc->absolute_rect.bottom - anc->absolute_rect.top;
            int64_t aox = (int64_t)anc->absolute_rect.left + (((int64_t)aw * (int64_t)anc->transition_origin_x + (IM_ONE / 2)) >> 16);
            int64_t aoy = (int64_t)anc->absolute_rect.top + (((int64_t)ah * (int64_t)anc->transition_origin_y + (IM_ONE / 2)) >> 16);
            if (aox < INT32_MIN || aox > INT32_MAX || aoy < INT32_MIN || aoy > INT32_MAX) return false;
            im_transform t1 = im_transform_translate(-(im_fixed)aox, -(im_fixed)aoy);
            im_transform t2 = im_transform_translate((im_fixed)aox, (im_fixed)aoy);
            im_transform m1, m2;
            if (!im_transform_multiply(&acc, &t1, &m1)) return false;
            if (!im_transform_multiply(&m1, &anc->transition_transform, &m2)) return false;
            if (!im_transform_multiply(&m2, &t2, &acc)) return false;
        }
    }

    *out = acc;
    return true;
}

bool im_node_device_transform(const im_node *node, im_fixed scale, im_transform *out) {
    if (!node || !out || scale <= 0) return false;
    im_transform logical;
    if (!node_logical_transform(node, &logical)) return false;
    if (scale == IM_ONE) {
        *out = logical;
        return true;
    }
    im_transform sc = im_transform_scale(scale, scale);
    return im_transform_multiply(&logical, &sc, out);
}

static im_fixed q30_128_to_fixed_floor_sat(__int128 q) {
    const int shift = IM_TR_SHIFT - 16; /* 14 */
    const __int128 step = ((__int128)1) << shift;
    __int128 f = (q >= 0) ? (q >> shift) : -(((-q) + step - 1) >> shift);
    if (f < (__int128)INT32_MIN) return INT32_MIN;
    if (f > (__int128)INT32_MAX) return INT32_MAX;
    return (im_fixed)f;
}

static im_fixed q30_128_to_fixed_ceil_sat(__int128 q) {
    const int shift = IM_TR_SHIFT - 16; /* 14 */
    const __int128 step = ((__int128)1) << shift;
    __int128 c = (q >= 0) ? ((q + step - 1) >> shift) : -((-q) >> shift);
    if (c < (__int128)INT32_MIN) return INT32_MIN;
    if (c > (__int128)INT32_MAX) return INT32_MAX;
    return (im_fixed)c;
}

bool im_node_transformed_logical_bounds(const im_node *node, im_rect *out_logical) {
    if (!node || !out_logical) return false;
    int64_t w_q30 = im_fixed_to_q30(node->absolute_rect.right) - im_fixed_to_q30(node->absolute_rect.left);
    int64_t h_q30 = im_fixed_to_q30(node->absolute_rect.bottom) - im_fixed_to_q30(node->absolute_rect.top);
    if (w_q30 <= 0 || h_q30 <= 0) return false;
    im_transform logical;
    if (!node_logical_transform(node, &logical)) return false;

    int64_t cx[4] = {0, w_q30, w_q30, 0};
    int64_t cy[4] = {0, 0, h_q30, h_q30};
    __int128 min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    for (int i = 0; i < 4; i++) {
        __int128 sx = (__int128)cx[i] * logical.m11 + (__int128)cy[i] * logical.m21;
        __int128 sy = (__int128)cx[i] * logical.m12 + (__int128)cy[i] * logical.m22;
        __int128 rxf = ((sx >= 0) ? (sx >> IM_TR_SHIFT) : -(((-sx) + IM_TR_ONE - 1) >> IM_TR_SHIFT)) + logical.dx;
        __int128 rxc = ((sx >= 0) ? ((sx + IM_TR_ONE - 1) >> IM_TR_SHIFT) : -((-sx) >> IM_TR_SHIFT)) + logical.dx;
        __int128 ryf = ((sy >= 0) ? (sy >> IM_TR_SHIFT) : -(((-sy) + IM_TR_ONE - 1) >> IM_TR_SHIFT)) + logical.dy;
        __int128 ryc = ((sy >= 0) ? ((sy + IM_TR_ONE - 1) >> IM_TR_SHIFT) : -((-sy) >> IM_TR_SHIFT)) + logical.dy;
        if (i == 0 || rxf < min_x) min_x = rxf;
        if (i == 0 || rxc > max_x) max_x = rxc;
        if (i == 0 || ryf < min_y) min_y = ryf;
        if (i == 0 || ryc > max_y) max_y = ryc;
    }

    im_rect res;
    res.left = q30_128_to_fixed_floor_sat(min_x);
    res.top = q30_128_to_fixed_floor_sat(min_y);
    res.right = q30_128_to_fixed_ceil_sat(max_x);
    res.bottom = q30_128_to_fixed_ceil_sat(max_y);
    if (res.left >= res.right || res.top >= res.bottom) return false;
    *out_logical = res;
    return true;
}

bool im_input_to_local(const im_node *node, im_fixed scale,
                       im_fixed dev_x, im_fixed dev_y,
                       im_fixed *out_local_x, im_fixed *out_local_y) {
    if (!node || !out_local_x || !out_local_y || scale <= 0) return false;
    im_transform dev_tr, inv;
    if (!im_node_device_transform(node, scale, &dev_tr)) return false;
    if (!im_transform_invert(&dev_tr, &inv)) return false;
    return im_transform_point(&inv, dev_x, dev_y, out_local_x, out_local_y);
}

static im_node *hit_test_recursive(im_node *node, im_fixed scale,
                                   im_fixed dev_x, im_fixed dev_y,
                                   im_fixed *out_local_x, im_fixed *out_local_y) {
    if (!node || node->visibility == IM_VIS_COLLAPSED || node->layout_overflowed) return NULL;

    /* If this node's transform chain is singular, neither it nor its subtree can be hit. */
    im_transform dev_tr, inv;
    if (!im_node_device_transform(node, scale, &dev_tr)) return NULL;
    if (!im_transform_invert(&dev_tr, &inv)) return NULL;

    /* Front-to-back: visit children in reverse paint order first (even if child extends outside parent).
       For Canvas, O(N) fast-path when all Z-indices are equal; otherwise reverse stable Canvas.ZIndex order. */
    bool canvas_mixed_z = false;
    if (node->kind == IM_ELEM_CANVAS && node->child_count > 1u) {
        int32_t z0 = node->children[0]->canvas_z;
        for (uint32_t i = 1u; i < node->child_count; i++) {
            if (node->children[i]->canvas_z != z0) { canvas_mixed_z = true; break; }
        }
    }
    if (canvas_mixed_z) {
        int32_t prev_z = INT32_MAX;
        uint32_t prev_idx = UINT32_MAX;
        bool has_prev = false;
        for (uint32_t visited = 0u; visited < node->child_count; visited++) {
            uint32_t best = UINT32_MAX;
            for (uint32_t i = node->child_count; i > 0u; i--) {
                uint32_t idx = i - 1u;
                int32_t zi = node->children[idx]->canvas_z;
                if (has_prev && (zi > prev_z || (zi == prev_z && idx >= prev_idx))) continue;
                if (best == UINT32_MAX || zi > node->children[best]->canvas_z) {
                    best = idx;
                }
            }
            if (best == UINT32_MAX) break;
            im_node *hit = hit_test_recursive(node->children[best], scale, dev_x, dev_y, out_local_x, out_local_y);
            if (hit) return hit;
            prev_z = node->children[best]->canvas_z;
            prev_idx = best;
            has_prev = true;
        }
    } else {
        for (uint32_t i = node->child_count; i > 0u; i--) {
            im_node *hit = hit_test_recursive(node->children[i - 1u], scale, dev_x, dev_y, out_local_x, out_local_y);
            if (hit) return hit;
        }
    }

    int64_t lqx = 0, lqy = 0;
    if (!im_transform_point_q30(&inv, im_fixed_to_q30(dev_x), im_fixed_to_q30(dev_y), &lqx, &lqy)) return NULL;
    int64_t w_q30 = im_fixed_to_q30(node->absolute_rect.right) - im_fixed_to_q30(node->absolute_rect.left);
    int64_t h_q30 = im_fixed_to_q30(node->absolute_rect.bottom) - im_fixed_to_q30(node->absolute_rect.top);
    if (w_q30 > 0 && h_q30 > 0 && lqx >= 0 && lqx < w_q30 && lqy >= 0 && lqy < h_q30) {
        if (out_local_x || out_local_y) {
            im_fixed lx = 0, ly = 0;
            if (!im_transform_point(&inv, dev_x, dev_y, &lx, &ly)) return NULL;
            if (out_local_x) *out_local_x = lx;
            if (out_local_y) *out_local_y = ly;
        }
        return node;
    }
    return NULL;
}

im_node *im_input_hit_test(im_node *root, im_fixed scale,
                           im_fixed dev_x, im_fixed dev_y,
                           im_fixed *out_local_x, im_fixed *out_local_y) {
    if (!root || scale <= 0) return NULL;
    return hit_test_recursive(root, scale, dev_x, dev_y, out_local_x, out_local_y);
}
