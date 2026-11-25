/* Impossible UI: subtree theme resources, named overrides, in-place restyling, and contrast themes. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "theme.h"
#include "tree.h"

/* The four built-in Windows 11 contrast themes (matching hcblack, hcwhite, hc1, hc2). */
typedef enum im_contrast_theme {
    IM_CONTRAST_AQUATIC = 0,
    IM_CONTRAST_DESERT,
    IM_CONTRAST_DUSK,
    IM_CONTRAST_NIGHT_SKY
} im_contrast_theme;

/* Explicit system colours for a custom high-contrast theme (0xAARRGGBB). */
typedef struct im_contrast_colors {
    uint32_t window;
    uint32_t window_text;
    uint32_t button_face;
    uint32_t button_text;
    uint32_t highlight;
    uint32_t highlight_text;
    uint32_t hotlight;
    uint32_t gray_text;
    uint32_t control_accent;
} im_contrast_colors;

/* Initialize palette for one of the four built-in Windows 11 contrast themes while retaining system_accent_argb. */
void im_palette_init_contrast(im_palette *out, im_contrast_theme preset, uint32_t system_accent_argb);

/* Initialize palette for a custom high-contrast theme from explicit system colours. */
void im_palette_init_contrast_custom(im_palette *out, const im_contrast_colors *colors);

/* Look up a generated WinUI brush ID by exact key name. Returns false if unknown. */
bool im_brush_id_from_name(const char *name, enum im_brush_id *out_id);

/* Look up a generated WinUI numeric/thickness/radius value ID by exact key name. Returns false if unknown. */
bool im_value_id_from_name(const char *name, enum im_value_id *out_id);

/* Resolve the effective theme (Dark, Light, or Contrast) for node under window_palette.
   When window_palette->theme == IM_CONTRAST, contrast always wins while preserving node->requested_theme. */
im_theme im_resources_effective_theme(const im_node *node, const im_palette *window_palette);

/* Build the effective im_palette (resolved theme + window slots) for node under window_palette. */
im_palette im_resources_effective_palette(const im_node *node, const im_palette *window_palette);

/* Check whether an ancestor (including node) overrides brush id for node's effective theme. */
bool im_resources_lookup_brush_override(const im_node *node, const im_palette *window_palette,
                                        enum im_brush_id id, uint32_t *out_argb);

/* Check whether an ancestor (including node) overrides value id for node's effective theme. */
bool im_resources_lookup_value_override(const im_node *node, const im_palette *window_palette,
                                        enum im_value_id id, im_fixed out[4]);

/* Resolve a brush colour at (y, height, scale) for node, honouring subtree overrides and effective theme. */
uint32_t im_resources_resolve_brush(const im_node *node, const im_palette *window_palette,
                                    enum im_brush_id id, int64_t y, int64_t height, im_fixed scale);

/* Resolve a 4-component numeric/thickness/radius value for node, honouring subtree overrides and effective theme. */
void im_resources_resolve_value(const im_node *node, const im_palette *window_palette,
                                enum im_value_id id, im_fixed out[4]);

/* Set or replace a brush override on node by enum ID. Atomic: returns false on invalid ID or OOM without mutating. */
bool im_resources_set_brush_override(im_tree *tree, im_node *node, im_requested_theme scope,
                                     enum im_brush_id id, uint32_t argb);

/* Set or replace a brush override on node by exact WinUI key name. Rejects unknown keys or non-brush keys. */
bool im_resources_set_brush_override_by_name(im_tree *tree, im_node *node, im_requested_theme scope,
                                             const char *name, uint32_t argb);

/* Remove a brush override on node for (scope, id), revealing any broader/ancestor/theme value. */
bool im_resources_clear_brush_override(im_node *node, im_requested_theme scope, enum im_brush_id id);

/* Remove a brush override on node by exact WinUI key name. */
bool im_resources_clear_brush_override_by_name(im_node *node, im_requested_theme scope, const char *name);

/* Set or replace a 4-component value override on node by enum ID. Atomic on invalid ID or OOM. */
bool im_resources_set_value_override(im_tree *tree, im_node *node, im_requested_theme scope,
                                     enum im_value_id id, const im_fixed val[4]);

/* Set or replace a value override on node by exact WinUI key name. Rejects unknown keys or non-value keys. */
bool im_resources_set_value_override_by_name(im_tree *tree, im_node *node, im_requested_theme scope,
                                             const char *name, const im_fixed val[4]);

/* Remove a value override on node for (scope, id), revealing any broader/ancestor/theme value. */
bool im_resources_clear_value_override(im_node *node, im_requested_theme scope, enum im_value_id id);

/* Remove a value override on node by exact WinUI key name. */
bool im_resources_clear_value_override_by_name(im_node *node, im_requested_theme scope, const char *name);

/* Restyle an existing node subtree in place without rebuilding nodes or losing focus, selection, scroll, or text.
   Updates resource-backed properties and attached state machines, marking paint_dirty (and measure_dirty when
   layout-affecting values change) only on nodes whose resolved outputs changed. Returns count of updated nodes. */
uint32_t im_resources_restyle_tree(im_tree *tree, im_node *root, const im_palette *window_palette, uint32_t now_ms);
