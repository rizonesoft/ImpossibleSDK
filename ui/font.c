/* Impossible UI: fixed-point TrueType and OpenType CFF font parser and outline rasteriser. Portable. */
#include "font.h"

/* ---- Safe big-endian readers (zero UB, strict bounds checking) ---- */

static bool range_ok(size_t size, size_t off, size_t len) {
    return off <= size && len <= size - off;
}

static bool rd_u8(const uint8_t *d, size_t sz, size_t off, uint8_t *out) {
    if (!range_ok(sz, off, 1u)) return false;
    *out = d[off];
    return true;
}

static bool rd_u16(const uint8_t *d, size_t sz, size_t off, uint16_t *out) {
    if (!range_ok(sz, off, 2u)) return false;
    *out = (uint16_t)(((uint16_t)d[off] << 8) | (uint16_t)d[off + 1u]);
    return true;
}

static bool rd_i16(const uint8_t *d, size_t sz, size_t off, int16_t *out) {
    uint16_t u = 0;
    if (!rd_u16(d, sz, off, &u)) return false;
    *out = (int16_t)u;
    return true;
}

static bool rd_u32(const uint8_t *d, size_t sz, size_t off, uint32_t *out) {
    if (!range_ok(sz, off, 4u)) return false;
    *out = ((uint32_t)d[off] << 24) | ((uint32_t)d[off + 1u] << 16) |
           ((uint32_t)d[off + 2u] << 8) | (uint32_t)d[off + 3u];
    return true;
}

static uint32_t mk_tag(char a, char b, char c, char d) {
    return ((uint32_t)(uint8_t)a << 24) | ((uint32_t)(uint8_t)b << 16) |
           ((uint32_t)(uint8_t)c << 8) | (uint32_t)(uint8_t)d;
}

static void zero_bytes(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; ++i) b[i] = 0;
}

/* Saturate an int64_t into im_fixed (int32_t). */
static im_fixed sat_fx(int64_t v) {
    if (v > (int64_t)INT32_MAX) return INT32_MAX;
    if (v < (int64_t)INT32_MIN) return INT32_MIN;
    return (im_fixed)v;
}

/* Symmetric integer division with half-away-from-zero rounding. */
static int64_t div_round_i64(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    if (num >= 0) {
        if (num > INT64_MAX - den / 2) return num / den;
        return (num + den / 2) / den;
    } else {
        if (num < INT64_MIN + den / 2) return num / den;
        return (num - den / 2) / den;
    }
}

im_fixed im_font_scale_units(const im_font *font, int32_t units, im_fixed size_px) {
    if (!font || font->metrics.units_per_em == 0 || size_px == 0) return 0;
    int64_t num = (int64_t)units * (int64_t)size_px;
    return sat_fx(div_round_i64(num, (int64_t)font->metrics.units_per_em));
}

/* ---- CFF v1 helpers ---- */

typedef struct cff_index {
    uint32_t count;
    uint8_t off_size;
    size_t offsets_pos;
    size_t data_pos; /* 1-based offset base: item i starts at data_pos + offset[i] - 1 */
    size_t end_pos;
} cff_index;

static bool cff_read_offset(const uint8_t *d, size_t sz, size_t pos, uint8_t off_size, uint32_t *out) {
    if (off_size < 1u || off_size > 4u || !range_ok(sz, pos, off_size)) return false;
    uint32_t v = 0;
    for (uint8_t i = 0; i < off_size; ++i) {
        v = (v << 8) | (uint32_t)d[pos + i];
    }
    *out = v;
    return true;
}

static bool cff_parse_index(const uint8_t *d, size_t sz, size_t cff_base, size_t cff_end,
                            size_t pos, cff_index *out) {
    if (pos < cff_base || pos > cff_end) return false;
    uint16_t count = 0;
    if (cff_end - pos < 2u || !rd_u16(d, sz, pos, &count)) return false;
    out->count = count;
    if (count == 0) {
        out->off_size = 0;
        out->offsets_pos = pos + 2u;
        out->data_pos = pos + 2u;
        out->end_pos = pos + 2u;
        return true;
    }
    uint8_t off_size = 0;
    if (cff_end - pos < 3u || !rd_u8(d, sz, pos + 2u, &off_size)) return false;
    if (off_size < 1u || off_size > 4u) return false;
    size_t offsets_pos = pos + 3u;
    size_t off_bytes = ((size_t)count + 1u) * (size_t)off_size;
    if (off_bytes > cff_end - offsets_pos) return false;
    size_t data_pos = offsets_pos + off_bytes;
    uint32_t first_off = 0, last_off = 0;
    if (!cff_read_offset(d, sz, offsets_pos, off_size, &first_off)) return false;
    if (!cff_read_offset(d, sz, offsets_pos + (size_t)count * off_size, off_size, &last_off)) return false;
    if (first_off != 1u || last_off < 1u) return false;
    if ((size_t)(last_off - 1u) > cff_end - data_pos) return false;
    out->off_size = off_size;
    out->offsets_pos = offsets_pos;
    out->data_pos = data_pos;
    out->end_pos = data_pos + (size_t)(last_off - 1u);
    return true;
}

static bool cff_index_get(const uint8_t *d, size_t sz, const cff_index *idx, uint32_t i,
                          size_t *out_start, size_t *out_len) {
    if (i >= idx->count || idx->off_size == 0) return false;
    uint32_t o0 = 0, o1 = 0;
    size_t p0 = idx->offsets_pos + (size_t)i * idx->off_size;
    size_t p1 = idx->offsets_pos + (size_t)(i + 1u) * idx->off_size;
    if (!cff_read_offset(d, sz, p0, idx->off_size, &o0)) return false;
    if (!cff_read_offset(d, sz, p1, idx->off_size, &o1)) return false;
    if (o0 < 1u || o1 < o0) return false;
    size_t start = idx->data_pos + (size_t)(o0 - 1u);
    size_t len = (size_t)(o1 - o0);
    if (start > idx->end_pos || len > idx->end_pos - start) return false;
    *out_start = start;
    *out_len = len;
    return true;
}

static bool cff_read_dict_int(const uint8_t *d, size_t end, size_t *pos, int32_t *out) {
    if (*pos >= end) return false;
    uint8_t b0 = d[*pos];
    if (b0 >= 32u && b0 <= 246u) {
        *out = (int32_t)b0 - 139;
        (*pos) += 1u;
        return true;
    }
    if (b0 >= 247u && b0 <= 250u) {
        if (*pos + 2u > end) return false;
        *out = ((int32_t)b0 - 247) * 256 + (int32_t)d[*pos + 1u] + 108;
        (*pos) += 2u;
        return true;
    }
    if (b0 >= 251u && b0 <= 254u) {
        if (*pos + 2u > end) return false;
        *out = -((int32_t)b0 - 251) * 256 - (int32_t)d[*pos + 1u] - 108;
        (*pos) += 2u;
        return true;
    }
    if (b0 == 28u) {
        if (*pos + 3u > end) return false;
        int16_t v = (int16_t)(((uint16_t)d[*pos + 1u] << 8) | (uint16_t)d[*pos + 2u]);
        *out = (int32_t)v;
        (*pos) += 3u;
        return true;
    }
    if (b0 == 29u) {
        if (*pos + 5u > end) return false;
        uint32_t u = ((uint32_t)d[*pos + 1u] << 24) | ((uint32_t)d[*pos + 2u] << 16) |
                     ((uint32_t)d[*pos + 3u] << 8) | (uint32_t)d[*pos + 4u];
        *out = (int32_t)u;
        (*pos) += 5u;
        return true;
    }
    if (b0 == 30u) {
        /* BCD real number in DICT: consume nibbles until 0xF, return integer part (or 0). */
        (*pos) += 1u;
        bool neg = false;
        bool seen_dot = false;
        int64_t val = 0;
        while (*pos < end) {
            uint8_t byte = d[(*pos)++];
            for (int shift = 4; shift >= 0; shift -= 4) {
                uint8_t nib = (uint8_t)((byte >> shift) & 0xFu);
                if (nib == 0xFu) {
                    *out = (int32_t)(neg ? -val : val);
                    return true;
                }
                if (nib <= 9u) {
                    if (!seen_dot && val < 100000000LL) val = val * 10 + (int64_t)nib;
                } else if (nib == 0xAu || nib == 0xBu || nib == 0xCu) {
                    seen_dot = true;
                } else if (nib == 0xEu) {
                    neg = true;
                }
            }
        }
        return false;
    }
    return false;
}

static bool cff_init_tables(im_font *font) {
    const uint8_t *d = font->data;
    size_t sz = font->size;
    size_t base = font->cff_off;
    size_t end = base + font->cff_len;
    if (font->cff_len < 4u || !range_ok(sz, base, font->cff_len)) return false;

    uint8_t major = 0, minor = 0, hdr_size = 0, off_size = 0;
    if (!rd_u8(d, sz, base, &major) || !rd_u8(d, sz, base + 1u, &minor) ||
        !rd_u8(d, sz, base + 2u, &hdr_size) || !rd_u8(d, sz, base + 3u, &off_size)) return false;
    /* Only CFF v1 (major == 1) is supported; CFF2 (major == 2) is explicitly refused. */
    if (major != 1u || hdr_size < 4u || (size_t)hdr_size > font->cff_len) return false;

    cff_index name_idx, top_dict_idx, string_idx, gsubr_idx;
    if (!cff_parse_index(d, sz, base, end, base + hdr_size, &name_idx)) return false;
    if (name_idx.count == 0) return false;
    if (!cff_parse_index(d, sz, base, end, name_idx.end_pos, &top_dict_idx)) return false;
    if (top_dict_idx.count == 0) return false;
    if (!cff_parse_index(d, sz, base, end, top_dict_idx.end_pos, &string_idx)) return false;
    if (!cff_parse_index(d, sz, base, end, string_idx.end_pos, &gsubr_idx)) return false;
    font->cff_gsubrs_off = (uint32_t)(string_idx.end_pos - base);

    size_t td_start = 0, td_len = 0;
    if (!cff_index_get(d, sz, &top_dict_idx, 0, &td_start, &td_len)) return false;

    int32_t stack[48];
    uint32_t sp = 0;
    bool has_cs = false;
    uint32_t cs_off = 0;
    int32_t charstring_type = 2;
    bool has_priv = false;
    uint32_t priv_size = 0, priv_off = 0;
    bool is_cid = false;

    size_t p = td_start;
    size_t td_end = td_start + td_len;
    while (p < td_end) {
        uint8_t b = d[p];
        if (b >= 28u) {
            int32_t v = 0;
            if (!cff_read_dict_int(d, td_end, &p, &v)) return false;
            if (sp < 48u) stack[sp++] = v;
        } else {
            uint16_t op = b;
            p += 1u;
            if (b == 12u) {
                if (p >= td_end) return false;
                op = (uint16_t)(0x0C00u | d[p++]);
            }
            if (op == 17u && sp >= 1u) {
                /* CharStrings offset */
                if (stack[0] < 0) return false;
                cs_off = (uint32_t)stack[0];
                has_cs = true;
            } else if (op == 18u && sp >= 2u) {
                /* Private (size, offset) */
                if (stack[0] < 0 || stack[1] < 0) return false;
                priv_size = (uint32_t)stack[0];
                priv_off = (uint32_t)stack[1];
                has_priv = true;
            } else if (op == 0x0C06u && sp >= 1u) {
                /* CharstringType */
                charstring_type = stack[0];
            } else if (op == 0x0C1Eu) {
                /* ROS -> CID-keyed CFF font (not supported in v1 simple outline parser) */
                is_cid = true;
            }
            sp = 0;
        }
    }

    if (is_cid || charstring_type != 2 || !has_cs) return false;
    if (cs_off >= font->cff_len) return false;

    cff_index cs_idx;
    if (!cff_parse_index(d, sz, base, end, base + cs_off, &cs_idx)) return false;
    if (cs_idx.count == 0) return false;
    font->cff_charstrings_off = cs_off;

    if (has_priv && priv_size > 0) {
        if ((size_t)priv_off > font->cff_len || (size_t)priv_size > font->cff_len - priv_off) return false;
        size_t pp = base + priv_off;
        size_t pp_end = pp + priv_size;
        sp = 0;
        while (pp < pp_end) {
            uint8_t b = d[pp];
            if (b >= 28u) {
                int32_t v = 0;
                if (!cff_read_dict_int(d, pp_end, &pp, &v)) return false;
                if (sp < 48u) stack[sp++] = v;
            } else {
                uint16_t op = b;
                pp += 1u;
                if (b == 12u) {
                    if (pp >= pp_end) return false;
                    op = (uint16_t)(0x0C00u | d[pp++]);
                }
                if (op == 19u && sp >= 1u) {
                    /* Subrs offset relative to Private DICT start */
                    if (stack[0] < 0) return false;
                    size_t abs_subr = (size_t)priv_off + (size_t)stack[0];
                    if (abs_subr >= font->cff_len) return false;
                    cff_index lsubr_idx;
                    if (!cff_parse_index(d, sz, base, end, base + abs_subr, &lsubr_idx)) return false;
                    font->cff_lsubrs_off = (uint32_t)abs_subr;
                } else if (op == 20u && sp >= 1u) {
                    font->cff_default_width_x = stack[0];
                } else if (op == 21u && sp >= 1u) {
                    font->cff_nominal_width_x = stack[0];
                }
                sp = 0;
            }
        }
    }
    return true;
}

/* ---- cmap parsing ---- */

static bool parse_cmap(im_font *font) {
    const uint8_t *d = font->data;
    size_t sz = font->size;
    size_t base = font->cmap_off;
    size_t len = font->cmap_len;
    if (len < 4u || !range_ok(sz, base, len)) return false;

    uint16_t num_tables = 0;
    if (!rd_u16(d, sz, base + 2u, &num_tables)) return false;
    if (4u + (size_t)num_tables * 8u > len) return false;

    int best_rank = -1;
    uint32_t best_off = 0;
    uint16_t best_fmt = 0;

    for (uint16_t i = 0; i < num_tables; ++i) {
        size_t rec = base + 4u + (size_t)i * 8u;
        uint16_t plat = 0, enc = 0;
        uint32_t sub_rel = 0;
        if (!rd_u16(d, sz, rec, &plat) || !rd_u16(d, sz, rec + 2u, &enc) ||
            !rd_u32(d, sz, rec + 4u, &sub_rel)) return false;
        if (sub_rel >= len || len - sub_rel < 4u) continue;
        uint16_t fmt = 0;
        if (!rd_u16(d, sz, base + sub_rel, &fmt)) continue;
        uint32_t sub_len = 0;
        if (fmt == 4u) {
            uint16_t l16 = 0;
            if (!rd_u16(d, sz, base + sub_rel + 2u, &l16) || l16 < 16u) continue;
            sub_len = l16;
        } else if (fmt == 12u) {
            if (len - sub_rel < 16u || !rd_u32(d, sz, base + sub_rel + 4u, &sub_len) || sub_len < 16u) continue;
        } else {
            continue;
        }
        if ((size_t)sub_len > len - sub_rel) continue;

        int rank = -1;
        if (plat == 3u && enc == 10u && fmt == 12u) rank = 5;
        else if (plat == 0u && enc == 4u && fmt == 12u) rank = 4;
        else if (plat == 3u && enc == 1u && fmt == 4u) rank = 3;
        else if (plat == 0u && enc == 3u && fmt == 4u) rank = 2;
        else if (plat == 0u && fmt == 4u) rank = 1;

        if (rank > best_rank) {
            best_rank = rank;
            best_off = font->cmap_off + sub_rel;
            font->cmap_subtable_end = font->cmap_off + sub_rel + sub_len;
            best_fmt = fmt;
        }
    }

    if (best_rank < 0) return false;
    font->cmap_subtable_off = best_off;
    font->cmap_format = best_fmt;
    return true;
}

/* ---- GPOS kern feature discovery ---- */

/* Find the FeatureList offset for the 'kern' feature of a given ScriptTag (e.g. 'latn' or 'DFLT'). */
static bool gpos_find_script_kern(const uint8_t *d, size_t sz, size_t base, size_t len,
                                  uint16_t sl_rel, uint16_t fl_rel, uint32_t want_script,
                                  uint32_t *out_feat_off) {
    if ((size_t)sl_rel + 2u > len || (size_t)fl_rel + 2u > len) return false;
    size_t sl = base + sl_rel;
    size_t fl = base + fl_rel;
    uint16_t scount = 0, fcount = 0;
    if (!rd_u16(d, sz, sl, &scount) || !rd_u16(d, sz, fl, &fcount)) return false;
    if (2u + (size_t)scount * 6u > len - sl_rel || 2u + (size_t)fcount * 6u > len - fl_rel) return false;

    uint32_t kern_tag = mk_tag('k', 'e', 'r', 'n');
    for (uint16_t i = 0; i < scount; ++i) {
        uint32_t stag = 0;
        uint16_t srel = 0;
        if (!rd_u32(d, sz, sl + 2u + (size_t)i * 6u, &stag) ||
            !rd_u16(d, sz, sl + 6u + (size_t)i * 6u, &srel)) return false;
        if (stag != want_script) continue;
        size_t s_off = (size_t)sl_rel + (size_t)srel;
        if (s_off + 4u > len) return false;
        uint16_t dls_rel = 0;
        if (!rd_u16(d, sz, base + s_off, &dls_rel) || dls_rel == 0) return false;
        size_t ls_off = s_off + (size_t)dls_rel;
        if (ls_off + 6u > len) return false;
        uint16_t idx_count = 0;
        if (!rd_u16(d, sz, base + ls_off + 4u, &idx_count)) return false;
        if (ls_off + 6u + (size_t)idx_count * 2u > len) return false;
        for (uint16_t k = 0; k < idx_count; ++k) {
            uint16_t fidx = 0;
            if (!rd_u16(d, sz, base + ls_off + 6u + (size_t)k * 2u, &fidx) || fidx >= fcount) continue;
            uint32_t ftag = 0;
            uint16_t frel = 0;
            if (!rd_u32(d, sz, fl + 2u + (size_t)fidx * 6u, &ftag) ||
                !rd_u16(d, sz, fl + 6u + (size_t)fidx * 6u, &frel)) continue;
            if (ftag == kern_tag) {
                size_t feat_off = (size_t)fl_rel + (size_t)frel;
                if (feat_off + 4u <= len) {
                    *out_feat_off = (uint32_t)feat_off;
                    return true;
                }
            }
        }
    }
    return false;
}

static void discover_gpos_kern(im_font *font) {
    const uint8_t *d = font->data;
    size_t sz = font->size;
    size_t base = font->gpos_off;
    size_t len = font->gpos_len;
    if (len < 10u || !range_ok(sz, base, len)) return;

    uint16_t major = 0, script_list_rel = 0, feat_list_rel = 0, lookup_list_rel = 0;
    if (!rd_u16(d, sz, base, &major) || major != 1u) return;
    if (!rd_u16(d, sz, base + 4u, &script_list_rel) ||
        !rd_u16(d, sz, base + 6u, &feat_list_rel) ||
        !rd_u16(d, sz, base + 8u, &lookup_list_rel)) return;
    if ((size_t)feat_list_rel + 2u > len || (size_t)lookup_list_rel + 2u > len) return;

    uint32_t feat_off = 0;
    if (gpos_find_script_kern(d, sz, base, len, script_list_rel, feat_list_rel, mk_tag('l', 'a', 't', 'n'), &feat_off) ||
        gpos_find_script_kern(d, sz, base, len, script_list_rel, feat_list_rel, mk_tag('D', 'F', 'L', 'T'), &feat_off)) {
        font->gpos_kern_feature_off = feat_off;
        font->has_gpos_kern = true;
    }
}

/* ---- Public initialization ---- */

bool im_font_init_face(im_font *font, const uint8_t *data, size_t size, uint32_t face_index) {
    if (!font) return false;
    zero_bytes(font, sizeof(*font));
    if (!data || size < 12u) return false;

    uint32_t sfnt_ver = 0;
    if (!rd_u32(data, size, 0, &sfnt_ver)) return false;

    size_t dir_off = 0;
    if (sfnt_ver == mk_tag('t', 't', 'c', 'f')) {
        uint32_t num_fonts = 0;
        if (!rd_u32(data, size, 8u, &num_fonts)) return false;
        if (face_index >= num_fonts) return false;
        uint32_t table_dir_off = 0;
        if (!rd_u32(data, size, 12u + (size_t)face_index * 4u, &table_dir_off)) return false;
        if ((size_t)table_dir_off > size || size - (size_t)table_dir_off < 12u) return false;
        dir_off = table_dir_off;
        if (!rd_u32(data, size, dir_off, &sfnt_ver)) return false;
    } else if (face_index != 0) {
        return false;
    }

    bool is_tt = (sfnt_ver == 0x00010000u || sfnt_ver == mk_tag('t', 'r', 'u', 'e'));
    bool is_otf = (sfnt_ver == mk_tag('O', 'T', 'T', 'O'));
    if (!is_tt && !is_otf) return false;

    uint16_t num_tables = 0;
    if (!rd_u16(data, size, dir_off + 4u, &num_tables) || num_tables == 0) return false;
    if (!range_ok(size, dir_off + 12u, (size_t)num_tables * 16u)) return false;

    font->data = data;
    font->size = size;
    font->metrics.is_cff = is_otf;

    uint32_t head_off = 0, head_len = 0;
    uint32_t maxp_off = 0, maxp_len = 0;
    uint32_t hhea_off = 0, hhea_len = 0;
    uint32_t os2_off = 0, os2_len = 0;

    for (uint16_t i = 0; i < num_tables; ++i) {
        size_t rec = dir_off + 12u + (size_t)i * 16u;
        uint32_t tag = 0, off = 0, len = 0;
        if (!rd_u32(data, size, rec, &tag) || !rd_u32(data, size, rec + 8u, &off) ||
            !rd_u32(data, size, rec + 12u, &len)) {
            zero_bytes(font, sizeof(*font));
            return false;
        }
        if (!range_ok(size, off, len)) {
            zero_bytes(font, sizeof(*font));
            return false;
        }
        if (tag == mk_tag('h', 'e', 'a', 'd')) { head_off = off; head_len = len; }
        else if (tag == mk_tag('m', 'a', 'x', 'p')) { maxp_off = off; maxp_len = len; }
        else if (tag == mk_tag('h', 'h', 'e', 'a')) { hhea_off = off; hhea_len = len; }
        else if (tag == mk_tag('O', 'S', '/', '2')) { os2_off = off; os2_len = len; }
        else if (tag == mk_tag('h', 'm', 't', 'x')) { font->hmtx_off = off; font->hmtx_len = len; }
        else if (tag == mk_tag('c', 'm', 'a', 'p')) { font->cmap_off = off; font->cmap_len = len; }
        else if (tag == mk_tag('l', 'o', 'c', 'a')) { font->loca_off = off; font->loca_len = len; }
        else if (tag == mk_tag('g', 'l', 'y', 'f')) { font->glyf_off = off; font->glyf_len = len; }
        else if (tag == mk_tag('C', 'F', 'F', ' ')) { font->cff_off = off; font->cff_len = len; }
        else if (tag == mk_tag('k', 'e', 'r', 'n')) { font->kern_off = off; font->kern_len = len; }
        else if (tag == mk_tag('G', 'P', 'O', 'S')) { font->gpos_off = off; font->gpos_len = len; }
    }

    /* Validate head */
    if (head_len < 54u) { zero_bytes(font, sizeof(*font)); return false; }
    uint16_t upem = 0;
    int16_t loc_fmt = 0;
    if (!rd_u16(data, size, head_off + 18u, &upem) ||
        !rd_i16(data, size, head_off + 36u, &font->metrics.x_min) ||
        !rd_i16(data, size, head_off + 38u, &font->metrics.y_min) ||
        !rd_i16(data, size, head_off + 40u, &font->metrics.x_max) ||
        !rd_i16(data, size, head_off + 42u, &font->metrics.y_max) ||
        !rd_i16(data, size, head_off + 50u, &loc_fmt)) {
        zero_bytes(font, sizeof(*font));
        return false;
    }
    if (upem < 16u || upem > 16384u) { zero_bytes(font, sizeof(*font)); return false; }
    if (!is_otf && loc_fmt != 0 && loc_fmt != 1) { zero_bytes(font, sizeof(*font)); return false; }
    font->metrics.units_per_em = upem;
    font->index_to_loc_format = loc_fmt;

    /* Validate maxp */
    if (maxp_len < 6u) { zero_bytes(font, sizeof(*font)); return false; }
    uint16_t num_glyphs = 0;
    if (!rd_u16(data, size, maxp_off + 4u, &num_glyphs) || num_glyphs == 0) {
        zero_bytes(font, sizeof(*font));
        return false;
    }
    font->metrics.num_glyphs = num_glyphs;

    /* Validate hhea */
    if (hhea_len < 36u) { zero_bytes(font, sizeof(*font)); return false; }
    int16_t h_asc = 0, h_desc = 0, h_gap = 0;
    uint16_t num_hm = 0;
    if (!rd_i16(data, size, hhea_off + 4u, &h_asc) ||
        !rd_i16(data, size, hhea_off + 6u, &h_desc) ||
        !rd_i16(data, size, hhea_off + 8u, &h_gap) ||
        !rd_u16(data, size, hhea_off + 34u, &num_hm) || num_hm == 0 || num_hm > num_glyphs) {
        zero_bytes(font, sizeof(*font));
        return false;
    }
    font->num_hmetrics = num_hm;
    font->metrics.ascent = h_asc;
    font->metrics.descent = h_desc;
    font->metrics.line_gap = h_gap;

    /* Prefer OS/2 metrics matching DirectWrite / WPF: USE_TYPO_METRICS (bit 7) -> sTypo*, else usWin*. */
    if (os2_len >= 78u) {
        uint16_t fs_sel = 0, win_asc = 0, win_desc = 0;
        int16_t typo_asc = 0, typo_desc = 0, typo_gap = 0;
        if (rd_u16(data, size, os2_off + 62u, &fs_sel) &&
            rd_i16(data, size, os2_off + 68u, &typo_asc) &&
            rd_i16(data, size, os2_off + 70u, &typo_desc) &&
            rd_i16(data, size, os2_off + 72u, &typo_gap) &&
            rd_u16(data, size, os2_off + 74u, &win_asc) &&
            rd_u16(data, size, os2_off + 76u, &win_desc)) {
            if ((fs_sel & 0x0080u) != 0 && typo_asc > 0) {
                font->metrics.ascent = typo_asc;
                font->metrics.descent = typo_desc;
                font->metrics.line_gap = typo_gap;
            } else if (win_asc > 0 || win_desc > 0) {
                font->metrics.ascent = (int16_t)(win_asc <= 32767u ? win_asc : 32767u);
                font->metrics.descent = (int16_t)(-(int32_t)(win_desc <= 32767u ? win_desc : 32767u));
                font->metrics.line_gap = 0;
            }
        }
    }

    /* Validate hmtx size */
    size_t need_hmtx = (size_t)num_hm * 4u + (size_t)(num_glyphs - num_hm) * 2u;
    if (font->hmtx_len < need_hmtx) { zero_bytes(font, sizeof(*font)); return false; }

    /* Validate cmap */
    if (!parse_cmap(font)) { zero_bytes(font, sizeof(*font)); return false; }

    /* Validate outline tables */
    if (is_otf) {
        if (font->cff_len == 0 || !cff_init_tables(font)) {
            zero_bytes(font, sizeof(*font));
            return false;
        }
    } else {
        size_t need_loca = ((size_t)num_glyphs + 1u) * (loc_fmt == 0 ? 2u : 4u);
        if (font->loca_len < need_loca || font->glyf_len == 0) {
            zero_bytes(font, sizeof(*font));
            return false;
        }
    }

    if (font->gpos_len > 0) discover_gpos_kern(font);
    return true;
}

bool im_font_init(im_font *font, const uint8_t *data, size_t size) {
    return im_font_init_face(font, data, size, 0);
}

im_font_metrics im_font_get_metrics(const im_font *font) {
    im_font_metrics zero;
    zero_bytes(&zero, sizeof(zero));
    return font ? font->metrics : zero;
}

/* ---- cmap lookup ---- */

uint16_t im_font_glyph_index(const im_font *font, uint32_t cp) {
    if (!font || !font->data || cp > 0x10FFFFu) return 0;
    const uint8_t *d = font->data;
    size_t sz = font->cmap_subtable_end;
    if (sz > font->size) sz = font->size;
    size_t sub = font->cmap_subtable_off;
    if (sub >= sz) return 0;

    if (font->cmap_format == 12u) {
        uint32_t num_groups = 0;
        if (!rd_u32(d, sz, sub + 12u, &num_groups)) return 0;
        size_t groups_off = sub + 16u;
        if (!range_ok(sz, groups_off, (size_t)num_groups * 12u)) return 0;
        uint32_t lo = 0, hi = num_groups;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            size_t rec = groups_off + (size_t)mid * 12u;
            uint32_t start_cp = 0, end_cp = 0, start_gid = 0;
            if (!rd_u32(d, sz, rec, &start_cp) || !rd_u32(d, sz, rec + 4u, &end_cp) ||
                !rd_u32(d, sz, rec + 8u, &start_gid)) return 0;
            if (cp < start_cp) hi = mid;
            else if (cp > end_cp) lo = mid + 1u;
            else {
                uint32_t gid = start_gid + (cp - start_cp);
                return (gid < font->metrics.num_glyphs) ? (uint16_t)gid : 0u;
            }
        }
        return 0;
    }

    if (font->cmap_format == 4u) {
        if (cp > 0xFFFFu) return 0;
        uint16_t seg_count_x2 = 0;
        if (!rd_u16(d, sz, sub + 6u, &seg_count_x2) || seg_count_x2 < 2u || (seg_count_x2 & 1u) != 0) return 0;
        uint16_t seg_count = (uint16_t)(seg_count_x2 / 2u);
        size_t end_codes = sub + 14u;
        size_t start_codes = end_codes + (size_t)seg_count_x2 + 2u;
        size_t id_deltas = start_codes + (size_t)seg_count_x2;
        size_t id_range_offs = id_deltas + (size_t)seg_count_x2;
        if (!range_ok(sz, end_codes, (size_t)seg_count_x2 * 4u + 2u)) return 0;

        uint16_t lo = 0, hi = seg_count;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            uint16_t end_c = 0;
            if (!rd_u16(d, sz, end_codes + (size_t)mid * 2u, &end_c)) return 0;
            if ((uint16_t)cp > end_c) lo = (uint16_t)(mid + 1u);
            else hi = mid;
        }
        if (lo >= seg_count) return 0;
        uint16_t start_c = 0, end_c = 0, range_off = 0;
        int16_t delta = 0;
        if (!rd_u16(d, sz, start_codes + (size_t)lo * 2u, &start_c) ||
            !rd_u16(d, sz, end_codes + (size_t)lo * 2u, &end_c) ||
            !rd_i16(d, sz, id_deltas + (size_t)lo * 2u, &delta) ||
            !rd_u16(d, sz, id_range_offs + (size_t)lo * 2u, &range_off)) return 0;
        if ((uint16_t)cp < start_c || (uint16_t)cp > end_c) return 0;

        uint16_t gid = 0;
        if (range_off == 0) {
            gid = (uint16_t)((cp + (uint32_t)(uint16_t)delta) & 0xFFFFu);
        } else {
            size_t glyph_addr = id_range_offs + (size_t)lo * 2u + (size_t)range_off +
                                (size_t)((uint16_t)cp - start_c) * 2u;
            if (!rd_u16(d, sz, glyph_addr, &gid)) return 0;
            if (gid != 0) gid = (uint16_t)((gid + (uint16_t)delta) & 0xFFFFu);
        }
        return (gid < font->metrics.num_glyphs) ? gid : 0u;
    }

    return 0;
}

/* ---- TrueType loca helper ---- */

static bool tt_glyph_range(const im_font *font, uint16_t gid, size_t *out_off, size_t *out_len) {
    if (gid >= font->metrics.num_glyphs || font->metrics.is_cff) return false;
    const uint8_t *d = font->data;
    size_t sz = font->size;
    uint32_t o0 = 0, o1 = 0;
    if (font->index_to_loc_format == 0) {
        uint16_t a = 0, b = 0;
        if (!rd_u16(d, sz, font->loca_off + (size_t)gid * 2u, &a) ||
            !rd_u16(d, sz, font->loca_off + (size_t)(gid + 1u) * 2u, &b)) return false;
        o0 = (uint32_t)a * 2u;
        o1 = (uint32_t)b * 2u;
    } else {
        if (!rd_u32(d, sz, font->loca_off + (size_t)gid * 4u, &o0) ||
            !rd_u32(d, sz, font->loca_off + (size_t)(gid + 1u) * 4u, &o1)) return false;
    }
    if (o1 < o0 || o1 > font->glyf_len) return false;
    *out_off = font->glyf_off + (size_t)o0;
    *out_len = (size_t)(o1 - o0);
    return true;
}

/* Forward declaration for exact CFF glyph bounds & empty-glyph inspection. */
static bool cff_measure_glyph(const im_font *font, uint16_t gid, im_glyph_metrics *out);

/* ---- Glyph metrics ---- */

bool im_font_glyph_metrics(const im_font *font, uint16_t glyph_id, im_glyph_metrics *out) {
    if (!out) return false;
    zero_bytes(out, sizeof(*out));
    if (!font || !font->data || glyph_id >= font->metrics.num_glyphs) return false;

    const uint8_t *d = font->data;
    size_t sz = font->size;
    if (glyph_id < font->num_hmetrics) {
        if (!rd_u16(d, sz, font->hmtx_off + (size_t)glyph_id * 4u, &out->advance_width) ||
            !rd_i16(d, sz, font->hmtx_off + (size_t)glyph_id * 4u + 2u, &out->left_side_bearing)) {
            zero_bytes(out, sizeof(*out));
            return false;
        }
    } else {
        size_t last_off = font->hmtx_off + (size_t)(font->num_hmetrics - 1u) * 4u;
        size_t lsb_off = font->hmtx_off + (size_t)font->num_hmetrics * 4u +
                         (size_t)(glyph_id - font->num_hmetrics) * 2u;
        if (!rd_u16(d, sz, last_off, &out->advance_width) ||
            !rd_i16(d, sz, lsb_off, &out->left_side_bearing)) {
            zero_bytes(out, sizeof(*out));
            return false;
        }
    }

    if (!font->metrics.is_cff) {
        size_t goff = 0, glen = 0;
        if (!tt_glyph_range(font, glyph_id, &goff, &glen)) {
            zero_bytes(out, sizeof(*out));
            return false;
        }
        if (glen == 0) {
            out->is_empty = true;
            return true;
        }
        if (glen < 10u) {
            zero_bytes(out, sizeof(*out));
            return false;
        }
        if (!rd_i16(d, sz, goff + 2u, &out->x_min) ||
            !rd_i16(d, sz, goff + 4u, &out->y_min) ||
            !rd_i16(d, sz, goff + 6u, &out->x_max) ||
            !rd_i16(d, sz, goff + 8u, &out->y_max)) {
            zero_bytes(out, sizeof(*out));
            return false;
        }
        out->is_empty = false;
    } else {
        if (!cff_measure_glyph(font, glyph_id, out)) {
            zero_bytes(out, sizeof(*out));
            return false;
        }
    }
    return true;
}

/* ---- Kerning: GPOS kern PairPos (Format 1 & 2) with legacy kern Format 0 fallback ---- */

static int32_t gpos_coverage_index(const uint8_t *d, size_t sz, size_t cov_off, size_t table_end, uint16_t gid) {
    if (cov_off + 4u > table_end) return -1;
    uint16_t fmt = 0, count = 0;
    if (!rd_u16(d, sz, cov_off, &fmt) || !rd_u16(d, sz, cov_off + 2u, &count)) return -1;
    if (fmt == 1u) {
        if (cov_off + 4u + (size_t)count * 2u > table_end) return -1;
        uint16_t lo = 0, hi = count;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            uint16_t g = 0;
            if (!rd_u16(d, sz, cov_off + 4u + (size_t)mid * 2u, &g)) return -1;
            if (gid < g) hi = mid;
            else if (gid > g) lo = (uint16_t)(mid + 1u);
            else return (int32_t)mid;
        }
        return -1;
    }
    if (fmt == 2u) {
        if (cov_off + 4u + (size_t)count * 6u > table_end) return -1;
        uint16_t lo = 0, hi = count;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            size_t rec = cov_off + 4u + (size_t)mid * 6u;
            uint16_t s = 0, e = 0, idx = 0;
            if (!rd_u16(d, sz, rec, &s) || !rd_u16(d, sz, rec + 2u, &e) || !rd_u16(d, sz, rec + 4u, &idx)) return -1;
            if (gid < s) hi = mid;
            else if (gid > e) lo = (uint16_t)(mid + 1u);
            else return (int32_t)idx + (int32_t)(gid - s);
        }
        return -1;
    }
    return -1;
}

static uint16_t gpos_class_def(const uint8_t *d, size_t sz, size_t cd_off, size_t table_end, uint16_t gid) {
    if (cd_off + 4u > table_end) return 0;
    uint16_t fmt = 0;
    if (!rd_u16(d, sz, cd_off, &fmt)) return 0;
    if (fmt == 1u) {
        uint16_t start = 0, count = 0;
        if (cd_off + 6u > table_end || !rd_u16(d, sz, cd_off + 2u, &start) || !rd_u16(d, sz, cd_off + 4u, &count)) return 0;
        if (gid < start || gid >= (uint32_t)start + count) return 0;
        size_t pos = cd_off + 6u + (size_t)(gid - start) * 2u;
        if (pos + 2u > table_end) return 0;
        uint16_t cls = 0;
        return rd_u16(d, sz, pos, &cls) ? cls : 0u;
    }
    if (fmt == 2u) {
        uint16_t count = 0;
        if (!rd_u16(d, sz, cd_off + 2u, &count)) return 0;
        if (cd_off + 4u + (size_t)count * 6u > table_end) return 0;
        uint16_t lo = 0, hi = count;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            size_t rec = cd_off + 4u + (size_t)mid * 6u;
            uint16_t s = 0, e = 0, cls = 0;
            if (!rd_u16(d, sz, rec, &s) || !rd_u16(d, sz, rec + 2u, &e) || !rd_u16(d, sz, rec + 4u, &cls)) return 0;
            if (gid < s) hi = mid;
            else if (gid > e) lo = (uint16_t)(mid + 1u);
            else return cls;
        }
        return 0;
    }
    return 0;
}

static size_t gpos_value_record_size(uint16_t vf) {
    size_t n = 0;
    for (uint32_t bit = 0; bit < 8u; ++bit) {
        if ((vf & (1u << bit)) != 0) n += 2u;
    }
    return n;
}

static int32_t gpos_read_x_advance(const uint8_t *d, size_t sz, size_t pos, size_t table_end, uint16_t vf) {
    if ((vf & 0x0004u) == 0) return 0;
    size_t off = 0;
    if (vf & 0x0001u) off += 2u;
    if (vf & 0x0002u) off += 2u;
    if (pos + off + 2u > table_end) return 0;
    int16_t v = 0;
    return rd_i16(d, sz, pos + off, &v) ? (int32_t)v : 0;
}

static int32_t gpos_read_x_placement(const uint8_t *d, size_t sz, size_t pos, size_t table_end, uint16_t vf) {
    if ((vf & 0x0001u) == 0 || pos + 2u > table_end) return 0;
    int16_t v = 0;
    return rd_i16(d, sz, pos, &v) ? (int32_t)v : 0;
}

static int32_t gpos_read_pair_horiz(const uint8_t *d, size_t sz, size_t pos1, size_t sz1,
                                    size_t table_end, uint16_t vf1, uint16_t vf2) {
    int32_t a1 = gpos_read_x_advance(d, sz, pos1, table_end, vf1);
    int32_t p2 = gpos_read_x_placement(d, sz, pos1 + sz1, table_end, vf2);
    int32_t a2 = gpos_read_x_advance(d, sz, pos1 + sz1, table_end, vf2);
    return a1 + p2 + a2;
}

static bool gpos_pair_pos_subtable(const uint8_t *d, size_t sz, size_t st_off, size_t table_end,
                                   uint16_t left, uint16_t right, int32_t *out_adj) {
    if (st_off + 10u > table_end) return false;
    uint16_t fmt = 0, cov_rel = 0, vf1 = 0, vf2 = 0;
    if (!rd_u16(d, sz, st_off, &fmt) || !rd_u16(d, sz, st_off + 2u, &cov_rel) ||
        !rd_u16(d, sz, st_off + 4u, &vf1) || !rd_u16(d, sz, st_off + 6u, &vf2)) return false;
    int32_t cov_idx = gpos_coverage_index(d, sz, st_off + cov_rel, table_end, left);
    if (cov_idx < 0) return false;

    size_t sz1 = gpos_value_record_size(vf1);
    size_t sz2 = gpos_value_record_size(vf2);

    if (fmt == 1u) {
        uint16_t ps_count = 0;
        if (!rd_u16(d, sz, st_off + 8u, &ps_count) || (uint32_t)cov_idx >= ps_count) return false;
        uint16_t ps_rel = 0;
        if (st_off + 10u + (size_t)cov_idx * 2u + 2u > table_end) return false;
        if (!rd_u16(d, sz, st_off + 10u + (size_t)cov_idx * 2u, &ps_rel)) return false;
        size_t ps_off = st_off + ps_rel;
        if (ps_off + 2u > table_end) return false;
        uint16_t pv_count = 0;
        if (!rd_u16(d, sz, ps_off, &pv_count)) return false;
        size_t rec_sz = 2u + sz1 + sz2;
        if (ps_off + 2u + (size_t)pv_count * rec_sz > table_end) return false;
        uint16_t lo = 0, hi = pv_count;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            size_t rec = ps_off + 2u + (size_t)mid * rec_sz;
            uint16_t g2 = 0;
            if (!rd_u16(d, sz, rec, &g2)) return false;
            if (right < g2) hi = mid;
            else if (right > g2) lo = (uint16_t)(mid + 1u);
            else {
                *out_adj = gpos_read_pair_horiz(d, sz, rec + 2u, sz1, table_end, vf1, vf2);
                return true;
            }
        }
        return false;
    }

    if (fmt == 2u) {
        if (st_off + 16u > table_end) return false;
        uint16_t cd1_rel = 0, cd2_rel = 0, c1_cnt = 0, c2_cnt = 0;
        if (!rd_u16(d, sz, st_off + 8u, &cd1_rel) || !rd_u16(d, sz, st_off + 10u, &cd2_rel) ||
            !rd_u16(d, sz, st_off + 12u, &c1_cnt) || !rd_u16(d, sz, st_off + 14u, &c2_cnt)) return false;
        uint16_t c1 = gpos_class_def(d, sz, st_off + cd1_rel, table_end, left);
        uint16_t c2 = gpos_class_def(d, sz, st_off + cd2_rel, table_end, right);
        if (c1 >= c1_cnt || c2 >= c2_cnt) return false;
        size_t rec_sz = sz1 + sz2;
        size_t idx = (size_t)c1 * (size_t)c2_cnt + (size_t)c2;
        size_t rec = st_off + 16u + idx * rec_sz;
        if (rec + rec_sz > table_end) return false;
        *out_adj = gpos_read_pair_horiz(d, sz, rec, sz1, table_end, vf1, vf2);
        return true;
    }
    return false;
}

static int32_t gpos_kern_lookup(const im_font *font, uint16_t left, uint16_t right) {
    const uint8_t *d = font->data;
    size_t sz = font->size;
    size_t base = font->gpos_off;
    size_t table_end = base + font->gpos_len;
    uint16_t ll_rel = 0;
    if (!rd_u16(d, sz, base + 8u, &ll_rel)) return 0;
    size_t ll = base + ll_rel;
    uint16_t lookup_count = 0;
    if (ll + 2u > table_end || !rd_u16(d, sz, ll, &lookup_count)) return 0;

    size_t feat = base + font->gpos_kern_feature_off;
    uint16_t feat_lookups = 0;
    if (feat + 4u > table_end || !rd_u16(d, sz, feat + 2u, &feat_lookups)) return 0;
    if (feat + 4u + (size_t)feat_lookups * 2u > table_end) return 0;

    int32_t total = 0;
    for (uint16_t i = 0; i < feat_lookups; ++i) {
        uint16_t lidx = 0;
        if (!rd_u16(d, sz, feat + 4u + (size_t)i * 2u, &lidx) || lidx >= lookup_count) continue;
        uint16_t lrel = 0;
        if (ll + 2u + (size_t)lidx * 2u + 2u > table_end) continue;
        if (!rd_u16(d, sz, ll + 2u + (size_t)lidx * 2u, &lrel)) continue;
        size_t loff = ll + lrel;
        uint16_t ltype = 0, st_count = 0;
        if (loff + 6u > table_end || !rd_u16(d, sz, loff, &ltype) || !rd_u16(d, sz, loff + 4u, &st_count)) continue;
        if (loff + 6u + (size_t)st_count * 2u > table_end) continue;

        for (uint16_t s = 0; s < st_count; ++s) {
            uint16_t srel = 0;
            if (!rd_u16(d, sz, loff + 6u + (size_t)s * 2u, &srel)) continue;
            size_t st = loff + srel;
            uint16_t eff_type = ltype;
            if (eff_type == 9u) {
                /* Extension Positioning subtable */
                uint16_t ext_fmt = 0, ext_type = 0;
                uint32_t ext_off = 0;
                if (st + 8u > table_end || !rd_u16(d, sz, st, &ext_fmt) || ext_fmt != 1u ||
                    !rd_u16(d, sz, st + 2u, &ext_type) || !rd_u32(d, sz, st + 4u, &ext_off)) continue;
                if (st + (size_t)ext_off > table_end) continue;
                st += (size_t)ext_off;
                eff_type = ext_type;
            }
            if (eff_type == 2u) {
                int32_t adj = 0;
                if (gpos_pair_pos_subtable(d, sz, st, table_end, left, right, &adj)) {
                    total += adj;
                    break; /* First matching subtable per lookup wins per OpenType spec. */
                }
            }
        }
    }
    return total;
}

static int32_t legacy_kern_lookup(const im_font *font, uint16_t left, uint16_t right) {
    const uint8_t *d = font->data;
    size_t sz = font->size;
    size_t base = font->kern_off;
    size_t end = base + font->kern_len;
    if (font->kern_len < 4u || !range_ok(sz, base, font->kern_len)) return 0;

    uint16_t ver = 0, ntables = 0;
    if (!rd_u16(d, sz, base, &ver) || ver != 0 || !rd_u16(d, sz, base + 2u, &ntables)) return 0;

    size_t pos = base + 4u;
    uint32_t key = ((uint32_t)left << 16) | (uint32_t)right;
    int32_t total = 0;
    for (uint16_t t = 0; t < ntables; ++t) {
        if (pos + 6u > end) break;
        uint16_t slen = 0, cov = 0;
        if (!rd_u16(d, sz, pos + 2u, &slen) || !rd_u16(d, sz, pos + 4u, &cov)) break;
        if (slen < 6u || pos + slen > end) break;
        uint8_t fmt = (uint8_t)(cov >> 8);
        /* Require horizontal (bit 0), not minimum (bit 1), not cross-stream (bit 2). */
        if (fmt == 0 && (cov & 0x0007u) == 0x0001u && slen >= 14u) {
            bool is_override = (cov & 0x0008u) != 0;
            uint16_t npairs = 0;
            if (rd_u16(d, sz, pos + 6u, &npairs) && pos + 14u + (size_t)npairs * 6u <= pos + slen) {
                uint16_t lo = 0, hi = npairs;
                while (lo < hi) {
                    uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
                    size_t rec = pos + 14u + (size_t)mid * 6u;
                    uint32_t k = 0;
                    if (!rd_u32(d, sz, rec, &k)) break;
                    if (key < k) hi = mid;
                    else if (key > k) lo = (uint16_t)(mid + 1u);
                    else {
                        int16_t v = 0;
                        if (rd_i16(d, sz, rec + 4u, &v)) {
                            total = is_override ? (int32_t)v : (total + (int32_t)v);
                        }
                        break;
                    }
                }
            }
        }
        pos += slen;
    }
    return total;
}

int32_t im_font_kerning(const im_font *font, uint16_t left_glyph, uint16_t right_glyph) {
    if (!font || !font->data) return 0;
    if (left_glyph >= font->metrics.num_glyphs || right_glyph >= font->metrics.num_glyphs) return 0;
    /* Use GPOS kern when present; fall back to legacy kern only if GPOS kern is absent (never double-apply). */
    if (font->has_gpos_kern) return gpos_kern_lookup(font, left_glyph, right_glyph);
    if (font->kern_len > 0) return legacy_kern_lookup(font, left_glyph, right_glyph);
    return 0;
}

/* ---- TrueType outline extraction (simple + composite with point attachment) ---- */

typedef struct tt_point {
    int64_t x; /* 14-bit fractional font units (16384 == 1 font unit) */
    int64_t y;
    uint8_t on_curve;
} tt_point;

typedef struct tt_xform {
    int32_t a, b, c, d; /* 2.14 fixed point (16384 == 1.0) */
    int32_t dx, dy;     /* font units */
} tt_xform;

typedef struct tt_buf {
    im_allocator alloc;
    tt_point *pts;
    uint32_t pt_count;
    uint32_t pt_cap;
    uint32_t *contour_ends;
    uint32_t contour_count;
    uint32_t contour_cap;
    uint32_t components;
    uint32_t ops;
} tt_buf;

static bool tt_buf_reserve_pts(tt_buf *b, uint32_t extra) {
    if (extra > IM_FONT_MAX_POINTS || b->pt_count > IM_FONT_MAX_POINTS - extra) return false;
    uint32_t need = b->pt_count + extra;
    if (need <= b->pt_cap) return true;
    uint32_t ncap = b->pt_cap ? b->pt_cap * 2u : 64u;
    while (ncap < need) ncap *= 2u;
    if (ncap > IM_FONT_MAX_POINTS) ncap = IM_FONT_MAX_POINTS;
    tt_point *np = (tt_point *)b->alloc.realloc(b->alloc.ctx, b->pts,
                                                (size_t)b->pt_cap * sizeof(tt_point),
                                                (size_t)ncap * sizeof(tt_point));
    if (!np) return false;
    b->pts = np;
    b->pt_cap = ncap;
    return true;
}

static bool tt_buf_reserve_contours(tt_buf *b, uint32_t extra) {
    if (extra > IM_FONT_MAX_POINTS || b->contour_count > IM_FONT_MAX_POINTS - extra) return false;
    uint32_t need = b->contour_count + extra;
    if (need <= b->contour_cap) return true;
    uint32_t ncap = b->contour_cap ? b->contour_cap * 2u : 16u;
    while (ncap < need) ncap *= 2u;
    if (ncap > IM_FONT_MAX_POINTS) ncap = IM_FONT_MAX_POINTS;
    uint32_t *nc = (uint32_t *)b->alloc.realloc(b->alloc.ctx, b->contour_ends,
                                                (size_t)b->contour_cap * sizeof(uint32_t),
                                                (size_t)ncap * sizeof(uint32_t));
    if (!nc) return false;
    b->contour_ends = nc;
    b->contour_cap = ncap;
    return true;
}

static void tt_buf_destroy(tt_buf *b) {
    if (b->pts) b->alloc.free(b->alloc.ctx, b->pts, (size_t)b->pt_cap * sizeof(tt_point));
    if (b->contour_ends) b->alloc.free(b->alloc.ctx, b->contour_ends, (size_t)b->contour_cap * sizeof(uint32_t));
    b->pts = NULL;
    b->contour_ends = NULL;
    b->pt_count = b->pt_cap = b->contour_count = b->contour_cap = 0;
}

static bool tt_decode_glyph(const im_font *font, uint16_t gid, tt_buf *out, uint32_t depth) {
    if (depth > IM_FONT_MAX_DEPTH) return false;
    size_t goff = 0, glen = 0;
    if (!tt_glyph_range(font, gid, &goff, &glen)) return false;
    if (glen == 0) return true;
    if (glen < 10u) return false;

    const uint8_t *d = font->data;
    size_t sz = font->size;
    size_t gend = goff + glen;

    int16_t num_contours = 0;
    if (!rd_i16(d, sz, goff, &num_contours)) return false;

    if (num_contours == 0) return true;

    if (num_contours > 0) {
        uint32_t nc = (uint32_t)num_contours;
        size_t pos = goff + 10u;
        if (pos + (size_t)nc * 2u + 2u > gend) return false;

        uint32_t base_pt = out->pt_count;
        if (!tt_buf_reserve_contours(out, nc)) return false;

        uint16_t last_end = 0;
        for (uint32_t c = 0; c < nc; ++c) {
            uint16_t e = 0;
            if (!rd_u16(d, sz, pos, &e)) return false;
            pos += 2u;
            if (c > 0 && e < last_end) return false;
            last_end = e;
            out->contour_ends[out->contour_count++] = base_pt + (uint32_t)e;
        }
        uint32_t npts = (uint32_t)last_end + 1u;
        if (!tt_buf_reserve_pts(out, npts)) return false;

        uint16_t instr_len = 0;
        if (!rd_u16(d, sz, pos, &instr_len)) return false;
        pos += 2u;
        if (pos + instr_len > gend) return false;
        pos += instr_len;

        /* Flags */
        uint32_t p = 0;
        while (p < npts) {
            if (pos >= gend) return false;
            uint8_t fl = d[pos++];
            uint32_t rep = 1u;
            if (fl & 0x08u) {
                if (pos >= gend) return false;
                rep += (uint32_t)d[pos++];
            }
            if (rep > npts - p) return false;
            for (uint32_t r = 0; r < rep; ++r) {
                out->pts[base_pt + p].on_curve = fl;
                out->pts[base_pt + p].x = 0;
                out->pts[base_pt + p].y = 0;
                ++p;
            }
        }

        /* X coordinates (stored in 14-bit fractional font units) */
        int64_t cx = 0;
        for (uint32_t i = 0; i < npts; ++i) {
            uint8_t fl = out->pts[base_pt + i].on_curve;
            if (fl & 0x02u) {
                if (pos >= gend) return false;
                int64_t dx = (int64_t)d[pos++];
                cx += (fl & 0x10u) ? dx : -dx;
            } else if (!(fl & 0x10u)) {
                int16_t dx = 0;
                if (pos + 2u > gend || !rd_i16(d, sz, pos, &dx)) return false;
                pos += 2u;
                cx += (int64_t)dx;
            }
            out->pts[base_pt + i].x = cx * 16384;
        }

        /* Y coordinates (stored in 14-bit fractional font units) */
        int64_t cy = 0;
        for (uint32_t i = 0; i < npts; ++i) {
            uint8_t fl = out->pts[base_pt + i].on_curve;
            if (fl & 0x04u) {
                if (pos >= gend) return false;
                int64_t dy = (int64_t)d[pos++];
                cy += (fl & 0x20u) ? dy : -dy;
            } else if (!(fl & 0x20u)) {
                int16_t dy = 0;
                if (pos + 2u > gend || !rd_i16(d, sz, pos, &dy)) return false;
                pos += 2u;
                cy += (int64_t)dy;
            }
            out->pts[base_pt + i].y = cy * 16384;
            out->pts[base_pt + i].on_curve = (uint8_t)(fl & 0x01u);
        }

        out->pt_count += npts;
        return true;
    }

    /* Composite glyph (num_contours < 0) */
    size_t pos = goff + 10u;
    uint32_t parent_start_pt = out->pt_count;
    for (;;) {
        if (++out->components > IM_FONT_MAX_COMPONENTS) return false;
        uint16_t flags = 0, comp_gid = 0;
        if (pos + 4u > gend || !rd_u16(d, sz, pos, &flags) || !rd_u16(d, sz, pos + 2u, &comp_gid)) return false;
        pos += 4u;
        if (comp_gid >= font->metrics.num_glyphs) return false;

        int32_t arg1 = 0, arg2 = 0;
        if (flags & 0x0001u) { /* ARG_1_AND_2_ARE_WORDS */
            if (pos + 4u > gend) return false;
            if (flags & 0x0002u) { /* ARGS_ARE_XY_VALUES (signed) */
                int16_t a1 = 0, a2 = 0;
                if (!rd_i16(d, sz, pos, &a1) || !rd_i16(d, sz, pos + 2u, &a2)) return false;
                arg1 = a1; arg2 = a2;
            } else { /* Point indices (unsigned) */
                uint16_t a1 = 0, a2 = 0;
                if (!rd_u16(d, sz, pos, &a1) || !rd_u16(d, sz, pos + 2u, &a2)) return false;
                arg1 = a1; arg2 = a2;
            }
            pos += 4u;
        } else {
            if (pos + 2u > gend) return false;
            if (flags & 0x0002u) {
                arg1 = (int8_t)d[pos];
                arg2 = (int8_t)d[pos + 1u];
            } else {
                arg1 = (uint8_t)d[pos];
                arg2 = (uint8_t)d[pos + 1u];
            }
            pos += 2u;
        }

        tt_xform xf = { 16384, 0, 0, 16384, 0, 0 };
        if (flags & 0x0008u) { /* WE_HAVE_A_SCALE */
            int16_t s = 0;
            if (pos + 2u > gend || !rd_i16(d, sz, pos, &s)) return false;
            pos += 2u;
            xf.a = s; xf.d = s;
        } else if (flags & 0x0040u) { /* WE_HAVE_AN_X_AND_Y_SCALE */
            int16_t sx = 0, sy = 0;
            if (pos + 4u > gend || !rd_i16(d, sz, pos, &sx) || !rd_i16(d, sz, pos + 2u, &sy)) return false;
            pos += 4u;
            xf.a = sx; xf.d = sy;
        } else if (flags & 0x0080u) { /* WE_HAVE_A_TWO_BY_TWO */
            int16_t a = 0, b = 0, c = 0, dd = 0;
            if (pos + 8u > gend || !rd_i16(d, sz, pos, &a) || !rd_i16(d, sz, pos + 2u, &b) ||
                !rd_i16(d, sz, pos + 4u, &c) || !rd_i16(d, sz, pos + 6u, &dd)) return false;
            pos += 8u;
            xf.a = a; xf.b = b; xf.c = c; xf.d = dd;
        }

        uint32_t comp_start_pt = out->pt_count;
        if (!tt_decode_glyph(font, comp_gid, out, depth + 1u)) return false;
        uint32_t comp_end_pt = out->pt_count;

        /* Apply linear 2x2 transform keeping 14-bit fractional font-unit precision */
        for (uint32_t i = comp_start_pt; i < comp_end_pt; ++i) {
            __int128 px = out->pts[i].x;
            __int128 py = out->pts[i].y;
            __int128 tx_num = px * xf.a + py * xf.c;
            __int128 ty_num = px * xf.b + py * xf.d;
            __int128 tx = (tx_num >= 0) ? ((tx_num + 8192) >> 14) : -(((-tx_num) + 8192) >> 14);
            __int128 ty = (ty_num >= 0) ? ((ty_num + 8192) >> 14) : -(((-ty_num) + 8192) >> 14);
            if (tx > (int64_t)1 << 46 || tx < -((int64_t)1 << 46) ||
                ty > (int64_t)1 << 46 || ty < -((int64_t)1 << 46)) return false;
            out->pts[i].x = (int64_t)tx;
            out->pts[i].y = (int64_t)ty;
        }

        /* Apply translation: either XY offset or point-matching attachment (refusing invalid indices) */
        int64_t dx = 0, dy = 0;
        if (flags & 0x0002u) {
            if (flags & 0x0800u) { /* SCALED_COMPONENT_OFFSET */
                dx = (int64_t)arg1 * xf.a + (int64_t)arg2 * xf.c;
                dy = (int64_t)arg1 * xf.b + (int64_t)arg2 * xf.d;
            } else {
                dx = (int64_t)arg1 * 16384;
                dy = (int64_t)arg2 * 16384;
            }
        } else {
            uint32_t p_idx = parent_start_pt + (uint32_t)arg1;
            uint32_t c_idx = comp_start_pt + (uint32_t)arg2;
            if (p_idx >= comp_start_pt || c_idx >= comp_end_pt) return false;
            dx = out->pts[p_idx].x - out->pts[c_idx].x;
            dy = out->pts[p_idx].y - out->pts[c_idx].y;
        }
        if (dx != 0 || dy != 0) {
            for (uint32_t i = comp_start_pt; i < comp_end_pt; ++i) {
                out->pts[i].x += dx;
                out->pts[i].y += dy;
            }
        }

        if (!(flags & 0x0020u)) break; /* MORE_COMPONENTS */
    }
    return true;
}

static im_fixed tt_scale_q14(const im_font *font, int64_t q14_val, im_fixed size_px) {
    int64_t upem = (int64_t)font->metrics.units_per_em;
    if (upem <= 0 || size_px == 0) return 0;
    __int128 prod = (__int128)q14_val * (__int128)size_px;
    __int128 shifted = (prod >= 0) ? ((prod + 8192) >> 14) : -(((-prod) + 8192) >> 14);
    if (shifted > INT64_MAX / 2) shifted = INT64_MAX / 2;
    if (shifted < INT64_MIN / 2) shifted = INT64_MIN / 2;
    return sat_fx(div_round_i64((int64_t)shifted, upem));
}

static bool tt_emit_path(const im_font *font, const tt_buf *b, im_fixed size_px, im_path *path) {
    uint32_t start = 0;
    for (uint32_t c = 0; c < b->contour_count; ++c) {
        uint32_t end = b->contour_ends[c];
        if (end >= b->pt_count || end < start) return false;
        uint32_t n = end - start + 1u;
        if (n < 2u) { start = end + 1u; continue; }

        int64_t sx = 0, sy = 0;
        uint32_t first_idx = 0;
        const tt_point *p0 = &b->pts[start];
        const tt_point *plast = &b->pts[end];
        if (p0->on_curve) {
            sx = p0->x; sy = p0->y;
            first_idx = 1u;
        } else if (plast->on_curve) {
            sx = plast->x; sy = plast->y;
            first_idx = 0u;
        } else {
            sx = (p0->x + plast->x) / 2;
            sy = (p0->y + plast->y) / 2;
            first_idx = 0u;
        }

        if (!im_path_move_to(path, tt_scale_q14(font, sx, size_px), tt_scale_q14(font, -sy, size_px))) return false;

        uint32_t count = (p0->on_curve) ? (n - 1u) : n;
        for (uint32_t step = 0; step < count; ++step) {
            uint32_t idx = start + ((first_idx + step) % n);
            const tt_point *pt = &b->pts[idx];
            if (pt->on_curve) {
                if (!im_path_line_to(path, tt_scale_q14(font, pt->x, size_px), tt_scale_q14(font, -pt->y, size_px))) return false;
            } else {
                im_fixed cx = tt_scale_q14(font, pt->x, size_px);
                im_fixed cy = tt_scale_q14(font, -pt->y, size_px);
                if (step + 1u < count) {
                    uint32_t nidx = start + ((first_idx + step + 1u) % n);
                    const tt_point *npt = &b->pts[nidx];
                    if (npt->on_curve) {
                        if (!im_path_quad_to(path, cx, cy,
                                             tt_scale_q14(font, npt->x, size_px),
                                             tt_scale_q14(font, -npt->y, size_px))) return false;
                        ++step;
                    } else {
                        int64_t mx = (pt->x + npt->x) / 2;
                        int64_t my = (pt->y + npt->y) / 2;
                        if (!im_path_quad_to(path, cx, cy,
                                             tt_scale_q14(font, mx, size_px),
                                             tt_scale_q14(font, -my, size_px))) return false;
                    }
                } else {
                    if (!im_path_quad_to(path, cx, cy,
                                         tt_scale_q14(font, sx, size_px),
                                         tt_scale_q14(font, -sy, size_px))) return false;
                }
            }
        }
        if (!im_path_close(path)) return false;
        start = end + 1u;
    }
    return true;
}

/* ---- OpenType CFF v1 Type 2 charstring interpreter (fixed-point 16.16) ---- */

typedef struct cff_vm {
    const im_font *font;
    im_fixed size_px;
    im_path *path; /* NULL when measuring bounds only */
    cff_index gsubrs;
    cff_index lsubrs;
    bool has_lsubrs;
    int32_t stack[48]; /* 16.16 fixed point font units */
    uint32_t sp;
    int64_t x, y;      /* current point in 16.16 font units */
    bool open;
    bool have_width;
    bool ended;
    bool has_ink;
    int64_t min_x, min_y, max_x, max_y; /* 16.16 font units */
    uint32_t num_stems;
    uint32_t ops;
    uint32_t depth;
} cff_vm;

static void cff_track_pt(cff_vm *vm, int64_t px, int64_t py) {
    if (!vm->has_ink) {
        vm->has_ink = true;
        vm->min_x = vm->max_x = px;
        vm->min_y = vm->max_y = py;
    } else {
        if (px < vm->min_x) vm->min_x = px;
        if (px > vm->max_x) vm->max_x = px;
        if (py < vm->min_y) vm->min_y = py;
        if (py > vm->max_y) vm->max_y = py;
    }
}

static im_fixed cff_to_px_x(const cff_vm *vm, int64_t x_16) {
    /* x_16 is 16.16 font units; result is 16.16 pixels = round((x_16 * size_px / 65536) / upem).
       Shift >> 16 first in 128-bit so the division is pure 64-bit (no __udivti3 in freestanding). */
    int64_t upem = (int64_t)vm->font->metrics.units_per_em;
    if (upem <= 0) return 0;
    __int128 prod = (__int128)x_16 * (__int128)vm->size_px;
    __int128 shifted = (prod >= 0) ? ((prod + 32768) >> 16) : -(((-prod) + 32768) >> 16);
    if (shifted > INT64_MAX / 2) shifted = INT64_MAX / 2;
    if (shifted < INT64_MIN / 2) shifted = INT64_MIN / 2;
    return sat_fx(div_round_i64((int64_t)shifted, upem));
}

static im_fixed cff_to_px_y(const cff_vm *vm, int64_t y_16) {
    return cff_to_px_x(vm, -y_16);
}

static int32_t cff_subr_bias(uint32_t count) {
    if (count < 1240u) return 107;
    if (count < 33900u) return 1131;
    return 32768;
}

static bool cff_move(cff_vm *vm, int64_t dx, int64_t dy) {
    if (vm->open && vm->path) {
        if (!im_path_close(vm->path)) return false;
    }
    vm->x += dx;
    vm->y += dy;
    vm->open = true;
    cff_track_pt(vm, vm->x, vm->y);
    return vm->path ? im_path_move_to(vm->path, cff_to_px_x(vm, vm->x), cff_to_px_y(vm, vm->y)) : true;
}

static bool cff_line(cff_vm *vm, int64_t dx, int64_t dy) {
    if (!vm->open) return false;
    vm->x += dx;
    vm->y += dy;
    cff_track_pt(vm, vm->x, vm->y);
    return vm->path ? im_path_line_to(vm->path, cff_to_px_x(vm, vm->x), cff_to_px_y(vm, vm->y)) : true;
}

static bool cff_cubic(cff_vm *vm, int64_t dx1, int64_t dy1, int64_t dx2, int64_t dy2, int64_t dx3, int64_t dy3) {
    if (!vm->open) return false;
    int64_t x1 = vm->x + dx1, y1 = vm->y + dy1;
    int64_t x2 = x1 + dx2, y2 = y1 + dy2;
    int64_t x3 = x2 + dx3, y3 = y2 + dy3;
    vm->x = x3; vm->y = y3;
    cff_track_pt(vm, x1, y1);
    cff_track_pt(vm, x2, y2);
    cff_track_pt(vm, x3, y3);
    return vm->path ? im_path_cubic_to(vm->path,
                                       cff_to_px_x(vm, x1), cff_to_px_y(vm, y1),
                                       cff_to_px_x(vm, x2), cff_to_px_y(vm, y2),
                                       cff_to_px_x(vm, x3), cff_to_px_y(vm, y3)) : true;
}

static bool cff_exec(cff_vm *vm, size_t start, size_t len) {
    if (vm->depth > IM_FONT_MAX_DEPTH) return false;
    const uint8_t *d = vm->font->data;
    size_t pos = start;
    size_t end = start + len;

    while (pos < end) {
        if (++vm->ops > IM_FONT_MAX_OPS) return false;
        uint8_t b0 = d[pos++];

        if (b0 >= 32u && b0 <= 246u) {
            if (vm->sp >= 48u) return false;
            vm->stack[vm->sp++] = ((int32_t)b0 - 139) * 65536;
            continue;
        }
        if (b0 >= 247u && b0 <= 250u) {
            if (pos >= end || vm->sp >= 48u) return false;
            int32_t v = ((int32_t)b0 - 247) * 256 + (int32_t)d[pos++] + 108;
            vm->stack[vm->sp++] = v * 65536;
            continue;
        }
        if (b0 >= 251u && b0 <= 254u) {
            if (pos >= end || vm->sp >= 48u) return false;
            int32_t v = -((int32_t)b0 - 251) * 256 - (int32_t)d[pos++] - 108;
            vm->stack[vm->sp++] = v * 65536;
            continue;
        }
        if (b0 == 28u) {
            if (pos + 2u > end || vm->sp >= 48u) return false;
            int16_t v = (int16_t)(((uint16_t)d[pos] << 8) | (uint16_t)d[pos + 1u]);
            pos += 2u;
            vm->stack[vm->sp++] = (int32_t)v * 65536;
            continue;
        }
        if (b0 == 255u) {
            if (pos + 4u > end || vm->sp >= 48u) return false;
            uint32_t u = ((uint32_t)d[pos] << 24) | ((uint32_t)d[pos + 1u] << 16) |
                         ((uint32_t)d[pos + 2u] << 8) | (uint32_t)d[pos + 3u];
            pos += 4u;
            vm->stack[vm->sp++] = (int32_t)u; /* Already 16.16 fixed point! */
            continue;
        }

        uint16_t op = b0;
        if (b0 == 12u) {
            if (pos >= end) return false;
            op = (uint16_t)(0x0C00u | d[pos++]);
        }

        switch (op) {
            case 1u: case 3u: case 18u: case 23u: /* hstem, vstem, hstemhm, vstemhm */
                if (!vm->have_width && (vm->sp & 1u) != 0) {
                    vm->have_width = true;
                    vm->num_stems += (vm->sp - 1u) / 2u;
                } else {
                    vm->have_width = true;
                    vm->num_stems += vm->sp / 2u;
                }
                vm->sp = 0;
                break;

            case 19u: case 20u: /* hintmask, cntrmask */
                if (!vm->have_width && (vm->sp & 1u) != 0) {
                    vm->have_width = true;
                    vm->num_stems += (vm->sp - 1u) / 2u;
                } else {
                    vm->have_width = true;
                    vm->num_stems += vm->sp / 2u;
                }
                vm->sp = 0;
                {
                    uint32_t mask_bytes = (vm->num_stems + 7u) / 8u;
                    if (mask_bytes > end - pos) return false;
                    pos += mask_bytes;
                }
                break;

            case 4u: /* vmoveto */ {
                uint32_t idx = 0;
                if (!vm->have_width && vm->sp > 1u) idx = 1u;
                vm->have_width = true;
                if (vm->sp <= idx) return false;
                if (!cff_move(vm, 0, vm->stack[idx])) return false;
                vm->sp = 0;
                break;
            }
            case 22u: /* hmoveto */ {
                uint32_t idx = 0;
                if (!vm->have_width && vm->sp > 1u) idx = 1u;
                vm->have_width = true;
                if (vm->sp <= idx) return false;
                if (!cff_move(vm, vm->stack[idx], 0)) return false;
                vm->sp = 0;
                break;
            }
            case 21u: /* rmoveto */ {
                uint32_t idx = 0;
                if (!vm->have_width && vm->sp > 2u) idx = 1u;
                vm->have_width = true;
                if (vm->sp < idx + 2u) return false;
                if (!cff_move(vm, vm->stack[idx], vm->stack[idx + 1u])) return false;
                vm->sp = 0;
                break;
            }
            case 5u: /* rlineto */
                if (vm->sp < 2u || (vm->sp & 1u) != 0) return false;
                for (uint32_t i = 0; i < vm->sp; i += 2u) {
                    if (!cff_line(vm, vm->stack[i], vm->stack[i + 1u])) return false;
                }
                vm->sp = 0;
                break;
            case 6u: /* hlineto */
                if (vm->sp < 1u) return false;
                for (uint32_t i = 0; i < vm->sp; ++i) {
                    if ((i & 1u) == 0) { if (!cff_line(vm, vm->stack[i], 0)) return false; }
                    else { if (!cff_line(vm, 0, vm->stack[i])) return false; }
                }
                vm->sp = 0;
                break;
            case 7u: /* vlineto */
                if (vm->sp < 1u) return false;
                for (uint32_t i = 0; i < vm->sp; ++i) {
                    if ((i & 1u) == 0) { if (!cff_line(vm, 0, vm->stack[i])) return false; }
                    else { if (!cff_line(vm, vm->stack[i], 0)) return false; }
                }
                vm->sp = 0;
                break;
            case 8u: /* rrcurveto */
                if (vm->sp < 6u || (vm->sp % 6u) != 0) return false;
                for (uint32_t i = 0; i < vm->sp; i += 6u) {
                    if (!cff_cubic(vm, vm->stack[i], vm->stack[i + 1u], vm->stack[i + 2u],
                                   vm->stack[i + 3u], vm->stack[i + 4u], vm->stack[i + 5u])) return false;
                }
                vm->sp = 0;
                break;
            case 24u: /* rcurveline */
                if (vm->sp < 8u) return false;
                for (uint32_t i = 0; i + 7u < vm->sp; i += 6u) {
                    if (!cff_cubic(vm, vm->stack[i], vm->stack[i + 1u], vm->stack[i + 2u],
                                   vm->stack[i + 3u], vm->stack[i + 4u], vm->stack[i + 5u])) return false;
                }
                if (!cff_line(vm, vm->stack[vm->sp - 2u], vm->stack[vm->sp - 1u])) return false;
                vm->sp = 0;
                break;
            case 25u: /* rlinecurve */
                if (vm->sp < 8u) return false;
                for (uint32_t i = 0; i + 7u < vm->sp; i += 2u) {
                    if (!cff_line(vm, vm->stack[i], vm->stack[i + 1u])) return false;
                }
                if (!cff_cubic(vm, vm->stack[vm->sp - 6u], vm->stack[vm->sp - 5u],
                               vm->stack[vm->sp - 4u], vm->stack[vm->sp - 3u],
                               vm->stack[vm->sp - 2u], vm->stack[vm->sp - 1u])) return false;
                vm->sp = 0;
                break;
            case 26u: /* vvcurveto */ {
                uint32_t i = 0;
                int64_t dx1 = 0;
                if ((vm->sp & 1u) != 0) dx1 = vm->stack[i++];
                for (; i + 3u < vm->sp; i += 4u) {
                    if (!cff_cubic(vm, dx1, vm->stack[i], vm->stack[i + 1u], vm->stack[i + 2u], 0, vm->stack[i + 3u])) return false;
                    dx1 = 0;
                }
                vm->sp = 0;
                break;
            }
            case 27u: /* hhcurveto */ {
                uint32_t i = 0;
                int64_t dy1 = 0;
                if ((vm->sp & 1u) != 0) dy1 = vm->stack[i++];
                for (; i + 3u < vm->sp; i += 4u) {
                    if (!cff_cubic(vm, vm->stack[i], dy1, vm->stack[i + 1u], vm->stack[i + 2u], vm->stack[i + 3u], 0)) return false;
                    dy1 = 0;
                }
                vm->sp = 0;
                break;
            }
            case 30u: /* vhcurveto */
            case 31u: /* hvcurveto */ {
                bool horiz = (op == 31u);
                uint32_t i = 0;
                while (i + 3u < vm->sp) {
                    uint32_t rem = vm->sp - i;
                    int64_t extra = (rem == 5u) ? vm->stack[i + 4u] : 0;
                    if (horiz) {
                        if (!cff_cubic(vm, vm->stack[i], 0, vm->stack[i + 1u], vm->stack[i + 2u], extra, vm->stack[i + 3u])) return false;
                    } else {
                        if (!cff_cubic(vm, 0, vm->stack[i], vm->stack[i + 1u], vm->stack[i + 2u], vm->stack[i + 3u], extra)) return false;
                    }
                    i += (rem == 5u) ? 5u : 4u;
                    horiz = !horiz;
                }
                vm->sp = 0;
                break;
            }
            case 0x0C22u: /* hflex */
                if (vm->sp < 7u) return false;
                if (!cff_cubic(vm, (int64_t)vm->stack[0], 0, (int64_t)vm->stack[1], (int64_t)vm->stack[2], (int64_t)vm->stack[3], 0)) return false;
                if (!cff_cubic(vm, (int64_t)vm->stack[4], 0, (int64_t)vm->stack[5], -(int64_t)vm->stack[2], (int64_t)vm->stack[6], 0)) return false;
                vm->sp = 0;
                break;
            case 0x0C23u: /* flex */
                if (vm->sp < 13u) return false;
                if (!cff_cubic(vm, (int64_t)vm->stack[0], (int64_t)vm->stack[1], (int64_t)vm->stack[2], (int64_t)vm->stack[3], (int64_t)vm->stack[4], (int64_t)vm->stack[5])) return false;
                if (!cff_cubic(vm, (int64_t)vm->stack[6], (int64_t)vm->stack[7], (int64_t)vm->stack[8], (int64_t)vm->stack[9], (int64_t)vm->stack[10], (int64_t)vm->stack[11])) return false;
                vm->sp = 0;
                break;
            case 0x0C24u: /* hflex1 */
                if (vm->sp < 9u) return false;
                {
                    int64_t dy6 = -((int64_t)vm->stack[1] + (int64_t)vm->stack[3] + (int64_t)vm->stack[7]);
                    if (!cff_cubic(vm, (int64_t)vm->stack[0], (int64_t)vm->stack[1], (int64_t)vm->stack[2], (int64_t)vm->stack[3], (int64_t)vm->stack[4], 0)) return false;
                    if (!cff_cubic(vm, (int64_t)vm->stack[5], 0, (int64_t)vm->stack[6], (int64_t)vm->stack[7], (int64_t)vm->stack[8], dy6)) return false;
                }
                vm->sp = 0;
                break;
            case 0x0C25u: /* flex1 */
                if (vm->sp < 11u) return false;
                {
                    int64_t dx = (int64_t)vm->stack[0] + (int64_t)vm->stack[2] + (int64_t)vm->stack[4] + (int64_t)vm->stack[6] + (int64_t)vm->stack[8];
                    int64_t dy = (int64_t)vm->stack[1] + (int64_t)vm->stack[3] + (int64_t)vm->stack[5] + (int64_t)vm->stack[7] + (int64_t)vm->stack[9];
                    int64_t adx = dx < 0 ? -dx : dx;
                    int64_t ady = dy < 0 ? -dy : dy;
                    int64_t dx6 = (adx > ady) ? (int64_t)vm->stack[10] : -dx;
                    int64_t dy6 = (adx > ady) ? -dy : (int64_t)vm->stack[10];
                    if (!cff_cubic(vm, (int64_t)vm->stack[0], (int64_t)vm->stack[1], (int64_t)vm->stack[2], (int64_t)vm->stack[3], (int64_t)vm->stack[4], (int64_t)vm->stack[5])) return false;
                    if (!cff_cubic(vm, (int64_t)vm->stack[6], (int64_t)vm->stack[7], (int64_t)vm->stack[8], (int64_t)vm->stack[9], dx6, dy6)) return false;
                }
                vm->sp = 0;
                break;

            case 10u: /* callsubr */
            case 29u: /* callgsubr */ {
                if (vm->sp < 1u) return false;
                int32_t raw_idx = vm->stack[--vm->sp] >> 16;
                const cff_index *subrs = (op == 10u) ? (vm->has_lsubrs ? &vm->lsubrs : NULL) : &vm->gsubrs;
                if (!subrs || subrs->count == 0) return false;
                int32_t idx = raw_idx + cff_subr_bias(subrs->count);
                if (idx < 0 || (uint32_t)idx >= subrs->count) return false;
                size_t s_start = 0, s_len = 0;
                if (!cff_index_get(vm->font->data, vm->font->size, subrs, (uint32_t)idx, &s_start, &s_len)) return false;
                vm->depth += 1u;
                bool ok = cff_exec(vm, s_start, s_len);
                vm->depth -= 1u;
                if (!ok) return false;
                if (vm->ended) return true;
                break;
            }
            case 11u: /* return */
                if (vm->depth == 0) return false; /* top-level return without endchar is invalid */
                return true;
            case 14u: /* endchar */
                if (vm->sp >= 4u) return false; /* deprecated seac arguments explicitly refused */
                if (vm->open && vm->path) {
                    if (!im_path_close(vm->path)) return false;
                }
                vm->open = false;
                vm->ended = true;
                vm->sp = 0;
                return true;

            default:
                /* Unsupported Type 2 operator */
                return false;
        }
    }
    return true;
}

static bool cff_decode_glyph(const im_font *font, uint16_t gid, im_fixed size_px, im_path *out_path) {
    size_t base = font->cff_off;
    size_t end = base + font->cff_len;
    cff_index cs_idx;
    if (!cff_parse_index(font->data, font->size, base, end, base + font->cff_charstrings_off, &cs_idx)) return false;
    if (gid >= cs_idx.count) return false;

    size_t cs_start = 0, cs_len = 0;
    if (!cff_index_get(font->data, font->size, &cs_idx, gid, &cs_start, &cs_len)) return false;

    cff_vm vm;
    zero_bytes(&vm, sizeof(vm));
    vm.font = font;
    vm.size_px = size_px;
    vm.path = out_path;
    if (!cff_parse_index(font->data, font->size, base, end, base + font->cff_gsubrs_off, &vm.gsubrs)) return false;
    if (font->cff_lsubrs_off > 0) {
        if (!cff_parse_index(font->data, font->size, base, end, base + font->cff_lsubrs_off, &vm.lsubrs)) return false;
        vm.has_lsubrs = true;
    }

    if (!cff_exec(&vm, cs_start, cs_len) || !vm.ended) return false;
    return true;
}

static bool cff_measure_glyph(const im_font *font, uint16_t gid, im_glyph_metrics *out) {
    size_t base = font->cff_off;
    size_t end = base + font->cff_len;
    cff_index cs_idx;
    if (!cff_parse_index(font->data, font->size, base, end, base + font->cff_charstrings_off, &cs_idx)) return false;
    if (gid >= cs_idx.count) return false;
    size_t cs_start = 0, cs_len = 0;
    if (!cff_index_get(font->data, font->size, &cs_idx, gid, &cs_start, &cs_len)) return false;

    cff_vm vm;
    zero_bytes(&vm, sizeof(vm));
    vm.font = font;
    vm.size_px = IM_ONE;
    vm.path = NULL;
    if (!cff_parse_index(font->data, font->size, base, end, base + font->cff_gsubrs_off, &vm.gsubrs)) return false;
    if (font->cff_lsubrs_off > 0) {
        if (!cff_parse_index(font->data, font->size, base, end, base + font->cff_lsubrs_off, &vm.lsubrs)) return false;
        vm.has_lsubrs = true;
    }
    if (!cff_exec(&vm, cs_start, cs_len) || !vm.ended) return false;
    out->is_empty = !vm.has_ink;
    if (vm.has_ink) {
        int64_t x0 = vm.min_x >> 16;
        int64_t y0 = vm.min_y >> 16;
        int64_t x1 = (vm.max_x + 65535) >> 16;
        int64_t y1 = (vm.max_y + 65535) >> 16;
        if (x0 < INT16_MIN) x0 = INT16_MIN;
        if (x0 > INT16_MAX) x0 = INT16_MAX;
        if (y0 < INT16_MIN) y0 = INT16_MIN;
        if (y0 > INT16_MAX) y0 = INT16_MAX;
        if (x1 < INT16_MIN) x1 = INT16_MIN;
        if (x1 > INT16_MAX) x1 = INT16_MAX;
        if (y1 < INT16_MIN) y1 = INT16_MIN;
        if (y1 > INT16_MAX) y1 = INT16_MAX;
        out->x_min = (int16_t)x0;
        out->y_min = (int16_t)y0;
        out->x_max = (int16_t)x1;
        out->y_max = (int16_t)y1;
    }
    return true;
}

/* ---- Public outline & rasterization ---- */

bool im_font_glyph_outline(const im_font *font, uint16_t glyph_id, im_fixed size_px, im_path *out_path) {
    if (!out_path) return false;
    im_path_clear(out_path);
    if (!font || !font->data || glyph_id >= font->metrics.num_glyphs || size_px <= 0) return false;
    if (!out_path->alloc.realloc || !out_path->alloc.free) return false;

    out_path->fill_rule = IM_FILL_NON_ZERO;

    if (font->metrics.is_cff) {
        if (!cff_decode_glyph(font, glyph_id, size_px, out_path)) {
            im_path_clear(out_path);
            return false;
        }
        return true;
    }

    tt_buf buf;
    zero_bytes(&buf, sizeof(buf));
    buf.alloc = out_path->alloc;
    bool ok = tt_decode_glyph(font, glyph_id, &buf, 0);
    if (ok) ok = tt_emit_path(font, &buf, size_px, out_path);
    tt_buf_destroy(&buf);
    if (!ok) im_path_clear(out_path);
    return ok;
}

bool im_font_draw_glyph(im_surface *surface, im_allocator alloc, const im_font *font,
                        uint16_t glyph_id, im_fixed x, im_fixed y, im_fixed size_px, uint32_t argb) {
    if (!surface || !font || !font->data || glyph_id >= font->metrics.num_glyphs || size_px <= 0) return false;
    if (!alloc.realloc || !alloc.free) return false;

    im_path path;
    if (!im_path_init(&path, alloc, IM_FILL_NON_ZERO)) return false;
    bool ok = im_font_glyph_outline(font, glyph_id, size_px, &path);
    if (ok && path.count > 0) {
        for (uint32_t i = 0; i < path.count; ++i) {
            im_path_seg *s = &path.segs[i];
            if (s->cmd == IM_PATH_MOVE || s->cmd == IM_PATH_LINE) {
                s->x1 = sat_fx((int64_t)s->x1 + x);
                s->y1 = sat_fx((int64_t)s->y1 + y);
            } else if (s->cmd == IM_PATH_QUAD) {
                s->x1 = sat_fx((int64_t)s->x1 + x);
                s->y1 = sat_fx((int64_t)s->y1 + y);
                s->x2 = sat_fx((int64_t)s->x2 + x);
                s->y2 = sat_fx((int64_t)s->y2 + y);
            } else if (s->cmd == IM_PATH_CUBIC) {
                s->x1 = sat_fx((int64_t)s->x1 + x);
                s->y1 = sat_fx((int64_t)s->y1 + y);
                s->x2 = sat_fx((int64_t)s->x2 + x);
                s->y2 = sat_fx((int64_t)s->y2 + y);
                s->x3 = sat_fx((int64_t)s->x3 + x);
                s->y3 = sat_fx((int64_t)s->y3 + y);
            }
        }
        im_path_data view = im_path_view(&path);
        ok = im_path_fill_argb(surface, alloc, &view, argb);
    }
    im_path_destroy(&path);
    return ok;
}
