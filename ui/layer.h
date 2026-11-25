/* Impossible UI: bounded offscreen layers, group opacity and nested clip/layer stacks. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "clip.h"

/* Result of pushing a layer onto an im_layer_ctx. */
typedef enum im_layer_result {
    IM_LAYER_OK = 0,       /* Offscreen premultiplied buffer allocated and active; caller must call im_layer_pop. */
    IM_LAYER_FALLBACK = 1, /* Buffer refused (memory limit or alloc failure); direct-draw fallback active; caller must call im_layer_pop. */
    IM_LAYER_ERR = 2       /* Stack metadata allocation failed or invalid context; nothing pushed; do NOT call im_layer_pop. */
} im_layer_result;

/* One active layer frame on the layer stack. */
typedef struct im_layer_frame {
    uint32_t *pixels; /* Premultiplied 0xAARRGGBB buffer, or NULL for fallback / empty-bounds layer. */
    size_t buffer_bytes;
    int x;
    int y;
    int width;
    int height;
    uint8_t opacity;          /* Requested layer opacity (0..255). */
    uint8_t fallback_opacity; /* Effective cumulative direct-draw opacity while this frame is active. */
    bool is_fallback;
    bool has_clip;
    bool empty_bounds;
    uint32_t clip_base;       /* Inclusive clip stack depth after this layer's own optional clip (if has_clip). */
} im_layer_frame;

/* Clip and offscreen layer context attached to an im_surface via surface->layers. */
typedef struct im_layer_ctx {
    im_surface *target;
    im_allocator alloc;
    size_t memory_limit;
    size_t bytes_used;
    im_clip_stack clips;
    im_layer_frame *frames;
    uint32_t frame_count;
    uint32_t frame_cap;
} im_layer_ctx;

/* Initialise a layer context bound to target with a caller-supplied allocator and live buffer memory_limit.
   Attaches ctx to target->layers. Returns false if target or allocator is invalid. */
bool im_layer_init(im_layer_ctx *ctx, im_surface *target, im_allocator alloc, size_t memory_limit);

/* Destroy any remaining layers, release all memory, and detach ctx from target->layers. */
void im_layer_destroy(im_layer_ctx *ctx);

/* Push a rectangular clip on ctx. Returns false on allocation failure, leaving stack unchanged. */
bool im_layer_push_clip_rect(im_layer_ctx *ctx, im_rect rect);

/* Push a rounded-rectangle clip on ctx. Returns false on allocation failure, leaving stack unchanged. */
bool im_layer_push_clip_rounded(im_layer_ctx *ctx, im_rect rect, im_radii radii);

/* Pop the top clip pushed within the current layer. */
void im_layer_pop_clip(im_layer_ctx *ctx);

/* Push a bounded group-opacity layer (opacity 0..255) with optional rounded clip (radii may be all 0;
   pass has_clip=false for no extra clip). Bounds are intersected against parent bounds and active clips
   before checking bytes_used + needed <= memory_limit.
   Returns IM_LAYER_OK (offscreen buffer active), IM_LAYER_FALLBACK (direct-draw fallback active;
   still call im_layer_pop), or IM_LAYER_ERR (nothing pushed; do not call im_layer_pop). */
im_layer_result im_layer_push(im_layer_ctx *ctx, im_rect bounds, uint8_t opacity, bool has_clip, im_radii clip_radii);

/* Composite the top layer onto its parent (applying opacity and clip) and release its buffer,
   or pop a fallback frame and restore parent clip and opacity. */
void im_layer_pop(im_layer_ctx *ctx);

/* Blend one straight 0xAARRGGBB color with coverage (0..IM_ONE) at target-space pixel (x, y)
   through the active clip stack and active layer (or target surface). Used by ui/draw.c. */
void im_layer_blend_pixel(im_layer_ctx *ctx, int x, int y, uint32_t argb, uint32_t coverage);

/* Return the effective integer pixel bounds [left, right) x [top, bottom) for drawing into ctx. */
void im_layer_draw_bounds(const im_layer_ctx *ctx, int *left, int *top, int *right, int *bottom);
