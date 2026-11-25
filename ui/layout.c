/* Impossible UI: WinUI-compatible layout rounding, measure and arrange. Portable. */
#include "layout.h"

/* Portable floor(x / 2^shift) toward -infinity for signed 64-bit integers. */
static int64_t floor_div_pow2(int64_t x, int shift) {
    if (x >= 0) return x >> shift;
    uint64_t mag = (uint64_t)(-(x + 1)) + 1u;
    uint64_t mask = ((uint64_t)1u << shift) - 1u;
    return -(int64_t)((mag + mask) >> shift);
}

static im_fixed clamp_i64_fixed(int64_t v) {
    if (v >= (int64_t)IM_FIXED_INF) return IM_FIXED_INF;
    if (v <= (int64_t)IM_FIXED_AUTO) return IM_FIXED_AUTO + 1;
    return (im_fixed)v;
}

static im_fixed add_fixed(im_fixed a, im_fixed b) {
    if (a == IM_FIXED_INF || b == IM_FIXED_INF) return IM_FIXED_INF;
    return clamp_i64_fixed((int64_t)a + (int64_t)b);
}

static im_fixed sub_nonneg(im_fixed a, im_fixed b) {
    if (a == IM_FIXED_INF) return IM_FIXED_INF;
    int64_t r = (int64_t)a - (int64_t)b;
    return r <= 0 ? 0 : clamp_i64_fixed(r);
}

static im_fixed min_fixed(im_fixed a, im_fixed b) { return a < b ? a : b; }
static im_fixed max_fixed(im_fixed a, im_fixed b) { return a > b ? a : b; }

im_fixed im_layout_round(im_fixed value, im_fixed scale) {
    if (value == IM_FIXED_AUTO || value == IM_FIXED_INF) return value;
    if (scale <= 0) scale = IM_ONE;
    if (scale == IM_ONE) {
        int64_t px = floor_div_pow2((int64_t)value + 32768, 16);
        return clamp_i64_fixed(px * (int64_t)IM_ONE);
    }
    /* Match WinUI CUIElement::LayoutRoundHelper: strictly unbiased XcpFloor(value * scale + 0.5) / scale. */
    int64_t scaled = (int64_t)value * (int64_t)scale;
    int64_t px = floor_div_pow2(scaled + ((int64_t)1 << 31), 32);
    int64_t num = px * ((int64_t)IM_ONE * (int64_t)IM_ONE);
    int64_t half = (int64_t)scale / 2;
    int64_t res = num >= 0 ? (num + half) / (int64_t)scale : -((-num + half) / (int64_t)scale);
    return clamp_i64_fixed(res);
}

static im_fixed round_if(im_fixed v, im_fixed scale, bool enabled) {
    return enabled ? im_layout_round(v, scale) : v;
}

static im_thickness round_thickness_if(im_thickness t, im_fixed scale, bool enabled) {
    return enabled ? im_layout_round_thickness(t, scale) : t;
}

im_thickness im_layout_round_thickness(im_thickness t, im_fixed scale) {
    return (im_thickness){
        im_layout_round(t.left, scale),
        im_layout_round(t.top, scale),
        im_layout_round(t.right, scale),
        im_layout_round(t.bottom, scale),
    };
}

static im_fixed round_half_if(im_fixed diff, im_fixed scale, bool enabled) {
    if (!enabled) return diff / 2;
    if (diff == IM_FIXED_AUTO || diff == IM_FIXED_INF) return diff;
    if (scale <= 0) scale = IM_ONE;
    if (scale == IM_ONE) {
        int64_t px = floor_div_pow2((int64_t)diff + (int64_t)IM_ONE, 17);
        return clamp_i64_fixed(px * (int64_t)IM_ONE);
    }
    /* Strictly unbiased floor((diff * scale) / 2 + 0.5) / scale without truncating diff / 2 first. */
    int64_t scaled = (int64_t)diff * (int64_t)scale;
    int64_t px = floor_div_pow2(scaled + ((int64_t)1 << 32), 33);
    int64_t num = px * ((int64_t)IM_ONE * (int64_t)IM_ONE);
    int64_t half = (int64_t)scale / 2;
    int64_t res = num >= 0 ? (num + half) / (int64_t)scale : -((-num + half) / (int64_t)scale);
    return clamp_i64_fixed(res);
}

/* Convert a layout-rounded 16.16 value back to exact 32.32 physical pixels (or raw value*scale when unrounded)
   so multi-term Grid sums never suffer 16.16 quantization drift across non-power-of-two DPI scales. */
static int64_t scaled_px_32_32(im_fixed v, im_fixed scale, bool already_rounded) {
    int64_t scaled = (int64_t)v * (int64_t)scale;
    if (!already_rounded) return scaled;
    int64_t px = floor_div_pow2(scaled + ((int64_t)1 << 31), 32);
    int64_t snapped = px << 32;
    int64_t err = scaled >= snapped ? (scaled - snapped) : (snapped - scaled);
    return (err <= (int64_t)scale) ? snapped : scaled;
}

static im_fixed round_from_32_32(int64_t scaled_sum, im_fixed scale) {
    int64_t px = floor_div_pow2(scaled_sum + ((int64_t)1 << 31), 32);
    int64_t num = px * ((int64_t)IM_ONE * (int64_t)IM_ONE);
    int64_t half = (int64_t)scale / 2;
    int64_t res = num >= 0 ? (num + half) / (int64_t)scale : -((-num + half) / (int64_t)scale);
    return clamp_i64_fixed(res);
}

/* WinUI Border.cpp:404 rounds BorderThickness edges; Padding stays unrounded until innerRect is arranged. */
static im_thickness combined_inset(const im_node *node, im_fixed scale, bool rounding) {
    im_thickness b = round_thickness_if(im_node_effective_border_thickness(node), scale, rounding);
    im_thickness p = im_node_effective_padding(node);
    return (im_thickness){
        add_fixed(b.left, p.left),
        add_fixed(b.top, p.top),
        add_fixed(b.right, p.right),
        add_fixed(b.bottom, p.bottom),
    };
}

static im_size combined_inset_size(const im_node *node, im_fixed scale, bool rounding) {
    im_thickness ins = combined_inset(node, scale, rounding);
    return (im_size){add_fixed(ins.left, ins.right), add_fixed(ins.top, ins.bottom)};
}

typedef struct min_max_box {
    im_fixed min_w;
    im_fixed max_w;
    im_fixed min_h;
    im_fixed max_h;
} min_max_box;

static min_max_box compute_min_max(const im_node *node, im_fixed scale, bool rounding) {
    im_fixed min_w = round_if(max_fixed(0, node->min_width), scale, rounding);
    im_fixed max_w = (node->max_width == IM_FIXED_INF) ? IM_FIXED_INF : round_if(max_fixed(0, node->max_width), scale, rounding);
    im_fixed min_h = round_if(max_fixed(0, node->min_height), scale, rounding);
    im_fixed max_h = (node->max_height == IM_FIXED_INF) ? IM_FIXED_INF : round_if(max_fixed(0, node->max_height), scale, rounding);

    if (node->width != IM_FIXED_AUTO) {
        im_fixed w = round_if(max_fixed(0, node->width), scale, rounding);
        max_w = max_fixed(min_fixed(w, max_w), min_w);
        min_w = max_w;
    } else if (max_w < min_w) {
        max_w = min_w;
    }

    if (node->height != IM_FIXED_AUTO) {
        im_fixed h = round_if(max_fixed(0, node->height), scale, rounding);
        max_h = max_fixed(min_fixed(h, max_h), min_h);
        min_h = max_h;
    } else if (max_h < min_h) {
        max_h = min_h;
    }
    return (min_max_box){min_w, max_w, min_h, max_h};
}

/* Grid track runtime state. */
typedef struct grid_track {
    im_grid_unit unit;
    im_fixed value;
    im_fixed min_s;
    im_fixed max_s;
    im_fixed size;
    im_fixed offset;
    bool locked;
} grid_track;

static uint32_t clamp_index(uint32_t idx, uint32_t count) {
    return (count == 0) ? 0u : (idx >= count ? count - 1u : idx);
}

static uint32_t clamp_span(uint32_t idx, uint32_t span, uint32_t count) {
    if (count == 0) return 1u;
    if (idx >= count) idx = count - 1u;
    if (span == 0) span = 1u;
    if (span > count - idx) span = count - idx;
    return span == 0 ? 1u : span;
}

static void init_grid_tracks(grid_track *tracks, const im_grid_def *defs, uint32_t def_count, im_fixed scale, bool rounding) {
    if (def_count == 0) {
        tracks[0].unit = IM_GRID_STAR;
        tracks[0].value = IM_ONE;
        tracks[0].min_s = 0;
        tracks[0].max_s = IM_FIXED_INF;
        tracks[0].size = 0;
        tracks[0].offset = 0;
        tracks[0].locked = false;
        return;
    }
    for (uint32_t i = 0; i < def_count; i++) {
        im_fixed mn = max_fixed(0, defs[i].min_size);
        im_fixed mx = (defs[i].max_size == IM_FIXED_INF) ? IM_FIXED_INF : max_fixed(0, defs[i].max_size);
        if (mx < mn) mx = mn;
        tracks[i].unit = defs[i].unit;
        tracks[i].value = defs[i].value;
        tracks[i].min_s = mn;
        tracks[i].max_s = mx;
        tracks[i].offset = 0;
        tracks[i].locked = false;
        if (defs[i].unit == IM_GRID_PIXEL) {
            im_fixed px = round_if(max_fixed(0, defs[i].value), scale, rounding);
            tracks[i].size = max_fixed(mn, min_fixed(px, mx));
        } else {
            tracks[i].size = mn;
        }
    }
}

static im_fixed span_sum(const grid_track *tracks, uint32_t start, uint32_t span, im_fixed spacing) {
    im_fixed total = 0;
    for (uint32_t i = 0; i < span; i++) {
        total = add_fixed(total, tracks[start + i].size);
    }
    if (span > 1u && spacing > 0) {
        total = add_fixed(total, clamp_i64_fixed((int64_t)(span - 1u) * (int64_t)spacing));
    }
    return total;
}

static bool span_has_unit(const grid_track *tracks, uint32_t start, uint32_t span, im_grid_unit unit) {
    for (uint32_t i = 0; i < span; i++) {
        if (tracks[start + i].unit == unit) return true;
    }
    return false;
}

static void expand_span_tracks(grid_track *tracks, uint32_t start, uint32_t span, im_fixed spacing, im_fixed needed, bool include_star) {
    im_fixed cur = span_sum(tracks, start, span, spacing);
    if (needed <= cur) return;
    im_fixed extra = needed - cur;
    for (uint32_t i = 0; i < span && extra > 0; i++) {
        grid_track *t = &tracks[start + i];
        if (t->unit == IM_GRID_PIXEL) continue;
        if (t->unit == IM_GRID_STAR && !include_star) continue;
        im_fixed room = (t->max_s == IM_FIXED_INF) ? extra : sub_nonneg(t->max_s, t->size);
        im_fixed add = min_fixed(extra, room);
        t->size = add_fixed(t->size, add);
        extra -= add;
    }
}

static void distribute_star_tracks(grid_track *tracks, uint32_t count, im_fixed avail, im_fixed spacing, im_fixed scale, bool rounding) {
    im_fixed total_sp = (count > 1u && spacing > 0) ? clamp_i64_fixed((int64_t)(count - 1u) * (int64_t)spacing) : 0;
    im_fixed fixed_sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (tracks[i].unit != IM_GRID_STAR) {
            tracks[i].locked = true;
            fixed_sum = add_fixed(fixed_sum, tracks[i].size);
        } else if (tracks[i].value <= 0) {
            /* 0* track gets zero share (clamped to min_s) and does not dilute positive-weight Star tracks. */
            tracks[i].size = tracks[i].min_s;
            tracks[i].locked = true;
            fixed_sum = add_fixed(fixed_sum, tracks[i].size);
        } else {
            tracks[i].locked = false;
        }
    }
    im_fixed rem = sub_nonneg(avail, add_fixed(fixed_sum, total_sp));

    for (;;) {
        int64_t weight_sum = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (!tracks[i].locked) {
                weight_sum += (int64_t)tracks[i].value;
            }
        }
        if (weight_sum <= 0) break;

        /* Exact water-filling: compare sum of clamped provisional shares against rem.
           If clamped_sum > rem, the true rate is lower so every min-clamped track is definitely at min_s.
           If clamped_sum < rem, the true rate is higher so every max-clamped track is definitely at max_s. */
        int64_t clamped_sum = 0;
        bool any_below_min = false;
        bool any_above_max = false;
        for (uint32_t i = 0; i < count; i++) {
            if (tracks[i].locked) continue;
            int64_t w = (int64_t)tracks[i].value;
            im_fixed share = clamp_i64_fixed(((int64_t)rem * w) / weight_sum);
            if (share < tracks[i].min_s) {
                any_below_min = true;
                clamped_sum += (int64_t)tracks[i].min_s;
            } else if (share > tracks[i].max_s) {
                any_above_max = true;
                clamped_sum += (int64_t)tracks[i].max_s;
            } else {
                clamped_sum += (int64_t)share;
            }
        }

        bool locked_any = false;
        if (any_below_min || any_above_max) {
            bool lock_mins = (clamped_sum >= (int64_t)rem) ? any_below_min : !any_above_max;
            im_fixed iter_rem = rem;
            for (uint32_t i = 0; i < count; i++) {
                if (tracks[i].locked) continue;
                int64_t w = (int64_t)tracks[i].value;
                im_fixed share = clamp_i64_fixed(((int64_t)iter_rem * w) / weight_sum);
                if (lock_mins && share < tracks[i].min_s) {
                    tracks[i].size = tracks[i].min_s;
                    tracks[i].locked = true;
                    rem = sub_nonneg(rem, tracks[i].size);
                    locked_any = true;
                } else if (!lock_mins && share > tracks[i].max_s) {
                    tracks[i].size = tracks[i].max_s;
                    tracks[i].locked = true;
                    rem = sub_nonneg(rem, tracks[i].size);
                    locked_any = true;
                }
            }
        }
        if (!locked_any) {
            int64_t rem_weight = weight_sum;
            im_fixed rem_space = rem;
            for (uint32_t i = 0; i < count; i++) {
                if (tracks[i].locked) continue;
                int64_t w = (int64_t)tracks[i].value;
                im_fixed share = (rem_weight == w) ? rem_space : clamp_i64_fixed(((int64_t)rem_space * w) / rem_weight);
                share = round_if(share, scale, rounding);
                share = max_fixed(tracks[i].min_s, min_fixed(share, tracks[i].max_s));
                if (rem_weight == w && share > rem_space && i > 0) {
                    /* Match WinUI Grid.cpp:1411: trim exact unrounded rounding excess from previous Star track. */
                    im_fixed excess = share - rem_space;
                    for (uint32_t p = i; p > 0; p--) {
                        if (!tracks[p - 1u].locked && tracks[p - 1u].size > tracks[p - 1u].min_s) {
                            im_fixed take = min_fixed(excess, tracks[p - 1u].size - tracks[p - 1u].min_s);
                            tracks[p - 1u].size = sub_nonneg(tracks[p - 1u].size, take);
                            break;
                        }
                    }
                }
                tracks[i].size = share;
                rem_space = sub_nonneg(rem_space, share);
                rem_weight -= w;
            }
            break;
        }
    }
}

static void compute_track_offsets(grid_track *tracks, uint32_t count, im_fixed spacing) {
    im_fixed pos = 0;
    for (uint32_t i = 0; i < count; i++) {
        tracks[i].offset = pos;
        pos = add_fixed(pos, tracks[i].size);
        if (i + 1u < count) pos = add_fixed(pos, spacing);
    }
}

static grid_track *alloc_tracks(im_tree *tree, grid_track *stack_buf, uint32_t stack_cap, uint32_t count) {
    if (count <= stack_cap) return stack_buf;
    if (!tree || !tree->alloc.alloc) return NULL;
    return (grid_track *)tree->alloc.alloc(tree->alloc.ctx, (size_t)count * sizeof(grid_track));
}

static void free_tracks(im_tree *tree, grid_track *buf, grid_track *stack_buf, uint32_t count) {
    if (buf && buf != stack_buf && tree && tree->alloc.free) {
        tree->alloc.free(tree->alloc.ctx, buf, (size_t)count * sizeof(grid_track));
    }
}

static im_size measure_grid(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding, bool *ok) {
    im_size inset_sz = combined_inset_size(node, scale, rounding);
    im_fixed inset_w = inset_sz.width;
    im_fixed inset_h = inset_sz.height;
    im_fixed inner_w = sub_nonneg(avail.width, inset_w);
    im_fixed inner_h = sub_nonneg(avail.height, inset_h);

    uint32_t rcount = node->row_count > 0 ? node->row_count : 1u;
    uint32_t ccount = node->col_count > 0 ? node->col_count : 1u;
    im_fixed row_sp = max_fixed(0, node->row_spacing);
    im_fixed col_sp = max_fixed(0, node->col_spacing);

    grid_track r_stack[16], c_stack[16];
    grid_track *rows = alloc_tracks(tree, r_stack, 16u, rcount);
    grid_track *cols = alloc_tracks(tree, c_stack, 16u, ccount);
    if (!rows || !cols) {
        free_tracks(tree, rows, r_stack, rcount);
        free_tracks(tree, cols, c_stack, ccount);
        if (ok) *ok = false;
        return (im_size){inset_w, inset_h};
    }

    init_grid_tracks(rows, node->rows, node->row_count, scale, rounding);
    init_grid_tracks(cols, node->cols, node->col_count, scale, rounding);

    bool inf_w = (inner_w == IM_FIXED_INF);
    bool inf_h = (inner_h == IM_FIXED_INF);
    node->measured_inf_w = inf_w;
    node->measured_inf_h = inf_h;

    /* Pass 1: single-span children not in finite Star tracks (Auto/Pixel and Star-under-inf). */
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            ch->desired_size = (im_size){0, 0};
            ch->unclipped_desired = (im_size){0, 0};
            ch->measure_dirty = false;
            continue;
        }
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        bool c_star = !inf_w && span_has_unit(cols, c, cs, IM_GRID_STAR);
        bool r_star = !inf_h && span_has_unit(rows, r, rs, IM_GRID_STAR);
        if (c_star || r_star) continue;

        im_fixed cw = (cols[c].unit == IM_GRID_PIXEL && cs == 1u) ? cols[c].size : IM_FIXED_INF;
        im_fixed rh = (rows[r].unit == IM_GRID_PIXEL && rs == 1u) ? rows[r].size : IM_FIXED_INF;
        if (cs > 1u && !span_has_unit(cols, c, cs, IM_GRID_AUTO) && !span_has_unit(cols, c, cs, IM_GRID_STAR)) {
            cw = span_sum(cols, c, cs, col_sp);
        }
        if (rs > 1u && !span_has_unit(rows, r, rs, IM_GRID_AUTO) && !span_has_unit(rows, r, rs, IM_GRID_STAR)) {
            rh = span_sum(rows, r, rs, row_sp);
        }
        im_size d = im_layout_measure(tree, ch, (im_size){cw, rh}, scale);
        if (cs == 1u && (cols[c].unit == IM_GRID_AUTO || (cols[c].unit == IM_GRID_STAR && inf_w))) {
            im_fixed cand = max_fixed(cols[c].min_s, min_fixed(d.width, cols[c].max_s));
            if (cand > cols[c].size) cols[c].size = cand;
        }
        if (rs == 1u && (rows[r].unit == IM_GRID_AUTO || (rows[r].unit == IM_GRID_STAR && inf_h))) {
            im_fixed cand = max_fixed(rows[r].min_s, min_fixed(d.height, rows[r].max_s));
            if (cand > rows[r].size) rows[r].size = cand;
        }
    }

    /* Pass 2: multi-span children in non-Star tracks. */
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        if (cs == 1u && rs == 1u) continue;
        bool c_star = !inf_w && span_has_unit(cols, c, cs, IM_GRID_STAR);
        bool r_star = !inf_h && span_has_unit(rows, r, rs, IM_GRID_STAR);
        if (c_star || r_star) continue;
        if (cs > 1u) expand_span_tracks(cols, c, cs, col_sp, ch->desired_size.width, inf_w);
        if (rs > 1u) expand_span_tracks(rows, r, rs, row_sp, ch->desired_size.height, inf_h);
    }

    /* Pass 3: distribute finite Star columns and rows. */
    if (!inf_w) distribute_star_tracks(cols, ccount, inner_w, col_sp, scale, rounding);
    if (!inf_h) distribute_star_tracks(rows, rcount, inner_h, row_sp, scale, rounding);

    /* Pass 4: measure children touching Star tracks, updating Auto (and unbounded Star) tracks if needed. */
    bool auto_changed = false;
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        bool c_star = !inf_w && span_has_unit(cols, c, cs, IM_GRID_STAR);
        bool r_star = !inf_h && span_has_unit(rows, r, rs, IM_GRID_STAR);
        if (!c_star && !r_star) continue;

        bool c_auto_like = !c_star && (span_has_unit(cols, c, cs, IM_GRID_AUTO) || (inf_w && span_has_unit(cols, c, cs, IM_GRID_STAR)));
        bool r_auto_like = !r_star && (span_has_unit(rows, r, rs, IM_GRID_AUTO) || (inf_h && span_has_unit(rows, r, rs, IM_GRID_STAR)));
        im_fixed cw = c_auto_like ? IM_FIXED_INF : span_sum(cols, c, cs, col_sp);
        im_fixed rh = r_auto_like ? IM_FIXED_INF : span_sum(rows, r, rs, row_sp);
        im_size d = im_layout_measure(tree, ch, (im_size){cw, rh}, scale);

        if (c_auto_like) {
            im_fixed before = span_sum(cols, c, cs, col_sp);
            expand_span_tracks(cols, c, cs, col_sp, d.width, inf_w);
            if (span_sum(cols, c, cs, col_sp) != before) auto_changed = true;
        }
        if (r_auto_like) {
            im_fixed before = span_sum(rows, r, rs, row_sp);
            expand_span_tracks(rows, r, rs, row_sp, d.height, inf_h);
            if (span_sum(rows, r, rs, row_sp) != before) auto_changed = true;
        }
    }

    if (auto_changed) {
        if (!inf_w) distribute_star_tracks(cols, ccount, inner_w, col_sp, scale, rounding);
        if (!inf_h) distribute_star_tracks(rows, rcount, inner_h, row_sp, scale, rounding);
    }

    /* Final measure for Star children so their desired_size matches final cell bounds. */
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        im_fixed cw = span_sum(cols, c, cs, col_sp);
        im_fixed rh = span_sum(rows, r, rs, row_sp);
        im_layout_measure(tree, ch, (im_size){cw, rh}, scale);
    }

    /* Desired size (WinUI Grid.cpp:1135): Pixel + Auto tracks + desired (not Star-expanded) size of Star tracks.
       Accumulate in exact 32.32 scaled physical pixels when rounding at non-100% scales so multi-term sums
       never lose sub-LSB precision while keeping im_layout_round strictly unbiased. */
    im_thickness b_th = round_thickness_if(im_node_effective_border_thickness(node), scale, rounding);
    im_thickness p_th = im_node_effective_padding(node);
    im_fixed total_w = 0;
    im_fixed total_h = 0;
    int64_t total_w_32 = scaled_px_32_32(b_th.left, scale, rounding) + scaled_px_32_32(b_th.right, scale, rounding) +
                         scaled_px_32_32(p_th.left, scale, false) + scaled_px_32_32(p_th.right, scale, false);
    int64_t total_h_32 = scaled_px_32_32(b_th.top, scale, rounding) + scaled_px_32_32(b_th.bottom, scale, rounding) +
                         scaled_px_32_32(p_th.top, scale, false) + scaled_px_32_32(p_th.bottom, scale, false);
    for (uint32_t c = 0; c < ccount; c++) {
        if (c > 0) {
            total_w = add_fixed(total_w, col_sp);
            total_w_32 += scaled_px_32_32(col_sp, scale, false);
        }
        im_fixed cw_term = 0;
        if (cols[c].unit != IM_GRID_STAR || inf_w) {
            cw_term = cols[c].size;
        } else {
            im_fixed star_d = cols[c].min_s;
            for (uint32_t i = 0; i < node->child_count; i++) {
                im_node *ch = node->children[i];
                if (ch->visibility == IM_VIS_COLLAPSED) continue;
                uint32_t cc = clamp_index(ch->grid_col, ccount);
                uint32_t cs = clamp_span(cc, ch->grid_col_span, ccount);
                if (cc == c && cs == 1u) {
                    im_fixed cand = min_fixed(ch->desired_size.width, cols[c].max_s);
                    if (cand > star_d) star_d = cand;
                }
            }
            cw_term = star_d;
        }
        total_w = add_fixed(total_w, cw_term);
        total_w_32 += scaled_px_32_32(cw_term, scale, rounding);
    }
    for (uint32_t r = 0; r < rcount; r++) {
        if (r > 0) {
            total_h = add_fixed(total_h, row_sp);
            total_h_32 += scaled_px_32_32(row_sp, scale, false);
        }
        im_fixed rh_term = 0;
        if (rows[r].unit != IM_GRID_STAR || inf_h) {
            rh_term = rows[r].size;
        } else {
            im_fixed star_d = rows[r].min_s;
            for (uint32_t i = 0; i < node->child_count; i++) {
                im_node *ch = node->children[i];
                if (ch->visibility == IM_VIS_COLLAPSED) continue;
                uint32_t rr = clamp_index(ch->grid_row, rcount);
                uint32_t rs = clamp_span(rr, ch->grid_row_span, rcount);
                if (rr == r && rs == 1u) {
                    im_fixed cand = min_fixed(ch->desired_size.height, rows[r].max_s);
                    if (cand > star_d) star_d = cand;
                }
            }
            rh_term = star_d;
        }
        total_h = add_fixed(total_h, rh_term);
        total_h_32 += scaled_px_32_32(rh_term, scale, rounding);
    }

    free_tracks(tree, rows, r_stack, rcount);
    free_tracks(tree, cols, c_stack, ccount);
    if (rounding && scale != IM_ONE) {
        return (im_size){round_from_32_32(total_w_32, scale), round_from_32_32(total_h_32, scale)};
    }
    return (im_size){add_fixed(total_w, inset_w), add_fixed(total_h, inset_h)};
}

static void arrange_grid(im_tree *tree, im_node *node, im_size final_size, im_fixed scale, bool rounding, bool *ok) {
    im_thickness inset = combined_inset(node, scale, rounding);
    im_fixed inset_w = add_fixed(inset.left, inset.right);
    im_fixed inset_h = add_fixed(inset.top, inset.bottom);
    im_fixed inner_w = sub_nonneg(final_size.width, inset_w);
    im_fixed inner_h = sub_nonneg(final_size.height, inset_h);

    uint32_t rcount = node->row_count > 0 ? node->row_count : 1u;
    uint32_t ccount = node->col_count > 0 ? node->col_count : 1u;
    im_fixed row_sp = max_fixed(0, node->row_spacing);
    im_fixed col_sp = max_fixed(0, node->col_spacing);

    grid_track r_stack[16], c_stack[16];
    grid_track *rows = alloc_tracks(tree, r_stack, 16u, rcount);
    grid_track *cols = alloc_tracks(tree, c_stack, 16u, ccount);
    if (!rows || !cols) {
        free_tracks(tree, rows, r_stack, rcount);
        free_tracks(tree, cols, c_stack, ccount);
        if (ok) *ok = false;
        return;
    }

    init_grid_tracks(rows, node->rows, node->row_count, scale, rounding);
    init_grid_tracks(cols, node->cols, node->col_count, scale, rounding);

    /* Populate Auto tracks (and Star tracks when measured unbounded) from children's desired_size. */
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        if (cs == 1u && (cols[c].unit == IM_GRID_AUTO || (cols[c].unit == IM_GRID_STAR && node->measured_inf_w))) {
            im_fixed cand = max_fixed(cols[c].min_s, min_fixed(ch->desired_size.width, cols[c].max_s));
            if (cand > cols[c].size) cols[c].size = cand;
        }
        if (rs == 1u && (rows[r].unit == IM_GRID_AUTO || (rows[r].unit == IM_GRID_STAR && node->measured_inf_h))) {
            im_fixed cand = max_fixed(rows[r].min_s, min_fixed(ch->desired_size.height, rows[r].max_s));
            if (cand > rows[r].size) rows[r].size = cand;
        }
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        if (cs > 1u && (node->measured_inf_w || !span_has_unit(cols, c, cs, IM_GRID_STAR))) {
            expand_span_tracks(cols, c, cs, col_sp, ch->desired_size.width, node->measured_inf_w);
        }
        if (rs > 1u && (node->measured_inf_h || !span_has_unit(rows, r, rs, IM_GRID_STAR))) {
            expand_span_tracks(rows, r, rs, row_sp, ch->desired_size.height, node->measured_inf_h);
        }
    }

    if (node->measured_inf_w) {
        for (uint32_t c = 0; c < ccount; c++) {
            if (cols[c].unit == IM_GRID_STAR && cols[c].size > cols[c].min_s) cols[c].min_s = cols[c].size;
        }
    }
    if (node->measured_inf_h) {
        for (uint32_t r = 0; r < rcount; r++) {
            if (rows[r].unit == IM_GRID_STAR && rows[r].size > rows[r].min_s) rows[r].min_s = rows[r].size;
        }
    }
    distribute_star_tracks(cols, ccount, inner_w, col_sp, scale, rounding);
    distribute_star_tracks(rows, rcount, inner_h, row_sp, scale, rounding);
    compute_track_offsets(cols, ccount, col_sp);
    compute_track_offsets(rows, rcount, row_sp);

    im_thickness b_th = round_thickness_if(im_node_effective_border_thickness(node), scale, rounding);
    im_thickness p_th = im_node_effective_padding(node);
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            ch->layout_rect = (im_rect){0, 0, 0, 0};
            ch->absolute_rect = (im_rect){0, 0, 0, 0};
            ch->arrange_dirty = false;
            continue;
        }
        uint32_t r = clamp_index(ch->grid_row, rcount);
        uint32_t c = clamp_index(ch->grid_col, ccount);
        uint32_t rs = clamp_span(r, ch->grid_row_span, rcount);
        uint32_t cs = clamp_span(c, ch->grid_col_span, ccount);
        im_fixed cx = add_fixed(inset.left, cols[c].offset);
        im_fixed cy = add_fixed(inset.top, rows[r].offset);
        if (rounding && scale != IM_ONE) {
            int64_t cx32 = scaled_px_32_32(b_th.left, scale, true) + scaled_px_32_32(p_th.left, scale, false);
            for (uint32_t k = 0; k < c; k++) {
                cx32 += scaled_px_32_32(cols[k].size, scale, true) + scaled_px_32_32(col_sp, scale, false);
            }
            int64_t cy32 = scaled_px_32_32(b_th.top, scale, true) + scaled_px_32_32(p_th.top, scale, false);
            for (uint32_t k = 0; k < r; k++) {
                cy32 += scaled_px_32_32(rows[k].size, scale, true) + scaled_px_32_32(row_sp, scale, false);
            }
            cx = round_from_32_32(cx32, scale);
            cy = round_from_32_32(cy32, scale);
        }
        im_fixed cw = span_sum(cols, c, cs, col_sp);
        im_fixed ch_h = span_sum(rows, r, rs, row_sp);
        im_layout_arrange(tree, ch, (im_rect){cx, cy, add_fixed(cx, cw), add_fixed(cy, ch_h)}, scale);
    }

    free_tracks(tree, rows, r_stack, rcount);
    free_tracks(tree, cols, c_stack, ccount);
}

static im_size measure_stack(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding) {
    im_thickness b = im_node_effective_border_thickness(node);
    im_thickness p = im_node_effective_padding(node);
    im_fixed inset_w = add_fixed(add_fixed(b.left, b.right), add_fixed(p.left, p.right));
    im_fixed inset_h = add_fixed(add_fixed(b.top, b.bottom), add_fixed(p.top, p.bottom));
    im_fixed inner_w = sub_nonneg(avail.width, inset_w);
    im_fixed inner_h = sub_nonneg(avail.height, inset_h);
    im_fixed sp = max_fixed(0, node->spacing);
    bool horiz = (node->orientation == IM_ORIENT_HORIZONTAL);
    (void)rounding;

    im_size child_avail = horiz ? (im_size){IM_FIXED_INF, inner_h} : (im_size){inner_w, IM_FIXED_INF};
    im_fixed sum_main = 0;
    int64_t sum_main_32 = 0;
    im_fixed max_cross = 0;
    uint32_t visible = 0;

    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            ch->desired_size = (im_size){0, 0};
            ch->unclipped_desired = (im_size){0, 0};
            ch->measure_dirty = false;
            continue;
        }
        im_size d = im_layout_measure(tree, ch, child_avail, scale);
        if (visible > 0) {
            sum_main = add_fixed(sum_main, sp);
            sum_main_32 += scaled_px_32_32(sp, scale, false);
        }
        if (horiz) {
            sum_main = add_fixed(sum_main, d.width);
            sum_main_32 += scaled_px_32_32(d.width, scale, ch->use_layout_rounding);
            if (d.height > max_cross) max_cross = d.height;
        } else {
            sum_main = add_fixed(sum_main, d.height);
            sum_main_32 += scaled_px_32_32(d.height, scale, ch->use_layout_rounding);
            if (d.width > max_cross) max_cross = d.width;
        }
        visible++;
    }

    im_fixed out_w = horiz ? add_fixed(sum_main, inset_w) : add_fixed(max_cross, inset_w);
    im_fixed out_h = horiz ? add_fixed(max_cross, inset_h) : add_fixed(sum_main, inset_h);
    if (rounding && scale != IM_ONE) {
        if (horiz) {
            out_w = round_from_32_32(sum_main_32 + scaled_px_32_32(inset_w, scale, false), scale);
        } else {
            out_h = round_from_32_32(sum_main_32 + scaled_px_32_32(inset_h, scale, false), scale);
        }
    }
    return (im_size){out_w, out_h};
}

static void arrange_stack(im_tree *tree, im_node *node, im_size final_size, im_fixed scale, bool rounding) {
    im_thickness b = im_node_effective_border_thickness(node);
    im_thickness p = im_node_effective_padding(node);
    im_thickness inset = {
        add_fixed(b.left, p.left),
        add_fixed(b.top, p.top),
        add_fixed(b.right, p.right),
        add_fixed(b.bottom, p.bottom),
    };
    im_fixed inset_w = add_fixed(inset.left, inset.right);
    im_fixed inset_h = add_fixed(inset.top, inset.bottom);
    im_fixed inner_w = sub_nonneg(final_size.width, inset_w);
    im_fixed inner_h = sub_nonneg(final_size.height, inset_h);
    im_fixed sp = max_fixed(0, node->spacing);
    bool horiz = (node->orientation == IM_ORIENT_HORIZONTAL);

    im_fixed cursor = horiz ? inset.left : inset.top;
    int64_t cursor_32 = scaled_px_32_32(cursor, scale, false);
    uint32_t visible = 0;

    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            ch->layout_rect = (im_rect){0, 0, 0, 0};
            ch->absolute_rect = (im_rect){0, 0, 0, 0};
            ch->arrange_dirty = false;
            continue;
        }
        if (visible > 0) {
            cursor = add_fixed(cursor, sp);
            cursor_32 += scaled_px_32_32(sp, scale, false);
        }
        im_fixed pos = (rounding && scale != IM_ONE) ? round_from_32_32(cursor_32, scale) : cursor;
        if (horiz) {
            im_fixed cw = ch->desired_size.width;
            im_fixed ch_h = max_fixed(inner_h, ch->desired_size.height);
            im_layout_arrange(tree, ch, (im_rect){pos, inset.top, add_fixed(pos, cw), add_fixed(inset.top, ch_h)}, scale);
            cursor = add_fixed(cursor, cw);
            cursor_32 += scaled_px_32_32(cw, scale, ch->use_layout_rounding);
        } else {
            im_fixed cw = max_fixed(inner_w, ch->desired_size.width);
            im_fixed ch_h = ch->desired_size.height;
            im_layout_arrange(tree, ch, (im_rect){inset.left, pos, add_fixed(inset.left, cw), add_fixed(pos, ch_h)}, scale);
            cursor = add_fixed(cursor, ch_h);
            cursor_32 += scaled_px_32_32(ch_h, scale, ch->use_layout_rounding);
        }
        visible++;
    }
}

static im_size measure_single_box(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding) {
    im_thickness inset = combined_inset(node, scale, rounding);
    im_fixed inset_w = add_fixed(inset.left, inset.right);
    im_fixed inset_h = add_fixed(inset.top, inset.bottom);
    im_fixed inner_w = sub_nonneg(avail.width, inset_w);
    im_fixed inner_h = sub_nonneg(avail.height, inset_h);

    im_size child_d = {0, 0};
    if (node->child_count > 0) {
        if (node->children[0]->visibility == IM_VIS_COLLAPSED) {
            node->children[0]->desired_size = (im_size){0, 0};
            node->children[0]->unclipped_desired = (im_size){0, 0};
            node->children[0]->measure_dirty = false;
        } else {
            child_d = im_layout_measure(tree, node->children[0], (im_size){inner_w, inner_h}, scale);
        }
    }
    return (im_size){add_fixed(child_d.width, inset_w), add_fixed(child_d.height, inset_h)};
}

static im_fixed align_offset(im_align align, im_fixed slot, im_fixed item, im_fixed scale, bool rounding) {
    im_fixed diff = slot - item;
    if (diff <= 0 && align == IM_ALIGN_STRETCH) return 0;
    switch (align) {
    case IM_ALIGN_START:
        return 0;
    case IM_ALIGN_CENTER:
        return round_half_if(diff, scale, rounding);
    case IM_ALIGN_END:
        return diff;
    case IM_ALIGN_STRETCH:
    default:
        return round_half_if(diff, scale, rounding);
    }
}

static void arrange_single_box(im_tree *tree, im_node *node, im_size final_size, im_fixed scale, bool rounding, bool use_content_align) {
    if (node->child_count == 0) return;
    im_node *ch = node->children[0];
    if (ch->visibility == IM_VIS_COLLAPSED) {
        ch->layout_rect = (im_rect){0, 0, 0, 0};
        ch->absolute_rect = (im_rect){0, 0, 0, 0};
        ch->arrange_dirty = false;
        return;
    }
    im_thickness inset = combined_inset(node, scale, rounding);
    im_fixed inset_w = add_fixed(inset.left, inset.right);
    im_fixed inset_h = add_fixed(inset.top, inset.bottom);
    im_fixed inner_w = sub_nonneg(final_size.width, inset_w);
    im_fixed inner_h = sub_nonneg(final_size.height, inset_h);

    if (!use_content_align) {
        im_layout_arrange(tree, ch, (im_rect){inset.left, inset.top, add_fixed(inset.left, inner_w), add_fixed(inset.top, inner_h)}, scale);
        return;
    }

    im_align ha = im_node_effective_h_content_align(node);
    im_align va = im_node_effective_v_content_align(node);
    im_fixed slot_w = (ha == IM_ALIGN_STRETCH) ? inner_w : min_fixed(inner_w, ch->desired_size.width);
    im_fixed slot_h = (va == IM_ALIGN_STRETCH) ? inner_h : min_fixed(inner_h, ch->desired_size.height);
    im_fixed al_x = align_offset(ha, inner_w, slot_w, scale, rounding);
    im_fixed al_y = align_offset(va, inner_h, slot_h, scale, rounding);
    im_fixed ox = add_fixed(inset.left, al_x);
    im_fixed oy = add_fixed(inset.top, al_y);
    if (rounding && scale != IM_ONE) {
        im_thickness b_th = round_thickness_if(im_node_effective_border_thickness(node), scale, true);
        im_thickness p_th = im_node_effective_padding(node);
        ox = round_from_32_32(scaled_px_32_32(b_th.left, scale, true) + scaled_px_32_32(p_th.left, scale, false) + scaled_px_32_32(al_x, scale, true), scale);
        oy = round_from_32_32(scaled_px_32_32(b_th.top, scale, true) + scaled_px_32_32(p_th.top, scale, false) + scaled_px_32_32(al_y, scale, true), scale);
    }
    im_layout_arrange(tree, ch, (im_rect){ox, oy, add_fixed(ox, slot_w), add_fixed(oy, slot_h)}, scale);
}

static im_size measure_override(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding, bool *ok) {
    switch (node->kind) {
    case IM_ELEM_GRID:
        return measure_grid(tree, node, avail, scale, rounding, ok);
    case IM_ELEM_STACK_PANEL:
    case IM_ELEM_ITEMS_STACK_PANEL:
        return measure_stack(tree, node, avail, scale, rounding);
    case IM_ELEM_BORDER:
    case IM_ELEM_CONTENT_PRESENTER:
        return measure_single_box(tree, node, avail, scale, rounding);
    case IM_ELEM_BUTTON:
    default:
        if (node->child_count > 0 && node->children[0]->visibility != IM_VIS_COLLAPSED) {
            return im_layout_measure(tree, node->children[0], avail, scale);
        }
        return (im_size){
            round_if(max_fixed(0, node->intrinsic_width), scale, rounding),
            round_if(max_fixed(0, node->intrinsic_height), scale, rounding),
        };
    }
}

static void arrange_override(im_tree *tree, im_node *node, im_size final_size, im_fixed scale, bool rounding, bool *ok) {
    switch (node->kind) {
    case IM_ELEM_GRID:
        arrange_grid(tree, node, final_size, scale, rounding, ok);
        break;
    case IM_ELEM_STACK_PANEL:
    case IM_ELEM_ITEMS_STACK_PANEL:
        arrange_stack(tree, node, final_size, scale, rounding);
        break;
    case IM_ELEM_BORDER:
        arrange_single_box(tree, node, final_size, scale, rounding, false);
        break;
    case IM_ELEM_CONTENT_PRESENTER:
        arrange_single_box(tree, node, final_size, scale, rounding, true);
        break;
    case IM_ELEM_BUTTON:
    default:
        if (node->child_count > 0) {
            im_layout_arrange(tree, node->children[0], (im_rect){0, 0, final_size.width, final_size.height}, scale);
        }
        break;
    }
}

im_size im_layout_measure(im_tree *tree, im_node *node, im_size available, im_fixed scale) {
    if (!node) return (im_size){0, 0};
    if (scale <= 0) scale = IM_ONE;
    if (!node->measure_dirty && node->has_measured &&
        node->last_avail.width == available.width &&
        node->last_avail.height == available.height &&
        node->last_measure_scale == scale) {
        return node->desired_size;
    }

    node->last_avail = available;
    node->last_measure_scale = scale;

    if (node->visibility == IM_VIS_COLLAPSED) {
        node->desired_size = (im_size){0, 0};
        node->unclipped_desired = (im_size){0, 0};
        node->measure_dirty = false;
        node->has_measured = true;
        return node->desired_size;
    }

    bool rounding = node->use_layout_rounding;
    /* WinUI layoutelement.cpp:80 rounds the combined margin width/height sum. */
    im_fixed m_w = round_if(add_fixed(node->margin.left, node->margin.right), scale, rounding);
    im_fixed m_h = round_if(add_fixed(node->margin.top, node->margin.bottom), scale, rounding);

    im_fixed fw_w = sub_nonneg(available.width, m_w);
    im_fixed fw_h = sub_nonneg(available.height, m_h);

    min_max_box mm = compute_min_max(node, scale, rounding);
    im_fixed child_w = max_fixed(mm.min_w, min_fixed(fw_w, mm.max_w));
    im_fixed child_h = max_fixed(mm.min_h, min_fixed(fw_h, mm.max_h));

    bool ok = true;
    im_size inner = measure_override(tree, node, (im_size){child_w, child_h}, scale, rounding, &ok);
    for (uint32_t i = 0; i < node->child_count && ok; i++) {
        if (node->children[i]->visibility != IM_VIS_COLLAPSED && node->children[i]->measure_dirty) {
            ok = false;
        }
    }

    im_fixed unclipped_w = max_fixed(inner.width, mm.min_w);
    im_fixed unclipped_h = max_fixed(inner.height, mm.min_h);
    if (unclipped_w > mm.max_w) unclipped_w = mm.max_w;
    if (unclipped_h > mm.max_h) unclipped_h = mm.max_h;

    unclipped_w = round_if(unclipped_w, scale, rounding);
    unclipped_h = round_if(unclipped_h, scale, rounding);
    node->unclipped_desired = (im_size){unclipped_w, unclipped_h};

    im_fixed des_w = add_fixed(unclipped_w, m_w);
    im_fixed des_h = add_fixed(unclipped_h, m_h);
    if (available.width != IM_FIXED_INF && des_w > available.width) des_w = available.width;
    if (available.height != IM_FIXED_INF && des_h > available.height) des_h = available.height;
    des_w = round_if(max_fixed(0, des_w), scale, rounding);
    des_h = round_if(max_fixed(0, des_h), scale, rounding);

    node->desired_size = (im_size){des_w, des_h};
    if (ok) {
        node->measure_dirty = false;
        node->has_measured = true;
    } else {
        node->measure_dirty = true;
        node->has_measured = false;
    }
    return node->desired_size;
}

static bool rect_eq(im_rect a, im_rect b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static void compute_absolute_rect(im_node *node) {
    if (!node) return;
    if (node->visibility == IM_VIS_COLLAPSED) {
        node->absolute_rect = (im_rect){0, 0, 0, 0};
        return;
    }
    im_fixed arrange_w = node->layout_rect.right - node->layout_rect.left;
    im_fixed arrange_h = node->layout_rect.bottom - node->layout_rect.top;
    if (!node->parent) {
        node->absolute_rect = node->layout_rect;
        node->last_parent_abs = (im_rect){0, 0, 0, 0};
        node->last_parent_flow = IM_FLOW_LTR;
    } else {
        im_rect pabs = node->parent->absolute_rect;
        im_flow_dir pflow = im_node_effective_flow_direction(node->parent);
        im_fixed pw = pabs.right - pabs.left;
        im_fixed abs_x = (pflow == IM_FLOW_RTL)
            ? add_fixed(pabs.left, (pw - node->layout_rect.left - arrange_w))
            : add_fixed(pabs.left, node->layout_rect.left);
        im_fixed abs_y = add_fixed(pabs.top, node->layout_rect.top);
        node->absolute_rect = (im_rect){abs_x, abs_y, add_fixed(abs_x, arrange_w), add_fixed(abs_y, arrange_h)};
        node->last_parent_abs = pabs;
        node->last_parent_flow = pflow;
    }
}

static void propagate_absolute_rects(im_node *node) {
    if (!node) return;
    compute_absolute_rect(node);
    for (uint32_t i = 0; i < node->child_count; i++) {
        propagate_absolute_rects(node->children[i]);
    }
}

void im_layout_arrange(im_tree *tree, im_node *node, im_rect final_rect, im_fixed scale) {
    if (!node) return;
    if (scale <= 0) scale = IM_ONE;
    im_rect cur_pabs = node->parent ? node->parent->absolute_rect : (im_rect){0, 0, 0, 0};
    im_flow_dir cur_pflow = node->parent ? im_node_effective_flow_direction(node->parent) : IM_FLOW_LTR;
    if (!node->arrange_dirty && node->has_arranged &&
        rect_eq(node->last_arrange_rect, final_rect) &&
        node->last_arrange_scale == scale) {
        if (!rect_eq(node->last_parent_abs, cur_pabs) || node->last_parent_flow != cur_pflow) {
            propagate_absolute_rects(node);
        }
        return;
    }

    node->last_arrange_rect = final_rect;
    node->last_arrange_scale = scale;

    if (node->visibility == IM_VIS_COLLAPSED) {
        node->layout_rect = (im_rect){0, 0, 0, 0};
        node->absolute_rect = (im_rect){0, 0, 0, 0};
        node->last_parent_abs = cur_pabs;
        node->last_parent_flow = cur_pflow;
        node->arrange_dirty = false;
        node->has_arranged = true;
        return;
    }

    bool rounding = node->use_layout_rounding;
    im_fixed rx = round_if(final_rect.left, scale, rounding);
    im_fixed ry = round_if(final_rect.top, scale, rounding);
    im_fixed rw = round_if(max_fixed(0, final_rect.right - final_rect.left), scale, rounding);
    im_fixed rh = round_if(max_fixed(0, final_rect.bottom - final_rect.top), scale, rounding);

    im_thickness m = round_thickness_if(node->margin, scale, rounding);
    im_fixed m_w = round_if(add_fixed(node->margin.left, node->margin.right), scale, rounding);
    im_fixed m_h = round_if(add_fixed(node->margin.top, node->margin.bottom), scale, rounding);

    im_fixed client_w = sub_nonneg(rw, m_w);
    im_fixed client_h = sub_nonneg(rh, m_h);

    min_max_box mm = compute_min_max(node, scale, rounding);

    im_fixed arrange_w = node->unclipped_desired.width;
    if (node->h_align == IM_ALIGN_STRETCH && node->width == IM_FIXED_AUTO) {
        arrange_w = max_fixed(arrange_w, client_w);
    }
    if (arrange_w > mm.max_w) arrange_w = mm.max_w;
    if (arrange_w < mm.min_w) arrange_w = mm.min_w;
    if (node->width == IM_FIXED_AUTO && arrange_w > client_w && node->h_align == IM_ALIGN_STRETCH) {
        arrange_w = max_fixed(client_w, mm.min_w);
    }
    arrange_w = round_if(arrange_w, scale, rounding);

    im_fixed arrange_h = node->unclipped_desired.height;
    if (node->v_align == IM_ALIGN_STRETCH && node->height == IM_FIXED_AUTO) {
        arrange_h = max_fixed(arrange_h, client_h);
    }
    if (arrange_h > mm.max_h) arrange_h = mm.max_h;
    if (arrange_h < mm.min_h) arrange_h = mm.min_h;
    if (node->height == IM_FIXED_AUTO && arrange_h > client_h && node->v_align == IM_ALIGN_STRETCH) {
        arrange_h = max_fixed(client_h, mm.min_h);
    }
    arrange_h = round_if(arrange_h, scale, rounding);

    im_fixed ox = align_offset(node->h_align, client_w, arrange_w, scale, rounding);
    im_fixed oy = align_offset(node->v_align, client_h, arrange_h, scale, rounding);

    im_fixed lx = add_fixed(add_fixed(rx, m.left), ox);
    im_fixed ly = add_fixed(add_fixed(ry, m.top), oy);
    node->layout_rect = (im_rect){lx, ly, add_fixed(lx, arrange_w), add_fixed(ly, arrange_h)};

    /* Compute root-relative absolute_rect (applying parent RTL mirroring when active). */
    compute_absolute_rect(node);

    bool ok = true;
    arrange_override(tree, node, (im_size){arrange_w, arrange_h}, scale, rounding, &ok);
    for (uint32_t i = 0; i < node->child_count && ok; i++) {
        if (node->children[i]->visibility != IM_VIS_COLLAPSED && node->children[i]->arrange_dirty) {
            ok = false;
        }
    }

    if (ok) {
        node->arrange_dirty = false;
        node->has_arranged = true;
    } else {
        node->arrange_dirty = true;
        node->has_arranged = false;
    }
}

void im_layout_run(im_tree *tree, im_node *root, im_fixed avail_w, im_fixed avail_h, im_fixed scale) {
    if (!root) return;
    im_layout_measure(tree, root, (im_size){avail_w, avail_h}, scale);
    im_layout_arrange(tree, root, (im_rect){0, 0, avail_w, avail_h}, scale);
}
