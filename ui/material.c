/* Impossible UI: Mica, Mica Alt, and Acrylic material region recording and theme fallback painting. Portable. */
#include "material.h"
#include "resources.h"
#include "transform.h"

void im_material_list_init(im_material_list *list, im_allocator alloc) {
    if (!list) return;
    list->alloc = alloc;
    list->items = NULL;
    list->count = 0u;
    list->cap = 0u;
}

void im_material_list_reset(im_material_list *list) {
    if (!list) return;
    list->count = 0u;
}

void im_material_list_destroy(im_material_list *list) {
    if (!list) return;
    if (list->items && list->alloc.free) {
        list->alloc.free(list->alloc.ctx, list->items, (size_t)list->cap * sizeof(im_material_region));
    }
    list->items = NULL;
    list->count = 0u;
    list->cap = 0u;
}

static bool list_append(im_material_list *list, const im_material_region *reg) {
    if (!list || !reg) return false;
    if (list->count == list->cap) {
        if (!list->alloc.realloc) return false;
        uint32_t new_cap = list->cap ? list->cap * 2u : 4u;
        if (new_cap < list->cap) return false;
        size_t old_bytes = (size_t)list->cap * sizeof(im_material_region);
        size_t new_bytes = (size_t)new_cap * sizeof(im_material_region);
        if (new_bytes / sizeof(im_material_region) != (size_t)new_cap) return false;
        im_material_region *grown = (im_material_region *)list->alloc.realloc(
            list->alloc.ctx, list->items, old_bytes, new_bytes);
        if (!grown) return false; /* atomic: preserve existing list entries */
        list->items = grown;
        list->cap = new_cap;
    }
    list->items[list->count++] = *reg;
    return true;
}

void im_material_resolve(im_material_kind kind, const im_node *node, const im_palette *window_palette,
                         bool transparency_enabled, bool host_supports_backdrop, im_material_region *out) {
    if (!out) return;
    out->bounds = (im_rect){0, 0, 0, 0};
    out->radii = (im_radii){0, 0, 0, 0};
    out->kind = kind;
    if (kind == IM_MATERIAL_NONE) {
        out->fallback_argb = 0u;
        out->tint_argb = 0u;
        out->uses_fallback = true;
        return;
    }

    im_palette eff = im_resources_effective_palette(node, window_palette);
    im_theme th = eff.theme;
    bool use_fb = (!transparency_enabled) || (th == IM_CONTRAST) || (!host_supports_backdrop);
    out->uses_fallback = use_fb;

    if (th == IM_CONTRAST) {
        uint32_t win_col = eff.slot[IM_SLOT_COLOR_WINDOW];
        out->tint_argb = win_col;
        out->fallback_argb = win_col;
        return;
    }

    switch (kind) {
    case IM_MATERIAL_MICA:
        out->fallback_argb = im_resources_resolve_brush(node, window_palette, IM_BRUSH_SolidBackgroundFillColorBaseBrush, 0, IM_ONE, IM_ONE);
        out->tint_argb = (th == IM_LIGHT) ? 0xCCF3F3F3u : 0xCC202020u;
        break;
    case IM_MATERIAL_MICA_ALT:
        out->fallback_argb = im_resources_resolve_brush(node, window_palette, IM_BRUSH_SolidBackgroundFillColorBaseAltBrush, 0, IM_ONE, IM_ONE);
        out->tint_argb = (th == IM_LIGHT) ? 0xCCDADADAu : 0xCC0A0A0Au;
        break;
    case IM_MATERIAL_ACRYLIC_BACKDROP:
        /* Pinned AcrylicBrush_themeresources.xaml: SystemChromeAltHighColor at opacity 0.8 (#CC1C1C1C Dark, #CCFFFFFF Light). */
        out->fallback_argb = im_resources_resolve_brush(node, window_palette, IM_BRUSH_SystemControlAcrylicWindowBrush, 0, IM_ONE, IM_ONE);
        out->tint_argb = (th == IM_LIGHT) ? 0xCCFFFFFFu : 0xCC1C1C1Cu;
        break;
    case IM_MATERIAL_ACRYLIC_IN_APP:
        out->fallback_argb = im_resources_resolve_brush(node, window_palette, IM_BRUSH_SystemControlAcrylicElementBrush, 0, IM_ONE, IM_ONE);
        out->tint_argb = (th == IM_LIGHT) ? 0xCCFFFFFFu : 0xCC1C1C1Cu;
        break;
    default:
        out->fallback_argb = 0u;
        out->tint_argb = 0u;
        out->uses_fallback = true;
        break;
    }
}

bool im_material_draw(im_surface *surface, im_material_list *list, im_rect bounds, im_radii radii,
                      im_material_kind kind, const im_node *node, const im_palette *window_palette,
                      bool transparency_enabled, bool host_supports_backdrop) {
    if (!surface || kind == IM_MATERIAL_NONE) return true;
    if (surface->transform) {
        im_transform inv;
        if (!im_transform_invert(surface->transform, &inv)) return true; /* singular transform = no-op */
    }

    const im_palette *wp = window_palette ? window_palette : &surface->palette;
    im_material_region reg;
    im_material_resolve(kind, node, wp, transparency_enabled, host_supports_backdrop, &reg);
    reg.bounds = bounds;
    reg.radii = radii;

    if (list) {
        if (!list_append(list, &reg)) return false;
    }

    uint32_t paint_col = reg.uses_fallback ? reg.fallback_argb : reg.tint_argb;
    im_fill_argb(surface, bounds, radii, paint_col);
    return true;
}
