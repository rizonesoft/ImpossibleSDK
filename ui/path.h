/* Impossible UI: fixed-point vector paths, arcs, strokes and analytic fill rasterization. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "transform.h"
#include "tree.h"

/* Path interior fill rule matching WinUI FillRule. */
typedef enum im_fill_rule {
    IM_FILL_EVEN_ODD = 0,
    IM_FILL_NON_ZERO = 1
} im_fill_rule;

/* Stroke line cap shape matching WinUI PenLineCap. */
typedef enum im_line_cap {
    IM_CAP_FLAT = 0,
    IM_CAP_SQUARE = 1,
    IM_CAP_ROUND = 2,
    IM_CAP_TRIANGLE = 3
} im_line_cap;

/* Stroke line join shape matching WinUI PenLineJoin. */
typedef enum im_line_join {
    IM_JOIN_MITER = 0,
    IM_JOIN_BEVEL = 1,
    IM_JOIN_ROUND = 2
} im_line_join;

/* Vector path segment command kind. */
typedef enum im_path_cmd {
    IM_PATH_MOVE = 0,
    IM_PATH_LINE = 1,
    IM_PATH_QUAD = 2,
    IM_PATH_CUBIC = 3,
    IM_PATH_ARC = 4,
    IM_PATH_CLOSE = 5
} im_path_cmd;

/* One command in a fixed-point vector path.
   MOVE/LINE use (x1, y1); QUAD uses ctrl (x1, y1), end (x2, y2);
   CUBIC uses ctrl1 (x1, y1), ctrl2 (x2, y2), end (x3, y3);
   ARC uses radii (x1, y1), end (x2, y2), x-axis angle_deg, large_arc, sweep;
   CLOSE uses no coordinates. */
typedef struct im_path_seg {
    im_path_cmd cmd;
    im_fixed x1, y1;
    im_fixed x2, y2;
    im_fixed x3, y3;
    im_fixed angle_deg;
    bool large_arc;
    bool sweep;
} im_path_seg;

/* Read-only view of a fixed-point vector path (usable with static compiled tables without allocation). */
typedef struct im_path_data {
    const im_path_seg *segs;
    uint32_t count;
    im_fill_rule fill_rule;
} im_path_data;

/* Growable fixed-point vector path backed by a caller-supplied allocator. */
typedef struct im_path {
    im_allocator alloc;
    im_path_seg *segs;
    uint32_t count;
    uint32_t cap;
    im_fill_rule fill_rule;
} im_path;

/* Stroke expansion parameters matching WinUI Shape stroke properties.
   dash_array and dash_offset are expressed in multiples of thickness (matching WinUI StrokeDashArray). */
typedef struct im_stroke_style {
    im_fixed thickness;
    im_line_cap start_cap;
    im_line_cap end_cap;
    im_line_cap dash_cap;
    im_line_join line_join;
    im_fixed miter_limit; /* <= 0 defaults to WinUI 10.0 */
    const im_fixed *dash_array;
    uint32_t dash_count;
    im_fixed dash_offset;
} im_stroke_style;

/* Per-pixel shader callback receiving local-space Q30 coordinates and returning straight 0xAARRGGBB. */
typedef uint32_t (*im_path_shader_fn)(void *ctx, int64_t lx_q30, int64_t ly_q30);

/* Initialise an empty growable path with a caller-supplied allocator and fill rule. */
bool im_path_init(im_path *path, im_allocator alloc, im_fill_rule fill_rule);

/* Release all memory owned by path. */
void im_path_destroy(im_path *path);

/* Remove all segments from path while keeping its allocated buffer. */
void im_path_clear(im_path *path);

/* Return a read-only im_path_data view of path. */
im_path_data im_path_view(const im_path *path);

/* Append a MoveTo command starting a new subpath at (x, y). */
bool im_path_move_to(im_path *path, im_fixed x, im_fixed y);

/* Append a LineTo command to (x, y). */
bool im_path_line_to(im_path *path, im_fixed x, im_fixed y);

/* Append a quadratic Bezier curve with control point (cx, cy) and end point (x, y). */
bool im_path_quad_to(im_path *path, im_fixed cx, im_fixed cy, im_fixed x, im_fixed y);

/* Append a cubic Bezier curve with control points (c1x, c1y), (c2x, c2y) and end point (x, y). */
bool im_path_cubic_to(im_path *path, im_fixed c1x, im_fixed c1y, im_fixed c2x, im_fixed c2y, im_fixed x, im_fixed y);

/* Append an SVG/WinUI endpoint-parameterized elliptical arc to (x, y). */
bool im_path_arc_to(im_path *path, im_fixed rx, im_fixed ry, im_fixed x_axis_rot_deg,
                    bool large_arc, bool sweep, im_fixed x, im_fixed y);

/* Append an elliptical arc parameterized by center (cx, cy), radii (rx, ry), start_deg and sweep_deg
   (16.16 degrees, clockwise in screen coordinates: 0 = +X, 90 = +Y), split into <= 45-degree cubic
   Bezier segments using exact Q30 trigonometry. If move_to_start is true, emits MoveTo at the start
   point; otherwise LineTo connects from the current point. */
bool im_path_arc_angles(im_path *path, im_fixed cx, im_fixed cy, im_fixed rx, im_fixed ry,
                        im_fixed start_deg, im_fixed sweep_deg, bool move_to_start);

/* Append a ClosePath command closing the current subpath. */
bool im_path_close(im_path *path);

/* Compute the local axis-aligned bounding box of path. Returns false if path is empty. */
bool im_path_bounds(const im_path_data *path, im_rect *out_bounds);

/* Return the SDK-owned geometric checkmark path (normalized to [0, 16] x [0, 16]). */
im_path_data im_path_sdk_checkmark(void);

/* Return the SDK-owned geometric window maximize icon path (normalized to [0, 10] x [0, 10]). */
im_path_data im_path_sdk_window_maximize(void);

/* Return the SDK-owned geometric window restore icon path (normalized to [0, 10] x [0, 10]). */
im_path_data im_path_sdk_window_restore(void);

/* Fill path with a straight 0xAARRGGBB color using 16-subscanline analytic horizontal coverage,
   honouring surface->transform and surface->layers. Returns false on allocation failure without
   modifying surface pixels. */
bool im_path_fill_argb(im_surface *surface, im_allocator alloc, const im_path_data *path, uint32_t argb);

/* Fill path with a per-pixel local-space shader callback, honouring surface->transform and
   surface->layers. Returns false on allocation failure without modifying surface pixels. */
bool im_path_fill_shaded(im_surface *surface, im_allocator alloc, const im_path_data *path,
                         im_path_shader_fn shader, void *shader_ctx);

/* Stroke path in local space (before surface->transform) with style and straight 0xAARRGGBB color,
   honouring caps, joins, miter limit, dashes, surface->transform and surface->layers.
   Returns false on allocation failure without modifying surface pixels. */
bool im_path_stroke_argb(im_surface *surface, im_allocator alloc, const im_path_data *path,
                         const im_stroke_style *style, uint32_t argb);

/* Stroke path in local space (before surface->transform) with style and a per-pixel local-space
   shader callback, honouring surface->transform and surface->layers.
   Returns false on allocation failure without modifying surface pixels. */
bool im_path_stroke_shaded(im_surface *surface, im_allocator alloc, const im_path_data *path,
                           const im_stroke_style *style, im_path_shader_fn shader, void *shader_ctx);

/* Draw a compiled path stretched into dst_rect (stretch_mode matches im_stretch: 0=None, 1=Fill,
   2=Uniform, 3=UniformToFill) with optional straight ARGB fill and optional stroke. */
bool im_path_draw_stretched(im_surface *surface, im_allocator alloc, const im_path_data *path,
                            im_rect dst_rect, int stretch_mode, uint32_t fill_argb,
                            const im_stroke_style *stroke_style, uint32_t stroke_argb);
