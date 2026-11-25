/* Impossible UI: dirty-rectangle collection, bounded merging, and scaled damage conversion. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "draw.h"
#include "layer.h"
#include "tree.h"

/* Maximum number of dirty rectangles held after merging. */
#define IM_DIRTY_MAX_RECTS 16

/* Integer device-pixel rectangle [left, right) x [top, bottom). */
typedef struct im_irect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} im_irect;

/* Bounded set of device-pixel dirty rectangles clipped to [0, width) x [0, height). */
typedef struct im_dirty_list {
    im_irect rects[IM_DIRTY_MAX_RECTS];
    uint32_t count;
    int32_t width;
    int32_t height;
    im_fixed last_scale;
    bool has_scale;
} im_dirty_list;

/* Initialize an empty dirty-rectangle list for a device-pixel viewport of width x height. */
void im_dirty_init(im_dirty_list *list, int32_t width, int32_t height);

/* Clear all pending dirty rectangles while retaining viewport dimensions and last_scale. */
void im_dirty_clear(im_dirty_list *list);

/* Update the viewport size (and mark the full new viewport dirty if dimensions changed). */
void im_dirty_set_viewport(im_dirty_list *list, int32_t width, int32_t height);

/* Add a device-pixel rectangle, clipping to viewport and merging if count would exceed IM_DIRTY_MAX_RECTS. */
bool im_dirty_add(im_dirty_list *list, im_irect rect);

/* Scale a 16.16 logical rectangle by scale (16.16), round outward to whole device pixels, clip, and add. */
bool im_dirty_add_scaled(im_dirty_list *list, im_rect logical_rect, im_fixed scale);

/* Drain detached-subtree damage from tree and collect all changed, moved, collapsed, or paint-dirty
   live nodes at device scale (marking full viewport dirty if scale changed since last collect). */
void im_dirty_collect_tree(im_dirty_list *list, im_tree *tree, im_node *root, im_fixed scale);

/* Return true if every pixel of rect (after clipping to list viewport) is covered by list->rects. */
bool im_dirty_covers(const im_dirty_list *list, im_irect rect);

/* Push a device-pixel dirty rectangle as a clip on ctx; returns false without altering outer clips on failure. */
bool im_dirty_push_clip(im_layer_ctx *ctx, im_irect rect);
