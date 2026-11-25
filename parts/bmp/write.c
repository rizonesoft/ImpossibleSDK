/* Impossible SDK: portable freestanding BMP encoder (24-bpp BI_RGB and 32-bpp V5 BI_BITFIELDS). No floats, no OS calls. */
#include "bmp.h"

static void put_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

im_image_error im_bmp_encode(const im_image *img,
                             im_bmp_write_mode mode,
                             const im_bmp_alloc *alloc,
                             uint8_t **out_data,
                             size_t *out_len) {
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0u;
    if (!img || !img->pixels || !alloc || !alloc->alloc || !alloc->free || !out_data || !out_len) {
        return IM_IMAGE_BAD_ARG;
    }
    if (img->width == 0u || img->height == 0u) return IM_IMAGE_BAD_ARG;
    if (img->format != IM_PIXEL_BGRA8 && img->format != IM_PIXEL_RGBA8) return IM_IMAGE_BAD_ARG;
    if (mode > IM_BMP_WRITE_32) return IM_IMAGE_BAD_ARG;
    if (img->width > 0x7FFFFFFFu || img->height > 0x7FFFFFFFu) return IM_IMAGE_TOO_LARGE;

    uint64_t min_stride = (uint64_t)img->width * 4ULL;
    if ((uint64_t)img->stride < min_stride) return IM_IMAGE_BAD_ARG;
    uint64_t req_bytes = (uint64_t)img->stride * (uint64_t)img->height;
    if (req_bytes > (uint64_t)img->byte_size) return IM_IMAGE_BAD_ARG;

    /* Reject dimensions whose minimum possible BMP file size exceeds the 32-bit bfSize ceiling
       BEFORE scanning any pixel memory. */
    {
        bool min_32 = (mode == IM_BMP_WRITE_32);
        uint64_t min_hdr = min_32 ? 138ULL : 54ULL;
        uint64_t min_row = min_32 ? ((uint64_t)img->width * 4ULL) : ((((uint64_t)img->width * 3ULL) + 3ULL) & ~3ULL);
        if (min_row > (0xFFFFFFFFULL - min_hdr) / (uint64_t)img->height) return IM_IMAGE_TOO_LARGE;
        uint64_t min_total = min_hdr + min_row * (uint64_t)img->height;
        if (min_total > 0xFFFFFFFFULL || min_total > (uint64_t)SIZE_MAX) return IM_IMAGE_TOO_LARGE;
    }

    bool has_alpha = false;
    if (mode != IM_BMP_WRITE_32) {
        for (uint32_t y = 0; y < img->height && !has_alpha; ++y) {
            const uint8_t *row = img->pixels + (size_t)y * (size_t)img->stride;
            for (uint32_t x = 0; x < img->width; ++x) {
                if (row[(size_t)x * 4u + 3u] != 255u) {
                    has_alpha = true;
                    break;
                }
            }
        }
    }

    if (mode == IM_BMP_WRITE_24 && has_alpha) return IM_IMAGE_INVALID;
    bool use_32 = (mode == IM_BMP_WRITE_32) || (mode == IM_BMP_WRITE_AUTO && has_alpha);

    uint32_t dib_size = use_32 ? 124u : 40u;
    uint32_t pixel_off = 14u + dib_size;
    uint64_t dst_stride = use_32
        ? ((uint64_t)img->width * 4ULL)
        : ((((uint64_t)img->width * 3ULL) + 3ULL) & ~3ULL);
    uint64_t pix_bytes = dst_stride * (uint64_t)img->height;
    uint64_t total_size = (uint64_t)pixel_off + pix_bytes;
    if (total_size > 0xFFFFFFFFULL || total_size > (uint64_t)SIZE_MAX) return IM_IMAGE_TOO_LARGE;

    size_t alloc_size = (size_t)total_size;
    uint8_t *buf = (uint8_t *)alloc->alloc(alloc->ctx, alloc_size);
    if (!buf) return IM_IMAGE_NO_MEMORY;

    for (size_t i = 0; i < pixel_off; ++i) buf[i] = 0u;

    /* 14-byte BITMAPFILEHEADER */
    buf[0] = 'B';
    buf[1] = 'M';
    put_u32_le(buf + 2u, (uint32_t)total_size);
    put_u32_le(buf + 10u, pixel_off);

    /* DIB header (40-byte BITMAPINFOHEADER for 24-bpp BI_RGB, or 124-byte BITMAPV5HEADER for 32-bpp BI_BITFIELDS) */
    uint8_t *dib = buf + 14u;
    put_u32_le(dib + 0u, dib_size);
    put_u32_le(dib + 4u, img->width);
    put_u32_le(dib + 8u, img->height); /* positive => standard bottom-up row order */
    put_u16_le(dib + 12u, 1u);         /* planes */
    put_u16_le(dib + 14u, use_32 ? 32u : 24u);
    put_u32_le(dib + 16u, use_32 ? 3u : 0u); /* BI_BITFIELDS (3) or BI_RGB (0) */
    put_u32_le(dib + 20u, (uint32_t)pix_bytes);
    if (use_32) {
        /* Explicit BGRA masks + LCS_sRGB ('sRGB' = 0x73524742) so Windows WIC preserves straight 8-bit alpha. */
        put_u32_le(dib + 40u, 0x00FF0000u); /* bV5RedMask */
        put_u32_le(dib + 44u, 0x0000FF00u); /* bV5GreenMask */
        put_u32_le(dib + 48u, 0x000000FFu); /* bV5BlueMask */
        put_u32_le(dib + 52u, 0xFF000000u); /* bV5AlphaMask */
        put_u32_le(dib + 56u, 0x73524742u); /* bV5CSType = LCS_sRGB */
        put_u32_le(dib + 108u, 4u);         /* bV5Intent = LCS_GM_IMAGES */
    }

    /* Write pixels in bottom-up order (row height-1 first). */
    for (uint32_t dy = 0; dy < img->height; ++dy) {
        uint32_t sy = img->height - 1u - dy;
        const uint8_t *src = img->pixels + (size_t)sy * (size_t)img->stride;
        uint8_t *dst = buf + pixel_off + (size_t)dy * (size_t)dst_stride;
        if (use_32) {
            for (uint32_t x = 0; x < img->width; ++x) {
                const uint8_t *sp = src + (size_t)x * 4u;
                uint8_t *dp = dst + (size_t)x * 4u;
                if (img->format == IM_PIXEL_BGRA8) {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
                } else {
                    dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = sp[3];
                }
            }
        } else {
            size_t row_data = (size_t)img->width * 3u;
            for (uint32_t x = 0; x < img->width; ++x) {
                const uint8_t *sp = src + (size_t)x * 4u;
                uint8_t *dp = dst + (size_t)x * 3u;
                if (img->format == IM_PIXEL_BGRA8) {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                } else {
                    dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0];
                }
            }
            for (size_t p = row_data; p < (size_t)dst_stride; ++p) dst[p] = 0u;
        }
    }

    *out_data = buf;
    *out_len = alloc_size;
    return IM_IMAGE_OK;
}
