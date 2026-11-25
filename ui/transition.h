/* Impossible UI: WinUI PVL theme transitions (entrance, popup, add/delete, reposition, reorder, content, drill). Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "animate.h"
#include "dirty.h"
#include "template.h"
#include "transform.h"
#include "tree.h"

/* Named WinUI theme transition operations. */
typedef enum im_transition_kind {
    IM_TRANS_ENTRANCE = 0,
    IM_TRANS_POPUP_OPEN,
    IM_TRANS_POPUP_CLOSE,
    IM_TRANS_ADD,
    IM_TRANS_DELETE,
    IM_TRANS_REPOSITION,
    IM_TRANS_REORDER,
    IM_TRANS_CONTENT,
    IM_TRANS_DRILL_IN,
    IM_TRANS_DRILL_OUT,
    IM_TRANS_KIND_COUNT
} im_transition_kind;

/* Static WinUI PVL timing and curve specification for one named theme transition. */
typedef struct im_transition_spec {
    im_transition_kind kind;
    const char *xaml_name;
    uint32_t move_duration_ms;
    uint32_t opacity_duration_ms;
    uint32_t stagger_delay_ms;
    uint32_t stagger_max_items;
    im_fixed default_from_x;
    im_fixed default_from_y;
    im_fixed from_scale;
    im_fixed to_scale;
    im_fixed from_opacity;
    im_fixed to_opacity;
    im_fixed origin_x;
    im_fixed origin_y;
    im_fixed spline_x1, spline_y1, spline_x2, spline_y2;
} im_transition_spec;

/* Sampled presentation values of a theme transition at one instant. */
typedef struct im_transition_sample {
    im_fixed translate_x;
    im_fixed translate_y;
    im_fixed scale;
    im_fixed opacity;
    im_fixed origin_x;
    im_fixed origin_y;
    bool active;
} im_transition_sample;

/* In-flight theme transition state with interruption/reversal support and live reduced-motion snapping. */
typedef struct im_transition {
    im_transition_kind kind;
    bool active;
    bool reduced_motion;
    uint32_t start_ms;
    uint32_t delay_ms;
    uint32_t move_duration_ms;
    uint32_t opacity_duration_ms;
    im_fixed from_x, to_x;
    im_fixed from_y, to_y;
    im_fixed from_scale, to_scale;
    im_fixed from_opacity, to_opacity;
    im_fixed origin_x, origin_y;
    im_fixed spline_x1, spline_y1, spline_x2, spline_y2;
} im_transition;

/* Maximum simultaneous outgoing exit visuals (detached/destroyed nodes finishing a delete or content-exit transition). */
#define IM_MAX_EXIT_GHOSTS 16

/* Self-contained exit-visual snapshot that owns its geometry/appearance independently of the detached im_node. */
typedef struct im_transition_ghost {
    bool active;
    uint32_t tag_id;
    im_rect base_bounds;
    uint32_t fill_argb;
    im_fixed authored_opacity;
    bool had_prev_dev_rect;
    int32_t prev_dev_left, prev_dev_top, prev_dev_right, prev_dev_bottom;
    im_transition transition;
} im_transition_ghost;

/* Manager for detached exit-visual ghosts (safe across immediate im_tree_destroy_subtree, reinsertion, and cancellation). */
typedef struct im_transition_ghosts {
    im_transition_ghost items[IM_MAX_EXIT_GHOSTS];
} im_transition_ghosts;

/* Look up the WinUI PVL specification for a theme transition kind. */
const im_transition_spec *im_transition_spec_by_kind(im_transition_kind kind);

/* Look up the WinUI PVL specification by XAML element name (e.g. "EntranceThemeTransition"). Returns NULL if unknown. */
const im_transition_spec *im_transition_spec_by_name(const char *xaml_name);

/* Start a theme transition at now_ms (with optional stagger_index and reduced_motion). */
void im_transition_start(im_transition *tr, im_transition_kind kind, uint32_t now_ms,
                         uint32_t stagger_index, bool reduced_motion);

/* Start a theme transition with explicit from/to translation offsets (e.g. Reposition/Reorder/Entrance/Content/Popup). */
void im_transition_start_offset(im_transition *tr, im_transition_kind kind,
                                im_fixed from_x, im_fixed from_y, im_fixed to_x, im_fixed to_y,
                                uint32_t now_ms, uint32_t stagger_index, bool reduced_motion);

/* Sample translation, scale, origin, and opacity at now_ms. Returns active status. */
im_transition_sample im_transition_sample_at(const im_transition *tr, uint32_t now_ms);

/* Return true while the transition is still animating at now_ms. */
bool im_transition_is_active(const im_transition *tr, uint32_t now_ms);

/* Retarget a running transition smoothly from its exact current sampled state at now_ms (zero jump). */
void im_transition_retarget(im_transition *tr, im_fixed new_to_x, im_fixed new_to_y,
                            im_fixed new_to_scale, im_fixed new_to_opacity, uint32_t now_ms);

/* Reverse a running transition smoothly from its exact current sampled state toward its original start at now_ms. */
void im_transition_reverse(im_transition *tr, uint32_t now_ms);

/* Toggle reduced motion live; enabling snaps an active transition to its endpoint, disabling leaves settled ones settled. */
bool im_transition_set_reduced_motion(im_transition *tr, bool reduced_motion, uint32_t now_ms);

/* Sample tr at now_ms and apply its distinct presentation transform & opacity to node (clearing on settlement).
   Returns true while active (caller keeps frame scheduler ticking), false when settled or on composition failure. */
bool im_transition_apply_to_node(im_transition *tr, im_node *node, uint32_t now_ms);

/* Trigger EntranceThemeTransition from owner's compiled transition_decls; returns false if undeclared. */
bool im_transition_on_enter(const im_node *owner, im_transition *tr, uint32_t now_ms, uint32_t stagger_index, bool reduced_motion);

/* Trigger AddDeleteThemeTransition (add) from owner's compiled transition_decls; returns false if undeclared. */
bool im_transition_on_add(const im_node *owner, im_transition *tr, uint32_t now_ms, uint32_t stagger_index, bool reduced_motion);

/* Trigger RepositionThemeTransition (or AddDelete reposition) from old_rect to new_rect; returns false if undeclared. */
bool im_transition_on_reposition(const im_node *owner, im_transition *tr, im_rect old_rect, im_rect new_rect,
                                 uint32_t now_ms, uint32_t stagger_index, bool reduced_motion);

/* Trigger ReorderThemeTransition from old_rect to new_rect; returns false if undeclared. */
bool im_transition_on_reorder(const im_node *owner, im_transition *tr, im_rect old_rect, im_rect new_rect,
                              uint32_t now_ms, bool reduced_motion);

/* Trigger PopupThemeTransition (open) from owner's compiled transition_decls; returns false if undeclared. */
bool im_transition_on_popup_open(const im_node *owner, im_transition *tr, uint32_t now_ms, bool reduced_motion);

/* Trigger ContentThemeTransition from owner's compiled transition_decls; returns false if undeclared. */
bool im_transition_on_content_change(const im_node *owner, im_transition *tr, uint32_t now_ms, bool reduced_motion);

/* Trigger NavigationThemeTransition (DrillIn forward / DrillOut back) from owner's declarations. */
bool im_transition_on_navigate(const im_node *owner, im_transition *tr, bool is_back, uint32_t now_ms, bool reduced_motion);

/* Snapshot an outgoing node into ghosts for a delete/exit transition (safe even if outgoing is immediately destroyed). */
bool im_transition_on_delete(const im_node *owner, im_transition_ghosts *ghosts, const im_node *outgoing,
                             uint32_t tag_id, uint32_t fill_argb, uint32_t now_ms, bool reduced_motion);

/* Advance all active exit ghosts at now_ms, appending old/new device-pixel dirty rects to dl. Returns true if any active. */
bool im_transition_ghosts_step(im_transition_ghosts *ghosts, im_dirty_list *dl, im_fixed scale, uint32_t now_ms);

/* Cancel an exit ghost by tag_id (e.g. on item reinsertion or explicit cancellation), emitting its cleanup dirty rect. */
bool im_transition_ghosts_cancel(im_transition_ghosts *ghosts, uint32_t tag_id, im_dirty_list *dl);
