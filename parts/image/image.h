/* Impossible SDK: shared portable image type, limits, and errors. No floats, no OS calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Default maximum width or height accepted by image decoders when no custom limit is supplied. */
#define IM_IMAGE_DEFAULT_MAX_DIM 16384u

/* Default maximum decoded pixel buffer size in bytes (16 MiB) when no custom limit is supplied. */
#define IM_IMAGE_DEFAULT_MAX_BYTES ((size_t)(16u * 1024u * 1024u))

/* Named errors returned by all image decoders and limit checks. */
typedef enum im_image_error {
    /* Decoding or validation succeeded. */
    IM_IMAGE_OK = 0,
    /* A required pointer or parameter was NULL or out of range. */
    IM_IMAGE_BAD_ARG,
    /* Input bytes do not conform to the format or declare zero dimensions. */
    IM_IMAGE_INVALID,
    /* Input ended before the full image payload could be read. */
    IM_IMAGE_TRUNCATED,
    /* Input uses a valid format feature that the decoder does not support. */
    IM_IMAGE_UNSUPPORTED,
    /* Image dimensions or total pixel byte size exceed the configured limits or overflow. */
    IM_IMAGE_TOO_LARGE,
    /* Caller allocator failed to provide the requested pixel buffer. */
    IM_IMAGE_NO_MEMORY
} im_image_error;

/* Pixel channel layout for 32-bit decoded images (4 bytes per pixel, straight non-premultiplied sRGB). */
typedef enum im_pixel_format {
    /* Blue, green, red, alpha in memory order (little-endian 0xAARRGGBB; shares BGR byte positions with Impossible UI's opaque 0x00RRGGBB surfaces while carrying straight 8-bit alpha in byte 3). */
    IM_PIXEL_BGRA8 = 0,
    /* Red, green, blue, alpha in memory order. */
    IM_PIXEL_RGBA8
} im_pixel_format;

/* Caller-supplied bounds checked before any pixel buffer allocation is attempted. */
typedef struct im_image_limits {
    /* Maximum allowed image width in pixels (0 uses IM_IMAGE_DEFAULT_MAX_DIM). */
    uint32_t max_width;
    /* Maximum allowed image height in pixels (0 uses IM_IMAGE_DEFAULT_MAX_DIM). */
    uint32_t max_height;
    /* Maximum allowed decoded pixel buffer size in bytes (0 uses IM_IMAGE_DEFAULT_MAX_BYTES). */
    size_t max_bytes;
} im_image_limits;

/* Decoded 32-bit image: top-to-bottom row order, stride in bytes (>= width * 4), caller-owned pixels.
   On any non-OK error return, decoders zero-initialize the entire struct so no partial image is exposed. */
typedef struct im_image {
    /* Image width in pixels. */
    uint32_t width;
    /* Image height in pixels. */
    uint32_t height;
    /* Distance between consecutive rows in bytes (always >= width * 4). */
    uint32_t stride;
    /* Channel order of each 4-byte pixel. */
    im_pixel_format format;
    /* Caller-owned pixel storage (NULL when empty or on error); lifetime is managed by the caller. */
    uint8_t *pixels;
    /* Total size of the pixel buffer in bytes (stride * height). */
    size_t byte_size;
} im_image;

/* Validate image dimensions and compute width * height * 4 without overflow before allocating pixels.
   Passing NULL for limits applies IM_IMAGE_DEFAULT_MAX_DIM and IM_IMAGE_DEFAULT_MAX_BYTES. */
im_image_error im_image_check_limits(uint32_t width, uint32_t height, const im_image_limits *limits, size_t *out_bytes);

/* Return a stable ASCII name for an im_image_error value, or "IM_IMAGE_UNKNOWN" if out of range. */
const char *im_image_error_name(im_image_error err);
