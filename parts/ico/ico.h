/* Impossible SDK: portable freestanding ICO and CUR decoder and encoder. No floats, no OS calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "image.h"

/* Maximum number of directory entries supported in a single ICO or CUR file. */
#define IM_ICO_MAX_ENTRIES 256u

/* Caller-supplied memory allocator for ICO/CUR decoding and encoding (both callbacks required). */
typedef struct im_ico_alloc {
    /* Allocate size bytes, returning NULL on failure. */
    void *(*alloc)(void *ctx, size_t size);
    /* Release a buffer previously returned by alloc with its original size. */
    void (*free)(void *ctx, void *ptr, size_t size);
    /* Opaque caller context passed to alloc and free. */
    void *ctx;
} im_ico_alloc;

/* Container type declared in the ICONDIR header. */
typedef enum im_ico_type {
    /* Icon container (.ico, header type 1). */
    IM_ICO_TYPE_ICON = 1,
    /* Cursor container (.cur, header type 2). */
    IM_ICO_TYPE_CURSOR = 2
} im_ico_type;

/* Payload encoding mode for each entry written by im_ico_encode. */
typedef enum im_ico_encoding {
    /* Encode < 256x256 entries as DIB with AND mask, and 256x256 entries as 32-bit RGBA PNG. */
    IM_ICO_ENTRY_AUTO = 0,
    /* Encode as a DIB (BITMAPINFOHEADER with double height) plus 1-bpp AND mask. */
    IM_ICO_ENTRY_DIB = 1,
    /* Encode as an 8-bit color-type-6 RGBA PNG payload (cannot carry destination-invert pixels). */
    IM_ICO_ENTRY_PNG = 2
} im_ico_encoding;

/* Directory and payload metadata for one entry in an ICO or CUR container. */
typedef struct im_ico_entry_info {
    /* Width declared in ICONDIRENTRY (1..256; raw 0 decoded as 256). Used by im_ico_choose_entry. */
    uint32_t dir_width;
    /* Height declared in ICONDIRENTRY (1..256; raw 0 decoded as 256). Used by im_ico_choose_entry. */
    uint32_t dir_height;
    /* Palette color count declared in ICONDIRENTRY (0 means >= 256 or unspecified). */
    uint16_t dir_color_count;
    /* Color planes declared in ICONDIRENTRY (ICO only; 0 for CUR). */
    uint16_t dir_planes;
    /* Bits per pixel declared in ICONDIRENTRY (ICO only; 0 for CUR). */
    uint16_t dir_bit_count;
    /* Decoded payload width in pixels (from PNG IHDR or DIB BITMAPINFOHEADER). */
    uint32_t width;
    /* Decoded payload height in pixels (from PNG IHDR or half of DIB biHeight). */
    uint32_t height;
    /* Decoded payload bits per pixel (from DIB biBitCount, or 32 for PNG). */
    uint16_t bit_count;
    /* Cursor hotspot X coordinate (CUR only; 0 for ICO). */
    uint16_t hotspot_x;
    /* Cursor hotspot Y coordinate (CUR only; 0 for ICO). */
    uint16_t hotspot_y;
    /* Byte offset of the entry payload within the container. */
    uint32_t offset;
    /* Byte length of the entry payload within the container. */
    uint32_t size;
    /* True when the entry payload begins with the 8-byte PNG signature. */
    bool is_png;
    /* True when decoding found at least one Windows destination-invert pixel (AND=1 and XOR!=0). */
    bool has_invert_mask;
} im_ico_entry_info;

/* Input descriptor for one entry written by im_ico_encode. */
typedef struct im_ico_write_entry {
    /* Source 32-bit image (width and height in 1..256, IM_PIXEL_BGRA8 or IM_PIXEL_RGBA8). */
    const im_image *image;
    /* Optional 1-bit MSB-first row-packed mask (((width + 7) / 8) * height bytes) marking Windows AND=1/XOR!=0 invert pixels. */
    const uint8_t *invert_mask;
    /* Byte length of invert_mask (ignored when invert_mask is NULL). */
    size_t invert_mask_size;
    /* Cursor hotspot X coordinate (must be < image->width for IM_ICO_TYPE_CURSOR; ignored for IM_ICO_TYPE_ICON). */
    uint16_t hotspot_x;
    /* Cursor hotspot Y coordinate (must be < image->height for IM_ICO_TYPE_CURSOR; ignored for IM_ICO_TYPE_ICON). */
    uint16_t hotspot_y;
    /* Payload encoding choice (IM_ICO_ENTRY_AUTO, IM_ICO_ENTRY_DIB, or IM_ICO_ENTRY_PNG). */
    im_ico_encoding encoding;
} im_ico_write_entry;

/* Validate an ICO or CUR container header, entry bounds, and non-overlap without allocating memory.
   Refuses count == 0, count > IM_ICO_MAX_ENTRIES, out-of-bounds spans, and any overlapping entry spans. */
im_image_error im_ico_probe(const uint8_t *data, size_t len, im_ico_type *out_type, uint16_t *out_count);

/* Validate an ICO or CUR container and populate directory and payload metadata for every entry.
   Always writes the container's entry count to *out_count when the container is valid.
   When cap < count, writes zero entries into entries and returns IM_IMAGE_TOO_LARGE. */
im_image_error im_ico_list(const uint8_t *data,
                           size_t len,
                           im_ico_type *out_type,
                           im_ico_entry_info *entries,
                           uint16_t cap,
                           uint16_t *out_count);

/* Decode entry index (0..count-1) of an ICO or CUR container into straight 32-bit pixels (fmt).
   Validates container non-overlap, entry payload bounds, limits, and (for CUR) hotspot < decoded dimensions.
   When out_invert_mask is non-NULL (requires out_invert_len != NULL) and the entry contains Windows
   AND=1/XOR!=0 destination-invert pixels, allocates an exact-size (((w + 7) / 8) * h) 1-bit MSB-first mask
   through alloc; otherwise leaves *out_invert_mask = NULL and *out_invert_len = 0.
   On any error, zeroes *out_img and clears optional outputs while freeing all internal allocations. */
im_image_error im_ico_decode_entry(const uint8_t *data,
                                   size_t len,
                                   uint16_t index,
                                   im_pixel_format fmt,
                                   const im_image_limits *limits,
                                   const im_ico_alloc *alloc,
                                   im_image *out_img,
                                   uint8_t **out_invert_mask,
                                   size_t *out_invert_len,
                                   im_ico_entry_info *out_info);

/* Composite a decoded icon or cursor (with optional 1-bit invert_mask) onto a 32-bit destination buffer
   at (0, 0), preserving Windows AND=1/XOR!=0 destination-invert semantics and straight-alpha blending. */
void im_ico_composite_onto(uint8_t *dst_pixels,
                           uint32_t dst_stride,
                           uint32_t dst_w,
                           uint32_t dst_h,
                           im_pixel_format dst_fmt,
                           const im_image *icon,
                           const uint8_t *invert_mask,
                           size_t invert_mask_size);

/* Choose the best entry index (0..count-1) for target_w x target_h at screen_bpp (0 defaults to 32),
   matching Windows user32 LookupIconIdFromDirectoryEx using directory fields (minimizing combined
   size distance with 2x upscale penalty plus 2x color-depth distance, preferring closer color depth
   and earlier directory order on ties). Returns -1 on invalid arguments. */
int32_t im_ico_choose_entry(const im_ico_entry_info *entries,
                            uint16_t count,
                            uint32_t target_w,
                            uint32_t target_h,
                            uint32_t screen_bpp);

/* Choose the best entry index for logical_w x logical_h at scale_percent (e.g. 100, 125, 150, 200)
   and screen_bpp (0 defaults to 32), using overflow-safe half-up rounding to target pixel dimensions.
   Returns -1 on invalid arguments (entries == NULL, count == 0, logical_w == 0, logical_h == 0, or scale_percent == 0). */
int32_t im_ico_choose_entry_scaled(const im_ico_entry_info *entries,
                                   uint16_t count,
                                   uint32_t logical_w,
                                   uint32_t logical_h,
                                   uint32_t scale_percent,
                                   uint32_t screen_bpp);

/* Encode one or more images into an ICO or CUR container in a single exact-size *out_len buffer from alloc.
   DIB entries without invert_mask write 32-bpp BGRA with zeroed RGB where alpha == 0 plus a 1-bpp AND mask;
   DIB entries with invert_mask write 24-bpp BGR plus a 1-bpp AND mask so Windows executes AND/XOR inversion;
   PNG entries always encode as 8-bit color-type-6 RGBA. On any error, frees all allocations and clears outputs. */
im_image_error im_ico_encode(im_ico_type type,
                             const im_ico_write_entry *entries,
                             uint16_t count,
                             const im_ico_alloc *alloc,
                             uint8_t **out_data,
                             size_t *out_len);
