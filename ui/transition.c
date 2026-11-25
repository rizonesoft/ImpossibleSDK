/* Impossible UI: WinUI PVL theme transitions and exit-visual ghost runner. Portable: no float, no libc. */
#include "transition.h"
#include <stddef.h>

#define FX(n) ((im_fixed)((int64_t)(n) * IM_ONE))
/* Standard WinUI PVL scale constants in 16.16 fixed point. */
#define SCALE_ADD_DELETE ((im_fixed)55706) /* 0.85 */
#define SCALE_DRILL_IN   ((im_fixed)60293) /* 0.92 */
#define SCALE_DRILL_OUT  ((im_fixed)70779) /* 1.08 */

static const im_transition_spec k_specs[IM_TRANS_KIND_COUNT] = {
    [IM_TRANS_ENTRANCE] = {
        IM_TRANS_ENTRANCE, "EntranceThemeTransition", 367u, 167u, 33u, 6u,
        FX(100), 0, IM_ONE, IM_ONE, 0, IM_ONE, 0, 0,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_POPUP_OPEN] = {
        IM_TRANS_POPUP_OPEN, "PopupThemeTransition", 167u, 83u, 0u, 0u,
        0, FX(50), IM_ONE, IM_ONE, 0, IM_ONE, 0, 0,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_POPUP_CLOSE] = {
        IM_TRANS_POPUP_CLOSE, "PopupThemeTransition", 83u, 83u, 0u, 0u,
        0, 0, IM_ONE, IM_ONE, IM_ONE, 0, 0, 0,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_ADD] = {
        IM_TRANS_ADD, "AddDeleteThemeTransition", 167u, 167u, 33u, 6u,
        0, 0, SCALE_ADD_DELETE, IM_ONE, 0, IM_ONE, IM_ONE / 2, IM_ONE / 2,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_DELETE] = {
        IM_TRANS_DELETE, "AddDeleteThemeTransition", 167u, 167u, 0u, 0u,
        0, 0, IM_ONE, SCALE_ADD_DELETE, IM_ONE, 0, IM_ONE / 2, IM_ONE / 2,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_REPOSITION] = {
        IM_TRANS_REPOSITION, "RepositionThemeTransition", 367u, 0u, 20u, 6u,
        0, 0, IM_ONE, IM_ONE, IM_ONE, IM_ONE, 0, 0,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_REORDER] = {
        IM_TRANS_REORDER, "ReorderThemeTransition", 200u, 0u, 0u, 0u,
        0, 0, IM_ONE, IM_ONE, IM_ONE, IM_ONE, 0, 0,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_CONTENT] = {
        IM_TRANS_CONTENT, "ContentThemeTransition", 367u, 167u, 0u, 0u,
        FX(40), 0, IM_ONE, IM_ONE, 0, IM_ONE, 0, 0,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_DRILL_IN] = {
        IM_TRANS_DRILL_IN, "NavigationThemeTransition", 367u, 167u, 0u, 0u,
        0, 0, SCALE_DRILL_IN, IM_ONE, 0, IM_ONE, IM_ONE / 2, IM_ONE / 2,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
    [IM_TRANS_DRILL_OUT] = {
        IM_TRANS_DRILL_OUT, "NavigationThemeTransition", 367u, 167u, 0u, 0u,
        0, 0, SCALE_DRILL_OUT, IM_ONE, 0, IM_ONE, IM_ONE / 2, IM_ONE / 2,
        IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2
    },
};

static bool str_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

const im_transition_spec *im_transition_spec_by_kind(im_transition_kind kind) {
    if (kind < 0 || kind >= IM_TRANS_KIND_COUNT) return NULL;
    return &k_specs[kind];
}

const im_transition_spec *im_transition_spec_by_name(const char *xaml_name) {
    if (!xaml_name) return NULL;
    for (int i = 0; i < IM_TRANS_KIND_COUNT; i++) {
        if (str_eq(k_specs[i].xaml_name, xaml_name)) return &k_specs[i];
    }
    return NULL;
}

void im_transition_start_offset(im_transition *tr, im_transition_kind kind,
                                im_fixed from_x, im_fixed from_y, im_fixed to_x, im_fixed to_y,
                                uint32_t now_ms, uint32_t stagger_index, bool reduced_motion) {
    if (!tr) return;
    const im_transition_spec *sp = im_transition_spec_by_kind(kind);
    if (!sp) {
        tr->active = false;
        return;
    }
    uint32_t capped = (sp->stagger_max_items > 0u && stagger_index > sp->stagger_max_items)
        ? sp->stagger_max_items : stagger_index;
    tr->kind = kind;
    tr->reduced_motion = reduced_motion;
    tr->start_ms = now_ms;
    tr->delay_ms = reduced_motion ? 0u : (capped * sp->stagger_delay_ms);
    tr->move_duration_ms = sp->move_duration_ms;
    tr->opacity_duration_ms = sp->opacity_duration_ms;
    tr->from_x = from_x;
    tr->to_x = to_x;
    tr->from_y = from_y;
    tr->to_y = to_y;
    tr->from_scale = sp->from_scale;
    tr->to_scale = sp->to_scale;
    tr->from_opacity = sp->from_opacity;
    tr->to_opacity = sp->to_opacity;
    tr->origin_x = sp->origin_x;
    tr->origin_y = sp->origin_y;
    tr->spline_x1 = sp->spline_x1;
    tr->spline_y1 = sp->spline_y1;
    tr->spline_x2 = sp->spline_x2;
    tr->spline_y2 = sp->spline_y2;
    tr->active = !reduced_motion && (tr->move_duration_ms > 0u || tr->opacity_duration_ms > 0u);
}

void im_transition_start(im_transition *tr, im_transition_kind kind, uint32_t now_ms,
                         uint32_t stagger_index, bool reduced_motion) {
    const im_transition_spec *sp = im_transition_spec_by_kind(kind);
    if (!sp) {
        if (tr) tr->active = false;
        return;
    }
    im_transition_start_offset(tr, kind, sp->default_from_x, sp->default_from_y, 0, 0,
                               now_ms, stagger_index, reduced_motion);
}

static uint32_t total_duration_ms(const im_transition *tr) {
    uint32_t m = tr->move_duration_ms;
    uint32_t o = tr->opacity_duration_ms;
    return tr->delay_ms + (m > o ? m : o);
}

bool im_transition_is_active(const im_transition *tr, uint32_t now_ms) {
    if (!tr || !tr->active || tr->reduced_motion) return false;
    uint32_t total = total_duration_ms(tr);
    if (total == 0u) return false;
    return im_anim_elapsed_ms(tr->start_ms, now_ms) < total;
}

im_transition_sample im_transition_sample_at(const im_transition *tr, uint32_t now_ms) {
    im_transition_sample out = { 0, 0, IM_ONE, IM_ONE, 0, 0, false };
    if (!tr) return out;
    out.origin_x = tr->origin_x;
    out.origin_y = tr->origin_y;
    if (!tr->active || tr->reduced_motion) {
        out.translate_x = tr->to_x;
        out.translate_y = tr->to_y;
        out.scale = tr->to_scale;
        out.opacity = tr->to_opacity;
        out.active = false;
        return out;
    }
    uint32_t el = im_anim_elapsed_ms(tr->start_ms, now_ms);
    if (el < tr->delay_ms) {
        out.translate_x = tr->from_x;
        out.translate_y = tr->from_y;
        out.scale = tr->from_scale;
        out.opacity = tr->from_opacity;
        out.active = true;
        return out;
    }
    uint32_t t = el - tr->delay_ms;
    if (tr->move_duration_ms == 0u || t >= tr->move_duration_ms) {
        out.translate_x = tr->to_x;
        out.translate_y = tr->to_y;
        out.scale = tr->to_scale;
    } else {
        im_fixed p = im_spline_eval_ratio(tr->spline_x1, tr->spline_y1, tr->spline_x2, tr->spline_y2, t, tr->move_duration_ms);
        out.translate_x = im_lerp_fixed(tr->from_x, tr->to_x, p);
        out.translate_y = im_lerp_fixed(tr->from_y, tr->to_y, p);
        out.scale = im_lerp_fixed(tr->from_scale, tr->to_scale, p);
    }
    if (tr->opacity_duration_ms == 0u || t >= tr->opacity_duration_ms) {
        out.opacity = tr->to_opacity;
    } else {
        out.opacity = im_lerp_fixed_ratio(tr->from_opacity, tr->to_opacity, t, tr->opacity_duration_ms);
    }
    out.active = (el < total_duration_ms(tr));
    return out;
}

void im_transition_retarget(im_transition *tr, im_fixed new_to_x, im_fixed new_to_y,
                            im_fixed new_to_scale, im_fixed new_to_opacity, uint32_t now_ms) {
    if (!tr) return;
    im_transition_sample cur = im_transition_sample_at(tr, now_ms);
    tr->from_x = cur.translate_x;
    tr->from_y = cur.translate_y;
    tr->from_scale = cur.scale;
    tr->from_opacity = cur.opacity;
    tr->to_x = new_to_x;
    tr->to_y = new_to_y;
    tr->to_scale = new_to_scale;
    tr->to_opacity = new_to_opacity;
    tr->start_ms = now_ms;
    tr->delay_ms = 0u;
    if (tr->reduced_motion) {
        tr->active = false;
    } else {
        bool any_diff = (tr->from_x != tr->to_x) || (tr->from_y != tr->to_y) ||
                        (tr->from_scale != tr->to_scale) || (tr->from_opacity != tr->to_opacity);
        tr->active = any_diff && (tr->move_duration_ms > 0u || tr->opacity_duration_ms > 0u);
    }
}

void im_transition_reverse(im_transition *tr, uint32_t now_ms) {
    if (!tr) return;
    im_fixed orig_x = tr->from_x;
    im_fixed orig_y = tr->from_y;
    im_fixed orig_scale = tr->from_scale;
    im_fixed orig_opacity = tr->from_opacity;
    im_transition_retarget(tr, orig_x, orig_y, orig_scale, orig_opacity, now_ms);
}

bool im_transition_set_reduced_motion(im_transition *tr, bool reduced_motion, uint32_t now_ms) {
    if (!tr) return false;
    bool was_active = im_transition_is_active(tr, now_ms);
    tr->reduced_motion = reduced_motion;
    if (reduced_motion && was_active) {
        tr->active = false;
        return true; /* caller should emit final settled repaint */
    }
    /* Toggling reduced_motion back off never resurrects an already settled transition. */
    return false;
}

bool im_transition_apply_to_node(im_transition *tr, im_node *node, uint32_t now_ms) {
    if (!tr || !node) return false;
    im_transition_sample s = im_transition_sample_at(tr, now_ms);
    if (!s.active) {
        tr->active = false;
        im_node_clear_transition_presentation(node);
        return false;
    }
    im_transform ttr;
    if (!im_transform_composite(0, 0, s.scale, s.scale, 0, 0, 0, s.translate_x, s.translate_y, &ttr)) {
        /* Checked composition failed: do not leave half-applied presentation state. */
        tr->active = false;
        im_node_clear_transition_presentation(node);
        return false;
    }
    bool has_tr = (s.translate_x != 0 || s.translate_y != 0 || s.scale != IM_ONE);
    im_node_set_transition_presentation(node, has_tr, &ttr, s.origin_x, s.origin_y, s.opacity);
    return true;
}

static const im_transition_decl *find_decl(const im_node *owner, im_element_kind kind) {
    if (!owner) return NULL;
    for (uint32_t i = 0; i < owner->transition_decl_count; i++) {
        if (owner->transition_decls[i].kind == kind) return &owner->transition_decls[i];
    }
    return NULL;
}

bool im_transition_on_enter(const im_node *owner, im_transition *tr, uint32_t now_ms,
                            uint32_t stagger_index, bool reduced_motion) {
    const im_transition_decl *d = find_decl(owner, IM_ELEM_ENTRANCE_THEME_TRANSITION);
    if (!d) return false;
    const im_transition_spec *sp = &k_specs[IM_TRANS_ENTRANCE];
    im_fixed fx = d->has_from_h ? d->from_h : sp->default_from_x;
    im_fixed fy = d->has_from_v ? d->from_v : sp->default_from_y;
    im_transition_start_offset(tr, IM_TRANS_ENTRANCE, fx, fy, 0, 0, now_ms,
                               d->is_staggering ? stagger_index : 0u, reduced_motion);
    return true;
}

bool im_transition_on_add(const im_node *owner, im_transition *tr, uint32_t now_ms,
                          uint32_t stagger_index, bool reduced_motion) {
    const im_transition_decl *d = find_decl(owner, IM_ELEM_ADD_DELETE_THEME_TRANSITION);
    if (!d) return false;
    im_transition_start(tr, IM_TRANS_ADD, now_ms, stagger_index, reduced_motion);
    return true;
}

bool im_transition_on_reposition(const im_node *owner, im_transition *tr, im_rect old_rect, im_rect new_rect,
                                 uint32_t now_ms, uint32_t stagger_index, bool reduced_motion) {
    const im_transition_decl *d = find_decl(owner, IM_ELEM_REPOSITION_THEME_TRANSITION);
    if (!d) d = find_decl(owner, IM_ELEM_ADD_DELETE_THEME_TRANSITION);
    if (!d) return false;
    im_fixed dx = old_rect.left - new_rect.left;
    im_fixed dy = old_rect.top - new_rect.top;
    if (dx == 0 && dy == 0) return false;
    im_transition_start_offset(tr, IM_TRANS_REPOSITION, dx, dy, 0, 0, now_ms,
                               d->is_staggering ? stagger_index : 0u, reduced_motion);
    return true;
}

bool im_transition_on_reorder(const im_node *owner, im_transition *tr, im_rect old_rect, im_rect new_rect,
                              uint32_t now_ms, bool reduced_motion) {
    if (!find_decl(owner, IM_ELEM_REORDER_THEME_TRANSITION)) return false;
    im_fixed dx = old_rect.left - new_rect.left;
    im_fixed dy = old_rect.top - new_rect.top;
    if (dx == 0 && dy == 0) return false;
    im_transition_start_offset(tr, IM_TRANS_REORDER, dx, dy, 0, 0, now_ms, 0u, reduced_motion);
    return true;
}

bool im_transition_on_popup_open(const im_node *owner, im_transition *tr, uint32_t now_ms, bool reduced_motion) {
    const im_transition_decl *d = find_decl(owner, IM_ELEM_POPUP_THEME_TRANSITION);
    if (!d) return false;
    const im_transition_spec *sp = &k_specs[IM_TRANS_POPUP_OPEN];
    im_fixed fx = d->has_from_h ? d->from_h : sp->default_from_x;
    im_fixed fy = d->has_from_v ? d->from_v : sp->default_from_y;
    im_transition_start_offset(tr, IM_TRANS_POPUP_OPEN, fx, fy, 0, 0, now_ms, 0u, reduced_motion);
    return true;
}

bool im_transition_on_content_change(const im_node *owner, im_transition *tr, uint32_t now_ms, bool reduced_motion) {
    const im_transition_decl *d = find_decl(owner, IM_ELEM_CONTENT_THEME_TRANSITION);
    if (!d) return false;
    const im_transition_spec *sp = &k_specs[IM_TRANS_CONTENT];
    im_fixed fx = d->has_from_h ? d->from_h : sp->default_from_x;
    im_fixed fy = d->has_from_v ? d->from_v : sp->default_from_y;
    im_transition_start_offset(tr, IM_TRANS_CONTENT, fx, fy, 0, 0, now_ms, 0u, reduced_motion);
    return true;
}

bool im_transition_on_navigate(const im_node *owner, im_transition *tr, bool is_back,
                               uint32_t now_ms, bool reduced_motion) {
    const im_transition_decl *d = find_decl(owner, IM_ELEM_NAVIGATION_THEME_TRANSITION);
    if (!d) return false;
    if (d->nav_info == IM_ELEM_SUPPRESS_NAV_INFO) {
        if (tr) tr->active = false;
        return false;
    }
    im_transition_start(tr, is_back ? IM_TRANS_DRILL_OUT : IM_TRANS_DRILL_IN, now_ms, 0u, reduced_motion);
    return true;
}

bool im_transition_on_delete(const im_node *owner, im_transition_ghosts *ghosts, const im_node *outgoing,
                             uint32_t tag_id, uint32_t fill_argb, uint32_t now_ms, bool reduced_motion) {
    if (!ghosts || !outgoing || !find_decl(owner, IM_ELEM_ADD_DELETE_THEME_TRANSITION)) return false;
    if (reduced_motion) return false;
    im_transition_ghost *slot = NULL;
    for (int i = 0; i < IM_MAX_EXIT_GHOSTS; i++) {
        if (ghosts->items[i].active && ghosts->items[i].tag_id == tag_id) {
            slot = &ghosts->items[i];
            break;
        }
        if (!ghosts->items[i].active && !slot) slot = &ghosts->items[i];
    }
    if (!slot) return false;
    slot->active = true;
    slot->tag_id = tag_id;
    slot->base_bounds = outgoing->absolute_rect;
    slot->fill_argb = fill_argb;
    slot->authored_opacity = outgoing->opacity;
    slot->had_prev_dev_rect = false;
    im_transition_start(&slot->transition, IM_TRANS_DELETE, now_ms, 0u, false);
    return true;
}

static void ghost_dev_rect(const im_transition_ghost *g, im_fixed scale, int32_t out[4]) {
    int64_t l = ((int64_t)g->base_bounds.left * (int64_t)scale) >> 16;
    int64_t t = ((int64_t)g->base_bounds.top * (int64_t)scale) >> 16;
    int64_t r = ((int64_t)g->base_bounds.right * (int64_t)scale + IM_ONE - 1) >> 16;
    int64_t b = ((int64_t)g->base_bounds.bottom * (int64_t)scale + IM_ONE - 1) >> 16;
    out[0] = (int32_t)(l >> 16);
    out[1] = (int32_t)(t >> 16);
    out[2] = (int32_t)(r >> 16);
    out[3] = (int32_t)(b >> 16);
}

bool im_transition_ghosts_step(im_transition_ghosts *ghosts, im_dirty_list *dl, im_fixed scale, uint32_t now_ms) {
    if (!ghosts) return false;
    if (scale <= 0) scale = IM_ONE;
    bool any = false;
    for (int i = 0; i < IM_MAX_EXIT_GHOSTS; i++) {
        im_transition_ghost *g = &ghosts->items[i];
        if (!g->active) continue;
        im_transition_sample s = im_transition_sample_at(&g->transition, now_ms);
        if (dl && g->had_prev_dev_rect) {
            im_dirty_add(dl, (im_irect){ g->prev_dev_left, g->prev_dev_top, g->prev_dev_right, g->prev_dev_bottom });
        }
        if (!s.active) {
            g->active = false;
            g->had_prev_dev_rect = false;
            continue;
        }
        int32_t dr[4];
        ghost_dev_rect(g, scale, dr);
        if (dl) im_dirty_add(dl, (im_irect){ dr[0], dr[1], dr[2], dr[3] });
        g->prev_dev_left = dr[0]; g->prev_dev_top = dr[1];
        g->prev_dev_right = dr[2]; g->prev_dev_bottom = dr[3];
        g->had_prev_dev_rect = true;
        any = true;
    }
    return any;
}

bool im_transition_ghosts_cancel(im_transition_ghosts *ghosts, uint32_t tag_id, im_dirty_list *dl) {
    if (!ghosts) return false;
    for (int i = 0; i < IM_MAX_EXIT_GHOSTS; i++) {
        im_transition_ghost *g = &ghosts->items[i];
        if (!g->active || g->tag_id != tag_id) continue;
        if (dl && g->had_prev_dev_rect) {
            im_dirty_add(dl, (im_irect){ g->prev_dev_left, g->prev_dev_top, g->prev_dev_right, g->prev_dev_bottom });
        }
        g->active = false;
        g->had_prev_dev_rect = false;
        return true;
    }
    return false;
}
