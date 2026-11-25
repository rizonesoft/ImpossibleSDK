/* Impossible UI: element tree, properties, template instantiation and invalidation. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "template.h"
#include "theme.h"
#include "transform.h"

/* Sentinel for an unset/Auto dimension (width or height). */
#define IM_FIXED_AUTO ((im_fixed)INT32_MIN)

/* Sentinel for an unbounded dimension (max width/height or infinite available measure size). */
#define IM_FIXED_INF ((im_fixed)INT32_MAX)

/* Caller-supplied allocator callbacks used by im_tree for all dynamic storage. */
typedef void *(*im_alloc_fn)(void *ctx, size_t size);
typedef void *(*im_realloc_fn)(void *ctx, void *ptr, size_t old_size, size_t new_size);
typedef void (*im_free_fn)(void *ctx, void *ptr, size_t size);

/* Allocator table passed to im_tree_init. Every callback must be non-NULL. */
typedef struct im_allocator {
    im_alloc_fn alloc;
    im_realloc_fn realloc;
    im_free_fn free;
    void *ctx;
} im_allocator;

/* Two-dimensional size in 16.16 fixed-point logical units. */
typedef struct im_size {
    im_fixed width;
    im_fixed height;
} im_size;

/* Four-edge thickness (Margin, Padding, BorderThickness) in 16.16 fixed-point logical units. */
typedef struct im_thickness {
    im_fixed left;
    im_fixed top;
    im_fixed right;
    im_fixed bottom;
} im_thickness;

/* Horizontal or vertical alignment of an element within its layout slot. */
typedef enum im_align {
    IM_ALIGN_STRETCH = 0,
    IM_ALIGN_START,
    IM_ALIGN_CENTER,
    IM_ALIGN_END
} im_align;

/* Layout visibility (Collapsed occupies zero space and is excluded from StackPanel spacing). */
typedef enum im_visibility {
    IM_VIS_VISIBLE = 0,
    IM_VIS_COLLAPSED
} im_visibility;

/* Orientation of a StackPanel. */
typedef enum im_orientation {
    IM_ORIENT_VERTICAL = 0,
    IM_ORIENT_HORIZONTAL
} im_orientation;

/* Flow direction controlling horizontal layout mirroring. */
typedef enum im_flow_dir {
    IM_FLOW_LTR = 0,
    IM_FLOW_RTL
} im_flow_dir;

/* Grid track sizing unit (Pixel, Auto, or Star). */
typedef enum im_grid_unit {
    IM_GRID_STAR = 0,
    IM_GRID_AUTO,
    IM_GRID_PIXEL
} im_grid_unit;

/* One RowDefinition or ColumnDefinition of a Grid. */
typedef struct im_grid_def {
    im_grid_unit unit;
    im_fixed value;
    im_fixed min_size;
    im_fixed max_size;
} im_grid_def;

/* Bitmask of properties that have a local override or template binding on an element. */
typedef enum im_prop_bit {
    IM_PBIT_PADDING = 1u << 0,
    IM_PBIT_BORDER_THICKNESS = 1u << 1,
    IM_PBIT_H_CONTENT_ALIGN = 1u << 2,
    IM_PBIT_V_CONTENT_ALIGN = 1u << 3,
    IM_PBIT_FLOW_DIR = 1u << 4
} im_prop_bit;

/* Bitmask of layout-affecting properties currently overridden by a visual state. */
typedef enum im_state_prop_bit {
    IM_SBIT_WIDTH = 1u << 0,
    IM_SBIT_HEIGHT = 1u << 1,
    IM_SBIT_MARGIN = 1u << 2,
    IM_SBIT_VISIBILITY = 1u << 3,
    IM_SBIT_H_ALIGN = 1u << 4,
    IM_SBIT_V_ALIGN = 1u << 5,
    IM_SBIT_BORDER_THICKNESS = 1u << 6
} im_state_prop_bit;

/* One element in the portable UI tree. */
typedef struct im_node {
    im_element_kind kind;
    const char *name;
    struct im_node *parent;
    struct im_node *templated_parent;
    struct im_node **children;
    uint32_t child_count;
    uint32_t child_cap;

    /* Box and alignment properties. */
    im_fixed width;
    im_fixed height;
    im_fixed min_width;
    im_fixed min_height;
    im_fixed max_width;
    im_fixed max_height;
    im_fixed intrinsic_width;
    im_fixed intrinsic_height;
    im_thickness margin;
    im_thickness padding;
    im_thickness default_padding;
    im_thickness border_thickness;
    im_thickness default_border_thickness;
    im_align h_align;
    im_align v_align;
    im_align h_content_align;
    im_align default_h_content_align;
    im_align v_content_align;
    im_align default_v_content_align;
    im_visibility visibility;
    im_flow_dir flow_direction;
    bool use_layout_rounding;

    /* Property override and TemplateBinding masks. */
    uint32_t local_mask;
    uint32_t bind_mask;

    /* Visual-state presentation overrides and saved underlying local values. */
    uint32_t state_mask;
    im_fixed base_width;
    im_fixed base_height;
    im_thickness base_margin;
    im_visibility base_visibility;
    im_align base_h_align;
    im_align base_v_align;
    im_thickness base_border_thickness;

    /* StackPanel properties. */
    im_orientation orientation;
    im_fixed spacing;

    /* Grid panel and attached child properties. */
    im_grid_def *rows;
    uint32_t row_count;
    im_grid_def *cols;
    uint32_t col_count;
    im_fixed row_spacing;
    im_fixed col_spacing;
    uint32_t grid_row;
    uint32_t grid_col;
    uint32_t grid_row_span;
    uint32_t grid_col_span;

    /* Layout invalidation and cached pass results. */
    bool measure_dirty;
    bool arrange_dirty;
    bool has_measured;
    bool has_arranged;
    bool measured_inf_w;
    bool measured_inf_h;
    im_size last_avail;
    im_rect last_arrange_rect;
    im_rect last_parent_abs;
    im_flow_dir last_parent_flow;
    im_fixed last_measure_scale;
    im_fixed last_arrange_scale;
    im_size desired_size;
    im_size unclipped_desired;
    im_rect layout_rect;
    im_rect absolute_rect;
    bool paint_dirty;
    bool had_prev_rect;
    im_rect prev_absolute_rect;
    bool had_prev_device_rect;
    int32_t prev_dev_left, prev_dev_top, prev_dev_right, prev_dev_bottom;

    /* Optional RenderTransform applied around (render_transform_origin_x * w, render_transform_origin_y * h). */
    bool has_render_transform;
    im_transform render_transform;
    im_fixed render_transform_origin_x;
    im_fixed render_transform_origin_y;
    const char *render_transform_name;
    im_element_kind render_transform_kind;
} im_node;

/* Maximum number of detached/destroyed subtree bounds buffered before overflowing to a full repaint. */
#define IM_TREE_PENDING_DAMAGE_CAP 32

/* Element tree owning the allocator, root pointer, and detached-subtree damage queue. */
typedef struct im_tree {
    im_allocator alloc;
    im_node *root;
    uint32_t node_count;
    im_rect pending_damage[IM_TREE_PENDING_DAMAGE_CAP];
    uint32_t pending_damage_count;
    int32_t pending_dev_damage[IM_TREE_PENDING_DAMAGE_CAP][4];
    uint32_t pending_dev_damage_count;
    bool pending_damage_overflow;
} im_tree;

/* Initialise an empty element tree with a caller-supplied allocator. Returns false if invalid. */
bool im_tree_init(im_tree *tree, im_allocator alloc);

/* Destroy every node reachable from tree->root and reset the tree to empty. */
void im_tree_destroy(im_tree *tree);

/* Create a detached element node with WinUI defaults. Returns NULL on allocation failure. */
im_node *im_tree_create_node(im_tree *tree, im_element_kind kind, const char *name);

/* Destroy a detached subtree (or detach from parent/root first) and release all memory. */
void im_tree_destroy_subtree(im_tree *tree, im_node *node);

/* Append child to parent. On allocation failure, leaves parent and child unchanged and returns false. */
bool im_tree_append_child(im_tree *tree, im_node *parent, im_node *child);

/* Insert child at index in parent. On allocation failure, leaves parent and child unchanged and returns false. */
bool im_tree_insert_child(im_tree *tree, im_node *parent, uint32_t index, im_node *child);

/* Remove child from its parent without destroying it. Invalidates parent measure. */
bool im_tree_remove_child(im_tree *tree, im_node *parent, im_node *child);

/* Replace existing child at index with replacement, returning the removed child (or NULL on error). */
im_node *im_tree_replace_child(im_tree *tree, im_node *parent, uint32_t index, im_node *replacement);

/* Find the first descendant (or self) matching name. */
im_node *im_tree_find_by_name(im_node *root, const char *name);

/* Mark node and its ancestors measure-dirty and arrange-dirty. */
void im_tree_invalidate_measure(im_node *node);

/* Mark node and its ancestors arrange-dirty. */
void im_tree_invalidate_arrange(im_node *node);

/* Mark node as needing a visual repaint (e.g. brush/color/state change without geometry change). */
void im_tree_invalidate_paint(im_node *node);

/* Set or replace node's RenderTransform and normalized origin [0..IM_ONE]; marks node and subtree paint-dirty. */
void im_node_set_render_transform(im_node *node, const im_transform *transform, im_fixed origin_x, im_fixed origin_y);

/* Clear node's RenderTransform back to identity; marks node and subtree paint-dirty. */
void im_node_clear_render_transform(im_node *node);

/* Set Grid row definitions transactionally; on allocation failure, existing rows remain intact. */
bool im_tree_set_grid_rows(im_tree *tree, im_node *grid, const im_grid_def *defs, uint32_t count);

/* Set Grid column definitions transactionally; on allocation failure, existing columns remain intact. */
bool im_tree_set_grid_cols(im_tree *tree, im_node *grid, const im_grid_def *defs, uint32_t count);

/* Set explicit Width/Height (use IM_FIXED_AUTO for Auto). Invalidates measure on change. */
void im_node_set_size(im_node *node, im_fixed width, im_fixed height);

/* Set MinWidth/MinHeight/MaxWidth/MaxHeight constraints. Invalidates measure on change. */
void im_node_set_min_max(im_node *node, im_fixed min_w, im_fixed min_h, im_fixed max_w, im_fixed max_h);

/* Set intrinsic content size for leaf elements. Invalidates measure on change. */
void im_node_set_intrinsic_size(im_node *node, im_fixed width, im_fixed height);

/* Set Margin. Invalidates measure on change. */
void im_node_set_margin(im_node *node, im_thickness margin);

/* Set local Padding override (flows to TemplateBinding children). Invalidates measure on change. */
void im_node_set_padding(im_node *node, im_thickness padding);

/* Clear local Padding override back to template/style default. Invalidates measure on change. */
void im_node_clear_padding(im_node *node);

/* Set local BorderThickness override (flows to TemplateBinding children). Invalidates measure on change. */
void im_node_set_border_thickness(im_node *node, im_thickness thickness);

/* Clear local BorderThickness override back to template/style default. Invalidates measure on change. */
void im_node_clear_border_thickness(im_node *node);

/* Set HorizontalAlignment and VerticalAlignment. Invalidates arrange on change. */
void im_node_set_alignment(im_node *node, im_align h_align, im_align v_align);

/* Set local HorizontalContentAlignment and VerticalContentAlignment overrides. Invalidates measure on change. */
void im_node_set_content_alignment(im_node *node, im_align h_align, im_align v_align);

/* Clear local content alignment overrides back to template/style defaults. Invalidates measure on change. */
void im_node_clear_content_alignment(im_node *node);

/* Set Visibility (Visible or Collapsed). Invalidates measure on change. */
void im_node_set_visibility(im_node *node, im_visibility visibility);

/* Set explicit FlowDirection override on node. Invalidates arrange on change. */
void im_node_set_flow_direction(im_node *node, im_flow_dir dir);

/* Set UseLayoutRounding (default true). Invalidates measure on change. */
void im_node_set_layout_rounding(im_node *node, bool enabled);

/* Set StackPanel orientation and spacing. Invalidates measure on change. */
void im_node_set_stack(im_node *node, im_orientation orientation, im_fixed spacing);

/* Set Grid RowSpacing and ColumnSpacing. Invalidates measure on change. */
void im_node_set_grid_spacing(im_node *node, im_fixed row_spacing, im_fixed col_spacing);

/* Set Grid.Row, Grid.Column, Grid.RowSpan, Grid.ColumnSpan attached properties. Invalidates measure on change. */
void im_node_set_grid_cell(im_node *node, uint32_t row, uint32_t col, uint32_t row_span, uint32_t col_span);

/* Apply or clear a visual-state presentation override for Width without losing the local value. */
void im_node_set_state_width(im_node *node, bool active, im_fixed width);

/* Apply or clear a visual-state presentation override for Height without losing the local value. */
void im_node_set_state_height(im_node *node, bool active, im_fixed height);

/* Apply or clear a visual-state presentation override for Margin without losing the local value. */
void im_node_set_state_margin(im_node *node, bool active, im_thickness margin);

/* Apply or clear a visual-state presentation override for Visibility without losing the local value. */
void im_node_set_state_visibility(im_node *node, bool active, im_visibility visibility);

/* Apply or clear a visual-state presentation override for HorizontalAlignment without losing the local value. */
void im_node_set_state_h_align(im_node *node, bool active, im_align align);

/* Apply or clear a visual-state presentation override for VerticalAlignment without losing the local value. */
void im_node_set_state_v_align(im_node *node, bool active, im_align align);

/* Apply or clear a visual-state presentation override for BorderThickness without losing the local value. */
void im_node_set_state_border_thickness(im_node *node, bool active, im_thickness thickness);

/* Resolve effective Padding on node (following TemplateBinding when active). */
im_thickness im_node_effective_padding(const im_node *node);

/* Resolve effective BorderThickness on node (following TemplateBinding when active). */
im_thickness im_node_effective_border_thickness(const im_node *node);

/* Resolve effective HorizontalContentAlignment on node (following TemplateBinding when active). */
im_align im_node_effective_h_content_align(const im_node *node);

/* Resolve effective VerticalContentAlignment on node (following TemplateBinding when active). */
im_align im_node_effective_v_content_align(const im_node *node);

/* Resolve effective FlowDirection on node (inheriting from ancestors if unset). */
im_flow_dir im_node_effective_flow_direction(const im_node *node);

/* Instantiate a compiled WinUI template (ui/template_data.c) into a new control node subtree,
   resolving theme resources with im_value(palette, id) and wiring TemplateBindings.
   On any allocation failure, frees all partial nodes and returns NULL leaving tree unchanged. */
im_node *im_tree_instantiate_template(im_tree *tree, const im_palette *palette, enum im_template_id id, const char *name);

/* Instantiate an arbitrary im_template descriptor with custom tables (used for compiled fixture tests).
   On any allocation failure, frees all partial nodes and returns NULL leaving tree unchanged. */
im_node *im_tree_instantiate_custom_template(
    im_tree *tree,
    const im_palette *palette,
    const im_template *tpl,
    const im_template_setter *setters,
    const im_template_element *elems,
    const im_template_prop *props,
    const char *name);
