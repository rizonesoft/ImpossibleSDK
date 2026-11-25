/* Impossible SDK: portable freestanding streaming DEFLATE (RFC 1951), zlib (RFC 1950), and gzip (RFC 1952) compressor. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stream container format for im_deflate_init and im_deflate_all. */
typedef enum im_deflate_format {
    IM_DEFLATE_RAW = 0,
    IM_DEFLATE_ZLIB = 1,
    IM_DEFLATE_GZIP = 2
} im_deflate_format;

/* Compression effort level for im_deflate_init and im_deflate_all. */
typedef enum im_deflate_effort {
    IM_DEFLATE_FAST = 0,
    IM_DEFLATE_BALANCED = 1,
    IM_DEFLATE_BEST = 2
} im_deflate_effort;

/* Named status codes returned by im_deflate_init, im_deflate_step, and im_deflate_all. */
typedef enum im_deflate_status {
    /* Initialization succeeded, or stream finished and trailer was emitted. */
    IM_DEFLATE_OK = 0,
    /* More input bytes or is_eof=true are required to continue. */
    IM_DEFLATE_NEED_INPUT = 1,
    /* Caller output buffer is full; call again with more output space. */
    IM_DEFLATE_OUT_FULL = 2,
    /* Invalid pointer, format, effort, or post-EOF trailing input. */
    IM_DEFLATE_BAD_ARG = 3
} im_deflate_status;

/* Fixed-size caller-allocated streaming deflater state (zero heap allocation, < 512 KiB). */
typedef struct im_deflate {
    im_deflate_format fmt;
    im_deflate_effort effort;
    uint32_t state;
    bool eof_latched;

    /* Running wrapper checksum (Adler-32 for zlib, CRC-32 for gzip) and input byte count. */
    uint32_t check;
    uint64_t total_in;

    /* Bit buffer (LSB-first) for streaming output emission. */
    uint64_t bitbuf;
    uint32_t bitcnt;

    /* Wrapper header/trailer byte staging (max 10 bytes for gzip header). */
    uint8_t wrap_buf[10];
    uint32_t wrap_pos;
    uint32_t wrap_len;

    /* 64 KiB sliding window (two 32 KiB halves) and LZ77 state. */
    uint32_t strstart;
    uint32_t lookahead;
    uint32_t valid_history;
    uint32_t block_start;
    bool prev_match_avail;
    uint32_t prev_len;
    uint32_t prev_dist;

    /* Active block emission state. */
    uint8_t blk_type;      /* 0=stored, 1=fixed, 2=dynamic */
    uint8_t blk_final;
    uint32_t blk_raw_pos;
    uint32_t blk_raw_end;
    uint32_t blk_raw_len;
    uint32_t emit_phase;
    uint32_t emit_idx;
    uint32_t n_tokens;

    /* Dynamic Huffman header tables for active block. */
    uint16_t hlit;
    uint16_t hdist;
    uint16_t hclen;
    uint16_t rle_len;
    uint8_t rle_sym[286 + 30];
    uint8_t rle_extra[286 + 30];

    /* Active Huffman code/length tables (bit-reversed codes for LSB-first emission). */
    uint16_t lit_code[286];
    uint8_t lit_len[286];
    uint16_t dist_code[30];
    uint8_t dist_len[30];
    uint16_t clen_code[19];
    uint8_t clen_len[19];

    /* Accounted Huffman construction scratch (zero heap, tiny call stack). */
    uint32_t lit_freq[286];
    uint32_t dist_freq[30];
    uint32_t clen_freq[19];
    uint32_t huff_weight[286 * 2];
    uint16_t huff_parent[286 * 2];
    uint16_t huff_depth[286 * 2];
    uint16_t huff_heap[286 + 2];
    uint16_t huff_order[286];

    /* 64 KiB sliding window, 15-bit hash chains, and 32K token buffer (~320 KiB total). */
    uint8_t window[65536];
    uint16_t head[32768];
    uint16_t prev[32768];
    uint16_t dist_buf[32768];
    uint8_t litlen_buf[32768];
} im_deflate;

/* Initialize a caller-owned im_deflate state for fmt and effort.
   Returns IM_DEFLATE_OK on success, or IM_DEFLATE_BAD_ARG on invalid arguments. */
im_deflate_status im_deflate_init(im_deflate *st, im_deflate_format fmt, im_deflate_effort effort);

/* Advance streaming compression by consuming up to in_len bytes from in and writing up to out_cap
   bytes to out. Always sets *in_used and *out_used (if non-NULL) on every return. in may be NULL
   only when in_len == 0; out may be NULL only when out_cap == 0. Pass is_eof=true once no more
   input will arrive (is_eof takes effect only once all bytes of in[0..in_len-1] are consumed).
   After returning IM_DEFLATE_OK, subsequent calls with in_len == 0 return IM_DEFLATE_OK with 0/0
   counts, and calls with in_len > 0 return IM_DEFLATE_BAD_ARG. */
im_deflate_status im_deflate_step(im_deflate *st,
                                  const uint8_t *in, size_t in_len, bool is_eof, size_t *in_used,
                                  uint8_t *out, size_t out_cap, size_t *out_used);

/* Convenience one-shot deflate of in[0..in_len-1] into out[0..out_cap-1] using caller-owned st.
   Writes total bytes emitted to *out_len (if non-NULL) on IM_DEFLATE_OK and IM_DEFLATE_OUT_FULL,
   or 0 on IM_DEFLATE_BAD_ARG. */
im_deflate_status im_deflate_all(im_deflate *st,
                                 im_deflate_format fmt, im_deflate_effort effort,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap, size_t *out_len);
