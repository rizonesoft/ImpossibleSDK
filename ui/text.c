/* Impossible UI: fixed-point UTF-8 text shaping, UAX #14 line breaking, alignment and drawing. Portable. */
#include "text.h"
#include "unicode.h"

static void zero_bytes(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; ++i) b[i] = 0;
}

static im_fixed sat_add_fx(im_fixed a, im_fixed b) {
    int64_t s = (int64_t)a + (int64_t)b;
    if (s > INT32_MAX) return INT32_MAX;
    if (s < INT32_MIN) return INT32_MIN;
    return (im_fixed)s;
}

/* WHATWG / Unicode maximal subpart UTF-8 decoder: invalid bytes emit U+FFFD and consume
   only the invalid prefix so any immediately following valid byte sequence survives. */
size_t im_utf8_decode(const char *src, size_t len, uint32_t *out_cp) {
    if (!src || len == 0) {
        if (out_cp) *out_cp = 0xFFFDu;
        return 0;
    }
    uint8_t b0 = (uint8_t)src[0];
    if (b0 < 0x80u) {
        if (out_cp) *out_cp = b0;
        return 1u;
    }
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (len >= 2u) {
            uint8_t b1 = (uint8_t)src[1];
            if ((b1 & 0xC0u) == 0x80u) {
                if (out_cp) *out_cp = (((uint32_t)b0 & 0x1Fu) << 6) | ((uint32_t)b1 & 0x3Fu);
                return 2u;
            }
        }
        if (out_cp) *out_cp = 0xFFFDu;
        return 1u;
    }
    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        uint8_t lo = (b0 == 0xE0u) ? 0xA0u : 0x80u;
        uint8_t hi = (b0 == 0xEDu) ? 0x9Fu : 0xBFu;
        if (len >= 2u) {
            uint8_t b1 = (uint8_t)src[1];
            if (b1 >= lo && b1 <= hi) {
                if (len >= 3u) {
                    uint8_t b2 = (uint8_t)src[2];
                    if ((b2 & 0xC0u) == 0x80u) {
                        if (out_cp) {
                            *out_cp = (((uint32_t)b0 & 0x0Fu) << 12) |
                                      (((uint32_t)b1 & 0x3Fu) << 6) |
                                      ((uint32_t)b2 & 0x3Fu);
                        }
                        return 3u;
                    }
                }
                if (out_cp) *out_cp = 0xFFFDu;
                return 2u; /* maximal subpart of truncated/broken 3-byte sequence */
            }
        }
        if (out_cp) *out_cp = 0xFFFDu;
        return 1u;
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        uint8_t lo = (b0 == 0xF0u) ? 0x90u : 0x80u;
        uint8_t hi = (b0 == 0xF4u) ? 0x8Fu : 0xBFu;
        if (len >= 2u) {
            uint8_t b1 = (uint8_t)src[1];
            if (b1 >= lo && b1 <= hi) {
                if (len >= 3u) {
                    uint8_t b2 = (uint8_t)src[2];
                    if ((b2 & 0xC0u) == 0x80u) {
                        if (len >= 4u) {
                            uint8_t b3 = (uint8_t)src[3];
                            if ((b3 & 0xC0u) == 0x80u) {
                                if (out_cp) {
                                    *out_cp = (((uint32_t)b0 & 0x07u) << 18) |
                                              (((uint32_t)b1 & 0x3Fu) << 12) |
                                              (((uint32_t)b2 & 0x3Fu) << 6) |
                                              ((uint32_t)b3 & 0x3Fu);
                                }
                                return 4u;
                            }
                        }
                        if (out_cp) *out_cp = 0xFFFDu;
                        return 3u;
                    }
                }
                if (out_cp) *out_cp = 0xFFFDu;
                return 2u;
            }
        }
        if (out_cp) *out_cp = 0xFFFDu;
        return 1u;
    }
    if (out_cp) *out_cp = 0xFFFDu;
    return 1u;
}

static bool is_hard_break_cp(uint32_t cp) {
    return cp == 0x000Au || cp == 0x000Du || cp == 0x000Bu || cp == 0x000Cu ||
           cp == 0x0085u || cp == 0x2028u || cp == 0x2029u;
}

static bool is_trailing_space_cp(uint32_t cp) {
    return cp == 0x0020u || cp == 0x0009u || cp == 0x2000u || cp == 0x2001u ||
           cp == 0x2002u || cp == 0x2003u || cp == 0x2004u || cp == 0x2005u ||
           cp == 0x2006u || cp == 0x2008u || cp == 0x2009u || cp == 0x200Au ||
           cp == 0x3000u;
}

typedef struct span_item {
    uint32_t cp;
    uint32_t byte_off;
    uint32_t byte_len;
    uint16_t glyph_id;
    im_fixed nominal_adv;
} span_item;

/* Compute positioned width of items[start..end) and visible width excluding trailing whitespace. */
static void measure_range(const span_item *items, uint32_t start, uint32_t end,
                          const im_text_style *style, im_fixed *out_visible, im_fixed *out_full) {
    /* Find last non-trailing-space item in [start, end) */
    uint32_t vis_end = end;
    while (vis_end > start && is_trailing_space_cp(items[vis_end - 1u].cp)) {
        --vis_end;
    }
    int64_t full_w = 0;
    int64_t vis_w = 0;
    for (uint32_t i = start; i < end; ++i) {
        int64_t adv = items[i].nominal_adv;
        if (style->kerning && i + 1u < end) {
            int32_t kern = im_font_kerning(style->font, items[i].glyph_id, items[i + 1u].glyph_id);
            if (kern != 0) adv += im_font_scale_units(style->font, kern, style->font_size);
        }
        full_w += adv;
        if (i < vis_end) {
            /* Visible width stops kerning after the last visible glyph */
            int64_t vadv = items[i].nominal_adv;
            if (style->kerning && i + 1u < vis_end) {
                int32_t kern = im_font_kerning(style->font, items[i].glyph_id, items[i + 1u].glyph_id);
                if (kern != 0) vadv += im_font_scale_units(style->font, kern, style->font_size);
            }
            vis_w += vadv;
        }
    }
    if (vis_w > INT32_MAX) vis_w = INT32_MAX;
    if (vis_w < 0) vis_w = 0;
    if (full_w > INT32_MAX) full_w = INT32_MAX;
    if (full_w < 0) full_w = 0;
    *out_visible = (im_fixed)vis_w;
    *out_full = (im_fixed)full_w;
}

typedef struct line_vec {
    im_allocator alloc;
    im_text_line *data;
    uint32_t count;
    uint32_t cap;
} line_vec;

static bool line_vec_push(line_vec *v, im_text_line ln) {
    if (v->count == v->cap) {
        uint32_t ncap = v->cap ? v->cap * 2u : 8u;
        im_text_line *nd = (im_text_line *)v->alloc.realloc(v->alloc.ctx, v->data,
                                                            (size_t)v->cap * sizeof(im_text_line),
                                                            (size_t)ncap * sizeof(im_text_line));
        if (!nd) return false;
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->count++] = ln;
    return true;
}

typedef struct glyph_vec {
    im_allocator alloc;
    im_text_glyph *data;
    uint32_t count;
    uint32_t cap;
} glyph_vec;

static bool glyph_vec_push(glyph_vec *v, im_text_glyph g) {
    if (v->count == v->cap) {
        uint32_t ncap = v->cap ? v->cap * 2u : 32u;
        im_text_glyph *nd = (im_text_glyph *)v->alloc.realloc(v->alloc.ctx, v->data,
                                                              (size_t)v->cap * sizeof(im_text_glyph),
                                                              (size_t)ncap * sizeof(im_text_glyph));
        if (!nd) return false;
        v->data = nd;
        v->cap = ncap;
    }
    v->data[v->count++] = g;
    return true;
}

static bool emit_line(line_vec *lvec, glyph_vec *gvec,
                      const span_item *items, uint32_t start, uint32_t end,
                      uint32_t byte_start, uint32_t byte_end,
                      const im_text_style *style, im_fixed line_h, im_fixed baseline, im_fixed y) {
    im_fixed vis_w = 0, full_w = 0;
    measure_range(items, start, end, style, &vis_w, &full_w);

    uint32_t gstart = gvec->count;
    im_fixed pen = 0;
    for (uint32_t i = start; i < end; ++i) {
        im_fixed adv = items[i].nominal_adv;
        if (style->kerning && i + 1u < end) {
            int32_t kern = im_font_kerning(style->font, items[i].glyph_id, items[i + 1u].glyph_id);
            if (kern != 0) adv = sat_add_fx(adv, im_font_scale_units(style->font, kern, style->font_size));
        }
        im_text_glyph g;
        g.glyph_id = items[i].glyph_id;
        g.byte_offset = items[i].byte_off;
        g.byte_len = items[i].byte_len;
        g.x = pen;
        g.y = sat_add_fx(y, baseline);
        g.advance = adv;
        g.nominal_adv = items[i].nominal_adv;
        if (!glyph_vec_push(gvec, g)) return false;
        pen = sat_add_fx(pen, adv);
    }

    im_text_line ln;
    ln.byte_start = byte_start;
    ln.byte_end = byte_end;
    ln.glyph_start = gstart;
    ln.glyph_count = end - start;
    ln.x = 0;
    ln.y = y;
    ln.width = vis_w;
    ln.full_width = full_w;
    ln.height = line_h;
    ln.baseline = baseline;
    return line_vec_push(lvec, ln);
}

bool im_text_layout_build(im_text_layout *layout, im_allocator alloc,
                          const char *utf8, size_t utf8_len, const im_text_style *style) {
    if (!layout) return false;
    zero_bytes(layout, sizeof(*layout));
    if (!style || !style->font || !style->font->data || style->font_size <= 0) return false;
    if (!alloc.alloc || !alloc.realloc || !alloc.free) return false;
    if (!utf8 && utf8_len > 0) return false;

    im_font_metrics fm = im_font_get_metrics(style->font);
    im_fixed baseline = im_font_scale_units(style->font, fm.ascent, style->font_size);
    im_fixed natural_h = im_font_scale_units(style->font,
                                             (int32_t)fm.ascent - (int32_t)fm.descent + (int32_t)fm.line_gap,
                                             style->font_size);
    if (natural_h <= 0) natural_h = style->font_size;
    im_fixed line_h = (style->line_height > 0) ? style->line_height : natural_h;

    line_vec lvec = { alloc, NULL, 0, 0 };
    glyph_vec gvec = { alloc, NULL, 0, 0 };

    /* Empty string produces one empty line of natural height (matching WinUI TextBlock). */
    if (utf8_len == 0) {
        im_text_line empty = { 0, 0, 0, 0, 0, 0, 0, 0, line_h, baseline };
        if (!line_vec_push(&lvec, empty)) return false;
        layout->alloc = alloc;
        layout->lines = lvec.data;
        layout->line_count = lvec.count;
        layout->line_cap = lvec.cap;
        layout->glyphs = NULL;
        layout->glyph_count = 0;
        layout->glyph_cap = 0;
        layout->width = 0;
        layout->height = line_h;
        return true;
    }

    /* Pass 1: count codepoints */
    size_t cp_count = 0;
    size_t off = 0;
    while (off < utf8_len) {
        uint32_t cp = 0;
        size_t n = im_utf8_decode(utf8 + off, utf8_len - off, &cp);
        if (n == 0) break;
        off += n;
        ++cp_count;
    }

    uint32_t *cps = (uint32_t *)alloc.alloc(alloc.ctx, cp_count * sizeof(uint32_t));
    span_item *items = (span_item *)alloc.alloc(alloc.ctx, cp_count * sizeof(span_item));
    uint8_t *lbreaks = (uint8_t *)alloc.alloc(alloc.ctx, (cp_count + 1u) * sizeof(uint8_t));
    uint8_t *gbreaks = (uint8_t *)alloc.alloc(alloc.ctx, (cp_count + 1u) * sizeof(uint8_t));
    if (!cps || !items || !lbreaks || !gbreaks) {
        if (cps) alloc.free(alloc.ctx, cps, cp_count * sizeof(uint32_t));
        if (items) alloc.free(alloc.ctx, items, cp_count * sizeof(span_item));
        if (lbreaks) alloc.free(alloc.ctx, lbreaks, (cp_count + 1u) * sizeof(uint8_t));
        if (gbreaks) alloc.free(alloc.ctx, gbreaks, (cp_count + 1u) * sizeof(uint8_t));
        return false;
    }

    /* Pass 2: decode codepoints and nominal glyph advances */
    bool ok = true;
    off = 0;
    for (size_t i = 0; i < cp_count; ++i) {
        uint32_t cp = 0;
        size_t n = im_utf8_decode(utf8 + off, utf8_len - off, &cp);
        cps[i] = cp;
        items[i].cp = cp;
        items[i].byte_off = (uint32_t)off;
        items[i].byte_len = (uint32_t)n;
        uint16_t gid = im_font_glyph_index(style->font, cp);
        items[i].glyph_id = gid;
        im_glyph_metrics gm;
        if (!im_font_glyph_metrics(style->font, gid, &gm)) {
            ok = false;
            break;
        }
        items[i].nominal_adv = im_font_scale_units(style->font, (int32_t)gm.advance_width, style->font_size);
        off += n;
    }

    if (ok) {
        ok = (im_unicode_line_breaks(cps, cp_count, lbreaks, cp_count + 1u) == IM_UNICODE_OK &&
              im_unicode_grapheme_breaks(cps, cp_count, gbreaks, cp_count + 1u) == IM_UNICODE_OK);
    }

    im_fixed cur_y = 0;
    uint32_t line_start = 0;
    bool ended_with_hard_break = false;
    uint32_t trailing_byte_off = (uint32_t)utf8_len;

    while (ok && line_start < (uint32_t)cp_count) {
        /* Find the next explicit hard break in [line_start, cp_count). */
        uint32_t seg_end = line_start;
        uint32_t next_after_hard = (uint32_t)cp_count;
        bool has_hard = false;
        while (seg_end < (uint32_t)cp_count) {
            if (is_hard_break_cp(items[seg_end].cp)) {
                has_hard = true;
                next_after_hard = seg_end + 1u;
                if (items[seg_end].cp == 0x000Du && next_after_hard < (uint32_t)cp_count &&
                    items[next_after_hard].cp == 0x000Au) {
                    next_after_hard += 1u; /* CRLF is one hard break */
                }
                break;
            }
            ++seg_end;
        }

        if (style->wrap == IM_TEXT_NO_WRAP || style->max_width <= 0) {
            uint32_t bs = items[line_start].byte_off;
            uint32_t be = (seg_end > line_start) ? (items[seg_end - 1u].byte_off + items[seg_end - 1u].byte_len) : bs;
            ok = emit_line(&lvec, &gvec, items, line_start, seg_end, bs, be, style, line_h, baseline, cur_y);
            cur_y = sat_add_fx(cur_y, line_h);
            line_start = next_after_hard;
            ended_with_hard_break = has_hard && (line_start == (uint32_t)cp_count);
            if (ended_with_hard_break) trailing_byte_off = (uint32_t)utf8_len;
            continue;
        }

        /* Wrap paragraph segment [line_start, seg_end) against style->max_width. */
        if (line_start == seg_end) {
            uint32_t bs = items[line_start].byte_off;
            ok = emit_line(&lvec, &gvec, items, line_start, seg_end, bs, bs, style, line_h, baseline, cur_y);
            cur_y = sat_add_fx(cur_y, line_h);
            line_start = next_after_hard;
            ended_with_hard_break = has_hard && (line_start == (uint32_t)cp_count);
            continue;
        }

        uint32_t p = line_start;
        while (ok && p < seg_end) {
            /* Incrementally advance forward from p (O(line_len), never rescanning the full remaining suffix). */
            uint32_t best_uax = 0;
            uint32_t first_uax = 0;
            uint32_t best_gr = 0;
            uint32_t first_gr = 0;
            int64_t full_w = 0;
            int64_t last_vis_w = 0;
            bool fits_all = true;

            for (uint32_t idx = p; idx < seg_end; ++idx) {
                int64_t adv = items[idx].nominal_adv;
                if (style->kerning && idx > p) {
                    int32_t kern = im_font_kerning(style->font, items[idx - 1u].glyph_id, items[idx].glyph_id);
                    if (kern != 0) full_w += im_font_scale_units(style->font, kern, style->font_size);
                }
                full_w += adv;
                if (!is_trailing_space_cp(items[idx].cp)) {
                    last_vis_w = full_w;
                }
                uint32_t cand = idx + 1u;
                bool is_uax = (cand == seg_end) || (lbreaks[cand] != IM_BREAK_NONE);
                bool is_gr = (cand == seg_end) || (gbreaks[cand] != IM_BREAK_NONE);
                if (is_uax && first_uax == 0) first_uax = cand;
                if (is_gr && first_gr == 0) first_gr = cand;

                if (last_vis_w <= style->max_width) {
                    if (is_uax) best_uax = cand;
                    if (is_gr) best_gr = cand;
                } else {
                    fits_all = false;
                    if (first_uax != 0 && first_gr != 0) break;
                }
            }

            uint32_t chosen = fits_all ? seg_end : best_uax;
            if (chosen == 0) {
                if (style->wrap == IM_TEXT_WRAP_WHOLE_WORDS) {
                    chosen = first_uax ? first_uax : seg_end;
                } else {
                    chosen = best_gr ? best_gr : (first_gr ? first_gr : (p + 1u));
                }
            }

            /* Consume trailing spaces that fit visually at the end of a soft-wrapped line. */
            while (chosen < seg_end && is_trailing_space_cp(items[chosen].cp)) {
                ++chosen;
            }

            uint32_t bs = items[p].byte_off;
            uint32_t be = items[chosen - 1u].byte_off + items[chosen - 1u].byte_len;
            ok = emit_line(&lvec, &gvec, items, p, chosen, bs, be, style, line_h, baseline, cur_y);
            cur_y = sat_add_fx(cur_y, line_h);
            p = chosen;
        }

        line_start = next_after_hard;
        ended_with_hard_break = has_hard && (line_start == (uint32_t)cp_count);
    }

    /* Terminal hard break (e.g. "Hello\n" or "Hello\r\n") emits a trailing empty line. */
    if (ok && ended_with_hard_break) {
        im_text_line trail = { trailing_byte_off, trailing_byte_off, gvec.count, 0, 0, cur_y, 0, 0, line_h, baseline };
        ok = line_vec_push(&lvec, trail);
        cur_y = sat_add_fx(cur_y, line_h);
    }

    alloc.free(alloc.ctx, cps, cp_count * sizeof(uint32_t));
    alloc.free(alloc.ctx, items, cp_count * sizeof(span_item));
    alloc.free(alloc.ctx, lbreaks, (cp_count + 1u) * sizeof(uint8_t));
    alloc.free(alloc.ctx, gbreaks, (cp_count + 1u) * sizeof(uint8_t));

    if (!ok) {
        if (lvec.data) alloc.free(alloc.ctx, lvec.data, (size_t)lvec.cap * sizeof(im_text_line));
        if (gvec.data) alloc.free(alloc.ctx, gvec.data, (size_t)gvec.cap * sizeof(im_text_glyph));
        return false;
    }

    /* Compute layout width and apply horizontal alignment (Left / Center / Right). */
    im_fixed max_line_w = 0;
    for (uint32_t i = 0; i < lvec.count; ++i) {
        if (lvec.data[i].width > max_line_w) max_line_w = lvec.data[i].width;
    }
    im_fixed align_w = (style->max_width > 0) ? style->max_width : max_line_w;

    for (uint32_t i = 0; i < lvec.count; ++i) {
        im_text_line *ln = &lvec.data[i];
        im_fixed lx = 0;
        if (style->align == IM_TEXT_ALIGN_CENTER) {
            lx = (im_fixed)(((int64_t)align_w - (int64_t)ln->width) / 2);
        } else if (style->align == IM_TEXT_ALIGN_RIGHT) {
            lx = (im_fixed)((int64_t)align_w - (int64_t)ln->width);
        }
        ln->x = lx;
        for (uint32_t g = 0; g < ln->glyph_count; ++g) {
            gvec.data[ln->glyph_start + g].x = sat_add_fx(gvec.data[ln->glyph_start + g].x, lx);
        }
    }

    layout->alloc = alloc;
    layout->lines = lvec.data;
    layout->line_count = lvec.count;
    layout->line_cap = lvec.cap;
    layout->glyphs = gvec.data;
    layout->glyph_count = gvec.count;
    layout->glyph_cap = gvec.cap;
    layout->width = max_line_w;
    layout->height = cur_y;
    return true;
}

void im_text_layout_destroy(im_text_layout *layout) {
    if (!layout) return;
    if (layout->alloc.free) {
        if (layout->lines) layout->alloc.free(layout->alloc.ctx, layout->lines, (size_t)layout->line_cap * sizeof(im_text_line));
        if (layout->glyphs) layout->alloc.free(layout->alloc.ctx, layout->glyphs, (size_t)layout->glyph_cap * sizeof(im_text_glyph));
    }
    zero_bytes(layout, sizeof(*layout));
}

bool im_text_layout_measure(im_allocator alloc, const char *utf8, size_t utf8_len,
                            const im_text_style *style, im_fixed *out_width, im_fixed *out_height) {
    im_text_layout tmp;
    if (!im_text_layout_build(&tmp, alloc, utf8, utf8_len, style)) return false;
    if (out_width) *out_width = tmp.width;
    if (out_height) *out_height = tmp.height;
    im_text_layout_destroy(&tmp);
    return true;
}

bool im_text_layout_draw(im_surface *surface, im_allocator alloc,
                         const im_text_layout *layout, const im_text_style *style,
                         im_fixed x, im_fixed y) {
    if (!surface || !layout || !style || !style->font) return false;
    for (uint32_t i = 0; i < layout->glyph_count; ++i) {
        const im_text_glyph *g = &layout->glyphs[i];
        im_fixed gx = sat_add_fx(x, g->x);
        im_fixed gy = sat_add_fx(y, g->y);
        if (!im_font_draw_glyph(surface, alloc, style->font, g->glyph_id, gx, gy, style->font_size, style->color)) {
            return false;
        }
    }
    return true;
}
