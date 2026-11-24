/* Impossible SDK: freestanding Tier 0 test fixture for parts/unicode/.
   Modes:
     (no args)          self-tests (buffers/guards, wrapped bidi L1/L2, every-cut streaming breaks, mixed casefold, errors)
     --suite-grapheme   stdin: lines of "HEX ... | 0101..." -> verifies im_unicode_grapheme_breaks + streaming cuts
     --suite-word       stdin: lines of "HEX ... | 0101..." -> verifies im_unicode_word_breaks + streaming cuts
     --suite-line       stdin: lines of "HEX ... | 0101..." -> verifies im_unicode_line_breaks + streaming cuts
     --suite-bidi       stdin: lines of "BITSET | CLS ... | LVL ... | ORD ..." -> verifies im_unicode_bidi_resolve_classes
     --suite-bidi-char  stdin: lines of "BASE | HEX ... | EXP_BASE | LVL ... | ORD ..." -> verifies im_unicode_bidi_resolve
     --suite-norm       stdin: lines of "SRC | NFC | NFD | NFKC | NFKD" -> verifies im_unicode_normalize
     --suite-casefold   stdin: lines of "MODE | SRC | EXP" -> verifies im_unicode_casefold
     --suite-props      stdin: lines of "CP | GC | EAW | CCC | BIDI | SCRIPT | MIRROR | BTYPE | BPAIR | NAME" -> verifies all lookups
*/
#include <impossible/base.h>
#include <impossible/file.h>
#include "unicode.h"

static void print_u64(uint64_t v) {
    char buf[32];
    size_t n = 0u;
    do {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    char out[32];
    for (size_t i = 0u; i < n; i++) out[i] = buf[n - 1u - i];
    out[n] = '\0';
    im_print(out);
}

static bool str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static size_t cstr_len(const char *s) {
    size_t n = 0u;
    while (s[n] != '\0') n++;
    return n;
}

static uint32_t parse_hex(const char *s, size_t *pos, size_t end) {
    while (*pos < end && (s[*pos] == ' ' || s[*pos] == '\t')) (*pos)++;
    uint32_t v = 0u;
    while (*pos < end) {
        char c = s[*pos];
        if (c >= '0' && c <= '9') v = (v << 4) | (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (uint32_t)(c - 'A' + 10);
        else break;
        (*pos)++;
    }
    return v;
}

static int64_t parse_int(const char *s, size_t *pos, size_t end) {
    while (*pos < end && (s[*pos] == ' ' || s[*pos] == '\t')) (*pos)++;
    bool neg = false;
    if (*pos < end && s[*pos] == '-') { neg = true; (*pos)++; }
    int64_t v = 0;
    while (*pos < end && s[*pos] >= '0' && s[*pos] <= '9') {
        v = v * 10 + (s[*pos] - '0');
        (*pos)++;
    }
    return neg ? -v : v;
}

static size_t parse_hex_list(const char *s, size_t start, size_t end, uint32_t *out, size_t cap) {
    size_t n = 0u, pos = start;
    while (pos < end) {
        while (pos < end && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        if (pos >= end) break;
        uint32_t v = parse_hex(s, &pos, end);
        if (n < cap) out[n] = v;
        n++;
    }
    return n;
}

static size_t parse_u8_list(const char *s, size_t start, size_t end, uint8_t *out, size_t cap) {
    size_t n = 0u, pos = start;
    while (pos < end) {
        while (pos < end && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        if (pos >= end) break;
        int64_t v = parse_int(s, &pos, end);
        if (n < cap) out[n] = (uint8_t)v;
        n++;
    }
    return n;
}

static size_t find_pipe(const char *s, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        if (s[i] == '|') return i;
    }
    return end;
}

static int run_self_tests(void) {
    /* 1. Version and error names */
    if (!str_eq(im_unicode_version(), "16.0.0")) return 1;
    if (!str_eq(im_unicode_error_name(IM_UNICODE_OK), "IM_UNICODE_OK")) return 2;
    if (!str_eq(im_unicode_error_name(IM_UNICODE_BUFFER_TOO_SMALL), "IM_UNICODE_BUFFER_TOO_SMALL")) return 3;

    /* 2. im_unicode_name: assigned, unnamed, out-of-range, zero-cap, one-short guard, exact retry */
    size_t nlen = 0u;
    if (im_unicode_name(0x110000u, NULL, 0u, &nlen) != IM_UNICODE_OUT_OF_RANGE || nlen != 0u) return 10;
    if (im_unicode_name(0x0378u, NULL, 0u, &nlen) != IM_UNICODE_NOT_FOUND || nlen != 0u) return 11;
    if (im_unicode_name(0x0041u, NULL, 0u, &nlen) != IM_UNICODE_BUFFER_TOO_SMALL || nlen != 22u) return 12;

    char nbuf[32];
    memset(nbuf, 0x5A, sizeof(nbuf));
    if (im_unicode_name(0x0041u, nbuf, 22u, &nlen) != IM_UNICODE_BUFFER_TOO_SMALL || nlen != 22u) return 13;
    for (size_t i = 0u; i < sizeof(nbuf); i++) {
        if ((uint8_t)nbuf[i] != 0x5Au) return 14; /* must not write past cap (or corrupt guard) */
    }
    if (im_unicode_name(0x0041u, nbuf, 23u, &nlen) != IM_UNICODE_OK || nlen != 22u) return 15;
    if (!str_eq(nbuf, "LATIN CAPITAL LETTER A")) return 16;
    if ((uint8_t)nbuf[23] != 0x5Au) return 17;

    /* 3. Case folding: mixed string with unchanged chars, expanding ß -> ss, Turkic I/İ/ı, guard sentinels */
    const uint32_t mix_in[] = { 'a', '1', '!', 0x00DFu, 'I', 0x0130u, 0x0131u, 'Z' };
    const uint32_t mix_full[] = { 'a', '1', '!', 's', 's', 'i', 'i', 0x0307u, 0x0131u, 'z' };
    const uint32_t mix_turk[] = { 'a', '1', '!', 's', 's', 0x0131u, 'i', 0x0131u, 'z' };
    const uint32_t mix_simp[] = { 'a', '1', '!', 0x00DFu, 'i', 0x0130u, 0x0131u, 'z' };

    uint32_t cf_buf[16];
    size_t cf_len = 0u;
    if (im_unicode_casefold(IM_CASEFOLD_FULL, mix_in, 8u, NULL, 0u, &cf_len) != IM_UNICODE_BUFFER_TOO_SMALL || cf_len != 10u) return 20;
    for (size_t i = 0u; i < 16u; i++) cf_buf[i] = 0xDEADBEEFu;
    if (im_unicode_casefold(IM_CASEFOLD_FULL, mix_in, 8u, cf_buf, 9u, &cf_len) != IM_UNICODE_BUFFER_TOO_SMALL || cf_len != 10u) return 21;
    if (cf_buf[9] != 0xDEADBEEFu) return 22;
    if (im_unicode_casefold(IM_CASEFOLD_FULL, mix_in, 8u, cf_buf, 10u, &cf_len) != IM_UNICODE_OK || cf_len != 10u) return 23;
    if (cf_buf[10] != 0xDEADBEEFu) return 24;
    for (size_t i = 0u; i < 10u; i++) if (cf_buf[i] != mix_full[i]) return 25;

    if (im_unicode_casefold(IM_CASEFOLD_TURKIC, mix_in, 8u, cf_buf, 16u, &cf_len) != IM_UNICODE_OK || cf_len != 9u) return 26;
    for (size_t i = 0u; i < 9u; i++) if (cf_buf[i] != mix_turk[i]) return 27;

    if (im_unicode_casefold(IM_CASEFOLD_SIMPLE, mix_in, 8u, cf_buf, 16u, &cf_len) != IM_UNICODE_OK || cf_len != 8u) return 28;
    for (size_t i = 0u; i < 8u; i++) if (cf_buf[i] != mix_simp[i]) return 29;

    /* 4. Normalization buffer-too-small, zero-cap, one-short guard, exact retry */
    const uint32_t norm_in[] = { 0x0390u }; /* NFD -> 0x03B9 0x0308 0x0301 (3 codepoints) */
    const uint32_t norm_exp[] = { 0x03B9u, 0x0308u, 0x0301u };
    uint32_t norm_buf[8];
    size_t norm_len = 0u;
    if (im_unicode_normalize(IM_NORM_NFD, norm_in, 1u, NULL, 0u, &norm_len) != IM_UNICODE_BUFFER_TOO_SMALL || norm_len != 3u) return 30;
    for (size_t i = 0u; i < 8u; i++) norm_buf[i] = 0xCAFEBABEu;
    if (im_unicode_normalize(IM_NORM_NFD, norm_in, 1u, norm_buf, 2u, &norm_len) != IM_UNICODE_BUFFER_TOO_SMALL || norm_len != 3u) return 31;
    if (norm_buf[2] != 0xCAFEBABEu) return 32;
    if (im_unicode_normalize(IM_NORM_NFD, norm_in, 1u, norm_buf, 3u, &norm_len) != IM_UNICODE_OK || norm_len != 3u) return 33;
    if (norm_buf[3] != 0xCAFEBABEu) return 34;
    for (size_t i = 0u; i < 3u; i++) if (norm_buf[i] != norm_exp[i]) return 35;

    /* 5. Bidi short-scratch and short-output guard tests + wrapped L1/L2 at two wrap positions */
    /* Paragraph: "אב cd הו " (RTL base=1): indices 0:'א', 1:'ב', 2:' ', 3:'c', 4:'d', 5:' ', 6:'ה', 7:'ו', 8:' ' */
    const uint32_t para[] = { 0x05D0u, 0x05D1u, ' ', 'c', 'd', ' ', 0x05D4u, 0x05D5u, ' ' };
    size_t sc_need = im_unicode_bidi_scratch_size(9u);
    uint8_t *sc = (uint8_t *)im_alloc(sc_need + 16u);
    if (!sc) return 40;
    memset(sc, 0xA5, sc_need + 16u);
    uint8_t lvls[16];
    size_t ord[16];
    int8_t rbase = -1;
    memset(lvls, 0xEE, sizeof(lvls));
    for (size_t i = 0u; i < 16u; i++) ord[i] = 0x7777u;

    /* Short scratch (1 byte short) must fail and not touch guard */
    if (im_unicode_bidi_resolve(para, 9u, -1, sc, sc_need - 1u, lvls, 9u, ord, 9u, &rbase) != IM_UNICODE_BUFFER_TOO_SMALL) return 41;
    if (sc[sc_need - 1u] != 0xA5u) return 42;
    /* Short levels_out (1 unit short) must fail and not touch guard */
    if (im_unicode_bidi_resolve(para, 9u, -1, sc, sc_need, lvls, 8u, ord, 9u, &rbase) != IM_UNICODE_BUFFER_TOO_SMALL) return 43;
    if (lvls[8] != 0xEEu) return 44;
    /* Exact-size retry succeeds */
    if (im_unicode_bidi_resolve(para, 9u, -1, sc, sc_need, lvls, 9u, ord, 9u, &rbase) != IM_UNICODE_OK) return 45;
    if (rbase != 1 || lvls[9] != 0xEEu || ord[9] != 0x7777u) return 46;
    im_free(sc);

    /* Wrap 1: line1 = [0, 6) "אב cd " (trailing space at index 5 resets to para_level=1), line2 = [6, 9) "הו " */
    /* Also test LTR base=0 paragraph: "ab אב " -> indices 0:'a',1:'b',2:' ',3:'א',4:'ב',5:' ',6:'c',7:'d' */
    const uint32_t para_ltr[] = { 'a', 'b', ' ', 0x05D0u, 0x05D1u, ' ', 'c', 'd' };
    size_t sc2_need = im_unicode_bidi_scratch_size(8u);
    uint8_t *sc2 = (uint8_t *)im_alloc(sc2_need);
    uint8_t pl2[8];
    int8_t rb2 = -1;
    if (im_unicode_bidi_resolve(para_ltr, 8u, 0, sc2, sc2_need, pl2, 8u, NULL, 0u, &rb2) != IM_UNICODE_OK || rb2 != 0) return 47;
    im_free(sc2);
    /* Paragraph levels for "ab אב cd" (base=0): 0,0,0,1,1,0,0,0.
     * Now test "ab אב הו cd" where space between Hebrew words has level 1 in paragraph,
     * so wrapping AFTER that space resets it to level 0 via L1! */
    const uint32_t para_wrap[] = { 'a', 'b', ' ', 0x05D0u, 0x05D1u, ' ', 0x05D4u, 0x05D5u, ' ', 'c', 'd' };
    size_t sc3_need = im_unicode_bidi_scratch_size(11u);
    uint8_t *sc3 = (uint8_t *)im_alloc(sc3_need);
    uint8_t pl3[11];
    int8_t rb3 = -1;
    if (im_unicode_bidi_resolve(para_wrap, 11u, 0, sc3, sc3_need, pl3, 11u, NULL, 0u, &rb3) != IM_UNICODE_OK || rb3 != 0) return 48;
    im_free(sc3);
    /* In full paragraph, index 5 (' ' between אב and הו) has para_level 1! */
    if (pl3[5] != 1u) return 49;
    /* Wrap A at [0, 6) ("ab אב "): index 5 is now trailing space on line -> L1 resets line_levels[5] to 0!
     * Visual order for [0, 6): 0, 1, 2, 4, 3, 5 (trailing space at visual right, NOT reversed with Hebrew!). */
    uint8_t ll_a[6];
    size_t vo_a[6];
    if (im_unicode_bidi_reorder_line(para_wrap, pl3, 11u, rb3, 0u, 6u, ll_a, 6u, vo_a, 6u) != IM_UNICODE_OK) return 50;
    const size_t exp_a[6] = { 0u, 1u, 2u, 4u, 3u, 5u };
    if (ll_a[5] != 0u) return 51;
    for (size_t i = 0u; i < 6u; i++) if (vo_a[i] != exp_a[i]) return 52;
    /* Wrap B at [0, 8) ("ab אב הו"): index 5 is interior space (stays level 1!).
     * Visual order for [0, 8): 0, 1, 2, 7, 6, 5, 4, 3. */
    uint8_t ll_b[8];
    size_t vo_b[8];
    if (im_unicode_bidi_reorder_line(para_wrap, pl3, 11u, rb3, 0u, 8u, ll_b, 8u, vo_b, 8u) != IM_UNICODE_OK) return 53;
    const size_t exp_b[8] = { 0u, 1u, 2u, 7u, 6u, 5u, 4u, 3u };
    if (ll_b[5] != 1u) return 54;
    for (size_t i = 0u; i < 8u; i++) if (vo_b[i] != exp_b[i]) return 55;

    /* 6. Streaming chunked breaking: every single cut position matches whole-buffer exactly */
    const uint32_t s_ri[] = { 0x1F1FAu, 0x1F1F8u, 0x1F1ECu, 0x1F1E7u, 0x1F1EFu };
    const uint32_t s_zwj[] = { 0x1F469u, 0x1F3FDu, 0x200Du, 0x1F52Cu, 'a', '\'', 0x0301u, 'b' };
    const uint32_t s_wb[] = { 'd', 'o', 'n', '\'', 't', ' ', '3', '.', '1', '4', ' ', 0x05D0u, '"', 0x05D1u };
    const uint32_t s_lb[] = { '$', '(', '1', '0', '0', ')', ' ', '.', '5', ' ', 0x1F1FAu, 0x1F1F8u, 0x1F1ECu };
    const uint32_t *samples[4] = { s_ri, s_zwj, s_wb, s_lb };
    const size_t slens[4] = { 5u, 8u, 14u, 13u };

    for (unsigned kind = 0u; kind < 3u; kind++) {
        for (size_t si = 0u; si < 4u; si++) {
            uint8_t whole[32];
            size_t n = slens[si];
            if (kind == 0u) im_unicode_grapheme_breaks(samples[si], n, whole, 32u);
            else if (kind == 1u) im_unicode_word_breaks(samples[si], n, whole, 32u);
            else im_unicode_line_breaks(samples[si], n, whole, 32u);

            for (size_t cut = 0u; cut <= n; cut++) {
                im_unicode_breaker st;
                im_unicode_breaker_init(&st, (im_unicode_break_kind)kind);
                uint8_t chunked[32];
                size_t w1 = 0u, w2 = 0u;
                if (im_unicode_breaker_feed(&st, samples[si], cut, false, chunked, 32u, &w1) != IM_UNICODE_OK) return 60;
                if (im_unicode_breaker_feed(&st, samples[si] + cut, n - cut, true, chunked + w1, 32u - w1, &w2) != IM_UNICODE_OK) return 61;
                if (w1 + w2 != n + 1u) return 62;
                if (memcmp(whole, chunked, n + 1u) != 0) return 63;
            }
            /* Also test 1-codepoint-at-a-time streaming */
            im_unicode_breaker st;
            im_unicode_breaker_init(&st, (im_unicode_break_kind)kind);
            uint8_t single[32];
            size_t tot = 0u;
            for (size_t k = 0u; k < n; k++) {
                size_t wk = 0u;
                if (im_unicode_breaker_feed(&st, samples[si] + k, 1u, false, single + tot, 32u - tot, &wk) != IM_UNICODE_OK) return 64;
                tot += wk;
            }
            size_t wf = 0u;
            if (im_unicode_breaker_feed(&st, NULL, 0u, true, single + tot, 32u - tot, &wf) != IM_UNICODE_OK) return 65;
            tot += wf;
            if (tot != n + 1u || memcmp(whole, single, n + 1u) != 0) return 66;
        }
    }

    /* 7. Breaker short-buffer guard test */
    {
        im_unicode_breaker st;
        im_unicode_breaker_init(&st, IM_BREAK_KIND_GRAPHEME);
        uint8_t bbuf[8];
        memset(bbuf, 0xCC, sizeof(bbuf));
        size_t req = 0u;
        if (im_unicode_breaker_feed(&st, s_ri, 5u, true, bbuf, 5u, &req) != IM_UNICODE_BUFFER_TOO_SMALL || req != 6u) return 70;
        if (bbuf[5] != 0xCCu) return 71;
        if (im_unicode_breaker_feed(&st, s_ri, 5u, true, bbuf, 6u, &req) != IM_UNICODE_OK || req != 6u) return 72;
        if (bbuf[6] != 0xCCu) return 73;
    }

    im_print("SELF_TESTS OK\n");
    return 0;
}

static int run_break_suite(im_unicode_break_kind kind, const char *label, const char *data, size_t len) {
    uint64_t executed = 0u, failed = 0u;
    uint32_t cps[256];
    uint8_t got[257];
    uint8_t chunked[257];
    size_t pos = 0u;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < len) pos++;
        if (line_end > line_start && data[line_end - 1u] == '\r') line_end--;
        if (line_start == line_end) continue;

        size_t pipe = find_pipe(data, line_start, line_end);
        size_t n = parse_hex_list(data, line_start, pipe, cps, 256u);
        size_t exp_start = pipe + 1u;
        while (exp_start < line_end && (data[exp_start] == ' ' || data[exp_start] == '\t')) exp_start++;
        size_t exp_len = line_end - exp_start;
        executed++;

        im_unicode_error err;
        if (kind == IM_BREAK_KIND_GRAPHEME) err = im_unicode_grapheme_breaks(cps, n, got, 257u);
        else if (kind == IM_BREAK_KIND_WORD) err = im_unicode_word_breaks(cps, n, got, 257u);
        else err = im_unicode_line_breaks(cps, n, got, 257u);

        bool ok = (err == IM_UNICODE_OK && exp_len == n + 1u);
        if (ok) {
            for (size_t i = 0u; i < exp_len; i++) {
                uint8_t exp_b = (data[exp_start + i] == '1') ? 1u : 0u;
                uint8_t got_b = (got[i] != IM_BREAK_NONE) ? 1u : 0u;
                if (got_b != exp_b) { ok = false; break; }
            }
        }
        /* Also verify 1-by-1 streaming matches whole-buffer on every vector */
        if (ok) {
            im_unicode_breaker st;
            im_unicode_breaker_init(&st, kind);
            size_t tot = 0u;
            for (size_t i = 0u; i < n; i++) {
                size_t wi = 0u;
                if (im_unicode_breaker_feed(&st, cps + i, 1u, false, chunked + tot, 257u - tot, &wi) != IM_UNICODE_OK) {
                    ok = false; break;
                }
                tot += wi;
            }
            if (ok) {
                size_t wf = 0u;
                if (im_unicode_breaker_feed(&st, NULL, 0u, true, chunked + tot, 257u - tot, &wf) != IM_UNICODE_OK) ok = false;
                tot += wf;
            }
            if (ok && (tot != n + 1u || memcmp(got, chunked, n + 1u) != 0)) ok = false;
        }
        if (!ok) {
            if (failed < 5u) {
                im_print("FAIL "); im_print(label); im_print(" #"); print_u64(executed); im_print("\n");
            }
            failed++;
        }
    }
    im_print("SUITE "); im_print(label); im_print(" executed="); print_u64(executed);
    im_print(" failed="); print_u64(failed); im_print("\n");
    return (failed == 0u && executed > 0u) ? 0 : 1;
}

static int run_bidi_suite(bool char_mode, const char *label, const char *data, size_t len) {
    uint64_t executed = 0u, failed = 0u;
    size_t sc_cap = im_unicode_bidi_scratch_size(1024u);
    void *scratch = im_alloc(sc_cap);
    if (!scratch) return 2;
    uint32_t cps[1024];
    uint8_t cls[1024];
    uint8_t exp_lvl[1024];
    uint8_t got_lvl[1024];
    size_t got_ord[1024];
    size_t exp_ord[1024];

    size_t pos = 0u;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < len) pos++;
        if (line_end > line_start && data[line_end - 1u] == '\r') line_end--;
        if (line_start == line_end) continue;

        size_t p1 = find_pipe(data, line_start, line_end);
        size_t p2 = find_pipe(data, p1 + 1u, line_end);
        size_t p3 = find_pipe(data, p2 + 1u, line_end);

        if (!char_mode) {
            /* BidiTest.txt: BITSET | CLS... | EXP_LVL (255='x')... | EXP_ORD... */
            size_t tmp_p = line_start;
            uint32_t bitset = (uint32_t)parse_int(data, &tmp_p, p1);
            size_t n = parse_u8_list(data, p1 + 1u, p2, cls, 1024u);
            size_t nl = parse_u8_list(data, p2 + 1u, p3, exp_lvl, 1024u);
            size_t no = 0u;
            {
                size_t op = p3 + 1u;
                while (op < line_end) {
                    while (op < line_end && (data[op] == ' ' || data[op] == '\t')) op++;
                    if (op >= line_end) break;
                    exp_ord[no++] = (size_t)parse_int(data, &op, line_end);
                }
            }
            const int8_t modes[3] = { -1, 0, 1 };
            for (unsigned m = 0u; m < 3u; m++) {
                if ((bitset & (1u << m)) == 0u) continue;
                executed++;
                int8_t rb = -1;
                im_unicode_error err = im_unicode_bidi_resolve_classes(cls, n, modes[m], scratch, sc_cap,
                                                                       got_lvl, 1024u, got_ord, 1024u, &rb);
                bool ok = (err == IM_UNICODE_OK && nl == n);
                if (ok) {
                    for (size_t i = 0u; i < n; i++) {
                        if (exp_lvl[i] != 255u && got_lvl[i] != exp_lvl[i]) { ok = false; break; }
                    }
                }
                if (ok) {
                    /* Compare non-x visual order */
                    size_t oi = 0u;
                    for (size_t i = 0u; i < n; i++) {
                        size_t idx = got_ord[i];
                        if (exp_lvl[idx] != 255u) {
                            if (oi >= no || exp_ord[oi] != idx) { ok = false; break; }
                            oi++;
                        }
                    }
                    if (oi != no) ok = false;
                }
                if (!ok) {
                    if (failed < 5u) {
                        im_print("FAIL "); im_print(label); im_print(" #"); print_u64(executed); im_print("\n");
                    }
                    failed++;
                }
            }
        } else {
            /* BidiCharacterTest.txt: BASE | HEX... | EXP_BASE | EXP_LVL (255='x')... | EXP_ORD... */
            size_t p4 = find_pipe(data, p3 + 1u, line_end);
            size_t tmp_p = line_start;
            int8_t req_base = (int8_t)parse_int(data, &tmp_p, p1);
            size_t n = parse_hex_list(data, p1 + 1u, p2, cps, 1024u);
            tmp_p = p2 + 1u;
            int8_t exp_base = (int8_t)parse_int(data, &tmp_p, p3);
            size_t nl = parse_u8_list(data, p3 + 1u, p4, exp_lvl, 1024u);
            size_t no = 0u;
            {
                size_t op = p4 + 1u;
                while (op < line_end) {
                    while (op < line_end && (data[op] == ' ' || data[op] == '\t')) op++;
                    if (op >= line_end) break;
                    exp_ord[no++] = (size_t)parse_int(data, &op, line_end);
                }
            }
            executed++;
            int8_t rb = -1;
            im_unicode_error err = im_unicode_bidi_resolve(cps, n, req_base, scratch, sc_cap,
                                                           got_lvl, 1024u, got_ord, 1024u, &rb);
            bool ok = (err == IM_UNICODE_OK && rb == exp_base && nl == n);
            if (ok) {
                for (size_t i = 0u; i < n; i++) {
                    if (exp_lvl[i] != 255u && got_lvl[i] != exp_lvl[i]) { ok = false; break; }
                }
            }
            if (ok) {
                size_t oi = 0u;
                for (size_t i = 0u; i < n; i++) {
                    size_t idx = got_ord[i];
                    if (exp_lvl[idx] != 255u) {
                        if (oi >= no || exp_ord[oi] != idx) { ok = false; break; }
                        oi++;
                    }
                }
                if (oi != no) ok = false;
            }
            if (!ok) {
                if (failed < 5u) {
                    im_print("FAIL "); im_print(label); im_print(" #"); print_u64(executed); im_print("\n");
                }
                failed++;
            }
        }
    }
    im_free(scratch);
    im_print("SUITE "); im_print(label); im_print(" executed="); print_u64(executed);
    im_print(" failed="); print_u64(failed); im_print("\n");
    return (failed == 0u && executed > 0u) ? 0 : 1;
}

static bool eq_u32(const uint32_t *a, size_t na, const uint32_t *b, size_t nb) {
    if (na != nb) return false;
    for (size_t i = 0u; i < na; i++) if (a[i] != b[i]) return false;
    return true;
}

static int run_norm_suite(const char *data, size_t len) {
    uint64_t executed = 0u, failed = 0u;
    uint32_t c[5][64];
    size_t cn[5];
    uint32_t out[128];
    size_t pos = 0u;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < len) pos++;
        if (line_end > line_start && data[line_end - 1u] == '\r') line_end--;
        if (line_start == line_end) continue;

        size_t cur = line_start;
        for (int f = 0; f < 5; f++) {
            size_t next = (f < 4) ? find_pipe(data, cur, line_end) : line_end;
            cn[f] = parse_hex_list(data, cur, next, c[f], 64u);
            cur = next + 1u;
        }
        executed++;
        bool ok = true;
        size_t ol = 0u;
        /* Per NormalizationTest.txt invariants:
         * c2 == NFC(c1) == NFC(c2) == NFC(c3); c4 == NFC(c4) == NFC(c5)
         * c3 == NFD(c1) == NFD(c2) == NFD(c3); c5 == NFD(c4) == NFD(c5)
         * c4 == NFKC(c1..c5); c5 == NFKD(c1..c5) */
        for (int k = 0; k < 3 && ok; k++) {
            if (im_unicode_normalize(IM_NORM_NFC, c[k], cn[k], out, 128u, &ol) != IM_UNICODE_OK || !eq_u32(out, ol, c[1], cn[1])) ok = false;
            if (im_unicode_normalize(IM_NORM_NFD, c[k], cn[k], out, 128u, &ol) != IM_UNICODE_OK || !eq_u32(out, ol, c[2], cn[2])) ok = false;
        }
        for (int k = 3; k < 5 && ok; k++) {
            if (im_unicode_normalize(IM_NORM_NFC, c[k], cn[k], out, 128u, &ol) != IM_UNICODE_OK || !eq_u32(out, ol, c[3], cn[3])) ok = false;
            if (im_unicode_normalize(IM_NORM_NFD, c[k], cn[k], out, 128u, &ol) != IM_UNICODE_OK || !eq_u32(out, ol, c[4], cn[4])) ok = false;
        }
        for (int k = 0; k < 5 && ok; k++) {
            if (im_unicode_normalize(IM_NORM_NFKC, c[k], cn[k], out, 128u, &ol) != IM_UNICODE_OK || !eq_u32(out, ol, c[3], cn[3])) ok = false;
            if (im_unicode_normalize(IM_NORM_NFKD, c[k], cn[k], out, 128u, &ol) != IM_UNICODE_OK || !eq_u32(out, ol, c[4], cn[4])) ok = false;
        }
        if (!ok) {
            if (failed < 5u) {
                im_print("FAIL norm #"); print_u64(executed); im_print("\n");
            }
            failed++;
        }
    }
    im_print("SUITE norm executed="); print_u64(executed);
    im_print(" failed="); print_u64(failed); im_print("\n");
    return (failed == 0u && executed > 0u) ? 0 : 1;
}

static int run_casefold_suite(const char *data, size_t len) {
    uint64_t executed = 0u, failed = 0u;
    uint32_t src[16], exp[16], out[16];
    size_t pos = 0u;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < len) pos++;
        if (line_end > line_start && data[line_end - 1u] == '\r') line_end--;
        if (line_start == line_end) continue;

        size_t p1 = find_pipe(data, line_start, line_end);
        size_t p2 = find_pipe(data, p1 + 1u, line_end);
        size_t mp = line_start;
        int64_t mode = parse_int(data, &mp, p1);
        size_t sn = parse_hex_list(data, p1 + 1u, p2, src, 16u);
        size_t en = parse_hex_list(data, p2 + 1u, line_end, exp, 16u);
        executed++;
        size_t ol = 0u;
        im_unicode_error err = im_unicode_casefold((im_casefold_mode)mode, src, sn, out, 16u, &ol);
        if (err != IM_UNICODE_OK || !eq_u32(out, ol, exp, en)) {
            if (failed < 5u) {
                im_print("FAIL casefold #"); print_u64(executed); im_print("\n");
            }
            failed++;
        }
    }
    im_print("SUITE casefold executed="); print_u64(executed);
    im_print(" failed="); print_u64(failed); im_print("\n");
    return (failed == 0u && executed > 0u) ? 0 : 1;
}

static int run_props_suite(const char *data, size_t len) {
    uint64_t executed = 0u, failed = 0u;
    char nbuf[160];
    size_t pos = 0u;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && data[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < len) pos++;
        if (line_end > line_start && data[line_end - 1u] == '\r') line_end--;
        if (line_start == line_end) continue;

        /* Format: CP_HEX|GC|EAW|CCC|BIDI|SCRIPT_NAME|MIRROR_HEX|BTYPE|BPAIR_HEX|NAME */
        size_t f[10];
        size_t cur = line_start;
        for (int i = 0; i < 9; i++) {
            f[i] = find_pipe(data, cur, line_end);
            cur = f[i] + 1u;
        }
        size_t p = line_start;
        uint32_t cp = parse_hex(data, &p, f[0]);
        p = f[0] + 1u; int64_t gc = parse_int(data, &p, f[1]);
        p = f[1] + 1u; int64_t eaw = parse_int(data, &p, f[2]);
        p = f[2] + 1u; int64_t ccc = parse_int(data, &p, f[3]);
        p = f[3] + 1u; int64_t bidi = parse_int(data, &p, f[4]);
        size_t sn_start = f[4] + 1u, sn_len = f[5] - sn_start;
        p = f[5] + 1u; uint32_t mirror = parse_hex(data, &p, f[6]);
        p = f[6] + 1u; int64_t btype = parse_int(data, &p, f[7]);
        p = f[7] + 1u; uint32_t bpair = parse_hex(data, &p, f[8]);
        size_t nm_start = f[8] + 1u, nm_len = line_end - nm_start;

        executed++;
        bool ok = true;
        if ((int64_t)im_unicode_general_category(cp) != gc) ok = false;
        if ((int64_t)im_unicode_east_asian_width(cp) != eaw) ok = false;
        if ((int64_t)im_unicode_ccc(cp) != ccc) ok = false;
        if ((int64_t)im_unicode_bidi_class(cp) != bidi) ok = false;
        const char *sname = im_unicode_script_name(im_unicode_script(cp));
        if (cstr_len(sname) != sn_len || memcmp(sname, data + sn_start, sn_len) != 0) ok = false;
        if (im_unicode_bidi_mirror(cp) != mirror) ok = false;
        uint32_t got_bp = 0u;
        if ((int64_t)im_unicode_bidi_bracket(cp, &got_bp) != btype || got_bp != bpair) ok = false;
        size_t got_nl = 0u;
        im_unicode_error nerr = im_unicode_name(cp, nbuf, sizeof(nbuf), &got_nl);
        if (nm_len == 0u) {
            if (nerr != IM_UNICODE_NOT_FOUND || got_nl != 0u) ok = false;
        } else {
            if (nerr != IM_UNICODE_OK || got_nl != nm_len || memcmp(nbuf, data + nm_start, nm_len) != 0) ok = false;
        }
        if (!ok) {
            if (failed < 5u) {
                im_print("FAIL props cp="); print_u64(cp); im_print("\n");
            }
            failed++;
        }
    }
    im_print("SUITE props executed="); print_u64(executed);
    im_print(" failed="); print_u64(failed); im_print("\n");
    return (failed == 0u && executed > 0u) ? 0 : 1;
}

int im_main(void) {
    bool is_g = im_has_argument(L"--suite-grapheme");
    bool is_w = im_has_argument(L"--suite-word");
    bool is_l = im_has_argument(L"--suite-line");
    bool is_b = im_has_argument(L"--suite-bidi");
    bool is_bc = im_has_argument(L"--suite-bidi-char");
    bool is_n = im_has_argument(L"--suite-norm");
    bool is_cf = im_has_argument(L"--suite-casefold");
    bool is_pr = im_has_argument(L"--suite-props");

    if (!is_g && !is_w && !is_l && !is_b && !is_bc && !is_n && !is_cf && !is_pr) {
        return run_self_tests();
    }

    char *data = NULL;
    size_t len = 0u;
    if (im_read_input(128u * 1024u * 1024u, &data, &len) != IM_OK) {
        im_print("ERROR: failed to read stdin\n");
        return 3;
    }

    int rc = 1;
    if (is_g) rc = run_break_suite(IM_BREAK_KIND_GRAPHEME, "grapheme", data, len);
    else if (is_w) rc = run_break_suite(IM_BREAK_KIND_WORD, "word", data, len);
    else if (is_l) rc = run_break_suite(IM_BREAK_KIND_LINE, "line", data, len);
    else if (is_b) rc = run_bidi_suite(false, "bidi", data, len);
    else if (is_bc) rc = run_bidi_suite(true, "bidi_char", data, len);
    else if (is_n) rc = run_norm_suite(data, len);
    else if (is_cf) rc = run_casefold_suite(data, len);
    else if (is_pr) rc = run_props_suite(data, len);

    im_free(data);
    return rc;
}
