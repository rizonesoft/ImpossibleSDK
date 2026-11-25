/* Impossible UI: compiled WinUI visual state groups, transitions and setters. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "animate.h"
#include "draw.h"
#include "template.h"
#include "theme.h"
#include "tree.h"

/* Maximum number of independent visual state groups in a single control template. */
#define IM_MAX_STATE_GROUPS 8

/* Maximum number of caller-supplied binding values (e.g. TemplateSettings.* or PlaceholderForeground). */
#define IM_MAX_STATE_BINDINGS 16

/* Maximum number of simultaneously active state/transition property tracks across all groups. */
#define IM_MAX_STATE_ENTRIES 64

/* External table bundle used when initializing a state machine on a custom compiled template. */
typedef struct im_template_tables {
    const im_template_setter *setters;
    const im_template_element *elements;
    const im_template_prop *props;
    const im_template_group *groups;
    const im_template_state *states;
    const im_template_transition *transitions;
    const im_template_anim *anims;
    const im_template_keyframe *keyframes;
    const im_template_path_seg *path_segs;
} im_template_tables;

/* One caller-supplied binding value attached to a state machine. */
typedef struct im_state_binding {
    const char *name;
    im_template_value value;
} im_state_binding;

/* Kind of active property entry held by a visual state machine. */
typedef enum im_state_entry_kind {
    IM_SENTRY_SETTER = 0,
    IM_SENTRY_ANIM,
    IM_SENTRY_GEN_FIXED,
    IM_SENTRY_GEN_COLOR
} im_state_entry_kind;

/* One active setter, storyboard track, or generated return interpolation owned by a state group. */
typedef struct im_state_entry {
    im_state_entry_kind kind;
    uint8_t group_index;
    bool from_transition;
    bool snapped;
    const char *target;
    const char *property;
    const im_template_anim *anim;
    im_template_value setter_val;
    im_fixed start_fixed;
    im_fixed end_fixed;
    uint32_t start_argb;
    uint32_t end_argb;
    uint32_t start_ms;
    uint32_t duration_ms;
} im_state_entry;

/* Runtime visual state machine for a compiled WinUI control template. */
typedef struct im_state_machine {
    const im_template *tmpl;
    im_template_tables tables;
    im_palette palette;
    bool reduced_motion;

    int32_t group_state[IM_MAX_STATE_GROUPS];
    uint32_t group_start_ms[IM_MAX_STATE_GROUPS];
    uint32_t group_trans_dur_ms[IM_MAX_STATE_GROUPS];
    bool group_trans_snapped[IM_MAX_STATE_GROUPS];

    im_state_entry entries[IM_MAX_STATE_ENTRIES];
    uint32_t entry_count;

    im_state_binding bindings[IM_MAX_STATE_BINDINGS];
    uint32_t binding_count;

    bool has_bg_transition;
    const char *bg_target;
    im_brush_transition bg_transition;

    /* Optional subtree resource context (node + window palette) for per-subtree theme and named overrides. */
    const im_node *res_node;
    const im_palette *window_palette;
} im_state_machine;

/* Initialize a state machine for a compiled template from ui/template_data.c. */
void im_states_init(im_state_machine *sm, im_template_id id, const im_palette *palette);

/* Initialize a state machine for a custom template and tables (for custom fixtures and tests). */
void im_states_init_custom(im_state_machine *sm, const im_template *tmpl, const im_template_tables *tables, const im_palette *palette);

/* Attach a subtree node and window palette so every state brush/value resolves through ui/resources.c. */
void im_states_set_resource_context(im_state_machine *sm, const im_node *node, const im_palette *window_palette);

/* Refresh a state machine's effective palette and any in-flight BrushTransition target after a theme or override change.
   Returns true if the effective palette or active transition target changed. */
bool im_states_refresh_theme(im_state_machine *sm, const im_palette *window_palette, uint32_t now_ms);

/* Set or update a caller-supplied binding (e.g. TemplateSettings.KnobOffToOnOffset or PlaceholderForeground). */
bool im_states_set_binding(im_state_machine *sm, const char *name, im_template_value value);

/* Remove a caller-supplied binding so fallback resolution applies. */
void im_states_clear_binding(im_state_machine *sm, const char *name);

/* Enable or disable reduced-motion mode; enabling immediately snaps in-flight animations and transitions. */
void im_states_set_reduced_motion(im_state_machine *sm, bool reduced_motion, uint32_t now_ms);

/* Transition the matching state group to state_name at now_ms; returns false on unknown or unchanged state. */
bool im_states_go_to(im_state_machine *sm, const char *state_name, uint32_t now_ms);

/* Return the active state name in group_index, or NULL if no state has been entered in that group. */
const char *im_states_current(const im_state_machine *sm, uint32_t group_index);

/* Return true if any storyboard track, generated transition, or brush transition is active at now_ms. */
bool im_states_is_animating(const im_state_machine *sm, uint32_t now_ms);

/* Sample a typed template value for (target, property) at now_ms, resolving bindings and fallbacks. */
bool im_states_sample_value(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, im_template_value *out_val);

/* Sample a 16.16 fixed-point property for (target, property) at now_ms, or fallback if absent. */
im_fixed im_states_sample_fixed(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, im_fixed fallback);

/* Sample a straight 0xAARRGGBB color for (target, property) at now_ms, or fallback if absent. */
uint32_t im_states_sample_color(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, uint32_t fallback);

/* Sample a brush property for (target, property) at now_ms, reporting whether a BrushTransition is active. */
bool im_states_sample_brush(const im_state_machine *sm, const char *target, const char *property,
    uint32_t now_ms, enum im_brush_id *out_brush, uint32_t *out_argb, bool *out_is_transitioning);

/* Apply active layout-affecting visual-state overrides to a template element tree without losing local values. */
void im_states_apply_to_tree(const im_state_machine *sm, im_node *root, uint32_t now_ms);

/* Draw a Button using solely the state machine's resolved Background, BorderBrush, and BackgroundSizing. */
void im_states_draw_button(const im_state_machine *sm, im_surface *surface, im_rect bounds, uint32_t now_ms);

/* Draw a compiled IM_ELEM_PATH node using its arranged bounds, Stretch, Fill brush, Opacity,
   Visibility, and node device transform (mapping IM_ICON_CHECKMARK to im_path_sdk_checkmark). */
bool im_states_draw_path_node(const im_state_machine *sm, const im_node *node, im_surface *surface,
                              im_allocator alloc, const im_template_path_seg *path_segs, uint32_t now_ms);
