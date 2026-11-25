/* Impossible UI: premultiplied picture drawing with bilinear filtering and nine-slice. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "transform.h"

/* WinUI Stretch modes for picture placement inside a destination rectangle. */
typedef enum im_stretch {
    IM_STRETCH_NONE = 0,            /* Native size scaled by surface->scale, centered, clipped to dst_rect. */
    IM_STRETCH_FILL,                /* Stretched independently on X and Y to fill dst_rect. */
    IM_STRETCH_UNIFORM,             /* Uniformly scaled to fit inside dst_rect (centered, letterboxed). */
    IM_STRETCH_UNIFORM_TO_FILL      /* Uniformly scaled to cover dst_rect (centered, cropped to dst_rect). */
} im_stretch;

/* Premultiplied 0xAARRGGBB image held in memory, top row first. */
typedef struct im_picture {
    const uint32_t *pixels; /* Premultiplied 0xAARRGGBB; top row first. */
    int32_t width;          /* Width in pixels (> 0). */
    int32_t height;         /* Height in pixels (> 0). */
    int32_t stride_pixels;  /* Row stride in uint32_t pixels (>= width). */
} im_picture;

/* Nine-slice source insets in integer source image pixels. */
typedef struct im_nine_slice {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} im_nine_slice;

/* Convert straight (non-premultiplied) 4-byte pixels (BGRA8 when is_rgba == false, RGBA8 when true)
   with byte stride src_stride_bytes into premultiplied 0xAARRGGBB pixels in dst_pixels (stride dst_stride_pixels).
   Returns false on invalid dimensions, pointers, or strides. */
bool im_picture_premultiply_straight(const uint8_t *src_bytes, int32_t width, int32_t height,
                                     uint32_t src_stride_bytes, bool is_rgba,
                                     uint32_t *dst_pixels, int32_t dst_stride_pixels);

/* Draw pic into surface within dst_rect using WinUI bilinear premultiplied filtering and stretch mode,
   honouring surface->transform and surface->layers. Returns false on invalid input or singular transform. */
bool im_picture_draw(im_surface *surface, const im_picture *pic, im_rect dst_rect, im_stretch stretch);

/* Draw a nine-part picture into surface within dst_rect: corners keep exact scaled device-pixel sizes
   (round(inset * surface->scale), proportionally shrunk if dst_rect is smaller than corner sum),
   edges stretch along one axis, center stretches along both, and bilinear taps clamp within each slice. */
bool im_picture_draw_nine_slice(im_surface *surface, const im_picture *pic, im_rect dst_rect, im_nine_slice slice);
