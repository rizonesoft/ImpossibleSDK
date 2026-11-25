/* Impossible UI: compiled WinUI visual state groups, transitions and setters. Portable. */
#include "states.h"
#include "resources.h"
#include "button.h"
#include "input.h"
#include "layer.h"
#include "path.h"

static bool str_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool str_ieq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (ascii_lower(*a) != ascii_lower(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static bool target_matches(const char *entry_target, const char *query_target) {
    if (str_eq(entry_target, query_target)) return true;
    if (!entry_target || !query_target) return false;
    size_t i = 0;
    while (query_target[i] != '\0') {
        if (entry_target[i] != query_target[i]) return false;
        i++;
    }
    return entry_target[i] == '.';
}

static bool prop_matches(const char *entry_prop, const char *query_prop) {
    if (str_eq(entry_prop, query_prop)) return true;
    if (!entry_prop || !query_prop) return false;
    if ((str_eq(entry_prop, "(Shape.Stroke).(SolidColorBrush.Color)") && str_eq(query_prop, "Stroke")) ||
        (str_eq(query_prop, "(Shape.Stroke).(SolidColorBrush.Color)") && str_eq(entry_prop, "Stroke"))) {
        return true;
    }
    if ((str_eq(entry_prop, "(Shape.Fill).(SolidColorBrush.Color)") && str_eq(query_prop, "Fill")) ||
        (str_eq(query_prop, "(Shape.Fill).(SolidColorBrush.Color)") && str_eq(entry_prop, "Fill"))) {
        return true;
    }
    if ((str_eq(entry_prop, "(Panel.Background).(SolidColorBrush.Color)") && str_eq(query_prop, "Background")) ||
        (str_eq(query_prop, "(Panel.Background).(SolidColorBrush.Color)") && str_eq(entry_prop, "Background"))) {
        return true;
    }
    return false;
}

static bool resolve_theme_resource_fallback(const char *fallback, im_template_value *out_val) {
    if (!fallback || !out_val) return false;
    if (str_eq(fallback, "{ThemeResource TextControlPlaceholderForeground}")) {
        *out_val = (im_template_value){IM_TVAL_BRUSH, IM_BRUSH_TextControlPlaceholderForeground, 0, 0, 0, NULL, NULL};
        return true;
    }
    if (str_eq(fallback, "{ThemeResource TextControlPlaceholderForegroundDisabled}")) {
        *out_val = (im_template_value){IM_TVAL_BRUSH, IM_BRUSH_TextControlPlaceholderForegroundDisabled, 0, 0, 0, NULL, NULL};
        return true;
    }
    if (str_eq(fallback, "{ThemeResource TextControlPlaceholderForegroundFocused}")) {
        *out_val = (im_template_value){IM_TVAL_BRUSH, IM_BRUSH_TextControlPlaceholderForegroundFocused, 0, 0, 0, NULL, NULL};
        return true;
    }
    if (str_eq(fallback, "{ThemeResource TextControlPlaceholderForegroundPointerOver}")) {
        *out_val = (im_template_value){IM_TVAL_BRUSH, IM_BRUSH_TextControlPlaceholderForegroundPointerOver, 0, 0, 0, NULL, NULL};
        return true;
    }
    return false;
}

static bool resolve_binding(const im_state_machine *sm, const im_template_value *val, im_template_value *out_val) {
    if (!sm || !val || !out_val) return false;
    if (val->kind != IM_TVAL_BINDING) {
        *out_val = *val;
        return true;
    }
    if (val->text) {
        for (uint32_t i = 0; i < sm->binding_count; i++) {
            if (str_eq(sm->bindings[i].name, val->text)) {
                *out_val = sm->bindings[i].value;
                return true;
            }
        }
    }
    if (val->fallback && resolve_theme_resource_fallback(val->fallback, out_val)) {
        return true;
    }
    *out_val = (im_template_value){IM_TVAL_FIXED, 0, 0, 0, 0, NULL, NULL};
    return true;
}

static bool find_style_setter(const im_state_machine *sm, const char *property, im_template_value *out_val) {
    if (!sm || !sm->tmpl || !sm->tables.setters || !property || !out_val) return false;
    uint16_t start = sm->tmpl->style_setter_start;
    uint16_t count = sm->tmpl->style_setter_count;
    bool found = false;
    for (uint16_t i = 0; i < count; i++) {
        const im_template_setter *s = &sm->tables.setters[start + i];
        if (s->target == NULL && str_eq(s->property, property)) {
            *out_val = s->value;
            found = true;
        }
    }
    return found;
}

static bool find_element_base_prop(const im_state_machine *sm, const char *target, const char *property, im_template_value *out_val) {
    if (!sm || !sm->tmpl || !property || !out_val) return false;
    if (target == NULL) {
        return find_style_setter(sm, property, out_val);
    }
    if (!sm->tables.elements || !sm->tables.props) return false;
    uint16_t estart = sm->tmpl->elem_start;
    uint16_t ecount = sm->tmpl->elem_count;
    for (uint16_t i = 0; i < ecount; i++) {
        const im_template_element *e = &sm->tables.elements[estart + i];
        if (!str_eq(e->name, target)) continue;
        for (uint16_t p = 0; p < e->prop_count; p++) {
            const im_template_prop *pr = &sm->tables.props[e->prop_start + p];
            if (!prop_matches(pr->name, property)) continue;
            if (pr->value.kind == IM_TVAL_TEMPLATE_BINDING && pr->value.text) {
                for (uint32_t bi = 0; bi < sm->binding_count; bi++) {
                    if (str_eq(sm->bindings[bi].name, pr->value.text)) {
                        *out_val = sm->bindings[bi].value;
                        return true;
                    }
                }
                return find_style_setter(sm, pr->value.text, out_val);
            }
            if (pr->value.kind == IM_TVAL_BINDING) {
                return resolve_binding(sm, &pr->value, out_val);
            }
            *out_val = pr->value;
            return true;
        }
    }
    return false;
}

static im_fixed default_base_fixed(const im_state_machine *sm, const char *target, const char *property) {
    im_template_value val;
    if (find_element_base_prop(sm, target, property, &val)) {
        return im_template_value_to_fixed(&val, &sm->palette, 0);
    }
    if (str_eq(property, "Opacity") || str_eq(property, "ScaleX") || str_eq(property, "ScaleY")) {
        return IM_ONE;
    }
    return 0;
}

static uint32_t default_base_color(const im_state_machine *sm, const char *target, const char *property) {
    im_template_value val;
    if (find_element_base_prop(sm, target, property, &val)) {
        return im_template_value_to_argb(&val, &sm->palette);
    }
    return 0u;
}

static bool entry_eval_value(const im_state_machine *sm, const im_state_entry *e, uint32_t now_ms, im_template_value *out_val) {
    if (!sm || !e || !out_val) return false;
    bool rm = sm->reduced_motion || e->snapped;
    switch (e->kind) {
    case IM_SENTRY_SETTER:
        return resolve_binding(sm, &e->setter_val, out_val);
    case IM_SENTRY_GEN_FIXED: {
        uint32_t el = im_anim_elapsed_ms(e->start_ms, now_ms);
        im_fixed v = (rm || e->duration_ms == 0u || el >= e->duration_ms)
            ? e->end_fixed
            : im_lerp_fixed_ratio(e->start_fixed, e->end_fixed, el, e->duration_ms);
        *out_val = (im_template_value){IM_TVAL_FIXED, v, 0, 0, 0, NULL, NULL};
        return true;
    }
    case IM_SENTRY_GEN_COLOR: {
        uint32_t el = im_anim_elapsed_ms(e->start_ms, now_ms);
        uint32_t c = (rm || e->duration_ms == 0u || el >= e->duration_ms)
            ? e->end_argb
            : im_lerp_argb_ratio(e->start_argb, e->end_argb, el, e->duration_ms);
        *out_val = (im_template_value){IM_TVAL_COLOR, (int32_t)c, (int32_t)c, (int32_t)c, -1, NULL, NULL};
        return true;
    }
    case IM_SENTRY_ANIM: {
        if (!e->anim) return false;
        if (e->anim->kind == IM_ANIM_OBJECT_KEYFRAMES) {
            im_template_value raw;
            if (!im_anim_eval_object(e->anim, sm->tables.keyframes, e->start_ms, now_ms, rm, &raw)) {
                return false;
            }
            return resolve_binding(sm, &raw, out_val);
        }
        if (e->anim->kind == IM_ANIM_COLOR_KEYFRAMES) {
            uint32_t c = im_anim_eval_color(e->anim, sm->tables.keyframes, &sm->palette, e->start_argb, e->start_ms, now_ms, rm);
            *out_val = (im_template_value){IM_TVAL_COLOR, (int32_t)c, (int32_t)c, (int32_t)c, -1, NULL, NULL};
            return true;
        }
        if (e->anim->kind == IM_ANIM_DRAG_ITEM_THEME && str_eq(e->property, "Opacity")) {
            uint32_t dur = e->duration_ms;
            uint32_t el = im_anim_elapsed_ms(e->start_ms, now_ms);
            im_fixed v = (rm || dur == 0u || el >= dur)
                ? e->end_fixed
                : im_lerp_fixed_ratio(e->start_fixed, e->end_fixed, el, dur);
            *out_val = (im_template_value){IM_TVAL_FIXED, v, 0, 0, 0, NULL, NULL};
            return true;
        }
        im_fixed v = im_anim_eval_fixed(e->anim, sm->tables.keyframes, &sm->palette, e->start_fixed, e->start_ms, now_ms, rm);
        *out_val = (im_template_value){IM_TVAL_FIXED, v, 0, 0, 0, NULL, NULL};
        return true;
    }
    }
    return false;
}

static bool entry_is_active(const im_state_machine *sm, const im_state_entry *e, uint32_t now_ms) {
    if (!sm || !e || sm->reduced_motion || e->snapped) return false;
    if (e->duration_ms == 0u) return false;
    return im_anim_elapsed_ms(e->start_ms, now_ms) < e->duration_ms;
}

static void detect_brush_transition(im_state_machine *sm) {
    sm->has_bg_transition = false;
    sm->bg_target = NULL;
    if (!sm || !sm->tmpl || !sm->tables.elements) return;
    uint16_t estart = sm->tmpl->elem_start;
    uint16_t ecount = sm->tmpl->elem_count;
    for (uint16_t i = 0; i < ecount; i++) {
        const im_template_element *e = &sm->tables.elements[estart + i];
        if (e->kind != IM_ELEM_BRUSH_TRANSITION) continue;
        if (e->parent < 0 || e->parent >= (int32_t)ecount) continue;
        const im_template_element *parent = &sm->tables.elements[estart + e->parent];
        uint32_t dur = IM_BRUSH_TRANSITION_DURATION_MS;
        if (sm->tables.props) {
            for (uint16_t p = 0; p < e->prop_count; p++) {
                const im_template_prop *pr = &sm->tables.props[e->prop_start + p];
                if (str_eq(pr->name, "Duration") && pr->value.kind == IM_TVAL_DURATION_MS) {
                    dur = (uint32_t)pr->value.a;
                }
            }
        }
        im_template_value base_bg;
        bool is_solid = false;
        enum im_brush_id init_brush = IM_BRUSH_COUNT;
        uint32_t init_argb = 0u;
        if (find_element_base_prop(sm, parent->name, "Background", &base_bg)) {
            if (base_bg.kind == IM_TVAL_BRUSH) {
                init_brush = (enum im_brush_id)base_bg.a;
                init_argb = im_template_value_to_argb(&base_bg, &sm->palette);
            } else if (base_bg.kind == IM_TVAL_COLOR) {
                is_solid = true;
                init_argb = im_template_value_to_argb(&base_bg, &sm->palette);
            }
        }
        sm->has_bg_transition = true;
        sm->bg_target = parent->name;
        im_brush_transition_init(&sm->bg_transition, is_solid, init_brush, init_argb, dur);
        return;
    }
}

void im_states_init_custom(im_state_machine *sm, const im_template *tmpl, const im_template_tables *tables, const im_palette *palette) {
    if (!sm) return;
    uint8_t *raw = (uint8_t *)sm;
    for (size_t i = 0; i < sizeof(*sm); i++) raw[i] = 0;
    sm->tmpl = tmpl;
    if (tables) {
        sm->tables = *tables;
    } else {
        sm->tables.setters = im_template_setters;
        sm->tables.elements = im_template_elements;
        sm->tables.props = im_template_props;
        sm->tables.groups = im_template_groups;
        sm->tables.states = im_template_states;
        sm->tables.transitions = im_template_transitions;
        sm->tables.anims = im_template_anims;
        sm->tables.keyframes = im_template_keyframes;
    }
    if (palette) sm->palette = *palette;
    for (uint32_t g = 0; g < IM_MAX_STATE_GROUPS; g++) {
        sm->group_state[g] = -1;
    }
    detect_brush_transition(sm);
}

void im_states_init(im_state_machine *sm, im_template_id id, const im_palette *palette) {
    if (!sm) return;
    if ((int)id < 0 || id >= IM_TEMPLATE_COUNT) {
        im_states_init_custom(sm, NULL, NULL, palette);
        return;
    }
    im_states_init_custom(sm, &im_templates[id], NULL, palette);
}

bool im_states_set_binding(im_state_machine *sm, const char *name, im_template_value value) {
    if (!sm || !name) return false;
    for (uint32_t i = 0; i < sm->binding_count; i++) {
        if (str_eq(sm->bindings[i].name, name)) {
            sm->bindings[i].value = value;
            return true;
        }
    }
    if (sm->binding_count >= IM_MAX_STATE_BINDINGS) return false;
    sm->bindings[sm->binding_count].name = name;
    sm->bindings[sm->binding_count].value = value;
    sm->binding_count++;
    return true;
}

void im_states_clear_binding(im_state_machine *sm, const char *name) {
    if (!sm || !name) return;
    for (uint32_t i = 0; i < sm->binding_count; i++) {
        if (str_eq(sm->bindings[i].name, name)) {
            for (uint32_t j = i + 1u; j < sm->binding_count; j++) {
                sm->bindings[j - 1u] = sm->bindings[j];
            }
            sm->binding_count--;
            return;
        }
    }
}

void im_states_set_reduced_motion(im_state_machine *sm, bool reduced_motion, uint32_t now_ms) {
    (void)now_ms;
    if (!sm) return;
    sm->reduced_motion = reduced_motion;
    if (reduced_motion) {
        for (uint32_t i = 0; i < sm->entry_count; i++) {
            sm->entries[i].snapped = true;
        }
        for (uint32_t g = 0; g < IM_MAX_STATE_GROUPS; g++) {
            sm->group_trans_snapped[g] = true;
        }
        if (sm->has_bg_transition) {
            sm->bg_transition.from_argb = sm->bg_transition.to_argb;
            sm->bg_transition.active = false;
        }
    }
}

/* WinUI VisualStateManager.cpp:219-252 transition selection:
   - case-insensitive state name matching
   - retains the LAST default (NULL, NULL) transition
   - strict '>' first-win across non-default matches: (from,to)=3, (NULL,to)=2, (from,NULL)=1 */
static const im_template_transition *select_transition(const im_state_machine *sm, const im_template_group *grp,
    const char *from_name, const char *to_name) {
    if (!sm || !grp || !sm->tables.transitions) return NULL;
    const im_template_transition *default_trans = NULL;
    const im_template_transition *best_trans = NULL;
    int best_score = 0;
    for (uint16_t i = 0; i < grp->trans_count; i++) {
        const im_template_transition *tr = &sm->tables.transitions[grp->trans_start + i];
        if (tr->from_state == NULL && tr->to_state == NULL) {
            default_trans = tr;
            continue;
        }
        int score = 0;
        if (tr->from_state != NULL && tr->to_state != NULL) {
            if (from_name && to_name && str_ieq(tr->from_state, from_name) && str_ieq(tr->to_state, to_name)) {
                score = 3;
            }
        } else if (tr->from_state == NULL && tr->to_state != NULL) {
            if (to_name && str_ieq(tr->to_state, to_name)) {
                score = 2;
            }
        } else if (tr->from_state != NULL && tr->to_state == NULL) {
            if (from_name && str_ieq(tr->from_state, from_name)) {
                score = 1;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_trans = tr;
        }
    }
    return best_trans ? best_trans : default_trans;
}

static bool add_entry(im_state_machine *sm, im_state_entry entry) {
    if (!sm) return false;
    /* Replace a shadowed entry in the same group/target/property/phase ONLY when the new entry
       provides a value immediately at t = 0 (preserve earlier setters before delayed discrete keyframes). */
    bool immediate = !(entry.kind == IM_SENTRY_ANIM && entry.anim &&
        entry.anim->kind == IM_ANIM_OBJECT_KEYFRAMES && sm->tables.keyframes &&
        entry.anim->kf_count > 0u && sm->tables.keyframes[entry.anim->kf_start].key_time_ms > 0u);
    if (immediate) {
        for (uint32_t i = 0; i < sm->entry_count; i++) {
            if (sm->entries[i].group_index == entry.group_index &&
                sm->entries[i].from_transition == entry.from_transition &&
                str_eq(sm->entries[i].target, entry.target) &&
                prop_matches(sm->entries[i].property, entry.property)) {
                for (uint32_t j = i + 1u; j < sm->entry_count; j++) {
                    sm->entries[j - 1u] = sm->entries[j];
                }
                sm->entries[sm->entry_count - 1u] = entry;
                return true;
            }
        }
    }
    if (sm->entry_count >= IM_MAX_STATE_ENTRIES) return false;
    sm->entries[sm->entry_count++] = entry;
    return true;
}

typedef struct anim_capture {
    im_fixed f0;
    im_fixed f1;
    im_fixed f2;
    uint32_t c0;
} anim_capture;

static anim_capture capture_anim_start(const im_state_machine *sm, const im_template_anim *a, uint32_t now_ms) {
    anim_capture cap = {0, 0, 0, 0u};
    if (!sm || !a) return cap;
    switch (a->kind) {
    case IM_ANIM_COLOR_KEYFRAMES:
        cap.c0 = im_states_sample_color(sm, a->target_name, a->target_property, now_ms,
            default_base_color(sm, a->target_name, a->target_property));
        break;
    case IM_ANIM_DOUBLE:
    case IM_ANIM_DOUBLE_KEYFRAMES:
        cap.f0 = im_states_sample_fixed(sm, a->target_name, a->target_property, now_ms,
            default_base_fixed(sm, a->target_name, a->target_property));
        break;
    case IM_ANIM_DRAG_OVER_THEME: {
        const char *prop = (a->direction == IM_DIR_TOP || a->direction == IM_DIR_BOTTOM) ? "DragOverY" : "DragOverX";
        cap.f0 = im_states_sample_fixed(sm, a->target_name, prop, now_ms, 0);
        break;
    }
    case IM_ANIM_DRAG_ITEM_THEME:
        cap.f0 = im_states_sample_fixed(sm, a->target_name, "ScaleX", now_ms, IM_ONE);
        cap.f1 = im_states_sample_fixed(sm, a->target_name, "ScaleY", now_ms, IM_ONE);
        cap.f2 = im_states_sample_fixed(sm, a->target_name, "Opacity", now_ms, IM_ONE);
        break;
    case IM_ANIM_DROP_TARGET_THEME:
        cap.f0 = im_states_sample_fixed(sm, a->target_name, "ScaleX", now_ms, IM_ONE);
        cap.f1 = im_states_sample_fixed(sm, a->target_name, "ScaleY", now_ms, IM_ONE);
        break;
    case IM_ANIM_FADE_IN_THEME:
        cap.f0 = im_states_sample_fixed(sm, a->target_name, "Opacity", now_ms, 0);
        break;
    case IM_ANIM_FADE_OUT_THEME:
        cap.f0 = im_states_sample_fixed(sm, a->target_name, "Opacity", now_ms, IM_ONE);
        break;
    case IM_ANIM_REPOSITION_THEME:
    case IM_ANIM_OBJECT_KEYFRAMES:
        break;
    }
    return cap;
}

static void add_anim_entries_captured(im_state_machine *sm, uint8_t group_index, bool from_transition,
    const im_template_anim *anim, anim_capture cap, uint32_t now_ms) {
    if (!sm || !anim) return;
    uint32_t dur = im_anim_duration_ms(anim, sm->tables.keyframes);

    if (anim->kind == IM_ANIM_REPOSITION_THEME) {
        if (anim->has_from) {
            im_template_value resolved;
            resolve_binding(sm, &anim->from_val, &resolved);
            im_fixed sx = im_template_value_to_fixed(&resolved, &sm->palette, 0);
            im_state_entry e = {
                .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
                .snapped = sm->reduced_motion, .target = anim->target_name, .property = "RepositionX",
                .anim = anim, .start_fixed = sx, .end_fixed = 0, .start_ms = now_ms, .duration_ms = dur
            };
            add_entry(sm, e);
        }
        if (anim->has_from_y) {
            im_template_value resolved;
            resolve_binding(sm, &anim->from_y_val, &resolved);
            im_fixed sy = im_template_value_to_fixed(&resolved, &sm->palette, 0);
            im_state_entry e = {
                .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
                .snapped = sm->reduced_motion, .target = anim->target_name, .property = "RepositionY",
                .anim = anim, .start_fixed = sy, .end_fixed = 0, .start_ms = now_ms, .duration_ms = dur
            };
            add_entry(sm, e);
        }
        return;
    }

    if (anim->kind == IM_ANIM_DRAG_OVER_THEME) {
        const char *prop = (anim->direction == IM_DIR_TOP || anim->direction == IM_DIR_BOTTOM) ? "DragOverY" : "DragOverX";
        im_state_entry e = {
            .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
            .snapped = sm->reduced_motion, .target = anim->target_name, .property = prop,
            .anim = anim, .start_fixed = cap.f0, .start_ms = now_ms, .duration_ms = dur
        };
        add_entry(sm, e);
        return;
    }

    if (anim->kind == IM_ANIM_DRAG_ITEM_THEME) {
        const char *axes[2] = {"ScaleX", "ScaleY"};
        im_fixed starts[2] = {cap.f0, cap.f1};
        for (int a = 0; a < 2; a++) {
            im_state_entry e = {
                .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
                .snapped = sm->reduced_motion, .target = anim->target_name, .property = axes[a],
                .anim = anim, .start_fixed = starts[a], .end_fixed = IM_DRAG_ITEM_SCALE,
                .start_ms = now_ms, .duration_ms = dur
            };
            add_entry(sm, e);
        }
        /* DragItemThemeAnimation composes its 0.65 opacity multiplier against the winning
           surviving destination Opacity (newest-first in this group, else surviving other groups
           or element base, NEVER the removed outgoing state), while starting from cap.f2 for
           continuity. add_entry reuses any same-group Opacity slot so capacity cannot drop it. */
        im_fixed base_end_op = default_base_fixed(sm, anim->target_name, "Opacity");
        bool found_dest_op = false;
        for (uint32_t i = sm->entry_count; i > 0u; i--) {
            const im_state_entry *prev = &sm->entries[i - 1u];
            if (prev->from_transition && !entry_is_active(sm, prev, now_ms)) continue;
            if (target_matches(prev->target, anim->target_name) &&
                str_eq(prev->property, "Opacity")) {
                im_template_value end_v;
                if (entry_eval_value(sm, prev, now_ms + prev->duration_ms, &end_v)) {
                    base_end_op = im_template_value_to_fixed(&end_v, &sm->palette, base_end_op);
                    found_dest_op = true;
                }
                break;
            }
        }
        (void)found_dest_op;
        im_fixed comp_end = (im_fixed)(((int64_t)base_end_op * (int64_t)IM_DRAG_ITEM_OPACITY_MULT + (IM_ONE / 2)) >> 16);
        im_state_entry op_e = {
            .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
            .snapped = sm->reduced_motion, .target = anim->target_name, .property = "Opacity",
            .anim = anim, .start_fixed = cap.f2, .end_fixed = comp_end,
            .start_ms = now_ms, .duration_ms = dur
        };
        add_entry(sm, op_e);
        return;
    }

    if (anim->kind == IM_ANIM_DROP_TARGET_THEME) {
        const char *scales[2] = {"ScaleX", "ScaleY"};
        im_fixed starts[2] = {cap.f0, cap.f1};
        for (int a = 0; a < 2; a++) {
            im_state_entry e = {
                .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
                .snapped = sm->reduced_motion, .target = anim->target_name, .property = scales[a],
                .anim = anim, .start_fixed = starts[a], .end_fixed = IM_DROP_TARGET_SCALE,
                .start_ms = now_ms, .duration_ms = dur
            };
            add_entry(sm, e);
        }
        return;
    }

    if (anim->kind == IM_ANIM_FADE_IN_THEME || anim->kind == IM_ANIM_FADE_OUT_THEME) {
        im_state_entry e = {
            .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
            .snapped = sm->reduced_motion, .target = anim->target_name, .property = "Opacity",
            .anim = anim, .start_fixed = cap.f0, .start_ms = now_ms, .duration_ms = dur
        };
        add_entry(sm, e);
        return;
    }

    if (anim->kind == IM_ANIM_COLOR_KEYFRAMES) {
        im_state_entry e = {
            .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
            .snapped = sm->reduced_motion, .target = anim->target_name, .property = anim->target_property,
            .anim = anim, .start_argb = cap.c0, .start_ms = now_ms, .duration_ms = dur
        };
        add_entry(sm, e);
        return;
    }

    if (anim->kind == IM_ANIM_OBJECT_KEYFRAMES) {
        im_state_entry e = {
            .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
            .snapped = sm->reduced_motion, .target = anim->target_name, .property = anim->target_property,
            .anim = anim, .start_ms = now_ms, .duration_ms = dur
        };
        add_entry(sm, e);
        return;
    }

    /* IM_ANIM_DOUBLE / IM_ANIM_DOUBLE_KEYFRAMES */
    im_state_entry e = {
        .kind = IM_SENTRY_ANIM, .group_index = group_index, .from_transition = from_transition,
        .snapped = sm->reduced_motion, .target = anim->target_name, .property = anim->target_property,
        .anim = anim, .start_fixed = cap.f0, .start_ms = now_ms, .duration_ms = dur
    };
    add_entry(sm, e);
}

static bool sample_raw_value_no_bt(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, im_template_value *out_val, bool *out_from_state) {
    if (!sm || !property || !out_val) return false;
    if (out_from_state) *out_from_state = false;

    bool found = false;
    for (uint32_t i = sm->entry_count; i > 0u; i--) {
        const im_state_entry *e = &sm->entries[i - 1u];
        if (!target_matches(e->target, target) || !prop_matches(e->property, property)) continue;
        if (e->from_transition && !entry_is_active(sm, e, now_ms)) {
            /* Once a transition or generated return finishes, release ownership so any surviving
               active state group (or base property) governs the presentation value. */
            if (e->anim && e->anim->kind == IM_ANIM_REPOSITION_THEME) {
                *out_val = (im_template_value){IM_TVAL_FIXED, 0, 0, 0, 0, NULL, NULL};
                return true;
            }
            continue;
        }
        if (entry_eval_value(sm, e, now_ms, out_val)) {
            if (out_from_state) *out_from_state = true;
            found = true;
            break;
        }
    }
    if (found) return true;
    return find_element_base_prop(sm, target, property, out_val);
}

bool im_states_go_to(im_state_machine *sm, const char *state_name, uint32_t now_ms) {
    if (!sm || !sm->tmpl || !sm->tables.groups || !sm->tables.states || !state_name) return false;

    uint32_t group_count = sm->tmpl->group_count;
    if (group_count > IM_MAX_STATE_GROUPS) group_count = IM_MAX_STATE_GROUPS;

    int32_t target_group = -1;
    int32_t target_state_rel = -1;
    const im_template_group *grp = NULL;
    const im_template_state *st = NULL;

    for (uint32_t g = 0; g < group_count; g++) {
        const im_template_group *cand_grp = &sm->tables.groups[sm->tmpl->group_start + g];
        for (uint16_t s = 0; s < cand_grp->state_count; s++) {
            const im_template_state *cand_st = &sm->tables.states[cand_grp->state_start + s];
            if (str_ieq(cand_st->name, state_name)) {
                target_group = (int32_t)g;
                target_state_rel = (int32_t)s;
                grp = cand_grp;
                st = cand_st;
                break;
            }
        }
        if (target_group >= 0) break;
    }

    if (target_group < 0 || !grp || !st) return false;
    if (sm->group_state[target_group] == target_state_rel) return false;

    const char *from_name = NULL;
    if (sm->group_state[target_group] >= 0) {
        from_name = sm->tables.states[grp->state_start + sm->group_state[target_group]].name;
    }
    const im_template_transition *tr = select_transition(sm, grp, from_name, st->name);

    /* Snapshot outgoing entries owned by target_group before removing them (for generated transitions). */
    typedef struct out_snap {
        const char *target;
        const char *property;
        bool is_color;
        im_fixed fixed_val;
        uint32_t color_val;
    } out_snap;
    out_snap snaps[IM_MAX_STATE_ENTRIES];
    uint32_t snap_count = 0;

    if (tr && tr->duration_ms > 0u && tr->anim_count == 0u) {
        for (uint32_t i = 0; i < sm->entry_count; i++) {
            const im_state_entry *e = &sm->entries[i];
            if (e->group_index != (uint8_t)target_group) continue;
            /* Sample the visible presentation value across all groups (not the shadowed outgoing entry
               directly), so ending a drag while Disabled stays active does not jump away from Disabled. */
            im_template_value sv;
            if (!sample_raw_value_no_bt(sm, e->target, e->property, now_ms, &sv, NULL)) continue;
            if (sv.kind == IM_TVAL_FIXED) {
                snaps[snap_count++] = (out_snap){e->target, e->property, false, sv.a, 0u};
            } else if (sv.kind == IM_TVAL_COLOR || sv.kind == IM_TVAL_BRUSH) {
                snaps[snap_count++] = (out_snap){e->target, e->property, true, 0, im_template_value_to_argb(&sv, &sm->palette)};
            }
        }
    }

    /* Capture pre-removal presentation values for ALL incoming state and transition tracks
       (including theme tracks like DragOver/DragItem/DropTarget/FadeIn/FadeOut) BEFORE clearing group. */
    anim_capture state_caps[IM_MAX_STATE_ENTRIES];
    anim_capture trans_caps[IM_MAX_STATE_ENTRIES];
    uint32_t n_state_anims = st->anim_count;
    if (n_state_anims > IM_MAX_STATE_ENTRIES) n_state_anims = IM_MAX_STATE_ENTRIES;
    if (sm->tables.anims) {
        for (uint32_t i = 0; i < n_state_anims; i++) {
            state_caps[i] = capture_anim_start(sm, &sm->tables.anims[st->anim_start + i], now_ms);
        }
    }
    uint32_t n_trans_anims = (tr && sm->tables.anims) ? tr->anim_count : 0u;
    if (n_trans_anims > IM_MAX_STATE_ENTRIES) n_trans_anims = IM_MAX_STATE_ENTRIES;
    for (uint32_t i = 0; i < n_trans_anims; i++) {
        trans_caps[i] = capture_anim_start(sm, &sm->tables.anims[tr->anim_start + i], now_ms);
    }

    /* Remove outgoing entries owned by target_group ONLY; other groups stay intact. */
    uint32_t write_idx = 0;
    for (uint32_t i = 0; i < sm->entry_count; i++) {
        if (sm->entries[i].group_index != (uint8_t)target_group) {
            sm->entries[write_idx++] = sm->entries[i];
        }
    }
    sm->entry_count = write_idx;

    sm->group_state[target_group] = target_state_rel;
    sm->group_start_ms[target_group] = now_ms;
    sm->group_trans_dur_ms[target_group] = tr ? tr->duration_ms : 0u;
    sm->group_trans_snapped[target_group] = sm->reduced_motion;

    /* Add destination state setters. */
    if (sm->tables.setters) {
        for (uint16_t i = 0; i < st->setter_count; i++) {
            const im_template_setter *s = &sm->tables.setters[st->setter_start + i];
            im_state_entry e = {
                .kind = IM_SENTRY_SETTER, .group_index = (uint8_t)target_group, .from_transition = false,
                .snapped = true, .target = s->target, .property = s->property,
                .setter_val = s->value, .start_ms = now_ms, .duration_ms = 0u
            };
            add_entry(sm, e);
        }
    }

    /* Add destination state storyboard animations using pre-removal captured start values.
       Install non-DragItem tracks first so DragItem opacity composition is independent of storyboard order. */
    if (sm->tables.anims) {
        for (uint32_t i = 0; i < n_state_anims; i++) {
            const im_template_anim *a = &sm->tables.anims[st->anim_start + i];
            if (a->kind == IM_ANIM_DRAG_ITEM_THEME) continue;
            add_anim_entries_captured(sm, (uint8_t)target_group, false, a, state_caps[i], now_ms);
        }
        for (uint32_t i = 0; i < n_state_anims; i++) {
            const im_template_anim *a = &sm->tables.anims[st->anim_start + i];
            if (a->kind != IM_ANIM_DRAG_ITEM_THEME) continue;
            add_anim_entries_captured(sm, (uint8_t)target_group, false, a, state_caps[i], now_ms);
        }
    }

    /* Add explicit transition storyboard animations (same order-independent DragItem pass). */
    for (uint32_t i = 0; i < n_trans_anims; i++) {
        const im_template_anim *a = &sm->tables.anims[tr->anim_start + i];
        if (a->kind == IM_ANIM_DRAG_ITEM_THEME) continue;
        add_anim_entries_captured(sm, (uint8_t)target_group, true, a, trans_caps[i], now_ms);
    }
    for (uint32_t i = 0; i < n_trans_anims; i++) {
        const im_template_anim *a = &sm->tables.anims[tr->anim_start + i];
        if (a->kind != IM_ANIM_DRAG_ITEM_THEME) continue;
        add_anim_entries_captured(sm, (uint8_t)target_group, true, a, trans_caps[i], now_ms);
    }

    /* Zero-track generated transition: interpolate snapshotted outgoing properties toward the
       SURVIVING presentation value (honoring other active groups like Disabled!) and release on expiry. */
    if (tr && tr->duration_ms > 0u && tr->anim_count == 0u) {
        for (uint32_t i = 0; i < snap_count; i++) {
            bool overwritten = false;
            for (uint32_t j = 0; j < sm->entry_count; j++) {
                if (sm->entries[j].group_index == (uint8_t)target_group &&
                    target_matches(sm->entries[j].target, snaps[i].target) &&
                    prop_matches(sm->entries[j].property, snaps[i].property)) {
                    overwritten = true;
                    break;
                }
            }
            if (overwritten) continue;
            if (!snaps[i].is_color) {
                im_fixed base = default_base_fixed(sm, snaps[i].target, snaps[i].property);
                im_fixed dest = im_states_sample_fixed(sm, snaps[i].target, snaps[i].property, now_ms, base);
                if (snaps[i].fixed_val != dest) {
                    im_state_entry e = {
                        .kind = IM_SENTRY_GEN_FIXED, .group_index = (uint8_t)target_group, .from_transition = true,
                        .snapped = sm->reduced_motion, .target = snaps[i].target, .property = snaps[i].property,
                        .start_fixed = snaps[i].fixed_val, .end_fixed = dest,
                        .start_ms = now_ms, .duration_ms = tr->duration_ms
                    };
                    add_entry(sm, e);
                }
            } else {
                uint32_t base = default_base_color(sm, snaps[i].target, snaps[i].property);
                uint32_t dest = im_states_sample_color(sm, snaps[i].target, snaps[i].property, now_ms, base);
                if (snaps[i].color_val != dest) {
                    im_state_entry e = {
                        .kind = IM_SENTRY_GEN_COLOR, .group_index = (uint8_t)target_group, .from_transition = true,
                        .snapped = sm->reduced_motion, .target = snaps[i].target, .property = snaps[i].property,
                        .start_argb = snaps[i].color_val, .end_argb = dest,
                        .start_ms = now_ms, .duration_ms = tr->duration_ms
                    };
                    add_entry(sm, e);
                }
            }
        }
    }

    /* Retarget Background BrushTransition for both IM_TVAL_BRUSH and literal IM_TVAL_COLOR. */
    if (sm->has_bg_transition && sm->bg_target) {
        im_template_value bg_val;
        if (sample_raw_value_no_bt(sm, sm->bg_target, "Background", now_ms, &bg_val, NULL)) {
            if (bg_val.kind == IM_TVAL_BRUSH) {
                const im_palette *wp = sm->window_palette ? sm->window_palette : &sm->palette;
                uint32_t new_argb = sm->res_node
                    ? im_resources_resolve_brush(sm->res_node, wp, (enum im_brush_id)bg_val.a, 0, IM_ONE, IM_ONE)
                    : im_template_value_to_argb(&bg_val, &sm->palette);
                im_brush_transition_set(&sm->bg_transition, false, (enum im_brush_id)bg_val.a, new_argb, now_ms, sm->reduced_motion);
            } else if (bg_val.kind == IM_TVAL_COLOR) {
                uint32_t new_argb = im_template_value_to_argb(&bg_val, &sm->palette);
                im_brush_transition_set(&sm->bg_transition, true, IM_BRUSH_COUNT, new_argb, now_ms, sm->reduced_motion);
            }
        }
    }

    return true;
}

const char *im_states_current(const im_state_machine *sm, uint32_t group_index) {
    if (!sm || !sm->tmpl || !sm->tables.groups || !sm->tables.states) return NULL;
    if (group_index >= sm->tmpl->group_count || group_index >= IM_MAX_STATE_GROUPS) return NULL;
    int32_t rel = sm->group_state[group_index];
    if (rel < 0) return NULL;
    const im_template_group *grp = &sm->tables.groups[sm->tmpl->group_start + group_index];
    if ((uint16_t)rel >= grp->state_count) return NULL;
    return sm->tables.states[grp->state_start + rel].name;
}

bool im_states_is_animating(const im_state_machine *sm, uint32_t now_ms) {
    if (!sm || sm->reduced_motion) return false;
    if (sm->has_bg_transition && im_brush_transition_is_active(&sm->bg_transition, now_ms, false)) {
        return true;
    }
    for (uint32_t i = 0; i < sm->entry_count; i++) {
        if (entry_is_active(sm, &sm->entries[i], now_ms)) return true;
    }
    uint32_t gc = sm->tmpl ? sm->tmpl->group_count : 0u;
    if (gc > IM_MAX_STATE_GROUPS) gc = IM_MAX_STATE_GROUPS;
    for (uint32_t g = 0; g < gc; g++) {
        if (!sm->group_trans_snapped[g] && sm->group_trans_dur_ms[g] > 0u &&
            im_anim_elapsed_ms(sm->group_start_ms[g], now_ms) < sm->group_trans_dur_ms[g]) {
            return true;
        }
    }
    return false;
}

bool im_states_sample_value(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, im_template_value *out_val) {
    return sample_raw_value_no_bt(sm, target, property, now_ms, out_val, NULL);
}

static uint32_t resolve_sm_brush_argb(const im_state_machine *sm, enum im_brush_id id) {
    if (sm->res_node) {
        const im_palette *wp = sm->window_palette ? sm->window_palette : &sm->palette;
        return im_resources_resolve_brush(sm->res_node, wp, id, 0, IM_ONE, IM_ONE);
    }
    return im_brush_over(&sm->palette, id, 0, IM_ONE, IM_ONE);
}

static im_fixed resolve_sm_value_fixed(const im_state_machine *sm, enum im_value_id id, im_fixed fallback) {
    if (id < 0 || id >= IM_VALUE_COUNT) return fallback;
    if (sm->res_node) {
        const im_palette *wp = sm->window_palette ? sm->window_palette : &sm->palette;
        im_fixed v[4];
        im_resources_resolve_value(sm->res_node, wp, id, v);
        return v[0];
    }
    return im_value(&sm->palette, id)[0];
}

void im_states_set_resource_context(im_state_machine *sm, const im_node *node, const im_palette *window_palette) {
    if (!sm) return;
    sm->res_node = node;
    sm->window_palette = window_palette;
    if (node || window_palette) {
        sm->palette = im_resources_effective_palette(node, window_palette ? window_palette : &sm->palette);
    }
}

bool im_states_refresh_theme(im_state_machine *sm, const im_palette *window_palette, uint32_t now_ms) {
    if (!sm) return false;
    const im_palette *wp = window_palette ? window_palette : (sm->window_palette ? sm->window_palette : &sm->palette);
    sm->window_palette = window_palette ? window_palette : sm->window_palette;
    im_palette next = im_resources_effective_palette(sm->res_node, wp);
    bool changed = (next.theme != sm->palette.theme);
    for (int i = 0; i < IM_SLOT_COUNT && !changed; i++) {
        if (next.slot[i] != sm->palette.slot[i]) changed = true;
    }
    sm->palette = next;

    /* Update any in-flight BrushTransition target so mid-transition theme or override changes converge cleanly. */
    if (sm->has_bg_transition && !sm->bg_transition.is_solid_color && sm->bg_transition.target_brush < IM_BRUSH_COUNT) {
        uint32_t new_to = resolve_sm_brush_argb(sm, sm->bg_transition.target_brush);
        if (new_to != sm->bg_transition.to_argb) {
            uint32_t cur = im_brush_transition_sample(&sm->bg_transition, now_ms, sm->reduced_motion);
            sm->bg_transition.from_argb = cur;
            sm->bg_transition.to_argb = new_to;
            changed = true;
        }
    }
    return changed;
}

im_fixed im_states_sample_fixed(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, im_fixed fallback) {
    im_template_value val;
    if (!sample_raw_value_no_bt(sm, target, property, now_ms, &val, NULL)) return fallback;
    if (val.kind == IM_TVAL_THEME_VALUE) {
        return resolve_sm_value_fixed(sm, (enum im_value_id)val.a, fallback);
    }
    return im_template_value_to_fixed(&val, &sm->palette, fallback);
}

uint32_t im_states_sample_color(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, uint32_t fallback) {
    if (!sm) return fallback;
    if (sm->has_bg_transition && target_matches(sm->bg_target, target) && str_eq(property, "Background")) {
        return im_brush_transition_sample(&sm->bg_transition, now_ms, sm->reduced_motion);
    }
    im_template_value val;
    if (!sample_raw_value_no_bt(sm, target, property, now_ms, &val, NULL)) return fallback;
    if (val.kind == IM_TVAL_BRUSH) {
        return resolve_sm_brush_argb(sm, (enum im_brush_id)val.a);
    }
    if (val.kind == IM_TVAL_COLOR) {
        return im_template_value_to_argb(&val, &sm->palette);
    }
    return fallback;
}

bool im_states_sample_brush(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, enum im_brush_id *out_brush, uint32_t *out_argb, bool *out_is_transitioning) {
    if (!sm || !property) return false;
    if (sm->has_bg_transition && target_matches(sm->bg_target, target) && str_eq(property, "Background")) {
        bool active = im_brush_transition_is_active(&sm->bg_transition, now_ms, sm->reduced_motion);
        if (out_is_transitioning) *out_is_transitioning = active;
        if (out_brush) *out_brush = sm->bg_transition.is_solid_color ? IM_BRUSH_COUNT : sm->bg_transition.target_brush;
        if (out_argb) *out_argb = im_brush_transition_sample(&sm->bg_transition, now_ms, sm->reduced_motion);
        return true;
    }
    im_template_value val;
    if (!sample_raw_value_no_bt(sm, target, property, now_ms, &val, NULL)) return false;
    if (out_is_transitioning) *out_is_transitioning = false;
    if (val.kind == IM_TVAL_BRUSH) {
        if (out_brush) *out_brush = (enum im_brush_id)val.a;
        if (out_argb) *out_argb = resolve_sm_brush_argb(sm, (enum im_brush_id)val.a);
        return true;
    }
    if (val.kind == IM_TVAL_COLOR) {
        if (out_brush) *out_brush = IM_BRUSH_COUNT;
        if (out_argb) *out_argb = im_template_value_to_argb(&val, &sm->palette);
        return true;
    }
    return false;
}

static bool parse_align_str(const char *s, im_align *out) {
    if (!s || !out) return false;
    if (str_eq(s, "Left") || str_eq(s, "Top")) { *out = IM_ALIGN_START; return true; }
    if (str_eq(s, "Center")) { *out = IM_ALIGN_CENTER; return true; }
    if (str_eq(s, "Right") || str_eq(s, "Bottom")) { *out = IM_ALIGN_END; return true; }
    if (str_eq(s, "Stretch")) { *out = IM_ALIGN_STRETCH; return true; }
    return false;
}

static bool value_to_thickness(const im_template_value *v, const im_palette *palette, im_thickness *out) {
    if (!v || !out) return false;
    if (v->kind == IM_TVAL_QUAD) {
        *out = (im_thickness){v->a, v->b, v->c, v->d};
        return true;
    }
    if (v->kind == IM_TVAL_THEME_VALUE && palette && v->a >= 0 && v->a < IM_VALUE_COUNT) {
        const im_fixed *q = im_value(palette, (enum im_value_id)v->a);
        *out = (im_thickness){q[0], q[1], q[2], q[3]};
        return true;
    }
    return false;
}

void im_states_apply_to_tree(const im_state_machine *sm, im_node *root, uint32_t now_ms) {
    if (!sm || !root) return;
    if (root->name) {
        im_template_value val;
        bool from_state = false;

        if (sample_raw_value_no_bt(sm, root->name, "Width", now_ms, &val, &from_state) && from_state) {
            im_node_set_state_width(root, true, im_template_value_to_fixed(&val, &sm->palette, root->width));
        } else {
            im_node_set_state_width(root, false, 0);
        }

        if (sample_raw_value_no_bt(sm, root->name, "Height", now_ms, &val, &from_state) && from_state) {
            im_node_set_state_height(root, true, im_template_value_to_fixed(&val, &sm->palette, root->height));
        } else {
            im_node_set_state_height(root, false, 0);
        }

        im_thickness thick;
        if (sample_raw_value_no_bt(sm, root->name, "Margin", now_ms, &val, &from_state) && from_state &&
            value_to_thickness(&val, &sm->palette, &thick)) {
            im_node_set_state_margin(root, true, thick);
        } else {
            im_node_set_state_margin(root, false, (im_thickness){0, 0, 0, 0});
        }

        if (sample_raw_value_no_bt(sm, root->name, "Visibility", now_ms, &val, &from_state) && from_state &&
            val.kind == IM_TVAL_ENUM_STR && val.text) {
            im_visibility vis = str_eq(val.text, "Collapsed") ? IM_VIS_COLLAPSED : IM_VIS_VISIBLE;
            im_node_set_state_visibility(root, true, vis);
        } else {
            im_node_set_state_visibility(root, false, IM_VIS_VISIBLE);
        }

        im_align al;
        if (sample_raw_value_no_bt(sm, root->name, "HorizontalAlignment", now_ms, &val, &from_state) && from_state &&
            val.kind == IM_TVAL_ENUM_STR && parse_align_str(val.text, &al)) {
            im_node_set_state_h_align(root, true, al);
        } else {
            im_node_set_state_h_align(root, false, IM_ALIGN_STRETCH);
        }

        if (sample_raw_value_no_bt(sm, root->name, "VerticalAlignment", now_ms, &val, &from_state) && from_state &&
            val.kind == IM_TVAL_ENUM_STR && parse_align_str(val.text, &al)) {
            im_node_set_state_v_align(root, true, al);
        } else {
            im_node_set_state_v_align(root, false, IM_ALIGN_STRETCH);
        }

        if (sample_raw_value_no_bt(sm, root->name, "BorderThickness", now_ms, &val, &from_state) && from_state &&
            value_to_thickness(&val, &sm->palette, &thick)) {
            im_node_set_state_border_thickness(root, true, thick);
        } else {
            im_node_set_state_border_thickness(root, false, (im_thickness){0, 0, 0, 0});
        }

        im_fixed op = im_states_sample_fixed(sm, root->name, "Opacity", now_ms, root->base_opacity);
        if (op < 0) op = 0;
        if (op > IM_ONE) op = IM_ONE;
        if (op != root->opacity) {
            root->opacity = op;
            im_tree_invalidate_paint(root);
        }
    }
    if (root->kind == IM_ELEM_PATH) {
        im_template_value dval;
        bool has_dyn = false;
        if (root->name && im_states_sample_value(sm, root->name, "Data", now_ms, &dval)) {
            has_dyn = true;
        } else if (root->path_data_binding != NULL) {
            bool found_b = false;
            for (uint32_t i = 0; i < sm->binding_count; i++) {
                if (str_eq(sm->bindings[i].name, root->path_data_binding)) {
                    dval = sm->bindings[i].value;
                    found_b = true;
                    break;
                }
            }
            if (!found_b) {
                dval = (im_template_value){ IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, root->path_data_binding, NULL };
            }
            has_dyn = true;
        }

        uint8_t cur_kind = 0u;
        uint32_t cur_a = 0u, cur_b = 0u;
        uint8_t cur_c = 0u;
        if (has_dyn) {
            if (dval.kind == IM_TVAL_PATH && dval.b > 0) {
                im_node_set_path_intrinsic_bounds(root, sm->tables.path_segs, (uint32_t)dval.a, (uint32_t)dval.b);
                cur_kind = (uint8_t)IM_TVAL_PATH;
                cur_a = (uint32_t)dval.a;
                cur_b = (uint32_t)dval.b;
                cur_c = (uint8_t)dval.c;
            } else if (dval.kind == IM_TVAL_ICON && dval.a > IM_ICON_NONE && dval.a < IM_ICON_COUNT) {
                im_fixed sz = (dval.a == IM_ICON_CHECKMARK) ? (16 * IM_ONE) : (10 * IM_ONE);
                if (root->path_has_stroke && root->path_stroke_thickness > 0) {
                    int64_t s2 = (int64_t)sz + root->path_stroke_thickness;
                    sz = (s2 > INT32_MAX) ? INT32_MAX : (im_fixed)s2;
                }
                if (root->intrinsic_width != sz || root->intrinsic_height != sz) {
                    root->intrinsic_width = sz;
                    root->intrinsic_height = sz;
                    im_tree_invalidate_measure(root);
                }
                cur_kind = (uint8_t)IM_TVAL_ICON;
                cur_a = (uint32_t)dval.a;
            } else if (dval.kind == IM_TVAL_NULL || dval.kind == IM_TVAL_NONE || dval.kind == IM_TVAL_TEMPLATE_BINDING) {
                im_node_set_path_intrinsic_bounds(root, NULL, 0u, 0u);
                cur_kind = (uint8_t)IM_TVAL_NULL;
            }
        } else if (root->path_seg_count > 0u) {
            im_node_set_path_intrinsic_bounds(root, sm->tables.path_segs, root->path_seg_start, root->path_seg_count);
            cur_kind = (uint8_t)IM_TVAL_PATH;
            cur_a = root->path_seg_start;
            cur_b = root->path_seg_count;
            cur_c = root->path_fill_rule;
        }
        if (root->path_applied_valid &&
            (root->path_applied_kind != cur_kind || root->path_applied_a != cur_a ||
             root->path_applied_b != cur_b || root->path_applied_c != cur_c)) {
            im_tree_invalidate_paint(root);
        }
        root->path_applied_valid = true;
        root->path_applied_kind = cur_kind;
        root->path_applied_a = cur_a;
        root->path_applied_b = cur_b;
        root->path_applied_c = cur_c;
    }
    if (root->render_transform_name) {
        const char *tn = root->render_transform_name;
        im_transform tr = im_transform_identity();
        bool changed = false;
        if (root->render_transform_kind == IM_ELEM_TRANSLATE_TRANSFORM) {
            im_fixed tx = im_states_sample_fixed(sm, tn, "X", now_ms, 0);
            im_fixed ty = im_states_sample_fixed(sm, tn, "Y", now_ms, 0);
            if (tx != 0 || ty != 0) {
                tr = im_transform_translate(tx, ty);
                changed = true;
            }
        } else if (root->render_transform_kind == IM_ELEM_SCALE_TRANSFORM) {
            im_fixed sx = im_states_sample_fixed(sm, tn, "ScaleX", now_ms, IM_ONE);
            im_fixed sy = im_states_sample_fixed(sm, tn, "ScaleY", now_ms, IM_ONE);
            im_fixed cx = im_states_sample_fixed(sm, tn, "CenterX", now_ms, 0);
            im_fixed cy = im_states_sample_fixed(sm, tn, "CenterY", now_ms, 0);
            if (sx != IM_ONE || sy != IM_ONE) {
                tr = im_transform_scale_at(sx, sy, cx, cy);
                changed = true;
            }
        } else if (root->render_transform_kind == IM_ELEM_COMPOSITE_TRANSFORM) {
            im_fixed cx = im_states_sample_fixed(sm, tn, "CenterX", now_ms, 0);
            im_fixed cy = im_states_sample_fixed(sm, tn, "CenterY", now_ms, 0);
            im_fixed sx = im_states_sample_fixed(sm, tn, "ScaleX", now_ms, IM_ONE);
            im_fixed sy = im_states_sample_fixed(sm, tn, "ScaleY", now_ms, IM_ONE);
            im_fixed skx = im_states_sample_fixed(sm, tn, "SkewX", now_ms, 0);
            im_fixed sky = im_states_sample_fixed(sm, tn, "SkewY", now_ms, 0);
            im_fixed rot = im_states_sample_fixed(sm, tn, "Rotation", now_ms, 0);
            im_fixed tx = im_states_sample_fixed(sm, tn, "TranslateX", now_ms, 0);
            im_fixed ty = im_states_sample_fixed(sm, tn, "TranslateY", now_ms, 0);
            if (sx != IM_ONE || sy != IM_ONE || skx != 0 || sky != 0 || rot != 0 || tx != 0 || ty != 0) {
                changed = im_transform_composite(cx, cy, sx, sy, skx, sky, rot, tx, ty, &tr);
            }
        }
        if (changed) {
            im_node_set_render_transform(root, &tr, root->render_transform_origin_x, root->render_transform_origin_y);
        } else if (root->has_render_transform) {
            im_node_clear_render_transform(root);
        }
    }
    for (uint32_t i = 0; i < root->child_count; i++) {
        im_states_apply_to_tree(sm, root->children[i], now_ms);
    }
}

void im_states_draw_button(const im_state_machine *sm, im_surface *surface, im_rect bounds, uint32_t now_ms) {
    if (!sm || !surface) return;

    enum im_brush_id bg_brush = IM_BRUSH_COUNT;
    uint32_t bg_argb = 0u;
    bool bg_transitioning = false;
    if (!im_states_sample_brush(sm, "ContentPresenter", "Background", now_ms, &bg_brush, &bg_argb, &bg_transitioning)) {
        return;
    }

    enum im_brush_id border_brush = IM_BRUSH_COUNT;
    uint32_t border_argb = 0u;
    bool border_transitioning = false;
    bool has_border = im_states_sample_brush(sm, "ContentPresenter", "BorderBrush", now_ms,
        &border_brush, &border_argb, &border_transitioning);

    bool under_border = false;
    im_template_value sizing;
    if (im_states_sample_value(sm, NULL, "BackgroundSizing", now_ms, &sizing) &&
        sizing.kind == IM_TVAL_ENUM_STR && str_eq(sizing.text, "OuterBorderEdge")) {
        under_border = true;
    }

    im_surface local = *surface;
    const im_palette *wp = sm->window_palette ? sm->window_palette : &sm->palette;
    if (sm->res_node) {
        local.palette = im_resources_effective_palette(sm->res_node, wp);
    } else {
        local.palette = sm->palette;
    }

    im_rect fill_rect;
    im_radii fill_radii, stroke_radii;
    im_fixed thickness;
    im_button_geometry_for_node(&local, sm->res_node, wp, bounds, under_border, &fill_rect, &fill_radii, &stroke_radii, &thickness);

    uint32_t ov = 0u;
    if (bg_transitioning || bg_brush >= IM_BRUSH_COUNT) {
        im_fill_argb(&local, fill_rect, fill_radii, bg_argb);
    } else if (sm->res_node && im_resources_lookup_brush_override(sm->res_node, wp, bg_brush, &ov)) {
        im_fill_argb(&local, fill_rect, fill_radii, ov);
    } else {
        im_fill(&local, fill_rect, fill_radii, bg_brush);
    }
    if (has_border) {
        if (border_transitioning || border_brush >= IM_BRUSH_COUNT) {
            im_stroke_argb(&local, bounds, stroke_radii, thickness, border_argb);
        } else if (sm->res_node && im_resources_lookup_brush_override(sm->res_node, wp, border_brush, &ov)) {
            im_stroke_argb(&local, bounds, stroke_radii, thickness, ov);
        } else {
            im_stroke(&local, bounds, stroke_radii, thickness, border_brush);
        }
    }
}

static uint32_t apply_node_opacity(uint32_t argb, im_fixed opacity) {
    if (opacity >= IM_ONE) return argb;
    if (opacity <= 0) return 0u;
    uint32_t a = (argb >> 24) & 0xFFu;
    uint32_t sa = (uint32_t)(((uint64_t)a * (uint64_t)opacity + (IM_ONE / 2)) >> 16);
    return (sa << 24) | (argb & 0x00FFFFFFu);
}

static bool resolve_sdk_icon_path(im_icon_id id, im_path_data *out) {
    if (id == IM_ICON_CHECKMARK) { *out = im_path_sdk_checkmark(); return true; }
    if (id == IM_ICON_WINDOW_MAXIMIZE) { *out = im_path_sdk_window_maximize(); return true; }
    if (id == IM_ICON_WINDOW_RESTORE) { *out = im_path_sdk_window_restore(); return true; }
    return false;
}

bool im_states_draw_path_node(const im_state_machine *sm, const im_node *node, im_surface *surface,
                              im_allocator alloc, const im_template_path_seg *path_segs, uint32_t now_ms) {
    if (!sm || !node || !surface || node->kind != IM_ELEM_PATH) return true;
    if (node->visibility == IM_VIS_COLLAPSED || node->opacity <= 0) return true;
    int64_t w = (int64_t)node->absolute_rect.right - node->absolute_rect.left;
    int64_t h = (int64_t)node->absolute_rect.bottom - node->absolute_rect.top;
    if (w <= 0 || h <= 0) return true;

    im_fixed sc = (surface->scale > 0) ? surface->scale : IM_ONE;
    enum im_brush_id fill_brush = IM_BRUSH_COUNT, stroke_brush = IM_BRUSH_COUNT;
    uint32_t fill_argb = 0u, stroke_argb = 0u;
    bool fill_trans = false, stroke_trans = false;
    if (node->name && im_states_sample_brush(sm, node->name, "Fill", now_ms, &fill_brush, &fill_argb, &fill_trans)) {
        if (!fill_trans && fill_brush < IM_BRUSH_COUNT) {
            fill_argb = im_brush_over(&sm->palette, fill_brush, 0, h, sc);
        }
    } else if (node->path_has_fill) {
        fill_argb = (node->path_fill_brush < IM_BRUSH_COUNT)
            ? im_brush_over(&sm->palette, node->path_fill_brush, 0, h, sc)
            : node->path_fill_argb;
    }
    if (node->name && im_states_sample_brush(sm, node->name, "Stroke", now_ms, &stroke_brush, &stroke_argb, &stroke_trans)) {
        if (!stroke_trans && stroke_brush < IM_BRUSH_COUNT) {
            stroke_argb = im_brush_over(&sm->palette, stroke_brush, 0, h, sc);
        }
    } else if (node->path_has_stroke) {
        stroke_argb = (node->path_stroke_brush < IM_BRUSH_COUNT)
            ? im_brush_over(&sm->palette, node->path_stroke_brush, 0, h, sc)
            : node->path_stroke_argb;
    }
    bool has_fill = ((fill_argb >> 24) != 0u);
    bool has_stroke = ((stroke_argb >> 24) != 0u && node->path_stroke_thickness > 0);
    if (!has_fill && !has_stroke) return true;

    /* When both fill and stroke are present and Opacity < 1, composite through one opacity group
       so overlap pixels receive node->opacity once instead of twice. */
    bool use_group_opacity = (has_fill && has_stroke && node->opacity < IM_ONE);
    if (!use_group_opacity) {
        if (has_fill) fill_argb = apply_node_opacity(fill_argb, node->opacity);
        if (has_stroke) stroke_argb = apply_node_opacity(stroke_argb, node->opacity);
        has_fill = ((fill_argb >> 24) != 0u);
        has_stroke = ((stroke_argb >> 24) != 0u && node->path_stroke_thickness > 0);
        if (!has_fill && !has_stroke) return true;
    }

    /* Resolve dynamic Data from visual-state setters or caller bindings first (even on unnamed nodes);
       fall back to static node geometry. */
    im_icon_id icon_id = node->path_icon_id;
    uint32_t seg_start = node->path_seg_start;
    uint32_t seg_count = node->path_seg_count;
    uint8_t fill_rule = node->path_fill_rule;
    im_template_value dval;
    bool has_dyn = false;
    if (node->name && im_states_sample_value(sm, node->name, "Data", now_ms, &dval)) {
        has_dyn = true;
    } else if (node->path_data_binding != NULL) {
        bool found_b = false;
        for (uint32_t i = 0; i < sm->binding_count; i++) {
            if (str_eq(sm->bindings[i].name, node->path_data_binding)) {
                dval = sm->bindings[i].value;
                found_b = true;
                break;
            }
        }
        if (!found_b) {
            dval = (im_template_value){ IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, node->path_data_binding, NULL };
        }
        has_dyn = true;
    }
    if (has_dyn) {
        if (dval.kind == IM_TVAL_ICON) {
            icon_id = (im_icon_id)dval.a;
            seg_count = 0u;
        } else if (dval.kind == IM_TVAL_PATH && dval.b > 0) {
            icon_id = IM_ICON_NONE;
            seg_start = (uint32_t)dval.a;
            seg_count = (uint32_t)dval.b;
            fill_rule = (uint8_t)dval.c;
        } else if (dval.kind == IM_TVAL_NULL || dval.kind == IM_TVAL_NONE || dval.kind == IM_TVAL_TEMPLATE_BINDING) {
            /* Unbound or explicitly cleared Data paints nothing. */
            return true;
        }
    }

    im_transform dev_tr;
    if (!im_node_device_transform(node, sc, &dev_tr)) return true;

    const im_template_path_seg *segs = path_segs ? path_segs : sm->tables.path_segs;
    im_path_data pd = { NULL, 0u, IM_FILL_EVEN_ODD };
    if (resolve_sdk_icon_path(icon_id, &pd)) {
        /* SDK-owned icon path ready. */
    } else if (segs && seg_count > 0u) {
        /* Zero-copy bind: im_template_path_seg is typedef'd directly to im_path_seg. */
        pd = (im_path_data){ segs + seg_start, seg_count, (im_fill_rule)fill_rule };
    } else {
        return true;
    }

    im_stroke_style st = {
        node->path_stroke_thickness,
        (im_line_cap)node->path_start_cap,
        (im_line_cap)node->path_end_cap,
        IM_CAP_FLAT,
        (im_line_join)node->path_line_join,
        (node->path_miter_limit > 0) ? node->path_miter_limit : (10 * IM_ONE),
        NULL, 0u, 0
    };

    bool ok = true;
    im_layer_ctx tmp_layers;
    bool created_layers = false;
    bool pushed_layer = false;
    if (use_group_opacity) {
        uint8_t op8 = (uint8_t)(((uint64_t)node->opacity * 255u + (IM_ONE / 2)) >> 16);
        im_rect full_bounds = { 0, 0, (im_fixed)(surface->width * IM_ONE), (im_fixed)(surface->height * IM_ONE) };
        if (!surface->layers) {
            size_t cap = (size_t)surface->width * (size_t)surface->height * 4u + 4096u;
            if (!im_layer_init(&tmp_layers, surface, alloc, cap)) return false;
            created_layers = true;
        }
        im_layer_result lr = im_layer_push(surface->layers, full_bounds, op8, false, (im_radii){0, 0, 0, 0});
        if (lr == IM_LAYER_ERR) {
            if (created_layers) im_layer_destroy(&tmp_layers);
            return false;
        }
        pushed_layer = true;
    }

    const im_transform *saved = surface->transform;
    surface->transform = &dev_tr;
    ok = im_path_draw_stretched(surface, alloc, &pd, (im_rect){0, 0, (im_fixed)w, (im_fixed)h},
                                node->path_stretch, has_fill ? fill_argb : 0u,
                                has_stroke ? &st : NULL, has_stroke ? stroke_argb : 0u);
    surface->transform = saved;

    if (pushed_layer) {
        im_layer_pop(surface->layers);
    }
    if (created_layers) {
        im_layer_destroy(&tmp_layers);
    }
    return ok;
}
