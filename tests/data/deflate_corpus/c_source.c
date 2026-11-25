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
                uint16_t mask = (info.bit_depth == 16u) ? 0xFFFFu : (uint16_t)((1u << info.bit_depth) - 1u);
                ctx.trns_gray = (uint16_t)(rd_be16(cdata) & mask);
            } else if (info.color_type == 2u) {
                if (clen != 6u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                uint16_t mask = (info.bit_depth == 16u) ? 0xFFFFu : (uint16_t)((1u << info.bit_depth) - 1u);
                ctx.trns_r = (uint16_t)(rd_be16(cdata) & mask);
                ctx.trns_g = (uint16_t)(rd_be16(cdata + 2u) & mask);
                ctx.trns_b = (uint16_t)(rd_be16(cdata + 4u) & mask);
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
            if (clen == 0u || zlib_done) {
                /* Empty IDATs and unused trailing bytes in consecutive CRC-verified IDATs after
                   zlib completion are ignored per PNG §11.2.3. */
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
                        /* Unused trailing bytes in this CRC-verified IDAT are ignored per PNG §11.2.3. */
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

typedef struct bmp_chan {
    uint32_t mask;
    uint8_t shift;
    uint8_t bits;
    bool contiguous;
    uint64_t max_v;
    uint8_t def_zero;
} bmp_chan;

static void init_chan(bmp_chan *c, uint32_t mask, uint8_t default_if_zero) {
    c->mask = mask;
    c->def_zero = default_if_zero;
    if (mask == 0u) {
        c->shift = 0u;
        c->bits = 0u;
        c->contiguous = true;
        c->max_v = 1u;
        return;
    }
    uint8_t shift = 0u;
    while (((mask >> shift) & 1u) == 0u) shift++;
    uint32_t shifted = mask >> shift;
    uint8_t bits = 0u;
    for (uint32_t i = 0u; i < 32u; i++) {
        if ((mask >> i) & 1u) bits++;
    }
    uint64_t max_v = ((uint64_t)1u << bits) - 1u;
    c->shift = shift;
    c->bits = bits;
    c->contiguous = ((uint64_t)shifted == max_v);
    c->max_v = max_v;
}

/* Extract bits selected by a precomputed channel descriptor and scale to 0..255
   using rounded 64-bit integer arithmetic: (v * 255 + max_v / 2) / max_v. */
static uint8_t scale_chan(uint32_t word, const bmp_chan *c) {
    if (c->mask == 0u) return c->def_zero;
    uint32_t v;
    if (c->contiguous) {
        v = (word & c->mask) >> c->shift;
    } else {
        v = 0u;
        uint32_t b = 0u;
        for (uint32_t i = 0u; i < 32u; i++) {
            if ((c->mask >> i) & 1u) {
                if ((word >> i) & 1u) v |= (1u << b);
                b++;
            }
        }
    }
    return (uint8_t)(((uint64_t)v * 255u + (c->max_v >> 1)) / c->max_v);
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
    uint64_t palette_byte_len;
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
        }
        /* BI_RGB (comp == 0) is always opaque (5-5-5 for 16-bpp, BGRX for 32-bpp). */
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
    uint64_t pal_bytes = 0u;
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
        pal_bytes = (uint64_t)pal_entries * (uint64_t)pal_entry_size;
        if (pal_off + pal_bytes > (uint64_t)bf_off) return IM_IMAGE_INVALID;
    } else if (clr_used > 0u) {
        pal_bytes = (uint64_t)clr_used * (uint64_t)pal_entry_size;
        if (pal_off + pal_bytes > (uint64_t)bf_off) return IM_IMAGE_INVALID;
    }

    /* OS/2 1.x/2.x (12/16/64-byte DIB, or 40-byte OS/2 v2 when bfOffBits > 54) may store the
       combined header size in bfSize (14 + hdr_size). */
    bool os2_hdr_size_conv = ((is_os2_1 || is_os2_2) && bf_size == 14u + hdr_size) ||
                             (hdr_size == 40u && bf_size == 54u && bf_off > 54u);
    if (bf_size != 0u && !os2_hdr_size_conv) {
        if (bf_size < bf_off) return IM_IMAGE_INVALID;
        uint64_t decl_payload = (uint64_t)bf_size - (uint64_t)bf_off;
        bool is_rle_hdr = (comp == 1u || comp == 2u);
        if (!is_rle_hdr) {
            if (raw_bytes64 > decl_payload) return IM_IMAGE_INVALID;
            if (img_size != 0u && ((uint64_t)img_size < raw_bytes64 || (uint64_t)img_size > decl_payload)) {
                return IM_IMAGE_INVALID;
            }
        } else if (img_size != 0u && (uint64_t)img_size > decl_payload) {
            return IM_IMAGE_INVALID;
        }
    } else if (comp != 1u && comp != 2u && img_size != 0u && (uint64_t)img_size < raw_bytes64) {
        return IM_IMAGE_INVALID;
    }

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
    out->palette_byte_len = pal_bytes;
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
    uint64_t pal_end = pal_start + p->palette_byte_len;
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
    bool is_os2 = (p.info.header_size == 12u || p.info.header_size == 16u || p.info.header_size == 64u);
    bool os2_hdr_size_conv = (is_os2 && p.bf_size == 14u + p.info.header_size) ||
                             (p.info.header_size == 40u && p.bf_size == 54u && p.bf_off_bits > 54u);
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
        bmp_chan rc, gc, bc, ac;
        if (p.use_bitfields) {
            init_chan(&rc, p.r_mask, 0u);
            init_chan(&gc, p.g_mask, 0u);
            init_chan(&bc, p.b_mask, 0u);
            init_chan(&ac, p.a_mask, 255u);
        }

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
                        r = scale_chan(word, &rc);
                        g = scale_chan(word, &gc);
                        b = scale_chan(word, &bc);
                        a = scale_chan(word, &ac);
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
                        r = scale_chan(word, &rc);
                        g = scale_chan(word, &gc);
                        b = scale_chan(word, &bc);
                        a = scale_chan(word, &ac);
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
