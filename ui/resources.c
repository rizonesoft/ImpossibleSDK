/* Impossible UI: subtree theme resources, named overrides, in-place restyling, and contrast themes. Portable. */
#include "resources.h"
#include "accent.h"
#include "states.h"
#include <stddef.h>

static int str_cmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static bool valid_scope(im_requested_theme scope) {
    return scope == IM_REQ_THEME_DEFAULT || scope == IM_REQ_THEME_DARK ||
           scope == IM_REQ_THEME_LIGHT || scope == IM_REQ_THEME_CONTRAST;
}

static im_requested_theme theme_to_scope(im_theme t) {
    if (t == IM_LIGHT) return IM_REQ_THEME_LIGHT;
    if (t == IM_CONTRAST) return IM_REQ_THEME_CONTRAST;
    return IM_REQ_THEME_DARK;
}

/* Exact Windows 11 built-in contrast theme system colours (from hcblack, hcwhite, hc1, hc2). */
void im_palette_init_contrast(im_palette *out, im_contrast_theme preset, uint32_t system_accent_argb) {
    if (!out) return;
    im_contrast_colors c;
    switch (preset) {
    case IM_CONTRAST_DESERT: /* hcwhite.theme */
        c.window         = 0xFFFFFAEFu;
        c.window_text    = 0xFF3D3D3Du;
        c.button_face    = 0xFFFFFAEFu;
        c.button_text    = 0xFF202020u;
        c.highlight      = 0xFF903909u;
        c.highlight_text = 0xFFFFF5E3u;
        c.hotlight       = 0xFF1C5E75u;
        c.gray_text      = 0xFF676767u;
        break;
    case IM_CONTRAST_DUSK: /* hc1.theme */
        c.window         = 0xFF2D3236u;
        c.window_text    = 0xFFFFFFFFu;
        c.button_face    = 0xFF2D3236u;
        c.button_text    = 0xFFB6F6F0u;
        c.highlight      = 0xFFA1BFDEu;
        c.highlight_text = 0xFF212D3Bu;
        c.hotlight       = 0xFF70EBDEu;
        c.gray_text      = 0xFFA6A6A6u;
        break;
    case IM_CONTRAST_NIGHT_SKY: /* hc2.theme */
        c.window         = 0xFF000000u;
        c.window_text    = 0xFFFFFFFFu;
        c.button_face    = 0xFF000000u;
        c.button_text    = 0xFFFFEE32u;
        c.highlight      = 0xFFD6B4FDu;
        c.highlight_text = 0xFF2B2B2Bu;
        c.hotlight       = 0xFF8080FFu;
        c.gray_text      = 0xFFA6A6A6u;
        break;
    case IM_CONTRAST_AQUATIC: /* hcblack.theme */
    default:
        c.window         = 0xFF202020u;
        c.window_text    = 0xFFFFFFFFu;
        c.button_face    = 0xFF202020u;
        c.button_text    = 0xFFFFFFFFu;
        c.highlight      = 0xFF8EE3F0u;
        c.highlight_text = 0xFF263B50u;
        c.hotlight       = 0xFF75E9FCu;
        c.gray_text      = 0xFFA6A6A6u;
        break;
    }
    c.control_accent = 0xFF000000u | (system_accent_argb & 0x00FFFFFFu);
    im_palette_init_contrast_custom(out, &c);
}

void im_palette_init_contrast_custom(im_palette *out, const im_contrast_colors *colors) {
    if (!out || !colors) return;
    out->theme = IM_CONTRAST;
    im_accent_fill_palette(out, colors->control_accent);
    out->slot[IM_SLOT_COLOR_WINDOW_TEXT]    = colors->window_text;
    out->slot[IM_SLOT_COLOR_WINDOW]         = colors->window;
    out->slot[IM_SLOT_COLOR_BUTTON_FACE]    = colors->button_face;
    out->slot[IM_SLOT_COLOR_BUTTON_TEXT]    = colors->button_text;
    out->slot[IM_SLOT_COLOR_HIGHLIGHT]      = colors->highlight;
    out->slot[IM_SLOT_COLOR_HIGHLIGHT_TEXT] = colors->highlight_text;
    out->slot[IM_SLOT_COLOR_HOTLIGHT]       = colors->hotlight;
    out->slot[IM_SLOT_COLOR_GRAY_TEXT]      = colors->gray_text;
    /* Pinned FrameworkTheming.cpp:221-236: DisabledText uses GrayText and ControlAccent uses system accent. */
    out->slot[IM_SLOT_COLOR_DISABLED_TEXT]  = colors->gray_text;
    out->slot[IM_SLOT_COLOR_CONTROL_ACCENT] = 0xFF000000u | (colors->control_accent & 0x00FFFFFFu);
}

bool im_brush_id_from_name(const char *name, enum im_brush_id *out_id) {
    if (!name || !name[0]) return false;
    uint32_t lo = 0u, hi = (uint32_t)IM_BRUSH_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        int cmp = str_cmp(name, im_brush_names[mid]);
        if (cmp == 0) {
            if (out_id) *out_id = (enum im_brush_id)mid;
            return true;
        }
        if (cmp < 0) hi = mid;
        else lo = mid + 1u;
    }
    return false;
}

bool im_value_id_from_name(const char *name, enum im_value_id *out_id) {
    if (!name || !name[0]) return false;
    uint32_t lo = 0u, hi = (uint32_t)IM_VALUE_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        int cmp = str_cmp(name, im_value_names[mid]);
        if (cmp == 0) {
            if (out_id) *out_id = (enum im_value_id)mid;
            return true;
        }
        if (cmp < 0) hi = mid;
        else lo = mid + 1u;
    }
    return false;
}

im_theme im_resources_effective_theme(const im_node *node, const im_palette *window_palette) {
    im_theme base = window_palette ? window_palette->theme : IM_DARK;
    /* High Contrast overrides Light/Dark subtree requests while leaving node->requested_theme intact. */
    if (base == IM_CONTRAST) return IM_CONTRAST;
    for (const im_node *cur = node; cur; cur = cur->parent) {
        if (cur->requested_theme == IM_REQ_THEME_LIGHT) return IM_LIGHT;
        if (cur->requested_theme == IM_REQ_THEME_DARK) return IM_DARK;
        if (cur->requested_theme == IM_REQ_THEME_CONTRAST) return IM_CONTRAST;
    }
    return base;
}

im_palette im_resources_effective_palette(const im_node *node, const im_palette *window_palette) {
    im_palette p;
    if (window_palette) {
        p = *window_palette;
    } else {
        for (size_t i = 0; i < sizeof(p); i++) ((uint8_t *)&p)[i] = 0u;
        p.theme = IM_DARK;
    }
    p.theme = im_resources_effective_theme(node, window_palette);
    /* Honour nearest ancestor per-subtree accent override (unless overridden by High Contrast). */
    if (p.theme != IM_CONTRAST) {
        for (const im_node *cur = node; cur; cur = cur->parent) {
            if (cur->has_subtree_accent) {
                im_accent_fill_palette(&p, cur->subtree_accent_argb);
                break;
            }
        }
    }
    return p;
}

static const im_resource_override *find_override_on_node(const im_node *node, uint8_t kind,
                                                         uint16_t id, im_requested_theme eff_scope) {
    const im_resource_override *any_hit = NULL;
    for (uint32_t i = 0; i < node->res_override_count; i++) {
        const im_resource_override *e = &node->res_overrides[i];
        if (e->kind != kind || e->id != id) continue;
        if ((im_requested_theme)e->scope == eff_scope) return e; /* theme-specific beats any-theme */
        if ((im_requested_theme)e->scope == IM_REQ_THEME_DEFAULT) any_hit = e;
    }
    return any_hit;
}

bool im_resources_lookup_brush_override(const im_node *node, const im_palette *window_palette,
                                        enum im_brush_id id, uint32_t *out_argb) {
    if (!node || id < 0 || id >= IM_BRUSH_COUNT) return false;
    im_requested_theme eff = theme_to_scope(im_resources_effective_theme(node, window_palette));
    for (const im_node *cur = node; cur; cur = cur->parent) {
        const im_resource_override *hit = find_override_on_node(cur, 1u, (uint16_t)id, eff);
        if (hit) {
            if (out_argb) *out_argb = hit->argb;
            return true;
        }
    }
    return false;
}

bool im_resources_lookup_value_override(const im_node *node, const im_palette *window_palette,
                                        enum im_value_id id, im_fixed out[4]) {
    if (!node || id < 0 || id >= IM_VALUE_COUNT) return false;
    im_requested_theme eff = theme_to_scope(im_resources_effective_theme(node, window_palette));
    for (const im_node *cur = node; cur; cur = cur->parent) {
        const im_resource_override *hit = find_override_on_node(cur, 2u, (uint16_t)id, eff);
        if (hit) {
            if (out) {
                for (int k = 0; k < 4; k++) out[k] = hit->val[k];
            }
            return true;
        }
    }
    return false;
}

uint32_t im_resources_resolve_brush(const im_node *node, const im_palette *window_palette,
                                    enum im_brush_id id, int64_t y, int64_t height, im_fixed scale) {
    if (id < 0 || id >= IM_BRUSH_COUNT) return 0u;
    uint32_t ov = 0u;
    if (node && im_resources_lookup_brush_override(node, window_palette, id, &ov)) {
        return ov;
    }
    im_palette eff = im_resources_effective_palette(node, window_palette);
    return im_brush_over(&eff, id, y, height, scale);
}

void im_resources_resolve_value(const im_node *node, const im_palette *window_palette,
                                enum im_value_id id, im_fixed out[4]) {
    if (!out) return;
    if (id < 0 || id >= IM_VALUE_COUNT) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    if (node && im_resources_lookup_value_override(node, window_palette, id, out)) {
        return;
    }
    im_palette eff = im_resources_effective_palette(node, window_palette);
    const im_fixed *v = im_value(&eff, id);
    for (int k = 0; k < 4; k++) out[k] = v[k];
}

static bool set_override_raw(im_tree *tree, im_node *node, uint8_t kind,
                             im_requested_theme scope, uint16_t id,
                             uint32_t argb, const im_fixed val[4]) {
    if (!tree || !node || !valid_scope(scope)) return false;
    for (uint32_t i = 0; i < node->res_override_count; i++) {
        im_resource_override *e = &node->res_overrides[i];
        if (e->kind == kind && e->id == id && (im_requested_theme)e->scope == scope) {
            bool same = (kind == 1u)
                ? (e->argb == argb)
                : (e->val[0] == val[0] && e->val[1] == val[1] && e->val[2] == val[2] && e->val[3] == val[3]);
            if (same) return true;
            e->argb = argb;
            if (val) {
                for (int k = 0; k < 4; k++) e->val[k] = val[k];
            }
            return true;
        }
    }
    if (node->res_override_count == node->res_override_cap) {
        uint32_t new_cap = node->res_override_cap ? node->res_override_cap * 2u : 4u;
        if (new_cap < node->res_override_cap) return false;
        size_t old_bytes = (size_t)node->res_override_cap * sizeof(im_resource_override);
        size_t new_bytes = (size_t)new_cap * sizeof(im_resource_override);
        if (new_bytes / sizeof(im_resource_override) != (size_t)new_cap) return false;
        im_resource_override *grown = (im_resource_override *)tree->alloc.realloc(
            tree->alloc.ctx, node->res_overrides, old_bytes, new_bytes);
        if (!grown) return false; /* atomic: existing entries untouched */
        node->res_overrides = grown;
        node->res_override_cap = new_cap;
    }
    im_resource_override *slot = &node->res_overrides[node->res_override_count++];
    slot->kind = kind;
    slot->scope = (uint8_t)scope;
    slot->id = id;
    slot->argb = argb;
    for (int k = 0; k < 4; k++) slot->val[k] = val ? val[k] : 0;
    return true;
}

static bool clear_override_raw(im_node *node, uint8_t kind, im_requested_theme scope, uint16_t id) {
    if (!node || !valid_scope(scope)) return false;
    for (uint32_t i = 0; i < node->res_override_count; i++) {
        im_resource_override *e = &node->res_overrides[i];
        if (e->kind == kind && e->id == id && (im_requested_theme)e->scope == scope) {
            for (uint32_t j = i + 1u; j < node->res_override_count; j++) {
                node->res_overrides[j - 1u] = node->res_overrides[j];
            }
            node->res_override_count--;
            return true;
        }
    }
    return false;
}

bool im_resources_set_brush_override(im_tree *tree, im_node *node, im_requested_theme scope,
                                     enum im_brush_id id, uint32_t argb) {
    if (id < 0 || id >= IM_BRUSH_COUNT) return false;
    return set_override_raw(tree, node, 1u, scope, (uint16_t)id, argb, NULL);
}

bool im_resources_set_brush_override_by_name(im_tree *tree, im_node *node, im_requested_theme scope,
                                             const char *name, uint32_t argb) {
    enum im_brush_id id;
    if (!im_brush_id_from_name(name, &id)) return false;
    return im_resources_set_brush_override(tree, node, scope, id, argb);
}

bool im_resources_clear_brush_override(im_node *node, im_requested_theme scope, enum im_brush_id id) {
    if (id < 0 || id >= IM_BRUSH_COUNT) return false;
    return clear_override_raw(node, 1u, scope, (uint16_t)id);
}

bool im_resources_clear_brush_override_by_name(im_node *node, im_requested_theme scope, const char *name) {
    enum im_brush_id id;
    if (!im_brush_id_from_name(name, &id)) return false;
    return im_resources_clear_brush_override(node, scope, id);
}

bool im_resources_set_value_override(im_tree *tree, im_node *node, im_requested_theme scope,
                                     enum im_value_id id, const im_fixed val[4]) {
    if (id < 0 || id >= IM_VALUE_COUNT || !val) return false;
    return set_override_raw(tree, node, 2u, scope, (uint16_t)id, 0u, val);
}

bool im_resources_set_value_override_by_name(im_tree *tree, im_node *node, im_requested_theme scope,
                                             const char *name, const im_fixed val[4]) {
    enum im_value_id id;
    if (!im_value_id_from_name(name, &id)) return false;
    return im_resources_set_value_override(tree, node, scope, id, val);
}

bool im_resources_clear_value_override(im_node *node, im_requested_theme scope, enum im_value_id id) {
    if (id < 0 || id >= IM_VALUE_COUNT) return false;
    return clear_override_raw(node, 2u, scope, (uint16_t)id);
}

bool im_resources_clear_value_override_by_name(im_node *node, im_requested_theme scope, const char *name) {
    enum im_value_id id;
    if (!im_value_id_from_name(name, &id)) return false;
    return im_resources_clear_value_override(node, scope, id);
}

static uint32_t hash_palette(const im_palette *p) {
    uint32_t h = 2166136261u;
    h ^= (uint32_t)p->theme;
    h *= 16777619u;
    for (int i = 0; i < IM_SLOT_COUNT; i++) {
        h ^= p->slot[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t restyle_recursive(im_tree *tree, im_node *node, const im_palette *window_palette, uint32_t now_ms) {
    if (!node) return 0u;
    uint32_t updated = 0u;
    im_palette eff = im_resources_effective_palette(node, window_palette);
    uint32_t ph = hash_palette(&eff);
    bool paint_changed = false;
    bool layout_changed = false;
    bool has_explicit_bindings = node->has_theme_bg_brush || node->has_theme_border_brush ||
        node->has_theme_padding_val || node->has_theme_border_val ||
        node->bound_sm != NULL || node->kind == IM_ELEM_BUTTON;

    if (!node->has_resolved_theme || node->resolved_theme != eff.theme || node->resolved_palette_hash != ph) {
        /* Fallback nodes without explicit bindings dirty on theme-mode changes (Dark/Light/Contrast),
           never on unrelated accent-slot changes; bound nodes dirty strictly when consumed brushes/values change. */
        if (node->has_resolved_theme && !has_explicit_bindings && node->resolved_theme != eff.theme) {
            paint_changed = true;
        }
        node->resolved_theme = eff.theme;
        node->resolved_palette_hash = ph;
        node->has_resolved_theme = true;
    }

    if (node->has_theme_bg_brush) {
        uint32_t bg0 = im_resources_resolve_brush(node, window_palette, node->theme_bg_brush, 0, IM_ONE, IM_ONE);
        uint32_t bg1 = im_resources_resolve_brush(node, window_palette, node->theme_bg_brush, IM_ONE, IM_ONE, IM_ONE);
        if (bg0 != node->resolved_bg_argb || bg1 != node->resolved_bg_argb_end) {
            node->resolved_bg_argb = bg0;
            node->resolved_bg_argb_end = bg1;
            paint_changed = true;
        }
    }
    if (node->has_theme_border_brush) {
        uint32_t bd0 = im_resources_resolve_brush(node, window_palette, node->theme_border_brush, 0, IM_ONE, IM_ONE);
        uint32_t bd1 = im_resources_resolve_brush(node, window_palette, node->theme_border_brush, IM_ONE, IM_ONE, IM_ONE);
        if (bd0 != node->resolved_border_argb || bd1 != node->resolved_border_argb_end) {
            node->resolved_border_argb = bd0;
            node->resolved_border_argb_end = bd1;
            paint_changed = true;
        }
    }
    if (node->kind == IM_ELEM_BUTTON) {
        im_fixed cr[4], bt[4];
        im_resources_resolve_value(node, window_palette, IM_VALUE_ControlCornerRadius, cr);
        im_resources_resolve_value(node, window_palette, IM_VALUE_ButtonBorderThemeThickness, bt);
        uint32_t gh = 2166136261u;
        for (int k = 0; k < 4; k++) { gh ^= (uint32_t)cr[k]; gh *= 16777619u; gh ^= (uint32_t)bt[k]; gh *= 16777619u; }
        if (node->resolved_button_geom_hash != 0u && node->resolved_button_geom_hash != gh) {
            paint_changed = true;
        }
        node->resolved_button_geom_hash = gh ? gh : 1u;
    }
    if (node->has_theme_padding_val) {
        im_fixed v[4];
        im_resources_resolve_value(node, window_palette, node->theme_padding_val, v);
        im_thickness np = { v[0], v[1], v[2], v[3] };
        node->theme_padding_cache = np;
        node->default_padding = np;
        if (!(node->local_mask & IM_PBIT_PADDING) &&
            (np.left != node->padding.left || np.top != node->padding.top ||
             np.right != node->padding.right || np.bottom != node->padding.bottom)) {
            node->padding = np;
            im_tree_invalidate_measure(node);
            layout_changed = true;
            paint_changed = true;
        }
    }
    if (node->has_theme_border_val) {
        im_fixed v[4];
        im_resources_resolve_value(node, window_palette, node->theme_border_val, v);
        im_thickness nb = { v[0], v[1], v[2], v[3] };
        node->theme_border_cache = nb;
        node->default_border_thickness = nb;
        if (!(node->local_mask & IM_PBIT_BORDER_THICKNESS) &&
            (nb.left != node->border_thickness.left || nb.top != node->border_thickness.top ||
             nb.right != node->border_thickness.right || nb.bottom != node->border_thickness.bottom)) {
            node->border_thickness = nb;
            im_tree_invalidate_measure(node);
            layout_changed = true;
            paint_changed = true;
        }
    }
    if (node->bound_sm) {
        im_state_machine *sm = (im_state_machine *)node->bound_sm;
        im_states_set_resource_context(sm, node, window_palette);
        if (im_states_refresh_theme(sm, window_palette, now_ms)) {
            paint_changed = true;
        }
        im_states_apply_to_tree(sm, node, now_ms);
    }

    if (paint_changed || layout_changed) {
        if (paint_changed) im_tree_invalidate_paint(node);
        updated++;
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        updated += restyle_recursive(tree, node->children[i], window_palette, now_ms);
    }
    return updated;
}

uint32_t im_resources_restyle_tree(im_tree *tree, im_node *root, const im_palette *window_palette, uint32_t now_ms) {
    if (!root) return 0u;
    return restyle_recursive(tree, root, window_palette, now_ms);
}
