/* Impossible SDK: portable freestanding BMP decoder (OS/2 1.x/2.x and Windows V3/V4/V5). No floats, no OS calls. */
#include "bmp.h"

static void bmp_zero(void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0u;
}

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_le32s(const uint8_t *p) {
    return (int32_t)rd_le32(p);
}

static void write_pixel(uint8_t *dst, im_pixel_format fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (fmt == IM_PIXEL_BGRA8) {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = a;
    } else {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
    }
}

/* Extract bits selected by an arbitrary (contiguous or non-contiguous) mask and scale to 0..255
   using rounded 64-bit integer arithmetic: (v * 255 + max_v / 2) / max_v. */
static uint8_t scale_masked(uint32_t word, uint32_t mask, uint8_t default_if_zero) {
    if (mask == 0u) return default_if_zero;
    uint32_t v = 0u;
    uint32_t bits = 0u;
    for (uint32_t i = 0u; i < 32u; i++) {
        if ((mask >> i) & 1u) {
            if ((word >> i) & 1u) v |= (1u << bits);
            bits++;
        }
    }
    uint64_t max_v = ((uint64_t)1u << bits) - 1u;
    return (uint8_t)(((uint64_t)v * 255u + (max_v >> 1)) / max_v);
}

typedef struct bmp_parsed {
    im_bmp_info info;
    uint32_t bf_size;
    uint32_t bf_off_bits;
    uint32_t img_size;
    uint32_t clr_used;
    uint32_t palette_entries;
    uint32_t palette_entry_size;
    uint32_t palette_offset;
    uint32_t extra_masks_size;
    bool use_bitfields;
    uint32_t r_mask;
    uint32_t g_mask;
    uint32_t b_mask;
    uint32_t a_mask;
    bool has_v5_embedded;
    uint32_t v5_profile_data;
    uint32_t v5_profile_size;
    size_t row_stride;
    size_t raw_image_bytes;
} bmp_parsed;

static im_image_error parse_header(const uint8_t *data, size_t len,
                                   const im_image_limits *limits,
                                   bmp_parsed *out) {
    bmp_zero(out, sizeof(*out));
    if (!data) return IM_IMAGE_BAD_ARG;
    if (len == 0u) return IM_IMAGE_TRUNCATED;
    if (data[0] != (uint8_t)'B') return IM_IMAGE_INVALID;
    if (len == 1u) return IM_IMAGE_TRUNCATED;
    if (data[1] != (uint8_t)'M') return IM_IMAGE_INVALID;
    if (len < 18u) return IM_IMAGE_TRUNCATED;

    uint32_t bf_size = rd_le32(data + 2u);
    uint32_t bf_off = rd_le32(data + 10u);
    uint32_t hdr_size = rd_le32(data + 14u);

    bool is_os2_1 = (hdr_size == 12u);
    bool is_os2_2 = (hdr_size == 16u || hdr_size == 64u);
    bool is_win = (hdr_size == 40u || hdr_size == 52u || hdr_size == 56u || hdr_size == 108u || hdr_size == 124u);
    if (!is_os2_1 && !is_os2_2 && !is_win) return IM_IMAGE_INVALID;
    if (len < 14u + (size_t)hdr_size) return IM_IMAGE_TRUNCATED;

    uint32_t width = 0u;
    uint32_t height = 0u;
    bool top_down = false;
    uint16_t planes = 0u;
    uint16_t bpp = 0u;
    uint32_t comp = 0u;
    uint32_t img_size = 0u;
    uint32_t clr_used = 0u;

    if (is_os2_1) {
        width = rd_le16(data + 18u);
        height = rd_le16(data + 20u);
        planes = rd_le16(data + 22u);
        bpp = rd_le16(data + 24u);
    } else {
        int32_t sw = rd_le32s(data + 18u);
        int32_t sh = rd_le32s(data + 22u);
        if (sw <= 0 || sh == 0 || sh == INT32_MIN) return IM_IMAGE_INVALID;
        width = (uint32_t)sw;
        if (sh < 0) {
            top_down = true;
            height = (uint32_t)(-sh);
        } else {
            height = (uint32_t)sh;
        }
        planes = rd_le16(data + 26u);
        bpp = rd_le16(data + 28u);
        if (hdr_size >= 40u) {
            comp = rd_le32(data + 30u);
            img_size = rd_le32(data + 34u);
            clr_used = rd_le32(data + 46u);
        }
    }

    if (width == 0u || height == 0u || planes != 1u) return IM_IMAGE_INVALID;

    /* Classify unsupported compression methods before depth whitelist (e.g. JPEG/PNG with bpp == 0). */
    if (is_os2_2) {
        if (comp == 3u || comp == 4u) return IM_IMAGE_UNSUPPORTED;
        if (comp > 2u) return IM_IMAGE_INVALID;
        if (bpp != 1u && bpp != 4u && bpp != 8u && bpp != 24u) return IM_IMAGE_INVALID;
    } else if (is_os2_1) {
        if (bpp != 1u && bpp != 4u && bpp != 8u && bpp != 24u) return IM_IMAGE_INVALID;
    } else {
        if (comp == 4u || comp == 5u) return IM_IMAGE_UNSUPPORTED;
        if (comp != 0u && comp != 1u && comp != 2u && comp != 3u && comp != 6u) return IM_IMAGE_INVALID;
        if (bpp != 1u && bpp != 2u && bpp != 4u && bpp != 8u && bpp != 16u && bpp != 24u && bpp != 32u) {
            return IM_IMAGE_INVALID;
        }
    }

    /* Validate compression vs bit depth, orientation, and header size. */
    if (comp == 1u) {
        if (bpp != 8u || top_down) return IM_IMAGE_INVALID;
    } else if (comp == 2u) {
        if (bpp != 4u || top_down) return IM_IMAGE_INVALID;
    } else if (comp == 3u) {
        if (bpp != 16u && bpp != 32u) return IM_IMAGE_INVALID;
    } else if (comp == 6u) {
        if ((bpp != 16u && bpp != 32u) || hdr_size == 52u) return IM_IMAGE_INVALID;
    }

    /* Check dimension & decoded output byte limits BEFORE any allocation. */
    size_t byte_size = 0u;
    im_image_error lim_err = im_image_check_limits(width, height, limits, &byte_size);
    if (lim_err != IM_IMAGE_OK) return lim_err;

    /* Check uncompressed row stride & raw image byte size in 64-bit arithmetic. */
    uint64_t row_bits = (uint64_t)width * (uint64_t)bpp;
    uint64_t row_stride64 = ((row_bits + 31u) >> 5) << 2;
    if (row_stride64 > (uint64_t)((size_t)-1)) return IM_IMAGE_TOO_LARGE;
    uint64_t raw_bytes64 = row_stride64 * (uint64_t)height;
    if (row_stride64 != 0u && raw_bytes64 / row_stride64 != (uint64_t)height) return IM_IMAGE_TOO_LARGE;
    if (raw_bytes64 > (uint64_t)((size_t)-1)) return IM_IMAGE_TOO_LARGE;

    /* Determine bitfield mask layout and extra external masks after v3 (40-byte) header. */
    uint32_t extra_masks = 0u;
    bool use_bf = false;
    uint32_t r_mask = 0u, g_mask = 0u, b_mask = 0u, a_mask = 0u;

    if (is_win && (bpp == 16u || bpp == 32u)) {
        if (comp == 3u || comp == 6u) {
            use_bf = true;
            if (hdr_size == 40u) {
                extra_masks = (comp == 6u) ? 16u : 12u;
                if (len < 14u + 40u + (size_t)extra_masks) return IM_IMAGE_TRUNCATED;
                r_mask = rd_le32(data + 54u);
                g_mask = rd_le32(data + 58u);
                b_mask = rd_le32(data + 62u);
                a_mask = (comp == 6u) ? rd_le32(data + 66u) : 0u;
            } else {
                r_mask = rd_le32(data + 54u);
                g_mask = rd_le32(data + 58u);
                b_mask = rd_le32(data + 62u);
                a_mask = (hdr_size >= 56u) ? rd_le32(data + 66u) : 0u;
            }
        } else if (comp == 0u && hdr_size >= 56u) {
            /* V3/V4/V5 with BI_RGB: use standard RGB masks, plus alpha mask if header declares one?
               Per Microsoft BMP / bmpsuite (rgba32-1.bmp, rgba32h56.bmp), BI_RGB 16-bit is 5-5-5
               and BI_RGB 32-bit is 8-8-8-0 unless BI_BITFIELDS/BI_ALPHABITFIELDS or V4/V5 alpha mask. */
            uint32_t hdr_a = rd_le32(data + 66u);
            if (bpp == 32u && hdr_a == 0xFF000000u && rd_le32(data + 54u) == 0x00FF0000u &&
                rd_le32(data + 58u) == 0x0000FF00u && rd_le32(data + 62u) == 0x000000FFu) {
                use_bf = true;
                r_mask = 0x00FF0000u;
                g_mask = 0x0000FF00u;
                b_mask = 0x000000FFu;
                a_mask = 0xFF000000u;
            }
        }
    }

    if (use_bf) {
        uint32_t word_mask = (bpp == 16u) ? 0xFFFFu : 0xFFFFFFFFu;
        if (r_mask == 0u || g_mask == 0u || b_mask == 0u) return IM_IMAGE_INVALID;
        if (((r_mask | g_mask | b_mask | a_mask) & ~word_mask) != 0u) return IM_IMAGE_INVALID;
        if ((r_mask & g_mask) != 0u || (r_mask & b_mask) != 0u || (g_mask & b_mask) != 0u) return IM_IMAGE_INVALID;
        if ((a_mask & (r_mask | g_mask | b_mask)) != 0u) return IM_IMAGE_INVALID;
    }

    /* Validate header-declared offsets and palette bounds (without requiring payload bytes in probe). */
    uint64_t pal_off = 14u + (uint64_t)hdr_size + (uint64_t)extra_masks;
    if ((uint64_t)bf_off < pal_off) return IM_IMAGE_INVALID;

    uint32_t pal_entry_size = is_os2_1 ? 3u : 4u;
    uint32_t pal_entries = 0u;
    if (bpp <= 8u) {
        uint32_t max_entries = 1u << bpp;
        if (clr_used > max_entries) return IM_IMAGE_INVALID;
        if (clr_used > 0u) {
            pal_entries = clr_used;
        } else {
            uint64_t avail_entries = ((uint64_t)bf_off - pal_off) / (uint64_t)pal_entry_size;
            if (is_os2_1 || is_os2_2) {
                pal_entries = (avail_entries < (uint64_t)max_entries) ? (uint32_t)avail_entries : max_entries;
            } else {
                pal_entries = max_entries;
            }
        }
        uint64_t pal_bytes = (uint64_t)pal_entries * (uint64_t)pal_entry_size;
        if (pal_off + pal_bytes > (uint64_t)bf_off) return IM_IMAGE_INVALID;
    } else if (clr_used > 0u) {
        uint64_t pal_bytes = (uint64_t)clr_used * (uint64_t)pal_entry_size;
        if (pal_off + pal_bytes > (uint64_t)bf_off) return IM_IMAGE_INVALID;
    }

    bool os2_hdr_size_conv = (is_os2_1 && bf_size == 26u);
    if (bf_size != 0u && !os2_hdr_size_conv && bf_size < bf_off) return IM_IMAGE_INVALID;

    out->info.width = width;
    out->info.height = height;
    out->info.bit_count = bpp;
    out->info.compression = comp;
    out->info.header_size = hdr_size;
    out->info.top_down = top_down;
    out->info.byte_size = byte_size;

    out->bf_size = bf_size;
    out->bf_off_bits = bf_off;
    out->img_size = img_size;
    out->clr_used = clr_used;
    out->palette_entries = pal_entries;
    out->palette_entry_size = pal_entry_size;
    out->palette_offset = (uint32_t)pal_off;
    out->extra_masks_size = extra_masks;
    out->use_bitfields = use_bf;
    out->r_mask = r_mask;
    out->g_mask = g_mask;
    out->b_mask = b_mask;
    out->a_mask = a_mask;
    out->row_stride = (size_t)row_stride64;
    out->raw_image_bytes = (size_t)raw_bytes64;

    if (hdr_size == 124u) {
        uint32_t cs_type = rd_le32(data + 70u);
        if (cs_type == 0x4D424544u) { /* PROFILE_EMBEDDED ('MBED') */
            out->has_v5_embedded = true;
            out->v5_profile_data = rd_le32(data + 126u);
            out->v5_profile_size = rd_le32(data + 130u);
        }
    }

    return IM_IMAGE_OK;
}

im_image_error im_bmp_probe(const uint8_t *data, size_t len,
                            const im_image_limits *limits,
                            im_bmp_info *out_info) {
    if (out_info) bmp_zero(out_info, sizeof(*out_info));
    if (!data || !out_info) return IM_IMAGE_BAD_ARG;
    bmp_parsed p;
    im_image_error err = parse_header(data, len, limits, &p);
    if (err != IM_IMAGE_OK) return err;
    *out_info = p.info;
    return IM_IMAGE_OK;
}

static im_image_error decode_rle8(const uint8_t *src, size_t src_len,
                                  const bmp_parsed *p, const uint8_t pal[256][3],
                                  im_pixel_format fmt, uint8_t *pixels,
                                  size_t *out_consumed) {
    uint32_t w = p->info.width;
    uint32_t h = p->info.height;
    size_t stride = (size_t)w * 4u;
    uint32_t x = 0u;
    uint32_t y = 0u; /* bottom-up row index: 0 is bottom row (height - 1) */
    size_t pos = 0u;

    while (pos < src_len) {
        if (src_len - pos < 2u) return IM_IMAGE_TRUNCATED;
        uint8_t count = src[pos++];
        uint8_t val = src[pos++];

        if (count > 0u) {
            if (y >= h || (uint32_t)count > w - x) return IM_IMAGE_INVALID;
            if ((uint32_t)val >= p->palette_entries) return IM_IMAGE_INVALID;
            uint32_t dst_y = h - 1u - y;
            uint8_t *row = pixels + (size_t)dst_y * stride;
            uint8_t r = pal[val][0], g = pal[val][1], b = pal[val][2];
            for (uint32_t i = 0u; i < (uint32_t)count; i++) {
                write_pixel(row + (size_t)(x + i) * 4u, fmt, r, g, b, 255u);
            }
            x += (uint32_t)count;
        } else {
            if (val == 0u) {
                /* End of line */
                if (y >= h) return IM_IMAGE_INVALID;
                x = 0u;
                y++;
            } else if (val == 1u) {
                /* End of bitmap (permitted at y <= h) */
                *out_consumed = pos;
                return IM_IMAGE_OK;
            } else if (val == 2u) {
                /* Delta */
                if (src_len - pos < 2u) return IM_IMAGE_TRUNCATED;
                uint8_t dx = src[pos++];
                uint8_t dy = src[pos++];
                if (y >= h || (uint32_t)dx > w - x || (uint32_t)dy >= h - y) return IM_IMAGE_INVALID;
                x += (uint32_t)dx;
                y += (uint32_t)dy;
            } else {
                /* Absolute mode: val literal pixels, padded to 2-byte boundary */
                uint32_t n = (uint32_t)val;
                size_t padded = (size_t)n + (size_t)(n & 1u);
                if (src_len - pos < padded) return IM_IMAGE_TRUNCATED;
                if (y >= h || n > w - x) return IM_IMAGE_INVALID;
                uint32_t dst_y = h - 1u - y;
                uint8_t *row = pixels + (size_t)dst_y * stride;
                for (uint32_t i = 0u; i < n; i++) {
                    uint8_t idx = src[pos + i];
                    if ((uint32_t)idx >= p->palette_entries) return IM_IMAGE_INVALID;
                    write_pixel(row + (size_t)(x + i) * 4u, fmt, pal[idx][0], pal[idx][1], pal[idx][2], 255u);
                }
                x += n;
                pos += padded;
            }
        }
    }
    return IM_IMAGE_TRUNCATED;
}

static im_image_error decode_rle4(const uint8_t *src, size_t src_len,
                                  const bmp_parsed *p, const uint8_t pal[256][3],
                                  im_pixel_format fmt, uint8_t *pixels,
                                  size_t *out_consumed) {
    uint32_t w = p->info.width;
    uint32_t h = p->info.height;
    size_t stride = (size_t)w * 4u;
    uint32_t x = 0u;
    uint32_t y = 0u;
    size_t pos = 0u;

    while (pos < src_len) {
        if (src_len - pos < 2u) return IM_IMAGE_TRUNCATED;
        uint8_t count = src[pos++];
        uint8_t val = src[pos++];

        if (count > 0u) {
            if (y >= h || (uint32_t)count > w - x) return IM_IMAGE_INVALID;
            uint8_t idx_hi = (uint8_t)(val >> 4);
            uint8_t idx_lo = (uint8_t)(val & 0x0Fu);
            if ((uint32_t)idx_hi >= p->palette_entries) return IM_IMAGE_INVALID;
            if (count > 1u && (uint32_t)idx_lo >= p->palette_entries) return IM_IMAGE_INVALID;
            uint32_t dst_y = h - 1u - y;
            uint8_t *row = pixels + (size_t)dst_y * stride;
            for (uint32_t i = 0u; i < (uint32_t)count; i++) {
                uint8_t idx = (i & 1u) ? idx_lo : idx_hi;
                write_pixel(row + (size_t)(x + i) * 4u, fmt, pal[idx][0], pal[idx][1], pal[idx][2], 255u);
            }
            x += (uint32_t)count;
        } else {
            if (val == 0u) {
                if (y >= h) return IM_IMAGE_INVALID;
                x = 0u;
                y++;
            } else if (val == 1u) {
                *out_consumed = pos;
                return IM_IMAGE_OK;
            } else if (val == 2u) {
                if (src_len - pos < 2u) return IM_IMAGE_TRUNCATED;
                uint8_t dx = src[pos++];
                uint8_t dy = src[pos++];
                if (y >= h || (uint32_t)dx > w - x || (uint32_t)dy >= h - y) return IM_IMAGE_INVALID;
                x += (uint32_t)dx;
                y += (uint32_t)dy;
            } else {
                uint32_t n = (uint32_t)val;
                size_t packed = ((size_t)n + 1u) >> 1;
                size_t padded = packed + (packed & 1u);
                if (src_len - pos < padded) return IM_IMAGE_TRUNCATED;
                if (y >= h || n > w - x) return IM_IMAGE_INVALID;
                uint32_t dst_y = h - 1u - y;
                uint8_t *row = pixels + (size_t)dst_y * stride;
                for (uint32_t i = 0u; i < n; i++) {
                    uint8_t byte = src[pos + (i >> 1)];
                    uint8_t idx = (i & 1u) ? (uint8_t)(byte & 0x0Fu) : (uint8_t)(byte >> 4);
                    if ((uint32_t)idx >= p->palette_entries) return IM_IMAGE_INVALID;
                    write_pixel(row + (size_t)(x + i) * 4u, fmt, pal[idx][0], pal[idx][1], pal[idx][2], 255u);
                }
                x += n;
                pos += padded;
            }
        }
    }
    return IM_IMAGE_TRUNCATED;
}

static im_image_error validate_v5_profile(const bmp_parsed *p, size_t file_end, uint64_t payload_end) {
    if (!p->has_v5_embedded || p->v5_profile_size == 0u) return IM_IMAGE_OK;
    uint64_t pstart = 14u + (uint64_t)p->v5_profile_data;
    uint64_t pend = pstart + (uint64_t)p->v5_profile_size;
    if (pend < pstart) return IM_IMAGE_INVALID;
    if (pstart < 14u + (uint64_t)p->info.header_size) return IM_IMAGE_INVALID;
    if (pend > (uint64_t)file_end) return IM_IMAGE_INVALID;

    uint64_t pal_start = (uint64_t)p->palette_offset;
    uint64_t pal_end = pal_start + (uint64_t)p->palette_entries * (uint64_t)p->palette_entry_size;
    if (pal_end > pal_start && pstart < pal_end && pend > pal_start) return IM_IMAGE_INVALID;

    uint64_t pix_start = (uint64_t)p->bf_off_bits;
    if (payload_end > pix_start && pstart < payload_end && pend > pix_start) return IM_IMAGE_INVALID;
    return IM_IMAGE_OK;
}

im_image_error im_bmp_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_bmp_alloc *alloc,
                             im_image *out_image) {
    if (out_image) bmp_zero(out_image, sizeof(*out_image));
    if (!data || !out_image || !alloc || !alloc->alloc || !alloc->free) return IM_IMAGE_BAD_ARG;
    if (format != IM_PIXEL_BGRA8 && format != IM_PIXEL_RGBA8) return IM_IMAGE_BAD_ARG;

    bmp_parsed p;
    im_image_error err = parse_header(data, len, limits, &p);
    if (err != IM_IMAGE_OK) return err;

    /* Determine logical file_end (bounding all payload and profile reads). */
    bool os2_hdr_size_conv = (p.info.header_size == 12u && p.bf_size == 26u);
    size_t file_end = len;
    if (p.bf_size != 0u && !os2_hdr_size_conv) {
        if ((size_t)p.bf_size > len) return IM_IMAGE_TRUNCATED;
        file_end = (size_t)p.bf_size;
    }

    if ((size_t)p.bf_off_bits > file_end) return IM_IMAGE_TRUNCATED;

    bool is_rle = (p.info.compression == 1u || p.info.compression == 2u);
    size_t avail_payload = file_end - (size_t)p.bf_off_bits;
    size_t rle_limit = avail_payload;
    uint64_t declared_payload_end = 0u;

    if (!is_rle) {
        if (avail_payload < p.raw_image_bytes) return IM_IMAGE_TRUNCATED;
        if (p.img_size != 0u) {
            if ((size_t)p.img_size < p.raw_image_bytes || (size_t)p.img_size > avail_payload) {
                return IM_IMAGE_INVALID;
            }
            declared_payload_end = (uint64_t)p.bf_off_bits + (uint64_t)p.img_size;
        } else {
            declared_payload_end = (uint64_t)p.bf_off_bits + (uint64_t)p.raw_image_bytes;
        }
        err = validate_v5_profile(&p, file_end, declared_payload_end);
        if (err != IM_IMAGE_OK) return err;
    } else {
        if (p.img_size != 0u) {
            if ((size_t)p.img_size > avail_payload) return IM_IMAGE_TRUNCATED;
            rle_limit = (size_t)p.img_size;
            declared_payload_end = (uint64_t)p.bf_off_bits + (uint64_t)p.img_size;
            err = validate_v5_profile(&p, file_end, declared_payload_end);
            if (err != IM_IMAGE_OK) return err;
        } else if (p.has_v5_embedded && p.v5_profile_size > 0u) {
            uint64_t pstart = 14u + (uint64_t)p.v5_profile_data;
            if (pstart >= (uint64_t)p.bf_off_bits && pstart < (uint64_t)file_end) {
                rle_limit = (size_t)(pstart - (uint64_t)p.bf_off_bits);
            }
        }
    }

    /* Load palette (if indexed). */
    uint8_t pal[256][3];
    if (p.info.bit_count <= 8u) {
        const uint8_t *psrc = data + p.palette_offset;
        for (uint32_t i = 0u; i < p.palette_entries; i++) {
            const uint8_t *e = psrc + (size_t)i * (size_t)p.palette_entry_size;
            pal[i][0] = e[2]; /* R */
            pal[i][1] = e[1]; /* G */
            pal[i][2] = e[0]; /* B */
        }
    }

    uint8_t *pixels = (uint8_t *)alloc->alloc(alloc->ctx, p.info.byte_size);
    if (!pixels) return IM_IMAGE_NO_MEMORY;

    if (is_rle) {
        /* Deterministic skipped-pixel initialization: transparent black (0, 0, 0, 0). */
        bmp_zero(pixels, p.info.byte_size);
        size_t rle_consumed = 0u;
        const uint8_t *rle_src = data + p.bf_off_bits;
        err = (p.info.compression == 1u)
            ? decode_rle8(rle_src, rle_limit, &p, pal, format, pixels, &rle_consumed)
            : decode_rle4(rle_src, rle_limit, &p, pal, format, pixels, &rle_consumed);
        if (err != IM_IMAGE_OK) {
            alloc->free(alloc->ctx, pixels, p.info.byte_size);
            return err;
        }
        if (p.img_size == 0u) {
            err = validate_v5_profile(&p, file_end, (uint64_t)p.bf_off_bits + (uint64_t)rle_consumed);
            if (err != IM_IMAGE_OK) {
                alloc->free(alloc->ctx, pixels, p.info.byte_size);
                return err;
            }
        }
    } else {
        uint32_t w = p.info.width;
        uint32_t h = p.info.height;
        uint16_t bpp = p.info.bit_count;
        size_t dst_stride = (size_t)w * 4u;
        const uint8_t *pix_src = data + p.bf_off_bits;

        for (uint32_t row_idx = 0u; row_idx < h; row_idx++) {
            uint32_t dst_y = p.info.top_down ? row_idx : (h - 1u - row_idx);
            const uint8_t *srow = pix_src + (size_t)row_idx * p.row_stride;
            uint8_t *drow = pixels + (size_t)dst_y * dst_stride;

            if (bpp <= 8u) {
                uint32_t ppb = 8u / (uint32_t)bpp;
                uint8_t mask = (uint8_t)((1u << bpp) - 1u);
                for (uint32_t x = 0u; x < w; x++) {
                    uint8_t idx = (bpp == 8u)
                        ? srow[x]
                        : (uint8_t)((srow[x / ppb] >> ((ppb - 1u - (x % ppb)) * (uint32_t)bpp)) & mask);
                    if ((uint32_t)idx >= p.palette_entries) {
                        alloc->free(alloc->ctx, pixels, p.info.byte_size);
                        return IM_IMAGE_INVALID;
                    }
                    write_pixel(drow + (size_t)x * 4u, format, pal[idx][0], pal[idx][1], pal[idx][2], 255u);
                }
            } else if (bpp == 16u) {
                for (uint32_t x = 0u; x < w; x++) {
                    uint32_t word = rd_le16(srow + (size_t)x * 2u);
                    uint8_t r, g, b, a;
                    if (p.use_bitfields) {
                        r = scale_masked(word, p.r_mask, 0u);
                        g = scale_masked(word, p.g_mask, 0u);
                        b = scale_masked(word, p.b_mask, 0u);
                        a = scale_masked(word, p.a_mask, 255u);
                    } else {
                        uint32_t rv = (word >> 10) & 0x1Fu;
                        uint32_t gv = (word >> 5) & 0x1Fu;
                        uint32_t bv = word & 0x1Fu;
                        r = (uint8_t)((rv * 255u + 15u) / 31u);
                        g = (uint8_t)((gv * 255u + 15u) / 31u);
                        b = (uint8_t)((bv * 255u + 15u) / 31u);
                        a = 255u;
                    }
                    write_pixel(drow + (size_t)x * 4u, format, r, g, b, a);
                }
            } else if (bpp == 24u) {
                for (uint32_t x = 0u; x < w; x++) {
                    const uint8_t *sp = srow + (size_t)x * 3u;
                    write_pixel(drow + (size_t)x * 4u, format, sp[2], sp[1], sp[0], 255u);
                }
            } else {
                for (uint32_t x = 0u; x < w; x++) {
                    uint32_t word = rd_le32(srow + (size_t)x * 4u);
                    uint8_t r, g, b, a;
                    if (p.use_bitfields) {
                        r = scale_masked(word, p.r_mask, 0u);
                        g = scale_masked(word, p.g_mask, 0u);
                        b = scale_masked(word, p.b_mask, 0u);
                        a = scale_masked(word, p.a_mask, 255u);
                    } else {
                        b = (uint8_t)(word & 0xFFu);
                        g = (uint8_t)((word >> 8) & 0xFFu);
                        r = (uint8_t)((word >> 16) & 0xFFu);
                        a = 255u;
                    }
                    write_pixel(drow + (size_t)x * 4u, format, r, g, b, a);
                }
            }
        }
    }

    out_image->width = p.info.width;
    out_image->height = p.info.height;
    out_image->stride = p.info.width * 4u;
    out_image->format = format;
    out_image->pixels = pixels;
    out_image->byte_size = p.info.byte_size;
    return IM_IMAGE_OK;
}
