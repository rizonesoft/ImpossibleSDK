/* Impossible SDK: portable Unicode 16.0.0 data, breaking, bidi, normalization, and case folding.
   Freestanding C23: no OS calls, no floating point, no libc, no heap allocation. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Named status codes returned by Unicode APIs that write caller buffers or validate codepoints. */
typedef enum im_unicode_error {
    /* Operation completed without error. */
    IM_UNICODE_OK = 0,
    /* A required pointer, length, range, or enum argument was invalid. */
    IM_UNICODE_BAD_ARG,
    /* Codepoint exceeded the Unicode scalar limit 0x10FFFF. */
    IM_UNICODE_OUT_OF_RANGE,
    /* Codepoint is in 0x0000..0x10FFFF but has no assigned Unicode Name. */
    IM_UNICODE_NOT_FOUND,
    /* Caller output or scratch buffer is too small; required size was written to out_len (nothing written past capacity). */
    IM_UNICODE_BUFFER_TOO_SMALL
} im_unicode_error;

/* Unicode General_Category values (30 standard categories, matching UCD DerivedGeneralCategory.txt). */
typedef enum im_general_category {
    IM_GC_CN = 0,
    IM_GC_LU,
    IM_GC_LL,
    IM_GC_LT,
    IM_GC_LM,
    IM_GC_LO,
    IM_GC_MN,
    IM_GC_MC,
    IM_GC_ME,
    IM_GC_ND,
    IM_GC_NL,
    IM_GC_NO,
    IM_GC_PC,
    IM_GC_PD,
    IM_GC_PS,
    IM_GC_PE,
    IM_GC_PI,
    IM_GC_PF,
    IM_GC_PO,
    IM_GC_SM,
    IM_GC_SC,
    IM_GC_SK,
    IM_GC_SO,
    IM_GC_ZS,
    IM_GC_ZL,
    IM_GC_ZP,
    IM_GC_CC,
    IM_GC_CF,
    IM_GC_CS,
    IM_GC_CO
} im_general_category;

/* Unicode East_Asian_Width values (UAX #11). */
typedef enum im_east_asian_width {
    IM_EAW_N = 0,
    IM_EAW_A,
    IM_EAW_F,
    IM_EAW_H,
    IM_EAW_NA,
    IM_EAW_W
} im_east_asian_width;

/* Unicode Bidi_Class values (UAX #9). */
typedef enum im_bidi_class {
    IM_BIDI_L = 0,
    IM_BIDI_R,
    IM_BIDI_AL,
    IM_BIDI_EN,
    IM_BIDI_ES,
    IM_BIDI_ET,
    IM_BIDI_AN,
    IM_BIDI_CS,
    IM_BIDI_NSM,
    IM_BIDI_BN,
    IM_BIDI_B,
    IM_BIDI_S,
    IM_BIDI_WS,
    IM_BIDI_ON,
    IM_BIDI_LRE,
    IM_BIDI_LRO,
    IM_BIDI_RLE,
    IM_BIDI_RLO,
    IM_BIDI_PDF,
    IM_BIDI_LRI,
    IM_BIDI_RLI,
    IM_BIDI_FSI,
    IM_BIDI_PDI
} im_bidi_class;

/* Unicode Bidi_Paired_Bracket_Type values (UAX #9). */
typedef enum im_bidi_bracket_type {
    IM_BPT_NONE = 0,
    IM_BPT_OPEN,
    IM_BPT_CLOSE
} im_bidi_bracket_type;

/* Break classification emitted at each boundary position 0..len. */
typedef enum im_break_type {
    /* Break is prohibited at this boundary. */
    IM_BREAK_NONE = 0,
    /* Break is allowed at this boundary (grapheme, word, or line opportunity). */
    IM_BREAK_ALLOW = 1,
    /* Mandatory line break (BK/CR/LF/NL/EOT) in UAX #14. */
    IM_BREAK_MUST = 2
} im_break_type;

/* Segmentation algorithm selector for the streaming chunked breaker. */
typedef enum im_unicode_break_kind {
    IM_BREAK_KIND_GRAPHEME = 0,
    IM_BREAK_KIND_WORD,
    IM_BREAK_KIND_LINE
} im_unicode_break_kind;

/* Unicode Normalization Form selector (UAX #15). */
typedef enum im_norm_form {
    IM_NORM_NFD = 0,
    IM_NORM_NFC,
    IM_NORM_NFKD,
    IM_NORM_NFKC
} im_norm_form;

/* Unicode Case Folding mode selector (CaseFolding.txt statuses C+F, C+S, or C+F+T). */
typedef enum im_casefold_mode {
    /* Full case folding (statuses C + F). */
    IM_CASEFOLD_FULL = 0,
    /* Simple 1:1 case folding (statuses C + S). */
    IM_CASEFOLD_SIMPLE,
    /* Full case folding with Turkic dotted/dotless I override (statuses C + F + T). */
    IM_CASEFOLD_TURKIC
} im_casefold_mode;

/* Fixed-size caller-allocated state for streaming chunked grapheme, word, or line breaking.
   Opaque internals; initialize with im_unicode_breaker_init() before feeding chunks. */
typedef struct im_unicode_breaker {
    uint8_t kind;
    uint8_t started;
    uint8_t finished;
    uint8_t phase;
    uint32_t prev_cp;
    uint8_t prev_prop;
    uint8_t prev_non_ignorable;
    uint8_t prev_prev_non_ignorable;
    uint8_t ri_odd;
    uint8_t incb_state;
    uint8_t extpic_state;
    uint8_t lb_prev_class;
    uint8_t lb_prev_non_sp;
    uint8_t lb_prev_prev_non_sp;
    uint8_t lb_sp_after;
    uint8_t lb_ri_odd;
    uint8_t lb_brahmic_state;
    uint8_t lb_op_sp;
    uint8_t lb_qu_pi_sp;
    uint8_t lb_is_seen;
    uint8_t lb_nu_state;
    uint8_t lb_hh_after_al;
    uint8_t pending_kind;
    uint8_t pending_first;
    uint32_t pending_cp;
    size_t pending_mid_count;
} im_unicode_breaker;

/* Return the pinned Unicode version string (e.g., "16.0.0"). */
const char *im_unicode_version(void);

/* Return a stable ASCII name for an im_unicode_error value. */
const char *im_unicode_error_name(im_unicode_error err);

/* Return the General_Category of codepoint cp (IM_GC_CN for unassigned or > 0x10FFFF). */
im_general_category im_unicode_general_category(uint32_t cp);

/* Return the 2-letter UCD abbreviation (e.g., "Lu", "Cn") for a General_Category value. */
const char *im_unicode_general_category_name(im_general_category gc);

/* Return the East_Asian_Width of codepoint cp (IM_EAW_N for > 0x10FFFF). */
im_east_asian_width im_unicode_east_asian_width(uint32_t cp);

/* Return the compact Script identifier of codepoint cp (0 = Unknown). */
uint16_t im_unicode_script(uint32_t cp);

/* Return the UCD Script name (e.g., "Latin", "Arabic", "Unknown") for a Script identifier. */
const char *im_unicode_script_name(uint16_t script);

/* Return the Canonical_Combining_Class (0..254) of codepoint cp (0 for > 0x10FFFF). */
uint8_t im_unicode_ccc(uint32_t cp);

/* Return the UAX #9 Bidi_Class of codepoint cp (including default classes for unassigned ranges; IM_BIDI_L for > 0x10FFFF). */
im_bidi_class im_unicode_bidi_class(uint32_t cp);

/* Return the Bidi_Mirroring_Glyph of codepoint cp, or 0 if cp has no mirrored glyph. */
uint32_t im_unicode_bidi_mirror(uint32_t cp);

/* Return the Bidi_Paired_Bracket_Type of cp and write its canonical paired bracket to *paired_out (0 if none). */
im_bidi_bracket_type im_unicode_bidi_bracket(uint32_t cp, uint32_t *paired_out);

/* Write the exact Unicode 16.0.0 character name of cp as a NUL-terminated ASCII string into buf (capacity cap bytes).
   Returns IM_UNICODE_OUT_OF_RANGE for cp > 0x10FFFF, IM_UNICODE_NOT_FOUND for valid unnamed codepoints,
   or IM_UNICODE_BUFFER_TOO_SMALL (setting *out_len to exact byte length excluding NUL without writing past cap). */
im_unicode_error im_unicode_name(uint32_t cp, char *buf, size_t cap, size_t *out_len);

/* Compute UAX #29 extended grapheme cluster boundaries for cps[0..len-1], writing len + 1 bytes (0 or 1) into breaks. */
im_unicode_error im_unicode_grapheme_breaks(const uint32_t *cps, size_t len, uint8_t *breaks, size_t breaks_cap);

/* Compute UAX #29 word boundaries for cps[0..len-1], writing len + 1 bytes (0 or 1) into breaks. */
im_unicode_error im_unicode_word_breaks(const uint32_t *cps, size_t len, uint8_t *breaks, size_t breaks_cap);

/* Compute UAX #14 line break opportunities for cps[0..len-1], writing len + 1 bytes (IM_BREAK_NONE/ALLOW/MUST) into breaks. */
im_unicode_error im_unicode_line_breaks(const uint32_t *cps, size_t len, uint8_t *breaks, size_t breaks_cap);

/* Initialize a streaming chunked breaker for grapheme, word, or line segmentation. */
im_unicode_error im_unicode_breaker_init(im_unicode_breaker *st, im_unicode_break_kind kind);

/* Feed a chunk cps[0..len-1] into a streaming breaker; pass is_end=true on the final chunk to flush trailing boundaries.
   Concatenated outputs across chunks equal the corresponding whole-buffer breaks[0..total_len].
   On IM_UNICODE_BUFFER_TOO_SMALL, *out_written receives the exact minimum breaks_cap needed for this call and nothing is written past breaks_cap. */
im_unicode_error im_unicode_breaker_feed(im_unicode_breaker *st, const uint32_t *cps, size_t len, bool is_end,
                                         uint8_t *breaks, size_t breaks_cap, size_t *out_written);

/* Return the exact scratch buffer size in bytes required by im_unicode_bidi_resolve* for a paragraph of len codepoints. */
size_t im_unicode_bidi_scratch_size(size_t len);

/* Resolve UAX #9 paragraph embedding levels (and optional whole-paragraph visual order) for cps[0..len-1].
   base_level: 0 (LTR), 1 (RTL), or -1 (auto P2/P3). Caller provides scratch[scratch_bytes] (no heap allocation).
   levels_out[levels_cap] receives resolved levels (0..125); order_out (optional, NULL to skip) receives visual-to-logical indices. */
im_unicode_error im_unicode_bidi_resolve(const uint32_t *cps, size_t len, int8_t base_level,
                                         void *scratch, size_t scratch_bytes,
                                         uint8_t *levels_out, size_t levels_cap,
                                         size_t *order_out, size_t order_cap,
                                         int8_t *resolved_base_out);

/* Resolve UAX #9 embedding levels directly from an array of im_bidi_class values (for UCD BidiTest.txt verification). */
im_unicode_error im_unicode_bidi_resolve_classes(const uint8_t *classes, size_t len, int8_t base_level,
                                                 void *scratch, size_t scratch_bytes,
                                                 uint8_t *levels_out, size_t levels_cap,
                                                 size_t *order_out, size_t order_cap,
                                                 int8_t *resolved_base_out);

/* Apply UAX #9 rules L1 (trailing whitespace/separator/isolate reset) and L2 (visual reversal) to soft-wrapped line [line_start, line_end).
   Writes per-line resolved levels into line_levels_out (optional, NULL to skip) and logical paragraph indices into visual_order_out. */
im_unicode_error im_unicode_bidi_reorder_line(const uint32_t *cps, const uint8_t *para_levels, size_t para_len,
                                              int8_t resolved_base, size_t line_start, size_t line_end,
                                              uint8_t *line_levels_out, size_t line_levels_cap,
                                              size_t *visual_order_out, size_t visual_order_cap);

/* Normalize src[0..src_len-1] into dst[0..dst_cap-1] (capacity unit: uint32_t codepoints) using UAX #15 form.
   Always writes the exact required output codepoint count to *out_len; returns IM_UNICODE_BUFFER_TOO_SMALL without writing past dst_cap. */
im_unicode_error im_unicode_normalize(im_norm_form form, const uint32_t *src, size_t src_len,
                                      uint32_t *dst, size_t dst_cap, size_t *out_len);

/* Case-fold src[0..src_len-1] into dst[0..dst_cap-1] (capacity unit: uint32_t codepoints) using mode (unlisted codepoints pass through unchanged).
   Always writes the exact required output codepoint count to *out_len; returns IM_UNICODE_BUFFER_TOO_SMALL without writing past dst_cap. */
im_unicode_error im_unicode_casefold(im_casefold_mode mode, const uint32_t *src, size_t src_len,
                                     uint32_t *dst, size_t dst_cap, size_t *out_len);
