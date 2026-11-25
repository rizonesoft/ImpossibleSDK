/* Impossible UI: Canvas, Viewbox, UniformGrid, WrapPanel, RelativePanel,
   VariableSizedWrapGrid, ItemsWrapGrid, and ItemsStackPanel layout. Portable. */
#pragma once
#include "layout.h"

/* Measure one of the extended panels (Canvas, Viewbox, UniformGrid, WrapPanel, RelativePanel,
   VariableSizedWrapGrid, ItemsWrapGrid, ItemsStackPanel). Sets *ok = false and records
   tree->layout_error on cycle, missing RelativePanel sibling, or OOM. */
im_size im_panels_measure_override(im_tree *tree, im_node *node, im_size avail,
                                   im_fixed scale, bool rounding, bool *ok);

/* Arrange one of the extended panels within final_size. Sets *ok = false on failure. */
void im_panels_arrange_override(im_tree *tree, im_node *node, im_size final_size,
                                im_fixed scale, bool rounding, bool *ok);
