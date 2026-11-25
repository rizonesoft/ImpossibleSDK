/* Impossible SDK: portable freestanding PNG decoder (RFC 2083 / ISO 15948). No floats, no OS calls. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "image.h"

/* Caller-supplied memory allocator for PNG decoding (both callbacks required). */
typedef struct im_png_alloc {
    /* Allocate size bytes, returning NULL on failure. */
    void *(*alloc)(void *ctx, size_t size);
    /* Release a buffer previously returned by alloc with its original size. */
    void (*free)(void *ctx, void *ptr, size_t size);
    /* Opaque caller context passed to alloc and free. */
    void *ctx;
} im_png_alloc;

/* Header metadata returned by im_png_probe without allocating pixel storage. */
typedef struct im_png_info {
    /* Image width in pixels (1..0x7FFFFFFF). */
    uint32_t width;
    /* Image height in pixels (1..0x7FFFFFFF). */
    uint32_t height;
    /* Sample bit depth (1, 2, 4, 8, or 16). */
    uint8_t bit_depth;
    /* PNG color type (0=Gray, 2=RGB, 3=Indexed, 4=Gray+Alpha, 6=RGBA). */
    uint8_t color_type;
    /* Interlace method (0=None, 1=Adam7). */
    uint8_t interlace;
    /* Decoded 32-bit pixel buffer size in bytes (width * height * 4). */
    size_t byte_size;
} im_png_info;

/* Validate the 8-byte PNG signature and CRC-checked IHDR chunk (33 bytes minimum) and check
   dimensions and decoded byte_size against limits (NULL applies SDK defaults) without allocating.
   On any non-OK return, *out_info (if non-NULL) is zero-initialized. */
im_image_error im_png_probe(const uint8_t *data, size_t len,
                            const im_image_limits *limits,
                            im_png_info *out_info);

/* Decode a complete PNG buffer into a gamma-free 32-bit im_image (preserving encoded 8-bit sample
   values without gamma or color-profile conversion) in IM_PIXEL_BGRA8 or IM_PIXEL_RGBA8 order.
   Passing NULL for limits applies SDK defaults. On entry, *out_image is overwritten (the caller
   must have already freed any previous pixel buffer). On any non-OK return, all allocations made
   during decoding are freed and *out_image is zero-initialized so no partial picture is exposed.
   On IM_IMAGE_OK, only out_image->pixels remains live and the caller frees it via
   alloc->free(alloc->ctx, out_image->pixels, out_image->byte_size). */
im_image_error im_png_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_png_alloc *alloc,
                             im_image *out_image);
