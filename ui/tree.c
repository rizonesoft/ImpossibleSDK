/* Impossible UI: element tree, properties, template instantiation and invalidation. Portable. */
#include "tree.h"

static void zero_bytes(void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static bool thickness_eq(im_thickness a, im_thickness b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static bool is_non_visual_kind(im_element_kind kind) {
    switch (kind) {
    case IM_ELEM_ROW_DEFINITION:
    case IM_ELEM_COLUMN_DEFINITION:
    case IM_ELEM_TRANSLATE_TRANSFORM:
    case IM_ELEM_SCALE_TRANSFORM:
    case IM_ELEM_COMPOSITE_TRANSFORM:
    case IM_ELEM_RECTANGLE_GEOMETRY:
    case IM_ELEM_BRUSH_TRANSITION:
    case IM_ELEM_ADD_DELETE_THEME_TRANSITION:
    case IM_ELEM_CONTENT_THEME_TRANSITION:
    case IM_ELEM_REORDER_THEME_TRANSITION:
    case IM_ELEM_ENTRANCE_THEME_TRANSITION:
        return true;
    default:
        return false;
    }
}

bool im_tree_init(im_tree *tree, im_allocator alloc) {
    if (!tree || !alloc.alloc || !alloc.realloc || !alloc.free) return false;
    tree->alloc = alloc;
    tree->root = NULL;
    tree->node_count = 0;
    tree->pending_damage_count = 0u;
    tree->pending_dev_damage_count = 0u;
    tree->pending_damage_overflow = false;
    return true;
}

static void destroy_node_recursive(im_tree *tree, im_node *node) {
    if (!node) return;
    for (uint32_t i = 0; i < node->child_count; i++) {
        destroy_node_recursive(tree, node->children[i]);
    }
    if (node->children) {
        tree->alloc.free(tree->alloc.ctx, node->children, (size_t)node->child_cap * sizeof(im_node *));
    }
    if (node->rows) {
        tree->alloc.free(tree->alloc.ctx, node->rows, (size_t)node->row_count * sizeof(im_grid_def));
    }
    if (node->cols) {
        tree->alloc.free(tree->alloc.ctx, node->cols, (size_t)node->col_count * sizeof(im_grid_def));
    }
    tree->alloc.free(tree->alloc.ctx, node, sizeof(im_node));
    if (tree->node_count > 0) tree->node_count--;
}

void im_tree_destroy(im_tree *tree) {
    if (!tree) return;
    if (tree->root) {
        destroy_node_recursive(tree, tree->root);
        tree->root = NULL;
    }
}

im_node *im_tree_create_node(im_tree *tree, im_element_kind kind, const char *name) {
    if (!tree || !tree->alloc.alloc) return NULL;
    im_node *node = (im_node *)tree->alloc.alloc(tree->alloc.ctx, sizeof(im_node));
    if (!node) return NULL;
    zero_bytes(node, sizeof(im_node));
    node->kind = kind;
    node->name = name;
    node->width = IM_FIXED_AUTO;
    node->height = IM_FIXED_AUTO;
    node->min_width = 0;
    node->min_height = 0;
    node->max_width = IM_FIXED_INF;
    node->max_height = IM_FIXED_INF;
    node->h_align = IM_ALIGN_STRETCH;
    node->v_align = IM_ALIGN_STRETCH;
    node->h_content_align = IM_ALIGN_STRETCH;
    node->default_h_content_align = IM_ALIGN_STRETCH;
    node->v_content_align = IM_ALIGN_STRETCH;
    node->default_v_content_align = IM_ALIGN_STRETCH;
    if (kind == IM_ELEM_BUTTON) {
        node->h_content_align = IM_ALIGN_CENTER;
        node->default_h_content_align = IM_ALIGN_CENTER;
        node->v_content_align = IM_ALIGN_CENTER;
        node->default_v_content_align = IM_ALIGN_CENTER;
    }
    node->visibility = IM_VIS_VISIBLE;
    node->opacity = IM_ONE;
    node->base_opacity = IM_ONE;
    node->flow_direction = IM_FLOW_LTR;
    node->use_layout_rounding = true;
    node->orientation = IM_ORIENT_VERTICAL;
    node->grid_row_span = 1;
    node->grid_col_span = 1;
    node->measure_dirty = true;
    node->arrange_dirty = true;
    tree->node_count++;
    return node;
}

static void record_detached_damage(im_tree *tree, im_node *node) {
    if (!tree || !node) return;
    if (node->had_prev_rect) {
        if (tree->pending_damage_count < IM_TREE_PENDING_DAMAGE_CAP) {
            tree->pending_damage[tree->pending_damage_count++] = node->prev_absolute_rect;
        } else {
            tree->pending_damage_overflow = true;
        }
        node->had_prev_rect = false;
        node->prev_absolute_rect = (im_rect){0, 0, 0, 0};
    }
    if (node->had_prev_device_rect) {
        if (tree->pending_dev_damage_count < IM_TREE_PENDING_DAMAGE_CAP) {
            int32_t *dst = tree->pending_dev_damage[tree->pending_dev_damage_count++];
            dst[0] = node->prev_dev_left;
            dst[1] = node->prev_dev_top;
            dst[2] = node->prev_dev_right;
            dst[3] = node->prev_dev_bottom;
        } else {
            tree->pending_damage_overflow = true;
        }
        node->had_prev_device_rect = false;
        node->prev_dev_left = node->prev_dev_top = node->prev_dev_right = node->prev_dev_bottom = 0;
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        record_detached_damage(tree, node->children[i]);
    }
}

void im_tree_destroy_subtree(im_tree *tree, im_node *node) {
    if (!tree || !node) return;
    if (node->parent) {
        im_tree_remove_child(tree, node->parent, node);
    } else if (tree->root == node) {
        record_detached_damage(tree, node);
        tree->root = NULL;
    } else {
        record_detached_damage(tree, node);
    }
    destroy_node_recursive(tree, node);
}

static bool ensure_child_capacity(im_tree *tree, im_node *parent, uint32_t needed) {
    if (needed <= parent->child_cap) return true;
    uint32_t new_cap = parent->child_cap == 0 ? 4u : parent->child_cap;
    while (new_cap < needed) {
        if (new_cap > (UINT32_MAX / 2u)) return false;
        new_cap *= 2u;
    }
    if ((size_t)new_cap > (SIZE_MAX / sizeof(im_node *))) return false;
    size_t old_bytes = (size_t)parent->child_cap * sizeof(im_node *);
    size_t new_bytes = (size_t)new_cap * sizeof(im_node *);
    im_node **buf = (im_node **)tree->alloc.realloc(tree->alloc.ctx, parent->children, old_bytes, new_bytes);
    if (!buf) return false;
    parent->children = buf;
    parent->child_cap = new_cap;
    return true;
}

static bool is_ancestor(const im_node *ancestor, const im_node *node) {
    for (const im_node *cur = node; cur != NULL; cur = cur->parent) {
        if (cur == ancestor) return true;
    }
    return false;
}

static bool is_in_subtree(const im_node *root, const im_node *target) {
    if (!root || !target) return false;
    if (root == target) return true;
    for (uint32_t i = 0; i < root->child_count; i++) {
        if (is_in_subtree(root->children[i], target)) return true;
    }
    return false;
}

static void detach_external_templated_parents(im_node *subtree_root, im_node *node) {
    if (!node) return;
    if (node->templated_parent && !is_in_subtree(subtree_root, node->templated_parent)) {
        node->templated_parent = NULL;
        im_tree_invalidate_measure(node);
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        detach_external_templated_parents(subtree_root, node->children[i]);
    }
}

bool im_tree_insert_child(im_tree *tree, im_node *parent, uint32_t index, im_node *child) {
    if (!tree || !parent || !child || child->parent != NULL || child == parent) return false;
    if (is_ancestor(child, parent)) return false;
    if (index > parent->child_count) return false;
    if (parent->child_count == UINT32_MAX) return false;
    if (!ensure_child_capacity(tree, parent, parent->child_count + 1u)) return false;
    for (uint32_t i = parent->child_count; i > index; i--) {
        parent->children[i] = parent->children[i - 1u];
    }
    parent->children[index] = child;
    parent->child_count++;
    child->parent = parent;
    im_tree_invalidate_measure(parent);
    return true;
}

bool im_tree_append_child(im_tree *tree, im_node *parent, im_node *child) {
    if (!parent) return false;
    return im_tree_insert_child(tree, parent, parent->child_count, child);
}

bool im_tree_remove_child(im_tree *tree, im_node *parent, im_node *child) {
    if (!parent || !child || child->parent != parent) return false;
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            if (tree) record_detached_damage(tree, child);
            for (uint32_t j = i; j + 1u < parent->child_count; j++) {
                parent->children[j] = parent->children[j + 1u];
            }
            parent->child_count--;
            child->parent = NULL;
            detach_external_templated_parents(child, child);
            im_tree_invalidate_measure(parent);
            return true;
        }
    }
    return false;
}

im_node *im_tree_replace_child(im_tree *tree, im_node *parent, uint32_t index, im_node *replacement) {
    if (!parent || !replacement || replacement->parent != NULL || replacement == parent) return NULL;
    if (is_ancestor(replacement, parent)) return NULL;
    if (index >= parent->child_count) return NULL;
    im_node *old = parent->children[index];
    if (tree) record_detached_damage(tree, old);
    old->parent = NULL;
    detach_external_templated_parents(old, old);
    parent->children[index] = replacement;
    replacement->parent = parent;
    im_tree_invalidate_measure(parent);
    return old;
}

im_node *im_tree_find_by_name(im_node *root, const char *name) {
    if (!root || !name) return NULL;
    if (root->name && str_eq(root->name, name)) return root;
    for (uint32_t i = 0; i < root->child_count; i++) {
        im_node *found = im_tree_find_by_name(root->children[i], name);
        if (found) return found;
    }
    return NULL;
}

void im_tree_invalidate_measure(im_node *node) {
    im_node *cur = node;
    while (cur) {
        bool already = cur->measure_dirty && cur->arrange_dirty;
        cur->measure_dirty = true;
        cur->arrange_dirty = true;
        if (already) break;
        cur = cur->parent;
    }
}

void im_tree_invalidate_arrange(im_node *node) {
    im_node *cur = node;
    while (cur) {
        if (cur->arrange_dirty) break;
        cur->arrange_dirty = true;
        cur = cur->parent;
    }
}

void im_tree_invalidate_paint(im_node *node) {
    if (!node) return;
    node->paint_dirty = true;
}

static void mark_subtree_paint_dirty(im_node *node) {
    if (!node) return;
    node->paint_dirty = true;
    for (uint32_t i = 0; i < node->child_count; i++) {
        mark_subtree_paint_dirty(node->children[i]);
    }
}

void im_node_set_render_transform(im_node *node, const im_transform *transform, im_fixed origin_x, im_fixed origin_y) {
    if (!node || !transform) return;
    node->has_render_transform = true;
    node->render_transform = *transform;
    node->render_transform_origin_x = origin_x;
    node->render_transform_origin_y = origin_y;
    mark_subtree_paint_dirty(node);
}

void im_node_set_paint_outset(im_node *node, im_thickness outset) {
    if (!node) return;
    if (outset.left < 0) outset.left = 0;
    if (outset.top < 0) outset.top = 0;
    if (outset.right < 0) outset.right = 0;
    if (outset.bottom < 0) outset.bottom = 0;
    if (thickness_eq(node->paint_outset, outset)) return;
    node->paint_outset = outset;
    node->paint_dirty = true;
}

void im_node_set_path_intrinsic_bounds(im_node *node, const im_template_path_seg *segs,
                                       uint32_t start, uint32_t count) {
    if (!node) return;
    im_fixed nw = 0, nh = 0;
    if (segs && count > 0u) {
        bool has = false;
        int64_t min_x = INT64_MAX, min_y = INT64_MAX, max_x = INT64_MIN, max_y = INT64_MIN;
        for (uint32_t i = 0; i < count; i++) {
            const im_template_path_seg *s = &segs[start + i];
            int64_t xs[3], ys[3];
            int n = 0;
            if (s->cmd == 0u || s->cmd == 1u) { xs[0] = s->x1; ys[0] = s->y1; n = 1; }
            else if (s->cmd == 2u) { xs[0] = s->x1; ys[0] = s->y1; xs[1] = s->x2; ys[1] = s->y2; n = 2; }
            else if (s->cmd == 3u) { xs[0] = s->x1; ys[0] = s->y1; xs[1] = s->x2; ys[1] = s->y2; xs[2] = s->x3; ys[2] = s->y3; n = 3; }
            else if (s->cmd == 4u) { xs[0] = s->x2; ys[0] = s->y2; n = 1; }
            for (int k = 0; k < n; k++) {
                if (!has || xs[k] < min_x) min_x = xs[k];
                if (!has || xs[k] > max_x) max_x = xs[k];
                if (!has || ys[k] < min_y) min_y = ys[k];
                if (!has || ys[k] > max_y) max_y = ys[k];
                has = true;
            }
        }
        if (has) {
            int64_t dw = max_x - min_x;
            int64_t dh = max_y - min_y;
            if (dw > 0) nw = (dw > INT32_MAX) ? INT32_MAX : (im_fixed)dw;
            if (dh > 0) nh = (dh > INT32_MAX) ? INT32_MAX : (im_fixed)dh;
        }
    }
    if (node->intrinsic_width != nw || node->intrinsic_height != nh) {
        node->intrinsic_width = nw;
        node->intrinsic_height = nh;
        im_tree_invalidate_measure(node);
    }
}

void im_node_clear_render_transform(im_node *node) {
    if (!node) return;
    if (!node->has_render_transform) return;
    node->has_render_transform = false;
    node->render_transform = (im_transform){IM_TR_ONE, 0, 0, IM_TR_ONE, 0, 0};
    mark_subtree_paint_dirty(node);
}

static void invalidate_template_bindings(im_node *root, im_node *templated_parent, uint32_t bit) {
    if (!root) return;
    if (root->templated_parent == templated_parent && (root->bind_mask & bit)) {
        im_tree_invalidate_measure(root);
    }
    for (uint32_t i = 0; i < root->child_count; i++) {
        invalidate_template_bindings(root->children[i], templated_parent, bit);
    }
}

static bool set_grid_defs(im_tree *tree, im_grid_def **dst, uint32_t *dst_count, const im_grid_def *defs, uint32_t count) {
    if (!tree || !dst || !dst_count) return false;
    if (count > 0 && !defs) return false;
    if ((size_t)count > (SIZE_MAX / sizeof(im_grid_def))) return false;
    im_grid_def *buf = NULL;
    if (count > 0) {
        buf = (im_grid_def *)tree->alloc.alloc(tree->alloc.ctx, (size_t)count * sizeof(im_grid_def));
        if (!buf) return false;
        copy_bytes(buf, defs, (size_t)count * sizeof(im_grid_def));
        for (uint32_t i = 0; i < count; i++) {
            if (buf[i].max_size == 0 && defs[i].max_size == 0) {
                /* Keep explicit 0 max_size; callers set IM_FIXED_INF for unbounded. */
            }
        }
    }
    if (*dst) {
        tree->alloc.free(tree->alloc.ctx, *dst, (size_t)(*dst_count) * sizeof(im_grid_def));
    }
    *dst = buf;
    *dst_count = count;
    return true;
}

bool im_tree_set_grid_rows(im_tree *tree, im_node *grid, const im_grid_def *defs, uint32_t count) {
    if (!tree || !grid) return false;
    if (!set_grid_defs(tree, &grid->rows, &grid->row_count, defs, count)) return false;
    im_tree_invalidate_measure(grid);
    return true;
}

bool im_tree_set_grid_cols(im_tree *tree, im_node *grid, const im_grid_def *defs, uint32_t count) {
    if (!tree || !grid) return false;
    if (!set_grid_defs(tree, &grid->cols, &grid->col_count, defs, count)) return false;
    im_tree_invalidate_measure(grid);
    return true;
}

void im_node_set_size(im_node *node, im_fixed width, im_fixed height) {
    if (!node) return;
    bool changed = false;
    if (node->state_mask & IM_SBIT_WIDTH) {
        node->base_width = width;
    } else if (node->width != width) {
        node->width = width;
        changed = true;
    }
    if (node->state_mask & IM_SBIT_HEIGHT) {
        node->base_height = height;
    } else if (node->height != height) {
        node->height = height;
        changed = true;
    }
    if (changed) im_tree_invalidate_measure(node);
}

void im_node_set_min_max(im_node *node, im_fixed min_w, im_fixed min_h, im_fixed max_w, im_fixed max_h) {
    if (!node) return;
    if (node->min_width == min_w && node->min_height == min_h &&
        node->max_width == max_w && node->max_height == max_h) return;
    node->min_width = min_w;
    node->min_height = min_h;
    node->max_width = max_w;
    node->max_height = max_h;
    im_tree_invalidate_measure(node);
}

void im_node_set_intrinsic_size(im_node *node, im_fixed width, im_fixed height) {
    if (!node) return;
    if (node->intrinsic_width == width && node->intrinsic_height == height) return;
    node->intrinsic_width = width;
    node->intrinsic_height = height;
    im_tree_invalidate_measure(node);
}

void im_node_set_margin(im_node *node, im_thickness margin) {
    if (!node) return;
    if (node->state_mask & IM_SBIT_MARGIN) {
        node->base_margin = margin;
        return;
    }
    if (thickness_eq(node->margin, margin)) return;
    node->margin = margin;
    im_tree_invalidate_measure(node);
}

void im_node_set_padding(im_node *node, im_thickness padding) {
    if (!node) return;
    bool was_local = (node->local_mask & IM_PBIT_PADDING) != 0;
    if (was_local && thickness_eq(node->padding, padding)) return;
    node->padding = padding;
    node->local_mask |= IM_PBIT_PADDING;
    im_tree_invalidate_measure(node);
    invalidate_template_bindings(node, node, IM_PBIT_PADDING);
}

void im_node_clear_padding(im_node *node) {
    if (!node || !(node->local_mask & IM_PBIT_PADDING)) return;
    node->local_mask &= ~IM_PBIT_PADDING;
    node->padding = node->default_padding;
    im_tree_invalidate_measure(node);
    invalidate_template_bindings(node, node, IM_PBIT_PADDING);
}

void im_node_set_border_thickness(im_node *node, im_thickness thickness) {
    if (!node) return;
    bool was_local = (node->local_mask & IM_PBIT_BORDER_THICKNESS) != 0;
    if (node->state_mask & IM_SBIT_BORDER_THICKNESS) {
        node->base_border_thickness = thickness;
        node->local_mask |= IM_PBIT_BORDER_THICKNESS;
        return;
    }
    if (was_local && thickness_eq(node->border_thickness, thickness)) return;
    node->border_thickness = thickness;
    node->local_mask |= IM_PBIT_BORDER_THICKNESS;
    im_tree_invalidate_measure(node);
    invalidate_template_bindings(node, node, IM_PBIT_BORDER_THICKNESS);
}

void im_node_clear_border_thickness(im_node *node) {
    if (!node || !(node->local_mask & IM_PBIT_BORDER_THICKNESS)) return;
    node->local_mask &= ~IM_PBIT_BORDER_THICKNESS;
    if (node->state_mask & IM_SBIT_BORDER_THICKNESS) {
        node->base_border_thickness = node->default_border_thickness;
        return;
    }
    node->border_thickness = node->default_border_thickness;
    im_tree_invalidate_measure(node);
    invalidate_template_bindings(node, node, IM_PBIT_BORDER_THICKNESS);
}

void im_node_set_alignment(im_node *node, im_align h_align, im_align v_align) {
    if (!node) return;
    bool changed = false;
    if (node->state_mask & IM_SBIT_H_ALIGN) {
        node->base_h_align = h_align;
    } else if (node->h_align != h_align) {
        node->h_align = h_align;
        changed = true;
    }
    if (node->state_mask & IM_SBIT_V_ALIGN) {
        node->base_v_align = v_align;
    } else if (node->v_align != v_align) {
        node->v_align = v_align;
        changed = true;
    }
    if (changed) im_tree_invalidate_arrange(node);
}

void im_node_set_content_alignment(im_node *node, im_align h_align, im_align v_align) {
    if (!node) return;
    bool h_local = (node->local_mask & IM_PBIT_H_CONTENT_ALIGN) != 0;
    bool v_local = (node->local_mask & IM_PBIT_V_CONTENT_ALIGN) != 0;
    if (h_local && v_local && node->h_content_align == h_align && node->v_content_align == v_align) return;
    node->h_content_align = h_align;
    node->v_content_align = v_align;
    node->local_mask |= (IM_PBIT_H_CONTENT_ALIGN | IM_PBIT_V_CONTENT_ALIGN);
    im_tree_invalidate_measure(node);
    invalidate_template_bindings(node, node, IM_PBIT_H_CONTENT_ALIGN | IM_PBIT_V_CONTENT_ALIGN);
}

void im_node_clear_content_alignment(im_node *node) {
    if (!node) return;
    if (!(node->local_mask & (IM_PBIT_H_CONTENT_ALIGN | IM_PBIT_V_CONTENT_ALIGN))) return;
    node->local_mask &= ~(IM_PBIT_H_CONTENT_ALIGN | IM_PBIT_V_CONTENT_ALIGN);
    node->h_content_align = node->default_h_content_align;
    node->v_content_align = node->default_v_content_align;
    im_tree_invalidate_measure(node);
    invalidate_template_bindings(node, node, IM_PBIT_H_CONTENT_ALIGN | IM_PBIT_V_CONTENT_ALIGN);
}

void im_node_set_visibility(im_node *node, im_visibility visibility) {
    if (!node) return;
    if (node->state_mask & IM_SBIT_VISIBILITY) {
        node->base_visibility = visibility;
        return;
    }
    if (node->visibility == visibility) return;
    node->visibility = visibility;
    im_tree_invalidate_measure(node);
}

void im_node_set_state_width(im_node *node, bool active, im_fixed width) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_WIDTH)) {
            node->base_width = node->width;
            node->state_mask |= IM_SBIT_WIDTH;
        }
        if (node->width != width) {
            node->width = width;
            im_tree_invalidate_measure(node);
        }
    } else if (node->state_mask & IM_SBIT_WIDTH) {
        node->state_mask &= ~IM_SBIT_WIDTH;
        if (node->width != node->base_width) {
            node->width = node->base_width;
            im_tree_invalidate_measure(node);
        }
    }
}

void im_node_set_state_height(im_node *node, bool active, im_fixed height) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_HEIGHT)) {
            node->base_height = node->height;
            node->state_mask |= IM_SBIT_HEIGHT;
        }
        if (node->height != height) {
            node->height = height;
            im_tree_invalidate_measure(node);
        }
    } else if (node->state_mask & IM_SBIT_HEIGHT) {
        node->state_mask &= ~IM_SBIT_HEIGHT;
        if (node->height != node->base_height) {
            node->height = node->base_height;
            im_tree_invalidate_measure(node);
        }
    }
}

void im_node_set_state_margin(im_node *node, bool active, im_thickness margin) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_MARGIN)) {
            node->base_margin = node->margin;
            node->state_mask |= IM_SBIT_MARGIN;
        }
        if (!thickness_eq(node->margin, margin)) {
            node->margin = margin;
            im_tree_invalidate_measure(node);
        }
    } else if (node->state_mask & IM_SBIT_MARGIN) {
        node->state_mask &= ~IM_SBIT_MARGIN;
        if (!thickness_eq(node->margin, node->base_margin)) {
            node->margin = node->base_margin;
            im_tree_invalidate_measure(node);
        }
    }
}

void im_node_set_state_visibility(im_node *node, bool active, im_visibility visibility) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_VISIBILITY)) {
            node->base_visibility = node->visibility;
            node->state_mask |= IM_SBIT_VISIBILITY;
        }
        if (node->visibility != visibility) {
            node->visibility = visibility;
            im_tree_invalidate_measure(node);
        }
    } else if (node->state_mask & IM_SBIT_VISIBILITY) {
        node->state_mask &= ~IM_SBIT_VISIBILITY;
        if (node->visibility != node->base_visibility) {
            node->visibility = node->base_visibility;
            im_tree_invalidate_measure(node);
        }
    }
}

void im_node_set_state_h_align(im_node *node, bool active, im_align align) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_H_ALIGN)) {
            node->base_h_align = node->h_align;
            node->state_mask |= IM_SBIT_H_ALIGN;
        }
        if (node->h_align != align) {
            node->h_align = align;
            im_tree_invalidate_arrange(node);
        }
    } else if (node->state_mask & IM_SBIT_H_ALIGN) {
        node->state_mask &= ~IM_SBIT_H_ALIGN;
        if (node->h_align != node->base_h_align) {
            node->h_align = node->base_h_align;
            im_tree_invalidate_arrange(node);
        }
    }
}

void im_node_set_state_v_align(im_node *node, bool active, im_align align) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_V_ALIGN)) {
            node->base_v_align = node->v_align;
            node->state_mask |= IM_SBIT_V_ALIGN;
        }
        if (node->v_align != align) {
            node->v_align = align;
            im_tree_invalidate_arrange(node);
        }
    } else if (node->state_mask & IM_SBIT_V_ALIGN) {
        node->state_mask &= ~IM_SBIT_V_ALIGN;
        if (node->v_align != node->base_v_align) {
            node->v_align = node->base_v_align;
            im_tree_invalidate_arrange(node);
        }
    }
}

void im_node_set_state_border_thickness(im_node *node, bool active, im_thickness thickness) {
    if (!node) return;
    if (active) {
        if (!(node->state_mask & IM_SBIT_BORDER_THICKNESS)) {
            node->base_border_thickness = node->border_thickness;
            node->state_mask |= IM_SBIT_BORDER_THICKNESS;
        }
        if (!thickness_eq(node->border_thickness, thickness)) {
            node->border_thickness = thickness;
            im_tree_invalidate_measure(node);
        }
    } else if (node->state_mask & IM_SBIT_BORDER_THICKNESS) {
        node->state_mask &= ~IM_SBIT_BORDER_THICKNESS;
        if (!thickness_eq(node->border_thickness, node->base_border_thickness)) {
            node->border_thickness = node->base_border_thickness;
            im_tree_invalidate_measure(node);
        }
    }
}

void im_node_set_flow_direction(im_node *node, im_flow_dir dir) {
    if (!node) return;
    if ((node->local_mask & IM_PBIT_FLOW_DIR) && node->flow_direction == dir) return;
    node->flow_direction = dir;
    node->local_mask |= IM_PBIT_FLOW_DIR;
    im_tree_invalidate_arrange(node);
}

void im_node_set_layout_rounding(im_node *node, bool enabled) {
    if (!node || node->use_layout_rounding == enabled) return;
    node->use_layout_rounding = enabled;
    im_tree_invalidate_measure(node);
}

void im_node_set_stack(im_node *node, im_orientation orientation, im_fixed spacing) {
    if (!node) return;
    if (node->orientation == orientation && node->spacing == spacing) return;
    node->orientation = orientation;
    node->spacing = spacing;
    im_tree_invalidate_measure(node);
}

void im_node_set_grid_spacing(im_node *node, im_fixed row_spacing, im_fixed col_spacing) {
    if (!node) return;
    if (node->row_spacing == row_spacing && node->col_spacing == col_spacing) return;
    node->row_spacing = row_spacing;
    node->col_spacing = col_spacing;
    im_tree_invalidate_measure(node);
}

void im_node_set_grid_cell(im_node *node, uint32_t row, uint32_t col, uint32_t row_span, uint32_t col_span) {
    if (!node) return;
    if (row_span == 0) row_span = 1;
    if (col_span == 0) col_span = 1;
    if (node->grid_row == row && node->grid_col == col &&
        node->grid_row_span == row_span && node->grid_col_span == col_span) return;
    node->grid_row = row;
    node->grid_col = col;
    node->grid_row_span = row_span;
    node->grid_col_span = col_span;
    im_tree_invalidate_measure(node);
}

im_thickness im_node_effective_padding(const im_node *node) {
    if (!node) return (im_thickness){0, 0, 0, 0};
    if ((node->local_mask & IM_PBIT_PADDING) == 0 && (node->bind_mask & IM_PBIT_PADDING) && node->templated_parent) {
        return im_node_effective_padding(node->templated_parent);
    }
    return (node->local_mask & IM_PBIT_PADDING) ? node->padding : node->default_padding;
}

im_thickness im_node_effective_border_thickness(const im_node *node) {
    if (!node) return (im_thickness){0, 0, 0, 0};
    if (node->state_mask & IM_SBIT_BORDER_THICKNESS) {
        return node->border_thickness;
    }
    if ((node->local_mask & IM_PBIT_BORDER_THICKNESS) == 0 &&
        (node->bind_mask & IM_PBIT_BORDER_THICKNESS) && node->templated_parent) {
        return im_node_effective_border_thickness(node->templated_parent);
    }
    return (node->local_mask & IM_PBIT_BORDER_THICKNESS) ? node->border_thickness : node->default_border_thickness;
}

im_align im_node_effective_h_content_align(const im_node *node) {
    if (!node) return IM_ALIGN_STRETCH;
    if ((node->local_mask & IM_PBIT_H_CONTENT_ALIGN) == 0 &&
        (node->bind_mask & IM_PBIT_H_CONTENT_ALIGN) && node->templated_parent) {
        return im_node_effective_h_content_align(node->templated_parent);
    }
    return (node->local_mask & IM_PBIT_H_CONTENT_ALIGN) ? node->h_content_align : node->default_h_content_align;
}

im_align im_node_effective_v_content_align(const im_node *node) {
    if (!node) return IM_ALIGN_STRETCH;
    if ((node->local_mask & IM_PBIT_V_CONTENT_ALIGN) == 0 &&
        (node->bind_mask & IM_PBIT_V_CONTENT_ALIGN) && node->templated_parent) {
        return im_node_effective_v_content_align(node->templated_parent);
    }
    return (node->local_mask & IM_PBIT_V_CONTENT_ALIGN) ? node->v_content_align : node->default_v_content_align;
}

im_flow_dir im_node_effective_flow_direction(const im_node *node) {
    const im_node *cur = node;
    while (cur) {
        if (cur->local_mask & IM_PBIT_FLOW_DIR) return cur->flow_direction;
        cur = cur->parent;
    }
    return IM_FLOW_LTR;
}

static bool resolve_fixed_val(const im_palette *palette, const im_template_value *v, im_fixed *out) {
    if (v->kind == IM_TVAL_FIXED) {
        *out = v->a;
        return true;
    }
    if (v->kind == IM_TVAL_INT) {
        *out = v->a * IM_ONE;
        return true;
    }
    if (v->kind == IM_TVAL_THEME_VALUE && palette && v->a >= 0 && v->a < IM_VALUE_COUNT) {
        const im_fixed *tv = im_value(palette, (enum im_value_id)v->a);
        *out = tv[0];
        return true;
    }
    return false;
}

static bool resolve_thickness_val(const im_palette *palette, const im_template_value *v, im_thickness *out) {
    if (v->kind == IM_TVAL_QUAD) {
        *out = (im_thickness){v->a, v->b, v->c, v->d};
        return true;
    }
    if (v->kind == IM_TVAL_FIXED) {
        *out = (im_thickness){v->a, v->a, v->a, v->a};
        return true;
    }
    if (v->kind == IM_TVAL_THEME_VALUE && palette && v->a >= 0 && v->a < IM_VALUE_COUNT) {
        const im_fixed *tv = im_value(palette, (enum im_value_id)v->a);
        *out = (im_thickness){tv[0], tv[1], tv[2], tv[3]};
        return true;
    }
    return false;
}

static bool parse_align(const char *s, im_align *out) {
    if (!s) return false;
    if (str_eq(s, "Left") || str_eq(s, "Top")) { *out = IM_ALIGN_START; return true; }
    if (str_eq(s, "Center")) { *out = IM_ALIGN_CENTER; return true; }
    if (str_eq(s, "Right") || str_eq(s, "Bottom")) { *out = IM_ALIGN_END; return true; }
    if (str_eq(s, "Stretch")) { *out = IM_ALIGN_STRETCH; return true; }
    return false;
}

static uint8_t parse_line_cap_str(const char *s) {
    if (!s) return 0u;
    if (str_eq(s, "Square")) return 1u;
    if (str_eq(s, "Round")) return 2u;
    if (str_eq(s, "Triangle")) return 3u;
    return 0u;
}

static uint8_t parse_line_join_str(const char *s) {
    if (!s) return 0u;
    if (str_eq(s, "Bevel")) return 1u;
    if (str_eq(s, "Round")) return 2u;
    return 0u;
}

static void apply_template_prop(im_node *node, const im_palette *palette, const char *prop, const im_template_value *v, bool is_style_default) {
    if (!node || !prop || !v) return;
    if (v->kind == IM_TVAL_TEMPLATE_BINDING) {
        if (str_eq(prop, "Padding") && str_eq(v->text, "Padding")) {
            node->bind_mask |= IM_PBIT_PADDING;
        } else if (str_eq(prop, "BorderThickness") && str_eq(v->text, "BorderThickness")) {
            node->bind_mask |= IM_PBIT_BORDER_THICKNESS;
        } else if (str_eq(prop, "HorizontalContentAlignment") && str_eq(v->text, "HorizontalContentAlignment")) {
            node->bind_mask |= IM_PBIT_H_CONTENT_ALIGN;
        } else if (str_eq(prop, "VerticalContentAlignment") && str_eq(v->text, "VerticalContentAlignment")) {
            node->bind_mask |= IM_PBIT_V_CONTENT_ALIGN;
        } else if (str_eq(prop, "Data")) {
            node->path_data_binding = v->text;
        }
        return;
    }
    if (v->kind == IM_TVAL_BINDING && str_eq(prop, "Data")) {
        node->path_data_binding = v->text;
        return;
    }

    im_fixed fx = 0;
    im_thickness th = {0, 0, 0, 0};
    im_align al = IM_ALIGN_STRETCH;

    if (str_eq(prop, "Width") && resolve_fixed_val(palette, v, &fx)) {
        node->width = fx;
    } else if (str_eq(prop, "Height") && resolve_fixed_val(palette, v, &fx)) {
        node->height = fx;
    } else if (str_eq(prop, "MinWidth") && resolve_fixed_val(palette, v, &fx)) {
        node->min_width = fx;
    } else if (str_eq(prop, "MinHeight") && resolve_fixed_val(palette, v, &fx)) {
        node->min_height = fx;
    } else if (str_eq(prop, "MaxWidth") && resolve_fixed_val(palette, v, &fx)) {
        node->max_width = fx;
    } else if (str_eq(prop, "MaxHeight") && resolve_fixed_val(palette, v, &fx)) {
        node->max_height = fx;
    } else if (str_eq(prop, "Margin") && resolve_thickness_val(palette, v, &th)) {
        node->margin = th;
    } else if (str_eq(prop, "Padding") && resolve_thickness_val(palette, v, &th)) {
        if (is_style_default) {
            node->default_padding = th;
            if (!(node->local_mask & IM_PBIT_PADDING)) node->padding = th;
        } else {
            node->padding = th;
            node->default_padding = th;
        }
    } else if (str_eq(prop, "BorderThickness") && resolve_thickness_val(palette, v, &th)) {
        if (is_style_default) {
            node->default_border_thickness = th;
            if (!(node->local_mask & IM_PBIT_BORDER_THICKNESS)) node->border_thickness = th;
        } else {
            node->border_thickness = th;
            node->default_border_thickness = th;
        }
    } else if (str_eq(prop, "HorizontalAlignment") && v->kind == IM_TVAL_ENUM_STR && parse_align(v->text, &al)) {
        node->h_align = al;
    } else if (str_eq(prop, "VerticalAlignment") && v->kind == IM_TVAL_ENUM_STR && parse_align(v->text, &al)) {
        node->v_align = al;
    } else if (str_eq(prop, "HorizontalContentAlignment") && v->kind == IM_TVAL_ENUM_STR && parse_align(v->text, &al)) {
        node->default_h_content_align = al;
        if (!(node->local_mask & IM_PBIT_H_CONTENT_ALIGN)) node->h_content_align = al;
    } else if (str_eq(prop, "VerticalContentAlignment") && v->kind == IM_TVAL_ENUM_STR && parse_align(v->text, &al)) {
        node->default_v_content_align = al;
        if (!(node->local_mask & IM_PBIT_V_CONTENT_ALIGN)) node->v_content_align = al;
    } else if (str_eq(prop, "Visibility") && v->kind == IM_TVAL_ENUM_STR) {
        node->visibility = str_eq(v->text, "Collapsed") ? IM_VIS_COLLAPSED : IM_VIS_VISIBLE;
    } else if (str_eq(prop, "UseLayoutRounding") && v->kind == IM_TVAL_BOOL) {
        node->use_layout_rounding = v->a != 0;
    } else if (str_eq(prop, "FlowDirection") && v->kind == IM_TVAL_ENUM_STR) {
        node->flow_direction = str_eq(v->text, "RightToLeft") ? IM_FLOW_RTL : IM_FLOW_LTR;
        node->local_mask |= IM_PBIT_FLOW_DIR;
    } else if (str_eq(prop, "Orientation") && v->kind == IM_TVAL_ENUM_STR) {
        node->orientation = str_eq(v->text, "Horizontal") ? IM_ORIENT_HORIZONTAL : IM_ORIENT_VERTICAL;
    } else if (str_eq(prop, "Spacing") && resolve_fixed_val(palette, v, &fx)) {
        node->spacing = fx;
    } else if (str_eq(prop, "RowSpacing") && resolve_fixed_val(palette, v, &fx)) {
        node->row_spacing = fx;
    } else if (str_eq(prop, "ColumnSpacing") && resolve_fixed_val(palette, v, &fx)) {
        node->col_spacing = fx;
    } else if (str_eq(prop, "Grid.Row") && v->kind == IM_TVAL_INT && v->a >= 0) {
        node->grid_row = (uint32_t)v->a;
    } else if (str_eq(prop, "Grid.Column") && v->kind == IM_TVAL_INT && v->a >= 0) {
        node->grid_col = (uint32_t)v->a;
    } else if (str_eq(prop, "Grid.RowSpan") && v->kind == IM_TVAL_INT && v->a >= 1) {
        node->grid_row_span = (uint32_t)v->a;
    } else if (str_eq(prop, "Grid.ColumnSpan") && v->kind == IM_TVAL_INT && v->a >= 1) {
        node->grid_col_span = (uint32_t)v->a;
    } else if (str_eq(prop, "RenderTransformOrigin") && v->kind == IM_TVAL_POINT) {
        node->render_transform_origin_x = (im_fixed)v->a;
        node->render_transform_origin_y = (im_fixed)v->b;
    } else if (str_eq(prop, "Opacity") && resolve_fixed_val(palette, v, &fx)) {
        if (fx < 0) fx = 0;
        if (fx > IM_ONE) fx = IM_ONE;
        node->opacity = fx;
        node->base_opacity = fx;
    } else if (str_eq(prop, "Stretch") && v->kind == IM_TVAL_ENUM_STR) {
        if (str_eq(v->text, "Fill")) node->path_stretch = 1;
        else if (str_eq(v->text, "Uniform")) node->path_stretch = 2;
        else if (str_eq(v->text, "UniformToFill")) node->path_stretch = 3;
        else node->path_stretch = 0;
    } else if (str_eq(prop, "StrokeThickness") && resolve_fixed_val(palette, v, &fx)) {
        node->path_stroke_thickness = fx;
    } else if (str_eq(prop, "StrokeStartLineCap") && v->kind == IM_TVAL_ENUM_STR) {
        node->path_start_cap = parse_line_cap_str(v->text);
    } else if (str_eq(prop, "StrokeEndLineCap") && v->kind == IM_TVAL_ENUM_STR) {
        node->path_end_cap = parse_line_cap_str(v->text);
    } else if (str_eq(prop, "StrokeLineJoin") && v->kind == IM_TVAL_ENUM_STR) {
        node->path_line_join = parse_line_join_str(v->text);
    } else if (str_eq(prop, "StrokeMiterLimit") && resolve_fixed_val(palette, v, &fx)) {
        node->path_miter_limit = fx;
    } else if (str_eq(prop, "Fill") && v->kind == IM_TVAL_BRUSH && v->a >= 0 && v->a < IM_BRUSH_COUNT) {
        node->path_has_fill = true;
        node->path_fill_brush = (enum im_brush_id)v->a;
        node->path_fill_argb = 0u;
    } else if (str_eq(prop, "Fill") && v->kind == IM_TVAL_COLOR) {
        node->path_has_fill = true;
        node->path_fill_brush = IM_BRUSH_COUNT;
        node->path_fill_argb = (uint32_t)((palette && palette->theme == IM_LIGHT) ? v->b : v->a);
    } else if (str_eq(prop, "Stroke") && v->kind == IM_TVAL_BRUSH && v->a >= 0 && v->a < IM_BRUSH_COUNT) {
        node->path_has_stroke = true;
        node->path_stroke_brush = (enum im_brush_id)v->a;
        node->path_stroke_argb = 0u;
    } else if (str_eq(prop, "Stroke") && v->kind == IM_TVAL_COLOR) {
        node->path_has_stroke = true;
        node->path_stroke_brush = IM_BRUSH_COUNT;
        node->path_stroke_argb = (uint32_t)((palette && palette->theme == IM_LIGHT) ? v->b : v->a);
    } else if (str_eq(prop, "Data") && v->kind == IM_TVAL_PATH && v->b > 0) {
        node->path_seg_start = (uint32_t)v->a;
        node->path_seg_count = (uint32_t)v->b;
        node->path_fill_rule = (uint8_t)v->c;
        node->path_icon_id = IM_ICON_NONE;
    } else if (str_eq(prop, "Data") && v->kind == IM_TVAL_ICON && v->a > IM_ICON_NONE && v->a < IM_ICON_COUNT) {
        node->path_icon_id = (im_icon_id)v->a;
        node->intrinsic_width = (v->a == IM_ICON_CHECKMARK) ? (16 * IM_ONE) : (10 * IM_ONE);
        node->intrinsic_height = (v->a == IM_ICON_CHECKMARK) ? (16 * IM_ONE) : (10 * IM_ONE);
    }
}

static bool parse_star_weight(const char *s, im_fixed *out) {
    if (!s || !*s) return false;
    const char *p = s;
    if (p[0] == '*' && p[1] == '\0') {
        *out = IM_ONE;
        return true;
    }
    int64_t int_part = 0;
    int64_t frac_num = 0;
    int64_t frac_den = 1;
    bool has_digit = false;
    while (*p >= '0' && *p <= '9') {
        has_digit = true;
        if (int_part < 32767) int_part = int_part * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            has_digit = true;
            if (frac_den < 1000000) {
                frac_num = frac_num * 10 + (*p - '0');
                frac_den *= 10;
            }
            p++;
        }
    }
    if (!has_digit || p[0] != '*' || p[1] != '\0') return false;
    int64_t val = int_part * (int64_t)IM_ONE + (frac_num * (int64_t)IM_ONE + frac_den / 2) / frac_den;
    if (val >= (int64_t)IM_FIXED_INF) val = IM_FIXED_INF - 1;
    *out = (im_fixed)val;
    return true;
}

static bool parse_grid_def_from_elem(const im_palette *palette, const im_template_element *el, const im_template_prop *props, im_grid_def *out) {
    out->unit = IM_GRID_STAR;
    out->value = IM_ONE;
    out->min_size = 0;
    out->max_size = IM_FIXED_INF;
    for (uint16_t p = 0; p < el->prop_count; p++) {
        const im_template_prop *pr = &props[el->prop_start + p];
        im_fixed fx = 0;
        if (str_eq(pr->name, "Height") || str_eq(pr->name, "Width")) {
            if (pr->value.kind == IM_TVAL_ENUM_STR && str_eq(pr->value.text, "Auto")) {
                out->unit = IM_GRID_AUTO;
                out->value = 0;
            } else if (pr->value.kind == IM_TVAL_ENUM_STR && parse_star_weight(pr->value.text, &fx)) {
                out->unit = IM_GRID_STAR;
                out->value = fx;
            } else if (resolve_fixed_val(palette, &pr->value, &fx)) {
                out->unit = IM_GRID_PIXEL;
                out->value = fx;
            }
        } else if ((str_eq(pr->name, "MinHeight") || str_eq(pr->name, "MinWidth")) &&
                   resolve_fixed_val(palette, &pr->value, &fx)) {
            out->min_size = fx;
        } else if ((str_eq(pr->name, "MaxHeight") || str_eq(pr->name, "MaxWidth")) &&
                   resolve_fixed_val(palette, &pr->value, &fx)) {
            out->max_size = fx;
        }
    }
    return true;
}

static bool append_grid_def(im_tree *tree, im_grid_def **arr, uint32_t *count, im_grid_def def) {
    if (*count == UINT32_MAX) return false;
    uint32_t n = *count + 1u;
    if ((size_t)n > (SIZE_MAX / sizeof(im_grid_def))) return false;
    size_t old_b = (size_t)(*count) * sizeof(im_grid_def);
    size_t new_b = (size_t)n * sizeof(im_grid_def);
    im_grid_def *buf = (im_grid_def *)tree->alloc.realloc(tree->alloc.ctx, *arr, old_b, new_b);
    if (!buf) return false;
    buf[*count] = def;
    *arr = buf;
    *count = n;
    return true;
}

static im_element_kind root_kind_for_target(const char *target_type) {
    if (str_eq(target_type, "Button")) return IM_ELEM_BUTTON;
    if (str_eq(target_type, "Grid")) return IM_ELEM_GRID;
    if (str_eq(target_type, "StackPanel")) return IM_ELEM_STACK_PANEL;
    if (str_eq(target_type, "Border")) return IM_ELEM_BORDER;
    if (str_eq(target_type, "ContentPresenter")) return IM_ELEM_CONTENT_PRESENTER;
    return IM_ELEM_CONTENT_PRESENTER;
}

im_node *im_tree_instantiate_custom_template(
    im_tree *tree,
    const im_palette *palette,
    const im_template *tpl,
    const im_template_setter *setters,
    const im_template_element *elems,
    const im_template_prop *props,
    const char *name) {
    if (!tree || !tpl) return NULL;
    im_node *ctrl = im_tree_create_node(tree, root_kind_for_target(tpl->target_type), name ? name : tpl->key);
    if (!ctrl) return NULL;

    for (uint16_t i = 0; i < tpl->style_setter_count; i++) {
        const im_template_setter *st = &setters[tpl->style_setter_start + i];
        if (!st->target) {
            apply_template_prop(ctrl, palette, st->property, &st->value, true);
        }
    }

    if (tpl->elem_count == 0) return ctrl;

    size_t map_bytes = (size_t)tpl->elem_count * sizeof(im_node *);
    im_node **created = (im_node **)tree->alloc.alloc(tree->alloc.ctx, map_bytes);
    if (!created) {
        im_tree_destroy_subtree(tree, ctrl);
        return NULL;
    }
    zero_bytes(created, map_bytes);

    bool ok = true;
    for (uint16_t i = 0; i < tpl->elem_count && ok; i++) {
        const im_template_element *el = &elems[tpl->elem_start + i];
        im_node *parent_node = (el->parent < 0) ? ctrl : ((uint16_t)el->parent < i ? created[el->parent] : NULL);

        if (el->kind == IM_ELEM_ROW_DEFINITION || el->kind == IM_ELEM_COLUMN_DEFINITION) {
            if (parent_node && parent_node->kind == IM_ELEM_GRID) {
                im_grid_def def;
                parse_grid_def_from_elem(palette, el, props, &def);
                if (el->kind == IM_ELEM_ROW_DEFINITION) {
                    ok = append_grid_def(tree, &parent_node->rows, &parent_node->row_count, def);
                } else {
                    ok = append_grid_def(tree, &parent_node->cols, &parent_node->col_count, def);
                }
            }
            continue;
        }
        if (el->kind == IM_ELEM_TRANSLATE_TRANSFORM ||
            el->kind == IM_ELEM_SCALE_TRANSFORM ||
            el->kind == IM_ELEM_COMPOSITE_TRANSFORM) {
            if (parent_node) {
                parent_node->render_transform_name = el->name;
                parent_node->render_transform_kind = el->kind;
            }
            continue;
        }
        if (is_non_visual_kind(el->kind) || !parent_node) continue;

        im_node *child = im_tree_create_node(tree, el->kind, el->name);
        if (!child) {
            ok = false;
            break;
        }
        child->templated_parent = ctrl;
        for (uint16_t p = 0; p < el->prop_count; p++) {
            const im_template_prop *pr = &props[el->prop_start + p];
            apply_template_prop(child, palette, pr->name, &pr->value, false);
        }
        if (!im_tree_append_child(tree, parent_node, child)) {
            im_tree_destroy_subtree(tree, child);
            ok = false;
            break;
        }
        created[i] = child;
    }

    tree->alloc.free(tree->alloc.ctx, created, map_bytes);
    if (!ok) {
        im_tree_destroy_subtree(tree, ctrl);
        return NULL;
    }
    return ctrl;
}

im_node *im_tree_instantiate_template(im_tree *tree, const im_palette *palette, enum im_template_id id, const char *name) {
    if (!tree || id < 0 || id >= IM_TEMPLATE_COUNT) return NULL;
    return im_tree_instantiate_custom_template(
        tree,
        palette,
        &im_templates[id],
        im_template_setters,
        im_template_elements,
        im_template_props,
        name);
}
