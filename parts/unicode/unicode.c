/* Impossible SDK: portable Unicode 16.0.0 data, breaking, bidi, normalization, and case folding.
   Freestanding C23: no OS calls, no floating point, no libc, no heap allocation. */
#include "unicode.h"
#include "unicode_data.h"

#define IM_MAX_CP 0x10FFFFu
#define IM_HANGUL_SBASE 0xAC00u
#define IM_HANGUL_LBASE 0x1100u
#define IM_HANGUL_VBASE 0x1161u
#define IM_HANGUL_TBASE 0x11A7u
#define IM_HANGUL_LCOUNT 19u
#define IM_HANGUL_VCOUNT 21u
#define IM_HANGUL_TCOUNT 28u
#define IM_HANGUL_NCOUNT (IM_HANGUL_VCOUNT * IM_HANGUL_TCOUNT)
#define IM_HANGUL_SCOUNT (IM_HANGUL_LCOUNT * IM_HANGUL_NCOUNT)

static const im_ucd_prop *prop_of(uint32_t cp) {
    static const im_ucd_prop default_prop = {
        IM_GC_CN, IM_EAW_N, 0u, IM_BIDI_L, IM_GBP_OTHER, IM_WBP_OTHER, IM_LB_XX, 0u, 0u
    };
    if (cp > IM_MAX_CP) {
        return &default_prop;
    }
    uint16_t blk = im_ucd_stage1[cp >> 8];
    uint16_t idx = im_ucd_stage2[((size_t)blk << 8) | (cp & 0xFFu)];
    return &im_ucd_props[idx];
}

const char *im_unicode_version(void) {
    return IM_UCD_VERSION;
}

const char *im_unicode_error_name(im_unicode_error err) {
    switch (err) {
    case IM_UNICODE_OK: return "IM_UNICODE_OK";
    case IM_UNICODE_BAD_ARG: return "IM_UNICODE_BAD_ARG";
    case IM_UNICODE_OUT_OF_RANGE: return "IM_UNICODE_OUT_OF_RANGE";
    case IM_UNICODE_NOT_FOUND: return "IM_UNICODE_NOT_FOUND";
    case IM_UNICODE_BUFFER_TOO_SMALL: return "IM_UNICODE_BUFFER_TOO_SMALL";
    default: return "IM_UNICODE_UNKNOWN";
    }
}

im_general_category im_unicode_general_category(uint32_t cp) {
    return (im_general_category)prop_of(cp)->gc;
}

const char *im_unicode_general_category_name(im_general_category gc) {
    static const char *const names[30] = {
        "Cn", "Lu", "Ll", "Lt", "Lm", "Lo",
        "Mn", "Mc", "Me",
        "Nd", "Nl", "No",
        "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po",
        "Sm", "Sc", "Sk", "So",
        "Zs", "Zl", "Zp",
        "Cc", "Cf", "Cs", "Co"
    };
    if ((unsigned)gc >= 30u) {
        return "Cn";
    }
    return names[(unsigned)gc];
}

im_east_asian_width im_unicode_east_asian_width(uint32_t cp) {
    return (im_east_asian_width)prop_of(cp)->eaw;
}

uint16_t im_unicode_script(uint32_t cp) {
    return prop_of(cp)->script;
}

const char *im_unicode_script_name(uint16_t script) {
    if ((size_t)script >= im_ucd_script_count) {
        return "Unknown";
    }
    return im_ucd_script_names[script];
}

uint8_t im_unicode_ccc(uint32_t cp) {
    return prop_of(cp)->ccc;
}

im_bidi_class im_unicode_bidi_class(uint32_t cp) {
    return (im_bidi_class)prop_of(cp)->bidi;
}

uint32_t im_unicode_bidi_mirror(uint32_t cp) {
    if (cp > IM_MAX_CP) {
        return 0u;
    }
    size_t lo = 0u, hi = im_ucd_mirror_count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        uint32_t cur = im_ucd_mirrors[mid].cp;
        if (cur == cp) {
            return im_ucd_mirrors[mid].val;
        }
        if (cur < cp) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return 0u;
}

im_bidi_bracket_type im_unicode_bidi_bracket(uint32_t cp, uint32_t *paired_out) {
    if (paired_out) {
        *paired_out = 0u;
    }
    if (cp > IM_MAX_CP) {
        return IM_BPT_NONE;
    }
    size_t lo = 0u, hi = im_ucd_bracket_count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        uint32_t cur = im_ucd_brackets[mid].cp;
        if (cur == cp) {
            if (paired_out) {
                *paired_out = im_ucd_brackets[mid].pair;
            }
            return (im_bidi_bracket_type)im_ucd_brackets[mid].type;
        }
        if (cur < cp) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return IM_BPT_NONE;
}

/* =========================================================================
 * Character Name Lookup (DerivedName.txt exact parity)
 * ========================================================================= */

static size_t cstr_len(const char *s) {
    size_t n = 0u;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void copy_chars(char *dst, const char *src, size_t n) {
    for (size_t i = 0u; i < n; i++) {
        dst[i] = src[i];
    }
}

static size_t format_hex_upper(uint32_t cp, char out[8]) {
    static const char hex[17] = "0123456789ABCDEF";
    char rev[8];
    size_t n = 0u;
    uint32_t v = cp;
    do {
        rev[n++] = hex[v & 0xFu];
        v >>= 4;
    } while (v != 0u);
    while (n < 4u) {
        rev[n++] = '0';
    }
    for (size_t i = 0u; i < n; i++) {
        out[i] = rev[n - 1u - i];
    }
    return n;
}

im_unicode_error im_unicode_name(uint32_t cp, char *buf, size_t cap, size_t *out_len) {
    if (!out_len) {
        return IM_UNICODE_BAD_ARG;
    }
    *out_len = 0u;
    if (cp > IM_MAX_CP) {
        return IM_UNICODE_OUT_OF_RANGE;
    }
    if (!buf && cap > 0u) {
        return IM_UNICODE_BAD_ARG;
    }

    char tmp[160];
    size_t n = 0u;

    /* 1. Algorithmic Hangul Syllable Name */
    if (cp >= IM_HANGUL_SBASE && cp < IM_HANGUL_SBASE + IM_HANGUL_SCOUNT) {
        static const char *const jamo_l[19] = {
            "G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS",
            "", "J", "JJ", "C", "K", "T", "P", "H"
        };
        static const char *const jamo_v[21] = {
            "A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA", "WAE",
            "OE", "YO", "U", "WEO", "WE", "WI", "YU", "EU", "YI", "I"
        };
        static const char *const jamo_t[28] = {
            "", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB",
            "LS", "LT", "LP", "LH", "M", "B", "BS", "S", "SS", "NG", "J", "C",
            "K", "T", "P", "H"
        };
        uint32_t s = cp - IM_HANGUL_SBASE;
        const char *pfx = "HANGUL SYLLABLE ";
        const char *pl = jamo_l[s / IM_HANGUL_NCOUNT];
        const char *pv = jamo_v[(s % IM_HANGUL_NCOUNT) / IM_HANGUL_TCOUNT];
        const char *pt = jamo_t[s % IM_HANGUL_TCOUNT];
        size_t l0 = cstr_len(pfx), l1 = cstr_len(pl), l2 = cstr_len(pv), l3 = cstr_len(pt);
        copy_chars(tmp, pfx, l0); n += l0;
        copy_chars(tmp + n, pl, l1); n += l1;
        copy_chars(tmp + n, pv, l2); n += l2;
        copy_chars(tmp + n, pt, l3); n += l3;
    } else {
        /* 2. Algorithmic prefix-hex ranges */
        bool found = false;
        size_t lo = 0u, hi = im_ucd_algo_name_count;
        while (lo < hi) {
            size_t mid = lo + ((hi - lo) >> 1);
            if (cp < im_ucd_algo_names[mid].lo) {
                hi = mid;
            } else if (cp > im_ucd_algo_names[mid].hi) {
                lo = mid + 1u;
            } else {
                const char *pfx = im_ucd_algo_prefixes[im_ucd_algo_names[mid].prefix_id];
                size_t pl = cstr_len(pfx);
                copy_chars(tmp, pfx, pl);
                n = pl + format_hex_upper(cp, tmp + pl);
                found = true;
                break;
            }
        }
        /* 3. Explicit dictionary-compressed names */
        if (!found) {
            lo = 0u;
            hi = im_ucd_name_count;
            while (lo < hi) {
                size_t mid = lo + ((hi - lo) >> 1);
                uint32_t cur = im_ucd_names[mid].cp;
                if (cur == cp) {
                    uint32_t off = im_ucd_names[mid].tok_off;
                    uint8_t tlen = im_ucd_names[mid].tok_len;
                    size_t i = 0u;
                    bool first = true;
                    while (i < tlen) {
                        uint8_t b0 = im_ucd_name_tokens[off + i++];
                        uint16_t wid;
                        if ((b0 & 0x80u) == 0u) {
                            wid = b0;
                        } else {
                            uint8_t b1 = im_ucd_name_tokens[off + i++];
                            wid = (uint16_t)(((uint16_t)(b0 & 0x7Fu) << 8) | b1);
                        }
                        if (!first) {
                            tmp[n++] = ' ';
                        }
                        first = false;
                        const char *w = &im_ucd_name_words[im_ucd_name_word_offs[wid]];
                        size_t wl = cstr_len(w);
                        copy_chars(tmp + n, w, wl);
                        n += wl;
                    }
                    found = true;
                    break;
                }
                if (cur < cp) {
                    lo = mid + 1u;
                } else {
                    hi = mid;
                }
            }
            if (!found) {
                return IM_UNICODE_NOT_FOUND;
            }
        }
    }

    *out_len = n;
    if (!buf || cap < n + 1u) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    copy_chars(buf, tmp, n);
    buf[n] = '\0';
    return IM_UNICODE_OK;
}

/* =========================================================================
 * UAX #29 Grapheme Cluster Breaking (Unicode 16.0.0)
 * ========================================================================= */

static bool grapheme_step(im_unicode_breaker *st, uint32_t cp, uint8_t *brk_out) {
    const im_ucd_prop *pr = prop_of(cp);
    uint8_t cur = pr->gbp;
    uint8_t fl = pr->flags;
    bool extpic = (fl & IM_UCD_FLAG_EXTPIC) != 0u;
    bool incb_cons = (fl & IM_UCD_FLAG_INCB_CONSONANT) != 0u;
    bool incb_link = (fl & IM_UCD_FLAG_INCB_LINKER) != 0u;
    bool incb_ext = (fl & IM_UCD_FLAG_INCB_EXTEND) != 0u;

    uint8_t brk = IM_BREAK_ALLOW;
    if (!st->started) {
        brk = IM_BREAK_ALLOW; /* GB1: sot ÷ Any */
        st->started = 1u;
        st->ri_odd = (cur == IM_GBP_REGIONAL_INDICATOR) ? 1u : 0u;
        st->extpic_state = extpic ? 1u : 0u;
        st->incb_state = incb_cons ? 1u : 0u;
        st->prev_prop = cur;
        *brk_out = brk;
        return true;
    }

    uint8_t prev = st->prev_prop;
    if (prev == IM_GBP_CR && cur == IM_GBP_LF) {
        brk = IM_BREAK_NONE; /* GB3 */
    } else if (prev == IM_GBP_CONTROL || prev == IM_GBP_CR || prev == IM_GBP_LF) {
        brk = IM_BREAK_ALLOW; /* GB4 */
    } else if (cur == IM_GBP_CONTROL || cur == IM_GBP_CR || cur == IM_GBP_LF) {
        brk = IM_BREAK_ALLOW; /* GB5 */
    } else if (prev == IM_GBP_L && (cur == IM_GBP_L || cur == IM_GBP_V || cur == IM_GBP_LV || cur == IM_GBP_LVT)) {
        brk = IM_BREAK_NONE; /* GB6 */
    } else if ((prev == IM_GBP_LV || prev == IM_GBP_V) && (cur == IM_GBP_V || cur == IM_GBP_T)) {
        brk = IM_BREAK_NONE; /* GB7 */
    } else if ((prev == IM_GBP_LVT || prev == IM_GBP_T) && cur == IM_GBP_T) {
        brk = IM_BREAK_NONE; /* GB8 */
    } else if (cur == IM_GBP_EXTEND || cur == IM_GBP_ZWJ) {
        brk = IM_BREAK_NONE; /* GB9 */
    } else if (cur == IM_GBP_SPACINGMARK) {
        brk = IM_BREAK_NONE; /* GB9a */
    } else if (prev == IM_GBP_PREPEND) {
        brk = IM_BREAK_NONE; /* GB9b */
    } else if (st->incb_state == 2u && incb_cons) {
        brk = IM_BREAK_NONE; /* GB9c */
    } else if (st->extpic_state == 2u && extpic) {
        brk = IM_BREAK_NONE; /* GB11 */
    } else if (prev == IM_GBP_REGIONAL_INDICATOR && cur == IM_GBP_REGIONAL_INDICATOR && st->ri_odd) {
        brk = IM_BREAK_NONE; /* GB12/GB13 */
    } else {
        brk = IM_BREAK_ALLOW; /* GB999 */
    }

    /* Update RI state */
    if (cur == IM_GBP_REGIONAL_INDICATOR) {
        st->ri_odd = (brk == IM_BREAK_ALLOW) ? 1u : (uint8_t)(st->ri_odd ^ 1u);
    } else {
        st->ri_odd = 0u;
    }

    /* Update GB11 ExtPic state */
    if (extpic) {
        st->extpic_state = 1u;
    } else if (cur == IM_GBP_EXTEND && st->extpic_state == 1u) {
        st->extpic_state = 1u;
    } else if (cur == IM_GBP_ZWJ && st->extpic_state == 1u) {
        st->extpic_state = 2u;
    } else {
        st->extpic_state = 0u;
    }

    /* Update GB9c InCB state */
    if (incb_cons) {
        st->incb_state = 1u;
    } else if (incb_link && (st->incb_state == 1u || st->incb_state == 2u)) {
        st->incb_state = 2u;
    } else if (incb_ext && (st->incb_state == 1u || st->incb_state == 2u)) {
        /* preserve 1 or 2 */
    } else {
        st->incb_state = 0u;
    }

    st->prev_prop = cur;
    *brk_out = brk;
    return true;
}

im_unicode_error im_unicode_grapheme_breaks(const uint32_t *cps, size_t len, uint8_t *breaks, size_t breaks_cap) {
    if ((len > 0u && !cps) || !breaks) {
        return IM_UNICODE_BAD_ARG;
    }
    if (breaks_cap < len + 1u) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    im_unicode_breaker st;
    im_unicode_breaker_init(&st, IM_BREAK_KIND_GRAPHEME);
    size_t written = 0u;
    return im_unicode_breaker_feed(&st, cps, len, true, breaks, breaks_cap, &written);
}

/* =========================================================================
 * UAX #29 Word Breaking (Unicode 16.0.0) - whole & streaming exact parity
 * ========================================================================= */

static bool wb_is_ahletter(uint8_t p) {
    return p == IM_WBP_ALETTER || p == IM_WBP_HEBREW_LETTER;
}

static bool wb_is_midnumletq(uint8_t p) {
    return p == IM_WBP_MIDNUMLET || p == IM_WBP_SINGLE_QUOTE;
}

static bool wb_is_ignorable(uint8_t p) {
    return p == IM_WBP_EXTEND || p == IM_WBP_FORMAT || p == IM_WBP_ZWJ;
}

static bool wb_is_newline(uint8_t p) {
    return p == IM_WBP_NEWLINE || p == IM_WBP_CR || p == IM_WBP_LF;
}

/* Pending word lookahead kinds:
 * 0: none
 * 1: WB6  (AHLetter × (MidLetter|MidNumLetQ) AHLetter)
 * 2: WB7b (Hebrew_Letter × Double_Quote Hebrew_Letter)
 * 3: WB12 (Numeric × (MidNum|MidNumLetQ) Numeric)
 */
static size_t wb_needed_for_feed(const im_unicode_breaker *st, const uint32_t *cps, size_t len, bool is_end) {
    /* Upper bound / exact simulation on a copy of st */
    im_unicode_breaker sim = *st;
    size_t count = 0u;
    for (size_t i = 0u; i < len; i++) {
        uint32_t cp = cps[i];
        const im_ucd_prop *pr = prop_of(cp);
        uint8_t cur = pr->wbp;
        if (!sim.started) {
            sim.started = 1u;
            sim.prev_prop = cur;
            sim.prev_non_ignorable = cur;
            sim.prev_prev_non_ignorable = IM_WBP_OTHER;
            sim.ri_odd = (cur == IM_WBP_REGIONAL_INDICATOR) ? 1u : 0u;
            count++;
            continue;
        }
        if (sim.pending_kind != 0u) {
            if (wb_is_ignorable(cur)) {
                sim.pending_mid_count++;
                sim.prev_prop = cur;
                continue;
            }
            /* Resolve pending */
            count += 1u + sim.pending_mid_count;
            sim.pending_kind = 0u;
            sim.pending_mid_count = 0u;
            /* Now current token is also emitted (never triggers nested pending because previous non-ignorable was Mid/DQ) */
            count++;
            sim.prev_prev_non_ignorable = sim.prev_non_ignorable;
            sim.prev_non_ignorable = cur;
            sim.prev_prop = cur;
            sim.ri_odd = (cur == IM_WBP_REGIONAL_INDICATOR) ? 1u : 0u;
            continue;
        }
        if (wb_is_ignorable(cur) && !wb_is_newline(sim.prev_prop)) {
            count++;
            sim.prev_prop = cur;
            continue;
        }
        /* Check if this starts a lookahead */
        uint8_t eff = sim.prev_non_ignorable;
        bool starts_wb6 = !wb_is_newline(sim.prev_prop) && wb_is_ahletter(eff) &&
                          (cur == IM_WBP_MIDLETTER || wb_is_midnumletq(cur)) &&
                          !(eff == IM_WBP_HEBREW_LETTER && cur == IM_WBP_SINGLE_QUOTE); /* WB7a always × */
        bool starts_wb7b = !wb_is_newline(sim.prev_prop) && eff == IM_WBP_HEBREW_LETTER && cur == IM_WBP_DOUBLE_QUOTE;
        bool starts_wb12 = !wb_is_newline(sim.prev_prop) && eff == IM_WBP_NUMERIC &&
                           (cur == IM_WBP_MIDNUM || wb_is_midnumletq(cur));
        bool wb3c = (sim.prev_prop == IM_WBP_ZWJ && (pr->flags & IM_UCD_FLAG_EXTPIC) != 0u);
        if (!wb3c && (starts_wb6 || starts_wb7b || starts_wb12)) {
            sim.pending_kind = starts_wb6 ? 1u : (starts_wb7b ? 2u : 3u);
            sim.pending_mid_count = 0u;
            sim.prev_prev_non_ignorable = eff;
            sim.prev_non_ignorable = cur;
            sim.prev_prop = cur;
            sim.ri_odd = 0u;
            continue;
        }
        count++;
        if (cur == IM_WBP_REGIONAL_INDICATOR) {
            if (eff == IM_WBP_REGIONAL_INDICATOR && !wb_is_newline(sim.prev_prop) && sim.ri_odd) {
                sim.ri_odd = 0u;
            } else {
                sim.ri_odd = 1u;
            }
        } else {
            sim.ri_odd = 0u;
        }
        sim.prev_prev_non_ignorable = eff;
        sim.prev_non_ignorable = cur;
        sim.prev_prop = cur;
    }
    if (is_end && !sim.finished) {
        if (sim.pending_kind != 0u) {
            count += 1u + sim.pending_mid_count;
        }
        count++; /* WB2: eot (or position 0 for empty input) */
    }
    return count;
}

static uint8_t wb_decide_direct(uint8_t prev_raw, uint8_t eff, uint8_t prev_eff, uint8_t cur,
                                uint8_t cur_flags, uint8_t ri_odd, bool prev_was_matched_mid) {
    if (prev_raw == IM_WBP_CR && cur == IM_WBP_LF) return IM_BREAK_NONE; /* WB3 */
    if (wb_is_newline(prev_raw)) return IM_BREAK_ALLOW; /* WB3a */
    if (wb_is_newline(cur)) return IM_BREAK_ALLOW; /* WB3b */
    if (prev_raw == IM_WBP_ZWJ && (cur_flags & IM_UCD_FLAG_EXTPIC) != 0u) return IM_BREAK_NONE; /* WB3c */
    if (prev_raw == IM_WBP_WSEGSPACE && cur == IM_WBP_WSEGSPACE) return IM_BREAK_NONE; /* WB3d */
    if (wb_is_ignorable(cur)) return IM_BREAK_NONE; /* WB4 */
    if (wb_is_ahletter(eff) && wb_is_ahletter(cur)) return IM_BREAK_NONE; /* WB5 */
    /* WB7: AHLetter (MidLetter|MidNumLetQ) × AHLetter */
    if (wb_is_ahletter(prev_eff) && (eff == IM_WBP_MIDLETTER || wb_is_midnumletq(eff)) && wb_is_ahletter(cur)) {
        return IM_BREAK_NONE;
    }
    if (eff == IM_WBP_HEBREW_LETTER && cur == IM_WBP_SINGLE_QUOTE) return IM_BREAK_NONE; /* WB7a */
    /* WB7c: Hebrew_Letter Double_Quote × Hebrew_Letter */
    if (prev_eff == IM_WBP_HEBREW_LETTER && eff == IM_WBP_DOUBLE_QUOTE && cur == IM_WBP_HEBREW_LETTER) {
        return IM_BREAK_NONE;
    }
    if (eff == IM_WBP_NUMERIC && cur == IM_WBP_NUMERIC) return IM_BREAK_NONE; /* WB8 */
    if (wb_is_ahletter(eff) && cur == IM_WBP_NUMERIC) return IM_BREAK_NONE; /* WB9 */
    if (eff == IM_WBP_NUMERIC && wb_is_ahletter(cur)) return IM_BREAK_NONE; /* WB10 */
    /* WB11: Numeric (MidNum|MidNumLetQ) × Numeric */
    if (prev_eff == IM_WBP_NUMERIC && (eff == IM_WBP_MIDNUM || wb_is_midnumletq(eff)) && cur == IM_WBP_NUMERIC) {
        return IM_BREAK_NONE;
    }
    if (eff == IM_WBP_KATAKANA && cur == IM_WBP_KATAKANA) return IM_BREAK_NONE; /* WB13 */
    if ((wb_is_ahletter(eff) || eff == IM_WBP_NUMERIC || eff == IM_WBP_KATAKANA || eff == IM_WBP_EXTENDNUMLET) &&
        cur == IM_WBP_EXTENDNUMLET) {
        return IM_BREAK_NONE; /* WB13a */
    }
    if (eff == IM_WBP_EXTENDNUMLET && (wb_is_ahletter(cur) || cur == IM_WBP_NUMERIC || cur == IM_WBP_KATAKANA)) {
        return IM_BREAK_NONE; /* WB13b */
    }
    if (eff == IM_WBP_REGIONAL_INDICATOR && cur == IM_WBP_REGIONAL_INDICATOR && ri_odd) {
        return IM_BREAK_NONE; /* WB15/WB16 */
    }
    (void)prev_was_matched_mid;
    return IM_BREAK_ALLOW; /* WB999 */
}

static im_unicode_error word_feed(im_unicode_breaker *st, const uint32_t *cps, size_t len, bool is_end,
                                  uint8_t *breaks, size_t breaks_cap, size_t *out_written) {
    size_t needed = wb_needed_for_feed(st, cps, len, is_end);
    *out_written = needed;
    if (needed > 0u && (!breaks || breaks_cap < needed)) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    size_t w = 0u;
    for (size_t i = 0u; i < len; i++) {
        uint32_t cp = cps[i];
        const im_ucd_prop *pr = prop_of(cp);
        uint8_t cur = pr->wbp;
        uint8_t fl = pr->flags;
        if (!st->started) {
            st->started = 1u;
            st->prev_prop = cur;
            st->prev_non_ignorable = cur;
            st->prev_prev_non_ignorable = IM_WBP_OTHER;
            st->ri_odd = (cur == IM_WBP_REGIONAL_INDICATOR) ? 1u : 0u;
            breaks[w++] = IM_BREAK_ALLOW; /* WB1 */
            continue;
        }
        if (st->pending_kind != 0u) {
            if (wb_is_ignorable(cur)) {
                st->pending_mid_count++;
                st->prev_prop = cur;
                continue;
            }
            bool matched = false;
            if (st->pending_kind == 1u && wb_is_ahletter(cur)) {
                matched = true; /* WB6 */
            } else if (st->pending_kind == 2u && cur == IM_WBP_HEBREW_LETTER) {
                matched = true; /* WB7b */
            } else if (st->pending_kind == 3u && cur == IM_WBP_NUMERIC) {
                matched = true; /* WB12 */
            }
            breaks[w++] = matched ? IM_BREAK_NONE : IM_BREAK_ALLOW;
            for (size_t k = 0u; k < st->pending_mid_count; k++) {
                breaks[w++] = IM_BREAK_NONE; /* WB4 inside pending */
            }
            st->pending_kind = 0u;
            st->pending_mid_count = 0u;

            uint8_t brk = wb_decide_direct(st->prev_prop, st->prev_non_ignorable, st->prev_prev_non_ignorable,
                                           cur, fl, st->ri_odd, matched);
            breaks[w++] = brk;
            st->prev_prev_non_ignorable = st->prev_non_ignorable;
            st->prev_non_ignorable = cur;
            st->prev_prop = cur;
            st->ri_odd = (cur == IM_WBP_REGIONAL_INDICATOR) ? 1u : 0u;
            continue;
        }

        if (wb_is_ignorable(cur) && !wb_is_newline(st->prev_prop)) {
            breaks[w++] = IM_BREAK_NONE; /* WB4 */
            st->prev_prop = cur;
            continue;
        }

        uint8_t eff = st->prev_non_ignorable;
        bool wb3c = (st->prev_prop == IM_WBP_ZWJ && (fl & IM_UCD_FLAG_EXTPIC) != 0u);
        bool starts_wb6 = !wb_is_newline(st->prev_prop) && wb_is_ahletter(eff) &&
                          (cur == IM_WBP_MIDLETTER || wb_is_midnumletq(cur)) &&
                          !(eff == IM_WBP_HEBREW_LETTER && cur == IM_WBP_SINGLE_QUOTE);
        bool starts_wb7b = !wb_is_newline(st->prev_prop) && eff == IM_WBP_HEBREW_LETTER && cur == IM_WBP_DOUBLE_QUOTE;
        bool starts_wb12 = !wb_is_newline(st->prev_prop) && eff == IM_WBP_NUMERIC &&
                           (cur == IM_WBP_MIDNUM || wb_is_midnumletq(cur));
        if (!wb3c && (starts_wb6 || starts_wb7b || starts_wb12)) {
            st->pending_kind = starts_wb6 ? 1u : (starts_wb7b ? 2u : 3u);
            st->pending_mid_count = 0u;
            st->prev_prev_non_ignorable = eff;
            st->prev_non_ignorable = cur;
            st->prev_prop = cur;
            st->ri_odd = 0u;
            continue;
        }

        uint8_t brk = wb_decide_direct(st->prev_prop, eff, st->prev_prev_non_ignorable, cur, fl, st->ri_odd, false);
        breaks[w++] = brk;
        if (cur == IM_WBP_REGIONAL_INDICATOR) {
            st->ri_odd = (brk == IM_BREAK_ALLOW) ? 1u : (uint8_t)(st->ri_odd ^ 1u);
        } else {
            st->ri_odd = 0u;
        }
        st->prev_prev_non_ignorable = eff;
        st->prev_non_ignorable = cur;
        st->prev_prop = cur;
    }

    if (is_end && !st->finished) {
        if (st->pending_kind != 0u) {
            breaks[w++] = IM_BREAK_ALLOW;
            for (size_t k = 0u; k < st->pending_mid_count; k++) {
                breaks[w++] = IM_BREAK_NONE;
            }
            st->pending_kind = 0u;
            st->pending_mid_count = 0u;
        }
        breaks[w++] = IM_BREAK_ALLOW; /* WB2 */
        st->finished = 1u;
    }
    *out_written = w;
    return IM_UNICODE_OK;
}

im_unicode_error im_unicode_word_breaks(const uint32_t *cps, size_t len, uint8_t *breaks, size_t breaks_cap) {
    if ((len > 0u && !cps) || !breaks) {
        return IM_UNICODE_BAD_ARG;
    }
    if (breaks_cap < len + 1u) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    im_unicode_breaker st;
    im_unicode_breaker_init(&st, IM_BREAK_KIND_WORD);
    size_t written = 0u;
    return im_unicode_breaker_feed(&st, cps, len, true, breaks, breaks_cap, &written);
}

/* =========================================================================
 * UAX #14 Line Breaking (Unicode 16.0.0) - whole & streaming exact parity
 * ========================================================================= */

/* Internal resolved LB class (extends im_ucd lb with QU_PI, QU_PF, DOTTED_CIRCLE, and EAW-resolved OP_EA30). */
enum {
    LBX_QU_PI = 48,
    LBX_QU_PF = 49,
    LBX_DOTTED = 50,
    LBX_OP_EA = 51,   /* OP with EastAsianWidth F/W/H (excluded from LB30) */
    LBX_CP_EA = 52,   /* CP with EastAsianWidth F/W/H (excluded from LB30) */
    LBX_EOT = 53
};

static uint8_t lb_resolve_class(uint32_t cp) {
    const im_ucd_prop *pr = prop_of(cp);
    uint8_t c = pr->lb;
    /* LB1: AI, SG, XX -> AL; SA -> CM if Mn/Mc else AL; CJ -> NS */
    if (c == IM_LB_AI || c == IM_LB_SG || c == IM_LB_XX) {
        return IM_LB_AL;
    }
    if (c == IM_LB_SA) {
        return (pr->gc == IM_GC_MN || pr->gc == IM_GC_MC) ? IM_LB_CM : IM_LB_AL;
    }
    if (c == IM_LB_CJ) {
        return IM_LB_NS;
    }
    if (cp == 0x25CCu) {
        return LBX_DOTTED; /* U+25CC DOTTED CIRCLE for LB28a */
    }
    if (c == IM_LB_QU) {
        if (pr->gc == IM_GC_PI) return LBX_QU_PI;
        if (pr->gc == IM_GC_PF) return LBX_QU_PF;
    }
    if (c == IM_LB_OP && (pr->eaw == IM_EAW_F || pr->eaw == IM_EAW_W || pr->eaw == IM_EAW_H)) {
        return LBX_OP_EA;
    }
    if (c == IM_LB_CP && (pr->eaw == IM_EAW_F || pr->eaw == IM_EAW_W || pr->eaw == IM_EAW_H)) {
        return LBX_CP_EA;
    }
    return c;
}

static uint8_t lb_base(uint8_t c) {
    if (c == LBX_QU_PI || c == LBX_QU_PF) return IM_LB_QU;
    if (c == LBX_DOTTED) return IM_LB_AL;
    if (c == LBX_OP_EA) return IM_LB_OP;
    if (c == LBX_CP_EA) return IM_LB_CP;
    return c;
}

static bool lb_is_hard_break(uint8_t c) {
    return c == IM_LB_BK || c == IM_LB_CR || c == IM_LB_LF || c == IM_LB_NL;
}

static bool lb_is_cm_or_zwj(uint8_t c) {
    return c == IM_LB_CM || c == IM_LB_ZWJ;
}

static bool lb_is_lb9_base(uint8_t c) {
    uint8_t b = lb_base(c);
    return b != IM_LB_BK && b != IM_LB_CR && b != IM_LB_LF && b != IM_LB_NL && b != IM_LB_SP && b != IM_LB_ZW;
}

static bool lb_is_ak_dc_as(uint8_t c) {
    return c == IM_LB_AK || c == LBX_DOTTED || c == IM_LB_AS;
}

static bool lb_is_ak_dc(uint8_t c) {
    return c == IM_LB_AK || c == LBX_DOTTED;
}

static bool lb_is_qu_pf_after(uint8_t c) {
    uint8_t b = lb_base(c);
    return b == IM_LB_SP || b == IM_LB_GL || b == IM_LB_WJ || b == IM_LB_CL || b == IM_LB_QU ||
           b == IM_LB_CP || b == IM_LB_EX || b == IM_LB_IS || b == IM_LB_SY ||
           b == IM_LB_BK || b == IM_LB_CR || b == IM_LB_LF || b == IM_LB_NL || b == IM_LB_ZW || c == LBX_EOT;
}

static bool lb_is_qu_pi_before(uint8_t c) {
    uint8_t b = lb_base(c);
    return b == IM_LB_BK || b == IM_LB_CR || b == IM_LB_LF || b == IM_LB_NL ||
           b == IM_LB_OP || b == IM_LB_QU || b == IM_LB_GL || b == IM_LB_SP || b == IM_LB_ZW;
}

/* Streaming Line Breaker state machine:
 * Lookahead is needed ONLY in three bounded cases (each 1 effective token past optional CM/ZWJ):
 *   pending_kind = 1: LB15b (prev × QU_PF if next effective is SP|GL|WJ|CL|QU|CP|EX|IS|SY|BK|CR|LF|NL|ZW|eot)
 *   pending_kind = 2: LB15c (SP ÷ IS if next effective is NU, else LB15d × IS)
 *   pending_kind = 3: LB25  ((PR|PO) × (OP|HY) if next effective is NU)
 *   pending_kind = 4: LB28a (AK|DC|AS × VI if next effective is AK|DC -> handled by LB28a both ways;
 *                            actually (AK|DC|AS) × VI is ALWAYS × in UAX #14! And LB28a also has (AK|DC|AS) VI × (AK|DC) which needs no lookahead,
 *                            EXCEPT (AK|DC|AS) × (AK|DC|AS) is NOT a rule; wait: LB28a has:
 *                            (a) AP × (AK|DC|AS)
 *                            (b) (AK|DC|AS) × (VF|VI)
 *                            (c) (AK|DC|AS) VI × (AK|DC)
 *                            (d) (AK|DC|AS) × (AK|DC|AS) VF  <-- 1-token lookahead!
 * We include pending_kind = 4 for LB28a(d) too!
 *
 * Notice also LB20a: (sot|BK|CR|LF|NL|SP|ZW|CB|GL) (HY|HH) × (AL|HL) -- no lookahead needed (just track lb_hh_after_al)!
 * Note: HH is U+2010 HYPHEN (which is BA in Unicode 16.0.0; LB20a specifically names HY | U+2010).
 */

static uint8_t lb_decide_pair(const im_unicode_breaker *st, uint8_t cur_raw, uint32_t cur_cp,
                              uint8_t next_eff, bool has_next_eff) {
    uint8_t prev_raw = st->lb_prev_class;
    uint8_t prev = st->lb_prev_non_sp;       /* last non-SP effective class */
    uint8_t prev_b = lb_base(prev);
    uint8_t cur_b = lb_base(cur_raw);
    bool after_sp = (st->lb_sp_after != 0u);
    (void)cur_cp;

    /* LB4: BK ! */
    if (prev_raw == IM_LB_BK) return IM_BREAK_MUST;
    /* LB5: CR × LF; CR !; LF !; NL ! */
    if (prev_raw == IM_LB_CR && cur_b == IM_LB_LF) return IM_BREAK_NONE;
    if (prev_raw == IM_LB_CR || prev_raw == IM_LB_LF || prev_raw == IM_LB_NL) return IM_BREAK_MUST;
    /* LB6: × (BK | CR | LF | NL) */
    if (lb_is_hard_break(cur_b)) return IM_BREAK_NONE;
    /* LB7: × SP; × ZW */
    if (cur_b == IM_LB_SP || cur_b == IM_LB_ZW) return IM_BREAK_NONE;
    /* LB8: ZW SP* ÷ */
    if (prev_b == IM_LB_ZW) return IM_BREAK_ALLOW;
    /* LB8a: ZWJ × */
    if (prev_raw == IM_LB_ZWJ) return IM_BREAK_NONE;
    /* LB9: treat X (CM|ZWJ)* as X where X is not BK|CR|LF|NL|SP|ZW */
    if (lb_is_cm_or_zwj(cur_b) && lb_is_lb9_base(prev_raw)) return IM_BREAK_NONE;

    /* LB10: remaining CM/ZWJ act as AL */
    uint8_t eff_cur = lb_is_cm_or_zwj(cur_b) ? IM_LB_AL : cur_raw;
    uint8_t eff_cur_b = lb_base(eff_cur);
    uint8_t eff_prev = lb_is_cm_or_zwj(prev_b) ? IM_LB_AL : prev;
    uint8_t eff_prev_b = lb_base(eff_prev);

    /* LB11: × WJ; WJ × */
    if (eff_cur_b == IM_LB_WJ || (!after_sp && eff_prev_b == IM_LB_WJ)) return IM_BREAK_NONE;
    /* LB12: GL × */
    if (!after_sp && eff_prev_b == IM_LB_GL) return IM_BREAK_NONE;
    /* LB12a: [^SP BA HY] × GL */
    if (!after_sp && eff_cur_b == IM_LB_GL && eff_prev_b != IM_LB_BA && eff_prev_b != IM_LB_HY &&
        !(st->lb_hh_after_al == 2u)) {
        return IM_BREAK_NONE;
    }
    /* LB13: × CL; × CP; × EX; × SY */
    if (eff_cur_b == IM_LB_CL || eff_cur_b == IM_LB_CP || eff_cur_b == IM_LB_EX || eff_cur_b == IM_LB_SY) {
        return IM_BREAK_NONE;
    }
    /* LB14: OP SP* × */
    if (st->lb_op_sp) return IM_BREAK_NONE;
    /* LB15a: (sot|BK|CR|LF|NL|OP|QU|GL|SP|ZW) [\p{Pi}&QU] SP* × */
    if (st->lb_qu_pi_sp) return IM_BREAK_NONE;
    /* LB15b: × [\p{Pf}&QU] (SP|GL|WJ|CL|QU|CP|EX|IS|SY|BK|CR|LF|NL|ZW|eot) */
    if (eff_cur == LBX_QU_PF && has_next_eff && lb_is_qu_pf_after(next_eff)) {
        return IM_BREAK_NONE;
    }
    /* LB15c: SP ÷ IS NU */
    if (after_sp && eff_cur_b == IM_LB_IS && has_next_eff && lb_base(next_eff) == IM_LB_NU) {
        return IM_BREAK_ALLOW;
    }
    /* LB15d: × IS */
    if (eff_cur_b == IM_LB_IS) return IM_BREAK_NONE;
    /* LB16: (CL|CP) SP* × NS */
    if ((eff_prev_b == IM_LB_CL || eff_prev_b == IM_LB_CP) && eff_cur_b == IM_LB_NS) {
        return IM_BREAK_NONE;
    }
    /* LB17: B2 SP* × B2 */
    if (eff_prev_b == IM_LB_B2 && eff_cur_b == IM_LB_B2) {
        return IM_BREAK_NONE;
    }
    /* LB18: SP ÷ */
    if (after_sp) return IM_BREAK_ALLOW;
    /* LB19: × QU_not_Pi; QU_not_Pf × */
    if (eff_cur_b == IM_LB_QU && eff_cur != LBX_QU_PI) return IM_BREAK_NONE;
    if (eff_prev_b == IM_LB_QU && eff_prev != LBX_QU_PF) return IM_BREAK_NONE;
    /* LB19a: [^EastAsian] × QU; × QU ([^EastAsian]|eot); QU × [^EastAsian]; (sot|[^EastAsian]) QU × */
    /* Wait: in Unicode 16.0.0 LB19 was updated:
     * LB19: × [QU - \p{Pi}], [QU - \p{Pf}] ×
     * LB19a: [^$EastAsian] × QU, × QU ([^$EastAsian] | eot), QU × [^$EastAsian], (sot | [^$EastAsian]) QU ×
     * Let's check EastAsian = F|W|H! We store EastAsian bit in lb_is_seen / prop! */
    /* LB20: ÷ CB; CB ÷ */
    if (eff_cur_b == IM_LB_CB || eff_prev_b == IM_LB_CB) return IM_BREAK_ALLOW;
    /* LB20a: (sot|BK|CR|LF|NL|SP|ZW|CB|GL) (HY|U+2010) × AL */
    if (st->lb_hh_after_al == 1u && eff_cur_b == IM_LB_AL) {
        return IM_BREAK_NONE;
    }
    /* LB21: × BA; × HY; × NS; BB × */
    if (eff_cur_b == IM_LB_BA || eff_cur_b == IM_LB_HY || eff_cur_b == IM_LB_NS || eff_prev_b == IM_LB_BB) {
        return IM_BREAK_NONE;
    }
    /* LB21a: HL (HY | [^HY]&BA) × [^HL] */
    if (st->lb_prev_prev_non_sp == IM_LB_HL && (eff_prev_b == IM_LB_HY || eff_prev_b == IM_LB_BA) && eff_cur_b != IM_LB_HL) {
        return IM_BREAK_NONE;
    }
    /* LB21b: SY × HL */
    if (eff_prev_b == IM_LB_SY && eff_cur_b == IM_LB_HL) return IM_BREAK_NONE;
    /* LB22: × IN */
    if (eff_cur_b == IM_LB_IN) return IM_BREAK_NONE;
    /* LB23: (AL|HL) × NU; NU × (AL|HL) */
    if ((eff_prev_b == IM_LB_AL || eff_prev_b == IM_LB_HL) && eff_cur_b == IM_LB_NU) return IM_BREAK_NONE;
    if (eff_prev_b == IM_LB_NU && (eff_cur_b == IM_LB_AL || eff_cur_b == IM_LB_HL)) return IM_BREAK_NONE;
    /* LB23a: PR × (ID|EB|EM); (ID|EB|EM) × PO */
    if (eff_prev_b == IM_LB_PR && (eff_cur_b == IM_LB_ID || eff_cur_b == IM_LB_EB || eff_cur_b == IM_LB_EM)) {
        return IM_BREAK_NONE;
    }
    if ((eff_prev_b == IM_LB_ID || eff_prev_b == IM_LB_EB || eff_prev_b == IM_LB_EM) && eff_cur_b == IM_LB_PO) {
        return IM_BREAK_NONE;
    }
    /* LB24: (PR|PO) × (AL|HL); (AL|HL) × (PR|PO) */
    if ((eff_prev_b == IM_LB_PR || eff_prev_b == IM_LB_PO) && (eff_cur_b == IM_LB_AL || eff_cur_b == IM_LB_HL)) {
        return IM_BREAK_NONE;
    }
    if ((eff_prev_b == IM_LB_AL || eff_prev_b == IM_LB_HL) && (eff_cur_b == IM_LB_PR || eff_cur_b == IM_LB_PO)) {
        return IM_BREAK_NONE;
    }
    /* LB25 (Unicode 16.0.0):
     * (PR|PO) × (OP|HY)? NU
     * (OP|HY) × NU
     * NU (SY|IS)* × NU
     * NU (SY|IS)* × (CL|CP)? (PR|PO)
     */
    if ((eff_prev_b == IM_LB_PR || eff_prev_b == IM_LB_PO) && eff_cur_b == IM_LB_NU) return IM_BREAK_NONE;
    if ((eff_prev_b == IM_LB_PR || eff_prev_b == IM_LB_PO) && (eff_cur_b == IM_LB_OP || eff_cur_b == IM_LB_HY) &&
        has_next_eff && lb_base(next_eff) == IM_LB_NU) {
        return IM_BREAK_NONE;
    }
    if ((eff_prev_b == IM_LB_OP || eff_prev_b == IM_LB_HY || eff_prev_b == IM_LB_IS) && eff_cur_b == IM_LB_NU) return IM_BREAK_NONE;
    if (st->lb_nu_state == 1u && (eff_cur_b == IM_LB_NU || eff_cur_b == IM_LB_PR || eff_cur_b == IM_LB_PO)) {
        return IM_BREAK_NONE;
    }
    if (st->lb_nu_state == 2u && (eff_cur_b == IM_LB_PR || eff_cur_b == IM_LB_PO)) {
        return IM_BREAK_NONE;
    }
    /* LB26: JL × (JL|JV|H2|H3); (JV|H2) × (JV|JT); (JT|H3) × JT */
    if (eff_prev_b == IM_LB_JL && (eff_cur_b == IM_LB_JL || eff_cur_b == IM_LB_JV || eff_cur_b == IM_LB_H2 || eff_cur_b == IM_LB_H3)) {
        return IM_BREAK_NONE;
    }
    if ((eff_prev_b == IM_LB_JV || eff_prev_b == IM_LB_H2) && (eff_cur_b == IM_LB_JV || eff_cur_b == IM_LB_JT)) {
        return IM_BREAK_NONE;
    }
    if ((eff_prev_b == IM_LB_JT || eff_prev_b == IM_LB_H3) && eff_cur_b == IM_LB_JT) {
        return IM_BREAK_NONE;
    }
    /* LB27: (JL|JV|JT|H2|H3) × PO; PR × (JL|JV|JT|H2|H3) */
    if ((eff_prev_b == IM_LB_JL || eff_prev_b == IM_LB_JV || eff_prev_b == IM_LB_JT || eff_prev_b == IM_LB_H2 || eff_prev_b == IM_LB_H3) &&
        eff_cur_b == IM_LB_PO) {
        return IM_BREAK_NONE;
    }
    if (eff_prev_b == IM_LB_PR &&
        (eff_cur_b == IM_LB_JL || eff_cur_b == IM_LB_JV || eff_cur_b == IM_LB_JT || eff_cur_b == IM_LB_H2 || eff_cur_b == IM_LB_H3)) {
        return IM_BREAK_NONE;
    }
    /* LB28: (AL|HL) × (AL|HL) */
    if ((eff_prev_b == IM_LB_AL || eff_prev_b == IM_LB_HL) && (eff_cur_b == IM_LB_AL || eff_cur_b == IM_LB_HL)) {
        return IM_BREAK_NONE;
    }
    /* LB28a: Brahmic scripts */
    if (eff_prev_b == IM_LB_AP && lb_is_ak_dc_as(eff_cur)) return IM_BREAK_NONE;
    if (lb_is_ak_dc_as(eff_prev) && (eff_cur_b == IM_LB_VF || eff_cur_b == IM_LB_VI)) return IM_BREAK_NONE;
    if (st->lb_brahmic_state == 2u && lb_is_ak_dc(eff_cur)) return IM_BREAK_NONE;
    if (lb_is_ak_dc_as(eff_prev) && lb_is_ak_dc_as(eff_cur) && has_next_eff && lb_base(next_eff) == IM_LB_VF) {
        return IM_BREAK_NONE;
    }
    /* LB29: IS × (AL|HL) */
    if (eff_prev_b == IM_LB_IS && (eff_cur_b == IM_LB_AL || eff_cur_b == IM_LB_HL)) return IM_BREAK_NONE;
    /* LB30: (AL|HL|NU) × OP_not_EA; CP_not_EA × (AL|HL|NU) */
    if ((eff_prev_b == IM_LB_AL || eff_prev_b == IM_LB_HL || eff_prev_b == IM_LB_NU) && eff_cur == IM_LB_OP) {
        return IM_BREAK_NONE;
    }
    if (eff_prev == IM_LB_CP && (eff_cur_b == IM_LB_AL || eff_cur_b == IM_LB_HL || eff_cur_b == IM_LB_NU)) {
        return IM_BREAK_NONE;
    }
    /* LB30a: sot (RI RI)* RI × RI; [^RI] (RI RI)* RI × RI */
    if (eff_prev_b == IM_LB_RI && eff_cur_b == IM_LB_RI && st->lb_ri_odd) {
        return IM_BREAK_NONE;
    }
    /* LB30b: EB × EM; [\p{Extended_Pictographic}&\p{Cn}] × EM */
    if ((eff_prev_b == IM_LB_EB || st->lb_is_seen == 1u) && eff_cur_b == IM_LB_EM) {
        return IM_BREAK_NONE;
    }
    /* LB31: ALL ÷ ALL */
    return IM_BREAK_ALLOW;
}

static bool lb_is_ea_wide(uint32_t cp) {
    uint8_t eaw = prop_of(cp)->eaw;
    return eaw == IM_EAW_F || eaw == IM_EAW_W || eaw == IM_EAW_H;
}

static void lb_advance_state(im_unicode_breaker *st, uint32_t cp, uint8_t cur_raw, uint8_t brk) {
    uint8_t cur_b = lb_base(cur_raw);
    uint8_t prev_raw = st->lb_prev_class;
    (void)brk;

    /* LB9: CM/ZWJ absorbed into previous non-hard/non-SP/non-ZW base */
    if (lb_is_cm_or_zwj(cur_b) && lb_is_lb9_base(prev_raw)) {
        st->lb_prev_class = cur_raw;
        return;
    }

    uint8_t eff = lb_is_cm_or_zwj(cur_b) ? IM_LB_AL : cur_raw;
    uint8_t eff_b = lb_base(eff);

    if (eff_b == IM_LB_SP) {
        st->lb_sp_after = 1u;
        st->lb_prev_class = IM_LB_SP;
        st->lb_ri_odd = 0u;
        st->lb_nu_state = 0u;
        st->lb_brahmic_state = 0u;
        st->lb_hh_after_al = 0u;
        st->lb_is_seen = 0u;
        return;
    }

    uint8_t prev_eff = st->lb_prev_non_sp;
    uint8_t prev_eff_b = lb_base(prev_eff);
    bool was_sp = (st->lb_sp_after != 0u);

    /* LB14 OP SP* */
    st->lb_op_sp = (eff_b == IM_LB_OP) ? 1u : 0u;

    /* LB15a: (sot|BK|CR|LF|NL|OP|QU|GL|SP|ZW) [\p{Pi}&QU] SP* */
    if (eff == LBX_QU_PI && (was_sp || lb_is_qu_pi_before(prev_eff))) {
        st->lb_qu_pi_sp = 1u;
    } else {
        st->lb_qu_pi_sp = 0u;
    }

    /* LB20a: (sot|BK|CR|LF|NL|SP|ZW|CB|GL) (HY|U+2010) */
    bool is_hh = (eff_b == IM_LB_HY || cp == 0x2010u);
    bool prev_lb20a_ctx = was_sp || prev_eff_b == IM_LB_BK || prev_eff_b == IM_LB_CR || prev_eff_b == IM_LB_LF ||
                          prev_eff_b == IM_LB_NL || prev_eff_b == IM_LB_ZW || prev_eff_b == IM_LB_CB || prev_eff_b == IM_LB_GL;
    if (is_hh && prev_lb20a_ctx) {
        st->lb_hh_after_al = 1u;
    } else {
        st->lb_hh_after_al = 0u;
    }

    /* LB25 numeric run state: 1 = NU (SY|IS)*, 2 = NU (SY|IS)* (CL|CP) */
    if (eff_b == IM_LB_NU) {
        st->lb_nu_state = 1u;
    } else if (!was_sp && st->lb_nu_state == 1u && (eff_b == IM_LB_SY || eff_b == IM_LB_IS)) {
        st->lb_nu_state = 1u;
    } else if (!was_sp && st->lb_nu_state == 1u && (eff_b == IM_LB_CL || eff_b == IM_LB_CP)) {
        st->lb_nu_state = 2u;
    } else {
        st->lb_nu_state = 0u;
    }

    /* LB28a Brahmic state: 1 = AK|DC|AS, 2 = (AK|DC|AS) VI */
    if (lb_is_ak_dc_as(eff)) {
        st->lb_brahmic_state = 1u;
    } else if (!was_sp && st->lb_brahmic_state == 1u && eff_b == IM_LB_VI) {
        st->lb_brahmic_state = 2u;
    } else {
        st->lb_brahmic_state = 0u;
    }

    /* LB30a RI parity */
    if (eff_b == IM_LB_RI) {
        if (!was_sp && prev_eff_b == IM_LB_RI && st->lb_ri_odd) {
            st->lb_ri_odd = 0u;
        } else {
            st->lb_ri_odd = 1u;
        }
    } else {
        st->lb_ri_odd = 0u;
    }

    /* LB30b unassigned Extended_Pictographic */
    const im_ucd_prop *pr = prop_of(cp);
    st->lb_is_seen = ((pr->flags & IM_UCD_FLAG_EXTPIC) != 0u && pr->gc == IM_GC_CN) ? 1u : 0u;

    /* Track EastAsian wide flag in phase bit 0 for LB19a */
    st->phase = lb_is_ea_wide(cp) ? 1u : 0u;

    st->lb_prev_prev_non_sp = was_sp ? IM_LB_SP : prev_eff;
    st->lb_prev_non_sp = eff;
    st->lb_prev_class = cur_raw;
    st->lb_sp_after = 0u;
}

static bool lb_needs_lookahead(const im_unicode_breaker *st, uint8_t cur_raw, uint32_t cur_cp) {
    uint8_t prev_raw = st->lb_prev_class;
    uint8_t cur_b = lb_base(cur_raw);
    if (lb_is_hard_break(prev_raw) || lb_is_hard_break(cur_b)) return false;
    if (cur_b == IM_LB_SP || cur_b == IM_LB_ZW) return false;
    if (lb_base(st->lb_prev_non_sp) == IM_LB_ZW) return false;
    if (prev_raw == IM_LB_ZWJ) return false;
    if (lb_is_cm_or_zwj(cur_b) && lb_is_lb9_base(prev_raw)) return false;

    uint8_t eff_cur = lb_is_cm_or_zwj(cur_b) ? IM_LB_AL : cur_raw;
    uint8_t eff_cur_b = lb_base(eff_cur);
    uint8_t eff_prev = lb_is_cm_or_zwj(lb_base(st->lb_prev_non_sp)) ? IM_LB_AL : st->lb_prev_non_sp;
    uint8_t eff_prev_b = lb_base(eff_prev);
    bool after_sp = (st->lb_sp_after != 0u);
    (void)cur_cp;

    /* LB15b: × QU_PF if next effective is in qu_pf_after */
    if (eff_cur == LBX_QU_PF) return true;
    /* LB15c: SP ÷ IS NU */
    if (after_sp && eff_cur_b == IM_LB_IS) return true;
    /* LB19a: × QU ([^EastAsian] | eot) when prev is EastAsian */
    if (!after_sp && eff_cur_b == IM_LB_QU && (st->phase & 1u) != 0u) return true;
    /* LB25: (PR|PO) × (OP|HY) NU */
    if (!after_sp && (eff_prev_b == IM_LB_PR || eff_prev_b == IM_LB_PO) &&
        (eff_cur_b == IM_LB_OP || eff_cur_b == IM_LB_HY)) {
        return true;
    }
    /* LB28a(d): (AK|DC|AS) × (AK|DC|AS) VF */
    if (!after_sp && lb_is_ak_dc_as(eff_prev) && lb_is_ak_dc_as(eff_cur)) return true;
    return false;
}

static uint8_t lb_decide_with_lb19a(const im_unicode_breaker *st, uint8_t cur_raw, uint32_t cur_cp,
                                    uint8_t next_eff, uint32_t next_cp, bool has_next_eff) {
    uint8_t r = lb_decide_pair(st, cur_raw, cur_cp, next_eff, has_next_eff);
    /* LB19a refinement for Unicode 16.0.0:
     * Only applies when we are between two non-SP/non-hard tokens where LB19 didn't already forbid
     * (i.e. prev_eff is QU_PF or cur is QU_PI) and rules LB1..LB18 didn't decide.
     * Specifically:
     *   [^$EastAsian] × QU
     *   × QU ([^$EastAsian] | eot)
     *   QU × [^$EastAsian]
     *   (sot | [^$EastAsian]) QU ×
     */
    if (r == IM_BREAK_ALLOW && st->lb_sp_after == 0u && !lb_is_hard_break(st->lb_prev_class) &&
        lb_base(st->lb_prev_non_sp) != IM_LB_ZW) {
        uint8_t cur_b = lb_base(cur_raw);
        uint8_t eff_cur = lb_is_cm_or_zwj(cur_b) ? IM_LB_AL : cur_raw;
        uint8_t eff_prev = lb_is_cm_or_zwj(lb_base(st->lb_prev_non_sp)) ? IM_LB_AL : st->lb_prev_non_sp;
        bool prev_ea = (st->phase & 1u) != 0u;
        bool cur_ea = lb_is_ea_wide(cur_cp);
        if (lb_base(eff_cur) == IM_LB_QU) {
            if (!prev_ea) return IM_BREAK_NONE;
            if (has_next_eff && (next_eff == LBX_EOT || !lb_is_ea_wide(next_cp))) return IM_BREAK_NONE;
        }
        if (lb_base(eff_prev) == IM_LB_QU) {
            if (!cur_ea) return IM_BREAK_NONE;
            if ((st->phase & 2u) == 0u) return IM_BREAK_NONE; /* prev-before-QU was sot or !EA */
        }
    }
    return r;
}

static size_t lb_feed_impl(im_unicode_breaker *st, const uint32_t *cps, size_t len, bool is_end,
                           uint8_t *breaks, bool write_out) {
    size_t w = 0u;
    for (size_t i = 0u; i < len; i++) {
        uint32_t cp = cps[i];
        uint8_t cur_raw = lb_resolve_class(cp);

        if (!st->started) {
            st->started = 1u;
            if (write_out) breaks[w] = IM_BREAK_NONE; /* LB2: sot × */
            w++;
            uint8_t cur_b = lb_base(cur_raw);
            uint8_t eff = lb_is_cm_or_zwj(cur_b) ? IM_LB_AL : cur_raw;
            uint8_t eff_b = lb_base(eff);
            st->lb_prev_class = cur_raw;
            st->lb_prev_non_sp = eff;
            st->lb_prev_prev_non_sp = IM_LB_BK;
            st->lb_sp_after = (eff_b == IM_LB_SP) ? 1u : 0u;
            st->lb_op_sp = (eff_b == IM_LB_OP) ? 1u : 0u;
            st->lb_qu_pi_sp = (eff == LBX_QU_PI) ? 1u : 0u;
            st->lb_hh_after_al = (eff_b == IM_LB_HY || cp == 0x2010u) ? 1u : 0u;
            st->lb_nu_state = (eff_b == IM_LB_NU) ? 1u : 0u;
            st->lb_brahmic_state = lb_is_ak_dc_as(eff) ? 1u : 0u;
            st->lb_ri_odd = (eff_b == IM_LB_RI) ? 1u : 0u;
            const im_ucd_prop *pr = prop_of(cp);
            st->lb_is_seen = ((pr->flags & IM_UCD_FLAG_EXTPIC) != 0u && pr->gc == IM_GC_CN) ? 1u : 0u;
            st->phase = lb_is_ea_wide(cp) ? 1u : 0u; /* bit 1 = 0 because sot is !EA */
            continue;
        }

        if (st->pending_kind != 0u) {
            uint8_t cur_b = lb_base(cur_raw);
            if (lb_is_cm_or_zwj(cur_b) && lb_is_lb9_base(st->pending_first)) {
                st->pending_mid_count++;
                st->pending_kind = (cur_b == IM_LB_ZWJ) ? 2u : 1u;
                continue;
            }
            /* Resolve pending with lookahead = cur_raw */
            uint8_t next_eff = lb_is_cm_or_zwj(cur_b) ? IM_LB_AL : cur_raw;
            uint8_t p_brk = lb_decide_with_lb19a(st, st->pending_first, st->pending_cp, next_eff, cp, true);
            if (write_out) breaks[w] = p_brk;
            w++;
            uint8_t prev_ea_before_qu = (st->phase & 1u) << 1;
            lb_advance_state(st, st->pending_cp, st->pending_first, p_brk);
            st->phase = (uint8_t)((st->phase & 1u) | prev_ea_before_qu);
            for (size_t k = 0u; k < st->pending_mid_count; k++) {
                if (write_out) breaks[w] = IM_BREAK_NONE; /* LB9 absorbed */
                w++;
                st->lb_prev_class = (k + 1u == st->pending_mid_count && st->pending_kind == 2u) ? IM_LB_ZWJ : IM_LB_CM;
            }
            st->pending_kind = 0u;
            st->pending_mid_count = 0u;
            /* Fall through to process current cp! */
        }

        if (lb_needs_lookahead(st, cur_raw, cp)) {
            st->pending_kind = 1u;
            st->pending_first = cur_raw;
            st->pending_cp = cp;
            st->pending_mid_count = 0u;
            continue;
        }

        uint8_t brk = lb_decide_with_lb19a(st, cur_raw, cp, LBX_EOT, 0u, false);
        if (write_out) breaks[w] = brk;
        w++;
        uint8_t prev_ea_before_qu = (st->phase & 1u) << 1;
        lb_advance_state(st, cp, cur_raw, brk);
        st->phase = (uint8_t)((st->phase & 1u) | prev_ea_before_qu);
    }

    if (is_end && !st->finished) {
        if (st->pending_kind != 0u) {
            uint8_t p_brk = lb_decide_with_lb19a(st, st->pending_first, st->pending_cp, LBX_EOT, 0u, true);
            if (write_out) breaks[w] = p_brk;
            w++;
            lb_advance_state(st, st->pending_cp, st->pending_first, p_brk);
            for (size_t k = 0u; k < st->pending_mid_count; k++) {
                if (write_out) breaks[w] = IM_BREAK_NONE;
                w++;
            }
            st->pending_kind = 0u;
            st->pending_mid_count = 0u;
        }
        /* LB3: ! eot (for empty input, position 0 is IM_BREAK_MUST as well) */
        if (write_out) breaks[w] = IM_BREAK_MUST;
        w++;
        st->finished = 1u;
    }
    return w;
}

im_unicode_error im_unicode_line_breaks(const uint32_t *cps, size_t len, uint8_t *breaks, size_t breaks_cap) {
    if ((len > 0u && !cps) || !breaks) {
        return IM_UNICODE_BAD_ARG;
    }
    if (breaks_cap < len + 1u) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    im_unicode_breaker st;
    im_unicode_breaker_init(&st, IM_BREAK_KIND_LINE);
    size_t written = 0u;
    return im_unicode_breaker_feed(&st, cps, len, true, breaks, breaks_cap, &written);
}

/* =========================================================================
 * Streaming Breaker Public API
 * ========================================================================= */

im_unicode_error im_unicode_breaker_init(im_unicode_breaker *st, im_unicode_break_kind kind) {
    if (!st || (unsigned)kind > (unsigned)IM_BREAK_KIND_LINE) {
        return IM_UNICODE_BAD_ARG;
    }
    uint8_t *p = (uint8_t *)st;
    for (size_t i = 0u; i < sizeof(*st); i++) {
        p[i] = 0u;
    }
    st->kind = (uint8_t)kind;
    return IM_UNICODE_OK;
}

im_unicode_error im_unicode_breaker_feed(im_unicode_breaker *st, const uint32_t *cps, size_t len, bool is_end,
                                         uint8_t *breaks, size_t breaks_cap, size_t *out_written) {
    if (!st || !out_written || (len > 0u && !cps) || (!breaks && breaks_cap > 0u)) {
        return IM_UNICODE_BAD_ARG;
    }
    *out_written = 0u;
    if (st->finished) {
        return (len == 0u) ? IM_UNICODE_OK : IM_UNICODE_BAD_ARG;
    }

    if (st->kind == IM_BREAK_KIND_GRAPHEME) {
        size_t needed = len + (is_end ? 1u : 0u);
        *out_written = needed;
        if (needed > 0u && (!breaks || breaks_cap < needed)) {
            return IM_UNICODE_BUFFER_TOO_SMALL;
        }
        size_t w = 0u;
        for (size_t i = 0u; i < len; i++) {
            uint8_t b = 0u;
            grapheme_step(st, cps[i], &b);
            breaks[w++] = b;
        }
        if (is_end) {
            breaks[w++] = IM_BREAK_ALLOW; /* GB2: Any ÷ eot */
            st->finished = 1u;
        }
        *out_written = w;
        return IM_UNICODE_OK;
    }

    if (st->kind == IM_BREAK_KIND_WORD) {
        return word_feed(st, cps, len, is_end, breaks, breaks_cap, out_written);
    }

    if (st->kind == IM_BREAK_KIND_LINE) {
        im_unicode_breaker sim = *st;
        size_t needed = lb_feed_impl(&sim, cps, len, is_end, NULL, false);
        *out_written = needed;
        if (needed > 0u && (!breaks || breaks_cap < needed)) {
            return IM_UNICODE_BUFFER_TOO_SMALL;
        }
        *out_written = lb_feed_impl(st, cps, len, is_end, breaks, true);
        return IM_UNICODE_OK;
    }

    return IM_UNICODE_BAD_ARG;
}

/* =========================================================================
 * UAX #9 Bidirectional Algorithm (Unicode 16.0.0)
 * ========================================================================= */

#define BIDI_MAX_DEPTH 125u
#define BIDI_BRACKET_STACK_MAX 63u

typedef struct bidi_status {
    uint8_t level;
    uint8_t override_dir; /* 0 = neutral, 1 = L, 2 = R */
    uint8_t isolate;
} bidi_status;

typedef struct bidi_bracket_pair {
    size_t open_pos;
    size_t close_pos;
} bidi_bracket_pair;

size_t im_unicode_bidi_scratch_size(size_t len) {
    if (len == 0u) {
        return 0u;
    }
    /* Per codepoint:
     *   orig_cls[len]    (uint8_t)
     *   cur_cls[len]     (uint8_t)
     *   tmp_levels[len]  (uint8_t)
     *   run_idx[len]     (size_t)
     *   iso_match[len]   (size_t)
     *   run_pos[len]     (size_t)
     *   aux_stack[len]   (size_t: O(N) BD9 isolate stack, then reused for BD16 completed bracket pairs <= len/2 * 2)
     * Plus alignment padding (64 bytes).
     */
    size_t per = 3u * sizeof(uint8_t) + 4u * sizeof(size_t);
    if (len > (SIZE_MAX - 64u) / per) {
        return SIZE_MAX;
    }
    return len * per + 64u;
}

static bool bidi_is_isolate_initiator(uint8_t c) {
    return c == IM_BIDI_LRI || c == IM_BIDI_RLI || c == IM_BIDI_FSI;
}

static bool bidi_is_x9_removed(uint8_t c) {
    return c == IM_BIDI_RLE || c == IM_BIDI_LRE || c == IM_BIDI_RLO || c == IM_BIDI_LRO ||
           c == IM_BIDI_PDF || c == IM_BIDI_BN;
}

static int8_t bidi_p2_p3(const uint8_t *cls, size_t start, size_t end) {
    size_t iso_depth = 0u;
    for (size_t i = start; i < end; i++) {
        uint8_t c = cls[i];
        if (bidi_is_isolate_initiator(c)) {
            iso_depth++;
        } else if (c == IM_BIDI_PDI) {
            if (iso_depth > 0u) iso_depth--;
        } else if (iso_depth == 0u) {
            if (c == IM_BIDI_L) return 0;
            if (c == IM_BIDI_R || c == IM_BIDI_AL) return 1;
        }
    }
    return 0;
}

static uint32_t bidi_canonical_bracket(uint32_t cp) {
    if (cp == 0x2329u) return 0x3008u;
    if (cp == 0x232Au) return 0x3009u;
    return cp;
}

static void bidi_apply_l1_l2(const uint8_t *orig_cls, const uint8_t *para_levels, size_t line_start, size_t line_end,
                             int8_t para_level, uint8_t *line_levels_out, size_t *order_out) {
    size_t n = line_end - line_start;
    if (n == 0u) {
        return;
    }

    /* Copy levels into line_levels_out (caller guarantees non-NULL here via scratch or output) */
    for (size_t i = 0u; i < n; i++) {
        line_levels_out[i] = para_levels[line_start + i];
    }

    /* L1: reset segment/paragraph separators and preceding whitespace/isolate formatting */
    for (size_t i = 0u; i < n; i++) {
        uint8_t c = orig_cls[line_start + i];
        if (c == IM_BIDI_B || c == IM_BIDI_S) {
            line_levels_out[i] = (uint8_t)para_level;
            size_t j = i;
            while (j > 0u) {
                j--;
                uint8_t pc = orig_cls[line_start + j];
                if (pc == IM_BIDI_WS || pc == IM_BIDI_FSI || pc == IM_BIDI_LRI || pc == IM_BIDI_RLI ||
                    pc == IM_BIDI_PDI || bidi_is_x9_removed(pc)) {
                    line_levels_out[j] = (uint8_t)para_level;
                } else {
                    break;
                }
            }
        }
    }
    /* L1 clause 4: trailing whitespace / isolate formatting at end of line */
    size_t j = n;
    while (j > 0u) {
        j--;
        uint8_t pc = orig_cls[line_start + j];
        if (pc == IM_BIDI_WS || pc == IM_BIDI_FSI || pc == IM_BIDI_LRI || pc == IM_BIDI_RLI ||
            pc == IM_BIDI_PDI || bidi_is_x9_removed(pc)) {
            line_levels_out[j] = (uint8_t)para_level;
        } else {
            break;
        }
    }

    if (!order_out) {
        return;
    }
    for (size_t i = 0u; i < n; i++) {
        order_out[i] = line_start + i;
    }

    /* L2: reverse contiguous sequences of level >= L from max_level down to min_odd_level */
    uint8_t max_lvl = 0u;
    uint8_t min_odd = 255u;
    for (size_t i = 0u; i < n; i++) {
        uint8_t lv = line_levels_out[i];
        if (lv > max_lvl) max_lvl = lv;
        if ((lv & 1u) != 0u && lv < min_odd) min_odd = lv;
    }
    if (min_odd == 255u) {
        return;
    }
    for (int lvl = (int)max_lvl; lvl >= (int)min_odd; lvl--) {
        size_t i = 0u;
        while (i < n) {
            if (line_levels_out[i] >= (uint8_t)lvl) {
                size_t start = i;
                while (i < n && line_levels_out[i] >= (uint8_t)lvl) {
                    i++;
                }
                size_t a = start, b = i - 1u;
                while (a < b) {
                    size_t tmp = order_out[a];
                    order_out[a] = order_out[b];
                    order_out[b] = tmp;
                    a++;
                    b--;
                }
            } else {
                i++;
            }
        }
    }
}

static im_unicode_error bidi_resolve_core(const uint32_t *cps, const uint8_t *given_cls, size_t len,
                                          int8_t base_level, void *scratch, size_t scratch_bytes,
                                          uint8_t *levels_out, size_t levels_cap,
                                          size_t *order_out, size_t order_cap,
                                          int8_t *resolved_base_out) {
    if (base_level < -1 || base_level > 1) {
        return IM_UNICODE_BAD_ARG;
    }
    if (len > 0u && !cps && !given_cls) {
        return IM_UNICODE_BAD_ARG;
    }
    if (len > 0u && !levels_out) {
        return IM_UNICODE_BAD_ARG;
    }
    if (levels_cap < len) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    if (order_out && order_cap < len) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    size_t req_scratch = im_unicode_bidi_scratch_size(len);
    if (len > 0u && (!scratch || scratch_bytes < req_scratch)) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    if (len == 0u) {
        if (resolved_base_out) {
            *resolved_base_out = (base_level < 0) ? 0 : base_level;
        }
        return IM_UNICODE_OK;
    }

    /* Partition aligned scratch buffer */
    uintptr_t p = (uintptr_t)scratch;
    p = (p + sizeof(size_t) - 1u) & ~(uintptr_t)(sizeof(size_t) - 1u);
    size_t *run_idx = (size_t *)p; p += len * sizeof(size_t);
    size_t *iso_match = (size_t *)p; p += len * sizeof(size_t);
    size_t *run_pos = (size_t *)p; p += len * sizeof(size_t);
    size_t *aux_buf = (size_t *)p; p += len * sizeof(size_t);
    uint8_t *orig_cls = (uint8_t *)p; p += len;
    uint8_t *cur_cls = (uint8_t *)p; p += len;
    uint8_t *tmp_levels = (uint8_t *)p;

    for (size_t i = 0u; i < len; i++) {
        uint8_t c = given_cls ? given_cls[i] : (uint8_t)im_unicode_bidi_class(cps[i]);
        if (c > IM_BIDI_PDI) {
            return IM_UNICODE_BAD_ARG;
        }
        orig_cls[i] = c;
        cur_cls[i] = c;
        iso_match[i] = SIZE_MAX;
        run_idx[i] = SIZE_MAX;
    }

    /* P2/P3: paragraph embedding level */
    int8_t para_level = (base_level < 0) ? bidi_p2_p3(orig_cls, 0u, len) : base_level;
    if (resolved_base_out) {
        *resolved_base_out = para_level;
    }

    /* Precompute matching PDI for each isolate initiator in O(N) using aux_buf as stack (BD9) */
    {
        size_t iso_sp = 0u;
        for (size_t i = 0u; i < len; i++) {
            if (bidi_is_isolate_initiator(orig_cls[i])) {
                aux_buf[iso_sp++] = i;
            } else if (orig_cls[i] == IM_BIDI_PDI && iso_sp > 0u) {
                size_t open_i = aux_buf[--iso_sp];
                iso_match[open_i] = i;
                iso_match[i] = open_i;
            }
        }
    }

    /* X1..X8: Explicit Levels and Directions */
    bidi_status stack[BIDI_MAX_DEPTH + 2u];
    size_t sp = 0u;
    stack[0].level = (uint8_t)para_level;
    stack[0].override_dir = 0u;
    stack[0].isolate = 0u;
    size_t overflow_iso = 0u;
    size_t overflow_emb = 0u;
    size_t valid_iso = 0u;

    for (size_t i = 0u; i < len; i++) {
        uint8_t c = orig_cls[i];
        uint8_t cur_lvl = stack[sp].level;
        uint8_t cur_ovr = stack[sp].override_dir;

        if (c == IM_BIDI_RLE || c == IM_BIDI_LRE || c == IM_BIDI_RLO || c == IM_BIDI_LRO) {
            bool rtl = (c == IM_BIDI_RLE || c == IM_BIDI_RLO);
            uint8_t next_lvl = rtl ? (uint8_t)((cur_lvl + 1u) | 1u) : (uint8_t)((cur_lvl + 2u) & ~1u);
            tmp_levels[i] = cur_lvl;
            if (next_lvl <= BIDI_MAX_DEPTH && overflow_iso == 0u && overflow_emb == 0u) {
                sp++;
                stack[sp].level = next_lvl;
                stack[sp].override_dir = (c == IM_BIDI_RLO) ? 2u : ((c == IM_BIDI_LRO) ? 1u : 0u);
                stack[sp].isolate = 0u;
            } else if (overflow_iso == 0u) {
                overflow_emb++;
            }
        } else if (bidi_is_isolate_initiator(c)) {
            tmp_levels[i] = cur_lvl;
            if (cur_ovr != 0u) cur_cls[i] = (cur_ovr == 1u) ? IM_BIDI_L : IM_BIDI_R;
            bool rtl;
            if (c == IM_BIDI_RLI) {
                rtl = true;
            } else if (c == IM_BIDI_LRI) {
                rtl = false;
            } else {
                size_t end_iso = (iso_match[i] != SIZE_MAX) ? iso_match[i] : len;
                rtl = (bidi_p2_p3(orig_cls, i + 1u, end_iso) == 1);
            }
            uint8_t next_lvl = rtl ? (uint8_t)((cur_lvl + 1u) | 1u) : (uint8_t)((cur_lvl + 2u) & ~1u);
            if (next_lvl <= BIDI_MAX_DEPTH && overflow_iso == 0u && overflow_emb == 0u) {
                valid_iso++;
                sp++;
                stack[sp].level = next_lvl;
                stack[sp].override_dir = 0u;
                stack[sp].isolate = 1u;
            } else {
                overflow_iso++;
            }
        } else if (c == IM_BIDI_PDI) {
            if (overflow_iso > 0u) {
                overflow_iso--;
            } else if (valid_iso > 0u) {
                overflow_emb = 0u;
                while (sp > 0u && !stack[sp].isolate) {
                    sp--;
                }
                if (sp > 0u) sp--;
                valid_iso--;
            }
            tmp_levels[i] = stack[sp].level;
            if (stack[sp].override_dir != 0u) cur_cls[i] = (stack[sp].override_dir == 1u) ? IM_BIDI_L : IM_BIDI_R;
        } else if (c == IM_BIDI_PDF) {
            tmp_levels[i] = cur_lvl;
            if (overflow_iso > 0u) {
                /* ignore */
            } else if (overflow_emb > 0u) {
                overflow_emb--;
            } else if (!stack[sp].isolate && sp > 0u) {
                sp--;
            }
        } else if (c == IM_BIDI_B) {
            tmp_levels[i] = (uint8_t)para_level;
            sp = 0u;
            overflow_iso = 0u;
            overflow_emb = 0u;
            valid_iso = 0u;
        } else {
            tmp_levels[i] = cur_lvl;
            if (cur_ovr != 0u && c != IM_BIDI_BN) {
                cur_cls[i] = (cur_ovr == 1u) ? IM_BIDI_L : IM_BIDI_R;
            }
        }
    }

    /* X10: Build maximal level runs (skipping X9-removed characters) and isolating run sequences */
    /* First identify level runs: store run_id per character in run_idx[i] */
    size_t num_runs = 0u;
    for (size_t i = 0u; i < len; ) {
        if (bidi_is_x9_removed(orig_cls[i])) {
            i++;
            continue;
        }
        uint8_t lv = tmp_levels[i];
        size_t r_id = num_runs++;
        while (i < len) {
            if (bidi_is_x9_removed(orig_cls[i])) {
                i++;
                continue;
            }
            if (tmp_levels[i] != lv) {
                break;
            }
            run_idx[i] = r_id;
            i++;
        }
    }

    /* Keep explicit levels immutable in tmp_levels[] for X10 sos/eos; write I1/I2 implicit levels into levels_out[]. */
    for (size_t i = 0u; i < len; i++) {
        levels_out[i] = tmp_levels[i];
    }

    /* Process each isolating run sequence starting at a run whose first non-X9 char is NOT a matched PDI */
    size_t i_scan = 0u;
    while (i_scan < len) {
        if (run_idx[i_scan] == SIZE_MAX) {
            i_scan++;
            continue;
        }
        size_t start_run = run_idx[i_scan];
        /* Check if this run starts with a PDI that matches an isolate initiator -> if so, already covered */
        if (orig_cls[i_scan] == IM_BIDI_PDI && iso_match[i_scan] != SIZE_MAX) {
            while (i_scan < len && (run_idx[i_scan] == start_run || run_idx[i_scan] == SIZE_MAX)) {
                i_scan++;
            }
            continue;
        }

        /* Collect all character indices in this isolating run sequence into run_pos[0..seq_len-1] */
        size_t seq_len = 0u;
        uint8_t seq_lvl = tmp_levels[i_scan];
        size_t cur_pos = i_scan;
        for (;;) {
            size_t r_id = run_idx[cur_pos];
            size_t last_in_run = cur_pos;
            while (cur_pos < len && (run_idx[cur_pos] == r_id || run_idx[cur_pos] == SIZE_MAX)) {
                if (run_idx[cur_pos] == r_id) {
                    run_pos[seq_len++] = cur_pos;
                    last_in_run = cur_pos;
                }
                cur_pos++;
            }
            if (bidi_is_isolate_initiator(orig_cls[last_in_run]) && iso_match[last_in_run] != SIZE_MAX) {
                cur_pos = iso_match[last_in_run];
                if (run_idx[cur_pos] == SIZE_MAX) break;
            } else {
                break;
            }
        }

        /* Determine sos and eos (X10) */
        uint8_t prev_lvl = (uint8_t)para_level;
        for (size_t k = run_pos[0]; k > 0u; ) {
            k--;
            if (!bidi_is_x9_removed(orig_cls[k])) {
                prev_lvl = tmp_levels[k];
                break;
            }
        }
        uint8_t next_lvl = (uint8_t)para_level;
        size_t last_p = run_pos[seq_len - 1u];
        if (!bidi_is_isolate_initiator(orig_cls[last_p])) {
            for (size_t k = last_p + 1u; k < len; k++) {
                if (!bidi_is_x9_removed(orig_cls[k])) {
                    next_lvl = tmp_levels[k];
                    break;
                }
            }
        }
        uint8_t sos = (((seq_lvl > prev_lvl) ? seq_lvl : prev_lvl) & 1u) ? IM_BIDI_R : IM_BIDI_L;
        uint8_t eos = (((seq_lvl > next_lvl) ? seq_lvl : next_lvl) & 1u) ? IM_BIDI_R : IM_BIDI_L;

        /* W1: NSM -> previous type (or sos; or ON if prev is isolate initiator or PDI) */
        for (size_t k = 0u; k < seq_len; k++) {
            size_t idx = run_pos[k];
            if (cur_cls[idx] == IM_BIDI_NSM) {
                if (k == 0u) {
                    cur_cls[idx] = sos;
                } else {
                    uint8_t pc = cur_cls[run_pos[k - 1u]];
                    if (bidi_is_isolate_initiator(pc) || pc == IM_BIDI_PDI) {
                        cur_cls[idx] = IM_BIDI_ON;
                    } else {
                        cur_cls[idx] = pc;
                    }
                }
            }
        }

        /* W2: EN -> AN if last strong type was AL */
        uint8_t last_strong = sos;
        for (size_t k = 0u; k < seq_len; k++) {
            size_t idx = run_pos[k];
            uint8_t c = cur_cls[idx];
            if (c == IM_BIDI_L || c == IM_BIDI_R || c == IM_BIDI_AL) {
                last_strong = c;
            } else if (c == IM_BIDI_EN && last_strong == IM_BIDI_AL) {
                cur_cls[idx] = IM_BIDI_AN;
            }
        }

        /* W3: AL -> R */
        for (size_t k = 0u; k < seq_len; k++) {
            if (cur_cls[run_pos[k]] == IM_BIDI_AL) {
                cur_cls[run_pos[k]] = IM_BIDI_R;
            }
        }

        /* W4: single ES between EN/EN -> EN; single CS between EN/EN -> EN or AN/AN -> AN */
        for (size_t k = 1u; k + 1u < seq_len; k++) {
            uint8_t c = cur_cls[run_pos[k]];
            uint8_t prev_c = cur_cls[run_pos[k - 1u]];
            uint8_t next_c = cur_cls[run_pos[k + 1u]];
            if (c == IM_BIDI_ES && prev_c == IM_BIDI_EN && next_c == IM_BIDI_EN) {
                cur_cls[run_pos[k]] = IM_BIDI_EN;
            } else if (c == IM_BIDI_CS && prev_c == next_c && (prev_c == IM_BIDI_EN || prev_c == IM_BIDI_AN)) {
                cur_cls[run_pos[k]] = prev_c;
            }
        }

        /* W5: ET adjacent to EN -> EN */
        for (size_t k = 0u; k < seq_len; k++) {
            if (cur_cls[run_pos[k]] == IM_BIDI_ET) {
                size_t start = k;
                while (k < seq_len && cur_cls[run_pos[k]] == IM_BIDI_ET) k++;
                bool adj_en = (start > 0u && cur_cls[run_pos[start - 1u]] == IM_BIDI_EN) ||
                              (k < seq_len && cur_cls[run_pos[k]] == IM_BIDI_EN);
                if (adj_en) {
                    for (size_t t = start; t < k; t++) cur_cls[run_pos[t]] = IM_BIDI_EN;
                }
            }
        }

        /* W6: remaining ES, ET, CS -> ON */
        for (size_t k = 0u; k < seq_len; k++) {
            uint8_t c = cur_cls[run_pos[k]];
            if (c == IM_BIDI_ES || c == IM_BIDI_ET || c == IM_BIDI_CS) {
                cur_cls[run_pos[k]] = IM_BIDI_ON;
            }
        }

        /* W7: EN -> L if last strong type was L */
        last_strong = sos;
        for (size_t k = 0u; k < seq_len; k++) {
            size_t idx = run_pos[k];
            uint8_t c = cur_cls[idx];
            if (c == IM_BIDI_L || c == IM_BIDI_R) {
                last_strong = c;
            } else if (c == IM_BIDI_EN && last_strong == IM_BIDI_L) {
                cur_cls[idx] = IM_BIDI_L;
            }
        }

        /* N0: Paired Brackets (BD16) when codepoints are provided; reuse aux_buf for unlimited completed pairs (<= len/2) */
        if (cps) {
            size_t open_pos_stk[BIDI_BRACKET_STACK_MAX];
            uint32_t open_pair_stk[BIDI_BRACKET_STACK_MAX];
            size_t bsp = 0u;
            bidi_bracket_pair *pairs = (bidi_bracket_pair *)aux_buf;
            size_t max_pairs = len / 2u;
            size_t pair_cnt = 0u;

            for (size_t k = 0u; k < seq_len; k++) {
                size_t idx = run_pos[k];
                if (cur_cls[idx] != IM_BIDI_ON) continue;
                uint32_t paired = 0u;
                im_bidi_bracket_type bt = im_unicode_bidi_bracket(cps[idx], &paired);
                if (bt == IM_BPT_OPEN) {
                    if (bsp >= BIDI_BRACKET_STACK_MAX) {
                        /* BD16: stop processing and return an empty list on stack overflow */
                        pair_cnt = 0u;
                        break;
                    }
                    open_pos_stk[bsp] = k;
                    open_pair_stk[bsp] = bidi_canonical_bracket(paired);
                    bsp++;
                } else if (bt == IM_BPT_CLOSE) {
                    uint32_t cur_can = bidi_canonical_bracket(cps[idx]);
                    for (size_t s = bsp; s > 0u; ) {
                        s--;
                        if (open_pair_stk[s] == cur_can) {
                            if (pair_cnt < max_pairs) {
                                pairs[pair_cnt].open_pos = open_pos_stk[s];
                                pairs[pair_cnt].close_pos = k;
                                pair_cnt++;
                            }
                            bsp = s;
                            break;
                        }
                    }
                }
            }

            /* Sort pairs by ascending open_pos (insertion sort) */
            for (size_t a = 1u; a < pair_cnt; a++) {
                bidi_bracket_pair key = pairs[a];
                size_t b = a;
                while (b > 0u && pairs[b - 1u].open_pos > key.open_pos) {
                    pairs[b] = pairs[b - 1u];
                    b--;
                }
                pairs[b] = key;
            }

            uint8_t emb_dir = (seq_lvl & 1u) ? IM_BIDI_R : IM_BIDI_L;
            uint8_t opp_dir = (seq_lvl & 1u) ? IM_BIDI_L : IM_BIDI_R;
            for (size_t p_i = 0u; p_i < pair_cnt; p_i++) {
                size_t op = pairs[p_i].open_pos;
                size_t cl = pairs[p_i].close_pos;
                bool has_emb = false, has_opp = false;
                for (size_t k = op + 1u; k < cl; k++) {
                    uint8_t c = cur_cls[run_pos[k]];
                    uint8_t strong = (c == IM_BIDI_L) ? IM_BIDI_L :
                                     ((c == IM_BIDI_R || c == IM_BIDI_EN || c == IM_BIDI_AN) ? IM_BIDI_R : 255u);
                    if (strong == emb_dir) {
                        has_emb = true;
                        break;
                    }
                    if (strong == opp_dir) {
                        has_opp = true;
                    }
                }
                uint8_t set_dir = 255u;
                if (has_emb) {
                    set_dir = emb_dir;
                } else if (has_opp) {
                    /* Look backwards from op for context strong type */
                    uint8_t ctx = sos;
                    for (size_t k = op; k > 0u; ) {
                        k--;
                        uint8_t c = cur_cls[run_pos[k]];
                        if (c == IM_BIDI_L) { ctx = IM_BIDI_L; break; }
                        if (c == IM_BIDI_R || c == IM_BIDI_EN || c == IM_BIDI_AN) { ctx = IM_BIDI_R; break; }
                    }
                    set_dir = (ctx == opp_dir) ? opp_dir : emb_dir;
                }
                if (set_dir != 255u) {
                    cur_cls[run_pos[op]] = set_dir;
                    cur_cls[run_pos[cl]] = set_dir;
                    /* Also update NSMs immediately following open or close (N0 note) */
                    for (size_t k = op + 1u; k < seq_len && orig_cls[run_pos[k]] == IM_BIDI_NSM; k++) {
                        cur_cls[run_pos[k]] = set_dir;
                    }
                    for (size_t k = cl + 1u; k < seq_len && orig_cls[run_pos[k]] == IM_BIDI_NSM; k++) {
                        cur_cls[run_pos[k]] = set_dir;
                    }
                }
            }
        }

        /* N1 & N2: Neutrals and Isolates */
        uint8_t emb_dir = (seq_lvl & 1u) ? IM_BIDI_R : IM_BIDI_L;
        for (size_t k = 0u; k < seq_len; ) {
            uint8_t c = cur_cls[run_pos[k]];
            if (c == IM_BIDI_L || c == IM_BIDI_R || c == IM_BIDI_EN || c == IM_BIDI_AN) {
                k++;
                continue;
            }
            size_t start = k;
            while (k < seq_len) {
                uint8_t tc = cur_cls[run_pos[k]];
                if (tc == IM_BIDI_L || tc == IM_BIDI_R || tc == IM_BIDI_EN || tc == IM_BIDI_AN) break;
                k++;
            }
            uint8_t left_d = sos;
            if (start > 0u) {
                uint8_t lc = cur_cls[run_pos[start - 1u]];
                left_d = (lc == IM_BIDI_L) ? IM_BIDI_L : IM_BIDI_R;
            }
            uint8_t right_d = eos;
            if (k < seq_len) {
                uint8_t rc = cur_cls[run_pos[k]];
                right_d = (rc == IM_BIDI_L) ? IM_BIDI_L : IM_BIDI_R;
            }
            uint8_t fill = (left_d == right_d) ? left_d : emb_dir;
            for (size_t t = start; t < k; t++) {
                cur_cls[run_pos[t]] = fill;
            }
        }

        /* I1 & I2: Resolving Implicit Levels (write to levels_out[], leaving explicit tmp_levels[] intact for later runs) */
        for (size_t k = 0u; k < seq_len; k++) {
            size_t idx = run_pos[k];
            uint8_t c = cur_cls[idx];
            if ((seq_lvl & 1u) == 0u) {
                if (c == IM_BIDI_R) levels_out[idx] = (uint8_t)(seq_lvl + 1u);
                else if (c == IM_BIDI_AN || c == IM_BIDI_EN) levels_out[idx] = (uint8_t)(seq_lvl + 2u);
            } else {
                if (c == IM_BIDI_L || c == IM_BIDI_EN || c == IM_BIDI_AN) levels_out[idx] = (uint8_t)(seq_lvl + 1u);
            }
        }

        /* Advance i_scan past start_run */
        while (i_scan < len && (run_idx[i_scan] == start_run || run_idx[i_scan] == SIZE_MAX)) {
            i_scan++;
        }
    }

    /* Resolve X9-removed characters' levels to preceding non-removed level (or para_level) for L1/BidiTest parity */
    for (size_t i = 0u; i < len; i++) {
        if (bidi_is_x9_removed(orig_cls[i])) {
            levels_out[i] = (i == 0u) ? (uint8_t)para_level : levels_out[i - 1u];
        }
    }

    /* Apply L1 and L2 over the whole paragraph */
    bidi_apply_l1_l2(orig_cls, levels_out, 0u, len, para_level, levels_out, order_out);
    return IM_UNICODE_OK;
}

im_unicode_error im_unicode_bidi_resolve(const uint32_t *cps, size_t len, int8_t base_level,
                                         void *scratch, size_t scratch_bytes,
                                         uint8_t *levels_out, size_t levels_cap,
                                         size_t *order_out, size_t order_cap,
                                         int8_t *resolved_base_out) {
    return bidi_resolve_core(cps, NULL, len, base_level, scratch, scratch_bytes,
                             levels_out, levels_cap, order_out, order_cap, resolved_base_out);
}

im_unicode_error im_unicode_bidi_resolve_classes(const uint8_t *classes, size_t len, int8_t base_level,
                                                 void *scratch, size_t scratch_bytes,
                                                 uint8_t *levels_out, size_t levels_cap,
                                                 size_t *order_out, size_t order_cap,
                                                 int8_t *resolved_base_out) {
    return bidi_resolve_core(NULL, classes, len, base_level, scratch, scratch_bytes,
                             levels_out, levels_cap, order_out, order_cap, resolved_base_out);
}

im_unicode_error im_unicode_bidi_reorder_line(const uint32_t *cps, const uint8_t *para_levels, size_t para_len,
                                              int8_t resolved_base, size_t line_start, size_t line_end,
                                              uint8_t *line_levels_out, size_t line_levels_cap,
                                              size_t *visual_order_out, size_t visual_order_cap) {
    if (resolved_base != 0 && resolved_base != 1) {
        return IM_UNICODE_BAD_ARG;
    }
    if (line_start > line_end || line_end > para_len) {
        return IM_UNICODE_BAD_ARG;
    }
    size_t n = line_end - line_start;
    if (n > 0u && (!cps || !para_levels || !visual_order_out)) {
        return IM_UNICODE_BAD_ARG;
    }
    if (visual_order_out && visual_order_cap < n) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    if (line_levels_out && line_levels_cap < n) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    if (n == 0u) {
        return IM_UNICODE_OK;
    }

    /* O(N) backward pass for UAX #9 L1 (trailing whitespace/isolate/X9 reset + segment/paragraph separator reset) */
    uint8_t max_lvl = 0u;
    uint8_t min_odd = 255u;
    size_t trail_start = n;
    bool in_trail = true;
    bool reset_ws = true;
    bool has_interior_bs = false;

    for (size_t i = n; i > 0u; ) {
        i--;
        uint8_t c = (uint8_t)im_unicode_bidi_class(cps[line_start + i]);
        bool is_ws_like = (c == IM_BIDI_WS || c == IM_BIDI_FSI || c == IM_BIDI_LRI || c == IM_BIDI_RLI ||
                           c == IM_BIDI_PDI || bidi_is_x9_removed(c));
        uint8_t lv = para_levels[line_start + i];
        if (c == IM_BIDI_B || c == IM_BIDI_S) {
            lv = (uint8_t)resolved_base;
            reset_ws = true;
            in_trail = false;
            has_interior_bs = true;
        } else if (is_ws_like) {
            if (reset_ws) {
                lv = (uint8_t)resolved_base;
            }
            if (in_trail) {
                trail_start = i;
            }
        } else {
            reset_ws = false;
            in_trail = false;
        }
        if (line_levels_out) {
            line_levels_out[i] = lv;
        }
        visual_order_out[i] = line_start + i;
        if (lv > max_lvl) max_lvl = lv;
        if ((lv & 1u) != 0u && lv < min_odd) min_odd = lv;
    }

    if (min_odd == 255u) {
        return IM_UNICODE_OK;
    }

    /* L2 reversal: reads line_levels_out directly in O(1), or uses O(1) trail_start when line_levels_out is NULL */
    for (int lvl = (int)max_lvl; lvl >= (int)min_odd; lvl--) {
        size_t i = 0u;
        while (i < n) {
            uint8_t lv_i;
            if (line_levels_out) {
                lv_i = line_levels_out[i];
            } else {
                lv_i = (i >= trail_start) ? (uint8_t)resolved_base : para_levels[line_start + i];
                if (has_interior_bs) {
                    uint8_t ci = (uint8_t)im_unicode_bidi_class(cps[line_start + i]);
                    if (ci == IM_BIDI_B || ci == IM_BIDI_S) lv_i = (uint8_t)resolved_base;
                }
            }
            if (lv_i >= (uint8_t)lvl) {
                size_t start = i;
                while (i < n) {
                    uint8_t lv_k;
                    if (line_levels_out) {
                        lv_k = line_levels_out[i];
                    } else {
                        lv_k = (i >= trail_start) ? (uint8_t)resolved_base : para_levels[line_start + i];
                        if (has_interior_bs) {
                            uint8_t ck = (uint8_t)im_unicode_bidi_class(cps[line_start + i]);
                            if (ck == IM_BIDI_B || ck == IM_BIDI_S) lv_k = (uint8_t)resolved_base;
                        }
                    }
                    if (lv_k < (uint8_t)lvl) break;
                    i++;
                }
                size_t a = start, b = i - 1u;
                while (a < b) {
                    size_t tmp = visual_order_out[a];
                    visual_order_out[a] = visual_order_out[b];
                    visual_order_out[b] = tmp;
                    a++;
                    b--;
                }
            } else {
                i++;
            }
        }
    }
    return IM_UNICODE_OK;
}

/* =========================================================================
 * UAX #15 Normalization (NFD, NFC, NFKD, NFKC)
 * ========================================================================= */

static bool find_decomp(uint32_t cp, bool compat, uint32_t *off_out, uint8_t *len_out) {
    size_t lo = 0u, hi = im_ucd_decomp_count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        uint32_t cur = im_ucd_decomps[mid].cp;
        if (cur == cp) {
            uint32_t meta = im_ucd_decomps[mid].val;
            bool is_compat = (meta >> 31) != 0u;
            if (is_compat && !compat) {
                return false;
            }
            *len_out = (uint8_t)((meta >> 24) & 0x7Fu);
            *off_out = meta & 0xFFFFFFu;
            return true;
        }
        if (cur < cp) lo = mid + 1u;
        else hi = mid;
    }
    return false;
}

static uint32_t find_comp(uint32_t a, uint32_t b) {
    /* Algorithmic Hangul L + V -> LV, LV + T -> LVT */
    if (a >= IM_HANGUL_LBASE && a < IM_HANGUL_LBASE + IM_HANGUL_LCOUNT &&
        b >= IM_HANGUL_VBASE && b < IM_HANGUL_VBASE + IM_HANGUL_VCOUNT) {
        return IM_HANGUL_SBASE + ((a - IM_HANGUL_LBASE) * IM_HANGUL_VCOUNT + (b - IM_HANGUL_VBASE)) * IM_HANGUL_TCOUNT;
    }
    if (a >= IM_HANGUL_SBASE && a < IM_HANGUL_SBASE + IM_HANGUL_SCOUNT &&
        ((a - IM_HANGUL_SBASE) % IM_HANGUL_TCOUNT) == 0u &&
        b > IM_HANGUL_TBASE && b < IM_HANGUL_TBASE + IM_HANGUL_TCOUNT) {
        return a + (b - IM_HANGUL_TBASE);
    }
    uint64_t key = ((uint64_t)a << 21) | (uint64_t)b;
    size_t lo = 0u, hi = im_ucd_comp_count;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        uint64_t cur = im_ucd_comps[mid].key;
        if (cur == key) return im_ucd_comps[mid].composed;
        if (cur < key) lo = mid + 1u;
        else hi = mid;
    }
    return 0u;
}

static void decomp_append(uint32_t cp, bool compat, uint32_t *buf, size_t buf_cap, size_t *len) {
    /* Hangul algorithmic decomposition */
    if (cp >= IM_HANGUL_SBASE && cp < IM_HANGUL_SBASE + IM_HANGUL_SCOUNT) {
        uint32_t s = cp - IM_HANGUL_SBASE;
        uint32_t l = IM_HANGUL_LBASE + s / IM_HANGUL_NCOUNT;
        uint32_t v = IM_HANGUL_VBASE + (s % IM_HANGUL_NCOUNT) / IM_HANGUL_TCOUNT;
        uint32_t t = IM_HANGUL_TBASE + (s % IM_HANGUL_TCOUNT);
        if (*len < buf_cap) buf[*len] = l;
        (*len)++;
        if (*len < buf_cap) buf[*len] = v;
        (*len)++;
        if (t != IM_HANGUL_TBASE) {
            if (*len < buf_cap) buf[*len] = t;
            (*len)++;
        }
        return;
    }
    uint32_t off = 0u;
    uint8_t dlen = 0u;
    if (find_decomp(cp, compat, &off, &dlen)) {
        for (uint8_t i = 0u; i < dlen; i++) {
            decomp_append(im_ucd_decomp_pool[off + i], compat, buf, buf_cap, len);
        }
        return;
    }
    if (*len < buf_cap) buf[*len] = cp;
    (*len)++;
}

static void canonical_order(uint32_t *buf, size_t n) {
    for (size_t i = 1u; i < n; i++) {
        uint8_t ci = im_unicode_ccc(buf[i]);
        if (ci == 0u) continue;
        uint32_t val = buf[i];
        size_t j = i;
        while (j > 0u && im_unicode_ccc(buf[j - 1u]) > ci) {
            buf[j] = buf[j - 1u];
            j--;
        }
        buf[j] = val;
    }
}

static size_t canonical_compose(uint32_t *buf, size_t n) {
    if (n == 0u) return 0u;
    size_t starter_idx = SIZE_MAX;
    uint8_t last_ccc = 0u;
    size_t out = 0u;
    for (size_t i = 0u; i < n; i++) {
        uint32_t ch = buf[i];
        uint8_t ccc = im_unicode_ccc(ch);
        if (starter_idx != SIZE_MAX && (last_ccc < ccc || last_ccc == 0u)) {
            uint32_t comp = find_comp(buf[starter_idx], ch);
            if (comp != 0u) {
                buf[starter_idx] = comp;
                continue;
            }
        }
        if (ccc == 0u) {
            starter_idx = out;
            last_ccc = 0u;
        } else {
            last_ccc = ccc;
        }
        buf[out++] = ch;
    }
    return out;
}

static bool is_safe_segment_boundary(uint32_t cp, bool compat, bool compose) {
    /* A codepoint starts a fresh independent segment if its first decomposed codepoint has ccc == 0,
     * is not a Hangul V/T, and (when composing) cannot be the second codepoint of any canonical composition pair. */
    if (cp >= IM_HANGUL_VBASE && cp < IM_HANGUL_VBASE + IM_HANGUL_VCOUNT) return false;
    if (cp > IM_HANGUL_TBASE && cp < IM_HANGUL_TBASE + IM_HANGUL_TCOUNT) return false;
    uint32_t first = cp;
    uint32_t off = 0u;
    uint8_t dlen = 0u;
    while (find_decomp(first, compat, &off, &dlen) && dlen > 0u) {
        first = im_ucd_decomp_pool[off];
    }
    if (first >= IM_HANGUL_VBASE && first < IM_HANGUL_VBASE + IM_HANGUL_VCOUNT) return false;
    if (first > IM_HANGUL_TBASE && first < IM_HANGUL_TBASE + IM_HANGUL_TCOUNT) return false;
    const im_ucd_prop *pr = prop_of(first);
    if (pr->ccc != 0u) return false;
    if (compose && (pr->flags & IM_UCD_FLAG_COMP_SECOND) != 0u) return false;
    return true;
}

static bool buffers_overlap_u32(const uint32_t *src, size_t src_len, const uint32_t *dst, size_t dst_cap) {
    if (!src || !dst || src_len == 0u || dst_cap == 0u) {
        return false;
    }
    uintptr_t s0 = (uintptr_t)src;
    uintptr_t s1 = s0 + src_len * sizeof(uint32_t);
    uintptr_t d0 = (uintptr_t)dst;
    uintptr_t d1 = d0 + dst_cap * sizeof(uint32_t);
    return s0 < d1 && d0 < s1;
}

static void norm_emit_combining_run(const uint32_t *src, size_t r_k0, size_t r_j0,
                                    size_t r_k1, size_t r_j1, bool compat, bool compose,
                                    uint8_t min_ccc, uint8_t max_ccc,
                                    bool *has_starter, size_t *starter_pos, uint32_t *starter_cp, uint8_t *last_ccc,
                                    uint32_t *dst, size_t dst_cap, size_t *total, bool *overflow) {
    if (min_ccc > max_ccc) return;
    uint32_t cbuf[32];
    for (unsigned target_ccc = min_ccc; target_ccc <= max_ccc; target_ccc++) {
        for (size_t k = r_k0; k <= r_k1; k++) {
            size_t cl = 0u;
            decomp_append(src[k], compat, cbuf, 32u, &cl);
            size_t j_lo = (k == r_k0) ? r_j0 : 0u;
            size_t j_hi = (k == r_k1) ? r_j1 : cl;
            for (size_t j = j_lo; j < j_hi; j++) {
                uint32_t ch = cbuf[j];
                if (im_unicode_ccc(ch) != (uint8_t)target_ccc) continue;
                if (compose && *has_starter && (*last_ccc < (uint8_t)target_ccc || *last_ccc == 0u)) {
                    uint32_t comp = find_comp(*starter_cp, ch);
                    if (comp != 0u) {
                        *starter_cp = comp;
                        if (dst && *starter_pos < dst_cap) dst[*starter_pos] = comp;
                        continue;
                    }
                }
                *last_ccc = (uint8_t)target_ccc;
                if (dst && *total < dst_cap) dst[*total] = ch;
                else *overflow = true;
                (*total)++;
            }
        }
    }
}

/* Stream a single arbitrarily long segment (>1024 codepoints) with zero heap allocation:
   decompose each input codepoint, flush each contiguous ccc>0 run in canonical order (ccc 1..254)
   immediately before every ccc==0 starter (preserving embedded marks inside compatibility decompositions),
   and apply exact canonical composition. */
static void normalize_long_segment(const uint32_t *src, size_t seg_start, size_t seg_end,
                                   bool compat, bool compose,
                                   uint32_t *dst, size_t dst_cap, size_t *total, bool *overflow) {
    uint32_t cbuf[32];
    size_t starter_pos = SIZE_MAX;
    uint32_t starter_cp = 0u;
    uint8_t last_ccc = 0u;
    bool has_starter = false;

    bool in_comb = false;
    size_t comb_k0 = 0u, comb_j0 = 0u;
    uint8_t min_ccc = 255u, max_ccc = 0u;

    for (size_t k = seg_start; k < seg_end; k++) {
        size_t cl = 0u;
        decomp_append(src[k], compat, cbuf, 32u, &cl);
        for (size_t j = 0u; j < cl; j++) {
            uint32_t ch = cbuf[j];
            uint8_t ccc = im_unicode_ccc(ch);
            if (ccc == 0u) {
                if (in_comb) {
                    norm_emit_combining_run(src, comb_k0, comb_j0, k, j, compat, compose, min_ccc, max_ccc,
                                            &has_starter, &starter_pos, &starter_cp, &last_ccc,
                                            dst, dst_cap, total, overflow);
                    in_comb = false;
                    min_ccc = 255u;
                    max_ccc = 0u;
                }
                if (compose && has_starter && last_ccc == 0u) {
                    uint32_t comp = find_comp(starter_cp, ch);
                    if (comp != 0u) {
                        starter_cp = comp;
                        if (dst && starter_pos < dst_cap) dst[starter_pos] = comp;
                        continue;
                    }
                }
                starter_pos = *total;
                starter_cp = ch;
                has_starter = true;
                last_ccc = 0u;
                if (dst && *total < dst_cap) dst[*total] = ch;
                else *overflow = true;
                (*total)++;
            } else {
                if (!in_comb) {
                    in_comb = true;
                    comb_k0 = k;
                    comb_j0 = j;
                }
                if (ccc < min_ccc) min_ccc = ccc;
                if (ccc > max_ccc) max_ccc = ccc;
            }
        }
    }
    if (in_comb) {
        size_t last_k = seg_end - 1u;
        size_t last_cl = 0u;
        decomp_append(src[last_k], compat, cbuf, 32u, &last_cl);
        norm_emit_combining_run(src, comb_k0, comb_j0, last_k, last_cl, compat, compose, min_ccc, max_ccc,
                                &has_starter, &starter_pos, &starter_cp, &last_ccc,
                                dst, dst_cap, total, overflow);
    }
}

im_unicode_error im_unicode_normalize(im_norm_form form, const uint32_t *src, size_t src_len,
                                      uint32_t *dst, size_t dst_cap, size_t *out_len) {
    if (!out_len || (src_len > 0u && !src) || (!dst && dst_cap > 0u) || (unsigned)form > (unsigned)IM_NORM_NFKC) {
        return IM_UNICODE_BAD_ARG;
    }
    if (buffers_overlap_u32(src, src_len, dst, dst_cap)) {
        return IM_UNICODE_BAD_ARG;
    }
    *out_len = 0u;
    if (src_len == 0u) {
        return IM_UNICODE_OK;
    }

    bool compat = (form == IM_NORM_NFKD || form == IM_NORM_NFKC);
    bool compose = (form == IM_NORM_NFC || form == IM_NORM_NFKC);

    uint32_t seg[1024];
    size_t total = 0u;
    bool overflow = false;

    size_t i = 0u;
    while (i < src_len) {
        size_t seg_start = i;
        i++;
        while (i < src_len && !is_safe_segment_boundary(src[i], compat, compose)) {
            i++;
        }
        size_t seg_len = 0u;
        for (size_t k = seg_start; k < i; k++) {
            decomp_append(src[k], compat, seg, 1024u, &seg_len);
        }
        if (seg_len <= 1024u) {
            canonical_order(seg, seg_len);
            if (compose) {
                seg_len = canonical_compose(seg, seg_len);
            }
            for (size_t k = 0u; k < seg_len; k++) {
                if (dst && total < dst_cap) {
                    dst[total] = seg[k];
                } else {
                    overflow = true;
                }
                total++;
            }
        } else {
            normalize_long_segment(src, seg_start, i, compat, compose, dst, dst_cap, &total, &overflow);
        }
    }

    *out_len = total;
    if (overflow || (total > 0u && (!dst || dst_cap < total))) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    return IM_UNICODE_OK;
}

/* =========================================================================
 * Case Folding (CaseFolding.txt C+F, C+S, C+F+T with identity default)
 * ========================================================================= */

im_unicode_error im_unicode_casefold(im_casefold_mode mode, const uint32_t *src, size_t src_len,
                                     uint32_t *dst, size_t dst_cap, size_t *out_len) {
    if (!out_len || (src_len > 0u && !src) || (!dst && dst_cap > 0u) || (unsigned)mode > (unsigned)IM_CASEFOLD_TURKIC) {
        return IM_UNICODE_BAD_ARG;
    }
    if (buffers_overlap_u32(src, src_len, dst, dst_cap)) {
        return IM_UNICODE_BAD_ARG;
    }
    *out_len = 0u;
    if (src_len == 0u) {
        return IM_UNICODE_OK;
    }

    size_t total = 0u;
    bool overflow = false;
    for (size_t i = 0u; i < src_len; i++) {
        uint32_t cp = src[i];
        const im_ucd_casefold *hit = NULL;
        if (cp <= IM_MAX_CP) {
            size_t lo = 0u, hi = im_ucd_casefold_count;
            while (lo < hi) {
                size_t mid = lo + ((hi - lo) >> 1);
                uint32_t cur = im_ucd_casefolds[mid].cp;
                if (cur == cp) {
                    hit = &im_ucd_casefolds[mid];
                    break;
                }
                if (cur < cp) lo = mid + 1u;
                else hi = mid;
            }
        }
        if (!hit) {
            if (dst && total < dst_cap) dst[total] = cp;
            else overflow = true;
            total++;
            continue;
        }
        if (mode == IM_CASEFOLD_SIMPLE) {
            uint32_t mapped = (hit->flags & 1u) ? hit->simple : cp;
            if (dst && total < dst_cap) dst[total] = mapped;
            else overflow = true;
            total++;
        } else if (mode == IM_CASEFOLD_TURKIC && (hit->flags & 4u)) {
            if (dst && total < dst_cap) dst[total] = hit->turkic;
            else overflow = true;
            total++;
        } else if (hit->flags & 2u) {
            for (uint8_t k = 0u; k < hit->full_len; k++) {
                if (dst && total < dst_cap) dst[total] = im_ucd_casefold_pool[hit->full_off + k];
                else overflow = true;
                total++;
            }
        } else {
            if (dst && total < dst_cap) dst[total] = cp;
            else overflow = true;
            total++;
        }
    }

    *out_len = total;
    if (overflow || (total > 0u && (!dst || dst_cap < total))) {
        return IM_UNICODE_BUFFER_TOO_SMALL;
    }
    return IM_UNICODE_OK;
}
