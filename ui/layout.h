/* Impossible UI: WinUI-compatible layout rounding, measure and arrange. Portable. */
#pragma once
#include "tree.h"

/* Round a 16.16 logical coordinate to the nearest physical pixel at scale, matching
   WinUI's CUIElement::LayoutRound (uielement.cpp:4881-4915, XcpRound = XcpFloor(x + 0.5)).
   IM_FIXED_AUTO and IM_FIXED_INF pass through unchanged. */
im_fixed im_layout_round(im_fixed value, im_fixed scale);

/* Round all four edges of a thickness with im_layout_round. */
im_thickness im_layout_round_thickness(im_thickness t, im_fixed scale);

/* Measure node and its subtree within available size at the given 16.16 DPI scale.
   Pass IM_FIXED_INF for either axis to measure unbounded along that axis. */
im_size im_layout_measure(im_tree *tree, im_node *node, im_size available, im_fixed scale);

/* Arrange node and its subtree within final_rect (in parent coordinates) at scale,
   computing layout_rect and root-relative absolute_rect (with RTL mirroring). */
void im_layout_arrange(im_tree *tree, im_node *node, im_rect final_rect, im_fixed scale);

/* Convenience wrapper: clears layout_error, measures root in (avail_w, avail_h), and arranges it in
   (0, 0, avail_w, avail_h) only if measure succeeded with no layout error. */
void im_layout_run(im_tree *tree, im_node *root, im_fixed avail_w, im_fixed avail_h, im_fixed scale);

/* Return the last layout error recorded on tree (IM_LAYOUT_OK on success). If out_name is non-NULL,
   stores a pointer to the tree-owned offending node name string (or "" when none). */
im_layout_error im_layout_last_error(const im_tree *tree, const char **out_name);

/* Clear any recorded layout error on tree. */
void im_layout_clear_error(im_tree *tree);
