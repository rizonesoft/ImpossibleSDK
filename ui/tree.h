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

/* Orientation of a StackPanel, WrapPanel, or WrapGrid. */
typedef enum im_orientation {
    IM_ORIENT_VERTICAL = 0,
    IM_ORIENT_HORIZONTAL
} im_orientation;

/* StretchDirection matching WinUI StretchDirection (Viewbox). */
typedef enum im_stretch_dir {
    IM_STRETCH_DIR_BOTH = 0,
    IM_STRETCH_DIR_UP_ONLY = 1,
    IM_STRETCH_DIR_DOWN_ONLY = 2
} im_stretch_dir;

/* WrapPanel ItemsStretch matching pinned WinUI WrapPanelItemsStretch. */
typedef enum im_wrap_stretch {
    IM_WRAP_STRETCH_NONE = 0,
    IM_WRAP_STRETCH_LAST = 1
} im_wrap_stretch;

/* Per-subtree requested theme (Default inherits from parent/window; High Contrast always overrides Light/Dark). */
typedef enum im_requested_theme {
    IM_REQ_THEME_DEFAULT = 0,
    IM_REQ_THEME_DARK,
    IM_REQ_THEME_LIGHT,
    IM_REQ_THEME_CONTRAST
} im_requested_theme;

/* One per-node named resource override (kind 1 = brush ARGB, kind 2 = 4-component value). */
typedef struct im_resource_override {
    uint8_t kind;
    uint8_t scope;
    uint16_t id;
    uint32_t argb;
    im_fixed val[4];
} im_resource_override;

/* Maximum compiled theme transition declarations stored on a single node collection. */
#define IM_NODE_MAX_TRANSITIONS 8

/* One compiled theme transition declaration attached to a control or panel (preserving per-declaration values). */
typedef struct im_transition_decl {
    im_element_kind kind;
    im_fixed from_h;
    im_fixed from_v;
    bool has_from_h;
    bool has_from_v;
    bool is_staggering;
    im_element_kind nav_info; /* IM_ELEM_DRILL_IN_NAV_INFO / SLIDE / SUPPRESS for NavigationThemeTransition */
} im_transition_decl;

/* Sibling-relative constraint slots on RelativePanel children (name-resolved at layout time). */
typedef enum im_rel_slot {
    IM_REL_LEFT_OF = 0,
    IM_REL_ABOVE = 1,
    IM_REL_RIGHT_OF = 2,
    IM_REL_BELOW = 3,
    IM_REL_ALIGN_LEFT = 4,
    IM_REL_ALIGN_TOP = 5,
    IM_REL_ALIGN_RIGHT = 6,
    IM_REL_ALIGN_BOTTOM = 7,
    IM_REL_ALIGN_HCENTER = 8,
    IM_REL_ALIGN_VCENTER = 9,
    IM_REL_COUNT = 10
} im_rel_slot;

/* Bitmask flags for RelativePanel panel-edge alignment attached properties. */
typedef enum im_rel_panel_flags {
    IM_REL_PANEL_NONE = 0u,
    IM_REL_PANEL_LEFT = 1u << 0,
    IM_REL_PANEL_TOP = 1u << 1,
    IM_REL_PANEL_RIGHT = 1u << 2,
    IM_REL_PANEL_BOTTOM = 1u << 3,
    IM_REL_PANEL_HCENTER = 1u << 4,
    IM_REL_PANEL_VCENTER = 1u << 5
} im_rel_panel_flags;

/* Layout failure code reported on im_tree (e.g. RelativePanel cycle or missing sibling, VSWG OOM). */
typedef enum im_layout_error {
    IM_LAYOUT_OK = 0,
    IM_LAYOUT_ERR_CYCLE = 1,
    IM_LAYOUT_ERR_MISSING_TARGET = 2,
    IM_LAYOUT_ERR_OOM = 3
} im_layout_error;

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
    im_thickness paint_outset;

    /* Optional RenderTransform applied around (render_transform_origin_x * w, render_transform_origin_y * h). */
    bool has_render_transform;
    im_transform render_transform;
    im_fixed render_transform_origin_x;
    im_fixed render_transform_origin_y;
    const char *render_transform_name;
    im_element_kind render_transform_kind;

    /* Distinct ThemeTransition presentation transform & opacity (composed with, never overwriting, authored values). */
    bool has_transition_transform;
    im_transform transition_transform;
    im_fixed transition_origin_x;
    im_fixed transition_origin_y;
    im_fixed transition_opacity;
    im_transition_decl transition_decls[IM_NODE_MAX_TRANSITIONS];
    uint32_t transition_decl_count;

    /* Opacity (0..IM_ONE, default IM_ONE) and compiled Path element properties. */
    im_fixed opacity;
    im_fixed base_opacity;
    uint32_t path_seg_start;
    uint32_t path_seg_count;
    uint8_t path_fill_rule;
    im_icon_id path_icon_id;
    int path_stretch;
    im_fixed path_stroke_thickness;
    uint8_t path_start_cap;
    uint8_t path_end_cap;
    uint8_t path_line_join;
    im_fixed path_miter_limit;
    bool path_has_fill;
    enum im_brush_id path_fill_brush;
    uint32_t path_fill_argb;
    bool path_has_stroke;
    enum im_brush_id path_stroke_brush;
    uint32_t path_stroke_argb;
    const char *path_data_binding;
    bool path_applied_valid;
    uint8_t path_applied_kind;
    uint32_t path_applied_a;
    uint32_t path_applied_b;
    uint8_t path_applied_c;

    /* Canvas attached properties. */
    im_fixed canvas_left;
    im_fixed canvas_top;
    int32_t canvas_z;

    /* Viewbox properties and distinct Viewbox scaling transform (never overwrites child render_transform). */
    im_stretch viewbox_stretch;
    im_stretch_dir viewbox_stretch_dir;
    bool has_viewbox_transform;
    im_transform viewbox_transform;

    /* UniformGrid properties (reuses row_spacing, col_spacing, padding, border_thickness). */
    uint32_t uniform_rows;
    uint32_t uniform_cols;
    uint32_t uniform_first_col;

    /* WrapPanel properties (reuses orientation, padding, border_thickness). */
    im_fixed item_spacing;
    im_fixed line_spacing;
    im_wrap_stretch wrap_stretch;
    im_fixed wrap_measure_u;

    /* RelativePanel attached properties (node-owned inline strings so caller/sibling lifetimes never dangle). */
    char rel_target[IM_REL_COUNT][64];
    uint32_t rel_panel_flags;

    /* VariableSizedWrapGrid, ItemsWrapGrid, and ItemsStackPanel properties. */
    im_fixed item_width;
    im_fixed item_height;
    int32_t max_rows_or_cols;
    im_align h_children_align;
    im_align v_children_align;
    int32_t vswg_row_span;
    int32_t vswg_col_span;
    im_fixed vswg_measured_iw;
    im_fixed vswg_measured_ih;
    uint32_t vswg_measured_ipl;
    uint32_t vswg_measured_max_lines;
    bool vswg_has_measured_grid;
    im_thickness group_padding;
    bool is_group_header;
    bool layout_overflowed;

    /* Per-subtree theme, subtree accent, resource overrides, resource-backed bindings, and live interactive state. */
    im_requested_theme requested_theme;
    bool has_subtree_accent;
    uint32_t subtree_accent_argb;
    bool has_resolved_theme;
    im_theme resolved_theme;
    uint32_t resolved_palette_hash;
    uint32_t resolved_button_geom_hash;
    im_resource_override *res_overrides;
    uint32_t res_override_count;
    uint32_t res_override_cap;
    bool has_theme_bg_brush;
    enum im_brush_id theme_bg_brush;
    uint32_t resolved_bg_argb;
    uint32_t resolved_bg_argb_end;
    bool has_theme_border_brush;
    enum im_brush_id theme_border_brush;
    uint32_t resolved_border_argb;
    uint32_t resolved_border_argb_end;
    bool has_theme_padding_val;
    enum im_value_id theme_padding_val;
    bool has_local_padding;
    im_thickness theme_padding_cache;
    bool has_theme_border_val;
    enum im_value_id theme_border_val;
    bool has_local_border_thickness;
    im_thickness theme_border_cache;
    void *bound_sm;

    bool is_focused;
    uint32_t selection_start;
    uint32_t selection_end;
    im_fixed scroll_x;
    im_fixed scroll_y;
    char text[64];
} im_node;

/* Maximum number of detached/destroyed subtree bounds buffered before overflowing to a full repaint. */
#define IM_TREE_PENDING_DAMAGE_CAP 32

/* Element tree owning the allocator, root pointer, detached-subtree damage queue, and layout diagnostics. */
typedef struct im_tree {
    im_allocator alloc;
    im_node *root;
    uint32_t node_count;
    im_rect pending_damage[IM_TREE_PENDING_DAMAGE_CAP];
    uint32_t pending_damage_count;
    int32_t pending_dev_damage[IM_TREE_PENDING_DAMAGE_CAP][4];
    uint32_t pending_dev_damage_count;
    bool pending_damage_overflow;
    im_layout_error layout_error;
    char layout_error_name[128];
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

/* Set optional local-space visual paint outset (e.g. magnified glyph filter overhang, shadow, or stroke)
   so dirty-rectangle collection invalidates pixels painted beyond node->absolute_rect; negative sides clamp to 0. */
void im_node_set_paint_outset(im_node *node, im_thickness outset);

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

/* Set Canvas.Left and Canvas.Top attached properties. Invalidates parent arrange and paint on change. */
void im_node_set_canvas_pos(im_node *node, im_fixed left, im_fixed top);

/* Set Canvas.ZIndex attached property. Invalidates parent and node paint on change. */
void im_node_set_canvas_z(im_node *node, int32_t z_index);

/* Fill out_children (up to cap) with parent's children in stable back-to-front paint order
   (sorted ascending by canvas_z preserving insertion order on IM_ELEM_CANVAS). Returns child count. */
uint32_t im_node_paint_order(const im_node *parent, im_node **out_children, uint32_t cap);

/* Set Viewbox Stretch and StretchDirection. Invalidates measure, arrange, and paint on change. */
void im_node_set_viewbox_stretch(im_node *node, im_stretch stretch, im_stretch_dir dir);

/* Set UniformGrid Rows, Columns, and FirstColumn (0 = auto rows/cols). Invalidates measure on change. */
void im_node_set_uniform_grid(im_node *node, uint32_t rows, uint32_t cols, uint32_t first_col);

/* Set WrapPanel Orientation, ItemSpacing, LineSpacing, and ItemsStretch. Invalidates measure on change. */
void im_node_set_wrap_panel(im_node *node, im_orientation orientation, im_fixed item_spacing,
                            im_fixed line_spacing, im_wrap_stretch stretch);

/* Set or clear one RelativePanel sibling constraint by sibling name (copied into node-owned
   64-byte storage; NULL or empty string clears). Invalidates parent measure on change. */
void im_node_set_relative_target(im_node *node, im_rel_slot slot, const char *target_name);

/* Set RelativePanel panel-edge alignment bitmask (IM_REL_PANEL_*). Invalidates parent measure on change. */
void im_node_set_relative_panel_flags(im_node *node, uint32_t flags);

/* Set VariableSizedWrapGrid / ItemsWrapGrid orientation, item size, and MaximumRowsOrColumns. Invalidates measure. */
void im_node_set_wrap_grid(im_node *node, im_orientation orientation, im_fixed item_width,
                           im_fixed item_height, int32_t max_rows_or_cols);

/* Set VariableSizedWrapGrid.RowSpan and ColumnSpan attached properties. Invalidates parent measure on change. */
void im_node_set_vswg_span(im_node *node, int32_t row_span, int32_t col_span);

/* Set VariableSizedWrapGrid HorizontalChildrenAlignment and VerticalChildrenAlignment. Invalidates arrange on change. */
void im_node_set_vswg_children_alignment(im_node *node, im_align h_align, im_align v_align);

/* Set ItemsStackPanel / ItemsWrapGrid GroupPadding. Invalidates measure on change. */
void im_node_set_group_padding(im_node *node, im_thickness group_padding);

/* Mark a child inside ItemsStackPanel / ItemsWrapGrid as a group header container. Invalidates parent measure. */
void im_node_set_group_header(im_node *node, bool is_group_header);

/* Set per-subtree RequestedTheme (DEFAULT, DARK, or LIGHT; IM_REQ_THEME_CONTRAST is rejected on nodes). */
bool im_node_set_requested_theme(im_node *node, im_requested_theme theme);

/* Set or clear a per-subtree accent colour override (derives the 6 shades for this subtree only). */
void im_node_set_subtree_accent(im_node *node, bool active, uint32_t accent_argb);

/* Clear local Padding override so any theme-resource Padding value takes effect immediately. */
void im_node_clear_local_padding(im_node *node);

/* Clear local BorderThickness override so any theme-resource BorderThickness value takes effect immediately. */
void im_node_clear_local_border_thickness(im_node *node);

/* Bind a node's background and/or border brush IDs for in-place restyling via im_resources_restyle_tree. */
void im_node_bind_theme_brushes(im_node *node, bool has_bg, enum im_brush_id bg_brush,
                                bool has_border, enum im_brush_id border_brush);

/* Bind a node's padding and/or border-thickness value IDs for in-place restyling via im_resources_restyle_tree. */
void im_node_bind_theme_values(im_node *node, bool has_padding, enum im_value_id padding_val,
                               bool has_border, enum im_value_id border_val);

/* Attach an optional im_state_machine to node so im_resources_restyle_tree refreshes it in place. */
void im_node_bind_state_machine(im_node *node, void *sm);

/* Apply distinct transition presentation transform and opacity without overwriting authored RenderTransform or Opacity. */
void im_node_set_transition_presentation(im_node *node, bool has_transform, const im_transform *transform,
                                         im_fixed origin_x, im_fixed origin_y, im_fixed transition_opacity);

/* Clear transition presentation state, restoring pure authored RenderTransform and Opacity. */
void im_node_clear_transition_presentation(im_node *node);

/* Return effective node opacity (authored opacity * transition_opacity) in [0, IM_ONE]. */
im_fixed im_node_effective_opacity(const im_node *node);

/* Append one compiled theme transition declaration to node's transition collection. Returns false if full. */
bool im_node_add_transition_decl(im_node *node, const im_transition_decl *decl);

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
