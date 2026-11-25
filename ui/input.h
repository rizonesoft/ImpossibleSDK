/* Impossible UI: shared node-to-device transform mapping and pointer hit-testing. Portable. */
#pragma once
#include <stdbool.h>
#include "transform.h"
#include "tree.h"

/* Compute the shared node-local-to-device affine transform for node at DPI scale (16.16).
   Composes node local origin, node render_transform, RTL-aware absolute_rect offset,
   ancestor render_transform chain, and scale. Returns false if any ancestor is collapsed
   or if transform composition overflows. */
bool im_node_device_transform(const im_node *node, im_fixed scale, im_transform *out);

/* Compute the logical (pre-scale) axis-aligned painted bounding rect of node taking its
   own and all ancestor render_transforms into account, saturating wide bounds to the
   representable 16.16 range so zoomed visible nodes are never dropped. Returns false if
   empty, collapsed, or completely outside the representable 16.16 range. */
bool im_node_transformed_logical_bounds(const im_node *node, im_rect *out_logical);

/* Map a device-pixel pointer (dev_x, dev_y) in 16.16 fixed point back into node-local
   logical coordinates [0, width) x [0, height) at DPI scale. Returns false if any transform
   in the chain is singular or uninvertible. */
bool im_input_to_local(const im_node *node, im_fixed scale,
                       im_fixed dev_x, im_fixed dev_y,
                       im_fixed *out_local_x, im_fixed *out_local_y);

/* Hit-test device-pixel pointer (dev_x, dev_y) in 16.16 fixed point at DPI scale against root
   in front-to-back paint order (reverse child order), skipping collapsed or singular subtrees
   and allowing transformed children extending outside their parent to be hit.
   Returns the deepest hit node (and optional local coordinates), or NULL if nothing was hit. */
im_node *im_input_hit_test(im_node *root, im_fixed scale,
                           im_fixed dev_x, im_fixed dev_y,
                           im_fixed *out_local_x, im_fixed *out_local_y);
