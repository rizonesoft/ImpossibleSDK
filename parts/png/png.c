/* Impossible SDK: portable freestanding PNG decoder (RFC 2083 / ISO 15948). No floats, no OS calls. */
#include "png.h"
#include "checksum.h"
#include "inflate.h"

static const uint8_t PNG_SIG[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};

static const uint8_t ADAM7_X0[7] = {0u, 4u, 0u, 2u, 0u, 1u, 0u};
static const uint8_t ADAM7_Y0[7] = {0u, 0u, 4u, 0u, 2u, 0u, 1u};
static const uint8_t ADAM7_DX[7] = {8u, 8u, 4u, 4u, 2u, 2u, 1u};
static const uint8_t ADAM7_DY[7] = {8u, 8u, 8u, 4u, 4u, 2u, 2u};

static void png_zero(void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0u;
}

static void png_copy(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static bool is_ascii_letter(uint8_t c) {
    return (c >= (uint8_t)'A' && c <= (uint8_t)'Z') || (c >= (uint8_t)'a' && c <= (uint8_t)'z');
}

static bool chunk_is(const uint8_t *t, char a, char b, char c, char d) {
    return t[0] == (uint8_t)a && t[1] == (uint8_t)b && t[2] == (uint8_t)c && t[3] == (uint8_t)d;
}

static uint32_t color_channels(uint8_t color_type) {
    switch (color_type) {
        case 0u: return 1u;
        case 2u: return 3u;
        case 3u: return 1u;
        case 4u: return 2u;
        case 6u: return 4u;
        default: return 0u;
    }
}

static bool valid_color_depth(uint8_t color_type, uint8_t bit_depth) {
    switch (color_type) {
        case 0u: return bit_depth == 1u || bit_depth == 2u || bit_depth == 4u || bit_depth == 8u || bit_depth == 16u;
        case 2u: return bit_depth == 8u || bit_depth == 16u;
        case 3u: return bit_depth == 1u || bit_depth == 2u || bit_depth == 4u || bit_depth == 8u;
        case 4u: return bit_depth == 8u || bit_depth == 16u;
        case 6u: return bit_depth == 8u || bit_depth == 16u;
        default: return false;
    }
}

static bool calc_row_bytes(uint32_t width, uint8_t color_type, uint8_t bit_depth, size_t *out_row) {
    uint64_t bits = (uint64_t)width * (uint64_t)color_channels(color_type) * (uint64_t)bit_depth;
    uint64_t bytes = (bits + 7u) >> 3;
    if (bytes > (uint64_t)((size_t)-1) - 16u) return false;
    *out_row = (size_t)bytes;
    return true;
}

static bool calc_expected_filtered(uint32_t width, uint32_t height, uint8_t color_type, uint8_t bit_depth,
                                   uint8_t interlace, size_t *out_max_row, size_t *out_total) {
    size_t max_row = 0u;
    uint64_t total = 0u;
    if (interlace == 0u) {
        size_t rb = 0u;
        if (!calc_row_bytes(width, color_type, bit_depth, &rb)) return false;
        uint64_t per_row = (uint64_t)rb + 1u;
        if (per_row > UINT64_MAX / (uint64_t)height) return false;
        total = per_row * (uint64_t)height;
        max_row = rb;
    } else {
        for (int p = 0; p < 7; p++) {
            if (width <= (uint32_t)ADAM7_X0[p] || height <= (uint32_t)ADAM7_Y0[p]) continue;
            uint32_t pw = (width - 1u - (uint32_t)ADAM7_X0[p]) / (uint32_t)ADAM7_DX[p] + 1u;
            uint32_t ph = (height - 1u - (uint32_t)ADAM7_Y0[p]) / (uint32_t)ADAM7_DY[p] + 1u;
            size_t rb = 0u;
            if (!calc_row_bytes(pw, color_type, bit_depth, &rb)) return false;
            if (rb > max_row) max_row = rb;
            uint64_t per_row = (uint64_t)rb + 1u;
            if (per_row > UINT64_MAX / (uint64_t)ph) return false;
            uint64_t pass_bytes = per_row * (uint64_t)ph;
            if (total > UINT64_MAX - pass_bytes) return false;
            total += pass_bytes;
        }
    }
    if (total > (uint64_t)((size_t)-1)) return false;
    if (max_row > ((size_t)-1) / 2u - 2u) return false;
    *out_max_row = max_row;
    *out_total = (size_t)total;
    return true;
}

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int32_t ia = (int32_t)a;
    int32_t ib = (int32_t)b;
    int32_t ic = (int32_t)c;
    int32_t p = ia + ib - ic;
    int32_t pa = p >= ia ? (p - ia) : (ia - p);
    int32_t pb = p >= ib ? (p - ib) : (ib - p);
    int32_t pc = p >= ic ? (p - ic) : (ic - p);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static im_image_error unfilter_row(uint8_t filter, uint8_t *curr, const uint8_t *prev, size_t row_bytes, size_t bpp) {
    switch (filter) {
        case 0u:
            return IM_IMAGE_OK;
        case 1u:
            for (size_t i = bpp; i < row_bytes; i++) {
                curr[i] = (uint8_t)(curr[i] + curr[i - bpp]);
            }
            return IM_IMAGE_OK;
        case 2u:
            for (size_t i = 0; i < row_bytes; i++) {
                curr[i] = (uint8_t)(curr[i] + prev[i]);
            }
            return IM_IMAGE_OK;
        case 3u:
            for (size_t i = 0; i < row_bytes; i++) {
                uint32_t left = (i >= bpp) ? (uint32_t)curr[i - bpp] : 0u;
                uint32_t up = (uint32_t)prev[i];
                curr[i] = (uint8_t)(curr[i] + ((left + up) >> 1));
            }
            return IM_IMAGE_OK;
        case 4u:
            for (size_t i = 0; i < row_bytes; i++) {
                uint8_t left = (i >= bpp) ? curr[i - bpp] : 0u;
                uint8_t up = prev[i];
                uint8_t upleft = (i >= bpp) ? prev[i - bpp] : 0u;
                curr[i] = (uint8_t)(curr[i] + paeth_predictor(left, up, upleft));
            }
            return IM_IMAGE_OK;
        default:
            return IM_IMAGE_INVALID;
    }
}

static uint8_t expand_gray_subbyte(uint8_t v, uint8_t bit_depth) {
    if (bit_depth == 1u) return v ? 255u : 0u;
    if (bit_depth == 2u) return (uint8_t)(v * 85u);
    return (uint8_t)(v * 17u);
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

typedef struct png_decode_ctx {
    im_png_info info;
    im_pixel_format fmt;
    uint8_t *pixels;
    size_t stride;
    bool seen_plte;
    uint32_t plte_count;
    uint8_t plte[256][3];
    bool seen_trns;
    uint8_t trns_alpha[256];
    uint16_t trns_gray;
    uint16_t trns_r;
    uint16_t trns_g;
    uint16_t trns_b;
    size_t bpp;
    uint8_t *curr_row;
    uint8_t *prev_row;
    int pass;
    uint32_t pass_w;
    uint32_t pass_h;
    size_t pass_row_bytes;
    uint32_t row_y;
    size_t row_have;
    size_t total_filtered;
    size_t expected_filtered;
} png_decode_ctx;

static void advance_pass(png_decode_ctx *ctx) {
    if (ctx->info.interlace == 0u) {
        if (ctx->pass == 0) {
            ctx->pass_w = ctx->info.width;
            ctx->pass_h = ctx->info.height;
            calc_row_bytes(ctx->pass_w, ctx->info.color_type, ctx->info.bit_depth, &ctx->pass_row_bytes);
            ctx->row_y = 0u;
            ctx->row_have = 0u;
            png_zero(ctx->prev_row, ctx->pass_row_bytes);
        } else {
            ctx->pass_w = 0u;
            ctx->pass_h = 0u;
            ctx->pass_row_bytes = 0u;
        }
        return;
    }
    while (ctx->pass < 7) {
        int p = ctx->pass;
        if (ctx->info.width > (uint32_t)ADAM7_X0[p] && ctx->info.height > (uint32_t)ADAM7_Y0[p]) {
            ctx->pass_w = (ctx->info.width - 1u - (uint32_t)ADAM7_X0[p]) / (uint32_t)ADAM7_DX[p] + 1u;
            ctx->pass_h = (ctx->info.height - 1u - (uint32_t)ADAM7_Y0[p]) / (uint32_t)ADAM7_DY[p] + 1u;
            calc_row_bytes(ctx->pass_w, ctx->info.color_type, ctx->info.bit_depth, &ctx->pass_row_bytes);
            ctx->row_y = 0u;
            ctx->row_have = 0u;
            png_zero(ctx->prev_row, ctx->pass_row_bytes);
            return;
        }
        ctx->pass++;
    }
    ctx->pass_w = 0u;
    ctx->pass_h = 0u;
    ctx->pass_row_bytes = 0u;
}

static im_image_error unpack_row(png_decode_ctx *ctx, const uint8_t *row) {
    uint8_t ct = ctx->info.color_type;
    uint8_t bd = ctx->info.bit_depth;
    uint32_t y_dst = (ctx->info.interlace == 0u)
        ? ctx->row_y
        : ((uint32_t)ADAM7_Y0[ctx->pass] + ctx->row_y * (uint32_t)ADAM7_DY[ctx->pass]);
    uint32_t x0 = (ctx->info.interlace == 0u) ? 0u : (uint32_t)ADAM7_X0[ctx->pass];
    uint32_t dx = (ctx->info.interlace == 0u) ? 1u : (uint32_t)ADAM7_DX[ctx->pass];
    uint8_t *dst_line = ctx->pixels + (size_t)y_dst * ctx->stride;

    for (uint32_t x = 0; x < ctx->pass_w; x++) {
        uint8_t *px = dst_line + (size_t)(x0 + x * dx) * 4u;
        uint8_t r = 0u, g = 0u, b = 0u, a = 255u;
        if (ct == 0u) {
            if (bd < 8u) {
                uint32_t ppb = 8u / (uint32_t)bd;
                uint32_t shift = (ppb - 1u - (x % ppb)) * (uint32_t)bd;
                uint8_t mask = (uint8_t)((1u << bd) - 1u);
                uint8_t raw_v = (uint8_t)((row[x / ppb] >> shift) & mask);
                uint8_t v = expand_gray_subbyte(raw_v, bd);
                r = g = b = v;
                if (ctx->seen_trns && (uint16_t)raw_v == ctx->trns_gray) a = 0u;
            } else if (bd == 8u) {
                uint8_t raw_v = row[x];
                r = g = b = raw_v;
                if (ctx->seen_trns && (uint16_t)raw_v == ctx->trns_gray) a = 0u;
            } else {
                uint16_t raw_v = rd_be16(row + (size_t)x * 2u);
                uint8_t v = (uint8_t)(raw_v >> 8);
                r = g = b = v;
                if (ctx->seen_trns && raw_v == ctx->trns_gray) a = 0u;
            }
        } else if (ct == 2u) {
            if (bd == 8u) {
                const uint8_t *s = row + (size_t)x * 3u;
                r = s[0];
                g = s[1];
                b = s[2];
                if (ctx->seen_trns && (uint16_t)r == ctx->trns_r && (uint16_t)g == ctx->trns_g && (uint16_t)b == ctx->trns_b) a = 0u;
            } else {
                const uint8_t *s = row + (size_t)x * 6u;
                uint16_t rr = rd_be16(s);
                uint16_t gg = rd_be16(s + 2);
                uint16_t bb = rd_be16(s + 4);
                r = (uint8_t)(rr >> 8);
                g = (uint8_t)(gg >> 8);
                b = (uint8_t)(bb >> 8);
                if (ctx->seen_trns && rr == ctx->trns_r && gg == ctx->trns_g && bb == ctx->trns_b) a = 0u;
            }
        } else if (ct == 3u) {
            uint8_t idx = 0u;
            if (bd < 8u) {
                uint32_t ppb = 8u / (uint32_t)bd;
                uint32_t shift = (ppb - 1u - (x % ppb)) * (uint32_t)bd;
                uint8_t mask = (uint8_t)((1u << bd) - 1u);
                idx = (uint8_t)((row[x / ppb] >> shift) & mask);
            } else {
                idx = row[x];
            }
            if ((uint32_t)idx >= ctx->plte_count) return IM_IMAGE_INVALID;
            r = ctx->plte[idx][0];
            g = ctx->plte[idx][1];
            b = ctx->plte[idx][2];
            a = ctx->seen_trns ? ctx->trns_alpha[idx] : 255u;
        } else if (ct == 4u) {
            if (bd == 8u) {
                const uint8_t *s = row + (size_t)x * 2u;
                r = g = b = s[0];
                a = s[1];
            } else {
                const uint8_t *s = row + (size_t)x * 4u;
                r = g = b = s[0];
                a = s[2];
            }
        } else {
            if (bd == 8u) {
                const uint8_t *s = row + (size_t)x * 4u;
                r = s[0];
                g = s[1];
                b = s[2];
                a = s[3];
            } else {
                const uint8_t *s = row + (size_t)x * 8u;
                r = s[0];
                g = s[2];
                b = s[4];
                a = s[6];
            }
        }
        write_pixel(px, ctx->fmt, r, g, b, a);
    }
    return IM_IMAGE_OK;
}

im_image_error im_png_probe(const uint8_t *data, size_t len,
                            const im_image_limits *limits,
                            im_png_info *out_info) {
    if (out_info) png_zero(out_info, sizeof(*out_info));
    if (!data || !out_info) return IM_IMAGE_BAD_ARG;

    size_t sig_check = len < 8u ? len : 8u;
    for (size_t i = 0; i < sig_check; i++) {
        if (data[i] != PNG_SIG[i]) return IM_IMAGE_INVALID;
    }
    if (len < 8u) return IM_IMAGE_TRUNCATED;
    if (len < 16u) return IM_IMAGE_TRUNCATED;

    uint32_t ihdr_len = rd_be32(data + 8u);
    const uint8_t *ihdr_type = data + 12u;
    for (int i = 0; i < 4; i++) {
        if (!is_ascii_letter(ihdr_type[i])) return IM_IMAGE_INVALID;
    }
    if (!chunk_is(ihdr_type, 'I', 'H', 'D', 'R') || ihdr_len != 13u) return IM_IMAGE_INVALID;
    if (len < 33u) return IM_IMAGE_TRUNCATED;

    uint32_t calc_crc = im_crc32(data + 12u, 17u);
    uint32_t want_crc = rd_be32(data + 29u);
    if (calc_crc != want_crc) return IM_IMAGE_INVALID;

    const uint8_t *ihdr = data + 16u;
    uint32_t width = rd_be32(ihdr);
    uint32_t height = rd_be32(ihdr + 4u);
    uint8_t bit_depth = ihdr[8];
    uint8_t color_type = ihdr[9];
    uint8_t comp = ihdr[10];
    uint8_t filt = ihdr[11];
    uint8_t interlace = ihdr[12];

    if (width == 0u || width > 0x7FFFFFFFu || height == 0u || height > 0x7FFFFFFFu) return IM_IMAGE_INVALID;
    if (comp != 0u || filt != 0u || interlace > 1u) return IM_IMAGE_INVALID;
    if (!valid_color_depth(color_type, bit_depth)) return IM_IMAGE_INVALID;

    size_t byte_size = 0u;
    im_image_error lim_err = im_image_check_limits(width, height, limits, &byte_size);
    if (lim_err != IM_IMAGE_OK) return lim_err;

    size_t max_row = 0u;
    size_t total_filtered = 0u;
    if (!calc_expected_filtered(width, height, color_type, bit_depth, interlace, &max_row, &total_filtered)) {
        return IM_IMAGE_TOO_LARGE;
    }

    out_info->width = width;
    out_info->height = height;
    out_info->bit_depth = bit_depth;
    out_info->color_type = color_type;
    out_info->interlace = interlace;
    out_info->byte_size = byte_size;
    return IM_IMAGE_OK;
}

im_image_error im_png_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_png_alloc *alloc,
                             im_image *out_image) {
    if (out_image) png_zero(out_image, sizeof(*out_image));
    if (!data || !out_image || !alloc || !alloc->alloc || !alloc->free) return IM_IMAGE_BAD_ARG;
    if (format != IM_PIXEL_BGRA8 && format != IM_PIXEL_RGBA8) return IM_IMAGE_BAD_ARG;

    im_png_info info;
    im_image_error err = im_png_probe(data, len, limits, &info);
    if (err != IM_IMAGE_OK) return err;

    size_t max_row = 0u;
    size_t expected_filtered = 0u;
    if (!calc_expected_filtered(info.width, info.height, info.color_type, info.bit_depth, info.interlace,
                                &max_row, &expected_filtered)) {
        return IM_IMAGE_TOO_LARGE;
    }

    size_t row_buf_size = (max_row + 1u) + max_row;
    im_inflate *inf = (im_inflate *)alloc->alloc(alloc->ctx, sizeof(im_inflate));
    if (!inf) return IM_IMAGE_NO_MEMORY;

    uint8_t *row_buf = (uint8_t *)alloc->alloc(alloc->ctx, row_buf_size);
    if (!row_buf) {
        alloc->free(alloc->ctx, inf, sizeof(im_inflate));
        return IM_IMAGE_NO_MEMORY;
    }

    uint8_t *pixels = (uint8_t *)alloc->alloc(alloc->ctx, info.byte_size);
    if (!pixels) {
        alloc->free(alloc->ctx, row_buf, row_buf_size);
        alloc->free(alloc->ctx, inf, sizeof(im_inflate));
        return IM_IMAGE_NO_MEMORY;
    }

    png_decode_ctx ctx;
    png_zero(&ctx, sizeof(ctx));
    ctx.info = info;
    ctx.fmt = format;
    ctx.pixels = pixels;
    ctx.stride = (size_t)info.width * 4u;
    ctx.bpp = ((size_t)color_channels(info.color_type) * (size_t)info.bit_depth + 7u) >> 3;
    if (ctx.bpp == 0u) ctx.bpp = 1u;
    ctx.curr_row = row_buf;
    ctx.prev_row = row_buf + (max_row + 1u);
    ctx.expected_filtered = expected_filtered;

    bool seen_idat = false;
    bool idat_closed = false;
    bool seen_iend = false;
    bool zlib_done = false;
    im_image_error result = IM_IMAGE_OK;

    size_t pos = 33u;
    while (pos < len) {
        if (seen_iend) {
            result = IM_IMAGE_INVALID;
            goto fail;
        }
        if (len - pos < 12u) {
            result = IM_IMAGE_TRUNCATED;
            goto fail;
        }
        uint32_t clen = rd_be32(data + pos);
        const uint8_t *ctype = data + pos + 4u;
        if (clen > 0x7FFFFFFFu) {
            result = IM_IMAGE_INVALID;
            goto fail;
        }
        for (int i = 0; i < 4; i++) {
            if (!is_ascii_letter(ctype[i])) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
        }
        if (len - pos - 12u < (size_t)clen) {
            result = IM_IMAGE_TRUNCATED;
            goto fail;
        }

        uint32_t calc_crc = im_crc32(ctype, (size_t)clen + 4u);
        uint32_t want_crc = rd_be32(data + pos + 8u + (size_t)clen);
        if (calc_crc != want_crc) {
            result = IM_IMAGE_INVALID;
            goto fail;
        }

        const uint8_t *cdata = data + pos + 8u;
        if (chunk_is(ctype, 'I', 'H', 'D', 'R')) {
            result = IM_IMAGE_INVALID;
            goto fail;
        } else if (chunk_is(ctype, 'P', 'L', 'T', 'E')) {
            if (ctx.seen_plte || seen_idat || ctx.seen_trns || info.color_type == 0u || info.color_type == 4u) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            if (clen < 3u || clen > 768u || (clen % 3u) != 0u) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            uint32_t count = clen / 3u;
            if (info.color_type == 3u && count > (1u << info.bit_depth)) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            for (uint32_t i = 0; i < count; i++) {
                ctx.plte[i][0] = cdata[i * 3u + 0u];
                ctx.plte[i][1] = cdata[i * 3u + 1u];
                ctx.plte[i][2] = cdata[i * 3u + 2u];
            }
            ctx.plte_count = count;
            ctx.seen_plte = true;
        } else if (chunk_is(ctype, 't', 'R', 'N', 'S')) {
            if (ctx.seen_trns || seen_idat || info.color_type == 4u || info.color_type == 6u) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            if (info.color_type == 0u) {
                if (clen != 2u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                ctx.trns_gray = rd_be16(cdata);
            } else if (info.color_type == 2u) {
                if (clen != 6u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                ctx.trns_r = rd_be16(cdata);
                ctx.trns_g = rd_be16(cdata + 2u);
                ctx.trns_b = rd_be16(cdata + 4u);
            } else if (info.color_type == 3u) {
                if (!ctx.seen_plte || clen < 1u || clen > ctx.plte_count) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                for (uint32_t i = 0; i < 256u; i++) ctx.trns_alpha[i] = 255u;
                for (uint32_t i = 0; i < clen; i++) ctx.trns_alpha[i] = cdata[i];
            }
            ctx.seen_trns = true;
        } else if (chunk_is(ctype, 'I', 'D', 'A', 'T')) {
            if (idat_closed) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            if (info.color_type == 3u && !ctx.seen_plte) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            if (!seen_idat) {
                seen_idat = true;
                im_inflate_init(inf, IM_INFLATE_ZLIB, true, expected_filtered);
                ctx.pass = 0;
                advance_pass(&ctx);
            }
            if (clen == 0u) {
                /* Empty IDAT chunks are valid before, between, or after non-empty IDAT chunks. */
            } else if (zlib_done) {
                /* Trailing compressed data in a subsequent non-empty IDAT after zlib completion. */
                result = IM_IMAGE_INVALID;
                goto fail;
            } else {
                size_t in_pos = 0u;
                while (true) {
                    size_t row_need = (ctx.pass_h > 0u) ? (1u + ctx.pass_row_bytes) : 0u;
                    uint8_t *out_ptr = (row_need > ctx.row_have) ? (ctx.curr_row + ctx.row_have) : NULL;
                    size_t out_cap = (row_need > ctx.row_have) ? (row_need - ctx.row_have) : 0u;
                    size_t in_used = 0u;
                    size_t out_used = 0u;
                    im_inflate_status st = im_inflate_step(
                        inf,
                        cdata + in_pos, (size_t)clen - in_pos, false, &in_used,
                        out_ptr, out_cap, &out_used);
                    in_pos += in_used;
                    ctx.row_have += out_used;
                    ctx.total_filtered += out_used;

                    if (row_need > 0u && ctx.row_have == row_need) {
                        im_image_error uerr = unfilter_row(ctx.curr_row[0], ctx.curr_row + 1u, ctx.prev_row,
                                                           ctx.pass_row_bytes, ctx.bpp);
                        if (uerr != IM_IMAGE_OK) {
                            result = uerr;
                            goto fail;
                        }
                        im_image_error perr = unpack_row(&ctx, ctx.curr_row + 1u);
                        if (perr != IM_IMAGE_OK) {
                            result = perr;
                            goto fail;
                        }
                        png_copy(ctx.prev_row, ctx.curr_row + 1u, ctx.pass_row_bytes);
                        ctx.row_have = 0u;
                        ctx.row_y++;
                        if (ctx.row_y == ctx.pass_h) {
                            ctx.pass++;
                            advance_pass(&ctx);
                        }
                    }

                    if (st == IM_INFLATE_OK) {
                        zlib_done = true;
                        if (in_pos < (size_t)clen) {
                            result = IM_IMAGE_INVALID;
                            goto fail;
                        }
                        break;
                    } else if (st == IM_INFLATE_NEED_INPUT) {
                        if (in_pos == (size_t)clen) break;
                        if (in_used == 0u && out_used == 0u) {
                            result = IM_IMAGE_INVALID;
                            goto fail;
                        }
                    } else if (st == IM_INFLATE_OUT_FULL) {
                        if (ctx.pass_h == 0u) {
                            /* All expected rows decoded, yet zlib still wants to emit more output. */
                            result = IM_IMAGE_INVALID;
                            goto fail;
                        }
                    } else {
                        result = IM_IMAGE_INVALID;
                        goto fail;
                    }
                }
            }
        } else if (chunk_is(ctype, 'I', 'E', 'N', 'D')) {
            if (clen != 0u) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            if (seen_idat) idat_closed = true;
            seen_iend = true;
        } else {
            if (seen_idat) idat_closed = true;
            if ((ctype[0] & 0x20u) == 0u) {
                result = IM_IMAGE_UNSUPPORTED;
                goto fail;
            }
        }

        pos += 12u + (size_t)clen;
    }

    if (!seen_iend) {
        result = IM_IMAGE_TRUNCATED;
        goto fail;
    }
    if (!seen_idat) {
        result = IM_IMAGE_INVALID;
        goto fail;
    }
    if (!zlib_done) {
        size_t in_used = 0u;
        size_t out_used = 0u;
        im_inflate_status st = im_inflate_step(inf, NULL, 0u, true, &in_used, NULL, 0u, &out_used);
        if (st != IM_INFLATE_OK) {
            result = IM_IMAGE_INVALID;
            goto fail;
        }
        zlib_done = true;
    }
    if (ctx.total_filtered != expected_filtered || ctx.pass_h != 0u) {
        result = IM_IMAGE_INVALID;
        goto fail;
    }

    alloc->free(alloc->ctx, row_buf, row_buf_size);
    alloc->free(alloc->ctx, inf, sizeof(im_inflate));

    out_image->width = info.width;
    out_image->height = info.height;
    out_image->stride = info.width * 4u;
    out_image->format = format;
    out_image->pixels = pixels;
    out_image->byte_size = info.byte_size;
    return IM_IMAGE_OK;

fail:
    alloc->free(alloc->ctx, pixels, info.byte_size);
    alloc->free(alloc->ctx, row_buf, row_buf_size);
    alloc->free(alloc->ctx, inf, sizeof(im_inflate));
    png_zero(out_image, sizeof(*out_image));
    return result;
}
