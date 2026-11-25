/* Impossible SDK: portable freestanding streaming DEFLATE (RFC 1951), zlib (RFC 1950), and gzip (RFC 1952) inflater. */
#include "inflate.h"
#include "checksum.h"

enum inf_state {
    S_ZLIB_HDR = 0,
    S_GZIP_HDR,
    S_GZIP_FEXTRA_LEN,
    S_GZIP_FEXTRA_DATA,
    S_GZIP_FNAME,
    S_GZIP_FCOMMENT,
    S_GZIP_FHCRC,
    S_BLOCK_HDR,
    S_STORED_LEN,
    S_STORED_DATA,
    S_DYN_HDR,
    S_DYN_CLENS,
    S_DYN_LENS,
    S_DYN_LEN_EXTRA,
    S_SYMBOL,
    S_LEN_EXTRA,
    S_DIST,
    S_DIST_EXTRA,
    S_COPY,
    S_ZLIB_TRAILER,
    S_GZIP_TRAILER,
    S_GZIP_NEXT,
    S_DONE,
    S_BAD_ARG,
    S_ERR
};

static const uint8_t k_clen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static const uint16_t k_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t k_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const uint16_t k_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t k_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void zero_bytes(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; ++i) b[i] = 0;
}

/* Build canonical Huffman table from lengths[0..n-1].
   allow_empty: true for distance tree (literal-only block).
   allow_single: true for distance tree (1 symbol of length 1) or litlen tree (single EOB of length 1). */
static bool huff_build(im_inflate_huff *h, const uint8_t *lengths, uint16_t n,
                       bool allow_empty, bool allow_single) {
    for (int i = 0; i < 16; ++i) h->count[i] = 0;
    uint16_t nonzero = 0;
    for (uint16_t i = 0; i < n; ++i) {
        if (lengths[i] > 15u) return false;
        h->count[lengths[i]]++;
        if (lengths[i] != 0) ++nonzero;
    }
    h->num_syms = nonzero;
    if (nonzero == 0) return allow_empty;

    int32_t left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= (int32_t)h->count[len];
        if (left < 0) return false; /* oversubscribed */
    }
    if (left > 0) {
        /* Incomplete tree is legal ONLY for single-symbol degenerate trees (RFC 1951 s3.2.7). */
        if (!(allow_single && nonzero == 1u && h->count[1] == 1u)) return false;
    }

    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) {
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    }
    for (uint16_t sym = 0; sym < n; ++sym) {
        if (lengths[sym] != 0) {
            h->symbol[offs[lengths[sym]]++] = sym;
        }
    }
    return true;
}

static void build_fixed_tables(im_inflate *st) {
    uint8_t lens[288];
    for (int i = 0; i <= 143; ++i) lens[i] = 8;
    for (int i = 144; i <= 255; ++i) lens[i] = 9;
    for (int i = 256; i <= 279; ++i) lens[i] = 7;
    for (int i = 280; i <= 287; ++i) lens[i] = 8;
    huff_build(&st->lit_huff, lens, 288u, false, false);

    uint8_t dlens[32];
    for (int i = 0; i < 32; ++i) dlens[i] = 5;
    huff_build(&st->dist_huff, dlens, 32u, false, false);
}

/* Pull bytes into bitbuf until bitcnt >= need or input is exhausted. */
static bool ensure_bits(im_inflate *st, uint32_t need, const uint8_t *in, size_t in_len, size_t *ip) {
    while (st->bitcnt < need) {
        if (*ip >= in_len) return false;
        st->bitbuf |= ((uint64_t)in[(*ip)++]) << st->bitcnt;
        st->bitcnt += 8u;
    }
    return true;
}

/* Decode one canonical Huffman symbol without consuming bits on NEED_INPUT.
   Returns: 1 = decoded into *out_sym, 0 = need more input, -1 = invalid code. */
static int huff_decode(im_inflate *st, const im_inflate_huff *h,
                       const uint8_t *in, size_t in_len, size_t *ip, uint16_t *out_sym) {
    if (h->num_syms == 0) return -1;
    int32_t code = 0;
    int32_t first = 0;
    int32_t index = 0;
    for (uint32_t len = 1u; len <= 15u; ++len) {
        if (!ensure_bits(st, len, in, in_len, ip)) return 0;
        code |= (int32_t)((st->bitbuf >> (len - 1u)) & 1u);
        int32_t cnt = (int32_t)h->count[len];
        if (code - cnt < first) {
            st->bitbuf >>= len;
            st->bitcnt -= len;
            *out_sym = h->symbol[index + (code - first)];
            return 1;
        }
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static void drop_to_byte(im_inflate *st) {
    uint32_t rem = st->bitcnt & 7u;
    st->bitbuf >>= rem;
    st->bitcnt -= rem;
}

/* Read one aligned byte (consuming from bitbuf first if >= 8 bits are buffered, else from input). */
static bool read_aligned_byte(im_inflate *st, const uint8_t *in, size_t in_len, size_t *ip, uint8_t *out_b) {
    if (st->bitcnt >= 8u) {
        *out_b = (uint8_t)(st->bitbuf & 0xFFu);
        st->bitbuf >>= 8;
        st->bitcnt -= 8u;
        return true;
    }
    if (*ip >= in_len) return false;
    *out_b = in[(*ip)++];
    return true;
}

/* Push any whole bytes left in bitbuf back onto the input pointer so trailing bytes stay unconsumed! */
static void return_buffered_bytes(im_inflate *st, size_t *ip) {
    drop_to_byte(st);
    while (st->bitcnt >= 8u && *ip > 0u) {
        st->bitcnt -= 8u;
        (*ip)--;
    }
    st->bitbuf = 0;
    st->bitcnt = 0;
}

static void reset_member(im_inflate *st) {
    st->member_out = 0;
    st->win_pos = 0;
    st->bfinal = 0;
    st->hdr_have = 0;
    st->extra_rem = 0;
    st->stored_rem = 0;
    st->bitbuf = 0;
    st->bitcnt = 0;
    st->max_window = 32768u;
    st->check = (st->fmt == IM_INFLATE_ZLIB) ? im_adler32_init() : im_crc32_init();
    st->hdr_crc = im_crc32_init();
}

void im_inflate_init(im_inflate *st, im_inflate_format fmt, bool has_limit, size_t output_limit) {
    if (!st) return;
    zero_bytes(st, sizeof(*st));
    st->fmt = fmt;
    st->has_limit = has_limit;
    st->output_limit = output_limit;
    reset_member(st);
    if (fmt == IM_INFLATE_RAW) st->state = S_BLOCK_HDR;
    else if (fmt == IM_INFLATE_ZLIB) st->state = S_ZLIB_HDR;
    else if (fmt == IM_INFLATE_GZIP) st->state = S_GZIP_HDR;
    else st->state = S_BAD_ARG;
}

/* Emit one output byte into out[*op] + sliding window (checksum is updated in batch over contiguous spans).
   Returns: 1 = emitted, 0 = output buffer full, -1 = output_limit exceeded. */
static int emit_byte(im_inflate *st, uint8_t b, uint8_t *out, size_t out_cap, size_t *op) {
    if (st->has_limit && st->total_out >= (uint64_t)st->output_limit) return -1;
    if (*op >= out_cap) return 0;
    out[(*op)++] = b;
    st->window[st->win_pos] = b;
    st->win_pos = (st->win_pos + 1u) & 32767u;
    st->total_out++;
    st->member_out++;
    return 1;
}

static void flush_check_span(im_inflate *st, const uint8_t *out, size_t *chk_from, size_t op) {
    if (op > *chk_from) {
        if (st->fmt == IM_INFLATE_ZLIB) st->check = im_adler32_update(st->check, out + *chk_from, op - *chk_from);
        else if (st->fmt == IM_INFLATE_GZIP) st->check = im_crc32_update(st->check, out + *chk_from, op - *chk_from);
        *chk_from = op;
    }
}

im_inflate_status im_inflate_step(im_inflate *st,
                                  const uint8_t *in, size_t in_len, bool is_eof, size_t *in_used,
                                  uint8_t *out, size_t out_cap, size_t *out_used) {
    if (in_used) *in_used = 0;
    if (out_used) *out_used = 0;
    if (!st || (in_len > 0u && !in) || (out_cap > 0u && !out)) return IM_INFLATE_BAD_ARG;
    if (st->state == S_BAD_ARG) return IM_INFLATE_BAD_ARG;
    if (st->state == S_ERR) return IM_INFLATE_BAD_DATA;
    if (st->state == S_DONE) return IM_INFLATE_OK;

    size_t ip = 0;
    size_t op = 0;
    size_t chk_from = 0;
    im_inflate_status res = IM_INFLATE_NEED_INPUT;

    for (;;) {
        switch (st->state) {
        case S_ZLIB_HDR: {
            while (st->hdr_have < 2u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
            }
            uint8_t cmf = st->hdr_buf[0];
            uint8_t flg = st->hdr_buf[1];
            uint32_t cm = cmf & 0x0Fu;
            uint32_t cinfo = (cmf >> 4) & 0x0Fu;
            if (cm != 8u || cinfo > 7u || (((uint32_t)cmf * 256u + (uint32_t)flg) % 31u) != 0u) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            if ((flg & 0x20u) != 0u) {
                st->state = S_ERR;
                res = IM_INFLATE_NEED_DICT;
                goto done;
            }
            st->max_window = 1u << (cinfo + 8u);
            st->hdr_have = 0;
            st->state = S_BLOCK_HDR;
            break;
        }

        case S_GZIP_HDR: {
            while (st->hdr_have < 10u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
                st->hdr_crc = im_crc32_update(st->hdr_crc, &b, 1u);
            }
            if (st->hdr_buf[0] != 0x1Fu || st->hdr_buf[1] != 0x8Bu || st->hdr_buf[2] != 8u ||
                (st->hdr_buf[3] & 0xE0u) != 0u) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            st->gzip_flg = st->hdr_buf[3];
            st->hdr_have = 0;
            if (st->gzip_flg & 0x04u) st->state = S_GZIP_FEXTRA_LEN;
            else if (st->gzip_flg & 0x08u) st->state = S_GZIP_FNAME;
            else if (st->gzip_flg & 0x10u) st->state = S_GZIP_FCOMMENT;
            else if (st->gzip_flg & 0x02u) st->state = S_GZIP_FHCRC;
            else st->state = S_BLOCK_HDR;
            break;
        }

        case S_GZIP_FEXTRA_LEN: {
            while (st->hdr_have < 2u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
                st->hdr_crc = im_crc32_update(st->hdr_crc, &b, 1u);
            }
            st->extra_rem = (uint32_t)st->hdr_buf[0] | ((uint32_t)st->hdr_buf[1] << 8);
            st->hdr_have = 0;
            st->state = S_GZIP_FEXTRA_DATA;
            break;
        }

        case S_GZIP_FEXTRA_DATA: {
            while (st->extra_rem > 0u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_crc = im_crc32_update(st->hdr_crc, &b, 1u);
                st->extra_rem--;
            }
            if (st->gzip_flg & 0x08u) st->state = S_GZIP_FNAME;
            else if (st->gzip_flg & 0x10u) st->state = S_GZIP_FCOMMENT;
            else if (st->gzip_flg & 0x02u) st->state = S_GZIP_FHCRC;
            else st->state = S_BLOCK_HDR;
            break;
        }

        case S_GZIP_FNAME: {
            for (;;) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_crc = im_crc32_update(st->hdr_crc, &b, 1u);
                if (b == 0u) break;
            }
            if (st->gzip_flg & 0x10u) st->state = S_GZIP_FCOMMENT;
            else if (st->gzip_flg & 0x02u) st->state = S_GZIP_FHCRC;
            else st->state = S_BLOCK_HDR;
            break;
        }

        case S_GZIP_FCOMMENT: {
            for (;;) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_crc = im_crc32_update(st->hdr_crc, &b, 1u);
                if (b == 0u) break;
            }
            if (st->gzip_flg & 0x02u) st->state = S_GZIP_FHCRC;
            else st->state = S_BLOCK_HDR;
            break;
        }

        case S_GZIP_FHCRC: {
            while (st->hdr_have < 2u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
            }
            uint16_t want = (uint16_t)((uint16_t)st->hdr_buf[0] | ((uint16_t)st->hdr_buf[1] << 8));
            if (want != (uint16_t)(st->hdr_crc & 0xFFFFu)) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_CHECKSUM;
                goto done;
            }
            st->hdr_have = 0;
            st->state = S_BLOCK_HDR;
            break;
        }

        case S_BLOCK_HDR: {
            if (!ensure_bits(st, 3u, in, in_len, &ip)) {
                res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                goto done;
            }
            st->bfinal = (uint8_t)(st->bitbuf & 1u);
            uint32_t btype = (uint32_t)((st->bitbuf >> 1) & 3u);
            st->bitbuf >>= 3;
            st->bitcnt -= 3u;
            if (btype == 0u) {
                drop_to_byte(st);
                st->hdr_have = 0;
                st->state = S_STORED_LEN;
            } else if (btype == 1u) {
                build_fixed_tables(st);
                st->state = S_SYMBOL;
            } else if (btype == 2u) {
                st->state = S_DYN_HDR;
            } else {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            break;
        }

        case S_STORED_LEN: {
            while (st->hdr_have < 4u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
            }
            uint16_t slen = (uint16_t)((uint16_t)st->hdr_buf[0] | ((uint16_t)st->hdr_buf[1] << 8));
            uint16_t nlen = (uint16_t)((uint16_t)st->hdr_buf[2] | ((uint16_t)st->hdr_buf[3] << 8));
            if ((uint16_t)(slen ^ 0xFFFFu) != nlen) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            st->stored_rem = slen;
            st->hdr_have = 0;
            st->state = S_STORED_DATA;
            break;
        }

        case S_STORED_DATA: {
            while (st->stored_rem > 0u) {
                if (st->has_limit && st->total_out >= (uint64_t)st->output_limit) {
                    res = IM_INFLATE_LIMIT;
                    goto done;
                }
                if (op >= out_cap) {
                    res = IM_INFLATE_OUT_FULL;
                    goto done;
                }
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                (void)emit_byte(st, b, out, out_cap, &op);
                st->stored_rem--;
            }
            if (st->bfinal) {
                if (st->fmt == IM_INFLATE_RAW) {
                    return_buffered_bytes(st, &ip);
                    st->state = S_DONE;
                    res = IM_INFLATE_OK;
                    goto done;
                }
                drop_to_byte(st);
                st->hdr_have = 0;
                st->state = (st->fmt == IM_INFLATE_ZLIB) ? S_ZLIB_TRAILER : S_GZIP_TRAILER;
            } else {
                st->state = S_BLOCK_HDR;
            }
            break;
        }

        case S_DYN_HDR: {
            if (!ensure_bits(st, 14u, in, in_len, &ip)) {
                res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                goto done;
            }
            st->hlit = (uint16_t)((st->bitbuf & 31u) + 257u);
            st->hdist = (uint16_t)(((st->bitbuf >> 5) & 31u) + 1u);
            st->hclen = (uint16_t)(((st->bitbuf >> 10) & 15u) + 4u);
            st->bitbuf >>= 14;
            st->bitcnt -= 14u;
            /* RFC 1951 s3.2.7: HDIST + 1 may declare up to 32 entries (reserved 30/31 rejected if decoded). */
            if (st->hlit > 286u || st->hdist > 32u) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            for (int i = 0; i < 19; ++i) st->clen_lens[i] = 0;
            st->len_idx = 0;
            st->state = S_DYN_CLENS;
            break;
        }

        case S_DYN_CLENS: {
            while (st->len_idx < st->hclen) {
                if (!ensure_bits(st, 3u, in, in_len, &ip)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->clen_lens[k_clen_order[st->len_idx++]] = (uint8_t)(st->bitbuf & 7u);
                st->bitbuf >>= 3;
                st->bitcnt -= 3u;
            }
            if (!huff_build(&st->code_huff, st->clen_lens, 19u, false, true)) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            st->len_idx = 0;
            st->state = S_DYN_LENS;
            break;
        }

        case S_DYN_LENS: {
            uint16_t total = (uint16_t)(st->hlit + st->hdist);
            while (st->len_idx < total) {
                uint16_t sym = 0;
                int d = huff_decode(st, &st->code_huff, in, in_len, &ip, &sym);
                if (d == 0) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                if (d < 0) {
                    st->state = S_ERR;
                    res = IM_INFLATE_BAD_DATA;
                    goto done;
                }
                if (sym < 16u) {
                    st->code_lens[st->len_idx++] = (uint8_t)sym;
                } else {
                    st->sym_code = sym;
                    st->state = S_DYN_LEN_EXTRA;
                    goto dyn_len_extra;
                }
            }
            /* RFC 1951 s3.2.7: EOB (256) must be present; distance tree may be empty or single-symbol. */
            if (st->code_lens[256] == 0u ||
                !huff_build(&st->lit_huff, st->code_lens, st->hlit, false, true) ||
                !huff_build(&st->dist_huff, st->code_lens + st->hlit, st->hdist, true, true)) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            st->state = S_SYMBOL;
            break;
        }

        dyn_len_extra:
        case S_DYN_LEN_EXTRA: {
            uint32_t ebits = (st->sym_code == 16u) ? 2u : ((st->sym_code == 17u) ? 3u : 7u);
            if (!ensure_bits(st, ebits, in, in_len, &ip)) {
                res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                goto done;
            }
            uint32_t extra = (uint32_t)(st->bitbuf & ((1u << ebits) - 1u));
            st->bitbuf >>= ebits;
            st->bitcnt -= ebits;

            uint8_t val = 0;
            uint32_t rep = 0;
            if (st->sym_code == 16u) {
                if (st->len_idx == 0u) { st->state = S_ERR; res = IM_INFLATE_BAD_DATA; goto done; }
                val = st->code_lens[st->len_idx - 1u];
                rep = 3u + extra;
            } else if (st->sym_code == 17u) {
                val = 0;
                rep = 3u + extra;
            } else {
                val = 0;
                rep = 11u + extra;
            }
            uint16_t total = (uint16_t)(st->hlit + st->hdist);
            if (rep > (uint32_t)(total - st->len_idx)) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            for (uint32_t r = 0; r < rep; ++r) st->code_lens[st->len_idx++] = val;
            st->state = S_DYN_LENS;
            break;
        }

        case S_SYMBOL: {
            uint16_t sym = 0;
            /* Peek limit/output only when we actually need to emit a literal, so EOB succeeds at exact limit! */
            if (st->lit_huff.num_syms == 0) { st->state = S_ERR; res = IM_INFLATE_BAD_DATA; goto done; }
            /* Save bitbuf/ip in case output is full on a literal! */
            uint64_t saved_bb = st->bitbuf;
            uint32_t saved_bc = st->bitcnt;
            size_t saved_ip = ip;
            int d = huff_decode(st, &st->lit_huff, in, in_len, &ip, &sym);
            if (d == 0) {
                res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                goto done;
            }
            if (d < 0) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            if (sym < 256u) {
                int em = emit_byte(st, (uint8_t)sym, out, out_cap, &op);
                if (em < 0) {
                    st->bitbuf = saved_bb; st->bitcnt = saved_bc; ip = saved_ip;
                    res = IM_INFLATE_LIMIT;
                    goto done;
                }
                if (em == 0) {
                    st->bitbuf = saved_bb; st->bitcnt = saved_bc; ip = saved_ip;
                    res = IM_INFLATE_OUT_FULL;
                    goto done;
                }
            } else if (sym == 256u) {
                if (st->bfinal) {
                    if (st->fmt == IM_INFLATE_RAW) {
                        return_buffered_bytes(st, &ip);
                        st->state = S_DONE;
                        res = IM_INFLATE_OK;
                        goto done;
                    }
                    drop_to_byte(st);
                    st->hdr_have = 0;
                    st->state = (st->fmt == IM_INFLATE_ZLIB) ? S_ZLIB_TRAILER : S_GZIP_TRAILER;
                } else {
                    st->state = S_BLOCK_HDR;
                }
            } else if (sym <= 285u) {
                st->sym_code = (uint16_t)(sym - 257u);
                st->state = S_LEN_EXTRA;
            } else {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            break;
        }

        case S_LEN_EXTRA: {
            uint32_t eb = k_len_extra[st->sym_code];
            if (eb > 0u) {
                if (!ensure_bits(st, eb, in, in_len, &ip)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                uint32_t ex = (uint32_t)(st->bitbuf & ((1u << eb) - 1u));
                st->bitbuf >>= eb;
                st->bitcnt -= eb;
                /* Note: length code 284 (sym_code=27) + extra 31 yields 258, matching zlib's historical tolerance. */
                st->copy_len = k_len_base[st->sym_code] + ex;
            } else {
                st->copy_len = k_len_base[st->sym_code];
            }
            st->state = S_DIST;
            break;
        }

        case S_DIST: {
            uint16_t dsym = 0;
            int d = huff_decode(st, &st->dist_huff, in, in_len, &ip, &dsym);
            if (d == 0) {
                res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                goto done;
            }
            if (d < 0 || dsym >= 30u) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            st->sym_code = dsym;
            st->state = S_DIST_EXTRA;
            break;
        }

        case S_DIST_EXTRA: {
            uint32_t eb = k_dist_extra[st->sym_code];
            if (eb > 0u) {
                if (!ensure_bits(st, eb, in, in_len, &ip)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                uint32_t ex = (uint32_t)(st->bitbuf & ((1u << eb) - 1u));
                st->bitbuf >>= eb;
                st->bitcnt -= eb;
                st->copy_dist = k_dist_base[st->sym_code] + ex;
            } else {
                st->copy_dist = k_dist_base[st->sym_code];
            }
            if (st->copy_dist == 0u || st->copy_dist > st->max_window || (uint64_t)st->copy_dist > st->member_out) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_DATA;
                goto done;
            }
            st->state = S_COPY;
            break;
        }

        case S_COPY: {
            while (st->copy_len > 0u) {
                uint32_t src_idx = (st->win_pos - st->copy_dist) & 32767u;
                uint8_t b = st->window[src_idx];
                int em = emit_byte(st, b, out, out_cap, &op);
                if (em < 0) { res = IM_INFLATE_LIMIT; goto done; }
                if (em == 0) { res = IM_INFLATE_OUT_FULL; goto done; }
                st->copy_len--;
            }
            st->state = S_SYMBOL;
            break;
        }

        case S_ZLIB_TRAILER: {
            flush_check_span(st, out, &chk_from, op);
            while (st->hdr_have < 4u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
            }
            uint32_t want = ((uint32_t)st->hdr_buf[0] << 24) | ((uint32_t)st->hdr_buf[1] << 16) |
                            ((uint32_t)st->hdr_buf[2] << 8) | (uint32_t)st->hdr_buf[3];
            return_buffered_bytes(st, &ip);
            if (want != st->check) {
                st->state = S_ERR;
                res = IM_INFLATE_BAD_CHECKSUM;
                goto done;
            }
            st->state = S_DONE;
            res = IM_INFLATE_OK;
            goto done;
        }

        case S_GZIP_TRAILER: {
            flush_check_span(st, out, &chk_from, op);
            while (st->hdr_have < 8u) {
                uint8_t b = 0;
                if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                    res = is_eof ? IM_INFLATE_TRUNCATED : IM_INFLATE_NEED_INPUT;
                    goto done;
                }
                st->hdr_buf[st->hdr_have++] = b;
            }
            uint32_t want_crc = (uint32_t)st->hdr_buf[0] | ((uint32_t)st->hdr_buf[1] << 8) |
                                ((uint32_t)st->hdr_buf[2] << 16) | ((uint32_t)st->hdr_buf[3] << 24);
            uint32_t want_sz = (uint32_t)st->hdr_buf[4] | ((uint32_t)st->hdr_buf[5] << 8) |
                               ((uint32_t)st->hdr_buf[6] << 16) | ((uint32_t)st->hdr_buf[7] << 24);
            if (want_crc != st->check || want_sz != (uint32_t)(st->member_out & 0xFFFFFFFFu)) {
                return_buffered_bytes(st, &ip);
                st->state = S_ERR;
                res = IM_INFLATE_BAD_CHECKSUM;
                goto done;
            }
            st->hdr_have = 0;
            st->state = S_GZIP_NEXT;
            break;
        }

        case S_GZIP_NEXT: {
            /* RFC 1952 s2.2: concatenated gzip members share cumulative output_limit but reset history/CRC/ISIZE. */
            uint8_t b = 0;
            if (!read_aligned_byte(st, in, in_len, &ip, &b)) {
                if (is_eof) {
                    st->state = S_DONE;
                    res = IM_INFLATE_OK;
                } else {
                    res = IM_INFLATE_NEED_INPUT;
                }
                goto done;
            }
            /* More bytes follow: start the next gzip member with this first header byte! */
            reset_member(st);
            st->hdr_buf[0] = b;
            st->hdr_have = 1u;
            st->hdr_crc = im_crc32_update(st->hdr_crc, &b, 1u);
            st->state = S_GZIP_HDR;
            break;
        }

        default:
            st->state = S_ERR;
            res = IM_INFLATE_BAD_DATA;
            goto done;
        }
    }

done:
    flush_check_span(st, out, &chk_from, op);
    if (in_used) *in_used = ip;
    if (out_used) *out_used = op;
    return res;
}

im_inflate_status im_inflate_all(im_inflate_format fmt,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap,
                                 bool has_limit, size_t output_limit,
                                 size_t *out_len) {
    if (out_len) *out_len = 0;
    im_inflate st;
    im_inflate_init(&st, fmt, has_limit, output_limit);
    size_t in_used = 0, out_used = 0;
    im_inflate_status r = im_inflate_step(&st, in, in_len, true, &in_used, out, out_cap, &out_used);
    if (out_len) *out_len = out_used;
    return r;
}
