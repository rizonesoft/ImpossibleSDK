/* Impossible SDK: portable freestanding GIF87a/GIF89a decoder (LZW, local palettes, transparency, disposal, animation). No floats, no OS calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "image.h"

/* Default maximum number of animation frames decoded by im_gif_decode_anim when max_frames == 0. */
#define IM_GIF_DEFAULT_MAX_FRAMES 1024u

/* Caller-supplied memory allocator for GIF decoding (both callbacks required). */
typedef struct im_gif_alloc {
    /* Allocate size bytes, returning NULL on failure. */
    void *(*alloc)(void *ctx, size_t size);
    /* Release a buffer previously returned by alloc with its original size. */
    void (*free)(void *ctx, void *ptr, size_t size);
    /* Opaque caller context passed to alloc and free. */
    void *ctx;
} im_gif_alloc;

/* Header metadata returned by im_gif_probe without allocating pixel storage. */
typedef struct im_gif_info {
    /* Logical screen width in pixels (1..65535). */
    uint32_t width;
    /* Logical screen height in pixels (1..65535). */
    uint32_t height;
    /* True when a Global Color Table follows the Logical Screen Descriptor. */
    bool has_gct;
    /* Number of entries in the Global Color Table (0 when has_gct is false, else 2..256). */
    uint16_t gct_size;
    /* Background color index declared in the Logical Screen Descriptor. */
    uint8_t bg_index;
    /* True for GIF89a, false for GIF87a. */
    bool is_gif89a;
    /* Decoded 32-bit single-frame canvas size in bytes (width * height * 4). */
    size_t byte_size;
} im_gif_info;

/* One composited frame of a decoded GIF animation. */
typedef struct im_gif_frame {
    /* Full logical-screen composited 32-bit canvas after applying this frame. */
    im_image image;
    /* Frame display delay in milliseconds (GCE centiseconds * 10; 0 if no GCE). */
    uint32_t delay_ms;
    /* Encoded GCE disposal method (0=unspecified, 1=keep, 2=clear to transparent, 3=restore previous). */
    uint8_t disposal;
    /* Sub-rectangle left coordinate within the logical screen. */
    uint16_t left;
    /* Sub-rectangle top coordinate within the logical screen. */
    uint16_t top;
    /* Sub-rectangle width in pixels. */
    uint16_t frame_width;
    /* Sub-rectangle height in pixels. */
    uint16_t frame_height;
} im_gif_frame;

/* Full decoded GIF animation. Release via im_gif_anim_free(alloc, anim). */
typedef struct im_gif_anim {
    /* Logical screen width in pixels. */
    uint32_t width;
    /* Logical screen height in pixels. */
    uint32_t height;
    /* Number of decoded frames in frames[0..frame_count-1]. */
    uint32_t frame_count;
    /* Allocated capacity of the frames array (passed back to alloc->free by im_gif_anim_free). */
    uint32_t frame_cap;
    /* Loop count from NETSCAPE2.0 / ANIMEXTS1.0 (0 = infinite when has_loop_count is true). */
    uint32_t loop_count;
    /* True when a NETSCAPE2.0 or ANIMEXTS1.0 loop extension was present. */
    bool has_loop_count;
    /* Caller-owned array of frame_count decoded frames (capacity frame_cap). */
    im_gif_frame *frames;
} im_gif_anim;

/* Validate the 6-byte GIF87a/GIF89a signature and 7-byte Logical Screen Descriptor (13 bytes minimum)
   and check dimensions and single-frame byte_size against limits (NULL applies SDK defaults) without
   allocating. A non-NULL buffer with len == 0 returns IM_IMAGE_TRUNCATED. On any non-OK return,
   *out_info (if non-NULL) is zero-initialized. */
im_image_error im_gif_probe(const uint8_t *data, size_t len,
                            const im_image_limits *limits,
                            im_gif_info *out_info);

/* Decode a complete GIF buffer and return frame 0 as a gamma-free 32-bit im_image (straight 8-bit
   IM_PIXEL_BGRA8 or IM_PIXEL_RGBA8). All subsequent frames and the 0x3B trailer are decoded and
   validated in-place without retaining frames 1..N-1 so damaged or truncated streams never expose
   a partial picture. Plain Text Extensions (0x21 0x01) are not rendered into pixels but consume
   any pending Graphic Control Extension per GIF89a §§23, 25. The initial canvas and disposal 2
   sub-rectangle clears use transparent black (0, 0, 0, 0). Passing NULL for limits applies SDK
   defaults. On entry, *out_image is overwritten; on any error all allocations are freed and
   *out_image is zero-initialized. On IM_IMAGE_OK, only out_image->pixels remains live and the
   caller frees it via alloc->free(alloc->ctx, out_image->pixels, out_image->byte_size). */
im_image_error im_gif_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_gif_alloc *alloc,
                             im_image *out_image);

/* Decode all frames of a complete GIF buffer into *out_anim with full disposal compositing
   (initial canvas and disposal 2 clear to transparent black (0, 0, 0, 0)). max_frames == 0 defaults
   to IM_GIF_DEFAULT_MAX_FRAMES (1024). Cumulative decoded canvas bytes across all frames are checked
   against limits->max_bytes (or IM_IMAGE_DEFAULT_MAX_BYTES when limits == NULL or max_bytes == 0)
   before allocating each frame; exceeding either limit returns IM_IMAGE_TOO_LARGE with zero leaks.
   On any non-OK return, all allocations are freed and *out_anim is zero-initialized. */
im_image_error im_gif_decode_anim(const uint8_t *data, size_t len,
                                  im_pixel_format format,
                                  const im_image_limits *limits,
                                  uint32_t max_frames,
                                  const im_gif_alloc *alloc,
                                  im_gif_anim *out_anim);

/* Free all frame pixel buffers and the frames array of *anim using the original alloc and zero *anim. */
void im_gif_anim_free(const im_gif_alloc *alloc, im_gif_anim *anim);
