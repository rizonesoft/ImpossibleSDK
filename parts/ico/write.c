/* Impossible SDK: portable freestanding ICO and CUR encoder (DIB+mask and RGBA PNG). No floats, no OS calls. */
#include "ico.h"
#include "png.h"

static void wr_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void read_px(const uint8_t *row, uint32_t x, im_pixel_format fmt,
                    uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint8_t *p = row + (size_t)x * 4u;
    if (fmt == IM_PIXEL_BGRA8) {
        *b = p[0];
        *g = p[1];
        *r = p[2];
        *a = p[3];
    } else {
        *r = p[0];
        *g = p[1];
        *b = p[2];
        *a = p[3];
    }
}

static bool invert_mask_bit(const uint8_t *mask, size_t inv_row, uint32_t x, uint32_t y) {
    return ((mask[(size_t)y * inv_row + (x >> 3)] >> (7u - (x & 7u))) & 1u) != 0u;
}

static bool invert_mask_has_any(const uint8_t *mask, uint32_t w, uint32_t h) {
    if (!mask) return false;
    size_t inv_row = ((size_t)w + 7u) >> 3;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            if (invert_mask_bit(mask, inv_row, x, y)) return true;
        }
    }
    return false;
}

typedef struct encoded_entry {
    uint8_t *data;
    size_t len;
    uint16_t bit_count;
} encoded_entry;

static im_image_error encode_dib_payload(const im_ico_write_entry *e,
                                         bool has_invert,
                                         const im_ico_alloc *alloc,
                                         encoded_entry *out) {
    const im_image *img = e->image;
    uint32_t w = img->width;
    uint32_t h = img->height;
    /* When destination-invert pixels are present, write 24-bpp BGR + 1-bpp AND mask so Windows
       uses the AND/XOR raster op (32-bpp ARGB ignores the AND mask when any alpha byte is non-zero). */
    uint16_t bpp = has_invert ? 24u : 32u;
    size_t xor_row = (((size_t)w * (size_t)bpp + 31u) / 32u) * 4u;
    size_t xor_bytes = xor_row * (size_t)h;
    size_t and_row = (((size_t)w + 31u) / 32u) * 4u;
    size_t and_bytes = and_row * (size_t)h;
    size_t total = 40u + xor_bytes + and_bytes;

    uint8_t *buf = (uint8_t *)alloc->alloc(alloc->ctx, total);
    if (!buf) return IM_IMAGE_NO_MEMORY;
    for (size_t i = 0; i < total; ++i) buf[i] = 0u;

    /* 40-byte BITMAPINFOHEADER with biHeight = 2 * h. */
    wr_u32_le(buf + 0u, 40u);
    wr_u32_le(buf + 4u, w);
    wr_u32_le(buf + 8u, h * 2u);
    wr_u16_le(buf + 12u, 1u);
    wr_u16_le(buf + 14u, bpp);
    wr_u32_le(buf + 16u, 0u);
    wr_u32_le(buf + 20u, (uint32_t)(xor_bytes + and_bytes));

    uint8_t *xor_base = buf + 40u;
    uint8_t *and_base = xor_base + xor_bytes;
    size_t inv_row = ((size_t)w + 7u) >> 3;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *sr = img->pixels + (size_t)y * (size_t)img->stride;
        uint32_t dst_y = h - 1u - y;
        uint8_t *xr = xor_base + (size_t)dst_y * xor_row;
        uint8_t *ar = and_base + (size_t)dst_y * and_row;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = 0u, g = 0u, b = 0u, a = 0u;
            read_px(sr, x, img->format, &r, &g, &b, &a);
            bool inv = has_invert && invert_mask_bit(e->invert_mask, inv_row, x, y);
            if (bpp == 24u) {
                if (inv) {
                    xr[(size_t)x * 3u + 0u] = b;
                    xr[(size_t)x * 3u + 1u] = g;
                    xr[(size_t)x * 3u + 2u] = r;
                    ar[x >> 3] |= (uint8_t)(1u << (7u - (x & 7u)));
                } else if (a == 0u) {
                    /* Transparent: AND=1, XOR=0 (leave xr zeroed). */
                    ar[x >> 3] |= (uint8_t)(1u << (7u - (x & 7u)));
                } else {
                    xr[(size_t)x * 3u + 0u] = b;
                    xr[(size_t)x * 3u + 1u] = g;
                    xr[(size_t)x * 3u + 2u] = r;
                }
            } else {
                if (a == 0u) {
                    /* Zero hidden RGB so an all-transparent 32-bpp icon never triggers legacy XOR invert. */
                    ar[x >> 3] |= (uint8_t)(1u << (7u - (x & 7u)));
                } else {
                    xr[(size_t)x * 4u + 0u] = b;
                    xr[(size_t)x * 4u + 1u] = g;
                    xr[(size_t)x * 4u + 2u] = r;
                    xr[(size_t)x * 4u + 3u] = a;
                }
            }
        }
    }

    out->data = buf;
    out->len = total;
    out->bit_count = bpp;
    return IM_IMAGE_OK;
}

im_image_error im_ico_encode(im_ico_type type,
                             const im_ico_write_entry *entries,
                             uint16_t count,
                             const im_ico_alloc *alloc,
                             uint8_t **out_data,
                             size_t *out_len) {
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0u;

    if (!entries || !alloc || !alloc->alloc || !alloc->free || !out_data || !out_len) {
        return IM_IMAGE_BAD_ARG;
    }
    if (type != IM_ICO_TYPE_ICON && type != IM_ICO_TYPE_CURSOR) return IM_IMAGE_BAD_ARG;
    if (count == 0u) return IM_IMAGE_BAD_ARG;
    if (count > IM_ICO_MAX_ENTRIES) return IM_IMAGE_TOO_LARGE;

    for (uint16_t i = 0; i < count; ++i) {
        const im_ico_write_entry *e = &entries[i];
        if (!e->image || !e->image->pixels) return IM_IMAGE_BAD_ARG;
        if (e->encoding > IM_ICO_ENTRY_PNG) return IM_IMAGE_BAD_ARG;
        uint32_t w = e->image->width;
        uint32_t h = e->image->height;
        if (w == 0u || h == 0u || w > 256u || h > 256u) return IM_IMAGE_BAD_ARG;
        if (e->image->format != IM_PIXEL_BGRA8 && e->image->format != IM_PIXEL_RGBA8) {
            return IM_IMAGE_BAD_ARG;
        }
        uint64_t min_stride = (uint64_t)w * 4ULL;
        if ((uint64_t)e->image->stride < min_stride) return IM_IMAGE_BAD_ARG;
        uint64_t req_bytes = (uint64_t)e->image->stride * (uint64_t)h;
        if (req_bytes > (uint64_t)e->image->byte_size) return IM_IMAGE_BAD_ARG;
        if (e->invert_mask != NULL) {
            size_t need_inv = (((size_t)w + 7u) >> 3) * (size_t)h;
            if (e->invert_mask_size < need_inv) return IM_IMAGE_BAD_ARG;
        }
        if (type == IM_ICO_TYPE_CURSOR) {
            if ((uint32_t)e->hotspot_x >= w || (uint32_t)e->hotspot_y >= h) {
                return IM_IMAGE_BAD_ARG;
            }
        }
    }

    encoded_entry *enc = (encoded_entry *)alloc->alloc(alloc->ctx, (size_t)count * sizeof(encoded_entry));
    if (!enc) return IM_IMAGE_NO_MEMORY;
    for (uint16_t i = 0; i < count; ++i) {
        enc[i].data = NULL;
        enc[i].len = 0u;
        enc[i].bit_count = 32u;
    }

    im_image_error err = IM_IMAGE_OK;
    uint64_t total_len = 6ULL + (uint64_t)count * 16ULL;

    for (uint16_t i = 0; i < count; ++i) {
        const im_ico_write_entry *e = &entries[i];
        bool has_inv = invert_mask_has_any(e->invert_mask, e->image->width, e->image->height);
        bool use_png = (e->encoding == IM_ICO_ENTRY_PNG) ||
                       (e->encoding == IM_ICO_ENTRY_AUTO &&
                        (e->image->width == 256u || e->image->height == 256u) && !has_inv);
        if (use_png) {
            if (has_inv) {
                err = IM_IMAGE_UNSUPPORTED;
                goto cleanup;
            }
            im_png_alloc pa = { alloc->alloc, alloc->free, alloc->ctx };
            im_png_write_options popts = { IM_PNG_WRITE_RGBA, IM_DEFLATE_BALANCED };
            err = im_png_encode(e->image, &popts, &pa, &enc[i].data, &enc[i].len);
            enc[i].bit_count = 32u;
        } else {
            err = encode_dib_payload(e, has_inv, alloc, &enc[i]);
        }
        if (err != IM_IMAGE_OK) goto cleanup;

        if (enc[i].len == 0u || enc[i].len > 0xFFFFFFFFULL ||
            total_len + (uint64_t)enc[i].len > 0xFFFFFFFFULL) {
            err = IM_IMAGE_TOO_LARGE;
            goto cleanup;
        }
        total_len += (uint64_t)enc[i].len;
    }

    uint8_t *out = (uint8_t *)alloc->alloc(alloc->ctx, (size_t)total_len);
    if (!out) {
        err = IM_IMAGE_NO_MEMORY;
        goto cleanup;
    }

    wr_u16_le(out + 0u, 0u);
    wr_u16_le(out + 2u, (uint16_t)type);
    wr_u16_le(out + 4u, count);

    uint32_t cur_off = 6u + (uint32_t)count * 16u;
    for (uint16_t i = 0; i < count; ++i) {
        const im_ico_write_entry *e = &entries[i];
        uint8_t *de = out + 6u + (size_t)i * 16u;
        de[0] = (e->image->width == 256u) ? 0u : (uint8_t)e->image->width;
        de[1] = (e->image->height == 256u) ? 0u : (uint8_t)e->image->height;
        de[2] = 0u;
        de[3] = 0u;
        if (type == IM_ICO_TYPE_ICON) {
            wr_u16_le(de + 4u, 1u);
            wr_u16_le(de + 6u, enc[i].bit_count);
        } else {
            wr_u16_le(de + 4u, e->hotspot_x);
            wr_u16_le(de + 6u, e->hotspot_y);
        }
        wr_u32_le(de + 8u, (uint32_t)enc[i].len);
        wr_u32_le(de + 12u, cur_off);

        for (size_t j = 0; j < enc[i].len; ++j) {
            out[cur_off + j] = enc[i].data[j];
        }
        cur_off += (uint32_t)enc[i].len;
    }

    *out_data = out;
    *out_len = (size_t)total_len;
    err = IM_IMAGE_OK;

cleanup:
    for (uint16_t i = 0; i < count; ++i) {
        if (enc[i].data) alloc->free(alloc->ctx, enc[i].data, enc[i].len);
    }
    alloc->free(alloc->ctx, enc, (size_t)count * sizeof(encoded_entry));
    return err;
}
