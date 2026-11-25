/* Impossible UI: shared node-to-device transform mapping and pointer hit-testing. Portable. */
#include "input.h"

static bool node_local_render_transform(const im_node *node, im_transform *out) {
    if (!node || !out) return false;
    if (!node->has_render_transform) {
        *out = im_transform_identity();
        return true;
    }
    int64_t w = (int64_t)node->absolute_rect.right - node->absolute_rect.left;
    int64_t h = (int64_t)node->absolute_rect.bottom - node->absolute_rect.top;
    im_fixed ox = (im_fixed)(((int64_t)w * (int64_t)node->render_transform_origin_x + (IM_ONE / 2)) >> 16);
    im_fixed oy = (im_fixed)(((int64_t)h * (int64_t)node->render_transform_origin_y + (IM_ONE / 2)) >> 16);
    im_transform to_orig = im_transform_translate(-ox, -oy);
    im_transform from_orig = im_transform_translate(ox, oy);
    im_transform tmp;
    if (!im_transform_multiply(&to_orig, &node->render_transform, &tmp)) return false;
    return im_transform_multiply(&tmp, &from_orig, out);
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
        if (!anc->has_render_transform) continue;
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

bool im_node_transformed_logical_bounds(const im_node *node, im_rect *out_logical) {
    if (!node || !out_logical) return false;
    im_fixed w = (im_fixed)((int64_t)node->absolute_rect.right - node->absolute_rect.left);
    im_fixed h = (im_fixed)((int64_t)node->absolute_rect.bottom - node->absolute_rect.top);
    if (w <= 0 || h <= 0) return false;
    im_transform logical;
    if (!node_logical_transform(node, &logical)) return false;
    return im_transform_rect_bounds(&logical, (im_rect){0, 0, w, h}, out_logical);
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
    if (!node || node->visibility == IM_VIS_COLLAPSED) return NULL;

    /* If this node's transform chain is singular, neither it nor its subtree can be hit. */
    im_transform dev_tr, inv;
    if (!im_node_device_transform(node, scale, &dev_tr)) return NULL;
    if (!im_transform_invert(&dev_tr, &inv)) return NULL;

    /* Front-to-back: visit children in reverse paint order first (even if child extends outside parent). */
    for (uint32_t i = node->child_count; i > 0u; i--) {
        im_node *hit = hit_test_recursive(node->children[i - 1u], scale, dev_x, dev_y, out_local_x, out_local_y);
        if (hit) return hit;
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
