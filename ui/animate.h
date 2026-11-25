/* Impossible UI: fixed-point animation curves, keyframe tracks and brush transitions. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "template.h"
#include "theme.h"

/* Standard WinUI animation durations in milliseconds (pinned ThemeAnimations.cpp / PVL). */
#define IM_BRUSH_TRANSITION_DURATION_MS 83u
#define IM_REPOSITION_DURATION_MS 367u
#define IM_FADE_IN_DURATION_MS 167u
#define IM_FADE_OUT_DURATION_MS 167u
#define IM_DRAG_OVER_DURATION_MS 200u
#define IM_DRAG_ITEM_DURATION_MS 240u
#define IM_DROP_TARGET_DURATION_MS 240u

/* WinUI PVL Reposition/DragOver deceleration spline (0.1, 0.9, 0.2, 1.0) in 16.16 fixed point. */
#define IM_PVL_SPLINE_X1 ((im_fixed)6554)
#define IM_PVL_SPLINE_Y1 ((im_fixed)58982)
#define IM_PVL_SPLINE_X2 ((im_fixed)13107)
#define IM_PVL_SPLINE_Y2 ((im_fixed)65536)

/* WinUI DragItemThemeAnimation source scale (1.05) and opacity multiplier (0.65) in 16.16 fixed point. */
#define IM_DRAG_ITEM_SCALE ((im_fixed)68813)
#define IM_DRAG_ITEM_OPACITY_MULT ((im_fixed)42598)

/* WinUI DropTargetItemThemeAnimation target scale (0.95) in 16.16 fixed point. */
#define IM_DROP_TARGET_SCALE ((im_fixed)62259)

/* In-flight straight-ARGB brush transition state (e.g. Button Background 83 ms fade). */
typedef struct im_brush_transition {
    bool active;
    bool is_solid_color;
    enum im_brush_id target_brush;
    uint32_t from_argb;
    uint32_t to_argb;
    uint32_t start_ms;
    uint32_t duration_ms;
} im_brush_transition;

/* Unsigned 32-bit wrap-safe elapsed milliseconds since start_ms (valid for elapsed < 2^31 ms). */
uint32_t im_anim_elapsed_ms(uint32_t start_ms, uint32_t now_ms);

/* Evaluate a XAML cubic Bezier KeySpline at exact ratio num / den without pre-quantizing time to 16.16. */
im_fixed im_spline_eval_ratio(im_fixed x1, im_fixed y1, im_fixed x2, im_fixed y2, uint32_t num, uint32_t den);

/* Evaluate a XAML cubic Bezier KeySpline at 16.16 progress t in [0, IM_ONE]. */
im_fixed im_spline_eval(im_fixed x1, im_fixed y1, im_fixed x2, im_fixed y2, im_fixed t);

/* Linearly interpolate two 16.16 fixed-point values at exact ratio num / den without signed overflow. */
im_fixed im_lerp_fixed_ratio(im_fixed from, im_fixed to, uint32_t num, uint32_t den);

/* Linearly interpolate two 16.16 fixed-point values at 16.16 progress in [0, IM_ONE]. */
im_fixed im_lerp_fixed(im_fixed from, im_fixed to, im_fixed progress);

/* Linearly interpolate two straight 0xAARRGGBB colors across all four channels at exact ratio num / den. */
uint32_t im_lerp_argb_ratio(uint32_t from_argb, uint32_t to_argb, uint32_t num, uint32_t den);

/* Linearly interpolate two straight 0xAARRGGBB colors across all four channels at 16.16 progress. */
uint32_t im_lerp_argb(uint32_t from_argb, uint32_t to_argb, im_fixed progress);

/* Resolve a compiled template value (IM_TVAL_COLOR or IM_TVAL_BRUSH) to a straight 0xAARRGGBB color. */
uint32_t im_template_value_to_argb(const im_template_value *val, const im_palette *palette);

/* Resolve a compiled template value (IM_TVAL_FIXED, IM_TVAL_THEME_VALUE, or IM_TVAL_INT) to 16.16 fixed point. */
im_fixed im_template_value_to_fixed(const im_template_value *val, const im_palette *palette, im_fixed fallback);

/* Return the effective duration in milliseconds for a compiled storyboard animation entry. */
uint32_t im_anim_duration_ms(const im_template_anim *anim, const im_template_keyframe *keyframes);

/* Evaluate a numeric storyboard track (Double, DoubleKeyFrames, or theme animation) at now_ms. */
im_fixed im_anim_eval_fixed(const im_template_anim *anim, const im_template_keyframe *keyframes, const im_palette *palette,
    im_fixed start_val, uint32_t start_ms, uint32_t now_ms, bool reduced_motion);

/* Evaluate a ColorAnimationUsingKeyFrames track (straight ARGB) at now_ms. */
uint32_t im_anim_eval_color(const im_template_anim *anim, const im_template_keyframe *keyframes, const im_palette *palette,
    uint32_t start_argb, uint32_t start_ms, uint32_t now_ms, bool reduced_motion);

/* Evaluate an ObjectAnimationUsingKeyFrames track at now_ms; returns false if before the first keyframe. */
bool im_anim_eval_object(const im_template_anim *anim, const im_template_keyframe *keyframes,
    uint32_t start_ms, uint32_t now_ms, bool reduced_motion, im_template_value *out_val);

/* Initialize a brush transition in a settled state with its initial brush or solid color and duration. */
void im_brush_transition_init(im_brush_transition *bt, bool is_solid_color, enum im_brush_id initial_brush, uint32_t initial_argb, uint32_t duration_ms);

/* Retarget a brush transition at now_ms, seamlessly starting from the current presentation ARGB. */
void im_brush_transition_set(im_brush_transition *bt, bool is_solid_color, enum im_brush_id new_brush, uint32_t new_argb, uint32_t now_ms, bool reduced_motion);

/* Sample the straight 0xAARRGGBB color of a brush transition at now_ms. */
uint32_t im_brush_transition_sample(const im_brush_transition *bt, uint32_t now_ms, bool reduced_motion);

/* Return true while a brush transition is still interpolating at now_ms. */
bool im_brush_transition_is_active(const im_brush_transition *bt, uint32_t now_ms, bool reduced_motion);
