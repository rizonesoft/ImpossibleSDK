/* Impossible UI: rectangular and rounded-rectangle clip stacks. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "tree.h"

/* One clip entry on an im_clip_stack (rectangular when all radii are 0). */
typedef struct im_clip_entry {
    im_rect rect;
    im_radii radii;
    bool is_rounded;
    int int_left;
    int int_top;
    int int_right;
    int int_bottom;
} im_clip_entry;

/* Growable stack of active clips backed by a caller-supplied allocator. */
typedef struct im_clip_stack {
    im_allocator alloc;
    im_clip_entry *entries;
    uint32_t count;
    uint32_t cap;
    int surface_w;
    int surface_h;
} im_clip_stack;

/* Initialise an empty clip stack for a surface of dimensions (width, height). */
bool im_clip_init(im_clip_stack *stack, im_allocator alloc, int width, int height);

/* Release all memory owned by the clip stack. */
void im_clip_destroy(im_clip_stack *stack);

/* Push a rectangular clip. Returns false on allocation failure, leaving the stack unchanged. */
bool im_clip_push_rect(im_clip_stack *stack, im_rect rect);

/* Push a rounded-rectangle clip. Returns false on allocation failure, leaving the stack unchanged. */
bool im_clip_push_rounded(im_clip_stack *stack, im_rect rect, im_radii radii);

/* Pop the top clip entry. Safe no-op on an empty stack. */
void im_clip_pop(im_clip_stack *stack);

/* Return the intersected integer pixel bounding box [left, right) x [top, bottom) of all active clips. */
void im_clip_bounds(const im_clip_stack *stack, int *left, int *top, int *right, int *bottom);

/* Return the combined clip coverage (0 to IM_ONE) for pixel (x, y) across all active clips. */
uint32_t im_clip_pixel_coverage(const im_clip_stack *stack, int x, int y);
