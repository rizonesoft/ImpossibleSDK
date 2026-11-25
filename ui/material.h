/* Impossible UI: Mica, Mica Alt, and Acrylic material region recording and theme fallback painting. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "draw.h"
#include "tree.h"

/* WinUI backdrop and surface material kinds. */
typedef enum im_material_kind {
    IM_MATERIAL_NONE = 0,
    IM_MATERIAL_MICA,
    IM_MATERIAL_MICA_ALT,
    IM_MATERIAL_ACRYLIC_IN_APP,
    IM_MATERIAL_ACRYLIC_BACKDROP
} im_material_kind;

/* One material region recorded during a frame for handoff to the host compositor. */
typedef struct im_material_region {
    im_rect bounds;
    im_radii radii;
    im_material_kind kind;
    uint32_t tint_argb;
    uint32_t fallback_argb;
    bool uses_fallback;
} im_material_region;

/* Frame list of material regions handed to the host compositor (or recording test host). */
typedef struct im_material_list {
    im_allocator alloc;
    im_material_region *items;
    uint32_t count;
    uint32_t cap;
} im_material_list;

/* Initialize an empty material region list with caller-supplied allocator. */
void im_material_list_init(im_material_list *list, im_allocator alloc);

/* Clear all recorded regions for the next frame without releasing capacity. */
void im_material_list_reset(im_material_list *list);

/* Free all memory owned by list. */
void im_material_list_destroy(im_material_list *list);

/* Resolve the authoritative WinUI tint and fallback colours for kind under node's effective theme and settings. */
void im_material_resolve(im_material_kind kind, const im_node *node, const im_palette *window_palette,
                         bool transparency_enabled, bool host_supports_backdrop, im_material_region *out);

/* Record a material region for the host compositor and draw its fallback (or in-app translucent tint) onto surface.
   Honours surface->layers clips/opacity and surface->transform. Returns false on region-list allocation failure. */
bool im_material_draw(im_surface *surface, im_material_list *list, im_rect bounds, im_radii radii,
                      im_material_kind kind, const im_node *node, const im_palette *window_palette,
                      bool transparency_enabled, bool host_supports_backdrop);
