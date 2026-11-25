/* Impossible SDK: portable freestanding BMP decoder (OS/2 1.x/2.x and Windows V3/V4/V5). No floats, no OS calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "image.h"

/* Caller-supplied memory allocator for BMP decoding (both callbacks required). */
typedef struct im_bmp_alloc {
    /* Allocate size bytes, returning NULL on failure. */
    void *(*alloc)(void *ctx, size_t size);
    /* Release a buffer previously returned by alloc with its original size. */
    void (*free)(void *ctx, void *ptr, size_t size);
    /* Opaque caller context passed to alloc and free. */
    void *ctx;
} im_bmp_alloc;

/* Header metadata returned by im_bmp_probe without allocating pixel storage. */
typedef struct im_bmp_info {
    /* Image width in pixels (1..0x7FFFFFFF). */
    uint32_t width;
    /* Image height in pixels (1..0x7FFFFFFF). */
    uint32_t height;
    /* Bits per pixel declared by the DIB header (1, 2, 4, 8, 16, 24, or 32). */
    uint16_t bit_count;
    /* Compression method code declared by the DIB header (0=RGB, 1=RLE8, 2=RLE4, 3=BITFIELDS, 6=ALPHABITFIELDS). */
    uint32_t compression;
    /* Size in bytes of the DIB header (12, 16, 40, 52, 56, 64, 108, or 124). */
    uint32_t header_size;
    /* True when the DIB header declares top-down row order (negative height). */
    bool top_down;
    /* Decoded 32-bit pixel buffer size in bytes (width * height * 4). */
    size_t byte_size;
} im_bmp_info;

/* Validate the 14-byte BITMAPFILEHEADER, DIB header (14 + header_size bytes, plus 12 bytes for
   v3 BI_BITFIELDS or 16 bytes for v3 BI_ALPHABITFIELDS when header_size == 40), bitfield masks,
   and header-declared ranges against limits (NULL applies SDK defaults) without requiring pixel
   payload bytes and without allocating. A non-NULL buffer with len == 0 returns IM_IMAGE_TRUNCATED.
   On any non-OK return, *out_info (if non-NULL) is zero-initialized. */
im_image_error im_bmp_probe(const uint8_t *data, size_t len,
                            const im_image_limits *limits,
                            im_bmp_info *out_info);

/* Decode a complete BMP buffer into a gamma-free 32-bit im_image (preserving encoded 8-bit sample
   values without gamma or color-profile conversion; linked V5 ICC profiles are never opened) in
   IM_PIXEL_BGRA8 or IM_PIXEL_RGBA8 order. Passing NULL for limits applies SDK defaults. On entry,
   *out_image is overwritten (the caller must have already freed any previous pixel buffer). On any
   non-OK return, all allocations made during decoding are freed and *out_image is zero-initialized
   so no partial picture is exposed. On IM_IMAGE_OK, only out_image->pixels remains live and the
   caller frees it via alloc->free(alloc->ctx, out_image->pixels, out_image->byte_size). */
im_image_error im_bmp_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_bmp_alloc *alloc,
                             im_image *out_image);
