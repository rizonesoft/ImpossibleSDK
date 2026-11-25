/* Impossible SDK: portable freestanding ICO and CUR decoder and Windows-exact chooser. No floats, no OS calls. */
#include "ico.h"
#include "png.h"

static uint16_t rd_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_s32_le(const uint8_t *p) {
    return (int32_t)rd_u32_le(p);
}

static bool is_png_sig(const uint8_t *p, size_t n) {
    static const uint8_t k_sig[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    if (n < 8u) return false;
    for (size_t i = 0; i < 8u; ++i) {
        if (p[i] != k_sig[i]) return false;
    }
    return true;
}

static void zero_img(im_image *img) {
    if (!img) return;
    img->width = 0u;
    img->height = 0u;
    img->stride = 0u;
    img->format = IM_PIXEL_BGRA8;
    img->pixels = NULL;
    img->byte_size = 0u;
}

static void write_px(uint8_t *dst, im_pixel_format fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
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

/* In-place heapsort of entry spans by offset (ascending), O(N log N) with zero heap allocation. */
typedef struct ico_span {
    uint32_t start;
    uint32_t end;
} ico_span;

static void span_sift_down(ico_span *a, uint32_t root, uint32_t end) {
    for (;;) {
        uint32_t child = root * 2u + 1u;
        if (child >= end) break;
        if (child + 1u < end && a[child + 1u].start > a[child].start) {
            child += 1u;
        }
        if (a[root].start < a[child].start) {
            ico_span tmp = a[root];
            a[root] = a[child];
            a[child] = tmp;
            root = child;
        } else {
            break;
        }
    }
}

static im_image_error validate_container(const uint8_t *data, size_t len,
                                         im_ico_type *out_type, uint16_t *out_count) {
    if (!data) return IM_IMAGE_BAD_ARG;
    if (len < 6u) return IM_IMAGE_TRUNCATED;
    uint16_t reserved = rd_u16_le(data + 0u);
    uint16_t type = rd_u16_le(data + 2u);
    uint16_t count = rd_u16_le(data + 4u);
    if (reserved != 0u) return IM_IMAGE_INVALID;
    if (type != (uint16_t)IM_ICO_TYPE_ICON && type != (uint16_t)IM_ICO_TYPE_CURSOR) {
        return IM_IMAGE_INVALID;
    }
    if (count == 0u) return IM_IMAGE_INVALID;
    if (count > IM_ICO_MAX_ENTRIES) return IM_IMAGE_TOO_LARGE;

    uint64_t dir_end = 6ULL + (uint64_t)count * 16ULL;
    if (dir_end > (uint64_t)len) return IM_IMAGE_TRUNCATED;

    ico_span spans[IM_ICO_MAX_ENTRIES];
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *e = data + 6u + (size_t)i * 16u;
        uint32_t bytes_in_res = rd_u32_le(e + 8u);
        uint32_t offset = rd_u32_le(e + 12u);
        if (bytes_in_res == 0u) return IM_IMAGE_INVALID;
        if ((uint64_t)offset < dir_end) return IM_IMAGE_INVALID;
        uint64_t end64 = (uint64_t)offset + (uint64_t)bytes_in_res;
        if (end64 < (uint64_t)offset || end64 > (uint64_t)len) return IM_IMAGE_TRUNCATED;
        spans[i].start = offset;
        spans[i].end = (uint32_t)end64;
    }

    if (count > 1u) {
        for (uint32_t i = (uint32_t)count / 2u; i > 0u; --i) {
            span_sift_down(spans, i - 1u, (uint32_t)count);
        }
        for (uint32_t end = (uint32_t)count; end > 1u; --end) {
            ico_span tmp = spans[0];
            spans[0] = spans[end - 1u];
            spans[end - 1u] = tmp;
            span_sift_down(spans, 0u, end - 1u);
        }
        for (uint16_t i = 1u; i < count; ++i) {
            if (spans[i].start < spans[i - 1u].end) {
                return IM_IMAGE_INVALID;
            }
        }
    }

    if (out_type) *out_type = (im_ico_type)type;
    if (out_count) *out_count = count;
    return IM_IMAGE_OK;
}

static void inspect_entry_meta(const uint8_t *data, im_ico_type type, uint16_t index,
                               im_ico_entry_info *info) {
    const uint8_t *e = data + 6u + (size_t)index * 16u;
    uint32_t dw = e[0] ? (uint32_t)e[0] : 256u;
    uint32_t dh = e[1] ? (uint32_t)e[1] : 256u;
    uint16_t cc = (uint16_t)e[2];
    uint16_t f4 = rd_u16_le(e + 4u);
    uint16_t f6 = rd_u16_le(e + 6u);
    uint32_t sz = rd_u32_le(e + 8u);
    uint32_t off = rd_u32_le(e + 12u);

    info->dir_width = dw;
    info->dir_height = dh;
    info->dir_color_count = cc;
    if (type == IM_ICO_TYPE_ICON) {
        info->dir_planes = f4;
        info->dir_bit_count = f6;
        info->hotspot_x = 0u;
        info->hotspot_y = 0u;
    } else {
        info->dir_planes = 0u;
        info->dir_bit_count = 0u;
        info->hotspot_x = f4;
        info->hotspot_y = f6;
    }
    info->offset = off;
    info->size = sz;
    info->width = dw;
    info->height = dh;
    info->bit_count = (type == IM_ICO_TYPE_ICON) ? f6 : 0u;
    info->is_png = false;
    info->has_invert_mask = false;

    const uint8_t *payload = data + off;
    if (is_png_sig(payload, sz)) {
        info->is_png = true;
        im_png_info pinfo;
        if (im_png_probe(payload, sz, NULL, &pinfo) == IM_IMAGE_OK) {
            info->width = pinfo.width;
            info->height = pinfo.height;
            info->bit_count = 32u;
        }
    } else if (sz >= 40u) {
        uint32_t bi_size = rd_u32_le(payload + 0u);
        if ((bi_size == 40u || bi_size == 108u || bi_size == 124u) && sz >= bi_size) {
            int32_t bw = rd_s32_le(payload + 4u);
            int32_t bh = rd_s32_le(payload + 8u);
            uint16_t bpp = rd_u16_le(payload + 14u);
            if (bw > 0 && bh >= 2 && ((uint32_t)bh & 1u) == 0u) {
                info->width = (uint32_t)bw;
                info->height = (uint32_t)bh / 2u;
                info->bit_count = bpp;
            }
        }
    }
}

im_image_error im_ico_probe(const uint8_t *data, size_t len, im_ico_type *out_type, uint16_t *out_count) {
    if (out_type) *out_type = IM_ICO_TYPE_ICON;
    if (out_count) *out_count = 0u;
    return validate_container(data, len, out_type, out_count);
}

im_image_error im_ico_list(const uint8_t *data,
                           size_t len,
                           im_ico_type *out_type,
                           im_ico_entry_info *entries,
                           uint16_t cap,
                           uint16_t *out_count) {
    if (out_type) *out_type = IM_ICO_TYPE_ICON;
    if (out_count) *out_count = 0u;
    if (!data || !out_count) return IM_IMAGE_BAD_ARG;
    if (cap > 0u && !entries) return IM_IMAGE_BAD_ARG;

    im_ico_type type = IM_ICO_TYPE_ICON;
    uint16_t count = 0u;
    im_image_error err = validate_container(data, len, &type, &count);
    if (err != IM_IMAGE_OK) return err;

    if (out_type) *out_type = type;
    *out_count = count;
    if (cap < count) return IM_IMAGE_TOO_LARGE;

    for (uint16_t i = 0; i < count; ++i) {
        inspect_entry_meta(data, type, i, &entries[i]);
    }
    return IM_IMAGE_OK;
}

static im_image_error decode_dib_entry(const uint8_t *payload,
                                       size_t payload_len,
                                       im_pixel_format fmt,
                                       const im_image_limits *limits,
                                       const im_ico_alloc *alloc,
                                       im_image *out_img,
                                       uint8_t **out_invert_mask,
                                       size_t *out_invert_len,
                                       im_ico_entry_info *info) {
    if (payload_len < 40u) return IM_IMAGE_TRUNCATED;
    uint32_t bi_size = rd_u32_le(payload + 0u);
    if (bi_size != 40u && bi_size != 108u && bi_size != 124u) {
        return IM_IMAGE_INVALID;
    }
    if (payload_len < bi_size) return IM_IMAGE_TRUNCATED;

    int32_t bw_s = rd_s32_le(payload + 4u);
    int32_t bh_s = rd_s32_le(payload + 8u);
    uint16_t planes = rd_u16_le(payload + 12u);
    uint16_t bpp = rd_u16_le(payload + 14u);
    uint32_t comp = rd_u32_le(payload + 16u);
    uint32_t clr_used = rd_u32_le(payload + 32u);

    if (bw_s <= 0 || bh_s < 2 || ((uint32_t)bh_s & 1u) != 0u) return IM_IMAGE_INVALID;
    if (planes != 1u) return IM_IMAGE_INVALID;
    if (comp != 0u) return IM_IMAGE_UNSUPPORTED;
    if (bpp != 1u && bpp != 4u && bpp != 8u && bpp != 16u && bpp != 24u && bpp != 32u) {
        return IM_IMAGE_INVALID;
    }

    uint32_t w = (uint32_t)bw_s;
    uint32_t h = (uint32_t)bh_s / 2u;
    size_t pixel_bytes = 0u;
    im_image_error lerr = im_image_check_limits(w, h, limits, &pixel_bytes);
    if (lerr != IM_IMAGE_OK) return lerr;

    uint32_t pal_entries = 0u;
    if (bpp <= 8u) {
        uint32_t max_pal = 1u << bpp;
        pal_entries = (clr_used == 0u) ? max_pal : clr_used;
        if (pal_entries > max_pal) return IM_IMAGE_INVALID;
    } else {
        pal_entries = clr_used;
    }

    uint64_t pal_bytes = (uint64_t)pal_entries * 4ULL;
    uint64_t xor_off = (uint64_t)bi_size + pal_bytes;
    if (xor_off < (uint64_t)bi_size || xor_off > (uint64_t)payload_len) {
        return IM_IMAGE_TRUNCATED;
    }

    uint64_t xor_row = (((uint64_t)w * (uint64_t)bpp + 31ULL) / 32ULL) * 4ULL;
    uint64_t xor_bytes = xor_row * (uint64_t)h;
    uint64_t and_row = (((uint64_t)w + 31ULL) / 32ULL) * 4ULL;
    uint64_t and_bytes = and_row * (uint64_t)h;
    if (xor_off + xor_bytes < xor_off || xor_off + xor_bytes > (uint64_t)payload_len) {
        return IM_IMAGE_TRUNCATED;
    }

    uint64_t and_off = xor_off + xor_bytes;
    bool has_and = false;
    if (bpp < 32u) {
        if (and_off + and_bytes < and_off || and_off + and_bytes > (uint64_t)payload_len) {
            return IM_IMAGE_TRUNCATED;
        }
        has_and = true;
    } else {
        if ((uint64_t)payload_len == and_off) {
            has_and = false;
        } else if (and_off + and_bytes < and_off || and_off + and_bytes > (uint64_t)payload_len) {
            /* Partial AND plane is always truncated. */
            return IM_IMAGE_TRUNCATED;
        } else {
            has_and = true;
        }
    }

    const uint8_t *pal = payload + bi_size;
    const uint8_t *xor_base = payload + xor_off;
    const uint8_t *and_base = has_and ? (payload + and_off) : NULL;

    /* For 32-bpp, check whether any XOR pixel carries non-zero alpha (Windows ARGB icon). */
    bool use_32_alpha = false;
    if (bpp == 32u) {
        for (uint32_t y = 0; y < h && !use_32_alpha; ++y) {
            const uint8_t *sr = xor_base + (size_t)y * (size_t)xor_row;
            for (uint32_t x = 0; x < w; ++x) {
                if (sr[(size_t)x * 4u + 3u] != 0u) {
                    use_32_alpha = true;
                    break;
                }
            }
        }
        if (!use_32_alpha && !has_and) {
            /* All-zero 32-bpp alpha requires the AND mask to determine transparency. */
            return IM_IMAGE_TRUNCATED;
        }
    }

    /* Validate palette indices before allocating output pixels. */
    if (bpp <= 8u) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *sr = xor_base + (size_t)y * (size_t)xor_row;
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t idx = 0u;
                if (bpp == 1u) {
                    idx = (uint32_t)((sr[x >> 3] >> (7u - (x & 7u))) & 1u);
                } else if (bpp == 4u) {
                    idx = (uint32_t)((x & 1u) ? (sr[x >> 1] & 0x0Fu) : (sr[x >> 1] >> 4));
                } else {
                    idx = (uint32_t)sr[x];
                }
                if (idx >= pal_entries) return IM_IMAGE_INVALID;
            }
        }
    }

    uint8_t *pixels = (uint8_t *)alloc->alloc(alloc->ctx, pixel_bytes);
    if (!pixels) return IM_IMAGE_NO_MEMORY;

    size_t inv_row = ((size_t)w + 7u) >> 3;
    size_t inv_len = inv_row * (size_t)h;
    uint8_t *inv_mask = NULL;
    bool any_invert = false;

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t src_y = h - 1u - y;
        const uint8_t *sr = xor_base + (size_t)src_y * (size_t)xor_row;
        const uint8_t *ar = and_base ? (and_base + (size_t)src_y * (size_t)and_row) : NULL;
        uint8_t *dr = pixels + (size_t)y * (size_t)w * 4u;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = 0u, g = 0u, b = 0u, a = 255u;
            if (bpp == 1u) {
                uint32_t idx = (uint32_t)((sr[x >> 3] >> (7u - (x & 7u))) & 1u);
                b = pal[idx * 4u + 0u];
                g = pal[idx * 4u + 1u];
                r = pal[idx * 4u + 2u];
            } else if (bpp == 4u) {
                uint32_t idx = (uint32_t)((x & 1u) ? (sr[x >> 1] & 0x0Fu) : (sr[x >> 1] >> 4));
                b = pal[idx * 4u + 0u];
                g = pal[idx * 4u + 1u];
                r = pal[idx * 4u + 2u];
            } else if (bpp == 8u) {
                uint32_t idx = (uint32_t)sr[x];
                b = pal[idx * 4u + 0u];
                g = pal[idx * 4u + 1u];
                r = pal[idx * 4u + 2u];
            } else if (bpp == 16u) {
                uint16_t px = rd_u16_le(sr + (size_t)x * 2u);
                uint32_t r5 = (px >> 10) & 31u;
                uint32_t g5 = (px >> 5) & 31u;
                uint32_t b5 = px & 31u;
                r = (uint8_t)((r5 << 3) | (r5 >> 2));
                g = (uint8_t)((g5 << 3) | (g5 >> 2));
                b = (uint8_t)((b5 << 3) | (b5 >> 2));
            } else if (bpp == 24u) {
                b = sr[(size_t)x * 3u + 0u];
                g = sr[(size_t)x * 3u + 1u];
                r = sr[(size_t)x * 3u + 2u];
            } else {
                b = sr[(size_t)x * 4u + 0u];
                g = sr[(size_t)x * 4u + 1u];
                r = sr[(size_t)x * 4u + 2u];
                a = sr[(size_t)x * 4u + 3u];
            }

            if (!use_32_alpha) {
                uint8_t mask_bit = (uint8_t)((ar[x >> 3] >> (7u - (x & 7u))) & 1u);
                if (mask_bit != 0u) {
                    a = 0u;
                    if (r != 0u || g != 0u || b != 0u) {
                        any_invert = true;
                        if (out_invert_mask && !inv_mask) {
                            inv_mask = (uint8_t *)alloc->alloc(alloc->ctx, inv_len);
                            if (!inv_mask) {
                                alloc->free(alloc->ctx, pixels, pixel_bytes);
                                return IM_IMAGE_NO_MEMORY;
                            }
                            for (size_t i = 0; i < inv_len; ++i) inv_mask[i] = 0u;
                        }
                        if (inv_mask) {
                            inv_mask[(size_t)y * inv_row + (x >> 3)] |= (uint8_t)(1u << (7u - (x & 7u)));
                        }
                    }
                } else {
                    a = 255u;
                }
            }
            write_px(dr + (size_t)x * 4u, fmt, r, g, b, a);
        }
    }

    out_img->width = w;
    out_img->height = h;
    out_img->stride = w * 4u;
    out_img->format = fmt;
    out_img->pixels = pixels;
    out_img->byte_size = pixel_bytes;

    info->width = w;
    info->height = h;
    info->bit_count = bpp;
    info->is_png = false;
    info->has_invert_mask = any_invert;

    if (out_invert_mask && inv_mask) {
        *out_invert_mask = inv_mask;
        *out_invert_len = inv_len;
    }
    return IM_IMAGE_OK;
}

im_image_error im_ico_decode_entry(const uint8_t *data,
                                   size_t len,
                                   uint16_t index,
                                   im_pixel_format fmt,
                                   const im_image_limits *limits,
                                   const im_ico_alloc *alloc,
                                   im_image *out_img,
                                   uint8_t **out_invert_mask,
                                   size_t *out_invert_len,
                                   im_ico_entry_info *out_info) {
    zero_img(out_img);
    if (out_invert_mask) *out_invert_mask = NULL;
    if (out_invert_len) *out_invert_len = 0u;

    if (!data || !alloc || !alloc->alloc || !alloc->free || !out_img) {
        return IM_IMAGE_BAD_ARG;
    }
    if (out_invert_mask && !out_invert_len) return IM_IMAGE_BAD_ARG;
    if (fmt != IM_PIXEL_BGRA8 && fmt != IM_PIXEL_RGBA8) return IM_IMAGE_BAD_ARG;

    im_ico_type type = IM_ICO_TYPE_ICON;
    uint16_t count = 0u;
    im_image_error err = validate_container(data, len, &type, &count);
    if (err != IM_IMAGE_OK) return err;
    if (index >= count) return IM_IMAGE_BAD_ARG;

    im_ico_entry_info info;
    inspect_entry_meta(data, type, index, &info);
    const uint8_t *payload = data + info.offset;
    size_t payload_len = (size_t)info.size;

    if (is_png_sig(payload, payload_len)) {
        im_png_alloc pa = { alloc->alloc, alloc->free, alloc->ctx };
        err = im_png_decode(payload, payload_len, fmt, limits, &pa, out_img);
        if (err != IM_IMAGE_OK) {
            zero_img(out_img);
            return err;
        }
        info.width = out_img->width;
        info.height = out_img->height;
        info.bit_count = 32u;
        info.is_png = true;
        info.has_invert_mask = false;
    } else {
        err = decode_dib_entry(payload, payload_len, fmt, limits, alloc,
                               out_img, out_invert_mask, out_invert_len, &info);
        if (err != IM_IMAGE_OK) {
            zero_img(out_img);
            if (out_invert_mask && *out_invert_mask) {
                alloc->free(alloc->ctx, *out_invert_mask, *out_invert_len);
                *out_invert_mask = NULL;
                *out_invert_len = 0u;
            }
            return err;
        }
    }

    if (type == IM_ICO_TYPE_CURSOR) {
        if ((uint32_t)info.hotspot_x >= out_img->width || (uint32_t)info.hotspot_y >= out_img->height) {
            alloc->free(alloc->ctx, out_img->pixels, out_img->byte_size);
            zero_img(out_img);
            if (out_invert_mask && *out_invert_mask) {
                alloc->free(alloc->ctx, *out_invert_mask, *out_invert_len);
                *out_invert_mask = NULL;
                *out_invert_len = 0u;
            }
            return IM_IMAGE_INVALID;
        }
    }

    if (out_info) *out_info = info;
    return IM_IMAGE_OK;
}

void im_ico_composite_onto(uint8_t *dst_pixels,
                           uint32_t dst_stride,
                           uint32_t dst_w,
                           uint32_t dst_h,
                           im_pixel_format dst_fmt,
                           const im_image *icon,
                           const uint8_t *invert_mask,
                           size_t invert_mask_size) {
    if (!dst_pixels || !icon || !icon->pixels) return;
    if (dst_fmt != IM_PIXEL_BGRA8 && dst_fmt != IM_PIXEL_RGBA8) return;
    if (icon->format != IM_PIXEL_BGRA8 && icon->format != IM_PIXEL_RGBA8) return;
    uint32_t cw = icon->width < dst_w ? icon->width : dst_w;
    uint32_t ch = icon->height < dst_h ? icon->height : dst_h;
    size_t inv_row = ((size_t)icon->width + 7u) >> 3;
    bool use_inv = (invert_mask != NULL && invert_mask_size >= inv_row * (size_t)icon->height);

    for (uint32_t y = 0; y < ch; ++y) {
        const uint8_t *sr = icon->pixels + (size_t)y * (size_t)icon->stride;
        uint8_t *dr = dst_pixels + (size_t)y * (size_t)dst_stride;
        for (uint32_t x = 0; x < cw; ++x) {
            const uint8_t *sp = sr + (size_t)x * 4u;
            uint8_t *dp = dr + (size_t)x * 4u;
            uint8_t sr_c = (icon->format == IM_PIXEL_BGRA8) ? sp[2] : sp[0];
            uint8_t sg_c = sp[1];
            uint8_t sb_c = (icon->format == IM_PIXEL_BGRA8) ? sp[0] : sp[2];
            uint8_t sa_c = sp[3];

            uint8_t dr_c = (dst_fmt == IM_PIXEL_BGRA8) ? dp[2] : dp[0];
            uint8_t dg_c = dp[1];
            uint8_t db_c = (dst_fmt == IM_PIXEL_BGRA8) ? dp[0] : dp[2];

            bool is_inv = false;
            if (use_inv) {
                is_inv = (((invert_mask[(size_t)y * inv_row + (x >> 3)] >> (7u - (x & 7u))) & 1u) != 0u);
            }

            if (is_inv) {
                write_px(dp, dst_fmt, (uint8_t)(dr_c ^ sr_c), (uint8_t)(dg_c ^ sg_c), (uint8_t)(db_c ^ sb_c), 255u);
            } else if (sa_c == 255u) {
                write_px(dp, dst_fmt, sr_c, sg_c, sb_c, 255u);
            } else if (sa_c > 0u) {
                uint32_t a = (uint32_t)sa_c;
                uint32_t ia = 255u - a;
                uint8_t or_c = (uint8_t)((sr_c * a + dr_c * ia + 127u) / 255u);
                uint8_t og_c = (uint8_t)((sg_c * a + dg_c * ia + 127u) / 255u);
                uint8_t ob_c = (uint8_t)((sb_c * a + db_c * ia + 127u) / 255u);
                write_px(dp, dst_fmt, or_c, og_c, ob_c, 255u);
            }
        }
    }
}

static uint32_t entry_dir_bpp(const im_ico_entry_info *e) {
    if (e->dir_bit_count > 0u) {
        uint32_t pl = (e->dir_planes > 0u) ? (uint32_t)e->dir_planes : 1u;
        uint64_t prod = (uint64_t)pl * (uint64_t)e->dir_bit_count;
        return prod > 256ULL ? 256u : (uint32_t)prod;
    }
    if (e->dir_color_count > 0u) {
        if (e->dir_color_count <= 2u) return 1u;
        if (e->dir_color_count <= 16u) return 4u;
        return 8u;
    }
    /* CUR directories store hotspot instead of bit_count; fall back to inspected bit_count if known. */
    return e->bit_count ? (uint32_t)e->bit_count : 32u;
}

int32_t im_ico_choose_entry(const im_ico_entry_info *entries,
                            uint16_t count,
                            uint32_t target_w,
                            uint32_t target_h,
                            uint32_t screen_bpp) {
    if (!entries || count == 0u || target_w == 0u || target_h == 0u) return -1;
    uint32_t want_bpp = (screen_bpp == 0u) ? 32u : screen_bpp;

    int32_t best_idx = 0;
    uint64_t best_score = UINT64_MAX;
    uint64_t best_color_diff = UINT64_MAX;

    for (uint16_t i = 0; i < count; ++i) {
        uint32_t w = entries[i].dir_width ? entries[i].dir_width : 256u;
        uint32_t h = entries[i].dir_height ? entries[i].dir_height : 256u;
        uint64_t dx = (w < target_w) ? ((uint64_t)(target_w - w) * 2ULL) : (uint64_t)(w - target_w);
        uint64_t dy = (h < target_h) ? ((uint64_t)(target_h - h) * 2ULL) : (uint64_t)(h - target_h);
        uint64_t sdiff = dx + dy;

        uint32_t ebpp = entry_dir_bpp(&entries[i]);
        uint64_t cdiff = (ebpp > want_bpp) ? ((uint64_t)(ebpp - want_bpp) * 2ULL) : ((uint64_t)(want_bpp - ebpp) * 2ULL);
        uint64_t score = sdiff + cdiff;

        if (score < best_score || (score == best_score && cdiff < best_color_diff)) {
            best_score = score;
            best_color_diff = cdiff;
            best_idx = (int32_t)i;
        }
    }
    return best_idx;
}

int32_t im_ico_choose_entry_scaled(const im_ico_entry_info *entries,
                                   uint16_t count,
                                   uint32_t logical_w,
                                   uint32_t logical_h,
                                   uint32_t scale_percent,
                                   uint32_t screen_bpp) {
    if (!entries || count == 0u || logical_w == 0u || logical_h == 0u || scale_percent == 0u) {
        return -1;
    }
    uint64_t tw64 = ((uint64_t)logical_w * (uint64_t)scale_percent + 50ULL) / 100ULL;
    uint64_t th64 = ((uint64_t)logical_h * (uint64_t)scale_percent + 50ULL) / 100ULL;
    if (tw64 == 0ULL) tw64 = 1ULL;
    if (th64 == 0ULL) th64 = 1ULL;
    if (tw64 > 0xFFFFFFFFULL || th64 > 0xFFFFFFFFULL) return -1;
    return im_ico_choose_entry(entries, count, (uint32_t)tw64, (uint32_t)th64, screen_bpp);
}
