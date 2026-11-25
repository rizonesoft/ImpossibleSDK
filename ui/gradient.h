/* Impossible UI: fixed-point linear and radial gradients matching WinUI 3 brushes. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "draw.h"
#include "path.h"

/* Gradient spread behavior outside [0, 1] matching WinUI GradientSpreadMethod. */
typedef enum im_spread_method {
    IM_SPREAD_PAD = 0,
    IM_SPREAD_REFLECT = 1,
    IM_SPREAD_REPEAT = 2
} im_spread_method;

/* Coordinate mapping mode matching WinUI BrushMappingMode. */
typedef enum im_mapping_mode {
    IM_MAPPING_RELATIVE = 0, /* RelativeToBoundingBox: (0,0)=top-left, (1,1)=bottom-right of local bounds */
    IM_MAPPING_ABSOLUTE = 1  /* Absolute: local DIPs from (0,0) scaled by surface->scale */
} im_mapping_mode;

/* Color interpolation space for gradients (sRGB premultiplied matching WinUI Auto/RgbLinear default). */
typedef enum im_gradient_space {
    IM_GRAD_SPACE_SRGB = 0
} im_gradient_space;

/* One color stop of a linear or radial gradient. */
typedef struct im_gradient_stop {
    im_fixed offset; /* 16.16 in [0, IM_ONE] (stops outside [0, 1] are supported and sorted stably) */
    uint32_t argb;   /* straight 0xAARRGGBB */
} im_gradient_stop;

/* Linear gradient brush matching WinUI LinearGradientBrush. */
typedef struct im_linear_gradient {
    im_fixed start_x, start_y;
    im_fixed end_x, end_y;
    im_mapping_mode mapping;
    im_spread_method spread;
    im_gradient_space space;
    const im_gradient_stop *stops;
    uint32_t stop_count;
} im_linear_gradient;

/* Radial gradient brush matching pinned WinUI RadialGradientBrush.
   Center, RadiusX, RadiusY and GradientOrigin are all expressed in the same mapping coordinate space
   (element-relative [0..1] in IM_MAPPING_RELATIVE, or local DIPs in IM_MAPPING_ABSOLUTE). */
typedef struct im_radial_gradient {
    im_fixed center_x, center_y;
    im_fixed radius_x, radius_y;
    im_fixed origin_x, origin_y;
    im_mapping_mode mapping;
    im_spread_method spread;
    im_gradient_space space;
    const im_gradient_stop *stops;
    uint32_t stop_count;
} im_radial_gradient;

/* Construct an im_radial_gradient initialized with WinUI 3 defaults:
   Center=(0.5, 0.5), RadiusX=0.5, RadiusY=0.5, GradientOrigin=(0.5, 0.5),
   IM_MAPPING_RELATIVE, IM_SPREAD_PAD, IM_GRAD_SPACE_SRGB. */
im_radial_gradient im_radial_gradient_default(const im_gradient_stop *stops, uint32_t stop_count);

/* Sample a gradient stop array at Q30 parameter t_q30 with the given spread method,
   returning straight 0xAARRGGBB (premultiplied-alpha sRGB interpolation between adjacent stops). */
uint32_t im_gradient_sample_stops(const im_gradient_stop *stops, uint32_t stop_count,
                                  im_spread_method spread, int64_t t_q30);

/* Evaluate a linear gradient at local-space Q30 point (lx_q30, ly_q30) against local_bounds and scale,
   returning straight 0xAARRGGBB. */
uint32_t im_linear_gradient_eval(const im_linear_gradient *grad, im_rect local_bounds, im_fixed scale,
                                 int64_t lx_q30, int64_t ly_q30);

/* Evaluate a radial gradient at local-space Q30 point (lx_q30, ly_q30) against local_bounds and scale,
   using freestanding normalized __int128 ray-ellipse math. Returns straight 0xAARRGGBB. */
uint32_t im_radial_gradient_eval(const im_radial_gradient *grad, im_rect local_bounds, im_fixed scale,
                                 int64_t lx_q30, int64_t ly_q30);

/* Fill a rounded rectangle with a linear gradient, honouring surface->transform and surface->layers. */
void im_gradient_fill_rect_linear(im_surface *surface, im_rect rect, im_radii radii,
                                  const im_linear_gradient *grad);

/* Fill a rounded rectangle with a radial gradient, honouring surface->transform and surface->layers. */
void im_gradient_fill_rect_radial(im_surface *surface, im_rect rect, im_radii radii,
                                  const im_radial_gradient *grad);

/* Fill a vector path with a linear gradient, honouring surface->transform and surface->layers. */
bool im_gradient_fill_path_linear(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                  const im_linear_gradient *grad);

/* Fill a vector path with a radial gradient, honouring surface->transform and surface->layers. */
bool im_gradient_fill_path_radial(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                  const im_radial_gradient *grad);

/* Stroke a vector path with a linear gradient, honouring surface->transform and surface->layers. */
bool im_gradient_stroke_path_linear(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                    const im_stroke_style *style, const im_linear_gradient *grad);

/* Stroke a vector path with a radial gradient, honouring surface->transform and surface->layers. */
bool im_gradient_stroke_path_radial(im_surface *surface, im_allocator alloc, const im_path_data *path,
                                    const im_stroke_style *style, const im_radial_gradient *grad);
