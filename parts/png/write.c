/* Impossible SDK: portable freestanding PNG encoder (RFC 2083 / ISO 15948). No floats, no OS calls. */
#include "png.h"
#include "checksum.h"
#include "deflate.h"

static void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t read_pixel_rgba(const uint8_t *row, uint32_t x, im_pixel_format fmt) {
    const uint8_t *p = row + (size_t)x * 4u;
    if (fmt == IM_PIXEL_BGRA8) {
        return ((uint32_t)p[2] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[0] << 8) | (uint32_t)p[3];
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int32_t ia = (int32_t)a;
    int32_t ib = (int32_t)b;
    int32_t ic = (int32_t)c;
    int32_t p = ia + ib - ic;
    int32_t pa = p - ia; if (pa < 0) pa = -pa;
    int32_t pb = p - ib; if (pb < 0) pb = -pb;
    int32_t pc = p - ic; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

typedef struct grow_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
    const im_png_alloc *alloc;
} grow_buf;

static bool grow_reserve(grow_buf *gb, size_t need_extra) {
    if (need_extra > SIZE_MAX - gb->len) return false;
    size_t req = gb->len + need_extra;
    if (req <= gb->cap) return true;
    size_t new_cap = gb->cap ? gb->cap : 1024u;
    while (new_cap < req) {
        if (new_cap > SIZE_MAX / 2u) {
            new_cap = req;
            break;
        }
        new_cap *= 2u;
    }
    uint8_t *nb = (uint8_t *)gb->alloc->alloc(gb->alloc->ctx, new_cap);
    if (!nb) return false;
    for (size_t i = 0; i < gb->len; ++i) nb[i] = gb->data[i];
    if (gb->data) gb->alloc->free(gb->alloc->ctx, gb->data, gb->cap);
    gb->data = nb;
    gb->cap = new_cap;
    return true;
}

static bool grow_append(grow_buf *gb, const uint8_t *src, size_t n) {
    if (n == 0u) return true;
    if (!grow_reserve(gb, n)) return false;
    for (size_t i = 0; i < n; ++i) gb->data[gb->len + i] = src[i];
    gb->len += n;
    return true;
}

static bool append_chunk(grow_buf *gb, const uint8_t type[4], const uint8_t *payload, size_t payload_len) {
    if (payload_len > 0x7FFFFFFFu) return false;
    uint8_t hdr[8];
    put_u32_be(hdr, (uint32_t)payload_len);
    hdr[4] = type[0];
    hdr[5] = type[1];
    hdr[6] = type[2];
    hdr[7] = type[3];
    uint32_t crc = im_crc32_init();
    crc = im_crc32_update(crc, type, 4u);
    if (payload_len > 0u) crc = im_crc32_update(crc, payload, payload_len);
    uint8_t crc_b[4];
    put_u32_be(crc_b, crc);
    if (!grow_reserve(gb, 12u + payload_len)) return false;
    (void)grow_append(gb, hdr, 8u);
    if (payload_len > 0u) (void)grow_append(gb, payload, payload_len);
    (void)grow_append(gb, crc_b, 4u);
    return true;
}

typedef struct pal_state {
    uint32_t colors[256];
    uint16_t hash_slot[512]; /* 0 = empty, else index + 1 */
    uint32_t count;
    bool overflowed;
    bool all_opaque;
    bool all_gray;
    bool gray1_ok;
    bool gray2_ok;
    bool gray4_ok;
} pal_state;

static uint32_t rgba_hash(uint32_t c) {
    c ^= c >> 16;
    c *= 0x7FEB352Du;
    c ^= c >> 15;
    return c & 511u;
}

static void pal_init(pal_state *ps) {
    for (uint32_t i = 0; i < 512u; ++i) ps->hash_slot[i] = 0u;
    ps->count = 0u;
    ps->overflowed = false;
    ps->all_opaque = true;
    ps->all_gray = true;
    ps->gray1_ok = true;
    ps->gray2_ok = true;
    ps->gray4_ok = true;
}

static void pal_add(pal_state *ps, uint32_t rgba) {
    uint8_t r = (uint8_t)(rgba >> 24);
    uint8_t g = (uint8_t)(rgba >> 16);
    uint8_t b = (uint8_t)(rgba >> 8);
    uint8_t a = (uint8_t)(rgba);
    if (a != 255u) ps->all_opaque = false;
    if (r != g || g != b) {
        ps->all_gray = false;
        ps->gray1_ok = false;
        ps->gray2_ok = false;
        ps->gray4_ok = false;
    } else {
        if (r != 0u && r != 255u) ps->gray1_ok = false;
        if ((r % 85u) != 0u) ps->gray2_ok = false;
        if ((r % 17u) != 0u) ps->gray4_ok = false;
    }
    if (ps->overflowed) return;
    uint32_t h = rgba_hash(rgba);
    for (;;) {
        uint16_t slot = ps->hash_slot[h];
        if (slot == 0u) {
            if (ps->count >= 256u) {
                ps->overflowed = true;
                return;
            }
            uint32_t idx = ps->count++;
            ps->colors[idx] = rgba;
            ps->hash_slot[h] = (uint16_t)(idx + 1u);
            return;
        }
        if (ps->colors[slot - 1u] == rgba) return;
        h = (h + 1u) & 511u;
    }
}

/* Sort palette: translucent entries (a < 255) first (minimizing tRNS), then ascending RGBA. */
static void pal_sort_and_rehash(pal_state *ps, uint32_t *out_trns_len) {
    for (uint32_t i = 1u; i < ps->count; ++i) {
        uint32_t v = ps->colors[i];
        uint8_t va = (uint8_t)v;
        uint32_t j = i;
        while (j > 0u) {
            uint32_t u = ps->colors[j - 1u];
            uint8_t ua = (uint8_t)u;
            bool u_trans = (ua < 255u);
            bool v_trans = (va < 255u);
            bool swap = false;
            if (u_trans != v_trans) {
                swap = (!u_trans && v_trans);
            } else if (u_trans) {
                swap = (ua > va) || (ua == va && u > v);
            } else {
                swap = (u > v);
            }
            if (!swap) break;
            ps->colors[j] = u;
            --j;
        }
        ps->colors[j] = v;
    }
    for (uint32_t i = 0; i < 512u; ++i) ps->hash_slot[i] = 0u;
    uint32_t trns = 0u;
    for (uint32_t i = 0; i < ps->count; ++i) {
        if (((uint8_t)ps->colors[i]) < 255u) trns = i + 1u;
        uint32_t h = rgba_hash(ps->colors[i]);
        while (ps->hash_slot[h] != 0u) h = (h + 1u) & 511u;
        ps->hash_slot[h] = (uint16_t)(i + 1u);
    }
    *out_trns_len = trns;
}

static uint8_t pal_lookup(const pal_state *ps, uint32_t rgba) {
    uint32_t h = rgba_hash(rgba);
    for (;;) {
        uint16_t slot = ps->hash_slot[h];
        if (ps->colors[slot - 1u] == rgba) return (uint8_t)(slot - 1u);
        h = (h + 1u) & 511u;
    }
}

static uint8_t indexed_bit_depth(uint32_t count) {
    if (count <= 2u) return 1u;
    if (count <= 4u) return 2u;
    if (count <= 16u) return 4u;
    return 8u;
}

static uint64_t row_bytes_for(uint32_t width, uint8_t color_type, uint8_t bit_depth) {
    uint64_t spp = 1ULL;
    if (color_type == 2u) spp = 3ULL;
    else if (color_type == 4u) spp = 2ULL;
    else if (color_type == 6u) spp = 4ULL;
    uint64_t bits = (uint64_t)width * spp * (uint64_t)bit_depth;
    return (bits + 7ULL) >> 3;
}

static void pack_row(const uint8_t *src_row, uint32_t width, im_pixel_format fmt,
                     uint8_t color_type, uint8_t bit_depth, const pal_state *ps,
                     uint8_t *dst, size_t row_bytes) {
    if (color_type == 0u && bit_depth < 8u) {
        for (size_t i = 0; i < row_bytes; ++i) dst[i] = 0u;
        uint32_t ppb = 8u / bit_depth;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r = (uint8_t)(read_pixel_rgba(src_row, x, fmt) >> 24);
            uint8_t val = (bit_depth == 1u) ? (r ? 1u : 0u) :
                          (bit_depth == 2u) ? (uint8_t)(r / 85u) : (uint8_t)(r / 17u);
            uint32_t shift = (ppb - 1u - (x % ppb)) * bit_depth;
            dst[x / ppb] |= (uint8_t)(val << shift);
        }
        return;
    }
    if (color_type == 0u) {
        for (uint32_t x = 0; x < width; ++x) {
            dst[x] = (uint8_t)(read_pixel_rgba(src_row, x, fmt) >> 24);
        }
        return;
    }
    if (color_type == 2u) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t c = read_pixel_rgba(src_row, x, fmt);
            dst[(size_t)x * 3u + 0u] = (uint8_t)(c >> 24);
            dst[(size_t)x * 3u + 1u] = (uint8_t)(c >> 16);
            dst[(size_t)x * 3u + 2u] = (uint8_t)(c >> 8);
        }
        return;
    }
    if (color_type == 3u) {
        if (bit_depth < 8u) {
            for (size_t i = 0; i < row_bytes; ++i) dst[i] = 0u;
            uint32_t ppb = 8u / bit_depth;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t idx = pal_lookup(ps, read_pixel_rgba(src_row, x, fmt));
                uint32_t shift = (ppb - 1u - (x % ppb)) * bit_depth;
                dst[x / ppb] |= (uint8_t)(idx << shift);
            }
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                dst[x] = pal_lookup(ps, read_pixel_rgba(src_row, x, fmt));
            }
        }
        return;
    }
    if (color_type == 4u) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t c = read_pixel_rgba(src_row, x, fmt);
            dst[(size_t)x * 2u + 0u] = (uint8_t)(c >> 24);
            dst[(size_t)x * 2u + 1u] = (uint8_t)(c);
        }
        return;
    }
    for (uint32_t x = 0; x < width; ++x) {
        uint32_t c = read_pixel_rgba(src_row, x, fmt);
        dst[(size_t)x * 4u + 0u] = (uint8_t)(c >> 24);
        dst[(size_t)x * 4u + 1u] = (uint8_t)(c >> 16);
        dst[(size_t)x * 4u + 2u] = (uint8_t)(c >> 8);
        dst[(size_t)x * 4u + 3u] = (uint8_t)(c);
    }
}

/* Evaluate all 5 RFC 2083 row filters and write the minimum-SAD filtered row into best_out[0..row_bytes]. */
static void filter_best_row(const uint8_t *cur, const uint8_t *prev, size_t row_bytes,
                            size_t bpp, uint8_t *cand_buf, uint8_t *best_out) {
    uint64_t best_sad = UINT64_MAX;
    for (uint8_t f = 0u; f < 5u; ++f) {
        uint64_t sad = 0u;
        cand_buf[0] = f;
        for (size_t i = 0; i < row_bytes; ++i) {
            uint8_t x = cur[i];
            uint8_t a = (i >= bpp) ? cur[i - bpp] : 0u;
            uint8_t b = prev ? prev[i] : 0u;
            uint8_t c = (prev && i >= bpp) ? prev[i - bpp] : 0u;
            uint8_t v = 0u;
            if (f == 0u) v = x;
            else if (f == 1u) v = (uint8_t)(x - a);
            else if (f == 2u) v = (uint8_t)(x - b);
            else if (f == 3u) v = (uint8_t)(x - (uint8_t)(((uint32_t)a + (uint32_t)b) >> 1));
            else v = (uint8_t)(x - paeth_predictor(a, b, c));
            cand_buf[1u + i] = v;
            int32_t sv = (int32_t)(int8_t)v;
            sad += (uint64_t)(sv < 0 ? -sv : sv);
        }
        if (sad < best_sad) {
            best_sad = sad;
            for (size_t i = 0; i <= row_bytes; ++i) best_out[i] = cand_buf[i];
        }
    }
}

static bool deflate_feed_all(im_deflate *def, const uint8_t *in, size_t in_len, bool is_eof,
                             grow_buf *idat) {
    size_t in_pos = 0;
    for (;;) {
        if (!grow_reserve(idat, 4096u)) return false;
        size_t cap_avail = idat->cap - idat->len;
        size_t ui = 0, uo = 0;
        im_deflate_status rc = im_deflate_step(
            def,
            (in_pos < in_len) ? (in + in_pos) : NULL,
            in_len - in_pos,
            is_eof,
            &ui,
            idat->data + idat->len,
            cap_avail,
            &uo);
        in_pos += ui;
        idat->len += uo;
        if (rc == IM_DEFLATE_OK) return is_eof && (in_pos == in_len);
        if (rc == IM_DEFLATE_NEED_INPUT) {
            if (in_pos == in_len && !is_eof) return true;
            if (ui == 0u && uo == 0u) return false;
        } else if (rc == IM_DEFLATE_OUT_FULL) {
            if (!grow_reserve(idat, (cap_avail < 4096u ? 4096u : cap_avail) * 2u)) return false;
        } else {
            return false;
        }
    }
}

im_image_error im_png_encode(const im_image *img,
                             const im_png_write_options *opts,
                             const im_png_alloc *alloc,
                             uint8_t **out_data,
                             size_t *out_len) {
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0u;
    if (!img || !img->pixels || !alloc || !alloc->alloc || !alloc->free || !out_data || !out_len) {
        return IM_IMAGE_BAD_ARG;
    }
    if (img->width == 0u || img->height == 0u) return IM_IMAGE_BAD_ARG;
    if (img->format != IM_PIXEL_BGRA8 && img->format != IM_PIXEL_RGBA8) return IM_IMAGE_BAD_ARG;
    if (img->width > 0x7FFFFFFFu || img->height > 0x7FFFFFFFu) return IM_IMAGE_TOO_LARGE;

    uint64_t min_stride = (uint64_t)img->width * 4ULL;
    if ((uint64_t)img->stride < min_stride) return IM_IMAGE_BAD_ARG;
    uint64_t req_bytes = (uint64_t)img->stride * (uint64_t)img->height;
    if (req_bytes > (uint64_t)img->byte_size) return IM_IMAGE_BAD_ARG;

    im_png_write_mode mode = opts ? opts->mode : IM_PNG_WRITE_AUTO;
    im_deflate_effort effort = opts ? opts->effort : IM_DEFLATE_BALANCED;
    if (mode > IM_PNG_WRITE_RGBA) return IM_IMAGE_BAD_ARG;
    if (effort != IM_DEFLATE_FAST && effort != IM_DEFLATE_BALANCED && effort != IM_DEFLATE_BEST) {
        return IM_IMAGE_BAD_ARG;
    }

    pal_state *ps = (pal_state *)alloc->alloc(alloc->ctx, sizeof(pal_state));
    if (!ps) return IM_IMAGE_NO_MEMORY;
    pal_init(ps);
    for (uint32_t y = 0; y < img->height; ++y) {
        const uint8_t *row = img->pixels + (size_t)y * (size_t)img->stride;
        for (uint32_t x = 0; x < img->width; ++x) {
            pal_add(ps, read_pixel_rgba(row, x, img->format));
        }
    }

    uint32_t trns_len = 0u;
    if (!ps->overflowed) pal_sort_and_rehash(ps, &trns_len);

    uint8_t color_type = 6u;
    uint8_t bit_depth = 8u;

    if (mode == IM_PNG_WRITE_GRAY) {
        if (!ps->all_gray || !ps->all_opaque) {
            alloc->free(alloc->ctx, ps, sizeof(pal_state));
            return IM_IMAGE_INVALID;
        }
        color_type = 0u;
        bit_depth = ps->gray1_ok ? 1u : (ps->gray2_ok ? 2u : (ps->gray4_ok ? 4u : 8u));
    } else if (mode == IM_PNG_WRITE_RGB) {
        if (!ps->all_opaque) {
            alloc->free(alloc->ctx, ps, sizeof(pal_state));
            return IM_IMAGE_INVALID;
        }
        color_type = 2u;
        bit_depth = 8u;
    } else if (mode == IM_PNG_WRITE_INDEXED) {
        if (ps->overflowed || ps->count == 0u) {
            alloc->free(alloc->ctx, ps, sizeof(pal_state));
            return IM_IMAGE_INVALID;
        }
        color_type = 3u;
        bit_depth = indexed_bit_depth(ps->count);
    } else if (mode == IM_PNG_WRITE_GRAY_ALPHA) {
        if (!ps->all_gray) {
            alloc->free(alloc->ctx, ps, sizeof(pal_state));
            return IM_IMAGE_INVALID;
        }
        color_type = 4u;
        bit_depth = 8u;
    } else if (mode == IM_PNG_WRITE_RGBA) {
        color_type = 6u;
        bit_depth = 8u;
    } else {
        /* IM_PNG_WRITE_AUTO: pick lossless representation with smallest raw + palette/tRNS footprint. */
        uint64_t best_est = UINT64_MAX;
        if (ps->all_opaque && ps->all_gray) {
            uint8_t gd = ps->gray1_ok ? 1u : (ps->gray2_ok ? 2u : (ps->gray4_ok ? 4u : 8u));
            uint64_t rb = row_bytes_for(img->width, 0u, gd);
            uint64_t est = (rb + 1ULL) * (uint64_t)img->height;
            color_type = 0u;
            bit_depth = gd;
            best_est = est;
        } else if (ps->all_gray) {
            uint64_t rb = row_bytes_for(img->width, 4u, 8u);
            uint64_t est = (rb + 1ULL) * (uint64_t)img->height;
            color_type = 4u;
            bit_depth = 8u;
            best_est = est;
        } else if (ps->all_opaque) {
            uint64_t rb = row_bytes_for(img->width, 2u, 8u);
            uint64_t est = (rb + 1ULL) * (uint64_t)img->height;
            color_type = 2u;
            bit_depth = 8u;
            best_est = est;
        } else {
            uint64_t rb = row_bytes_for(img->width, 6u, 8u);
            uint64_t est = (rb + 1ULL) * (uint64_t)img->height;
            color_type = 6u;
            bit_depth = 8u;
            best_est = est;
        }
        if (!ps->overflowed && ps->count > 0u) {
            uint8_t id = indexed_bit_depth(ps->count);
            uint64_t rb = row_bytes_for(img->width, 3u, id);
            uint64_t pal_ovh = 12ULL + (uint64_t)ps->count * 3ULL + (trns_len ? (12ULL + trns_len) : 0ULL);
            uint64_t est = (rb + 1ULL) * (uint64_t)img->height + pal_ovh;
            if (est < best_est) {
                color_type = 3u;
                bit_depth = id;
            }
        }
    }

    uint64_t row_bytes_64 = row_bytes_for(img->width, color_type, bit_depth);
    if (row_bytes_64 == 0u || row_bytes_64 > (uint64_t)(SIZE_MAX - 1u)) {
        alloc->free(alloc->ctx, ps, sizeof(pal_state));
        return IM_IMAGE_TOO_LARGE;
    }
    size_t row_bytes = (size_t)row_bytes_64;
    size_t bpp = 1u;
    if (color_type == 2u) bpp = 3u;
    else if (color_type == 4u) bpp = 2u;
    else if (color_type == 6u) bpp = 4u;

    /* Allocate deflater and 4 small row buffers (cur, prev, cand, best) via caller allocator. */
    im_deflate *def = (im_deflate *)alloc->alloc(alloc->ctx, sizeof(im_deflate));
    uint8_t *cur_row = (uint8_t *)alloc->alloc(alloc->ctx, row_bytes);
    uint8_t *prev_row = (uint8_t *)alloc->alloc(alloc->ctx, row_bytes);
    uint8_t *cand_row = (uint8_t *)alloc->alloc(alloc->ctx, row_bytes + 1u);
    uint8_t *best_row = (uint8_t *)alloc->alloc(alloc->ctx, row_bytes + 1u);
    grow_buf idat = { NULL, 0u, 0u, alloc };
    grow_buf png = { NULL, 0u, 0u, alloc };
    im_image_error err = IM_IMAGE_NO_MEMORY;

    if (!def || !cur_row || !prev_row || !cand_row || !best_row) goto cleanup;
    if (im_deflate_init(def, IM_DEFLATE_ZLIB, effort) != IM_DEFLATE_OK) {
        err = IM_IMAGE_BAD_ARG;
        goto cleanup;
    }

    for (uint32_t y = 0; y < img->height; ++y) {
        const uint8_t *src_row = img->pixels + (size_t)y * (size_t)img->stride;
        pack_row(src_row, img->width, img->format, color_type, bit_depth, ps, cur_row, row_bytes);
        filter_best_row(cur_row, (y > 0u) ? prev_row : NULL, row_bytes, bpp, cand_row, best_row);
        bool last_row = (y + 1u == img->height);
        if (!deflate_feed_all(def, best_row, row_bytes + 1u, last_row, &idat)) {
            err = IM_IMAGE_NO_MEMORY;
            goto cleanup;
        }
        uint8_t *tmp = prev_row;
        prev_row = cur_row;
        cur_row = tmp;
    }

    /* In AUTO mode for small indexed images (color_type == 3), compare adaptive min-SAD IDAT
       against filter-0 (None) IDAT and keep whichever yields the strictly smaller compressed payload. */
    if (mode == IM_PNG_WRITE_AUTO && color_type == 3u && idat.len < 4096u) {
        grow_buf idat_none = { NULL, 0u, 0u, alloc };
        if (im_deflate_init(def, IM_DEFLATE_ZLIB, effort) == IM_DEFLATE_OK) {
            bool none_ok = true;
            for (uint32_t y = 0; y < img->height && none_ok; ++y) {
                const uint8_t *src_row = img->pixels + (size_t)y * (size_t)img->stride;
                best_row[0] = 0u;
                pack_row(src_row, img->width, img->format, color_type, bit_depth, ps, best_row + 1u, row_bytes);
                bool last_row = (y + 1u == img->height);
                if (!deflate_feed_all(def, best_row, row_bytes + 1u, last_row, &idat_none)) {
                    none_ok = false;
                }
            }
            if (!none_ok) {
                if (idat_none.data) alloc->free(alloc->ctx, idat_none.data, idat_none.cap);
                err = IM_IMAGE_NO_MEMORY;
                goto cleanup;
            }
            if (idat_none.len < idat.len) {
                if (idat.data) alloc->free(alloc->ctx, idat.data, idat.cap);
                idat = idat_none;
            } else if (idat_none.data) {
                alloc->free(alloc->ctx, idat_none.data, idat_none.cap);
            }
        }
    }

    /* Assemble minimal PNG: signature + IHDR + optional PLTE/tRNS + IDAT + IEND. */
    static const uint8_t k_sig[8] = { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    static const uint8_t k_ihdr[4] = { 'I', 'H', 'D', 'R' };
    static const uint8_t k_plte[4] = { 'P', 'L', 'T', 'E' };
    static const uint8_t k_trns[4] = { 't', 'R', 'N', 'S' };
    static const uint8_t k_idat[4] = { 'I', 'D', 'A', 'T' };
    static const uint8_t k_iend[4] = { 'I', 'E', 'N', 'D' };

    uint8_t ihdr[13];
    put_u32_be(ihdr, img->width);
    put_u32_be(ihdr + 4u, img->height);
    ihdr[8] = bit_depth;
    ihdr[9] = color_type;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = 0u;

    if (!grow_append(&png, k_sig, 8u)) goto cleanup;
    if (!append_chunk(&png, k_ihdr, ihdr, 13u)) goto cleanup;

    if (color_type == 3u) {
        uint8_t plte[256u * 3u];
        for (uint32_t i = 0; i < ps->count; ++i) {
            plte[i * 3u + 0u] = (uint8_t)(ps->colors[i] >> 24);
            plte[i * 3u + 1u] = (uint8_t)(ps->colors[i] >> 16);
            plte[i * 3u + 2u] = (uint8_t)(ps->colors[i] >> 8);
        }
        if (!append_chunk(&png, k_plte, plte, (size_t)ps->count * 3u)) goto cleanup;
        if (trns_len > 0u) {
            uint8_t trns[256];
            for (uint32_t i = 0; i < trns_len; ++i) trns[i] = (uint8_t)(ps->colors[i]);
            if (!append_chunk(&png, k_trns, trns, (size_t)trns_len)) goto cleanup;
        }
    }

    if (idat.len > 0x7FFFFFFFu) {
        err = IM_IMAGE_TOO_LARGE;
        goto cleanup;
    }
    if (!append_chunk(&png, k_idat, idat.data, idat.len)) goto cleanup;
    if (!append_chunk(&png, k_iend, NULL, 0u)) goto cleanup;

    /* Shrink to exact *out_len allocation so caller's size-aware alloc->free(ctx, *out_data, *out_len) matches! */
    if (png.cap != png.len) {
        uint8_t *exact = (uint8_t *)alloc->alloc(alloc->ctx, png.len);
        if (!exact) goto cleanup;
        for (size_t i = 0; i < png.len; ++i) exact[i] = png.data[i];
        alloc->free(alloc->ctx, png.data, png.cap);
        png.data = exact;
        png.cap = png.len;
    }

    *out_data = png.data;
    *out_len = png.len;
    png.data = NULL;
    png.cap = 0u;
    err = IM_IMAGE_OK;

cleanup:
    if (png.data) alloc->free(alloc->ctx, png.data, png.cap);
    if (idat.data) alloc->free(alloc->ctx, idat.data, idat.cap);
    if (best_row) alloc->free(alloc->ctx, best_row, row_bytes + 1u);
    if (cand_row) alloc->free(alloc->ctx, cand_row, row_bytes + 1u);
    if (prev_row) alloc->free(alloc->ctx, prev_row, row_bytes);
    if (cur_row) alloc->free(alloc->ctx, cur_row, row_bytes);
    if (def) alloc->free(alloc->ctx, def, sizeof(im_deflate));
    alloc->free(alloc->ctx, ps, sizeof(pal_state));
    return err;
}
