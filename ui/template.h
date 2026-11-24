/* Impossible UI: compiled WinUI control template tables. Portable, no floats, no OS calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "theme.h"

/* Portable SDK icon identifiers mapped from WinUI SymbolThemeFontFamily glyphs. */
typedef enum im_icon_id {
    IM_ICON_NONE = 0,
    IM_ICON_DISMISS,
    IM_ICON_CHECKMARK,
    IM_ICON_COUNT
} im_icon_id;

/* Element kinds supported by compiled WinUI control templates. */
typedef enum im_element_kind {
    IM_ELEM_GRID = 0,
    IM_ELEM_BORDER,
    IM_ELEM_CONTENT_PRESENTER,
    IM_ELEM_RECTANGLE,
    IM_ELEM_TEXT_BLOCK,
    IM_ELEM_BUTTON,
    IM_ELEM_SCROLL_VIEWER,
    IM_ELEM_THUMB,
    IM_ELEM_FONT_ICON,
    IM_ELEM_LIST_VIEW_ITEM_PRESENTER,
    IM_ELEM_ITEMS_STACK_PANEL,
    IM_ELEM_ITEMS_PRESENTER,
    IM_ELEM_ROW_DEFINITION,
    IM_ELEM_COLUMN_DEFINITION,
    IM_ELEM_TRANSLATE_TRANSFORM,
    IM_ELEM_SCALE_TRANSFORM,
    IM_ELEM_COMPOSITE_TRANSFORM,
    IM_ELEM_RECTANGLE_GEOMETRY,
    IM_ELEM_BRUSH_TRANSITION,
    IM_ELEM_ADD_DELETE_THEME_TRANSITION,
    IM_ELEM_CONTENT_THEME_TRANSITION,
    IM_ELEM_REORDER_THEME_TRANSITION,
    IM_ELEM_ENTRANCE_THEME_TRANSITION,
    IM_ELEM_COUNT
} im_element_kind;

/* Discriminator for a compiled template property, setter or keyframe value. */
typedef enum im_value_kind {
    IM_TVAL_NONE = 0,
    IM_TVAL_BRUSH,
    IM_TVAL_COLOR,
    IM_TVAL_THEME_VALUE,
    IM_TVAL_FIXED,
    IM_TVAL_QUAD,
    IM_TVAL_BOOL,
    IM_TVAL_INT,
    IM_TVAL_DURATION_MS,
    IM_TVAL_SPLINE,
    IM_TVAL_ICON,
    IM_TVAL_TEMPLATE_BINDING,
    IM_TVAL_BINDING,
    IM_TVAL_ENUM_STR,
    IM_TVAL_NULL,
    IM_TVAL_NESTED_TEMPLATE
} im_value_kind;

/* Storyboard animation kinds compiled from WinUI visual states and transitions. */
typedef enum im_anim_kind {
    IM_ANIM_OBJECT_KEYFRAMES = 0,
    IM_ANIM_DOUBLE,
    IM_ANIM_DOUBLE_KEYFRAMES,
    IM_ANIM_COLOR_KEYFRAMES,
    IM_ANIM_REPOSITION_THEME,
    IM_ANIM_FADE_IN_THEME,
    IM_ANIM_FADE_OUT_THEME,
    IM_ANIM_DRAG_ITEM_THEME,
    IM_ANIM_DRAG_OVER_THEME,
    IM_ANIM_DROP_TARGET_THEME
} im_anim_kind;

/* Keyframe interpolation kinds inside keyframe animations. */
typedef enum im_keyframe_kind {
    IM_KF_DISCRETE = 0,
    IM_KF_LINEAR_DOUBLE,
    IM_KF_SPLINE_DOUBLE,
    IM_KF_EASING_DOUBLE,
    IM_KF_LINEAR_COLOR
} im_keyframe_kind;

/* A typed value stored inside a compiled template table. */
typedef struct im_template_value {
    im_value_kind kind;
    int32_t a, b, c, d;
    const char *text;
    const char *fallback;
} im_template_value;

/* One named property on a compiled template element. */
typedef struct im_template_prop {
    const char *name;
    im_template_value value;
} im_template_prop;

/* One element in a compiled template tree (pre-order; parent == -1 for root). */
typedef struct im_template_element {
    im_element_kind kind;
    const char *name;
    int32_t parent;
    uint16_t prop_start;
    uint16_t prop_count;
} im_template_element;

/* One style setter or visual-state setter (target is NULL on style setters). */
typedef struct im_template_setter {
    const char *target;
    const char *property;
    im_template_value value;
} im_template_setter;

/* One keyframe in a keyframe animation (key_time_ms in ms, spline in 16.16 fixed point). */
typedef struct im_template_keyframe {
    im_keyframe_kind kind;
    uint32_t key_time_ms;
    im_template_value value;
    im_fixed spline[4];
} im_template_keyframe;

/* One animation entry inside a visual state or transition storyboard. */
typedef struct im_template_anim {
    im_anim_kind kind;
    const char *target_name;
    const char *target_property;
    uint32_t duration_ms;
    bool has_from;
    bool has_to;
    im_template_value from_val;
    im_template_value to_val;
    uint16_t kf_start;
    uint16_t kf_count;
} im_template_anim;

/* One named visual state within a visual state group. */
typedef struct im_template_state {
    const char *name;
    uint16_t setter_start;
    uint16_t setter_count;
    uint16_t anim_start;
    uint16_t anim_count;
} im_template_state;

/* One visual transition between states within a visual state group. */
typedef struct im_template_transition {
    const char *from_state;
    const char *to_state;
    uint32_t duration_ms;
    uint16_t anim_start;
    uint16_t anim_count;
} im_template_transition;

/* One visual state group owned by a compiled template. */
typedef struct im_template_group {
    const char *name;
    uint16_t state_start;
    uint16_t state_count;
    uint16_t trans_start;
    uint16_t trans_count;
} im_template_group;

/* Top-level compiled control style and template descriptor. */
typedef struct im_template {
    const char *key;
    const char *target_type;
    uint16_t style_setter_start;
    uint16_t style_setter_count;
    uint16_t elem_start;
    uint16_t elem_count;
    uint16_t group_start;
    uint16_t group_count;
} im_template;

#include "template_data.h"
