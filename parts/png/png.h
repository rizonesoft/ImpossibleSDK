/* Impossible SDK: portable freestanding PNG decoder and encoder (RFC 2083 / ISO 15948). No floats, no OS calls. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "deflate.h"
#include "image.h"

/* Caller-supplied memory allocator for PNG decoding and encoding (both callbacks required). */
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
   alloc->free(alloc->ctx, out_image->pixels, out_image->byte_size).
   Per PNG §11.2.3, unused trailing bytes in consecutive CRC-verified IDAT chunks after the zlib
   stream completes are ignored; extra decompressed bytes and invalid CRCs return IM_IMAGE_INVALID. */
im_image_error im_png_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_png_alloc *alloc,
                             im_image *out_image);

/* Color-type selection mode for im_png_encode. */
typedef enum im_png_write_mode {
    /* Losslessly select among Gray (0), RGB (2), Indexed (3), Gray+Alpha (4), and RGBA (6) by smallest estimated uncompressed row bytes plus PLTE/tRNS chunk overhead. */
    IM_PNG_WRITE_AUTO = 0,
    /* Force Grayscale (color type 0; 1/2/4/8-bit); refuses non-gray or translucent pixels with IM_IMAGE_INVALID. */
    IM_PNG_WRITE_GRAY = 1,
    /* Force Truecolor RGB (color type 2; 8-bit); refuses translucent pixels with IM_IMAGE_INVALID. */
    IM_PNG_WRITE_RGB = 2,
    /* Force Indexed palette (color type 3; 1/2/4/8-bit with minimal PLTE and optional trimmed tRNS); refuses >256 colors. */
    IM_PNG_WRITE_INDEXED = 3,
    /* Force Grayscale+Alpha (color type 4; 8-bit); refuses non-gray pixels with IM_IMAGE_INVALID. */
    IM_PNG_WRITE_GRAY_ALPHA = 4,
    /* Force Truecolor+Alpha RGBA (color type 6; 8-bit). */
    IM_PNG_WRITE_RGBA = 5
} im_png_write_mode;

/* Options for im_png_encode (passing NULL selects IM_PNG_WRITE_AUTO and IM_DEFLATE_BALANCED). */
typedef struct im_png_write_options {
    /* Color-type selection mode. */
    im_png_write_mode mode;
    /* DEFLATE compression effort for IDAT payload. */
    im_deflate_effort effort;
} im_png_write_options;

/* Losslessly encode img (IM_PIXEL_BGRA8 or IM_PIXEL_RGBA8) into a minimal PNG containing only
   IHDR, optional PLTE/tRNS, IDAT (with per-row minimum-SAD filtering across all 5 PNG filters),
   and IEND, with zero ancillary metadata. Each non-NULL output pointer is cleared independently on
   entry. On IM_IMAGE_OK, *out_data points to a buffer allocated with exact size *out_len, which
   the caller frees via alloc->free(alloc->ctx, *out_data, *out_len). On any non-OK return, all
   temporary allocations are freed with their exact sizes. */
im_image_error im_png_encode(const im_image *img,
                             const im_png_write_options *opts,
                             const im_png_alloc *alloc,
                             uint8_t **out_data,
                             size_t *out_len);
