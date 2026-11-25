/* Impossible UI: integer elevation shadows and LRU shadow mask cache (pinned DropShadowRecipe.h). Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "tree.h"

/* Exact WinUI ThemeShadow recipe derived from pinned DropShadowRecipe.h (integers only, 16.16 fixed point). */
typedef struct im_shadow_recipe {
    im_fixed elevation;
    im_fixed ambient_blur_radius;
    im_fixed directional_blur_radius;
    im_fixed ambient_y_offset;
    im_fixed directional_y_offset;
    uint8_t ambient_alpha;
    uint8_t directional_alpha;
    im_thickness insets;
} im_shadow_recipe;

/* One cached theme-independent shadow mask entry (directional + optional ambient blurred alpha masks). */
typedef struct im_shadow_cache_entry {
    int32_t caster_w;
    int32_t caster_h;
    im_radii caster_radii;
    im_fixed elevation;
    im_fixed scale;
    int32_t mask_w;
    int32_t mask_h;
    int32_t pad_left;
    int32_t pad_top;
    uint8_t *dir_mask;
    uint8_t *amb_mask;
    size_t bytes;
    uint64_t last_use;
} im_shadow_cache_entry;

/* Maximum number of cached shadow mask entries in an im_shadow_cache. */
#define IM_SHADOW_CACHE_MAX_ENTRIES 16

/* Bounded LRU cache for blurred elevation shadow masks. */
typedef struct im_shadow_cache {
    im_allocator alloc;
    size_t memory_limit;
    size_t used_bytes;
    uint64_t use_clock;
    uint32_t blur_computations;
    uint32_t cache_hits;
    uint32_t entry_count;
    im_shadow_cache_entry entries[IM_SHADOW_CACHE_MAX_ENTRIES];
} im_shadow_cache;

/* Compute the pinned WinUI DropShadowRecipe for translation_z (16.16 DIPs) and theme (Light/Dark/Contrast). */
void im_shadow_recipe_for(im_fixed translation_z, im_theme theme, im_shadow_recipe *out);

/* Return the logical shadow outset thickness (for im_node_set_paint_outset) at translation_z and theme. */
im_thickness im_shadow_outset(im_fixed translation_z, im_theme theme);

/* Initialize a shadow cache with caller-supplied allocator and byte memory_limit (0 = unlimited). */
void im_shadow_cache_init(im_shadow_cache *cache, im_allocator alloc, size_t memory_limit);

/* Destroy all cached shadow masks and free 100% of allocated memory. */
void im_shadow_cache_destroy(im_shadow_cache *cache);

/* Draw a hollow rounded-rectangle elevation shadow onto surface (using cache when non-NULL).
   Honours surface->layers clips/opacity and surface->transform (singular = no-op). Returns false on OOM. */
bool im_shadow_draw(im_surface *surface, im_shadow_cache *cache, im_rect caster_bounds,
                    im_radii caster_radii, im_fixed translation_z);
