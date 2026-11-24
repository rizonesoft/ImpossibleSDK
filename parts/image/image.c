#include "image.h"

im_image_error im_image_check_limits(uint32_t width, uint32_t height, const im_image_limits *limits, size_t *out_bytes) {
    if (!out_bytes) {
        return IM_IMAGE_BAD_ARG;
    }
    *out_bytes = 0;
    if (width == 0 || height == 0) {
        return IM_IMAGE_INVALID;
    }

    uint32_t max_w = (limits && limits->max_width != 0) ? limits->max_width : IM_IMAGE_DEFAULT_MAX_DIM;
    uint32_t max_h = (limits && limits->max_height != 0) ? limits->max_height : IM_IMAGE_DEFAULT_MAX_DIM;
    size_t max_b = (limits && limits->max_bytes != 0) ? limits->max_bytes : IM_IMAGE_DEFAULT_MAX_BYTES;

    if (width > max_w || height > max_h) {
        return IM_IMAGE_TOO_LARGE;
    }

    /* Check stride = width * 4 without overflowing uint32_t or size_t. */
    if ((uint64_t)width > (uint64_t)UINT32_MAX / 4u) {
        return IM_IMAGE_TOO_LARGE;
    }
    size_t row_bytes = (size_t)width * 4u;
    if (row_bytes > max_b || (size_t)height > max_b / row_bytes) {
        return IM_IMAGE_TOO_LARGE;
    }

    *out_bytes = row_bytes * (size_t)height;
    return IM_IMAGE_OK;
}

const char *im_image_error_name(im_image_error err) {
    switch (err) {
    case IM_IMAGE_OK: return "IM_IMAGE_OK";
    case IM_IMAGE_BAD_ARG: return "IM_IMAGE_BAD_ARG";
    case IM_IMAGE_INVALID: return "IM_IMAGE_INVALID";
    case IM_IMAGE_TRUNCATED: return "IM_IMAGE_TRUNCATED";
    case IM_IMAGE_UNSUPPORTED: return "IM_IMAGE_UNSUPPORTED";
    case IM_IMAGE_TOO_LARGE: return "IM_IMAGE_TOO_LARGE";
    case IM_IMAGE_NO_MEMORY: return "IM_IMAGE_NO_MEMORY";
    default: return "IM_IMAGE_UNKNOWN";
    }
}
