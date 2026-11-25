/* Impossible UI: drawing into a buffer of opaque pixels. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "theme.h"

/* A rectangle in pixels, 16.16 fixed point. */
typedef struct im_rect {
    im_fixed left, top, right, bottom; /* pixels */
} im_rect;

/* The four corner radii of a shape. */
typedef struct im_radii {
    im_fixed top_left, top_right, bottom_right, bottom_left;
} im_radii;

/* Where drawing goes, with the colours, scale and optional clip/layer/transform state to use. */
struct im_layer_ctx;
struct im_transform;
typedef struct im_surface {
    uint32_t *pixels; /* 0x00RRGGBB, top row first */
    int width, height, stride;
    im_palette palette;
    im_fixed scale; /* 1.0 at 96 DPI */
    struct im_layer_ctx *layers; /* NULL when drawing directly without clips or layers */
    const struct im_transform *transform; /* NULL for identity; when set, shapes and masks transform through it */
} im_surface;

/* Pixel coverage (0 to IM_ONE) of a rounded rectangle at integer pixel (x, y),
   using WinUI's proportional corner clamping and Wang-formula Bezier flattening. */
uint32_t im_shape_coverage(im_rect shape, im_radii radii, int x, int y);

/* Fill a rounded rectangle with a straight (non-premultiplied) 0xAARRGGBB color,
   honoring surface->layers clips, group opacity, and active offscreen layers. */
void im_fill_argb(im_surface *surface, im_rect shape, im_radii radii, uint32_t argb);

/* Edges are smoothed. Adjacent corner radii that exceed their shared edge are
   partitioned proportionally along that edge, matching WinUI's geometry rule. */
void im_fill(im_surface *surface, im_rect shape, im_radii radii, enum im_brush_id brush);

/* The ring between a shape and the same shape inset by the thickness. The radii
   are those of the line's middle, as WinUI's CornerRadius is: half a thickness
   more outside, half less inside. A gradient runs over the outer shape. */
void im_stroke(im_surface *surface, im_rect shape, im_radii radii, im_fixed thickness, enum im_brush_id brush);

/* Stroke a rounded rectangle with a straight (non-premultiplied) 0xAARRGGBB color. */
void im_stroke_argb(im_surface *surface, im_rect shape, im_radii radii, im_fixed thickness, uint32_t argb);

/* Draw an 8-bit alpha mask (e.g. a rasterized glyph) tinted with straight 0xAARRGGBB argb into dst_rect,
   honouring surface->transform (bilinear sampling) and surface->layers. */
void im_draw_alpha_mask(im_surface *surface, im_rect dst_rect, const uint8_t *alpha,
                        int width, int height, int stride_bytes, uint32_t argb);

/* Compute the local-space half-source-texel filter outset (pad_x, pad_y) in 16.16 fixed point that
   im_draw_alpha_mask can paint beyond dst_rect, so nodes hosting magnified masks can declare
   im_node_set_paint_outset for exact dirty-rectangle invalidation. */
void im_draw_alpha_mask_outset(im_fixed dst_w, im_fixed dst_h, int width, int height,
                               im_fixed *out_pad_x, im_fixed *out_pad_y);

/* The shape with every edge moved in by a distance. */
im_rect im_inset(im_rect shape, im_fixed by);

/* A corner radius moved out (or in, when negative) by a distance; square corners stay square. */
im_radii im_grow(im_radii radii, im_fixed by);
