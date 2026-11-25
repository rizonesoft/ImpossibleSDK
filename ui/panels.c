/* Impossible UI: Canvas, Viewbox, UniformGrid, WrapPanel, RelativePanel,
   VariableSizedWrapGrid, ItemsWrapGrid, and ItemsStackPanel layout. Portable. */
#include "panels.h"
#include "transform.h"

static im_fixed clamp_i64_fx(int64_t v) {
    if (v >= (int64_t)IM_FIXED_INF) return IM_FIXED_INF;
    if (v <= (int64_t)IM_FIXED_AUTO) return IM_FIXED_AUTO + 1;
    return (im_fixed)v;
}

static im_fixed add_fx(im_fixed a, im_fixed b) {
    if (a == IM_FIXED_INF || b == IM_FIXED_INF) return IM_FIXED_INF;
    return clamp_i64_fx((int64_t)a + (int64_t)b);
}

static im_fixed sub_nonneg_fx(im_fixed a, im_fixed b) {
    if (a == IM_FIXED_INF) return IM_FIXED_INF;
    int64_t r = (int64_t)a - (int64_t)b;
    return r <= 0 ? 0 : clamp_i64_fx(r);
}

static im_fixed max_fx(im_fixed a, im_fixed b) { return a > b ? a : b; }

static im_fixed round_if_fx(im_fixed v, im_fixed scale, bool enabled) {
    return enabled ? im_layout_round(v, scale) : v;
}

static im_thickness panel_inset(const im_node *node, im_fixed scale, bool rounding) {
    im_thickness b = im_node_effective_border_thickness(node);
    if (rounding) b = im_layout_round_thickness(b, scale);
    im_thickness p = im_node_effective_padding(node);
    return (im_thickness){
        add_fx(b.left, p.left),
        add_fx(b.top, p.top),
        add_fx(b.right, p.right),
        add_fx(b.bottom, p.bottom),
    };
}

static bool pstr_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

static void record_layout_error(im_tree *tree, im_layout_error err, const char *name) {
    if (!tree) return;
    tree->layout_error = err;
    size_t i = 0u;
    if (name) {
        while (name[i] != '\0' && i + 1u < sizeof(tree->layout_error_name)) {
            tree->layout_error_name[i] = name[i];
            i++;
        }
    }
    tree->layout_error_name[i] = '\0';
}

static uint32_t isqrt_u32_ceil(uint32_t n) {
    if (n <= 1u) return n;
    uint32_t r = 1u;
    while (r <= 65535u && r * r < n) r++;
    return r;
}

/* ---------- Canvas ---------- */

static im_size measure_canvas(im_tree *tree, im_node *node, im_fixed scale) {
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            (void)im_layout_measure(tree, ch, (im_size){0, 0}, scale);
        } else {
            (void)im_layout_measure(tree, ch, (im_size){IM_FIXED_INF, IM_FIXED_INF}, scale);
        }
    }
    return (im_size){0, 0};
}

static void arrange_canvas(im_tree *tree, im_node *node, im_fixed scale) {
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            ch->layout_rect = (im_rect){0, 0, 0, 0};
            ch->absolute_rect = (im_rect){0, 0, 0, 0};
            ch->arrange_dirty = false;
            ch->has_arranged = true;
            continue;
        }
        im_fixed x = ch->canvas_left;
        im_fixed y = ch->canvas_top;
        im_rect r = { x, y, add_fx(x, ch->desired_size.width), add_fx(y, ch->desired_size.height) };
        im_layout_arrange(tree, ch, r, scale);
    }
}

/* ---------- Viewbox (pinned dxaml/xcp/core/core/elements/Viewbox.cpp) ---------- */

static void compute_viewbox_scale(im_size avail, im_size content, im_stretch stretch,
                                  im_stretch_dir dir, int64_t *out_sx_q30, int64_t *out_sy_q30) {
    int64_t sx = IM_TR_ONE;
    int64_t sy = IM_TR_ONE;
    bool inf_w = (avail.width == IM_FIXED_INF);
    bool inf_h = (avail.height == IM_FIXED_INF);

    if (stretch != IM_STRETCH_NONE && (!inf_w || !inf_h)) {
        sx = (content.width <= 0) ? 0 : (inf_w ? IM_TR_ONE : im_sdiv128_i64((__int128)avail.width * IM_TR_ONE, content.width));
        sy = (content.height <= 0) ? 0 : (inf_h ? IM_TR_ONE : im_sdiv128_i64((__int128)avail.height * IM_TR_ONE, content.height));
        if (inf_w) {
            sx = sy;
        } else if (inf_h) {
            sy = sx;
        } else if (stretch == IM_STRETCH_UNIFORM) {
            int64_t m = (sx < sy) ? sx : sy;
            sx = sy = m;
        } else if (stretch == IM_STRETCH_UNIFORM_TO_FILL) {
            int64_t m = (sx > sy) ? sx : sy;
            sx = sy = m;
        }
        if (dir == IM_STRETCH_DIR_UP_ONLY) {
            if (sx < IM_TR_ONE) sx = IM_TR_ONE;
            if (sy < IM_TR_ONE) sy = IM_TR_ONE;
        } else if (dir == IM_STRETCH_DIR_DOWN_ONLY) {
            if (sx > IM_TR_ONE) sx = IM_TR_ONE;
            if (sy > IM_TR_ONE) sy = IM_TR_ONE;
        }
    }
    *out_sx_q30 = sx;
    *out_sy_q30 = sy;
}

static im_fixed scale_fx_q30(im_fixed v, int64_t q30) {
    if (v <= 0 || q30 <= 0) return 0;
    __int128 p = ((__int128)v * (__int128)q30 + (IM_TR_ONE >> 1)) >> IM_TR_SHIFT;
    if (p >= (__int128)IM_FIXED_INF) return IM_FIXED_INF;
    return (im_fixed)p;
}

static im_size measure_viewbox(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding) {
    if (node->child_count == 0u) {
        node->has_viewbox_transform = false;
        return (im_size){0, 0};
    }
    im_node *ch = node->children[0];
    if (ch->visibility == IM_VIS_COLLAPSED) {
        (void)im_layout_measure(tree, ch, (im_size){0, 0}, scale);
        node->has_viewbox_transform = false;
        return (im_size){0, 0};
    }
    im_size child_d = im_layout_measure(tree, ch, (im_size){IM_FIXED_INF, IM_FIXED_INF}, scale);
    int64_t sx = IM_TR_ONE, sy = IM_TR_ONE;
    compute_viewbox_scale(avail, child_d, node->viewbox_stretch, node->viewbox_stretch_dir, &sx, &sy);
    return (im_size){
        round_if_fx(scale_fx_q30(child_d.width, sx), scale, rounding),
        round_if_fx(scale_fx_q30(child_d.height, sy), scale, rounding),
    };
}

static void arrange_viewbox(im_tree *tree, im_node *node, im_size final_size, im_fixed scale) {
    if (node->child_count == 0u) {
        node->has_viewbox_transform = false;
        return;
    }
    im_node *ch = node->children[0];
    if (ch->visibility == IM_VIS_COLLAPSED) {
        ch->layout_rect = (im_rect){0, 0, 0, 0};
        ch->absolute_rect = (im_rect){0, 0, 0, 0};
        ch->arrange_dirty = false;
        ch->has_arranged = true;
        node->has_viewbox_transform = false;
        return;
    }
    im_size child_d = ch->desired_size;
    int64_t sx = IM_TR_ONE, sy = IM_TR_ONE;
    compute_viewbox_scale(final_size, child_d, node->viewbox_stretch, node->viewbox_stretch_dir, &sx, &sy);
    im_layout_arrange(tree, ch, (im_rect){0, 0, child_d.width, child_d.height}, scale);
    node->viewbox_transform = (im_transform){ sx, 0, 0, sy, 0, 0 };
    node->has_viewbox_transform = (sx != IM_TR_ONE || sy != IM_TR_ONE);
}

/* ---------- UniformGrid ---------- */

static void compute_uniform_dims(const im_node *node, uint32_t vis_n,
                                 uint32_t *out_rows, uint32_t *out_cols, uint32_t *out_first_col) {
    uint32_t rows = node->uniform_rows;
    uint32_t cols = node->uniform_cols;
    uint32_t first_col = node->uniform_first_col;
    if (rows == 0u && cols == 0u) {
        uint32_t total = vis_n + first_col;
        cols = isqrt_u32_ceil(total);
        if (cols == 0u) cols = 1u;
        rows = (total + cols - 1u) / cols;
        if (rows == 0u) rows = 1u;
    } else if (rows == 0u) {
        if (first_col >= cols) first_col = 0u;
        uint32_t total = vis_n + first_col;
        rows = (total + cols - 1u) / cols;
        if (rows == 0u) rows = 1u;
    } else if (cols == 0u) {
        uint32_t total = vis_n + first_col;
        cols = (total + rows - 1u) / rows;
        if (cols == 0u) cols = 1u;
        if (first_col >= cols) first_col = 0u;
    } else {
        if (first_col >= cols) first_col = 0u;
    }
    *out_rows = rows;
    *out_cols = cols;
    *out_first_col = first_col;
}

static im_size measure_uniform_grid(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_fixed ins_w = add_fx(ins.left, ins.right);
    im_fixed ins_h = add_fx(ins.top, ins.bottom);
    uint32_t vis_n = 0u;
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (node->children[i]->visibility == IM_VIS_COLLAPSED) {
            (void)im_layout_measure(tree, node->children[i], (im_size){0, 0}, scale);
        } else {
            vis_n++;
        }
    }
    if (vis_n == 0u) return (im_size){ ins_w, ins_h };

    uint32_t rows = 1u, cols = 1u, first_col = 0u;
    compute_uniform_dims(node, vis_n, &rows, &cols, &first_col);
    im_fixed csp = max_fx(0, node->col_spacing);
    im_fixed rsp = max_fx(0, node->row_spacing);
    im_fixed total_csp = clamp_i64_fx((int64_t)(cols - 1u) * (int64_t)csp);
    im_fixed total_rsp = clamp_i64_fx((int64_t)(rows - 1u) * (int64_t)rsp);

    im_fixed inner_w = sub_nonneg_fx(avail.width, ins_w);
    im_fixed inner_h = sub_nonneg_fx(avail.height, ins_h);
    im_fixed cell_w = (inner_w == IM_FIXED_INF) ? IM_FIXED_INF : (im_fixed)(sub_nonneg_fx(inner_w, total_csp) / (int32_t)cols);
    im_fixed cell_h = (inner_h == IM_FIXED_INF) ? IM_FIXED_INF : (im_fixed)(sub_nonneg_fx(inner_h, total_rsp) / (int32_t)rows);

    im_fixed max_w = 0, max_h = 0;
    uint32_t vis_idx = 0u;
    uint32_t cap = rows * cols;
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t slot = first_col + vis_idx++;
        if (slot >= cap) {
            (void)im_layout_measure(tree, ch, (im_size){0, 0}, scale);
            continue;
        }
        im_size d = im_layout_measure(tree, ch, (im_size){cell_w, cell_h}, scale);
        if (d.width > max_w) max_w = d.width;
        if (d.height > max_h) max_h = d.height;
    }

    im_fixed out_w = add_fx(add_fx(clamp_i64_fx((int64_t)max_w * (int64_t)cols), total_csp), ins_w);
    im_fixed out_h = add_fx(add_fx(clamp_i64_fx((int64_t)max_h * (int64_t)rows), total_rsp), ins_h);
    return (im_size){ round_if_fx(out_w, scale, rounding), round_if_fx(out_h, scale, rounding) };
}

static void arrange_uniform_grid(im_tree *tree, im_node *node, im_size final_size, im_fixed scale, bool rounding) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_fixed ins_w = add_fx(ins.left, ins.right);
    im_fixed ins_h = add_fx(ins.top, ins.bottom);
    uint32_t vis_n = 0u;
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (node->children[i]->visibility != IM_VIS_COLLAPSED) vis_n++;
    }
    if (vis_n == 0u) return;

    uint32_t rows = 1u, cols = 1u, first_col = 0u;
    compute_uniform_dims(node, vis_n, &rows, &cols, &first_col);
    im_fixed csp = max_fx(0, node->col_spacing);
    im_fixed rsp = max_fx(0, node->row_spacing);
    im_fixed total_csp = clamp_i64_fx((int64_t)(cols - 1u) * (int64_t)csp);
    im_fixed total_rsp = clamp_i64_fx((int64_t)(rows - 1u) * (int64_t)rsp);
    im_fixed inner_w = sub_nonneg_fx(final_size.width, ins_w);
    im_fixed inner_h = sub_nonneg_fx(final_size.height, ins_h);
    im_fixed cell_w = (im_fixed)(sub_nonneg_fx(inner_w, total_csp) / (int32_t)cols);
    im_fixed cell_h = (im_fixed)(sub_nonneg_fx(inner_h, total_rsp) / (int32_t)rows);

    uint32_t vis_idx = 0u;
    uint32_t cap = rows * cols;
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            ch->layout_rect = (im_rect){0, 0, 0, 0};
            ch->absolute_rect = (im_rect){0, 0, 0, 0};
            ch->arrange_dirty = false;
            ch->has_arranged = true;
            continue;
        }
        uint32_t slot = first_col + vis_idx++;
        if (slot >= cap) {
            ch->layout_rect = (im_rect){0, 0, 0, 0};
            ch->absolute_rect = (im_rect){0, 0, 0, 0};
            ch->arrange_dirty = false;
            ch->has_arranged = true;
            continue;
        }
        uint32_t r = slot / cols;
        uint32_t c = slot % cols;
        im_fixed x = add_fx(ins.left, clamp_i64_fx((int64_t)c * ((int64_t)cell_w + (int64_t)csp)));
        im_fixed y = add_fx(ins.top, clamp_i64_fx((int64_t)r * ((int64_t)cell_h + (int64_t)rsp)));
        im_layout_arrange(tree, ch, (im_rect){x, y, add_fx(x, cell_w), add_fx(y, cell_h)}, scale);
    }
}

/* ---------- WrapPanel (pinned controls/dev/WrapPanel/WrapPanel.cpp) ---------- */

static im_size run_wrap_panel(im_tree *tree, im_node *node, im_size avail, im_fixed scale,
                              bool rounding, bool do_arrange) {
    im_thickness ins = panel_inset(node, scale, rounding);
    bool horiz = (node->orientation == IM_ORIENT_HORIZONTAL);
    im_fixed pad_u0 = horiz ? ins.left : ins.top;
    im_fixed pad_v0 = horiz ? ins.top : ins.left;
    im_fixed pad_u1 = horiz ? ins.right : ins.bottom;
    im_fixed pad_v1 = horiz ? ins.bottom : ins.right;

    if (node->child_count == 0u) {
        return (im_size){ add_fx(ins.left, ins.right), add_fx(ins.top, ins.bottom) };
    }

    im_fixed parent_u = horiz ? avail.width : avail.height;
    im_fixed sp_u = max_fx(0, node->item_spacing);
    im_fixed sp_v = max_fx(0, node->line_spacing);

    im_fixed pos_u = pad_u0;
    im_fixed pos_v = pad_v0;
    im_fixed row_v = 0;
    uint32_t row_start = 0u;
    uint32_t row_vis = 0u;
    im_fixed final_u = 0;
    im_fixed final_v = 0;
    bool any_vis = false;

    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            if (do_arrange) {
                ch->layout_rect = (im_rect){0, 0, 0, 0};
                ch->absolute_rect = (im_rect){0, 0, 0, 0};
                ch->arrange_dirty = false;
                ch->has_arranged = true;
            }
            continue;
        }
        any_vis = true;
        im_fixed cu = horiz ? ch->desired_size.width : ch->desired_size.height;
        im_fixed cv = horiz ? ch->desired_size.height : ch->desired_size.width;

        /* Pinned WrapPanel.cpp:106: wrap when desired.U + position.U + padEnd.U > parent.U. */
        if (parent_u != IM_FIXED_INF && row_vis > 0u &&
            add_fx(add_fx(pos_u, cu), pad_u1) > parent_u) {
            if (do_arrange) {
                im_fixed au = pad_u0;
                for (uint32_t k = row_start; k < i; k++) {
                    im_node *rc = node->children[k];
                    if (rc->visibility == IM_VIS_COLLAPSED) continue;
                    im_fixed rcu = horiz ? rc->desired_size.width : rc->desired_size.height;
                    im_rect rr = horiz
                        ? (im_rect){ au, pos_v, add_fx(au, rcu), add_fx(pos_v, row_v) }
                        : (im_rect){ pos_v, au, add_fx(pos_v, row_v), add_fx(au, rcu) };
                    im_layout_arrange(tree, rc, rr, scale);
                    au = add_fx(add_fx(au, rcu), sp_u);
                }
            }
            pos_u = pad_u0;
            pos_v = add_fx(add_fx(pos_v, row_v), sp_v);
            row_v = 0;
            row_start = i;
            row_vis = 0u;
        }

        /* Pinned WrapPanel.cpp:144: only the final collection entry stretches when ItemsStretch == Last. */
        bool is_last = (i + 1u == node->child_count) && (node->wrap_stretch == IM_WRAP_STRETCH_LAST);
        if (is_last && parent_u != IM_FIXED_INF) {
            cu = sub_nonneg_fx(sub_nonneg_fx(parent_u, pos_u), pad_u1);
        }

        if (cv > row_v) row_v = cv;
        im_fixed item_end_u = add_fx(pos_u, cu);
        if (item_end_u > final_u) final_u = item_end_u;
        pos_u = add_fx(item_end_u, sp_u);
        row_vis++;
        final_v = add_fx(pos_v, row_v);
    }

    if (do_arrange && row_vis > 0u) {
        im_fixed au = pad_u0;
        for (uint32_t k = row_start; k < node->child_count; k++) {
            im_node *rc = node->children[k];
            if (rc->visibility == IM_VIS_COLLAPSED) continue;
            im_fixed rcu = horiz ? rc->desired_size.width : rc->desired_size.height;
            bool is_last = (k + 1u == node->child_count) && (node->wrap_stretch == IM_WRAP_STRETCH_LAST);
            if (is_last && parent_u != IM_FIXED_INF) {
                rcu = sub_nonneg_fx(sub_nonneg_fx(parent_u, au), pad_u1);
            }
            im_rect rr = horiz
                ? (im_rect){ au, pos_v, add_fx(au, rcu), add_fx(pos_v, row_v) }
                : (im_rect){ pos_v, au, add_fx(pos_v, row_v), add_fx(au, rcu) };
            im_layout_arrange(tree, rc, rr, scale);
            au = add_fx(add_fx(au, rcu), sp_u);
        }
    }

    if (!any_vis) {
        return (im_size){ add_fx(ins.left, ins.right), add_fx(ins.top, ins.bottom) };
    }
    im_fixed total_u = add_fx(final_u, pad_u1);
    im_fixed total_v = add_fx(final_v, pad_v1);
    return horiz
        ? (im_size){ round_if_fx(total_u, scale, rounding), round_if_fx(total_v, scale, rounding) }
        : (im_size){ round_if_fx(total_v, scale, rounding), round_if_fx(total_u, scale, rounding) };
}

static im_size measure_wrap_panel(im_tree *tree, im_node *node, im_size avail, im_fixed scale, bool rounding) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_size child_avail = {
        sub_nonneg_fx(avail.width, add_fx(ins.left, ins.right)),
        sub_nonneg_fx(avail.height, add_fx(ins.top, ins.bottom))
    };
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            (void)im_layout_measure(tree, ch, (im_size){0, 0}, scale);
        } else {
            (void)im_layout_measure(tree, ch, child_avail, scale);
        }
    }
    return run_wrap_panel(tree, node, avail, scale, rounding, false);
}

static void arrange_wrap_panel(im_tree *tree, im_node *node, im_size final_size, im_fixed scale, bool rounding) {
    (void)run_wrap_panel(tree, node, final_size, scale, rounding, true);
}

/* ---------- RelativePanel (pinned dxaml/xcp/core/core/elements/RelativePanel.cpp) ---------- */

typedef struct rel_node_state {
    int32_t deps[IM_REL_COUNT]; /* sibling index or -1 */
    uint8_t color;              /* 0=unvisited, 1=visiting, 2=done */
    im_fixed left, top, right, bottom;
} rel_node_state;

static int32_t find_sibling_by_name(const im_node *panel, const char *name) {
    if (!panel || !name) return -1;
    for (uint32_t i = 0; i < panel->child_count; i++) {
        if (panel->children[i]->name && pstr_eq(panel->children[i]->name, name)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool rel_topo_dfs(im_tree *tree, const im_node *panel, rel_node_state *st,
                         uint32_t idx, uint32_t *order, uint32_t *order_n) {
    st[idx].color = 1u;
    for (int s = 0; s < IM_REL_COUNT; s++) {
        int32_t dep = st[idx].deps[s];
        if (dep < 0) continue;
        if (st[dep].color == 1u) {
            const char *nm = panel->children[idx]->name ? panel->children[idx]->name : "<unnamed>";
            record_layout_error(tree, IM_LAYOUT_ERR_CYCLE, nm);
            return false;
        }
        if (st[dep].color == 0u) {
            if (!rel_topo_dfs(tree, panel, st, (uint32_t)dep, order, order_n)) return false;
        }
    }
    st[idx].color = 2u;
    order[(*order_n)++] = idx;
    return true;
}

static bool resolve_relative_panel(im_tree *tree, im_node *node, im_size inner_avail,
                                   im_fixed scale, bool do_measure, bool do_arrange,
                                   im_thickness ins, im_size *out_inner_desired) {
    uint32_t n = node->child_count;
    if (n == 0u) {
        if (out_inner_desired) *out_inner_desired = (im_size){0, 0};
        return true;
    }
    if ((size_t)n > (SIZE_MAX / sizeof(rel_node_state)) || (size_t)n > (SIZE_MAX / sizeof(uint32_t))) {
        record_layout_error(tree, IM_LAYOUT_ERR_OOM, node->name ? node->name : "RelativePanel");
        return false;
    }
    size_t st_bytes = (size_t)n * sizeof(rel_node_state);
    size_t ord_bytes = (size_t)n * sizeof(uint32_t);
    rel_node_state *st = (rel_node_state *)tree->alloc.alloc(tree->alloc.ctx, st_bytes);
    uint32_t *order = (uint32_t *)tree->alloc.alloc(tree->alloc.ctx, ord_bytes);
    if (!st || !order) {
        if (st) tree->alloc.free(tree->alloc.ctx, st, st_bytes);
        if (order) tree->alloc.free(tree->alloc.ctx, order, ord_bytes);
        record_layout_error(tree, IM_LAYOUT_ERR_OOM, node->name ? node->name : "RelativePanel");
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0; i < n && ok; i++) {
        st[i].color = 0u;
        st[i].left = st[i].top = st[i].right = st[i].bottom = 0;
        im_node *ch = node->children[i];
        for (int s = 0; s < IM_REL_COUNT; s++) {
            const char *tname = ch->rel_target[s];
            if (!tname) {
                st[i].deps[s] = -1;
            } else {
                int32_t idx = find_sibling_by_name(node, tname);
                if (idx < 0) {
                    record_layout_error(tree, IM_LAYOUT_ERR_MISSING_TARGET, tname);
                    ok = false;
                    break;
                }
                if ((uint32_t)idx == i) {
                    record_layout_error(tree, IM_LAYOUT_ERR_CYCLE, ch->name ? ch->name : tname);
                    ok = false;
                    break;
                }
                st[i].deps[s] = idx;
            }
        }
    }

    uint32_t order_n = 0u;
    for (uint32_t i = 0; i < n && ok; i++) {
        if (st[i].color == 0u) {
            if (!rel_topo_dfs(tree, node, st, i, order, &order_n)) {
                ok = false;
            }
        }
    }

    im_fixed max_r = 0, max_b = 0;
    im_fixed pw = inner_avail.width;
    im_fixed ph = inner_avail.height;

    for (uint32_t oi = 0; oi < order_n && ok; oi++) {
        uint32_t i = order[oi];
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            if (do_measure) (void)im_layout_measure(tree, ch, (im_size){0, 0}, scale);
            if (do_arrange) {
                ch->layout_rect = (im_rect){0, 0, 0, 0};
                ch->absolute_rect = (im_rect){0, 0, 0, 0};
                ch->arrange_dirty = false;
                ch->has_arranged = true;
            }
            continue;
        }
        uint32_t pf = ch->rel_panel_flags;
        bool has_l = false, has_r = false, has_t = false, has_b = false;
        im_fixed lx = 0, rx = pw, ty = 0, by = ph;

        if (pf & IM_REL_PANEL_LEFT) { lx = 0; has_l = true; }
        else if (st[i].deps[IM_REL_ALIGN_LEFT] >= 0) { lx = st[st[i].deps[IM_REL_ALIGN_LEFT]].left; has_l = true; }
        else if (st[i].deps[IM_REL_RIGHT_OF] >= 0) { lx = st[st[i].deps[IM_REL_RIGHT_OF]].right; has_l = true; }

        if (pf & IM_REL_PANEL_RIGHT) { rx = pw; has_r = (pw != IM_FIXED_INF); }
        else if (st[i].deps[IM_REL_ALIGN_RIGHT] >= 0) { rx = st[st[i].deps[IM_REL_ALIGN_RIGHT]].right; has_r = true; }
        else if (st[i].deps[IM_REL_LEFT_OF] >= 0) { rx = st[st[i].deps[IM_REL_LEFT_OF]].left; has_r = true; }

        if (pf & IM_REL_PANEL_TOP) { ty = 0; has_t = true; }
        else if (st[i].deps[IM_REL_ALIGN_TOP] >= 0) { ty = st[st[i].deps[IM_REL_ALIGN_TOP]].top; has_t = true; }
        else if (st[i].deps[IM_REL_BELOW] >= 0) { ty = st[st[i].deps[IM_REL_BELOW]].bottom; has_t = true; }

        if (pf & IM_REL_PANEL_BOTTOM) { by = ph; has_b = (ph != IM_FIXED_INF); }
        else if (st[i].deps[IM_REL_ALIGN_BOTTOM] >= 0) { by = st[st[i].deps[IM_REL_ALIGN_BOTTOM]].bottom; has_b = true; }
        else if (st[i].deps[IM_REL_ABOVE] >= 0) { by = st[st[i].deps[IM_REL_ABOVE]].top; has_b = true; }

        im_fixed c_avail_w = (has_l && has_r) ? sub_nonneg_fx(rx, lx) : (has_r ? rx : (pw == IM_FIXED_INF ? IM_FIXED_INF : sub_nonneg_fx(pw, lx)));
        im_fixed c_avail_h = (has_t && has_b) ? sub_nonneg_fx(by, ty) : (has_b ? by : (ph == IM_FIXED_INF ? IM_FIXED_INF : sub_nonneg_fx(ph, ty)));

        im_size cd = do_measure ? im_layout_measure(tree, ch, (im_size){c_avail_w, c_avail_h}, scale) : ch->desired_size;
        im_fixed w = (has_l && has_r) ? sub_nonneg_fx(rx, lx) : cd.width;
        im_fixed h = (has_t && has_b) ? sub_nonneg_fx(by, ty) : cd.height;

        im_fixed fx0 = 0, fy0 = 0;
        if (has_l && has_r) {
            fx0 = lx;
        } else if (has_l) {
            fx0 = lx;
        } else if (has_r) {
            fx0 = sub_nonneg_fx(rx, w);
        } else if (st[i].deps[IM_REL_ALIGN_HCENTER] >= 0) {
            const rel_node_state *ts = &st[st[i].deps[IM_REL_ALIGN_HCENTER]];
            im_fixed mid = (im_fixed)(((int64_t)ts->left + (int64_t)ts->right) / 2);
            fx0 = clamp_i64_fx((int64_t)mid - (int64_t)w / 2);
        } else if ((pf & IM_REL_PANEL_HCENTER) && pw != IM_FIXED_INF) {
            fx0 = clamp_i64_fx(((int64_t)pw - (int64_t)w) / 2);
        }

        if (has_t && has_b) {
            fy0 = ty;
        } else if (has_t) {
            fy0 = ty;
        } else if (has_b) {
            fy0 = sub_nonneg_fx(by, h);
        } else if (st[i].deps[IM_REL_ALIGN_VCENTER] >= 0) {
            const rel_node_state *ts = &st[st[i].deps[IM_REL_ALIGN_VCENTER]];
            im_fixed mid = (im_fixed)(((int64_t)ts->top + (int64_t)ts->bottom) / 2);
            fy0 = clamp_i64_fx((int64_t)mid - (int64_t)h / 2);
        } else if ((pf & IM_REL_PANEL_VCENTER) && ph != IM_FIXED_INF) {
            fy0 = clamp_i64_fx(((int64_t)ph - (int64_t)h) / 2);
        }

        st[i].left = fx0;
        st[i].top = fy0;
        st[i].right = add_fx(fx0, w);
        st[i].bottom = add_fx(fy0, h);

        if (st[i].right > max_r) max_r = st[i].right;
        if (st[i].bottom > max_b) max_b = st[i].bottom;

        if (do_arrange) {
            im_rect ar = {
                add_fx(ins.left, st[i].left),
                add_fx(ins.top, st[i].top),
                add_fx(ins.left, st[i].right),
                add_fx(ins.top, st[i].bottom),
            };
            im_layout_arrange(tree, ch, ar, scale);
        }
    }

    tree->alloc.free(tree->alloc.ctx, order, ord_bytes);
    tree->alloc.free(tree->alloc.ctx, st, st_bytes);
    if (ok && out_inner_desired) {
        *out_inner_desired = (im_size){ max_r, max_b };
    }
    return ok;
}

static im_size measure_relative_panel(im_tree *tree, im_node *node, im_size avail,
                                      im_fixed scale, bool rounding, bool *ok) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_fixed ins_w = add_fx(ins.left, ins.right);
    im_fixed ins_h = add_fx(ins.top, ins.bottom);
    im_size inner_avail = { sub_nonneg_fx(avail.width, ins_w), sub_nonneg_fx(avail.height, ins_h) };
    im_size inner_d = {0, 0};
    if (!resolve_relative_panel(tree, node, inner_avail, scale, true, false, ins, &inner_d)) {
        if (ok) *ok = false;
        return (im_size){0, 0};
    }
    return (im_size){
        round_if_fx(add_fx(inner_d.width, ins_w), scale, rounding),
        round_if_fx(add_fx(inner_d.height, ins_h), scale, rounding),
    };
}

static void arrange_relative_panel(im_tree *tree, im_node *node, im_size final_size,
                                   im_fixed scale, bool rounding, bool *ok) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_fixed ins_w = add_fx(ins.left, ins.right);
    im_fixed ins_h = add_fx(ins.top, ins.bottom);
    im_size inner = { sub_nonneg_fx(final_size.width, ins_w), sub_nonneg_fx(final_size.height, ins_h) };
    if (!resolve_relative_panel(tree, node, inner, scale, false, true, ins, NULL)) {
        if (ok) *ok = false;
    }
}

/* ---------- VariableSizedWrapGrid (pinned VariableSizedWrapGrid_Partial.cpp + OccupancyMap) ---------- */

typedef struct vswg_place {
    uint32_t line;
    uint32_t item;
    uint32_t line_span;
    uint32_t item_span;
} vswg_place;

static bool run_vswg(im_tree *tree, im_node *node, im_size avail, im_fixed scale,
                     bool rounding, bool do_measure, bool do_arrange, im_size *out_size) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_fixed ins_w = add_fx(ins.left, ins.right);
    im_fixed ins_h = add_fx(ins.top, ins.bottom);
    im_size inner = { sub_nonneg_fx(avail.width, ins_w), sub_nonneg_fx(avail.height, ins_h) };

    uint32_t n = node->child_count;
    im_node *first_vis = NULL;
    uint32_t vis_n = 0u;
    for (uint32_t i = 0; i < n; i++) {
        if (node->children[i]->visibility == IM_VIS_COLLAPSED) {
            if (do_measure) (void)im_layout_measure(tree, node->children[i], (im_size){0, 0}, scale);
            if (do_arrange) {
                node->children[i]->layout_rect = (im_rect){0, 0, 0, 0};
                node->children[i]->absolute_rect = (im_rect){0, 0, 0, 0};
                node->children[i]->arrange_dirty = false;
                node->children[i]->has_arranged = true;
            }
        } else {
            if (!first_vis) first_vis = node->children[i];
            vis_n++;
        }
    }
    if (vis_n == 0u) {
        if (out_size) *out_size = (im_size){ ins_w, ins_h };
        return true;
    }

    /* Pinned VariableSizedWrapGrid_Partial.cpp:127,135: auto ItemWidth/ItemHeight from first visible child. */
    im_fixed iw = node->item_width;
    im_fixed ih = node->item_height;
    if (iw == IM_FIXED_AUTO || ih == IM_FIXED_AUTO || iw <= 0 || ih <= 0) {
        im_size fd = do_measure
            ? im_layout_measure(tree, first_vis, (im_size){IM_FIXED_INF, IM_FIXED_INF}, scale)
            : first_vis->desired_size;
        int32_t fc = (first_vis->vswg_col_span > 0) ? first_vis->vswg_col_span : 1;
        int32_t fr = (first_vis->vswg_row_span > 0) ? first_vis->vswg_row_span : 1;
        if (iw == IM_FIXED_AUTO || iw <= 0) iw = (fd.width > 0) ? (fd.width / fc) : IM_ONE;
        if (ih == IM_FIXED_AUTO || ih <= 0) ih = (fd.height > 0) ? (fd.height / fr) : IM_ONE;
    }
    if (iw <= 0) iw = IM_ONE;
    if (ih <= 0) ih = IM_ONE;

    bool horiz = (node->orientation == IM_ORIENT_HORIZONTAL);
    im_fixed main_cell = horiz ? iw : ih;
    im_fixed main_avail = horiz ? inner.width : inner.height;

    uint32_t items_per_line = 0u;
    if (main_avail != IM_FIXED_INF) {
        int64_t fit = (int64_t)main_avail / (int64_t)main_cell;
        items_per_line = (fit > 1) ? (uint32_t)fit : 1u;
    } else {
        items_per_line = 0u; /* unbounded unless max_rows_or_cols > 0 */
    }
    if (node->max_rows_or_cols > 0) {
        uint32_t cap = (uint32_t)node->max_rows_or_cols;
        if (items_per_line == 0u || cap < items_per_line) items_per_line = cap;
    }

    /* Compute total span budget to size the occupancy map safely with checked arithmetic. */
    uint64_t sum_item_spans = 0u;
    uint64_t sum_line_spans = 0u;
    uint32_t max_item_span = 1u;
    for (uint32_t i = 0; i < n; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t cs = (ch->vswg_col_span > 0) ? (uint32_t)ch->vswg_col_span : 1u;
        uint32_t rs = (ch->vswg_row_span > 0) ? (uint32_t)ch->vswg_row_span : 1u;
        uint32_t ispan = horiz ? cs : rs;
        uint32_t lspan = horiz ? rs : cs;
        if (items_per_line > 0u && ispan > items_per_line) ispan = items_per_line;
        if (ispan > max_item_span) max_item_span = ispan;
        sum_item_spans += ispan;
        sum_line_spans += lspan;
    }
    if (items_per_line == 0u) {
        items_per_line = (sum_item_spans > UINT32_MAX) ? UINT32_MAX : (uint32_t)sum_item_spans;
        if (items_per_line == 0u) items_per_line = 1u;
    }
    uint64_t max_lines = sum_line_spans + 1u;
    if (max_lines > 65536u || items_per_line > 65536u ||
        (max_lines * (uint64_t)items_per_line) > (size_t)SIZE_MAX ||
        (size_t)n > (SIZE_MAX / sizeof(vswg_place))) {
        record_layout_error(tree, IM_LAYOUT_ERR_OOM, node->name ? node->name : "VariableSizedWrapGrid");
        return false;
    }

    size_t map_bytes = (size_t)(max_lines * (uint64_t)items_per_line);
    size_t pl_bytes = (size_t)n * sizeof(vswg_place);
    uint8_t *occ = (uint8_t *)tree->alloc.alloc(tree->alloc.ctx, map_bytes);
    vswg_place *places = (vswg_place *)tree->alloc.alloc(tree->alloc.ctx, pl_bytes);
    if (!occ || !places) {
        if (occ) tree->alloc.free(tree->alloc.ctx, occ, map_bytes);
        if (places) tree->alloc.free(tree->alloc.ctx, places, pl_bytes);
        record_layout_error(tree, IM_LAYOUT_ERR_OOM, node->name ? node->name : "VariableSizedWrapGrid");
        return false;
    }
    for (size_t k = 0; k < map_bytes; k++) occ[k] = 0u;

    uint32_t cur_line = 0u, cur_item = 0u;
    uint32_t used_lines = 0u, used_items = 0u;

    for (uint32_t i = 0; i < n; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        uint32_t cs = (ch->vswg_col_span > 0) ? (uint32_t)ch->vswg_col_span : 1u;
        uint32_t rs = (ch->vswg_row_span > 0) ? (uint32_t)ch->vswg_row_span : 1u;
        uint32_t ispan = horiz ? cs : rs;
        uint32_t lspan = horiz ? rs : cs;
        if (ispan > items_per_line) ispan = items_per_line;

        /* Find first free (cur_line, cur_item) block of size (lspan, ispan). */
        while (cur_line + lspan <= (uint32_t)max_lines) {
            if (cur_item + ispan > items_per_line) {
                cur_line++;
                cur_item = 0u;
                continue;
            }
            bool free_ok = true;
            for (uint32_t dl = 0; dl < lspan && free_ok; dl++) {
                size_t row_off = (size_t)(cur_line + dl) * (size_t)items_per_line;
                for (uint32_t di = 0; di < ispan; di++) {
                    if (occ[row_off + cur_item + di]) { free_ok = false; break; }
                }
            }
            if (free_ok) break;
            cur_item++;
        }

        for (uint32_t dl = 0; dl < lspan; dl++) {
            size_t row_off = (size_t)(cur_line + dl) * (size_t)items_per_line;
            for (uint32_t di = 0; di < ispan; di++) {
                occ[row_off + cur_item + di] = 1u;
            }
        }
        places[i] = (vswg_place){ cur_line, cur_item, lspan, ispan };
        if (cur_line + lspan > used_lines) used_lines = cur_line + lspan;
        if (cur_item + ispan > used_items) used_items = cur_item + ispan;

        im_fixed cw = clamp_i64_fx((int64_t)(horiz ? ispan : lspan) * (int64_t)iw);
        im_fixed ch_h = clamp_i64_fx((int64_t)(horiz ? lspan : ispan) * (int64_t)ih);
        if (do_measure) {
            (void)im_layout_measure(tree, ch, (im_size){cw, ch_h}, scale);
        }
        if (do_arrange) {
            uint32_t col = horiz ? cur_item : cur_line;
            uint32_t row = horiz ? cur_line : cur_item;
            im_fixed x = add_fx(ins.left, clamp_i64_fx((int64_t)col * (int64_t)iw));
            im_fixed y = add_fx(ins.top, clamp_i64_fx((int64_t)row * (int64_t)ih));
            im_layout_arrange(tree, ch, (im_rect){x, y, add_fx(x, cw), add_fx(y, ch_h)}, scale);
        }
        cur_item += ispan;
    }

    tree->alloc.free(tree->alloc.ctx, places, pl_bytes);
    tree->alloc.free(tree->alloc.ctx, occ, map_bytes);

    if (out_size) {
        uint32_t total_cols = horiz ? used_items : used_lines;
        uint32_t total_rows = horiz ? used_lines : used_items;
        im_fixed w = add_fx(clamp_i64_fx((int64_t)total_cols * (int64_t)iw), ins_w);
        im_fixed h = add_fx(clamp_i64_fx((int64_t)total_rows * (int64_t)ih), ins_h);
        *out_size = (im_size){ round_if_fx(w, scale, rounding), round_if_fx(h, scale, rounding) };
    }
    return true;
}

/* ---------- ItemsWrapGrid ---------- */

static im_size run_items_wrap_grid(im_tree *tree, im_node *node, im_size avail, im_fixed scale,
                                   bool rounding, bool do_measure, bool do_arrange) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_thickness gp = node->group_padding;
    im_fixed pad_l = add_fx(ins.left, gp.left);
    im_fixed pad_t = add_fx(ins.top, gp.top);
    im_fixed pad_r = add_fx(ins.right, gp.right);
    im_fixed pad_b = add_fx(ins.bottom, gp.bottom);
    im_fixed pad_w = add_fx(pad_l, pad_r);
    im_fixed pad_h = add_fx(pad_t, pad_b);

    im_node *first_vis = NULL;
    uint32_t vis_n = 0u;
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (node->children[i]->visibility == IM_VIS_COLLAPSED) {
            if (do_measure) (void)im_layout_measure(tree, node->children[i], (im_size){0, 0}, scale);
            if (do_arrange) {
                node->children[i]->layout_rect = (im_rect){0, 0, 0, 0};
                node->children[i]->absolute_rect = (im_rect){0, 0, 0, 0};
                node->children[i]->arrange_dirty = false;
                node->children[i]->has_arranged = true;
            }
        } else {
            if (!first_vis) first_vis = node->children[i];
            vis_n++;
        }
    }
    if (vis_n == 0u) return (im_size){ pad_w, pad_h };

    im_fixed iw = node->item_width;
    im_fixed ih = node->item_height;
    if (iw == IM_FIXED_AUTO || ih == IM_FIXED_AUTO || iw <= 0 || ih <= 0) {
        im_size fd = do_measure
            ? im_layout_measure(tree, first_vis, (im_size){IM_FIXED_INF, IM_FIXED_INF}, scale)
            : first_vis->desired_size;
        if (iw == IM_FIXED_AUTO || iw <= 0) iw = (fd.width > 0) ? fd.width : IM_ONE;
        if (ih == IM_FIXED_AUTO || ih <= 0) ih = (fd.height > 0) ? fd.height : IM_ONE;
    }

    bool horiz = (node->orientation == IM_ORIENT_HORIZONTAL);
    im_fixed inner_main = horiz ? sub_nonneg_fx(avail.width, pad_w) : sub_nonneg_fx(avail.height, pad_h);
    im_fixed cell_main = horiz ? iw : ih;
    uint32_t per_line = vis_n;
    if (inner_main != IM_FIXED_INF) {
        int64_t fit = (int64_t)inner_main / (int64_t)cell_main;
        per_line = (fit > 1) ? (uint32_t)fit : 1u;
    }
    if (node->max_rows_or_cols > 0 && (uint32_t)node->max_rows_or_cols < per_line) {
        per_line = (uint32_t)node->max_rows_or_cols;
    }
    if (per_line == 0u) per_line = 1u;

    uint32_t idx = 0u;
    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) continue;
        if (do_measure) (void)im_layout_measure(tree, ch, (im_size){iw, ih}, scale);
        if (do_arrange) {
            uint32_t line = idx / per_line;
            uint32_t pos = idx % per_line;
            uint32_t col = horiz ? pos : line;
            uint32_t row = horiz ? line : pos;
            im_fixed x = add_fx(pad_l, clamp_i64_fx((int64_t)col * (int64_t)iw));
            im_fixed y = add_fx(pad_t, clamp_i64_fx((int64_t)row * (int64_t)ih));
            im_layout_arrange(tree, ch, (im_rect){x, y, add_fx(x, iw), add_fx(y, ih)}, scale);
        }
        idx++;
    }

    uint32_t lines = (vis_n + per_line - 1u) / per_line;
    uint32_t used_main = (vis_n < per_line) ? vis_n : per_line;
    uint32_t total_cols = horiz ? used_main : lines;
    uint32_t total_rows = horiz ? lines : used_main;
    im_fixed w = add_fx(clamp_i64_fx((int64_t)total_cols * (int64_t)iw), pad_w);
    im_fixed h = add_fx(clamp_i64_fx((int64_t)total_rows * (int64_t)ih), pad_h);
    return (im_size){ round_if_fx(w, scale, rounding), round_if_fx(h, scale, rounding) };
}

/* ---------- ItemsStackPanel (pinned StackingLayoutStrategyImpl.cpp:81,96: variable containers + group headers) ---------- */

static im_size run_items_stack_panel(im_tree *tree, im_node *node, im_size avail, im_fixed scale,
                                     bool rounding, bool do_measure, bool do_arrange) {
    im_thickness ins = panel_inset(node, scale, rounding);
    im_thickness gp = node->group_padding;
    im_fixed ins_w = add_fx(ins.left, ins.right);
    im_fixed ins_h = add_fx(ins.top, ins.bottom);
    bool horiz = (node->orientation == IM_ORIENT_HORIZONTAL);

    im_fixed inner_w = sub_nonneg_fx(avail.width, ins_w);
    im_fixed inner_h = sub_nonneg_fx(avail.height, ins_h);
    im_fixed sp = max_fx(0, node->spacing);

    im_fixed cursor = horiz ? ins.left : ins.top;
    im_fixed max_cross = 0;
    bool in_group = false;
    uint32_t vis_in_panel = 0u;

    for (uint32_t i = 0; i < node->child_count; i++) {
        im_node *ch = node->children[i];
        if (ch->visibility == IM_VIS_COLLAPSED) {
            if (do_measure) (void)im_layout_measure(tree, ch, (im_size){0, 0}, scale);
            if (do_arrange) {
                ch->layout_rect = (im_rect){0, 0, 0, 0};
                ch->absolute_rect = (im_rect){0, 0, 0, 0};
                ch->arrange_dirty = false;
                ch->has_arranged = true;
            }
            continue;
        }
        if (ch->is_group_header) {
            if (in_group) {
                cursor = add_fx(cursor, horiz ? gp.right : gp.bottom);
                in_group = false;
            }
            if (vis_in_panel > 0u) cursor = add_fx(cursor, sp);
            im_size cav = horiz ? (im_size){IM_FIXED_INF, inner_h} : (im_size){inner_w, IM_FIXED_INF};
            im_size d = do_measure ? im_layout_measure(tree, ch, cav, scale) : ch->desired_size;
            im_fixed c_main = horiz ? d.width : d.height;
            im_fixed c_cross = horiz ? d.height : d.width;
            if (c_cross > max_cross) max_cross = c_cross;
            if (do_arrange) {
                im_fixed cross_span = max_fx(horiz ? inner_h : inner_w, c_cross);
                im_rect ar = horiz
                    ? (im_rect){ cursor, ins.top, add_fx(cursor, c_main), add_fx(ins.top, cross_span) }
                    : (im_rect){ ins.left, cursor, add_fx(ins.left, cross_span), add_fx(cursor, c_main) };
                im_layout_arrange(tree, ch, ar, scale);
            }
            cursor = add_fx(cursor, c_main);
            vis_in_panel++;
        } else {
            if (!in_group) {
                if (vis_in_panel > 0u) cursor = add_fx(cursor, sp);
                cursor = add_fx(cursor, horiz ? gp.left : gp.top);
                in_group = true;
            } else {
                cursor = add_fx(cursor, sp);
            }
            im_fixed g_cross_pad = horiz ? add_fx(gp.top, gp.bottom) : add_fx(gp.left, gp.right);
            im_fixed g_cross_off = horiz ? add_fx(ins.top, gp.top) : add_fx(ins.left, gp.left);
            im_fixed avail_cross = sub_nonneg_fx(horiz ? inner_h : inner_w, g_cross_pad);
            im_size cav = horiz ? (im_size){IM_FIXED_INF, avail_cross} : (im_size){avail_cross, IM_FIXED_INF};
            im_size d = do_measure ? im_layout_measure(tree, ch, cav, scale) : ch->desired_size;
            im_fixed c_main = horiz ? d.width : d.height;
            im_fixed c_cross = add_fx(horiz ? d.height : d.width, g_cross_pad);
            if (c_cross > max_cross) max_cross = c_cross;
            if (do_arrange) {
                im_fixed cross_span = max_fx(avail_cross, horiz ? d.height : d.width);
                im_rect ar = horiz
                    ? (im_rect){ cursor, g_cross_off, add_fx(cursor, c_main), add_fx(g_cross_off, cross_span) }
                    : (im_rect){ g_cross_off, cursor, add_fx(g_cross_off, cross_span), add_fx(cursor, c_main) };
                im_layout_arrange(tree, ch, ar, scale);
            }
            cursor = add_fx(cursor, c_main);
            vis_in_panel++;
        }
    }
    if (in_group) {
        cursor = add_fx(cursor, horiz ? gp.right : gp.bottom);
    }

    im_fixed total_main = add_fx(cursor, horiz ? ins.right : ins.bottom);
    im_fixed total_cross = add_fx(max_cross, horiz ? ins_h : ins_w);
    return horiz
        ? (im_size){ round_if_fx(total_main, scale, rounding), round_if_fx(total_cross, scale, rounding) }
        : (im_size){ round_if_fx(total_cross, scale, rounding), round_if_fx(total_main, scale, rounding) };
}

/* ---------- Public dispatch ---------- */

im_size im_panels_measure_override(im_tree *tree, im_node *node, im_size avail,
                                   im_fixed scale, bool rounding, bool *ok) {
    switch (node->kind) {
    case IM_ELEM_CANVAS:
        return measure_canvas(tree, node, scale);
    case IM_ELEM_VIEWBOX:
        return measure_viewbox(tree, node, avail, scale, rounding);
    case IM_ELEM_UNIFORM_GRID:
        return measure_uniform_grid(tree, node, avail, scale, rounding);
    case IM_ELEM_WRAP_PANEL:
        return measure_wrap_panel(tree, node, avail, scale, rounding);
    case IM_ELEM_RELATIVE_PANEL:
        return measure_relative_panel(tree, node, avail, scale, rounding, ok);
    case IM_ELEM_VARIABLE_SIZED_WRAP_GRID: {
        im_size out = {0, 0};
        if (!run_vswg(tree, node, avail, scale, rounding, true, false, &out)) {
            if (ok) *ok = false;
            return (im_size){0, 0};
        }
        return out;
    }
    case IM_ELEM_ITEMS_WRAP_GRID:
        return run_items_wrap_grid(tree, node, avail, scale, rounding, true, false);
    case IM_ELEM_ITEMS_STACK_PANEL:
        return run_items_stack_panel(tree, node, avail, scale, rounding, true, false);
    default:
        return (im_size){0, 0};
    }
}

void im_panels_arrange_override(im_tree *tree, im_node *node, im_size final_size,
                                im_fixed scale, bool rounding, bool *ok) {
    switch (node->kind) {
    case IM_ELEM_CANVAS:
        arrange_canvas(tree, node, scale);
        break;
    case IM_ELEM_VIEWBOX:
        arrange_viewbox(tree, node, final_size, scale);
        break;
    case IM_ELEM_UNIFORM_GRID:
        arrange_uniform_grid(tree, node, final_size, scale, rounding);
        break;
    case IM_ELEM_WRAP_PANEL:
        arrange_wrap_panel(tree, node, final_size, scale, rounding);
        break;
    case IM_ELEM_RELATIVE_PANEL:
        arrange_relative_panel(tree, node, final_size, scale, rounding, ok);
        break;
    case IM_ELEM_VARIABLE_SIZED_WRAP_GRID:
        if (!run_vswg(tree, node, final_size, scale, rounding, false, true, NULL)) {
            if (ok) *ok = false;
        }
        break;
    case IM_ELEM_ITEMS_WRAP_GRID:
        (void)run_items_wrap_grid(tree, node, final_size, scale, rounding, false, true);
        break;
    case IM_ELEM_ITEMS_STACK_PANEL:
        (void)run_items_stack_panel(tree, node, final_size, scale, rounding, false, true);
        break;
    default:
        break;
    }
}
