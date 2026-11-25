/* Impossible SDK: portable freestanding streaming DEFLATE (RFC 1951), zlib (RFC 1950), and gzip (RFC 1952) compressor. */
#include "deflate.h"
#include "checksum.h"

#define IM_DEF_WIN_SIZE      32768u
#define IM_DEF_WIN_MASK      32767u
#define IM_DEF_HASH_SIZE     32768u
#define IM_DEF_HASH_MASK     32767u
#define IM_DEF_MIN_MATCH     3u
#define IM_DEF_MAX_MATCH     258u
#define IM_DEF_MIN_LOOKAHEAD 262u
#define IM_DEF_MAX_TOKENS    32768u
#define IM_DEF_MAX_BLOCK_RAW 32000u

enum {
    ST_HEADER  = 0,
    ST_PROCESS = 1,
    ST_EMIT    = 2,
    ST_TRAILER = 3,
    ST_DONE    = 4
};

enum {
    EMIT_BLK_HDR   = 0,
    EMIT_STORED_LN = 1,
    EMIT_STORED_DT = 2,
    EMIT_DYN_HDR   = 3,
    EMIT_DYN_CLEN  = 4,
    EMIT_DYN_RLE   = 5,
    EMIT_TOKENS    = 6,
    EMIT_EOB       = 7,
    EMIT_FINAL_PAD = 8
};

/* RFC 1951 length code tables (symbols 257..285). */
static const uint16_t k_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t k_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

/* RFC 1951 distance code tables (symbols 0..29). */
static const uint16_t k_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t k_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* RFC 1951 code-length alphabet permutation order. */
static const uint8_t k_clen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* Effort tuning parameters matching zlib levels 1, 6, and 9. */
typedef struct effort_cfg {
    uint32_t max_chain;
    uint32_t good_len;
    uint32_t nice_len;
    uint32_t max_lazy;      /* 0 => greedy mode */
    uint32_t max_insert;    /* greedy-only max match length to insert into hash */
} effort_cfg;

static const effort_cfg k_effort[3] = {
    {    4u,  4u,   8u,   0u,   4u }, /* IM_DEFLATE_FAST     (~zlib level 1) */
    {  128u,  8u, 128u,  16u, 258u }, /* IM_DEFLATE_BALANCED (~zlib level 6) */
    { 4096u, 32u, 258u, 258u, 258u }  /* IM_DEFLATE_BEST     (~zlib level 9) */
};

static uint32_t hash3(const uint8_t *p) {
    uint32_t v = ((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ (uint32_t)p[2];
    v = (v ^ (v >> 15)) & IM_DEF_HASH_MASK;
    return v;
}

static uint16_t reverse_bits(uint16_t code, uint32_t bits) {
    uint16_t r = 0;
    for (uint32_t i = 0; i < bits; ++i) {
        r = (uint16_t)((r << 1) | (code & 1u));
        code >>= 1;
    }
    return r;
}

static uint32_t len_to_sym(uint32_t len) {
    if (len == 258u) return 285u;
    uint32_t lo = 0u, hi = 28u;
    while (lo + 1u < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (k_len_base[mid] <= len) lo = mid;
        else hi = mid;
    }
    return 257u + lo;
}

static uint32_t dist_to_sym(uint32_t dist) {
    uint32_t lo = 0u, hi = 30u;
    while (lo + 1u < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (k_dist_base[mid] <= dist) lo = mid;
        else hi = mid;
    }
    return lo;
}

static uint32_t fixed_lit_len(uint32_t sym) {
    if (sym <= 143u) return 8u;
    if (sym <= 255u) return 9u;
    if (sym <= 279u) return 7u;
    return 8u;
}

static void build_canonical_codes(const uint8_t *lens, uint32_t n_syms, uint16_t *codes) {
    uint16_t bl_count[16];
    uint16_t next_code[16];
    for (uint32_t i = 0; i < 16u; ++i) bl_count[i] = 0;
    for (uint32_t i = 0; i < n_syms; ++i) {
        if (lens[i] > 0u && lens[i] <= 15u) bl_count[lens[i]]++;
    }
    uint16_t code = 0;
    for (uint32_t bits = 1u; bits <= 15u; ++bits) {
        code = (uint16_t)((code + bl_count[bits - 1u]) << 1);
        next_code[bits] = code;
    }
    for (uint32_t i = 0; i < n_syms; ++i) {
        uint8_t l = lens[i];
        if (l > 0u) {
            codes[i] = reverse_bits(next_code[l]++, l);
        } else {
            codes[i] = 0;
        }
    }
}

/* Min-heap helpers over st->huff_heap[1..heap_size] ordered by (weight, depth). */
static bool heap_less(const im_deflate *st, uint16_t a, uint16_t b) {
    uint32_t wa = st->huff_weight[a];
    uint32_t wb = st->huff_weight[b];
    if (wa != wb) return wa < wb;
    if (st->huff_depth[a] != st->huff_depth[b]) return st->huff_depth[a] < st->huff_depth[b];
    return a < b;
}

static void heap_sift_down(im_deflate *st, uint32_t size, uint32_t k) {
    uint16_t v = st->huff_heap[k];
    for (;;) {
        uint32_t j = k << 1;
        if (j > size) break;
        if (j < size && heap_less(st, st->huff_heap[j + 1u], st->huff_heap[j])) ++j;
        if (!heap_less(st, st->huff_heap[j], v)) break;
        st->huff_heap[k] = st->huff_heap[j];
        k = j;
    }
    st->huff_heap[k] = v;
}

/* Build optimal length-limited canonical Huffman code lengths (<= max_bits) using st scratch. */
static void build_huff_lengths(im_deflate *st, const uint32_t *freq, uint32_t n_syms,
                               uint32_t max_bits, uint8_t *lens) {
    for (uint32_t i = 0; i < n_syms; ++i) lens[i] = 0;

    uint32_t heap_size = 0;
    uint32_t single_sym = 0;
    for (uint32_t i = 0; i < n_syms; ++i) {
        if (freq[i] > 0u) {
            single_sym = i;
            st->huff_weight[i] = freq[i];
            st->huff_depth[i] = 0;
            st->huff_parent[i] = 0;
            st->huff_heap[++heap_size] = (uint16_t)i;
        }
    }
    if (heap_size == 0u) return;
    if (heap_size == 1u) {
        lens[single_sym] = 1u;
        return;
    }

    uint32_t n_active = heap_size;
    for (uint32_t k = heap_size >> 1; k >= 1u; --k) {
        heap_sift_down(st, heap_size, k);
    }

    uint32_t next_node = n_syms;
    uint32_t order_top = n_active;
    while (heap_size > 1u) {
        uint16_t a = st->huff_heap[1];
        st->huff_heap[1] = st->huff_heap[heap_size--];
        heap_sift_down(st, heap_size, 1u);

        uint16_t b = st->huff_heap[1];
        if (a < n_syms) st->huff_order[--order_top] = a;
        if (b < n_syms) st->huff_order[--order_top] = b;

        uint16_t p = (uint16_t)(next_node++);
        st->huff_weight[p] = st->huff_weight[a] + st->huff_weight[b];
        uint16_t da = st->huff_depth[a];
        uint16_t db = st->huff_depth[b];
        st->huff_depth[p] = (uint16_t)((da > db ? da : db) + 1u);
        st->huff_parent[a] = p;
        st->huff_parent[b] = p;
        st->huff_parent[p] = 0;

        st->huff_heap[1] = p;
        heap_sift_down(st, heap_size, 1u);
    }

    /* Compute exact depths from root down to leaves. */
    uint32_t root = next_node - 1u;
    st->huff_depth[root] = 0;
    for (uint32_t n = root; n > n_syms; --n) {
        uint32_t idx = n - 1u;
        st->huff_depth[idx] = (uint16_t)(st->huff_depth[st->huff_parent[idx]] + 1u);
    }

    uint16_t bl_count[16];
    for (uint32_t i = 0; i < 16u; ++i) bl_count[i] = 0;
    uint32_t overflow = 0;
    for (uint32_t i = 0; i < n_syms; ++i) {
        if (freq[i] > 0u) {
            uint32_t d = (uint32_t)st->huff_depth[st->huff_parent[i]] + 1u;
            if (d > max_bits) {
                d = max_bits;
                ++overflow;
            }
            bl_count[d]++;
        }
    }

    if (overflow > 0u) {
        uint32_t target = 1u << max_bits;
        uint32_t kraft = 0;
        for (uint32_t b = 1u; b <= max_bits; ++b) {
            kraft += (uint32_t)bl_count[b] << (max_bits - b);
        }
        while (kraft > target) {
            uint32_t b = max_bits - 1u;
            while (b > 0u && bl_count[b] == 0u) --b;
            bl_count[b]--;
            bl_count[b + 1u]++;
            kraft -= 1u << (max_bits - b - 1u);
        }
        while (kraft < target) {
            uint32_t rem = target - kraft;
            uint32_t b = 2u;
            while (b <= max_bits) {
                uint32_t add = 1u << (max_bits - b);
                if (bl_count[b] > 0u && add <= rem) break;
                ++b;
            }
            bl_count[b]--;
            bl_count[b - 1u]++;
            kraft += 1u << (max_bits - b);
        }
    }

    /* Sort active symbols by descending frequency (insertion sort on <= 286 elements is fast & exact). */
    for (uint32_t i = 1u; i < n_active; ++i) {
        uint16_t sym = st->huff_order[i];
        uint32_t w = freq[sym];
        uint32_t j = i;
        while (j > 0u && (freq[st->huff_order[j - 1u]] < w ||
                          (freq[st->huff_order[j - 1u]] == w && st->huff_order[j - 1u] > sym))) {
            st->huff_order[j] = st->huff_order[j - 1u];
            --j;
        }
        st->huff_order[j] = sym;
    }

    /* Assign shortest lengths to highest-frequency symbols. */
    uint32_t pos = 0;
    for (uint32_t b = 1u; b <= max_bits; ++b) {
        for (uint32_t c = 0; c < bl_count[b]; ++c) {
            lens[st->huff_order[pos++]] = (uint8_t)b;
        }
    }
}

/* RLE-encode combined lit_len[0..hlit-1] + dist_len[0..hdist-1] into st->rle_sym/extra. */
static uint8_t combined_len_at(const im_deflate *st, uint32_t idx) {
    return (idx < st->hlit) ? st->lit_len[idx] : st->dist_len[idx - st->hlit];
}

static void encode_code_lengths(im_deflate *st) {
    uint32_t total = (uint32_t)st->hlit + (uint32_t)st->hdist;

    for (uint32_t i = 0; i < 19u; ++i) st->clen_freq[i] = 0;
    uint32_t out = 0;
    uint32_t i = 0;
    while (i < total) {
        uint8_t cur = combined_len_at(st, i);
        uint32_t run = 1u;
        while (i + run < total && combined_len_at(st, i + run) == cur) ++run;

        uint32_t orig_run = run;
        if (cur == 0u) {
            while (run > 0u) {
                if (run >= 11u) {
                    uint32_t take = run > 138u ? 138u : run;
                    st->rle_sym[out] = 18u;
                    st->rle_extra[out] = (uint8_t)(take - 11u);
                    ++out;
                    st->clen_freq[18]++;
                    run -= take;
                } else if (run >= 3u) {
                    st->rle_sym[out] = 17u;
                    st->rle_extra[out] = (uint8_t)(run - 3u);
                    ++out;
                    st->clen_freq[17]++;
                    run = 0;
                } else {
                    st->rle_sym[out] = 0u;
                    st->rle_extra[out] = 0u;
                    ++out;
                    st->clen_freq[0]++;
                    --run;
                }
            }
        } else {
            st->rle_sym[out] = cur;
            st->rle_extra[out] = 0u;
            ++out;
            st->clen_freq[cur]++;
            --run;
            while (run > 0u) {
                if (run >= 3u) {
                    uint32_t take = run > 6u ? 6u : run;
                    st->rle_sym[out] = 16u;
                    st->rle_extra[out] = (uint8_t)(take - 3u);
                    ++out;
                    st->clen_freq[16]++;
                    run -= take;
                } else {
                    st->rle_sym[out] = cur;
                    st->rle_extra[out] = 0u;
                    ++out;
                    st->clen_freq[cur]++;
                    --run;
                }
            }
        }
        i += orig_run;
    }
    st->rle_len = (uint16_t)out;
}

static void prepare_block(im_deflate *st, bool is_final) {
    st->blk_final = is_final ? 1u : 0u;
    st->blk_raw_pos = st->block_start;
    st->blk_raw_end = st->strstart;
    st->blk_raw_len = st->strstart - st->block_start;

    for (uint32_t i = 0; i < 286u; ++i) st->lit_freq[i] = 0;
    for (uint32_t i = 0; i < 30u; ++i) st->dist_freq[i] = 0;

    uint64_t fixed_bits = 3u; /* BFINAL + BTYPE */
    uint64_t extra_bits = 0u;

    for (uint32_t i = 0; i < st->n_tokens; ++i) {
        uint16_t dist = st->dist_buf[i];
        uint8_t ll = st->litlen_buf[i];
        if (dist == 0u) {
            st->lit_freq[ll]++;
            fixed_bits += fixed_lit_len(ll);
        } else {
            uint32_t lsym = len_to_sym((uint32_t)ll + 3u);
            uint32_t dsym = dist_to_sym(dist);
            st->lit_freq[lsym]++;
            st->dist_freq[dsym]++;
            uint32_t eb = (uint32_t)k_len_extra[lsym - 257u] + (uint32_t)k_dist_extra[dsym];
            extra_bits += eb;
            fixed_bits += fixed_lit_len(lsym) + 5u + eb;
        }
    }
    st->lit_freq[256]++; /* EOB */
    fixed_bits += fixed_lit_len(256u);

    /* Stored block bit cost (only valid when raw_len <= 65535; IM_DEF_MAX_BLOCK_RAW is 32000). */
    uint32_t cur_bit_mod = (st->bitcnt + 3u) & 7u;
    uint32_t pad_bits = (8u - cur_bit_mod) & 7u;
    uint64_t stored_bits = 3u + pad_bits + 32u + (uint64_t)st->blk_raw_len * 8u;

    /* Build dynamic Huffman trees and compute exact dynamic bit cost. */
    build_huff_lengths(st, st->lit_freq, 286u, 15u, st->lit_len);
    build_huff_lengths(st, st->dist_freq, 30u, 15u, st->dist_len);

    uint32_t hlit = 286u;
    while (hlit > 257u && st->lit_len[hlit - 1u] == 0u) --hlit;
    uint32_t hdist = 30u;
    while (hdist > 1u && st->dist_len[hdist - 1u] == 0u) --hdist;
    st->hlit = (uint16_t)hlit;
    st->hdist = (uint16_t)hdist;

    encode_code_lengths(st);
    build_huff_lengths(st, st->clen_freq, 19u, 7u, st->clen_len);
    build_canonical_codes(st->clen_len, 19u, st->clen_code);

    uint32_t hclen = 19u;
    while (hclen > 4u && st->clen_len[k_clen_order[hclen - 1u]] == 0u) --hclen;
    st->hclen = (uint16_t)hclen;

    uint64_t dyn_bits = 3u + 5u + 5u + 4u + (uint64_t)hclen * 3u + extra_bits;
    for (uint32_t i = 0; i < st->rle_len; ++i) {
        uint8_t s = st->rle_sym[i];
        dyn_bits += st->clen_len[s];
        if (s == 16u) dyn_bits += 2u;
        else if (s == 17u) dyn_bits += 3u;
        else if (s == 18u) dyn_bits += 7u;
    }
    for (uint32_t i = 0; i < 286u; ++i) {
        if (st->lit_freq[i] > 0u) dyn_bits += (uint64_t)st->lit_freq[i] * st->lit_len[i];
    }
    for (uint32_t i = 0; i < 30u; ++i) {
        if (st->dist_freq[i] > 0u) dyn_bits += (uint64_t)st->dist_freq[i] * st->dist_len[i];
    }

    if (stored_bits <= fixed_bits && stored_bits <= dyn_bits) {
        st->blk_type = 0u;
    } else if (fixed_bits <= dyn_bits) {
        st->blk_type = 1u;
        /* RFC 1951 s3.2.6 exact fixed literal/length codes (zero stack table). */
        for (uint32_t i = 0; i < 286u; ++i) {
            uint16_t code;
            uint8_t len;
            if (i <= 143u) {
                code = (uint16_t)(0x30u + i);
                len = 8u;
            } else if (i <= 255u) {
                code = (uint16_t)(0x190u + (i - 144u));
                len = 9u;
            } else if (i <= 279u) {
                code = (uint16_t)(i - 256u);
                len = 7u;
            } else {
                code = (uint16_t)(0xC0u + (i - 280u));
                len = 8u;
            }
            st->lit_len[i] = len;
            st->lit_code[i] = reverse_bits(code, len);
        }
        for (uint32_t i = 0; i < 30u; ++i) {
            st->dist_len[i] = 5u;
            st->dist_code[i] = reverse_bits((uint16_t)i, 5u);
        }
    } else {
        st->blk_type = 2u;
        build_canonical_codes(st->lit_len, 286u, st->lit_code);
        build_canonical_codes(st->dist_len, 30u, st->dist_code);
    }

    st->emit_phase = EMIT_BLK_HDR;
    st->emit_idx = 0;
    st->state = ST_EMIT;
}

static void put_bits(im_deflate *st, uint32_t val, uint32_t nbits) {
    st->bitbuf |= ((uint64_t)val) << st->bitcnt;
    st->bitcnt += nbits;
}

static void drain_bits(im_deflate *st, uint8_t *out, size_t out_cap, size_t *out_pos) {
    while (st->bitcnt >= 8u && *out_pos < out_cap) {
        out[(*out_pos)++] = (uint8_t)(st->bitbuf & 0xFFu);
        st->bitbuf >>= 8;
        st->bitcnt -= 8u;
    }
}

/* Emit active block into out[0..out_cap). Returns true when block emission is complete. */
static bool emit_block_step(im_deflate *st, uint8_t *out, size_t out_cap, size_t *out_pos) {
    for (;;) {
        drain_bits(st, out, out_cap, out_pos);
        if (st->bitcnt >= 8u) return false;

        switch (st->emit_phase) {
        case EMIT_BLK_HDR:
            put_bits(st, (uint32_t)st->blk_final | ((uint32_t)st->blk_type << 1), 3u);
            if (st->blk_type == 0u) {
                uint32_t pad = (8u - (st->bitcnt & 7u)) & 7u;
                if (pad > 0u) put_bits(st, 0u, pad);
                st->emit_phase = EMIT_STORED_LN;
            } else if (st->blk_type == 1u) {
                st->emit_idx = 0;
                st->emit_phase = EMIT_TOKENS;
            } else {
                st->emit_phase = EMIT_DYN_HDR;
            }
            break;

        case EMIT_STORED_LN: {
            uint16_t len = (uint16_t)st->blk_raw_len;
            uint16_t nlen = (uint16_t)(~len);
            put_bits(st, len, 16u);
            put_bits(st, nlen, 16u);
            st->emit_phase = EMIT_STORED_DT;
            break;
        }

        case EMIT_STORED_DT:
            drain_bits(st, out, out_cap, out_pos);
            if (st->bitcnt > 0u) return false;
            while (st->blk_raw_pos < st->blk_raw_end && *out_pos < out_cap) {
                out[(*out_pos)++] = st->window[st->blk_raw_pos++];
            }
            if (st->blk_raw_pos < st->blk_raw_end) return false;
            st->emit_phase = EMIT_FINAL_PAD;
            break;

        case EMIT_DYN_HDR:
            put_bits(st, (uint32_t)(st->hlit - 257u), 5u);
            put_bits(st, (uint32_t)(st->hdist - 1u), 5u);
            put_bits(st, (uint32_t)(st->hclen - 4u), 4u);
            st->emit_idx = 0;
            st->emit_phase = EMIT_DYN_CLEN;
            break;

        case EMIT_DYN_CLEN:
            while (st->emit_idx < st->hclen) {
                put_bits(st, st->clen_len[k_clen_order[st->emit_idx++]], 3u);
                drain_bits(st, out, out_cap, out_pos);
                if (st->bitcnt >= 8u) return false;
            }
            st->emit_idx = 0;
            st->emit_phase = EMIT_DYN_RLE;
            break;

        case EMIT_DYN_RLE:
            while (st->emit_idx < st->rle_len) {
                uint8_t s = st->rle_sym[st->emit_idx];
                uint8_t ex = st->rle_extra[st->emit_idx];
                ++st->emit_idx;
                put_bits(st, st->clen_code[s], st->clen_len[s]);
                if (s == 16u) put_bits(st, ex, 2u);
                else if (s == 17u) put_bits(st, ex, 3u);
                else if (s == 18u) put_bits(st, ex, 7u);
                drain_bits(st, out, out_cap, out_pos);
                if (st->bitcnt >= 8u) return false;
            }
            st->emit_idx = 0;
            st->emit_phase = EMIT_TOKENS;
            break;

        case EMIT_TOKENS:
            while (st->emit_idx < st->n_tokens) {
                uint16_t dist = st->dist_buf[st->emit_idx];
                uint8_t ll = st->litlen_buf[st->emit_idx];
                ++st->emit_idx;
                if (dist == 0u) {
                    put_bits(st, st->lit_code[ll], st->lit_len[ll]);
                } else {
                    uint32_t mlen = (uint32_t)ll + 3u;
                    uint32_t lsym = len_to_sym(mlen);
                    uint32_t lidx = lsym - 257u;
                    put_bits(st, st->lit_code[lsym], st->lit_len[lsym]);
                    if (k_len_extra[lidx] > 0u) {
                        put_bits(st, mlen - k_len_base[lidx], k_len_extra[lidx]);
                    }
                    uint32_t dsym = dist_to_sym(dist);
                    put_bits(st, st->dist_code[dsym], st->dist_len[dsym]);
                    if (k_dist_extra[dsym] > 0u) {
                        put_bits(st, (uint32_t)dist - k_dist_base[dsym], k_dist_extra[dsym]);
                    }
                }
                drain_bits(st, out, out_cap, out_pos);
                if (st->bitcnt >= 8u) return false;
            }
            st->emit_phase = EMIT_EOB;
            break;

        case EMIT_EOB:
            put_bits(st, st->lit_code[256], st->lit_len[256]);
            st->emit_phase = EMIT_FINAL_PAD;
            break;

        case EMIT_FINAL_PAD:
            if (st->blk_final && (st->bitcnt & 7u) != 0u) {
                put_bits(st, 0u, 8u - (st->bitcnt & 7u));
            }
            drain_bits(st, out, out_cap, out_pos);
            if (st->bitcnt >= 8u) return false;
            st->n_tokens = 0;
            st->block_start = st->strstart;
            return true;
        }
    }
}

static void insert_hash(im_deflate *st, uint32_t pos) {
    uint32_t h = hash3(st->window + pos);
    st->prev[pos & IM_DEF_WIN_MASK] = st->head[h];
    st->head[h] = (uint16_t)(pos + 1u);
}

static uint32_t find_best_match(const im_deflate *st, uint32_t pos, uint32_t chain_head_plus1,
                                uint32_t prev_best_len, uint32_t *out_dist) {
    const effort_cfg *cfg = &k_effort[st->effort];
    uint32_t max_len = st->lookahead < IM_DEF_MAX_MATCH ? st->lookahead : IM_DEF_MAX_MATCH;
    if (max_len < IM_DEF_MIN_MATCH || st->valid_history == 0u || prev_best_len >= max_len) return 0u;

    uint32_t max_dist = st->valid_history < IM_DEF_WIN_SIZE ? st->valid_history : IM_DEF_WIN_SIZE;
    uint32_t chain_limit = cfg->max_chain;
    if (prev_best_len >= cfg->good_len) chain_limit >>= 2;
    if (chain_limit == 0u) chain_limit = 1u;

    uint32_t nice_len = cfg->nice_len;
    if (nice_len > max_len) nice_len = max_len;

    uint32_t best_len = prev_best_len >= IM_DEF_MIN_MATCH ? prev_best_len : (IM_DEF_MIN_MATCH - 1u);
    uint32_t best_dist = 0;

    const uint8_t *cur = st->window + pos;
    uint8_t scan_end = cur[best_len];
    uint8_t scan_end1 = cur[best_len - 1u];

    uint32_t cand_p1 = chain_head_plus1;
    while (cand_p1 > 0u && chain_limit > 0u) {
        uint32_t cand = cand_p1 - 1u;
        if (cand >= pos) break;
        uint32_t dist = pos - cand;
        if (dist > max_dist) break;

        const uint8_t *m = st->window + cand;
        if (m[best_len] == scan_end && m[best_len - 1u] == scan_end1 &&
            m[0] == cur[0] && m[1] == cur[1]) {
            uint32_t len = 2u;
            while (len < max_len && m[len] == cur[len]) ++len;
            if (len > best_len) {
                best_len = len;
                best_dist = dist;
                if (best_len >= nice_len) break;
                scan_end = cur[best_len];
                scan_end1 = cur[best_len - 1u];
            }
        }
        cand_p1 = st->prev[cand & IM_DEF_WIN_MASK];
        --chain_limit;
    }

    if (best_dist > 0u && best_len >= IM_DEF_MIN_MATCH) {
        *out_dist = best_dist;
        return best_len;
    }
    return 0u;
}

static void slide_window(im_deflate *st) {
    for (uint32_t i = 0; i < IM_DEF_WIN_SIZE; ++i) {
        st->window[i] = st->window[i + IM_DEF_WIN_SIZE];
    }
    st->strstart -= IM_DEF_WIN_SIZE;
    st->block_start -= IM_DEF_WIN_SIZE;
    for (uint32_t i = 0; i < IM_DEF_HASH_SIZE; ++i) {
        uint16_t v = st->head[i];
        st->head[i] = (v > IM_DEF_WIN_SIZE) ? (uint16_t)(v - IM_DEF_WIN_SIZE) : 0u;
    }
    for (uint32_t i = 0; i < IM_DEF_WIN_SIZE; ++i) {
        uint16_t v = st->prev[i];
        st->prev[i] = (v > IM_DEF_WIN_SIZE) ? (uint16_t)(v - IM_DEF_WIN_SIZE) : 0u;
    }
}

static void emit_literal(im_deflate *st, uint8_t b) {
    st->dist_buf[st->n_tokens] = 0u;
    st->litlen_buf[st->n_tokens] = b;
    st->n_tokens++;
}

static void emit_match(im_deflate *st, uint32_t dist, uint32_t len) {
    st->dist_buf[st->n_tokens] = (uint16_t)dist;
    st->litlen_buf[st->n_tokens] = (uint8_t)(len - 3u);
    st->n_tokens++;
}

static void advance_history(im_deflate *st, uint32_t n) {
    st->strstart += n;
    st->lookahead -= n;
    uint32_t nh = st->valid_history + n;
    st->valid_history = nh > IM_DEF_WIN_SIZE ? IM_DEF_WIN_SIZE : nh;
}

static void build_wrapper_header(im_deflate *st) {
    st->wrap_pos = 0;
    if (st->fmt == IM_DEFLATE_RAW) {
        st->wrap_len = 0;
    } else if (st->fmt == IM_DEFLATE_ZLIB) {
        uint8_t cmf = 0x78u; /* DEFLATE, 32 KiB window */
        uint8_t flevel = (st->effort == IM_DEFLATE_FAST) ? 0u :
                         (st->effort == IM_DEFLATE_BALANCED) ? 2u : 3u;
        uint8_t flg = (uint8_t)(flevel << 6);
        uint32_t rem = ((uint32_t)cmf * 256u + flg) % 31u;
        if (rem != 0u) flg = (uint8_t)(flg + (31u - rem));
        st->wrap_buf[0] = cmf;
        st->wrap_buf[1] = flg;
        st->wrap_len = 2u;
    } else {
        /* RFC 1952 deterministic gzip header (zero MTIME, OS=255). */
        uint8_t xfl = (st->effort == IM_DEFLATE_FAST) ? 4u :
                      (st->effort == IM_DEFLATE_BEST) ? 2u : 0u;
        st->wrap_buf[0] = 0x1Fu;
        st->wrap_buf[1] = 0x8Bu;
        st->wrap_buf[2] = 0x08u;
        st->wrap_buf[3] = 0x00u;
        st->wrap_buf[4] = 0x00u;
        st->wrap_buf[5] = 0x00u;
        st->wrap_buf[6] = 0x00u;
        st->wrap_buf[7] = 0x00u;
        st->wrap_buf[8] = xfl;
        st->wrap_buf[9] = 0xFFu;
        st->wrap_len = 10u;
    }
}

static void build_wrapper_trailer(im_deflate *st) {
    st->wrap_pos = 0;
    if (st->fmt == IM_DEFLATE_RAW) {
        st->wrap_len = 0;
    } else if (st->fmt == IM_DEFLATE_ZLIB) {
        uint32_t a = st->check;
        st->wrap_buf[0] = (uint8_t)(a >> 24);
        st->wrap_buf[1] = (uint8_t)(a >> 16);
        st->wrap_buf[2] = (uint8_t)(a >> 8);
        st->wrap_buf[3] = (uint8_t)(a);
        st->wrap_len = 4u;
    } else {
        uint32_t c = st->check;
        uint32_t isize = (uint32_t)(st->total_in & 0xFFFFFFFFu);
        st->wrap_buf[0] = (uint8_t)(c);
        st->wrap_buf[1] = (uint8_t)(c >> 8);
        st->wrap_buf[2] = (uint8_t)(c >> 16);
        st->wrap_buf[3] = (uint8_t)(c >> 24);
        st->wrap_buf[4] = (uint8_t)(isize);
        st->wrap_buf[5] = (uint8_t)(isize >> 8);
        st->wrap_buf[6] = (uint8_t)(isize >> 16);
        st->wrap_buf[7] = (uint8_t)(isize >> 24);
        st->wrap_len = 8u;
    }
}

im_deflate_status im_deflate_init(im_deflate *st, im_deflate_format fmt, im_deflate_effort effort) {
    if (!st) return IM_DEFLATE_BAD_ARG;
    if (fmt != IM_DEFLATE_RAW && fmt != IM_DEFLATE_ZLIB && fmt != IM_DEFLATE_GZIP) {
        return IM_DEFLATE_BAD_ARG;
    }
    if (effort != IM_DEFLATE_FAST && effort != IM_DEFLATE_BALANCED && effort != IM_DEFLATE_BEST) {
        return IM_DEFLATE_BAD_ARG;
    }

    st->fmt = fmt;
    st->effort = effort;
    st->state = ST_HEADER;
    st->eof_latched = false;
    st->hdr_sent = false;
    st->check = (fmt == IM_DEFLATE_ZLIB) ? im_adler32_init() : im_crc32_init();
    st->total_in = 0;
    st->bitbuf = 0;
    st->bitcnt = 0;
    st->strstart = 0;
    st->lookahead = 0;
    st->valid_history = 0;
    st->block_start = 0;
    st->prev_match_avail = false;
    st->prev_len = 0;
    st->prev_dist = 0;
    st->n_tokens = 0;
    for (uint32_t i = 0; i < IM_DEF_HASH_SIZE; ++i) st->head[i] = 0;
    for (uint32_t i = 0; i < IM_DEF_WIN_SIZE; ++i) st->prev[i] = 0;
    build_wrapper_header(st);
    return IM_DEFLATE_OK;
}

im_deflate_status im_deflate_step(im_deflate *st,
                                  const uint8_t *in, size_t in_len, bool is_eof, size_t *in_used,
                                  uint8_t *out, size_t out_cap, size_t *out_used) {
    if (in_used) *in_used = 0;
    if (out_used) *out_used = 0;
    if (!st) return IM_DEFLATE_BAD_ARG;
    if ((in == NULL && in_len > 0u) || (out == NULL && out_cap > 0u)) return IM_DEFLATE_BAD_ARG;
    if (st->state == ST_DONE) {
        return (in_len > 0u) ? IM_DEFLATE_BAD_ARG : IM_DEFLATE_OK;
    }
    if (st->eof_latched && in_len > 0u) return IM_DEFLATE_BAD_ARG;
    if (st->state > ST_DONE) return IM_DEFLATE_BAD_ARG;

    size_t in_pos = 0;
    size_t out_pos = 0;
    const effort_cfg *cfg = &k_effort[st->effort];

    for (;;) {
        if (is_eof && in_pos == in_len) {
            st->eof_latched = true;
        }
        if (st->state == ST_HEADER) {
            while (st->wrap_pos < st->wrap_len && out_pos < out_cap) {
                out[out_pos++] = st->wrap_buf[st->wrap_pos++];
            }
            if (st->wrap_pos < st->wrap_len) {
                if (in_used) *in_used = in_pos;
                if (out_used) *out_used = out_pos;
                return IM_DEFLATE_OUT_FULL;
            }
            st->hdr_sent = true;
            st->state = ST_PROCESS;
            continue;
        }

        if (st->state == ST_EMIT) {
            if (!emit_block_step(st, out, out_cap, &out_pos)) {
                if (in_used) *in_used = in_pos;
                if (out_used) *out_used = out_pos;
                return IM_DEFLATE_OUT_FULL;
            }
            if (st->blk_final) {
                build_wrapper_trailer(st);
                st->state = ST_TRAILER;
            } else {
                st->state = ST_PROCESS;
            }
            continue;
        }

        if (st->state == ST_TRAILER) {
            while (st->wrap_pos < st->wrap_len && out_pos < out_cap) {
                out[out_pos++] = st->wrap_buf[st->wrap_pos++];
            }
            if (st->wrap_pos < st->wrap_len) {
                if (in_used) *in_used = in_pos;
                if (out_used) *out_used = out_pos;
                return IM_DEFLATE_OUT_FULL;
            }
            st->state = ST_DONE;
            if (in_used) *in_used = in_pos;
            if (out_used) *out_used = out_pos;
            return IM_DEFLATE_OK;
        }

        /* ST_PROCESS: pull available input into window[] first. */
        if (in_pos < in_len) {
            uint32_t win_end = st->strstart + st->lookahead;
            if (win_end < 65536u) {
                size_t space = (size_t)(65536u - win_end);
                size_t avail = in_len - in_pos;
                size_t take = avail < space ? avail : space;
                for (size_t i = 0; i < take; ++i) {
                    st->window[win_end + i] = in[in_pos + i];
                }
                if (st->fmt == IM_DEFLATE_ZLIB) {
                    st->check = im_adler32_update(st->check, in + in_pos, take);
                } else if (st->fmt == IM_DEFLATE_GZIP) {
                    st->check = im_crc32_update(st->check, in + in_pos, take);
                }
                st->total_in += take;
                st->lookahead += (uint32_t)take;
                in_pos += take;
            }
        }
        if (is_eof && in_pos == in_len) {
            st->eof_latched = true;
        }

        /* Check non-final block flush only when lookahead > 0 and no lazy match is pending,
           guaranteeing identical BFINAL boundaries regardless of input cuts or EOF schedule. */
        if (!st->prev_match_avail && st->lookahead > 0u &&
            (st->n_tokens >= IM_DEF_MAX_TOKENS - 2u ||
             st->strstart - st->block_start >= IM_DEF_MAX_BLOCK_RAW)) {
            prepare_block(st, false);
            continue;
        }

        /* Slide window once strstart reaches the upper guard; block_start >= 32768 holds by invariant.
           Safe even when prev_match_avail is true because strstart - 1 >= 32768 slides cleanly. */
        if (st->strstart >= 65536u - IM_DEF_MIN_LOOKAHEAD &&
            st->block_start >= IM_DEF_WIN_SIZE) {
            slide_window(st);
            continue;
        }

        /* Pause if not at EOF and lookahead is below MIN_LOOKAHEAD. */
        if (!st->eof_latched && st->lookahead < IM_DEF_MIN_LOOKAHEAD) {
            if (in_used) *in_used = in_pos;
            if (out_used) *out_used = out_pos;
            return IM_DEFLATE_NEED_INPUT;
        }

        /* At EOF with zero lookahead: flush final block. */
        if (st->lookahead == 0u) {
            prepare_block(st, true);
            continue;
        }

        /* Run one LZ77 step at st->strstart. */
        uint32_t cur_len = 0;
        uint32_t cur_dist = 0;
        if (st->lookahead >= IM_DEF_MIN_MATCH) {
            uint32_t h = hash3(st->window + st->strstart);
            uint16_t prev_head = st->head[h];
            st->prev[st->strstart & IM_DEF_WIN_MASK] = prev_head;
            st->head[h] = (uint16_t)(st->strstart + 1u);
            if (prev_head > 0u) {
                uint32_t prev_best = st->prev_match_avail ? st->prev_len : 0u;
                cur_len = find_best_match(st, st->strstart, prev_head, prev_best, &cur_dist);
            }
        }

        if (cfg->max_lazy == 0u) {
            /* Greedy mode (IM_DEFLATE_FAST). */
            if (cur_len >= IM_DEF_MIN_MATCH) {
                emit_match(st, cur_dist, cur_len);
                if (cur_len <= cfg->max_insert) {
                    for (uint32_t k = 1u; k < cur_len && st->lookahead - k >= IM_DEF_MIN_MATCH; ++k) {
                        insert_hash(st, st->strstart + k);
                    }
                }
                advance_history(st, cur_len);
            } else {
                emit_literal(st, st->window[st->strstart]);
                advance_history(st, 1u);
            }
        } else if (st->prev_match_avail) {
            /* Lazy mode with a deferred match at strstart - 1. */
            if (cur_len > st->prev_len) {
                emit_literal(st, st->window[st->strstart - 1u]);
                if (cur_len < cfg->max_lazy && cur_len < st->lookahead && st->lookahead >= 4u &&
                    st->n_tokens < IM_DEF_MAX_TOKENS - 2u &&
                    st->strstart - st->block_start < IM_DEF_MAX_BLOCK_RAW) {
                    st->prev_len = cur_len;
                    st->prev_dist = cur_dist;
                    advance_history(st, 1u);
                } else {
                    emit_match(st, cur_dist, cur_len);
                    for (uint32_t k = 1u; k < cur_len && st->lookahead - k >= IM_DEF_MIN_MATCH; ++k) {
                        insert_hash(st, st->strstart + k);
                    }
                    advance_history(st, cur_len);
                    st->prev_match_avail = false;
                }
            } else {
                emit_match(st, st->prev_dist, st->prev_len);
                uint32_t rem = st->prev_len - 1u;
                for (uint32_t k = 1u; k < rem && st->lookahead - k >= IM_DEF_MIN_MATCH; ++k) {
                    insert_hash(st, st->strstart + k);
                }
                advance_history(st, rem);
                st->prev_match_avail = false;
            }
        } else {
            /* Lazy mode without a pending match. */
            if (cur_len >= IM_DEF_MIN_MATCH) {
                if (cur_len < cfg->max_lazy && cur_len < st->lookahead && st->lookahead >= 4u &&
                    st->n_tokens < IM_DEF_MAX_TOKENS - 2u &&
                    st->strstart - st->block_start < IM_DEF_MAX_BLOCK_RAW) {
                    st->prev_match_avail = true;
                    st->prev_len = cur_len;
                    st->prev_dist = cur_dist;
                    advance_history(st, 1u);
                } else {
                    emit_match(st, cur_dist, cur_len);
                    for (uint32_t k = 1u; k < cur_len && st->lookahead - k >= IM_DEF_MIN_MATCH; ++k) {
                        insert_hash(st, st->strstart + k);
                    }
                    advance_history(st, cur_len);
                }
            } else {
                emit_literal(st, st->window[st->strstart]);
                advance_history(st, 1u);
            }
        }
    }
}

im_deflate_status im_deflate_all(im_deflate *st,
                                 im_deflate_format fmt, im_deflate_effort effort,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!st) return IM_DEFLATE_BAD_ARG;
    if ((in == NULL && in_len > 0u) || (out == NULL && out_cap > 0u)) return IM_DEFLATE_BAD_ARG;
    im_deflate_status init_st = im_deflate_init(st, fmt, effort);
    if (init_st != IM_DEFLATE_OK) return init_st;

    size_t in_pos = 0;
    size_t out_pos = 0;
    for (;;) {
        size_t used_in = 0, used_out = 0;
        im_deflate_status rc = im_deflate_step(
            st,
            in ? (in + in_pos) : NULL, in_len - in_pos, true, &used_in,
            out ? (out + out_pos) : NULL, out_cap - out_pos, &used_out);
        in_pos += used_in;
        out_pos += used_out;
        if (rc == IM_DEFLATE_OK) {
            if (out_len) *out_len = out_pos;
            return IM_DEFLATE_OK;
        }
        if (rc == IM_DEFLATE_OUT_FULL) {
            if (out_len) *out_len = out_pos;
            return IM_DEFLATE_OUT_FULL;
        }
        if (rc != IM_DEFLATE_NEED_INPUT) {
            if (out_len) *out_len = 0;
            return rc;
        }
    }
}
