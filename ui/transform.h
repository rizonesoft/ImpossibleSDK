/* Impossible UI: 2D affine transforms in Q30 signed 64-bit fixed point. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "draw.h"

/* Fractional bit shift and unit (1.0) for im_transform matrix entries (Q30 signed 64-bit). */
#define IM_TR_SHIFT 30
#define IM_TR_ONE ((int64_t)1 << IM_TR_SHIFT)

/* Maximum representable magnitude for any im_transform coefficient (leaves headroom for 128-bit math).
   2^58 in Q30 supports +/- 268,435,456 logical units (while 2^58 * 2^60 = 2^118 < 2^127). */
#define IM_TR_MAX_COEFF ((int64_t)1 << 58)

/* 2D affine matrix in row-vector (WinUI/Direct2D) convention:
   x' = x * m11 + y * m21 + dx
   y' = x * m12 + y * m22 + dy
   All entries are Q30 fixed point (1.0 == IM_TR_ONE). */
typedef struct im_transform {
    int64_t m11;
    int64_t m12;
    int64_t m21;
    int64_t m22;
    int64_t dx;
    int64_t dy;
} im_transform;

/* Convert a 16.16 im_fixed value to Q30 fixed point without undefined signed left shifts. */
static inline int64_t im_fixed_to_q30(im_fixed v) {
    return (int64_t)v * ((int64_t)1 << (IM_TR_SHIFT - 16));
}

/* Signed 128-bit by 64-bit integer division helper (freestanding, no __divti3/__udivti3). */
int64_t im_sdiv128_i64(__int128 num, int64_t den);

/* Sine of angle_deg (16.16 degrees) in Q30 fixed point ([-IM_TR_ONE, IM_TR_ONE]). */
int64_t im_sin_deg(im_fixed angle_deg);

/* Cosine of angle_deg (16.16 degrees) in Q30 fixed point ([-IM_TR_ONE, IM_TR_ONE]). */
int64_t im_cos_deg(im_fixed angle_deg);

/* Tangent of angle_deg (16.16 degrees) in Q30 fixed point; returns false at singular 90+180k angles or overflow. */
bool im_tan_deg(im_fixed angle_deg, int64_t *out_q30);

/* Return the identity transform. */
im_transform im_transform_identity(void);

/* Return a translation transform by (tx, ty) in 16.16 logical/pixel units. */
im_transform im_transform_translate(im_fixed tx, im_fixed ty);

/* Return a scale transform by (sx, sy) in 16.16 fixed point about the origin. */
im_transform im_transform_scale(im_fixed sx, im_fixed sy);

/* Return a scale transform by (sx, sy) about (cx, cy). */
im_transform im_transform_scale_at(im_fixed sx, im_fixed sy, im_fixed cx, im_fixed cy);

/* Return a clockwise rotation transform by angle_deg (16.16 degrees) about the origin. */
im_transform im_transform_rotate(im_fixed angle_deg);

/* Return a clockwise rotation transform by angle_deg (16.16 degrees) about (cx, cy). */
im_transform im_transform_rotate_at(im_fixed angle_deg, im_fixed cx, im_fixed cy);

/* Construct a WinUI SkewTransform (angle_x_deg, angle_y_deg in 16.16 degrees); returns false if either tangent is singular. */
bool im_transform_skew(im_fixed angle_x_deg, im_fixed angle_y_deg, im_transform *out);

/* Construct a WinUI CompositeTransform in WinUI order: Scale -> Skew -> Rotate -> Translate about (center_x, center_y).
   Returns false without mutating out on singular skew or coefficient overflow. */
bool im_transform_composite(im_fixed center_x, im_fixed center_y,
                            im_fixed scale_x, im_fixed scale_y,
                            im_fixed skew_x_deg, im_fixed skew_y_deg,
                            im_fixed rotation_deg,
                            im_fixed translate_x, im_fixed translate_y,
                            im_transform *out);

/* Multiply a * b (row-vector convention: applies a first, then b). Returns false without mutating out on overflow. */
bool im_transform_multiply(const im_transform *a, const im_transform *b, im_transform *out);

/* Invert t using 128-bit determinant and adjugate math. Returns false without mutating out if singular or unrepresentable. */
bool im_transform_invert(const im_transform *t, im_transform *out);

/* Transform a 16.16 point (x, y) through t with round-to-nearest. Returns false without mutating outputs on overflow. */
bool im_transform_point(const im_transform *t, im_fixed x, im_fixed y, im_fixed *out_x, im_fixed *out_y);

/* Transform a Q30 point (qx, qy) through t into Q30 outputs. Returns false without mutating outputs on overflow. */
bool im_transform_point_q30(const im_transform *t, int64_t qx, int64_t qy, int64_t *out_qx, int64_t *out_qy);

/* Compute the outward-rounded axis-aligned bounding im_rect of all 4 corners of rect transformed by t.
   Returns false without mutating out on overflow or empty input rect. */
bool im_transform_rect_bounds(const im_transform *t, im_rect rect, im_rect *out);
