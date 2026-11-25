/* Impossible SDK: portable freestanding streaming DEFLATE (RFC 1951), zlib (RFC 1950), and gzip (RFC 1952) inflater. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stream container format for im_inflate_init. */
typedef enum im_inflate_format {
    IM_INFLATE_RAW = 0,
    IM_INFLATE_ZLIB = 1,
    IM_INFLATE_GZIP = 2
} im_inflate_format;

/* Named status codes returned by im_inflate_step and im_inflate_all. */
typedef enum im_inflate_status {
    /* Stream completed and wrapper checksum/length verified. */
    IM_INFLATE_OK = 0,
    /* More compressed input bytes are required to continue. */
    IM_INFLATE_NEED_INPUT = 1,
    /* Caller output buffer is full; call again with more output space. */
    IM_INFLATE_OUT_FULL = 2,
    /* Output would exceed the caller's output_limit (bomb refused without writing past the limit). */
    IM_INFLATE_LIMIT = 3,
    /* Input ended (is_eof=true) before the stream and trailer completed. */
    IM_INFLATE_TRUNCATED = 4,
    /* Zlib stream requested a preset dictionary (FDICT) which is not supported. */
    IM_INFLATE_NEED_DICT = 5,
    /* Malformed header, invalid block, invalid Huffman tree, or out-of-range back-reference. */
    IM_INFLATE_BAD_DATA = 6,
    /* Zlib Adler-32, gzip FHCRC, gzip CRC-32, or gzip ISIZE mismatch. */
    IM_INFLATE_BAD_CHECKSUM = 7,
    /* Invalid pointer, format, or state argument. */
    IM_INFLATE_BAD_ARG = 8
} im_inflate_status;

/* Canonical Huffman code table (max 15-bit codes, up to 288 symbols). */
typedef struct im_inflate_huff {
    uint16_t count[16];
    uint16_t symbol[288];
    uint16_t num_syms;
} im_inflate_huff;

/* Fixed-size caller-allocated streaming inflater state (zero heap allocation). */
typedef struct im_inflate {
    im_inflate_format fmt;
    uint32_t state;
    bool has_limit;
    size_t output_limit;
    uint64_t total_out;       /* cumulative output across all members (checked against output_limit) */
    uint64_t member_out;      /* output in current member (history bound + gzip ISIZE) */
    uint32_t max_window;      /* 32768 for raw/gzip; 1<<(CINFO+8) for zlib */
    uint32_t check;           /* running Adler-32 (zlib) or CRC-32 (gzip) */
    uint32_t hdr_crc;         /* running gzip header CRC-32 for FHCRC */

    /* Bit buffer (LSB-first) */
    uint64_t bitbuf;
    uint32_t bitcnt;

    /* Current block / header scratch */
    uint8_t bfinal;
    uint8_t gzip_flg;
    uint8_t hdr_buf[10];
    uint32_t hdr_have;
    uint32_t extra_rem;
    uint32_t stored_rem;

    /* Dynamic Huffman decoding scratch */
    uint16_t hlit;
    uint16_t hdist;
    uint16_t hclen;
    uint16_t len_idx;
    uint8_t clen_lens[19];
    uint8_t code_lens[288 + 32];
    im_inflate_huff code_huff;
    im_inflate_huff lit_huff;
    im_inflate_huff dist_huff;

    /* Active match copy state */
    uint32_t copy_len;
    uint32_t copy_dist;
    uint16_t sym_code;

    /* 32 KiB sliding history window */
    uint32_t win_pos;
    uint8_t window[32768];
} im_inflate;

/* Initialize a caller-owned im_inflate state for fmt.
   If has_limit is true, decoding stops with IM_INFLATE_LIMIT before emitting byte output_limit + 1
   (allowing exact output_limit bytes, including 0 when output_limit == 0). */
void im_inflate_init(im_inflate *st, im_inflate_format fmt, bool has_limit, size_t output_limit);

/* Advance streaming decompression by consuming up to in_len bytes from in and writing up to out_cap
   bytes to out. Always sets *in_used and *out_used (if non-NULL). Pass is_eof=true once no more
   input will ever arrive; truncated streams then return IM_INFLATE_TRUNCATED. */
im_inflate_status im_inflate_step(im_inflate *st,
                                  const uint8_t *in, size_t in_len, bool is_eof, size_t *in_used,
                                  uint8_t *out, size_t out_cap, size_t *out_used);

/* Convenience one-shot inflate of a complete buffer in[0..in_len-1] into out[0..out_cap-1].
   Writes total bytes produced to *out_len (if non-NULL). */
im_inflate_status im_inflate_all(im_inflate_format fmt,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_cap,
                                 bool has_limit, size_t output_limit,
                                 size_t *out_len);
