/* Exits 0, or with the number of the first check that failed. */
#include <impossible/base.h>
#include "animate.h"
#include "button.h"
#include "layer.h"
#include "layout.h"
#include "states.h"
#include "template.h"

enum { W = 64, H = 64 };
static uint32_t pixels[W * H];
static im_surface surface = {pixels, W, H, W, {IM_DARK, {0}}, IM_ONE, NULL, NULL};

static void clear(void) {
    for (int i = 0; i < W * H; i++) pixels[i] = 0;
}

static im_rect box(int l, int t, int r, int b) {
    return (im_rect){l * IM_ONE, t * IM_ONE, r * IM_ONE, b * IM_ONE};
}

static uint32_t at(int x, int y) {
    return pixels[y * W + x];
}

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static bool has_state_prop_brush(const im_template *tpl, const char *state_name, const char *target_name, const char *target_prop, int32_t brush_id) {
    for (uint16_t g = 0; g < tpl->group_count; g++) {
        const im_template_group *grp = &im_template_groups[tpl->group_start + g];
        for (uint16_t s = 0; s < grp->state_count; s++) {
            const im_template_state *st = &im_template_states[grp->state_start + s];
            if (!str_eq(st->name, state_name)) continue;
            for (uint16_t a = 0; a < st->anim_count; a++) {
                const im_template_anim *an = &im_template_anims[st->anim_start + a];
                if (!str_eq(an->target_name, target_name) || !str_eq(an->target_property, target_prop)) continue;
                for (uint16_t k = 0; k < an->kf_count; k++) {
                    const im_template_keyframe *kf = &im_template_keyframes[an->kf_start + k];
                    if (kf->value.kind == IM_TVAL_BRUSH && kf->value.a == brush_id) return true;
                }
            }
        }
    }
    return false;
}

static bool has_style_setter_brush(const im_template *tpl, const char *prop_name, int32_t brush_id) {
    for (uint16_t i = 0; i < tpl->style_setter_count; i++) {
        const im_template_setter *st = &im_template_setters[tpl->style_setter_start + i];
        if (str_eq(st->property, prop_name) && st->value.kind == IM_TVAL_BRUSH && st->value.a == brush_id) return true;
    }
    return false;
}

static const im_template_prop *find_elem_prop(const im_template *tpl, const char *elem_name, const char *prop_name) {
    for (uint16_t e = 0; e < tpl->elem_count; e++) {
        const im_template_element *el = &im_template_elements[tpl->elem_start + e];
        if (elem_name && !str_eq(el->name, elem_name)) continue;
        for (uint16_t p = 0; p < el->prop_count; p++) {
            const im_template_prop *pr = &im_template_props[el->prop_start + p];
            if (str_eq(pr->name, prop_name)) return pr;
        }
    }
    return NULL;
}

static const im_template_anim *find_transition_anim(const im_template *tpl, const char *from_state, const char *to_state, im_anim_kind kind, const char *target_name) {
    for (uint16_t g = 0; g < tpl->group_count; g++) {
        const im_template_group *grp = &im_template_groups[tpl->group_start + g];
        for (uint16_t t = 0; t < grp->trans_count; t++) {
            const im_template_transition *tr = &im_template_transitions[grp->trans_start + t];
            if (!str_eq(tr->from_state, from_state) || !str_eq(tr->to_state, to_state)) continue;
            for (uint16_t a = 0; a < tr->anim_count; a++) {
                const im_template_anim *an = &im_template_anims[tr->anim_start + a];
                if (an->kind == kind && str_eq(an->target_name, target_name)) return an;
            }
        }
    }
    return NULL;
}

static const im_template_anim *find_state_anim(const im_template *tpl, const char *state_name, im_anim_kind kind, const char *target_name, const char *target_prop) {
    for (uint16_t g = 0; g < tpl->group_count; g++) {
        const im_template_group *grp = &im_template_groups[tpl->group_start + g];
        for (uint16_t s = 0; s < grp->state_count; s++) {
            const im_template_state *st = &im_template_states[grp->state_start + s];
            if (!str_eq(st->name, state_name)) continue;
            for (uint16_t a = 0; a < st->anim_count; a++) {
                const im_template_anim *an = &im_template_anims[st->anim_start + a];
                if (an->kind == kind && str_eq(an->target_name, target_name) && str_eq(an->target_property, target_prop)) return an;
            }
        }
    }
    return NULL;
}

static int test_templates(void) {
    /* 1. Button, AccentButton, and SubtleButton verify exact (target, property, brush) bindings. */
    const im_template *btn = &im_templates[IM_TEMPLATE_BUTTON];
    if (!has_state_prop_brush(btn, "PointerOver", "ContentPresenter", "Background", IM_BRUSH_ButtonBackgroundPointerOver)) return 40;
    if (!has_state_prop_brush(btn, "PointerOver", "ContentPresenter", "BorderBrush", IM_BRUSH_ButtonBorderBrushPointerOver)) return 41;
    if (!has_state_prop_brush(btn, "Pressed", "ContentPresenter", "Background", IM_BRUSH_ButtonBackgroundPressed)) return 42;
    if (!has_state_prop_brush(btn, "Pressed", "ContentPresenter", "BorderBrush", IM_BRUSH_ButtonBorderBrushPressed)) return 43;
    if (has_state_prop_brush(btn, "PointerOver", "ContentPresenter", "BorderBrush", IM_BRUSH_ButtonBackgroundPointerOver)) return 44;

    const im_template *acc = &im_templates[IM_TEMPLATE_BUTTON_ACCENT];
    if (!has_style_setter_brush(acc, "Background", IM_BRUSH_AccentButtonBackground)) return 45;
    if (!has_state_prop_brush(acc, "PointerOver", "ContentPresenter", "Background", IM_BRUSH_AccentButtonBackgroundPointerOver)) return 46;
    if (!has_state_prop_brush(acc, "Pressed", "ContentPresenter", "Background", IM_BRUSH_AccentButtonBackgroundPressed)) return 47;
    if (!has_state_prop_brush(acc, "Disabled", "ContentPresenter", "Background", IM_BRUSH_AccentButtonBackgroundDisabled)) return 73;

    const im_template *sub = &im_templates[IM_TEMPLATE_BUTTON_SUBTLE];
    if (!has_style_setter_brush(sub, "Background", IM_BRUSH_SubtleButtonBackground)) return 48;
    if (!has_state_prop_brush(sub, "PointerOver", "ContentPresenter", "Background", IM_BRUSH_SubtleButtonBackgroundPointerOver)) return 49;
    if (!has_state_prop_brush(sub, "Pressed", "ContentPresenter", "Background", IM_BRUSH_SubtleButtonBackgroundPressed)) return 50;
    if (!has_state_prop_brush(sub, "Disabled", "ContentPresenter", "Background", IM_BRUSH_SubtleButtonBackgroundDisabled)) return 74;

    /* 2. ToggleSwitch: ControlFasterAnimationDuration (83ms) on SwitchKnobBounds Opacity, spline (0,0,0,1), RepositionThemeAnimation offsets, and fixed-point RenderTransformOrigin. */
    const im_template *ts = &im_templates[IM_TEMPLATE_TOGGLE_SWITCH];
    const im_template_anim *ts_on_op = find_transition_anim(ts, "Dragging", "On", IM_ANIM_DOUBLE_KEYFRAMES, "SwitchKnobBounds");
    if (!ts_on_op || ts_on_op->kf_count != 1 || im_template_keyframes[ts_on_op->kf_start].key_time_ms != 83) return 51;
    const im_template_anim *ts_hover_w = find_state_anim(ts, "PointerOver", IM_ANIM_DOUBLE_KEYFRAMES, "SwitchKnobOn", "Width");
    if (!ts_hover_w || ts_hover_w->kf_count != 1) return 52;
    const im_template_keyframe *ts_kf = &im_template_keyframes[ts_hover_w->kf_start];
    if (ts_kf->kind != IM_KF_SPLINE_DOUBLE || ts_kf->key_time_ms != 83 ||
        ts_kf->spline[0] != 0 || ts_kf->spline[1] != 0 || ts_kf->spline[2] != 0 || ts_kf->spline[3] != IM_ONE) return 53;
    const im_template_anim *rep_on = find_transition_anim(ts, "Dragging", "On", IM_ANIM_REPOSITION_THEME, "SwitchKnob");
    const im_template_anim *rep_off = find_transition_anim(ts, "Dragging", "Off", IM_ANIM_REPOSITION_THEME, "SwitchKnob");
    if (!rep_on || !rep_on->has_from || rep_on->from_val.kind != IM_TVAL_BINDING ||
        rep_on->from_val.a != IM_BIND_TEMPLATED_PARENT || !str_eq(rep_on->from_val.text, "TemplateSettings.KnobCurrentToOnOffset")) return 54;
    if (!rep_off || !rep_off->has_from || rep_off->from_val.kind != IM_TVAL_BINDING ||
        rep_off->from_val.a != IM_BIND_TEMPLATED_PARENT || !str_eq(rep_off->from_val.text, "TemplateSettings.KnobCurrentToOffOffset")) return 55;
    const im_template_prop *knob_origin = find_elem_prop(ts, "SwitchKnobOn", "RenderTransformOrigin");
    if (!knob_origin || knob_origin->value.kind != IM_TVAL_POINT ||
        knob_origin->value.a != IM_ONE / 2 || knob_origin->value.b != IM_ONE / 2) return 56;

    /* 3. TextBox placeholder preserves TemplatedParent Binding + exact TargetNullValue, and DeleteButton uses IM_ICON_DISMISS + IM_TVAL_ICON_FONT. */
    const im_template *tb = &im_templates[IM_TEMPLATE_TEXT_BOX];
    const im_template_prop *ph_fg = find_elem_prop(tb, "PlaceholderTextContentPresenter", "Foreground");
    if (!ph_fg || ph_fg->value.kind != IM_TVAL_BINDING ||
        ph_fg->value.a != IM_BIND_TEMPLATED_PARENT || ph_fg->value.b != IM_BIND_ONE_WAY ||
        !str_eq(ph_fg->value.text, "PlaceholderForeground") ||
        !str_eq(ph_fg->value.fallback, "{ThemeResource TextControlPlaceholderForeground}")) return 57;
    const im_template_prop *del_style = find_elem_prop(tb, "DeleteButton", "Style");
    if (!del_style || del_style->value.kind != IM_TVAL_NESTED_TEMPLATE) return 58;
    const im_template *del_tpl = &im_templates[del_style->value.a];
    const im_template_prop *glyph = find_elem_prop(del_tpl, "GlyphElement", "Text");
    if (!glyph || glyph->value.kind != IM_TVAL_ICON || glyph->value.a != IM_ICON_DISMISS) return 59;
    const im_template_prop *glyph_ff = find_elem_prop(del_tpl, "GlyphElement", "FontFamily");
    if (!glyph_ff || glyph_ff->value.kind != IM_TVAL_ICON_FONT || glyph_ff->value.text != NULL) return 60;

    /* 4. ListView owns Border > ScrollViewer > ItemsPresenter, ItemsStackPanel ItemsPanel, and exact Header/Footer bindings. */
    const im_template *lv = &im_templates[IM_TEMPLATE_LIST_VIEW];
    if (lv->elem_count != 3) return 61;
    if (im_template_elements[lv->elem_start + 0].kind != IM_ELEM_BORDER || im_template_elements[lv->elem_start + 0].parent != -1) return 62;
    if (im_template_elements[lv->elem_start + 1].kind != IM_ELEM_SCROLL_VIEWER || im_template_elements[lv->elem_start + 1].parent != 0) return 63;
    if (im_template_elements[lv->elem_start + 2].kind != IM_ELEM_ITEMS_PRESENTER || im_template_elements[lv->elem_start + 2].parent != 1) return 64;
    const im_template_prop *lv_hdr = find_elem_prop(lv, NULL, "Header");
    const im_template_prop *lv_ftr = find_elem_prop(lv, NULL, "Footer");
    if (!lv_hdr || lv_hdr->value.kind != IM_TVAL_TEMPLATE_BINDING || !str_eq(lv_hdr->value.text, "Header")) return 65;
    if (!lv_ftr || lv_ftr->value.kind != IM_TVAL_TEMPLATE_BINDING || !str_eq(lv_ftr->value.text, "Footer")) return 66;
    bool found_panel = false;
    for (uint16_t i = 0; i < lv->style_setter_count; i++) {
        const im_template_setter *st = &im_template_setters[lv->style_setter_start + i];
        if (str_eq(st->property, "ItemsPanel") && st->value.kind == IM_TVAL_NESTED_TEMPLATE) {
            const im_template *pt = &im_templates[st->value.a];
            if (pt->elem_count == 1 && im_template_elements[pt->elem_start].kind == IM_ELEM_ITEMS_STACK_PANEL) found_panel = true;
        }
    }
    if (!found_panel) return 67;

    /* 5. DefaultListViewItemStyle has ListViewItemPresenter, and ListViewItemExpanded has IM_ICON_CHECKMARK + 4 distinct DragOver directions + RenderTransformOrigin. */
    const im_template *lvi_def = &im_templates[IM_TEMPLATE_LIST_VIEW_ITEM];
    if (lvi_def->elem_count != 1 || im_template_elements[lvi_def->elem_start].kind != IM_ELEM_LIST_VIEW_ITEM_PRESENTER) return 68;
    const im_template *lvi = &im_templates[IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED];
    const im_template_prop *chk = find_elem_prop(lvi, "MultiSelectCheck", "Glyph");
    if (!chk || chk->value.kind != IM_TVAL_ICON || chk->value.a != IM_ICON_CHECKMARK) return 69;
    const im_template_anim *d_bot = find_state_anim(lvi, "BottomReorderHint", IM_ANIM_DRAG_OVER_THEME, "ContentBorder", NULL);
    const im_template_anim *d_top = find_state_anim(lvi, "TopReorderHint", IM_ANIM_DRAG_OVER_THEME, "ContentBorder", NULL);
    const im_template_anim *d_rgt = find_state_anim(lvi, "RightReorderHint", IM_ANIM_DRAG_OVER_THEME, "ContentBorder", NULL);
    const im_template_anim *d_lft = find_state_anim(lvi, "LeftReorderHint", IM_ANIM_DRAG_OVER_THEME, "ContentBorder", NULL);
    if (!d_bot || d_bot->direction != IM_DIR_BOTTOM || !d_bot->has_to || d_bot->to_val.kind != IM_TVAL_THEME_VALUE) return 70;
    if (!d_top || d_top->direction != IM_DIR_TOP || !d_rgt || d_rgt->direction != IM_DIR_RIGHT || !d_lft || d_lft->direction != IM_DIR_LEFT) return 71;
    const im_template_prop *lvi_origin = find_elem_prop(lvi, "ContentBorder", "RenderTransformOrigin");
    if (!lvi_origin || lvi_origin->value.kind != IM_TVAL_POINT ||
        lvi_origin->value.a != IM_ONE / 2 || lvi_origin->value.b != IM_ONE / 2) return 72;
    return 0;
}

/* Fault-injecting tracked allocator for transactional tree tests. */
typedef struct tracked_alloc {
    uint8_t pool[65536];
    size_t used;
    uint32_t alloc_calls;
    uint32_t fail_on_call; /* 0 = never fail, N >= 1 = fail on Nth alloc/realloc call */
    uint32_t live_allocs;
    size_t live_bytes;
} tracked_alloc;

static void *t_alloc(void *ctx, size_t size) {
    tracked_alloc *a = (tracked_alloc *)ctx;
    a->alloc_calls++;
    if (a->fail_on_call != 0 && a->alloc_calls == a->fail_on_call) return NULL;
    size_t aligned = (size + 7u) & ~(size_t)7u;
    if (a->used + aligned > sizeof(a->pool)) return NULL;
    void *p = &a->pool[a->used];
    a->used += aligned;
    a->live_allocs++;
    a->live_bytes += size;
    return p;
}

static void *t_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    tracked_alloc *a = (tracked_alloc *)ctx;
    a->alloc_calls++;
    if (a->fail_on_call != 0 && a->alloc_calls == a->fail_on_call) return NULL;
    size_t aligned = (new_size + 7u) & ~(size_t)7u;
    if (a->used + aligned > sizeof(a->pool)) return NULL;
    void *n = &a->pool[a->used];
    a->used += aligned;
    if (ptr && old_size > 0) {
        size_t copy = old_size < new_size ? old_size : new_size;
        uint8_t *d = (uint8_t *)n;
        const uint8_t *s = (const uint8_t *)ptr;
        for (size_t i = 0; i < copy; i++) d[i] = s[i];
    }
    if (!ptr) {
        a->live_allocs++;
    } else {
        a->live_bytes -= old_size;
    }
    a->live_bytes += new_size;
    return n;
}

static void t_free(void *ctx, void *ptr, size_t size) {
    tracked_alloc *a = (tracked_alloc *)ctx;
    if (!ptr) return;
    if (a->live_allocs > 0) a->live_allocs--;
    a->live_bytes -= size;
}

static im_fixed fxi(int v) { return (im_fixed)(v * IM_ONE); }

static int test_tree_and_layout(void) {
    tracked_alloc ta = {0};
    im_allocator alloc = {t_alloc, t_realloc, t_free, &ta};
    im_palette pal = {IM_DARK, {0}};

    /* 1. Baseline tree + layout + observable failure on EVERY injected allocation + unchanged state + retry + zero leaks. */
    im_tree tree;
    if (!im_tree_init(&tree, alloc)) return 70;
    im_node *root = im_tree_create_node(&tree, IM_ELEM_STACK_PANEL, "Root");
    if (!root) return 71;
    tree.root = root;
    im_node_set_stack(root, IM_ORIENT_VERTICAL, fxi(6));
    im_node *a = im_tree_create_node(&tree, IM_ELEM_BORDER, "A");
    im_node *b = im_tree_create_node(&tree, IM_ELEM_BORDER, "B");
    if (!a || !b) return 72;
    im_node_set_size(a, fxi(40), fxi(12));
    im_node_set_size(b, fxi(50), fxi(18));
    if (!im_tree_append_child(&tree, root, a)) return 73;
    if (!im_tree_append_child(&tree, root, b)) return 74;
    im_layout_run(&tree, root, fxi(100), fxi(100), IM_ONE);
    im_rect base_a = a->absolute_rect;
    im_rect base_b = b->absolute_rect;
    uint32_t base_nodes = tree.node_count;

    /* Inject failure on create_node: must return NULL and leave tree unchanged. */
    ta.fail_on_call = ta.alloc_calls + 1u;
    if (im_tree_create_node(&tree, IM_ELEM_BORDER, "FailNode") != NULL) return 75;
    if (tree.node_count != base_nodes || root->child_count != 2u) return 76;
    if (a->absolute_rect.left != base_a.left || b->absolute_rect.top != base_b.top) return 77;
    ta.fail_on_call = 0;

    /* Fill child_cap (4) then inject failure on the 5th append realloc: parent and child stay untouched. */
    im_node *c = im_tree_create_node(&tree, IM_ELEM_BORDER, "C");
    im_node *d = im_tree_create_node(&tree, IM_ELEM_BORDER, "D");
    im_node *e = im_tree_create_node(&tree, IM_ELEM_BORDER, "E");
    if (!c || !d || !e) return 78;
    if (!im_tree_append_child(&tree, root, c)) return 79;
    if (!im_tree_append_child(&tree, root, d)) return 80;
    ta.fail_on_call = ta.alloc_calls + 1u;
    if (im_tree_append_child(&tree, root, e)) return 81;
    if (root->child_count != 4u || e->parent != NULL) return 82;
    /* Retry succeeds once fault injection is cleared. */
    ta.fail_on_call = 0;
    if (!im_tree_append_child(&tree, root, e)) return 83;
    if (root->child_count != 5u || e->parent != root) return 84;

    /* Inject failure on set_grid_rows: existing rows stay intact. */
    im_node *g = im_tree_create_node(&tree, IM_ELEM_GRID, "G");
    if (!g || !im_tree_append_child(&tree, root, g)) return 85;
    im_grid_def r1[2] = {{IM_GRID_PIXEL, fxi(10), 0, IM_FIXED_INF}, {IM_GRID_STAR, fxi(1), 0, IM_FIXED_INF}};
    im_grid_def r2[3] = {{IM_GRID_AUTO, 0, 0, IM_FIXED_INF}, {IM_GRID_AUTO, 0, 0, IM_FIXED_INF}, {IM_GRID_AUTO, 0, 0, IM_FIXED_INF}};
    if (!im_tree_set_grid_rows(&tree, g, r1, 2u)) return 86;
    ta.fail_on_call = ta.alloc_calls + 1u;
    if (im_tree_set_grid_rows(&tree, g, r2, 3u)) return 87;
    if (g->row_count != 2u || g->rows[0].unit != IM_GRID_PIXEL || g->rows[0].value != fxi(10)) return 88;
    ta.fail_on_call = 0;

    /* Inject failure at EVERY allocation step of im_tree_instantiate_template(IM_TEMPLATE_BUTTON): must rollback cleanly. */
    uint32_t nodes_before_tpl = tree.node_count;
    uint32_t live_before_tpl = ta.live_allocs;
    size_t bytes_before_tpl = ta.live_bytes;
    uint32_t max_steps = 0;
    for (uint32_t step = 1u; step <= 16u; step++) {
        ta.fail_on_call = ta.alloc_calls + step;
        im_node *btn_try = im_tree_instantiate_template(&tree, &pal, IM_TEMPLATE_BUTTON, "BtnFail");
        ta.fail_on_call = 0;
        if (!btn_try) {
            max_steps = step;
            if (tree.node_count != nodes_before_tpl || ta.live_allocs != live_before_tpl || ta.live_bytes != bytes_before_tpl) return 89;
        } else {
            im_tree_destroy_subtree(&tree, btn_try);
            if (tree.node_count != nodes_before_tpl || ta.live_allocs != live_before_tpl || ta.live_bytes != bytes_before_tpl) return 90;
            break;
        }
    }
    if (max_steps == 0) return 91;

    im_tree_destroy(&tree);
    if (tree.node_count != 0 || ta.live_allocs != 0 || ta.live_bytes != 0) return 92;

    /* 2. Collapse/restore, child replacement, and grandchild mutation remeasurement on the same tree. */
    ta.used = 0;
    if (!im_tree_init(&tree, alloc)) return 93;
    im_node *sp = im_tree_create_node(&tree, IM_ELEM_STACK_PANEL, "Sp");
    im_node_set_stack(sp, IM_ORIENT_VERTICAL, fxi(5));
    im_node *c1 = im_tree_create_node(&tree, IM_ELEM_BORDER, "C1");
    im_node *c2 = im_tree_create_node(&tree, IM_ELEM_BORDER, "C2");
    im_node *c3 = im_tree_create_node(&tree, IM_ELEM_BORDER, "C3");
    im_node *gc = im_tree_create_node(&tree, IM_ELEM_BORDER, "GC");
    im_node_set_size(c1, fxi(30), fxi(10));
    im_node_set_size(c2, fxi(30), fxi(20));
    im_node_set_size(gc, fxi(30), fxi(14));
    im_tree_append_child(&tree, c3, gc);
    im_tree_append_child(&tree, sp, c1);
    im_tree_append_child(&tree, sp, c2);
    im_tree_append_child(&tree, sp, c3);
    tree.root = sp;
    im_layout_run(&tree, sp, fxi(100), fxi(100), IM_ONE);
    /* Visible: C1(0..10) + 5 + C2(15..35) + 5 + C3(40..54). */
    if (c3->absolute_rect.top != fxi(40) || sp->desired_size.height != fxi(54)) return 94;
    /* Collapse C2: C1(0..10) + 5 + C3(15..29). */
    im_node_set_visibility(c2, IM_VIS_COLLAPSED);
    im_layout_run(&tree, sp, fxi(100), fxi(100), IM_ONE);
    if (c3->absolute_rect.top != fxi(15) || sp->desired_size.height != fxi(29)) return 95;
    /* Restore C2: back to 40 and 54. */
    im_node_set_visibility(c2, IM_VIS_VISIBLE);
    im_layout_run(&tree, sp, fxi(100), fxi(100), IM_ONE);
    if (c3->absolute_rect.top != fxi(40) || sp->desired_size.height != fxi(54)) return 96;
    /* Grandchild resize bubbles measure-dirty to sp. */
    im_node_set_size(gc, fxi(30), fxi(24));
    if (!sp->measure_dirty || !c3->measure_dirty) return 97;
    im_layout_run(&tree, sp, fxi(100), fxi(100), IM_ONE);
    if (sp->desired_size.height != fxi(64)) return 98;
    /* Replace C2 with a 10px border: C3 moves from 40 to 30. */
    im_node *c2_new = im_tree_create_node(&tree, IM_ELEM_BORDER, "C2New");
    im_node_set_size(c2_new, fxi(30), fxi(10));
    im_node *removed = im_tree_replace_child(&tree, sp, 1u, c2_new);
    if (removed != c2 || c2->parent != NULL) return 99;
    im_tree_destroy_subtree(&tree, removed);
    im_layout_run(&tree, sp, fxi(100), fxi(100), IM_ONE);
    if (c3->absolute_rect.top != fxi(30) || sp->desired_size.height != fxi(54)) return 100;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 101;

    /* 3. Button template instantiation: im_value 4-component padding, no BrushTransition visual child,
       local Padding override flows through TemplateBinding, and clearing returns to theme default. */
    ta.used = 0;
    if (!im_tree_init(&tree, alloc)) return 102;
    im_node *btn = im_tree_instantiate_template(&tree, &pal, IM_TEMPLATE_BUTTON, "Btn");
    if (!btn) return 103;
    tree.root = btn;
    /* Must have 1 visual child (ContentPresenter), NOT 2 (BrushTransition is non-visual). */
    if (btn->child_count != 1u || btn->children[0]->kind != IM_ELEM_CONTENT_PRESENTER) return 104;
    im_node *cp = btn->children[0];
    const im_fixed *pad_v = im_value(&pal, IM_VALUE_ButtonPadding);
    im_thickness eff_pad = im_node_effective_padding(cp);
    if (eff_pad.left != pad_v[0] || eff_pad.top != pad_v[1] || eff_pad.right != pad_v[2] || eff_pad.bottom != pad_v[3]) return 105;
    im_node *child = im_tree_create_node(&tree, IM_ELEM_BORDER, "Child");
    im_node_set_size(child, fxi(20), fxi(10));
    im_tree_append_child(&tree, cp, child);
    im_node_set_size(btn, fxi(100), fxi(40));
    im_node_set_content_alignment(btn, IM_ALIGN_START, IM_ALIGN_START);
    im_layout_run(&tree, btn, fxi(100), fxi(40), IM_ONE);
    /* BorderThickness(1,1,1,1) + ButtonPadding(11,5,11,6) => child at (12, 6). */
    im_rect def_child = child->absolute_rect;
    if (def_child.left != fxi(1) + pad_v[0] || def_child.top != fxi(1) + pad_v[1]) return 106;
    /* Override Button local Padding asymmetrically -> ContentPresenter picks it up via TemplateBinding. */
    im_node_set_padding(btn, (im_thickness){fxi(5), fxi(3), fxi(13), fxi(7)});
    im_layout_run(&tree, btn, fxi(100), fxi(40), IM_ONE);
    if (child->absolute_rect.left != fxi(6) || child->absolute_rect.top != fxi(4)) return 107;
    /* Clear local Padding -> returns to exact default geometry. */
    im_node_clear_padding(btn);
    im_layout_run(&tree, btn, fxi(100), fxi(40), IM_ONE);
    if (child->absolute_rect.left != def_child.left || child->absolute_rect.top != def_child.top) return 108;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 109;

    /* 4. Compiled StackPanel template instantiation via im_tree_instantiate_custom_template. */
    ta.used = 0;
    if (!im_tree_init(&tree, alloc)) return 110;
    static const im_template_prop sp_props[] = {
        {"Orientation", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Horizontal", NULL}},
        {"Spacing", {IM_TVAL_FIXED, 8 * IM_ONE, 0, 0, 0, NULL, NULL}},
        {"Width", {IM_TVAL_FIXED, 20 * IM_ONE, 0, 0, 0, NULL, NULL}},
        {"Height", {IM_TVAL_FIXED, 10 * IM_ONE, 0, 0, 0, NULL, NULL}},
        {"Width", {IM_TVAL_FIXED, 30 * IM_ONE, 0, 0, 0, NULL, NULL}},
        {"Height", {IM_TVAL_FIXED, 10 * IM_ONE, 0, 0, 0, NULL, NULL}},
    };
    static const im_template_element sp_elems[] = {
        {IM_ELEM_STACK_PANEL, "TplStack", -1, 0, 2},
        {IM_ELEM_BORDER, "TplA", 0, 2, 2},
        {IM_ELEM_BORDER, "TplB", 0, 4, 2},
    };
    static const im_template sp_tpl = {"StackTest", "ContentControl", 0, 0, 0, 3, 0, 0};
    im_node *st_ctrl = im_tree_instantiate_custom_template(&tree, &pal, &sp_tpl, NULL, sp_elems, sp_props, "StackHost");
    if (!st_ctrl) return 111;
    tree.root = st_ctrl;
    im_layout_run(&tree, st_ctrl, fxi(100), fxi(40), IM_ONE);
    im_node *tb = im_tree_find_by_name(st_ctrl, "TplB");
    if (!tb || tb->absolute_rect.left != fxi(28) || (tb->absolute_rect.right - tb->absolute_rect.left) != fxi(30)) return 112;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 113;

    /* 5. im_layout_round exact plateau values at 100%, 125%, 150%, 200%, 300% + INF/AUTO passthrough. */
    if (im_layout_round(IM_FIXED_INF, IM_ONE) != IM_FIXED_INF) return 114;
    if (im_layout_round(IM_FIXED_AUTO, IM_ONE) != IM_FIXED_AUTO) return 115;
    /* 9.5 at 100% -> 10.0; at 125% -> 12/1.25 = 9.6; at 150% -> 14/1.5 = 9.333...; at 200% -> 9.5; at 300% -> 29/3 = 9.667. */
    im_fixed nine_half = 9 * IM_ONE + IM_ONE / 2;
    if (im_layout_round(nine_half, 65536) != 10 * IM_ONE) return 116;
    if (im_layout_round(nine_half, 81920) != (im_fixed)((12LL * 65536LL * 65536LL + 40960) / 81920)) return 117;
    if (im_layout_round(nine_half, 98304) != (im_fixed)((14LL * 65536LL * 65536LL + 49152) / 98304)) return 118;
    if (im_layout_round(nine_half, 131072) != nine_half) return 119;
    if (im_layout_round(nine_half, 196608) != (im_fixed)((29LL * 65536LL * 65536LL + 98304) / 196608)) return 120;
    /* Strict floor(x + 0.5) at 200%: 16383 / 65536 * 2 = 0.49996948... < 0.5 -> must round to 0. */
    if (im_layout_round(16383, 131072) != 0) return 121;

    /* 6. Star track min/max water-filling: [min=60] + [max=10] in 100 DIP -> 90, 10; [min=80] + [max=30] -> 80, 20. */
    ta.used = 0; ta.fail_on_call = 0;
    if (!im_tree_init(&tree, alloc)) return 122;
    im_node *sg = im_tree_create_node(&tree, IM_ELEM_GRID, "StarGrid");
    im_node *sc0 = im_tree_create_node(&tree, IM_ELEM_BORDER, "SC0");
    im_node *sc1 = im_tree_create_node(&tree, IM_ELEM_BORDER, "SC1");
    if (!sg || !sc0 || !sc1) return 122;
    im_node_set_grid_cell(sc0, 0, 0, 1, 1);
    im_node_set_grid_cell(sc1, 0, 1, 1, 1);
    im_tree_append_child(&tree, sg, sc0);
    im_tree_append_child(&tree, sg, sc1);
    im_grid_def star_c1[2] = {
        {IM_GRID_STAR, IM_ONE, fxi(60), IM_FIXED_INF},
        {IM_GRID_STAR, IM_ONE, 0, fxi(10)},
    };
    if (!im_tree_set_grid_cols(&tree, sg, star_c1, 2)) return 122;
    tree.root = sg;
    im_layout_run(&tree, sg, fxi(100), fxi(40), IM_ONE);
    if ((sc0->absolute_rect.right - sc0->absolute_rect.left) != fxi(90) ||
        (sc1->absolute_rect.right - sc1->absolute_rect.left) != fxi(10)) return 123;
    im_grid_def star_c2[2] = {
        {IM_GRID_STAR, IM_ONE, fxi(80), IM_FIXED_INF},
        {IM_GRID_STAR, IM_ONE, 0, fxi(30)},
    };
    if (!im_tree_set_grid_cols(&tree, sg, star_c2, 2)) return 123;
    im_layout_run(&tree, sg, fxi(100), fxi(40), IM_ONE);
    if ((sc0->absolute_rect.right - sc0->absolute_rect.left) != fxi(80) ||
        (sc1->absolute_rect.right - sc1->absolute_rect.left) != fxi(20)) return 124;

    /* 7. Cycle rejection and unsigned grid span overflow clamp. */
    if (im_tree_append_child(&tree, sc0, sg)) return 125;
    im_node *sub = im_tree_create_node(&tree, IM_ELEM_BORDER, "Sub");
    if (!sub || !im_tree_append_child(&tree, sc0, sub)) return 125;
    if (im_tree_replace_child(&tree, sc0, 0, sg) != NULL) return 125;
    im_node_set_grid_cell(sc1, 0, 1, 1, UINT32_MAX);
    im_layout_run(&tree, sg, fxi(100), fxi(40), IM_ONE);
    if ((sc1->absolute_rect.right - sc1->absolute_rect.left) != fxi(20)) return 126;

    /* 8. Parent move propagates descendant absolute_rect even when child's local slot is unchanged. */
    im_layout_arrange(&tree, sg, (im_rect){fxi(10), fxi(5), fxi(110), fxi(45)}, IM_ONE);
    if (sc0->absolute_rect.left != fxi(10) || sc0->absolute_rect.top != fxi(5) ||
        sub->absolute_rect.left != fxi(10) || sub->absolute_rect.top != fxi(5)) return 127;

    /* 9. DPI scale change re-arranges even when logical final_rect is unchanged. */
    im_node_set_margin(sub, (im_thickness){IM_ONE / 2, 0, 0, 0});
    im_layout_run(&tree, sg, fxi(100), fxi(40), IM_ONE);
    im_fixed x_100 = sub->absolute_rect.left;
    im_layout_run(&tree, sg, fxi(100), fxi(40), 98304); /* 150% */
    im_fixed x_150 = sub->absolute_rect.left;
    if (x_100 != IM_ONE || x_150 == x_100) return 128;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 128;

    /* 10. Detaching a template child clears external templated_parent (no dangling read after destroying Button). */
    ta.used = 0;
    if (!im_tree_init(&tree, alloc)) return 129;
    im_node *dbtn = im_tree_instantiate_template(&tree, &pal, IM_TEMPLATE_BUTTON, "DetBtn");
    if (!dbtn || dbtn->child_count != 1) return 129;
    im_node *dcp = dbtn->children[0];
    im_node_set_padding(dbtn, (im_thickness){fxi(20), fxi(4), fxi(20), fxi(4)});
    if (!im_tree_remove_child(&tree, dbtn, dcp)) return 129;
    if (dcp->templated_parent != NULL) return 175;
    im_tree_destroy_subtree(&tree, dbtn);
    im_thickness eff_after = im_node_effective_padding(dcp);
    if (eff_after.left == fxi(20)) return 175;
    im_tree_destroy_subtree(&tree, dcp);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 175;

    /* 11. 17-column Grid transient alloc failure during measure & arrange does not cache failure; retry succeeds. */
    ta.used = 0;
    if (!im_tree_init(&tree, alloc)) return 176;
    im_node *g17 = im_tree_create_node(&tree, IM_ELEM_GRID, "Grid17");
    im_node *c17 = im_tree_create_node(&tree, IM_ELEM_BORDER, "Cell17");
    if (!g17 || !c17) return 176;
    im_grid_def cols17[17];
    for (int i = 0; i < 17; i++) cols17[i] = (im_grid_def){IM_GRID_PIXEL, fxi(2), 0, IM_FIXED_INF};
    if (!im_tree_set_grid_cols(&tree, g17, cols17, 17)) return 176;
    im_node_set_grid_cell(c17, 0, 16, 1, 1);
    im_tree_append_child(&tree, g17, c17);
    tree.root = g17;
    ta.fail_on_call = ta.alloc_calls + 1u;
    im_size fail_m = im_layout_measure(&tree, g17, (im_size){fxi(100), fxi(20)}, IM_ONE);
    ta.fail_on_call = 0;
    if (fail_m.width != 0 || !g17->measure_dirty) return 177;
    /* Retry with identical constraint and NO manual invalidation must produce 17 * 2 = 34! */
    im_size ok_m = im_layout_measure(&tree, g17, (im_size){fxi(100), fxi(20)}, IM_ONE);
    if (ok_m.width != fxi(34)) return 178;
    ta.fail_on_call = ta.alloc_calls + 1u;
    im_layout_arrange(&tree, g17, (im_rect){0, 0, fxi(100), fxi(20)}, IM_ONE);
    ta.fail_on_call = 0;
    if (!g17->arrange_dirty) return 179;
    im_layout_arrange(&tree, g17, (im_rect){0, 0, fxi(100), fxi(20)}, IM_ONE);
    if (c17->absolute_rect.left != fxi(32) || (c17->absolute_rect.right - c17->absolute_rect.left) != fxi(2)) return 180;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 180;

    /* 12. Parent StackPanel must NOT cache a child 17-column Grid's transient allocation failure. */
    ta.used = 0; ta.fail_on_call = 0;
    if (!im_tree_init(&tree, alloc)) return 193;
    im_node *pstack = im_tree_create_node(&tree, IM_ELEM_STACK_PANEL, "PStack");
    im_node *pg17 = im_tree_create_node(&tree, IM_ELEM_GRID, "PGrid17");
    im_node *pc17 = im_tree_create_node(&tree, IM_ELEM_BORDER, "PCell17");
    if (!pstack || !pg17 || !pc17) return 193;
    if (!im_tree_set_grid_cols(&tree, pg17, cols17, 17)) return 193;
    im_node_set_grid_cell(pc17, 0, 16, 1, 1);
    im_tree_append_child(&tree, pg17, pc17);
    im_tree_append_child(&tree, pstack, pg17);
    tree.root = pstack;
    ta.fail_on_call = ta.alloc_calls + 1u;
    im_layout_run(&tree, pstack, fxi(100), fxi(20), IM_ONE);
    ta.fail_on_call = 0;
    if (!pstack->measure_dirty) return 194;
    /* Retry at root StackPanel with identical constraints and NO manual invalidation must succeed! */
    im_layout_run(&tree, pstack, fxi(100), fxi(20), IM_ONE);
    if (pstack->desired_size.width != fxi(34) || pc17->absolute_rect.left != fxi(32)) return 195;
    /* Also verify parent arrange_dirty stays true when child Grid arrange allocation fails. */
    im_tree_invalidate_arrange(pg17);
    ta.fail_on_call = ta.alloc_calls + 1u;
    im_layout_arrange(&tree, pstack, (im_rect){0, 0, fxi(100), fxi(20)}, IM_ONE);
    ta.fail_on_call = 0;
    if (!pstack->arrange_dirty) return 195;
    im_layout_arrange(&tree, pstack, (im_rect){0, 0, fxi(100), fxi(20)}, IM_ONE);
    if (pstack->arrange_dirty || pc17->absolute_rect.left != fxi(32)) return 195;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 195;

    /* 13. Compiled Grid Star weights ("2*", "1*") AND direct "0*", "1*" Star weights. */
    ta.used = 0; ta.fail_on_call = 0;
    if (!im_tree_init(&tree, alloc)) return 196;
    static const im_template_prop gw_props[] = {
        {"Width", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "2*", NULL}},
        {"Width", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "1*", NULL}},
        {"Grid.Column", {IM_TVAL_INT, 0, 0, 0, 0, NULL, NULL}},
        {"Grid.Column", {IM_TVAL_INT, 1, 0, 0, 0, NULL, NULL}},
    };
    static const im_template_element gw_elems[] = {
        {IM_ELEM_GRID, "WGrid", -1, 0, 0},
        {IM_ELEM_COLUMN_DEFINITION, NULL, 0, 0, 1},
        {IM_ELEM_COLUMN_DEFINITION, NULL, 0, 1, 1},
        {IM_ELEM_BORDER, "WC0", 0, 2, 1},
        {IM_ELEM_BORDER, "WC1", 0, 3, 1},
    };
    static const im_template gw_tpl = {"GridStarTest", "ContentControl", 0, 0, 0, 5, 0, 0};
    im_node *gw_host = im_tree_instantiate_custom_template(&tree, &pal, &gw_tpl, NULL, gw_elems, gw_props, "GWHost");
    if (!gw_host) return 196;
    tree.root = gw_host;
    im_layout_run(&tree, gw_host, fxi(90), fxi(20), IM_ONE);
    im_node *wc0 = im_tree_find_by_name(gw_host, "WC0");
    im_node *wc1 = im_tree_find_by_name(gw_host, "WC1");
    if (!wc0 || !wc1 || (wc0->absolute_rect.right - wc0->absolute_rect.left) != fxi(60) ||
        (wc1->absolute_rect.right - wc1->absolute_rect.left) != fxi(30)) return 197;
    /* Direct 0* + 1* in 100 DIP -> 0 and 100. */
    im_node *wgrid = im_tree_find_by_name(gw_host, "WGrid");
    im_grid_def zero_star[2] = {
        {IM_GRID_STAR, 0, 0, IM_FIXED_INF},
        {IM_GRID_STAR, IM_ONE, 0, IM_FIXED_INF},
    };
    if (!im_tree_set_grid_cols(&tree, wgrid, zero_star, 2)) return 198;
    im_layout_run(&tree, gw_host, fxi(100), fxi(20), IM_ONE);
    if ((wc0->absolute_rect.right - wc0->absolute_rect.left) != 0 ||
        (wc1->absolute_rect.right - wc1->absolute_rect.left) != fxi(100)) return 199;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 199;

    /* 14. Strictly unbiased layout rounding at 125% (raw 26214 -> 0) and center alignment at 150% (109226 - 65536 -> 0). */
    ta.used = 0; ta.fail_on_call = 0;
    if (!im_tree_init(&tree, alloc)) return 200;
    im_node *ub = im_tree_create_node(&tree, IM_ELEM_BORDER, "UB");
    im_node *uc = im_tree_create_node(&tree, IM_ELEM_BORDER, "UC");
    if (!ub || !uc) return 200;
    im_tree_append_child(&tree, ub, uc);
    tree.root = ub;
    im_layout_measure(&tree, ub, (im_size){fxi(10), fxi(10)}, 81920);
    im_layout_arrange(&tree, ub, (im_rect){26214, 0, 26214 + fxi(10), fxi(10)}, 81920);
    if (ub->layout_rect.left != 0) return 201;
    im_node_set_alignment(uc, IM_ALIGN_CENTER, IM_ALIGN_START);
    im_node_set_size(uc, 65536, 65536);
    im_node_set_layout_rounding(ub, false);
    im_node_set_layout_rounding(uc, true);
    im_layout_measure(&tree, ub, (im_size){109226, fxi(10)}, 98304);
    im_layout_arrange(&tree, ub, (im_rect){0, 0, 109226, fxi(10)}, 98304);
    if (uc->layout_rect.left != 0) return 202;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 202;

    /* 15. Fractional Grid MinWidth (two 0.25-DIP columns at 125% = 0.625 px total -> 3rd column at 1 px, not 0). */
    ta.used = 0; ta.fail_on_call = 0;
    if (!im_tree_init(&tree, alloc)) return 206;
    im_node *fg = im_tree_create_node(&tree, IM_ELEM_GRID, "FracGrid");
    im_node *fc2 = im_tree_create_node(&tree, IM_ELEM_BORDER, "FracC2");
    if (!fg || !fc2) return 206;
    im_grid_def fcols[3] = {
        {IM_GRID_PIXEL, 0, IM_ONE / 4, IM_FIXED_INF},
        {IM_GRID_PIXEL, 0, IM_ONE / 4, IM_FIXED_INF},
        {IM_GRID_STAR, IM_ONE, 0, IM_FIXED_INF},
    };
    if (!im_tree_set_grid_cols(&tree, fg, fcols, 3)) return 206;
    im_node_set_grid_cell(fc2, 0, 2, 1, 1);
    im_tree_append_child(&tree, fg, fc2);
    tree.root = fg;
    im_layout_run(&tree, fg, fxi(20), fxi(10), 81920); /* 125% */
    if (fc2->absolute_rect.left != im_layout_round(IM_ONE / 2, 81920)) return 207;
    im_tree_destroy(&tree);
    if (ta.live_allocs != 0 || ta.live_bytes != 0) return 207;

    return 0;
}

/* Independent plain reference renderer for depth-40 nested clip+layer tree verification. */
static uint32_t ref_blend_premul(uint32_t dst, uint32_t sa, uint32_t sr, uint32_t sg, uint32_t sb, bool dst_opaque) {
    if (sa == 0) return dst;
    uint32_t inv = 255u - sa;
    uint32_t da = dst_opaque ? 255u : ((dst >> 24) & 255u);
    uint32_t dr = (dst >> 16) & 255u;
    uint32_t dg = (dst >> 8) & 255u;
    uint32_t db = dst & 255u;
    uint32_t oa = dst_opaque ? 0u : (sa + (da * inv + 127u) / 255u);
    uint32_t or_ = sr + (dr * inv + 127u) / 255u;
    uint32_t og = sg + (dg * inv + 127u) / 255u;
    uint32_t ob = sb + (db * inv + 127u) / 255u;
    if (oa > 255u) oa = 255u;
    if (or_ > 255u) or_ = 255u;
    if (og > 255u) og = 255u;
    if (ob > 255u) ob = 255u;
    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

typedef struct ref_op {
    uint8_t kind; /* 0 = clip+rect, 1 = layer+rect */
    im_rect rect;
    im_radii radii;
    uint32_t color;
    uint8_t opacity;
} ref_op;

static uint32_t prng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t ref_buf[76][16 * 16];
static uint32_t ref_deepest[16 * 16];

static void render_ref_recursive(const ref_op *ops, int depth, int max_depth, uint32_t *dst, bool dst_opaque, const im_rect *clips, const im_radii *clip_radii, int clip_count) {
    if (depth >= max_depth) {
        for (int i = 0; i < 16 * 16; i++) ref_deepest[i] = dst[i];
        return;
    }
    const ref_op *op = &ops[depth];
    if (op->kind == 0) {
        /* Push clip, draw rect, recurse, pop clip. */
        im_rect next_clips[76];
        im_radii next_radii[76];
        for (int i = 0; i < clip_count; i++) { next_clips[i] = clips[i]; next_radii[i] = clip_radii[i]; }
        next_clips[clip_count] = op->rect;
        next_radii[clip_count] = op->radii;
        int nc = clip_count + 1;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                uint32_t cov = im_shape_coverage(op->rect, (im_radii){0, 0, 0, 0}, x, y);
                for (int c = 0; c < nc && cov > 0; c++) {
                    uint32_t cc = im_shape_coverage(next_clips[c], next_radii[c], x, y);
                    cov = (uint32_t)(((uint64_t)cov * cc + IM_ONE / 2) >> 16);
                }
                if (cov > 0) {
                    uint32_t sa = (uint32_t)(((uint64_t)(op->color >> 24) * cov + IM_ONE / 2) >> 16);
                    uint32_t sr = (((op->color >> 16) & 255u) * sa + 127u) / 255u;
                    uint32_t sg = (((op->color >> 8) & 255u) * sa + 127u) / 255u;
                    uint32_t sb = ((op->color & 255u) * sa + 127u) / 255u;
                    dst[y * 16 + x] = ref_blend_premul(dst[y * 16 + x], sa, sr, sg, sb, dst_opaque);
                }
            }
        }
        render_ref_recursive(ops, depth + 1, max_depth, dst, dst_opaque, next_clips, next_radii, nc);
    } else {
        /* Push layer with opacity and optional rounded clip, draw rect, recurse, composite onto dst. */
        uint32_t *lb = ref_buf[depth];
        for (int i = 0; i < 16 * 16; i++) lb[i] = 0;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                uint32_t cov = im_shape_coverage(op->rect, (im_radii){0, 0, 0, 0}, x, y);
                if (cov > 0) {
                    uint32_t sa = (uint32_t)(((uint64_t)(op->color >> 24) * cov + IM_ONE / 2) >> 16);
                    uint32_t sr = (((op->color >> 16) & 255u) * sa + 127u) / 255u;
                    uint32_t sg = (((op->color >> 8) & 255u) * sa + 127u) / 255u;
                    uint32_t sb = ((op->color & 255u) * sa + 127u) / 255u;
                    lb[y * 16 + x] = ref_blend_premul(lb[y * 16 + x], sa, sr, sg, sb, false);
                }
            }
        }
        render_ref_recursive(ops, depth + 1, max_depth, lb, false, NULL, NULL, 0);
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                uint32_t sp = lb[y * 16 + x];
                if (sp == 0) continue;
                uint32_t cov = im_shape_coverage(op->rect, op->radii, x, y);
                for (int c = 0; c < clip_count && cov > 0; c++) {
                    uint32_t cc = im_shape_coverage(clips[c], clip_radii[c], x, y);
                    cov = (uint32_t)(((uint64_t)cov * cc + IM_ONE / 2) >> 16);
                }
                if (cov == 0) continue;
                uint32_t w = (uint32_t)(((uint64_t)op->opacity * cov + IM_ONE / 2) >> 16);
                if (w == 0) continue;
                uint32_t sa = (((sp >> 24) & 255u) * w + 127u) / 255u;
                uint32_t sr = (((sp >> 16) & 255u) * w + 127u) / 255u;
                uint32_t sg = (((sp >> 8) & 255u) * w + 127u) / 255u;
                uint32_t sb = ((sp & 255u) * w + 127u) / 255u;
                dst[y * 16 + x] = ref_blend_premul(dst[y * 16 + x], sa, sr, sg, sb, dst_opaque);
            }
        }
    }
}

static uint8_t layer_pool_mem[512 * 1024];

typedef struct layer_alloc_rec {
    size_t used;
    uint32_t alloc_calls;
    uint32_t realloc_calls;
    uint32_t fail_alloc_at;
    uint32_t fail_realloc_at;
    uint32_t live_allocs;
    size_t live_bytes;
} layer_alloc_rec;

static void *la_alloc(void *ctx, size_t size) {
    layer_alloc_rec *a = (layer_alloc_rec *)ctx;
    a->alloc_calls++;
    if (a->fail_alloc_at != 0 && a->alloc_calls == a->fail_alloc_at) return NULL;
    size_t aligned = (size + 7u) & ~(size_t)7u;
    if (a->used + aligned > sizeof(layer_pool_mem)) return NULL;
    void *p = &layer_pool_mem[a->used];
    a->used += aligned;
    a->live_allocs++;
    a->live_bytes += size;
    return p;
}

static void *la_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    layer_alloc_rec *a = (layer_alloc_rec *)ctx;
    a->realloc_calls++;
    if (a->fail_realloc_at != 0 && a->realloc_calls == a->fail_realloc_at) return NULL;
    size_t aligned = (new_size + 7u) & ~(size_t)7u;
    if (a->used + aligned > sizeof(layer_pool_mem)) return NULL;
    void *n = &layer_pool_mem[a->used];
    a->used += aligned;
    if (ptr && old_size > 0) {
        size_t copy = old_size < new_size ? old_size : new_size;
        uint8_t *d = (uint8_t *)n;
        const uint8_t *s = (const uint8_t *)ptr;
        for (size_t i = 0; i < copy; i++) d[i] = s[i];
    }
    if (!ptr) a->live_allocs++;
    else a->live_bytes -= old_size;
    a->live_bytes += new_size;
    return n;
}

static void la_free(void *ctx, void *ptr, size_t size) {
    layer_alloc_rec *a = (layer_alloc_rec *)ctx;
    if (!ptr) return;
    if (a->live_allocs > 0) a->live_allocs--;
    a->live_bytes -= size;
}

static int test_clips_and_layers(void) {
    layer_alloc_rec rec = {0};
    im_allocator alloc = {la_alloc, la_realloc, la_free, &rec};
    im_radii sq = {0, 0, 0, 0};

    /* 1. Rectangular & rounded clips on oversized child, empty intersection, and post-pop sibling. */
    clear();
    for (int i = 0; i < W * H; i++) pixels[i] = 0x00102030;
    im_layer_ctx ctx;
    if (!im_layer_init(&ctx, &surface, alloc, 256 * 1024)) return 130;
    if (!im_layer_push_clip_rounded(&ctx, box(10, 10, 50, 50), (im_radii){12 * IM_ONE, 12 * IM_ONE, 12 * IM_ONE, 12 * IM_ONE})) return 131;
    im_fill_argb(&surface, box(0, 0, 64, 64), sq, 0xFFFF0000);
    /* Outside clip must remain exact background 0x00102030; corner (10,10) must remain background (proves rounded, not rect). */
    if (at(5, 5) != 0x00102030 || at(10, 10) != 0x00102030) return 132;
    /* Interior (30,30) must be solid red 0x00FF0000. */
    if (at(30, 30) != 0x00FF0000) return 133;
    /* Empty intersection clip: push disjoint rect, fill green -> zero pixels change. */
    if (!im_layer_push_clip_rect(&ctx, box(52, 52, 60, 60))) return 134;
    im_fill_argb(&surface, box(0, 0, 64, 64), sq, 0xFF00FF00);
    if (at(55, 55) != 0x00102030 || at(30, 30) != 0x00FF0000) return 135;
    im_layer_pop_clip(&ctx);
    im_layer_pop_clip(&ctx);
    /* Empty fractional clip intersection: [0, 0.5) and [0.5, 1) share zero area -> coverage MUST be 0! */
    if (!im_layer_push_clip_rect(&ctx, (im_rect){0, 0, IM_ONE / 2, IM_ONE})) return 203;
    if (!im_layer_push_clip_rect(&ctx, (im_rect){IM_ONE / 2, 0, IM_ONE, IM_ONE})) return 203;
    if (im_clip_pixel_coverage(&ctx.clips, 0, 0) != 0) return 204;
    im_fill_argb(&surface, box(0, 0, 2, 2), sq, 0xFFFFFFFF);
    if (at(0, 0) != 0x00102030) return 205;
    im_layer_pop_clip(&ctx);
    im_layer_pop_clip(&ctx);
    /* Wide surface boundary (>= 32768 px): 1x1 clip at origin must not overflow signed 16.16. */
    im_clip_stack wide_clips;
    if (!im_clip_init(&wide_clips, alloc, 32768, 32768)) return 208;
    if (!im_clip_push_rect(&wide_clips, box(0, 0, 1, 1))) return 208;
    if (im_clip_pixel_coverage(&wide_clips, 0, 0) != IM_ONE) return 209;
    im_clip_destroy(&wide_clips);
    /* Post-pop sibling draws outside former clip. */
    im_fill_argb(&surface, box(2, 2, 6, 6), sq, 0xFF0000FF);
    if (at(3, 3) != 0x000000FF) return 136;
    im_layer_destroy(&ctx);
    if (rec.live_allocs != 0 || rec.live_bytes != 0) return 137;

    /* 2. Group opacity at 0, 128, and 255 over colored background: untouched, overlapping opaque (no bleed-through), and translucent child. */
    uint8_t ops_to_test[3] = {0u, 128u, 255u};
    for (int oi = 0; oi < 3; oi++) {
        uint8_t grp_op = ops_to_test[oi];
        rec.used = 0;
        for (int i = 0; i < W * H; i++) pixels[i] = 0x00102030;
        if (!im_layer_init(&ctx, &surface, alloc, 256 * 1024)) return 138;
        if (im_layer_push(&ctx, box(8, 8, 56, 56), grp_op, false, sq) != IM_LAYER_OK) return 139;
        /* Bottom opaque red child, top overlapping opaque blue child, separate translucent green child. */
        im_fill_argb(&surface, box(10, 10, 40, 40), sq, 0xFFFF0000);
        im_fill_argb(&surface, box(20, 20, 50, 50), sq, 0xFF0000FF);
        im_fill_argb(&surface, box(42, 10, 52, 18), sq, 0x8000FF00);
        im_layer_pop(&ctx);
        /* Untouched pixel (2,2) must stay 0x00102030. */
        if (at(2, 2) != 0x00102030) return 140;
        /* Overlap pixel (25,25): pure blue at grp_op over 0x102030 (zero red from bottom child!). */
        uint32_t want_overlap = ref_blend_premul(0x00102030, grp_op, 0u, 0u, grp_op, true);
        if (at(25, 25) != want_overlap) return 141;
        /* Translucent green pixel (45,14): alpha = 128 premultiplied -> (128, 0, 128, 0) scaled by grp_op over 0x102030. */
        uint32_t ta = (128u * grp_op + 127u) / 255u;
        uint32_t want_trans = ref_blend_premul(0x00102030, ta, 0u, ta, 0u, true);
        if (at(45, 14) != want_trans) return 142;
        im_layer_destroy(&ctx);
        if (rec.live_allocs != 0 || rec.live_bytes != 0) return 143;
    }

    /* 3. im_draw_button through an offset layer (opacity 180) with a rounded clip crossing BOTH fill and 1px stroke. */
    rec.used = 0;
    for (int i = 0; i < W * H; i++) pixels[i] = 0x00202020;
    surface.palette.theme = IM_DARK;
    if (!im_layer_init(&ctx, &surface, alloc, 256 * 1024)) return 144;
    if (im_layer_push(&ctx, box(12, 16, 52, 48), 180u, true, (im_radii){8 * IM_ONE, 8 * IM_ONE, 8 * IM_ONE, 8 * IM_ONE}) != IM_LAYER_OK) return 145;
    /* Button [10,54) x [18,46) extends outside the [12,52) clip on the left/right AND across the r=8 rounded corner at (13,19)! */
    im_draw_button(&surface, box(10, 18, 54, 46), IM_BUTTON, IM_REST);
    im_layer_pop(&ctx);
    /* (10,32) is inside button fill but outside clip -> must stay 0x00202020;
       (12,18) is inside button AND clip rect, but outside r=8 rounded corner -> must stay 0x00202020;
       (32,32) is inside button fill -> must change;
       (32,18) is on button 1px top stroke inside clip -> must differ from both background and fill! */
    if (at(10, 32) != 0x00202020 || at(12, 18) != 0x00202020 || at(32, 32) == 0x00202020) return 146;
    if (at(32, 18) == 0x00202020 || at(32, 18) == at(32, 32)) return 181;
    im_layer_destroy(&ctx);
    if (rec.live_allocs != 0 || rec.live_bytes != 0) return 147;

    /* 4. Cumulative memory_limit across nested layers with recording allocator + parent pop reuse. */
    rec.used = 0;
    for (int i = 0; i < W * H; i++) pixels[i] = 0x00102030;
    /* Budget = 10x10x4 = 400 bytes exact-fit for one 10x10 layer, NOT two nested 8x8 (256B) layers (400 + 256 > 400). */
    if (!im_layer_init(&ctx, &surface, alloc, 400u)) return 148;
    uint32_t calls_before = rec.alloc_calls;
    /* Single 12x12 (576B > 400B): must refuse BEFORE calling alloc.alloc! */
    if (im_layer_push(&ctx, box(0, 0, 12, 12), 128u, false, sq) != IM_LAYER_FALLBACK) return 149;
    if (rec.alloc_calls != calls_before) return 150;
    im_fill_argb(&surface, box(2, 2, 8, 8), sq, 0xFFFF0000);
    im_layer_pop(&ctx);
    /* Fallback still drew the element at group opacity 128! */
    if (at(4, 4) != ref_blend_premul(0x00102030, 128u, 128u, 0u, 0u, true)) return 151;
    /* Exact-fit 10x10 (400B): succeeds; nested 8x8 (256B): cumulative 656B > 400B refuses without calling alloc! */
    if (im_layer_push(&ctx, box(20, 20, 30, 30), 255u, false, sq) != IM_LAYER_OK) return 152;
    if (ctx.bytes_used != 400u) return 153;
    calls_before = rec.alloc_calls;
    if (im_layer_push(&ctx, box(21, 21, 29, 29), 128u, false, sq) != IM_LAYER_FALLBACK) return 154;
    if (rec.alloc_calls != calls_before) return 155;
    im_fill_argb(&surface, box(22, 22, 28, 28), sq, 0xFF00FF00);
    im_layer_pop(&ctx);
    im_layer_pop(&ctx);
    if (ctx.bytes_used != 0u) return 156;
    /* After popping parent, budget is released and a fresh 10x10 layer succeeds! */
    if (im_layer_push(&ctx, box(30, 30, 40, 40), 255u, false, sq) != IM_LAYER_OK) return 157;
    im_layer_pop(&ctx);
    im_layer_destroy(&ctx);
    if (rec.live_allocs != 0 || rec.live_bytes != 0) return 158;

    /* 4b. Fallback layer with has_clip=false must still enforce its own bounds (and clamp nested child layers). */
    rec.used = 0;
    for (int i = 0; i < W * H; i++) pixels[i] = 0x00000000;
    if (!im_layer_init(&ctx, &surface, alloc, 0u)) return 182;
    if (im_layer_push(&ctx, box(1, 1, 3, 3), 255u, false, sq) != IM_LAYER_FALLBACK) return 182;
    im_fill_argb(&surface, box(0, 0, 64, 64), sq, 0xFFFFFFFF);
    if (at(0, 0) != 0x00000000 || at(1, 1) != 0x00FFFFFF || at(3, 3) != 0x00000000) return 183;
    /* Nested layer inside fallback parent must also be clamped to [1,3)x[1,3). */
    ctx.memory_limit = 256 * 1024;
    if (im_layer_push(&ctx, box(0, 0, 6, 6), 255u, false, sq) != IM_LAYER_OK) return 184;
    if (ctx.frames[ctx.frame_count - 1u].width != 2 || ctx.frames[ctx.frame_count - 1u].height != 2) return 184;
    im_fill_argb(&surface, box(0, 0, 6, 6), sq, 0xFF00FF00);
    im_layer_pop(&ctx);
    im_layer_pop(&ctx);
    if (at(0, 0) != 0x00000000 || at(1, 1) != 0x0000FF00 || at(3, 3) != 0x00000000) return 185;
    im_layer_destroy(&ctx);
    if (rec.live_allocs != 0 || rec.live_bytes != 0) return 185;

    /* 5. Injected allocation failure on clip-stack growth, layer-frame growth (IM_LAYER_ERR), and layer buffer alloc + sibling + retry. */
    rec.used = 0;
    for (int i = 0; i < W * H; i++) pixels[i] = 0x00102030;
    if (!im_layer_init(&ctx, &surface, alloc, 256 * 1024)) return 159;
    if (!im_layer_push_clip_rect(&ctx, box(10, 10, 50, 50))) return 160;
    /* Inject failure on buffer alloc: falls back, honors outer clip (10..50), draws element, leaves sibling clean. */
    rec.fail_alloc_at = rec.alloc_calls + 1u;
    if (im_layer_push(&ctx, box(0, 0, 64, 64), 128u, false, sq) != IM_LAYER_FALLBACK) return 161;
    rec.fail_alloc_at = 0;
    im_fill_argb(&surface, box(0, 0, 64, 64), sq, 0xFFFF0000);
    im_layer_pop(&ctx);
    /* Outer clip still protected (5,5); inside (20,20) drew fallback red at 128. */
    if (at(5, 5) != 0x00102030 || at(20, 20) != ref_blend_premul(0x00102030, 128u, 128u, 0u, 0u, true)) return 162;
    /* Inject failure on clip-stack growth (push 7 more to fill cap=8, fail on 9th): returns false, stack unchanged. */
    for (int k = 0; k < 7; k++) {
        if (!im_layer_push_clip_rect(&ctx, box(10, 10, 50, 50))) return 163;
    }
    rec.fail_realloc_at = rec.realloc_calls + 1u;
    if (im_layer_push_clip_rect(&ctx, box(12, 12, 48, 48))) return 164;
    rec.fail_realloc_at = 0;
    if (ctx.clips.count != 8u) return 165;
    /* Retry clip push succeeds. */
    if (!im_layer_push_clip_rect(&ctx, box(12, 12, 48, 48))) return 166;
    im_fill_argb(&surface, box(14, 14, 18, 18), sq, 0xFF0000FF);
    if (at(15, 15) != 0x000000FF) return 167;
    /* Inject failure on layer-frame stack growth (push 8 layers to fill cap=8, fail on 9th -> IM_LAYER_ERR). */
    for (int k = 0; k < 8; k++) {
        if (im_layer_push(&ctx, box(14, 14, 24, 24), 255u, false, sq) != IM_LAYER_OK) return 186;
    }
    rec.fail_realloc_at = rec.realloc_calls + 1u;
    if (im_layer_push(&ctx, box(14, 14, 24, 24), 255u, false, sq) != IM_LAYER_ERR) return 187;
    rec.fail_realloc_at = 0;
    if (ctx.frame_count != 8u) return 188;
    /* Retry 9th layer push succeeds and unwinds cleanly. */
    if (im_layer_push(&ctx, box(14, 14, 24, 24), 255u, false, sq) != IM_LAYER_OK) return 189;
    im_fill_argb(&surface, box(16, 16, 20, 20), sq, 0xFF00FF00);
    for (int k = 0; k < 9; k++) im_layer_pop(&ctx);
    if (at(17, 17) != 0x0000FF00) return 189;
    im_layer_destroy(&ctx);
    if (rec.live_allocs != 0 || rec.live_bytes != 0) return 168;

    /* 6. Depth-72 (36 nested layers > 32 AND 72 clips > 32) interleaved tree vs plain reference renderer,
          verified BOTH at maximum active depth before unwinding AND after full unwinding. */
    static uint32_t actual_16[16 * 16];
    static uint32_t expected_16[16 * 16];
    for (int i = 0; i < 16 * 16; i++) actual_16[i] = expected_16[i] = 0x00182430;
    im_surface s16 = {actual_16, 16, 16, 16, {IM_DARK, {0}}, IM_ONE, NULL, NULL};
    ref_op ops[72];
    uint32_t seed = 0xCAFEBABEu;
    for (int d = 0; d < 72; d++) {
        ops[d].kind = (uint8_t)(d & 1);
        int x0 = (int)(prng_next(&seed) % 4u);
        int y0 = (int)(prng_next(&seed) % 4u);
        int x1 = 12 + (int)(prng_next(&seed) % 4u);
        int y1 = 12 + (int)(prng_next(&seed) % 4u);
        int rad = (int)(prng_next(&seed) % 3u);
        ops[d].rect = box(x0, y0, x1, y1);
        ops[d].radii = (im_radii){rad * IM_ONE, rad * IM_ONE, rad * IM_ONE, rad * IM_ONE};
        uint32_t a = 96u + (prng_next(&seed) % 160u);
        uint32_t r = prng_next(&seed) & 255u;
        uint32_t g = prng_next(&seed) & 255u;
        uint32_t b = prng_next(&seed) & 255u;
        ops[d].color = (a << 24) | (r << 16) | (g << 8) | b;
        ops[d].opacity = (uint8_t)(160u + (prng_next(&seed) % 96u));
    }
    render_ref_recursive(ops, 0, 72, expected_16, true, NULL, NULL, 0);

    rec.used = 0;
    if (!im_layer_init(&ctx, &s16, alloc, 512 * 1024)) return 169;
    for (int d = 0; d < 72; d++) {
        if (ops[d].kind == 0) {
            if (!im_layer_push_clip_rounded(&ctx, ops[d].rect, ops[d].radii)) return 170;
            im_fill_argb(&s16, ops[d].rect, sq, ops[d].color);
        } else {
            if (im_layer_push(&ctx, ops[d].rect, ops[d].opacity, true, ops[d].radii) != IM_LAYER_OK) return 171;
            im_fill_argb(&s16, ops[d].rect, sq, ops[d].color);
        }
    }
    /* Verify > 32 active layer frames AND check innermost active layer buffer pixels BEFORE unwinding! */
    if (ctx.frame_count != 36u || ctx.clips.count != 72u) return 190;
    const im_layer_frame *deep = &ctx.frames[ctx.frame_count - 1u];
    if (!deep->pixels || deep->width <= 0 || deep->height <= 0) return 191;
    for (int dy = 0; dy < deep->height; dy++) {
        for (int dx = 0; dx < deep->width; dx++) {
            uint32_t got_px = deep->pixels[(size_t)dy * (size_t)deep->width + (size_t)dx];
            uint32_t want_px = ref_deepest[(deep->y + dy) * 16 + (deep->x + dx)];
            if (got_px != want_px) return 192;
        }
    }
    for (int d = 71; d >= 0; d--) {
        if (ops[d].kind == 0) im_layer_pop_clip(&ctx);
        else im_layer_pop(&ctx);
    }
    for (int i = 0; i < 16 * 16; i++) {
        if (actual_16[i] != expected_16[i]) return 172;
    }
    /* After full unwinding, drawing a sibling at (0,0) must work unclipped. */
    im_fill_argb(&s16, box(0, 0, 2, 2), sq, 0xFF123456);
    if (actual_16[0] != 0x00123456) return 173;
    im_layer_destroy(&ctx);
    if (rec.live_allocs != 0 || rec.live_bytes != 0) return 174;

    return 0;
}

static int test_visual_states_and_animation(void) {
    /* 1. Settled 24-frame Button parity: im_states_draw_button == im_draw_button byte-for-byte. */
    const im_template_id btn_tpls[3] = {IM_TEMPLATE_BUTTON, IM_TEMPLATE_BUTTON_ACCENT, IM_TEMPLATE_BUTTON_SUBTLE};
    const im_button_style btn_styles[3] = {IM_BUTTON, IM_BUTTON_ACCENT, IM_BUTTON_SUBTLE};
    const char *btn_state_names[4] = {"Normal", "PointerOver", "Pressed", "Disabled"};
    const im_button_state btn_enums[4] = {IM_REST, IM_HOVER, IM_PRESSED, IM_DISABLED};
    const im_theme themes[2] = {IM_DARK, IM_LIGHT};
    uint32_t ref_pixels[W * H];

    for (int ti = 0; ti < 2; ti++) {
        im_palette pal = {themes[ti], {0}};
        pal.slot[IM_SLOT_ACCENT] = 0xFF0078D4u;
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_1] = 0xFF429CE3u;
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_2] = 0xFF60CDFFu;
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_3] = 0xFF99EBFFu;
        pal.slot[IM_SLOT_ACCENT_COLOR_DARK_1] = 0xFF005A9Eu;
        pal.slot[IM_SLOT_ACCENT_COLOR_DARK_2] = 0xFF004578u;
        pal.slot[IM_SLOT_ACCENT_COLOR_DARK_3] = 0xFF003056u;
        surface.palette = pal;
        surface.scale = IM_ONE;

        for (int si = 0; si < 3; si++) {
            im_state_machine sm;
            im_states_init(&sm, btn_tpls[si], &pal);
            for (int st = 0; st < 4; st++) {
                uint32_t t0 = (uint32_t)(st * 200u);
                im_states_go_to(&sm, btn_state_names[st], t0);
                /* Reference frame from im_draw_button. */
                for (int i = 0; i < W * H; i++) pixels[i] = 0x00202020u;
                im_draw_button(&surface, box(4, 16, 60, 48), btn_styles[si], btn_enums[st]);
                for (int i = 0; i < W * H; i++) ref_pixels[i] = pixels[i];

                /* State-machine frame on settled tick (t0 + 83). */
                for (int i = 0; i < W * H; i++) pixels[i] = 0x00202020u;
                im_states_draw_button(&sm, &surface, box(4, 16, 60, 48), t0 + 83u);
                for (int i = 0; i < W * H; i++) {
                    if (pixels[i] != ref_pixels[i]) return 220;
                }
            }
            /* Also verify Foreground and AnimatedIcon.State setters on Standard/Subtle buttons. */
            if (si == 0 || si == 2) {
                im_states_go_to(&sm, "PointerOver", 1000u);
                im_template_value icon_st, fg_val;
                if (!im_states_sample_value(&sm, "ContentPresenter", "AnimatedIcon.State", 1000u, &icon_st) ||
                    icon_st.kind != IM_TVAL_ENUM_STR || !str_eq(icon_st.text, "PointerOver")) return 221;
                if (!im_states_sample_value(&sm, "ContentPresenter", "Foreground", 1000u, &fg_val) ||
                    fg_val.kind != IM_TVAL_BRUSH) return 222;
                im_states_go_to(&sm, "Pressed", 1100u);
                if (!im_states_sample_value(&sm, "ContentPresenter", "AnimatedIcon.State", 1100u, &icon_st) ||
                    !str_eq(icon_st.text, "Pressed")) return 223;
            }
        }
    }

    /* 2. Custom-template brush mutation proves im_states_draw_button reads compiled tables (not im_draw_button). */
    {
        im_palette pal = {IM_DARK, {0}};
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_2] = 0xFF60CDFFu;
        surface.palette = pal;
        static const im_template_setter custom_setters[] = {
            {NULL, "Background", {IM_TVAL_BRUSH, IM_BRUSH_ButtonBackground, 0, 0, 0, NULL, NULL}},
            {NULL, "BackgroundSizing", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "InnerBorderEdge", NULL}},
            {NULL, "BorderBrush", {IM_TVAL_BRUSH, IM_BRUSH_ButtonBorderBrush, 0, 0, 0, NULL, NULL}},
        };
        static const im_template_prop custom_props[] = {
            {"Background", {IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "Background", NULL}},
            {"BorderBrush", {IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "BorderBrush", NULL}},
        };
        static const im_template_element custom_elems[] = {
            {IM_ELEM_CONTENT_PRESENTER, "ContentPresenter", -1, 0, 2},
        };
        static const im_template_keyframe custom_kfs[] = {
            {IM_KF_DISCRETE, 0, {IM_TVAL_BRUSH, IM_BRUSH_AccentButtonBackground, 0, 0, 0, NULL, NULL}, {0, 0, 0, 0}},
        };
        static const im_template_anim custom_anims[] = {
            {IM_ANIM_OBJECT_KEYFRAMES, "ContentPresenter", "Background", 0, IM_DIR_NONE, false, false, false,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 1},
        };
        static const im_template_state custom_states[] = {
            {"Normal", 0, 0, 0, 0},
            {"PointerOver", 0, 0, 0, 1},
        };
        static const im_template_group custom_groups[] = {
            {"CommonStates", 0, 2, 0, 0},
        };
        static const im_template custom_tpl = {"CustomBtn", "Button", 0, 3, 0, 1, 0, 1};
        im_template_tables tbls = {custom_setters, custom_elems, custom_props, custom_groups, custom_states, NULL, custom_anims, custom_kfs};
        im_state_machine csm;
        im_states_init_custom(&csm, &custom_tpl, &tbls, &pal);
        im_states_go_to(&csm, "PointerOver", 0u);
        for (int i = 0; i < W * H; i++) pixels[i] = 0x00202020u;
        im_states_draw_button(&csm, &surface, box(4, 16, 60, 48), 0u);
        if (at(32, 32) != 0x0060CDFFu) return 224;
    }

    /* 3. Standard Dark Button 83 ms ARGB BrushTransition (alpha-only 0x0FFFFFFF -> 0x15FFFFFF) + composited center pixel,
          plus Accent Button RGB transition, immediate BorderBrush switch, and mid-flight interruption at 40 ms without draw. */
    {
        im_palette pal = {IM_DARK, {0}};
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_2] = 0xFF60CDE0u;
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_1] = 0xFF429CE3u;
        surface.palette = pal;

        im_state_machine sm;
        im_states_init(&sm, IM_TEMPLATE_BUTTON, &pal);
        im_states_go_to(&sm, "Normal", 0u);
        uint32_t c0 = im_states_sample_color(&sm, "ContentPresenter", "Background", 0u, 0u);
        if ((c0 >> 24) != 0x0Fu) return 225;

        im_states_go_to(&sm, "PointerOver", 100u);
        if (!im_states_is_animating(&sm, 121u)) return 226;
        uint32_t c21 = im_states_sample_color(&sm, "ContentPresenter", "Background", 121u, 0u);
        uint32_t c62 = im_states_sample_color(&sm, "ContentPresenter", "Background", 162u, 0u);
        uint32_t c83 = im_states_sample_color(&sm, "ContentPresenter", "Background", 183u, 0u);
        if ((c21 >> 24) != 0x11u || (c62 >> 24) != 0x13u || (c83 >> 24) != 0x15u) return 227;
        if (im_states_is_animating(&sm, 183u)) return 228;

        /* Verify composited center pixel on 0x00202020 changes strictly from 100 -> 121 -> 162 -> 183. */
        uint32_t px_at[4];
        const uint32_t ts[4] = {100u, 121u, 162u, 183u};
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < W * H; i++) pixels[i] = 0x00202020u;
            im_states_draw_button(&sm, &surface, box(4, 16, 60, 48), ts[k]);
            px_at[k] = at(32, 32) & 0xFFu;
        }
        if (!(px_at[0] < px_at[1] && px_at[1] < px_at[2] && px_at[2] < px_at[3])) return 229;

        /* BorderBrush must switch immediately at t = 0 (not animated). */
        enum im_brush_id bb_id = (enum im_brush_id)0;
        bool bb_trans = true;
        if (!im_states_sample_brush(&sm, "ContentPresenter", "BorderBrush", 100u, &bb_id, NULL, &bb_trans)) return 230;
        if (bb_trans || bb_id != IM_BRUSH_ButtonBorderBrushPointerOver) return 231;

        /* Interrupt mid-flight at 140 ms (PointerOver -> Pressed) WITHOUT any intervening draw,
           plus Accent Button RGB channel check at 21 ms and 62 ms using distinct RGB endpoints. */
        im_state_machine asm_;
        im_states_init(&asm_, IM_TEMPLATE_BUTTON_ACCENT, &pal);
        im_states_go_to(&asm_, "Normal", 0u);
        im_states_go_to(&asm_, "Disabled", 100u);
        uint32_t acc_norm = im_brush_over(&pal, IM_BRUSH_AccentButtonBackground, 0, IM_ONE, IM_ONE);
        uint32_t acc_dis = im_brush_over(&pal, IM_BRUSH_AccentButtonBackgroundDisabled, 0, IM_ONE, IM_ONE);
        uint32_t acc_21 = im_states_sample_color(&asm_, "ContentPresenter", "Background", 121u, 0u);
        uint32_t acc_62 = im_states_sample_color(&asm_, "ContentPresenter", "Background", 162u, 0u);
        /* Independent straight-ARGB channel formula (does not call im_lerp_argb_ratio). */
        for (int shift = 0; shift < 32; shift += 8) {
            uint32_t c0 = (acc_norm >> shift) & 255u;
            uint32_t c1 = (acc_dis >> shift) & 255u;
            uint32_t w21 = (c0 * 62u + c1 * 21u + 41u) / 83u;
            uint32_t w62 = (c0 * 21u + c1 * 62u + 41u) / 83u;
            if (((acc_21 >> shift) & 255u) != w21) return 231;
            if (((acc_62 >> shift) & 255u) != w62) return 231;
        }
        /* Ensure R, G, B channels genuinely differ between endpoints and both interior samples. */
        if (((acc_21 >> 16) & 255u) == ((acc_62 >> 16) & 255u) ||
            ((acc_21 >> 8) & 255u) == ((acc_62 >> 8) & 255u) ||
            (acc_21 & 255u) == (acc_62 & 255u)) return 231;

        im_states_go_to(&asm_, "Normal", 300u);
        im_states_go_to(&asm_, "Disabled", 400u);
        uint32_t pre_int = im_states_sample_color(&asm_, "ContentPresenter", "Background", 440u, 0u);
        im_states_go_to(&asm_, "Pressed", 440u);
        uint32_t post_int = im_states_sample_color(&asm_, "ContentPresenter", "Background", 440u, 0u);
        if (pre_int != post_int) return 232;
        uint32_t settled = im_states_sample_color(&asm_, "ContentPresenter", "Background", 523u, 0u);
        uint32_t want_pressed = im_brush_over(&pal, IM_BRUSH_AccentButtonBackgroundPressed, 0, IM_ONE, IM_ONE);
        if (settled != want_pressed) return 233;

        /* Literal IM_TVAL_COLOR Button fill + border + BrushTransition must draw the literal ARGB (not brush 0). */
        static const im_template_setter lit_setters[] = {
            {NULL, "Background", {IM_TVAL_COLOR, (int32_t)0xFF102030u, (int32_t)0xFF102030u, (int32_t)0xFF102030u, -1, NULL, NULL}},
            {NULL, "BackgroundSizing", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "InnerBorderEdge", NULL}},
            {NULL, "BorderBrush", {IM_TVAL_COLOR, (int32_t)0xFFAA5500u, (int32_t)0xFFAA5500u, (int32_t)0xFFAA5500u, -1, NULL, NULL}},
        };
        static const im_template_prop lit_props[] = {
            {"Background", {IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "Background", NULL}},
            {"BorderBrush", {IM_TVAL_TEMPLATE_BINDING, 0, 0, 0, 0, "BorderBrush", NULL}},
            {"Duration", {IM_TVAL_DURATION_MS, 83, 0, 0, 0, NULL, NULL}},
        };
        static const im_template_element lit_elems[] = {
            {IM_ELEM_CONTENT_PRESENTER, "ContentPresenter", -1, 0, 2},
            {IM_ELEM_BRUSH_TRANSITION, NULL, 0, 2, 1},
        };
        static const im_template_keyframe lit_kfs[] = {
            {IM_KF_DISCRETE, 0, {IM_TVAL_COLOR, (int32_t)0xFF506070u, (int32_t)0xFF506070u, (int32_t)0xFF506070u, -1, NULL, NULL}, {0, 0, 0, 0}},
        };
        static const im_template_anim lit_anims[] = {
            {IM_ANIM_OBJECT_KEYFRAMES, "ContentPresenter", "Background", 0, IM_DIR_NONE, false, false, false,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 1},
        };
        static const im_template_state lit_states[] = {
            {"Normal", 0, 0, 0, 0},
            {"PointerOver", 0, 0, 0, 1},
        };
        static const im_template_group lit_groups[] = {{"CommonStates", 0, 2, 0, 0}};
        static const im_template lit_tpl = {"LitBtn", "Button", 0, 3, 0, 2, 0, 1};
        im_template_tables lit_tbls = {lit_setters, lit_elems, lit_props, lit_groups, lit_states, NULL, lit_anims, lit_kfs};
        im_state_machine lsm;
        im_states_init_custom(&lsm, &lit_tpl, &lit_tbls, &pal);
        im_states_go_to(&lsm, "Normal", 0u);
        for (int i = 0; i < W * H; i++) pixels[i] = 0x00000000u;
        im_states_draw_button(&lsm, &surface, box(4, 16, 60, 48), 0u);
        if (at(32, 32) != 0x00102030u || at(32, 16) != 0x00AA5500u) return 233;
        im_states_go_to(&lsm, "PointerOver", 100u);
        if (!im_states_is_animating(&lsm, 140u)) return 233;
        /* Render interior literal-color frame at 140 ms (40/83 from 0x102030 to 0x506070 -> 0x2F3F4F). */
        for (int i = 0; i < W * H; i++) pixels[i] = 0x00000000u;
        im_states_draw_button(&lsm, &surface, box(4, 16, 60, 48), 140u);
        if (at(32, 32) != 0x002F3F4Fu || at(32, 16) != 0x00AA5500u) return 233;
        for (int i = 0; i < W * H; i++) pixels[i] = 0x00000000u;
        im_states_draw_button(&lsm, &surface, box(4, 16, 60, 48), 183u);
        if (at(32, 32) != 0x00506070u || at(32, 16) != 0x00AA5500u) return 233;
    }

    /* 4. Unsigned 32-bit wrap-safe clock, late clamp, descending track, and IM_CONTRAST color evaluation. */
    {
        if (im_anim_elapsed_ms(0xFFFFFFF0u, 0x00000010u) != 32u) return 234;
        if (im_anim_elapsed_ms(100u, 90u) != 0u) return 235;
        im_template_keyframe desc_kfs[2] = {
            {IM_KF_LINEAR_DOUBLE, 0, {IM_TVAL_FIXED, 100 * IM_ONE, 0, 0, 0, NULL, NULL}, {0, 0, 0, 0}},
            {IM_KF_LINEAR_DOUBLE, 100, {IM_TVAL_FIXED, 20 * IM_ONE, 0, 0, 0, NULL, NULL}, {0, 0, 0, 0}},
        };
        im_template_anim desc_anim = {IM_ANIM_DOUBLE_KEYFRAMES, "X", "Y", 0, IM_DIR_NONE, false, false, false,
            {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
            {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 2};
        im_palette pal = {IM_CONTRAST, {0}};
        pal.slot[11] = 0xFF112233u;
        /* Across wrap: start=0xFFFFFFF0, now=0x00000022 -> elapsed=50 ms -> midpoint 60 * IM_ONE. */
        if (im_anim_eval_fixed(&desc_anim, desc_kfs, &pal, 0, 0xFFFFFFF0u, 0x00000022u, false) != 60 * IM_ONE) return 236;
        /* Late sample clamps to 20 * IM_ONE. */
        if (im_anim_eval_fixed(&desc_anim, desc_kfs, &pal, 0, 10u, 500u, false) != 20 * IM_ONE) return 237;
        /* IM_TVAL_COLOR multi-key + IM_TVAL_BRUSH + IM_CONTRAST slot 11 + overflow-safe lerp + negative IM_TVAL_INT. */
        pal.slot[IM_SLOT_ACCENT_COLOR_LIGHT_2] = 0xFF60CDFFu;
        im_template_keyframe col_kfs[2] = {
            {IM_KF_LINEAR_COLOR, 50, {IM_TVAL_COLOR, (int32_t)0xFF102030u, (int32_t)0xFFFFFFFFu, 0, 11, NULL, NULL}, {0, 0, 0, 0}},
            {IM_KF_LINEAR_COLOR, 100, {IM_TVAL_BRUSH, IM_BRUSH_AccentButtonBackground, 0, 0, 0, NULL, NULL}, {0, 0, 0, 0}},
        };
        im_template_anim col_anim = {IM_ANIM_COLOR_KEYFRAMES, "X", "C", 0, IM_DIR_NONE, false, false, false,
            {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
            {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 2};
        /* Independent expected ARGB at 25 ms (midpoint of 0xFF000000 -> 0xFF112233 = 0xFF09111A). */
        uint32_t mid1 = im_anim_eval_color(&col_anim, col_kfs, &pal, 0xFF000000u, 0u, 25u, false);
        if (mid1 != 0xFF09111Au) return 238;
        if (im_anim_eval_color(&col_anim, col_kfs, &pal, 0xFF000000u, 0u, 50u, false) != 0xFF112233u) return 238;
        uint32_t brush_c = im_brush_over(&pal, IM_BRUSH_AccentButtonBackground, 0, IM_ONE, IM_ONE);
        uint32_t mid2 = im_anim_eval_color(&col_anim, col_kfs, &pal, 0xFF000000u, 0u, 75u, false);
        /* Independent midpoint of 0xFF112233 -> 0xFF60CDFF = 0xFF397899 (or exact per-channel average). */
        for (int shift = 0; shift < 32; shift += 8) {
            uint32_t c0 = (0xFF112233u >> shift) & 255u;
            uint32_t c1 = (brush_c >> shift) & 255u;
            uint32_t want = (c0 + c1 + 1u) / 2u;
            if (((mid2 >> shift) & 255u) != want) return 238;
        }
        if (im_anim_eval_color(&col_anim, col_kfs, &pal, 0xFF000000u, 0u, 100u, false) != brush_c) return 238;
        im_template_value neg_int = {IM_TVAL_INT, -3, 0, 0, 0, NULL, NULL};
        if (im_template_value_to_fixed(&neg_int, &pal, 0) != -3 * IM_ONE) return 238;
        if (im_lerp_fixed_ratio(INT32_MIN / 2, INT32_MAX / 2, 0x40000000u, 0x80000000u) != 0) return 238;
    }

    /* 5. ToggleSwitch composed knob position (Off->On, On->Off, Dragging->On, unbound default 0) + 2D Reposition X/Y. */
    {
        im_palette pal = {IM_DARK, {0}};
        im_state_machine ts;
        im_states_init(&ts, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
        im_states_go_to(&ts, "Normal", 0u);
        im_states_go_to(&ts, "Off", 0u);
        im_states_go_to(&ts, "OffContent", 0u);

        /* Unbound offset defaults to 0. */
        im_states_go_to(&ts, "On", 10u);
        if (im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 10u, 999) != 0) return 239;
        im_states_go_to(&ts, "Off", 500u);

        /* Off -> On with KnobOffToOnOffset = -20 * IM_ONE. */
        im_states_set_binding(&ts, "TemplateSettings.KnobOffToOnOffset",
            (im_template_value){IM_TVAL_FIXED, -20 * IM_ONE, 0, 0, 0, NULL, NULL});
        im_states_go_to(&ts, "On", 1000u);
        im_fixed x0 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 1000u, 0)
                    + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 1000u, 0);
        im_fixed x100 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 1100u, 0)
                      + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 1100u, 0);
        im_fixed x367 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 1367u, 0)
                      + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 1367u, 0);
        if (x0 != 0 || !(x100 > 0 && x100 < 20 * IM_ONE) || x367 != 20 * IM_ONE) return 240;

        /* On -> Off with KnobOnToOffOffset = +20 * IM_ONE. */
        im_states_set_binding(&ts, "TemplateSettings.KnobOnToOffOffset",
            (im_template_value){IM_TVAL_FIXED, 20 * IM_ONE, 0, 0, 0, NULL, NULL});
        im_states_go_to(&ts, "Off", 2000u);
        im_fixed back0 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 2000u, 0)
                       + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 2000u, 0);
        im_fixed back367 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 2367u, 0)
                         + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 2367u, 0);
        if (back0 != 20 * IM_ONE || back367 != 0) return 241;

        /* Partial drag release Dragging -> On with KnobCurrentToOnOffset = -8 * IM_ONE. */
        im_states_go_to(&ts, "Dragging", 3000u);
        im_states_set_binding(&ts, "TemplateSettings.KnobCurrentToOnOffset",
            (im_template_value){IM_TVAL_FIXED, -8 * IM_ONE, 0, 0, 0, NULL, NULL});
        im_states_go_to(&ts, "On", 3100u);
        im_fixed drag0 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 3100u, 0)
                       + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 3100u, 0);
        im_fixed drag367 = im_states_sample_fixed(&ts, "KnobTranslateTransform", "X", 3467u, 0)
                         + im_states_sample_fixed(&ts, "SwitchKnob", "RepositionX", 3467u, 0);
        if (drag0 != 12 * IM_ONE || drag367 != 20 * IM_ONE) return 242;

        /* Independent groups: changing CommonStates to Pressed must NOT clear ToggleStates="On" or ContentStates="OffContent". */
        if (!im_states_go_to(&ts, "Pressed", 4000u)) return 243;
        if (!str_eq(im_states_current(&ts, 1), "On") || !str_eq(im_states_current(&ts, 2), "OffContent")) return 244;
        /* Repeat and unknown state return false (no-op). */
        if (im_states_go_to(&ts, "Pressed", 4010u)) return 245;
        if (im_states_go_to(&ts, "NonExistentState", 4010u)) return 246;

        /* 2D Reposition: literal X (12) and bound Y (-16) animate independently to 0 and snap under reduced_motion. */
        static const im_template_anim rep2d_anims[] = {
            {IM_ANIM_REPOSITION_THEME, "Box", NULL, 0, IM_DIR_NONE, true, true, false,
             {IM_TVAL_FIXED, 12 * IM_ONE, 0, 0, 0, NULL, NULL},
             {IM_TVAL_BINDING, IM_BIND_TEMPLATED_PARENT, IM_BIND_ONE_WAY, 0, 0, "OffsetY", NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 0},
        };
        static const im_template_state rep2d_states[] = {
            {"A", 0, 0, 0, 0},
            {"B", 0, 0, 0, 1},
        };
        static const im_template_group rep2d_groups[] = {{"G", 0, 2, 0, 0}};
        static const im_template rep2d_tpl = {"Rep2D", "Grid", 0, 0, 0, 0, 0, 1};
        im_template_tables rep2d_tbls = {NULL, NULL, NULL, rep2d_groups, rep2d_states, NULL, rep2d_anims, NULL};
        im_state_machine rsm;
        im_states_init_custom(&rsm, &rep2d_tpl, &rep2d_tbls, &pal);
        im_states_set_binding(&rsm, "OffsetY", (im_template_value){IM_TVAL_FIXED, -16 * IM_ONE, 0, 0, 0, NULL, NULL});
        im_states_go_to(&rsm, "B", 100u);
        if (im_states_sample_fixed(&rsm, "Box", "RepositionX", 100u, 99) != 12 * IM_ONE) return 247;
        if (im_states_sample_fixed(&rsm, "Box", "RepositionY", 100u, 99) != -16 * IM_ONE) return 248;
        im_fixed rx_mid = im_states_sample_fixed(&rsm, "Box", "RepositionX", 200u, 99);
        im_fixed ry_mid = im_states_sample_fixed(&rsm, "Box", "RepositionY", 200u, 99);
        if (!(rx_mid > 0 && rx_mid < 12 * IM_ONE) || !(ry_mid < 0 && ry_mid > -16 * IM_ONE)) return 249;
        /* Natural endpoint at 100 + 367 = 467 ms reaches 0 on both axes without reduced_motion. */
        if (im_states_sample_fixed(&rsm, "Box", "RepositionX", 467u, 99) != 0 ||
            im_states_sample_fixed(&rsm, "Box", "RepositionY", 467u, 99) != 0) return 249;
        /* Restart and verify reduced_motion snaps BOTH X and Y immediately to 0. */
        im_states_go_to(&rsm, "A", 500u);
        im_states_go_to(&rsm, "B", 600u);
        im_states_set_reduced_motion(&rsm, true, 650u);
        if (im_states_is_animating(&rsm, 650u)) return 250;
        if (im_states_sample_fixed(&rsm, "Box", "RepositionX", 650u, 99) != 0 ||
            im_states_sample_fixed(&rsm, "Box", "RepositionY", 650u, 99) != 0) return 251;
    }

    /* 6. Wildcard scoring ((NULL,B) beats (A,NULL)), case-insensitive matching, and last-default (NULL,NULL) retention. */
    {
        im_palette pal = {IM_DARK, {0}};
        static const im_template_transition wc_trans[] = {
            {NULL, NULL, 100, 0, 0},
            {NULL, NULL, 250, 0, 0}, /* last default must win */
            {"A", NULL, 150, 0, 0},  /* source-only score 1 */
            {NULL, "B", 300, 0, 0},  /* dest-only score 2 must beat source-only */
        };
        static const im_template_state wc_states[] = {
            {"A", 0, 0, 0, 0},
            {"B", 0, 0, 0, 0},
            {"C", 0, 0, 0, 0},
        };
        static const im_template_group wc_groups[] = {{"G", 0, 3, 0, 4}};
        static const im_template wc_tpl = {"WC", "Grid", 0, 0, 0, 0, 0, 1};
        im_template_tables wc_tbls = {NULL, NULL, NULL, wc_groups, wc_states, wc_trans, NULL, NULL};
        im_state_machine wsm;
        im_states_init_custom(&wsm, &wc_tpl, &wc_tbls, &pal);
        /* Case-insensitive "a" enters "A". */
        if (!im_states_go_to(&wsm, "a", 0u)) return 252;
        /* A -> B: dest-only (NULL, B) = 300 ms beats source-only (A, NULL) = 150 ms. */
        im_states_go_to(&wsm, "b", 100u);
        if (!im_states_is_animating(&wsm, 350u) || im_states_is_animating(&wsm, 400u)) return 253;
        /* B -> C: no specific match -> LAST default (250 ms, not 100 ms) wins. */
        im_states_go_to(&wsm, "C", 500u);
        if (!im_states_is_animating(&wsm, 700u) || im_states_is_animating(&wsm, 750u)) return 254;
    }

    /* 7. TextBox Focused -> Normal with and without PlaceholderForeground binding + nested DeleteButton press. */
    {
        im_palette pal = {IM_DARK, {0}};
        im_state_machine tb;
        im_states_init(&tb, IM_TEMPLATE_TEXT_BOX, &pal);
        im_states_go_to(&tb, "Normal", 0u);
        enum im_brush_id ph = (enum im_brush_id)0;
        if (!im_states_sample_brush(&tb, "PlaceholderTextContentPresenter", "Foreground", 0u, &ph, NULL, NULL) ||
            ph != IM_BRUSH_TextControlPlaceholderForeground) return 255;
        im_states_go_to(&tb, "Focused", 10u);
        if (!im_states_sample_brush(&tb, "PlaceholderTextContentPresenter", "Foreground", 10u, &ph, NULL, NULL) ||
            ph != IM_BRUSH_TextControlPlaceholderForegroundFocused) return 256;
        /* Nested DeleteButton (template index 5) presses independently while TextBox stays Focused. */
        im_state_machine del_btn;
        im_states_init(&del_btn, (im_template_id)5, &pal);
        im_states_go_to(&del_btn, "Pressed", 20u);
        enum im_brush_id del_bg = (enum im_brush_id)0, tb_border = (enum im_brush_id)0;
        if (!im_states_sample_brush(&del_btn, "ButtonLayoutGrid", "Background", 20u, &del_bg, NULL, NULL) ||
            del_bg != IM_BRUSH_TextControlButtonBackgroundPressed) return 257;
        if (!im_states_sample_brush(&tb, "BorderElement", "BorderBrush", 20u, &tb_border, NULL, NULL) ||
            tb_border != IM_BRUSH_TextControlBorderBrushFocused) return 258;
        /* Returning TextBox to Normal restores base fallback brush. */
        im_states_go_to(&tb, "Normal", 30u);
        if (!im_states_sample_brush(&tb, "PlaceholderTextContentPresenter", "Foreground", 30u, &ph, NULL, NULL) ||
            ph != IM_BRUSH_TextControlPlaceholderForeground) return 259;

        /* Now with caller-supplied PlaceholderForeground binding: stays caller brush before, during, and after Focused. */
        im_states_set_binding(&tb, "PlaceholderForeground",
            (im_template_value){IM_TVAL_BRUSH, IM_BRUSH_AccentButtonBackground, 0, 0, 0, NULL, NULL});
        if (!im_states_sample_brush(&tb, "PlaceholderTextContentPresenter", "Foreground", 40u, &ph, NULL, NULL) ||
            ph != IM_BRUSH_AccentButtonBackground) return 260;
        im_states_go_to(&tb, "Focused", 50u);
        if (!im_states_sample_brush(&tb, "PlaceholderTextContentPresenter", "Foreground", 50u, &ph, NULL, NULL) ||
            ph != IM_BRUSH_AccentButtonBackground) return 261;
        im_states_go_to(&tb, "Normal", 60u);
        if (!im_states_sample_brush(&tb, "PlaceholderTextContentPresenter", "Foreground", 60u, &ph, NULL, NULL) ||
            ph != IM_BRUSH_AccentButtonBackground) return 262;
    }

    /* 8. ListViewItemExpanded: DisabledStates, MultiSelectStates (333 ms Visibility), DataVirtualizationStates,
          ReorderHintStates (DragOver + 200 ms NoReorderHint), DragStates (DragItem, FadeIn/Out, DropTarget + 200 ms NotDragging). */
    {
        im_palette pal = {IM_DARK, {0}};
        im_state_machine lv;
        im_states_init(&lv, IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED, &pal);
        im_states_go_to(&lv, "Normal", 0u);
        im_states_go_to(&lv, "Enabled", 0u);
        im_states_go_to(&lv, "MultiSelectDisabled", 0u);
        im_states_go_to(&lv, "DataAvailable", 0u);
        im_states_go_to(&lv, "NoReorderHint", 0u);
        im_states_go_to(&lv, "NotDragging", 0u);

        /* DisabledStates */
        im_states_go_to(&lv, "Disabled", 10u);
        im_fixed dis_op = im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 10u, IM_ONE);
        if (dis_op != im_value(&pal, IM_VALUE_ListViewItemDisabledThemeOpacity)[0]) return 263;
        im_states_go_to(&lv, "Enabled", 20u);
        im_template_value op_check;
        if (im_states_sample_value(&lv, "ContentBorder", "Opacity", 20u, &op_check)) return 264;
        if (im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 20u, IM_ONE) != IM_ONE) return 264;

        /* MultiSelectStates: delayed Collapsed at 333 ms. */
        im_states_go_to(&lv, "MultiSelectEnabled", 100u);
        im_template_value vis;
        if (!im_states_sample_value(&lv, "MultiSelectSquare", "Visibility", 100u, &vis) ||
            !str_eq(vis.text, "Visible")) return 265;
        im_states_go_to(&lv, "MultiSelectDisabled", 500u);
        if (!im_states_sample_value(&lv, "MultiSelectSquare", "Visibility", 832u, &vis) ||
            !str_eq(vis.text, "Visible")) return 266;
        if (!im_states_sample_value(&lv, "MultiSelectSquare", "Visibility", 833u, &vis) ||
            !str_eq(vis.text, "Collapsed")) return 267;

        /* DataVirtualizationStates */
        im_states_go_to(&lv, "DataPlaceholder", 900u);
        if (!im_states_sample_value(&lv, "PlaceholderTextBlock", "Visibility", 900u, &vis) ||
            !str_eq(vis.text, "Visible")) return 268;
        if (!im_states_sample_value(&lv, "PlaceholderRect", "Visibility", 900u, &vis) ||
            !str_eq(vis.text, "Visible")) return 268;
        im_states_go_to(&lv, "DataAvailable", 910u);
        if (!im_states_sample_value(&lv, "PlaceholderRect", "Visibility", 910u, &vis) ||
            !str_eq(vis.text, "Collapsed")) return 269;

        /* ReorderHintStates: Bottom (+Y), Top (-Y), Left (-X), Right (+X), mid-flight reversal, and 200 ms generated return. */
        im_fixed hint_off = im_value(&pal, IM_VALUE_ListViewItemReorderHintThemeOffset)[0];
        im_states_go_to(&lv, "BottomReorderHint", 1000u);
        im_fixed bot_mid = im_states_sample_fixed(&lv, "ContentBorder", "DragOverY", 1100u, 0);
        if (!(bot_mid > 0 && bot_mid < hint_off)) return 270;
        /* Mid-flight reversal at 1100 ms: TopReorderHint must start from bot_mid (NOT 0!). */
        im_states_go_to(&lv, "TopReorderHint", 1100u);
        if (im_states_sample_fixed(&lv, "ContentBorder", "DragOverY", 1100u, 0) != bot_mid) return 270;
        if (im_states_sample_fixed(&lv, "ContentBorder", "DragOverY", 1300u, 0) != -hint_off) return 271;
        /* Horizontal Left (-X) and Right (+X) reorder hints with interior check at 100 ms. */
        im_states_go_to(&lv, "LeftReorderHint", 1350u);
        im_fixed left_mid = im_states_sample_fixed(&lv, "ContentBorder", "DragOverX", 1450u, 0);
        if (!(left_mid < 0 && left_mid > -hint_off)) return 271;
        if (im_states_sample_fixed(&lv, "ContentBorder", "DragOverX", 1550u, 0) != -hint_off) return 271;
        im_states_go_to(&lv, "RightReorderHint", 1600u);
        if (im_states_sample_fixed(&lv, "ContentBorder", "DragOverX", 1800u, 0) != hint_off) return 271;
        im_states_go_to(&lv, "NoReorderHint", 1900u);
        im_fixed ret_mid_x = im_states_sample_fixed(&lv, "ContentBorder", "DragOverX", 2000u, 0);
        im_fixed ret_end_x = im_states_sample_fixed(&lv, "ContentBorder", "DragOverX", 2100u, 0);
        if (!(ret_mid_x > 0 && ret_mid_x < hint_off) || ret_end_x != 0) return 272;

        /* DragStates: pinned DragItem (1.05 scale, 0.8*0.65=~0.52 composed opacity, 240 ms), FadeIn interior (83 ms),
           mid-flight DragItem->DragOver BEFORE 240 ms, DropTarget (0.95 scale, 240 ms), and Dragging -> Disabled -> NotDragging
           non-jump/non-masking! */
        im_states_go_to(&lv, "MultipleDraggingPrimary", 2200u);
        im_fixed fi_mid = im_states_sample_fixed(&lv, "MultiArrangeOverlayText", "Opacity", 2283u, 0);
        if (!(fi_mid > 0 && fi_mid < IM_ONE)) return 274;
        if (im_states_sample_fixed(&lv, "MultiArrangeOverlayText", "Opacity", 2367u, 0) != IM_ONE) return 274;
        im_fixed di_mid = im_states_sample_fixed(&lv, "ContentBorder", "ScaleX", 2320u, 0);
        if (!(di_mid > IM_ONE && di_mid < IM_DRAG_ITEM_SCALE)) return 273;
        /* Interrupt mid-flight at 2320 ms (120 ms < 240 ms): DragOver (DropTarget 0.95) must start from di_mid! */
        im_states_go_to(&lv, "DragOver", 2320u);
        if (im_states_sample_fixed(&lv, "ContentBorder", "ScaleX", 2320u, 0) != di_mid) return 277;
        if (im_states_sample_fixed(&lv, "ContentBorder", "ScaleX", 2560u, 0) != IM_DROP_TARGET_SCALE) return 277;
        /* Stock Dragging: verify composed 0.8 * 0.65 = ~0.52 opacity AND Dragging -> Disabled -> NotDragging (zero jump!). */
        im_states_go_to(&lv, "Dragging", 2700u);
        im_fixed drag_theme_op = im_value(&pal, IM_VALUE_ListViewItemDragThemeOpacity)[0];
        im_fixed want_drag_op = (im_fixed)(((int64_t)drag_theme_op * (int64_t)IM_DRAG_ITEM_OPACITY_MULT + (IM_ONE / 2)) >> 16);
        if (im_states_sample_fixed(&lv, "ContentBorder", "ScaleX", 2940u, 0) != IM_DRAG_ITEM_SCALE) return 273;
        if (im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 2940u, 0) != want_drag_op) return 273;
        /* Now disable while Dragging is active, then end drag to NotDragging: Opacity must stay Disabled (19661) at t=0 AND t=+200! */
        im_fixed dis_theme_op = im_value(&pal, IM_VALUE_ListViewItemDisabledThemeOpacity)[0];
        im_states_go_to(&lv, "Disabled", 3000u);
        if (im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 3000u, IM_ONE) != dis_theme_op) return 275;
        im_states_go_to(&lv, "NotDragging", 3100u);
        if (im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 3100u, IM_ONE) != dis_theme_op) return 275;
        im_fixed nd_mid = im_states_sample_fixed(&lv, "ContentBorder", "ScaleX", 3200u, IM_ONE);
        im_fixed nd_end = im_states_sample_fixed(&lv, "ContentBorder", "ScaleX", 3300u, IM_ONE);
        if (!(nd_mid > IM_ONE && nd_mid < IM_DRAG_ITEM_SCALE) || nd_end != IM_ONE) return 275;
        if (im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 3300u, IM_ONE) != dis_theme_op) return 275;
        im_states_go_to(&lv, "Enabled", 3400u);
        /* FadeOut interior tick at 83 ms and endpoint at 167 ms. */
        im_states_go_to(&lv, "ReorderedPlaceholder", 3500u);
        im_fixed fo_mid = im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 3583u, IM_ONE);
        if (!(fo_mid > 0 && fo_mid < IM_ONE)) return 276;
        if (im_states_sample_fixed(&lv, "ContentBorder", "Opacity", 3667u, IM_ONE) != 0) return 276;

        /* Custom storyboard with a destination Opacity setter (0.9) PLUS DragItemThemeAnimation declared
           BEFORE an explicit Opacity=0.8 track: settled opacity must compose the winning 0.8 * 0.65 (~0.52)
           regardless of setters or storyboard declaration order. */
        static const im_template_setter di_ord_setters[] = {
            {"ContentBorder", "Opacity", {IM_TVAL_FIXED, 58982, 0, 0, 0, NULL, NULL}},
        };
        static const im_template_anim di_ord_anims[] = {
            {IM_ANIM_DRAG_ITEM_THEME, "ContentBorder", NULL, 0, IM_DIR_NONE, false, false, false,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 0},
            {IM_ANIM_DOUBLE, "ContentBorder", "Opacity", 0, IM_DIR_NONE, false, false, true,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_THEME_VALUE, IM_VALUE_ListViewItemDragThemeOpacity, 0, 0, 0, NULL, NULL}, 0, 0},
        };
        static const im_template_state di_ord_states[] = {
            {"Rest", 0, 0, 0, 0},
            {"Drag", 0, 1, 0, 2},
        };
        static const im_template_group di_ord_groups[] = {{"DragStates", 0, 2, 0, 0}};
        static const im_template di_ord_tpl = {"DragOrd", "Grid", 0, 0, 0, 0, 0, 1};
        im_template_tables di_ord_tbls = {di_ord_setters, NULL, NULL, di_ord_groups, di_ord_states, NULL, di_ord_anims, NULL};
        im_state_machine dsm;
        im_states_init_custom(&dsm, &di_ord_tpl, &di_ord_tbls, &pal);
        im_states_go_to(&dsm, "Drag", 0u);
        if (im_states_sample_fixed(&dsm, "ContentBorder", "Opacity", 240u, 0) != want_drag_op) return 276;

        /* Outgoing Opacity=0.8 state -> DragItem-only state on base 1.0 element:
           must start from 0.8 for continuity and settle at 1.0 * 0.65 (NOT 0.8 * 0.65!). */
        static const im_template_anim di_out_anims[] = {
            {IM_ANIM_DOUBLE, "ContentBorder", "Opacity", 0, IM_DIR_NONE, false, false, true,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_THEME_VALUE, IM_VALUE_ListViewItemDragThemeOpacity, 0, 0, 0, NULL, NULL}, 0, 0},
            {IM_ANIM_DRAG_ITEM_THEME, "ContentBorder", NULL, 0, IM_DIR_NONE, false, false, false,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 0},
        };
        static const im_template_state di_out_states[] = {
            {"Dimmed", 0, 0, 0, 1},
            {"DragOnly", 0, 0, 1, 1},
        };
        static const im_template_group di_out_groups[] = {{"G", 0, 2, 0, 0}};
        static const im_template di_out_tpl = {"DragOut", "Grid", 0, 0, 0, 0, 0, 1};
        im_template_tables di_out_tbls = {NULL, NULL, NULL, di_out_groups, di_out_states, NULL, di_out_anims, NULL};
        im_state_machine dosm;
        im_states_init_custom(&dosm, &di_out_tpl, &di_out_tbls, &pal);
        im_states_go_to(&dosm, "Dimmed", 0u);
        im_states_go_to(&dosm, "DragOnly", 100u);
        if (im_states_sample_fixed(&dosm, "ContentBorder", "Opacity", 100u, 0) != drag_theme_op) return 276;
        if (im_states_sample_fixed(&dosm, "ContentBorder", "Opacity", 340u, 0) != IM_DRAG_ITEM_OPACITY_MULT) return 276;

        /* Near entry capacity (62 live entries): DragItem composing an existing Opacity entry reuses its slot
           so settled opacity still equals 0.8 * 0.65 (~0.52) instead of being dropped. */
        im_states_init_custom(&dsm, &di_ord_tpl, &di_ord_tbls, &pal);
        for (uint32_t k = 0; k < 61u; k++) {
            dsm.entries[dsm.entry_count++] = (im_state_entry){
                .kind = IM_SENTRY_SETTER, .group_index = 7, .target = "Pad", .property = "Tag", .snapped = true
            };
        }
        im_states_go_to(&dsm, "Drag", 0u);
        if (im_states_sample_fixed(&dsm, "ContentBorder", "Opacity", 240u, 0) != want_drag_op) return 276;

        /* Expired transition in Group A must NOT supply DragItem's multiplier base in Group B. */
        static const im_template_anim exp_anims[] = {
            {IM_ANIM_DOUBLE, "ContentBorder", "Opacity", 100, IM_DIR_NONE, false, false, true,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_THEME_VALUE, IM_VALUE_ListViewItemDragThemeOpacity, 0, 0, 0, NULL, NULL}, 0, 0},
            {IM_ANIM_DRAG_ITEM_THEME, "ContentBorder", NULL, 0, IM_DIR_NONE, false, false, false,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 0},
        };
        static const im_template_transition exp_trans[] = {
            {"A0", "A1", 100, 0, 1},
        };
        static const im_template_state exp_states[] = {
            {"A0", 0, 0, 0, 0},
            {"A1", 0, 0, 0, 0},
            {"B0", 0, 0, 0, 0},
            {"B1", 0, 0, 1, 1},
        };
        static const im_template_group exp_groups[] = {
            {"GA", 0, 2, 0, 1},
            {"GB", 2, 2, 0, 0},
        };
        static const im_template exp_tpl = {"ExpTrans", "Grid", 0, 0, 0, 0, 0, 2};
        im_template_tables exp_tbls = {NULL, NULL, NULL, exp_groups, exp_states, exp_trans, exp_anims, NULL};
        im_state_machine esm;
        im_states_init_custom(&esm, &exp_tpl, &exp_tbls, &pal);
        im_states_go_to(&esm, "A0", 0u);
        im_states_go_to(&esm, "A1", 10u);   /* 100 ms transition expires at 110 ms */
        im_states_go_to(&esm, "B1", 200u);  /* DragItem in GB after GA's transition expired */
        if (im_states_sample_fixed(&esm, "ContentBorder", "Opacity", 440u, 0) != IM_DRAG_ITEM_OPACITY_MULT) return 276;

        /* Destination setter Visibility=Collapsed + delayed discrete Visible keyframe at 333 ms:
           setter MUST remain active before 333 ms and switch to Visible at 333 ms. */
        static const im_template_prop del_props[] = {
            {"Visibility", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Visible", NULL}},
        };
        static const im_template_element del_elems[] = {
            {IM_ELEM_BORDER, "Box", -1, 0, 1},
        };
        static const im_template_setter del_setters[] = {
            {"Box", "Visibility", {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Collapsed", NULL}},
        };
        static const im_template_keyframe del_kfs[] = {
            {IM_KF_DISCRETE, 333, {IM_TVAL_ENUM_STR, 0, 0, 0, 0, "Visible", NULL}, {0, 0, 0, 0}},
        };
        static const im_template_anim del_anims[] = {
            {IM_ANIM_OBJECT_KEYFRAMES, "Box", "Visibility", 0, IM_DIR_NONE, false, false, false,
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL},
             {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, 0, 1},
        };
        static const im_template_state del_states[] = {
            {"S0", 0, 0, 0, 0},
            {"S1", 0, 1, 0, 1},
        };
        static const im_template_group del_groups[] = {{"G", 0, 2, 0, 0}};
        static const im_template del_tpl = {"DelKf", "Grid", 0, 0, 0, 1, 0, 1};
        im_template_tables del_tbls = {del_setters, del_elems, del_props, del_groups, del_states, NULL, del_anims, del_kfs};
        im_state_machine dlsm;
        im_states_init_custom(&dlsm, &del_tpl, &del_tbls, &pal);
        im_states_go_to(&dlsm, "S1", 100u);
        im_template_value dlv;
        if (!im_states_sample_value(&dlsm, "Box", "Visibility", 432u, &dlv) || !str_eq(dlv.text, "Collapsed")) return 276;
        if (!im_states_sample_value(&dlsm, "Box", "Visibility", 433u, &dlv) || !str_eq(dlv.text, "Visible")) return 276;
    }

    /* 9. im_states_apply_to_tree connects layout-affecting visual states to im_tree without losing local values. */
    {
        layer_alloc_rec rec = {0};
        rec.used = 0;
        im_allocator alloc = {la_alloc, la_realloc, la_free, &rec};
        im_palette pal = {IM_DARK, {0}};
        im_tree tree;
        if (!im_tree_init(&tree, alloc)) return 278;
        im_node *ts_root = im_tree_instantiate_template(&tree, &pal, IM_TEMPLATE_TOGGLE_SWITCH, "TS");
        if (!ts_root) return 279;
        tree.root = ts_root;
        im_node *knob_on = im_tree_find_by_name(ts_root, "SwitchKnobOn");
        if (!knob_on) return 280;
        /* Set a distinct caller local width (15 * IM_ONE) on SwitchKnobOn before applying Pressed state (17 * IM_ONE). */
        im_node_set_size(knob_on, 15 * IM_ONE, 15 * IM_ONE);
        im_layout_measure(&tree, ts_root, (im_size){200 * IM_ONE, 100 * IM_ONE}, IM_ONE);
        im_layout_arrange(&tree, ts_root, box(0, 0, 200, 100), IM_ONE);
        if ((knob_on->layout_rect.right - knob_on->layout_rect.left) != 15 * IM_ONE) return 281;

        im_state_machine sm;
        im_states_init(&sm, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
        im_states_go_to(&sm, "Pressed", 0u);
        im_states_apply_to_tree(&sm, ts_root, 83u);
        im_layout_measure(&tree, ts_root, (im_size){200 * IM_ONE, 100 * IM_ONE}, IM_ONE);
        im_layout_arrange(&tree, ts_root, box(0, 0, 200, 100), IM_ONE);
        if ((knob_on->layout_rect.right - knob_on->layout_rect.left) != 17 * IM_ONE) return 282;

        /* Re-init empty state machine (no active Pressed override) -> local 15 * IM_ONE must restore cleanly! */
        im_states_init(&sm, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
        im_states_apply_to_tree(&sm, ts_root, 200u);
        im_layout_measure(&tree, ts_root, (im_size){200 * IM_ONE, 100 * IM_ONE}, IM_ONE);
        im_layout_arrange(&tree, ts_root, box(0, 0, 200, 100), IM_ONE);
        if ((knob_on->layout_rect.right - knob_on->layout_rect.left) != 15 * IM_ONE) return 283;
        im_tree_destroy(&tree);
        if (rec.live_allocs != 0 || rec.live_bytes != 0) return 284;

        /* Also verify ListViewItemExpanded MultiSelectSquare Visibility toggles layout via im_states_apply_to_tree. */
        rec.used = 0;
        if (!im_tree_init(&tree, alloc)) return 290;
        im_node *lv_root = im_tree_instantiate_template(&tree, &pal, IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED, "LV");
        if (!lv_root) return 291;
        tree.root = lv_root;
        im_node *ms_sq = im_tree_find_by_name(lv_root, "MultiSelectSquare");
        if (!ms_sq) return 292;
        im_layout_measure(&tree, lv_root, (im_size){200 * IM_ONE, 40 * IM_ONE}, IM_ONE);
        if (ms_sq->visibility != IM_VIS_COLLAPSED || ms_sq->desired_size.width != 0) return 293;
        im_states_init(&sm, IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED, &pal);
        im_states_go_to(&sm, "MultiSelectEnabled", 0u);
        im_states_apply_to_tree(&sm, lv_root, 0u);
        im_layout_measure(&tree, lv_root, (im_size){200 * IM_ONE, 40 * IM_ONE}, IM_ONE);
        if (ms_sq->visibility != IM_VIS_VISIBLE || ms_sq->desired_size.width == 0) return 294;
        im_states_go_to(&sm, "MultiSelectDisabled", 100u);
        im_states_apply_to_tree(&sm, lv_root, 433u);
        im_layout_measure(&tree, lv_root, (im_size){200 * IM_ONE, 40 * IM_ONE}, IM_ONE);
        if (ms_sq->visibility != IM_VIS_COLLAPSED || ms_sq->desired_size.width != 0) return 295;
        im_tree_destroy(&tree);
        if (rec.live_allocs != 0 || rec.live_bytes != 0) return 296;

        /* Verify TextBox BorderElement BorderThickness reaches im_node_effective_border_thickness on Focused and restores on Normal. */
        rec.used = 0;
        if (!im_tree_init(&tree, alloc)) return 297;
        im_node *tb_root = im_tree_instantiate_template(&tree, &pal, IM_TEMPLATE_TEXT_BOX, "TB");
        if (!tb_root) return 297;
        tree.root = tb_root;
        im_node *tb_border = im_tree_find_by_name(tb_root, "BorderElement");
        if (!tb_border) return 297;
        im_thickness base_bt = im_node_effective_border_thickness(tb_border);
        im_states_init(&sm, IM_TEMPLATE_TEXT_BOX, &pal);
        im_states_go_to(&sm, "Focused", 0u);
        im_states_apply_to_tree(&sm, tb_root, 0u);
        im_thickness foc_bt = im_node_effective_border_thickness(tb_border);
        const im_fixed *want_foc = im_value(&pal, IM_VALUE_TextControlBorderThemeThicknessFocused);
        if (foc_bt.bottom != want_foc[3] || foc_bt.bottom == base_bt.bottom) return 298;
        im_states_go_to(&sm, "Normal", 10u);
        im_states_apply_to_tree(&sm, tb_root, 10u);
        im_thickness rest_bt = im_node_effective_border_thickness(tb_border);
        if (rest_bt.bottom != base_bt.bottom) return 299;
        im_tree_destroy(&tree);
        if (rec.live_allocs != 0 || rec.live_bytes != 0) return 299;
    }

    /* 10. Reduced motion: snap on entry, snap mid-flight, and re-enable for subsequent transitions. */
    {
        im_palette pal = {IM_DARK, {0}};
        im_state_machine sm;
        im_states_init(&sm, IM_TEMPLATE_BUTTON, &pal);
        im_states_set_reduced_motion(&sm, true, 0u);
        im_states_go_to(&sm, "Normal", 0u);
        im_states_go_to(&sm, "PointerOver", 100u);
        if (im_states_is_animating(&sm, 100u)) return 285;
        uint32_t want_po = im_brush_over(&pal, IM_BRUSH_ButtonBackgroundPointerOver, 0, IM_ONE, IM_ONE);
        if (im_states_sample_color(&sm, "ContentPresenter", "Background", 100u, 0u) != want_po) return 286;

        /* Re-enable animation: next transition must animate normally. */
        im_states_set_reduced_motion(&sm, false, 200u);
        im_states_go_to(&sm, "Pressed", 200u);
        if (!im_states_is_animating(&sm, 220u)) return 287;
        /* Mid-flight reduced_motion snaps immediately. */
        im_states_set_reduced_motion(&sm, true, 220u);
        if (im_states_is_animating(&sm, 220u)) return 288;
        uint32_t want_pr = im_brush_over(&pal, IM_BRUSH_ButtonBackgroundPressed, 0, IM_ONE, IM_ONE);
        if (im_states_sample_color(&sm, "ContentPresenter", "Background", 220u, 0u) != want_pr) return 289;
    }

    return 0;
}

int im_main(void) {
    const im_radii square = {0}, round = {4 * IM_ONE, 4 * IM_ONE, 4 * IM_ONE, 4 * IM_ONE};
    const enum im_brush_id white = IM_BRUSH_TextFillColorPrimaryBrush; /* #FFFFFF in the dark theme */

    clear();
    im_fill(&surface, box(10, 10, 20, 20), square, white);
    if (at(9, 15) || at(20, 15) || at(15, 9) || at(15, 20)) return 1;
    if (at(10, 10) != 0xFFFFFF || at(19, 19) != 0xFFFFFF) return 2;

    clear();
    im_fill(&surface, (im_rect){10 * IM_ONE + IM_ONE / 2, 10 * IM_ONE, 20 * IM_ONE, 20 * IM_ONE}, square, white);
    if (at(10, 15) != 0x808080) return 3; /* half covered */

    clear();
    im_fill(&surface, box(10, 10, 42, 42), round, white);
    if (at(10, 10) || at(41, 41) || at(14, 10) != 0xFFFFFF || at(26, 26) != 0xFFFFFF) return 4;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (at(10 + x, 10 + y) != at(41 - x, 10 + y) || at(10 + x, 10 + y) != at(10 + x, 41 - y)) return 5;

    clear(); /* oversized radii clamp to fit; a D2D-flattened N=8 (32-gon) circle of radius 16 has area 799.09 */
    im_fill(&surface, box(10, 10, 42, 42), (im_radii){99 * IM_ONE, 99 * IM_ONE, 99 * IM_ONE, 99 * IM_ONE}, white);
    if (at(10, 10) != 0 || (at(25, 10) & 255) < 230 || (at(10, 25) & 255) < 230 || at(25, 11) != 0xFFFFFF) return 6;
    uint32_t sum = 0;
    for (int i = 0; i < W * H; i++) sum += pixels[i] & 255;
    if (sum < 798 * 255 || sum > 800 * 255) return 6;

    clear();
    im_stroke(&surface, box(10, 10, 42, 42), round, IM_ONE, white);
    if (at(20, 10) != 0xFFFFFF || at(20, 11) || at(10, 20) != 0xFFFFFF || at(41, 20) != 0xFFFFFF || at(11, 20)) return 7;

    clear(); /* nothing outside the buffer is touched */
    im_fill(&surface, box(-100, -100, 200, 200), round, white);
    im_fill(&surface, box(200, 200, 300, 300), round, white);
    im_fill(&surface, box(20, 20, 10, 10), round, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 8;

    if (im_brush_over(&surface.palette, IM_BRUSH_ButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0x0FFFFFFF) return 9;
    surface.palette.theme = IM_LIGHT;
    if (im_brush_over(&surface.palette, IM_BRUSH_ButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0xB3FFFFFF) return 10;
    /* The light border is darker at the bottom, the dark one at the top. */
    uint32_t top = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE / 2, 32 * IM_ONE, IM_ONE);
    uint32_t low = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, 32 * IM_ONE - IM_ONE / 2, 32 * IM_ONE, IM_ONE);
    if (top >> 24 != 0x0F || low >> 24 != 0x29) return 11;

    surface.palette.slot[IM_SLOT_ACCENT_COLOR_DARK_1] = 0xFF005FB8;
    if (im_brush_over(&surface.palette, IM_BRUSH_AccentButtonBackground, 0, 32 * IM_ONE, IM_ONE) != 0xFF005FB8) return 12;
    if (im_brush_over(&surface.palette, IM_BRUSH_AccentButtonBackgroundPressed, 0, 32 * IM_ONE, IM_ONE) != 0xCC005FB8) return 13;
    const im_fixed *padding = im_value(&surface.palette, IM_VALUE_ButtonPadding);
    if (padding[0] != 11 * IM_ONE || padding[1] != 5 * IM_ONE || padding[2] != 11 * IM_ONE || padding[3] != 6 * IM_ONE) return 14;

    /* Coordinates at the limits of an im_fixed fill the buffer, and nothing wraps. */
    surface.palette.theme = IM_DARK;
    clear();
    im_fill(&surface, (im_rect){-20000 * IM_ONE, -20000 * IM_ONE, 20000 * IM_ONE, 20000 * IM_ONE}, square, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 15;
    clear();
    im_fill(&surface, (im_rect){0, 0, INT32_MAX, INT32_MAX}, square, white);
    if (at(0, 0) != 0xFFFFFF || at(63, 63) != 0xFFFFFF) return 16;
    clear();
    im_fill(&surface, (im_rect){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX}, (im_radii){INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}, white);
    if (at(32, 32) != 0xFFFFFF) return 17;
    clear();
    im_stroke(&surface, (im_rect){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX}, round, 40 * IM_ONE, white);
    if (at(32, 32) != 0) return 18;

    /* A gradient runs over the shape's exact height, however high it is. */
    surface.palette.theme = IM_LIGHT;
    uint32_t high = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE / 2, (int64_t)32768 * IM_ONE, IM_ONE);
    if (high >> 24 != 0x0F) return 19;
    clear();
    im_fill(&surface, (im_rect){0, 0, 8 * IM_ONE, 3 * IM_ONE + IM_ONE / 4}, square, IM_BRUSH_ButtonBorderBrush);
    uint32_t exact = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE + IM_ONE / 2, 3 * IM_ONE + IM_ONE / 4, IM_ONE);
    uint32_t white_by = im_brush_over(&surface.palette, IM_BRUSH_ButtonBorderBrush, IM_ONE + IM_ONE / 2, 3 * IM_ONE, IM_ONE);
    for (int i = 0; i < W * H; i++) pixels[i] = 0xFFFFFF;
    im_fill(&surface, (im_rect){0, 0, 8 * IM_ONE, 3 * IM_ONE + IM_ONE / 4}, square, IM_BRUSH_ButtonBorderBrush);
    uint32_t alpha = exact >> 24;
    if (alpha == white_by >> 24) return 20; /* the two heights give different opacities here */
    if ((at(2, 1) & 255) != (255 * (255 - alpha) + 127) / 255) return 21;

    /* The subtle button's background ends inside its border (InnerBorderEdge), and its corner is rounded. */
    surface.palette.theme = IM_DARK;
    clear();
    im_draw_button(&surface, box(0, 0, 60, 32), IM_BUTTON_SUBTLE, IM_HOVER);
    if (at(30, 0) != 0x0F0F0F || at(30, 16) != 0x0F0F0F) return 22;
    if (at(0, 0) != 0 || at(2, 0) == 0 || at(2, 0) == 0x0F0F0F) return 23;

    /* Asymmetric corner fitting matches CGeometryBuilder::CalculateRoundedCornersRectangle:
       a 40x20 box with top-left radius 16 and three square corners keeps radius 16 (not clamped to 10). */
    clear();
    im_fill(&surface, box(12, 22, 52, 42), (im_radii){16 * IM_ONE, 0, 0, 0}, white);
    if (at(12, 22) != 0 || at(12, 30) != 0) return 24; /* outside the r=16 corner; would be lit if clamped to 10 */
    if (at(12, 38) != 0xFFFFFF || at(51, 22) != 0xFFFFFF || at(51, 41) != 0xFFFFFF || at(12, 41) != 0xFFFFFF) return 25;

    /* Competing unequal radii: 40x20 with top_left=16, bottom_left=12 splits the 20px left edge
       proportionally (tl_y = 16*20/28 = 11.43, bl_y = 8.57) while keeping tl_x=16, bl_x=12. */
    clear();
    im_fill(&surface, box(10, 20, 50, 40), (im_radii){16 * IM_ONE, 0, 0, 12 * IM_ONE}, white);
    /* Left edge meets at y = 20 + 11.43 = 31.43: pixel (10, 31) is near the tangent, (10, 20) is empty,
       and along the top edge (25, 20) is still partial because tl_x remains 16 (not clamped to 11.43). */
    if (at(10, 20) != 0 || at(10, 39) != 0) return 26;
    if ((at(10, 31) & 255) < 200) return 27;
    if (at(25, 20) == 0xFFFFFF) return 28; /* tl_x = 16 leaves x=25 partial; clamping to 11.43 would fill it */
    if (at(26, 20) != 0xFFFFFF || at(22, 39) != 0xFFFFFF) return 29;
    /* Exercise im_stroke with competing unequal radii: outer radii are {17,0,0,13} on 40x20
       (tl_y = 17*20/30 = 11.33, tl_x = 17 unclamped) and inner radii are {15,0,0,11} on 36x16. */
    clear();
    im_stroke(&surface, box(10, 20, 50, 40), (im_radii){16 * IM_ONE, 0, 0, 12 * IM_ONE}, 2 * IM_ONE, white);
    if (at(10, 20) != 0 || at(30, 20) != 0xFFFFFF || at(30, 22) != 0 || at(49, 30) != 0xFFFFFF) return 30;
    /* Outer arc at x=25, y=20 is partial because outer tl_x=17 > 15 (half-short-side clamping to 10 would make it 0xFFFFFF). */
    if (at(25, 20) == 0 || at(25, 20) == 0xFFFFFF) return 31;
    /* Inner contour at x=25, y=22 is outside the inner arc (partial/lit); half-short-side clamping would leave it empty. */
    if (at(25, 22) == 0) return 32;

    /* Large radius (> 185 px) uses Wang's formula cubic flattening (N = 32 for r = 256) without overflow. */
    clear();
    im_fill(&surface, box(0, 0, 600, 600), (im_radii){256 * IM_ONE, 0, 0, 0}, white);
    if (at(0, 0) != 0 || at(63, 63) != 0) return 33; /* top-left 64x64 viewport lies outside the r=256 corner */
    clear();
    im_fill(&surface, box(-200, 0, 400, 600), (im_radii){256 * IM_ONE, 0, 0, 0}, white);
    if (at(63, 63) != 0xFFFFFF || at(0, 0) != 0) return 34; /* spans across the r=256 flattened cubic */

    /* Cubic Bezier subdivision at r=8 (N=4, t=0.25 vertex at (0.92142, 0.38925)*8) gives 37 at (8,12)
       and 126 at (8,13), distinct from equal-angle chords (40, 128) and exact circle (49, 151). */
    clear();
    im_fill(&surface, box(8, 8, 56, 56), (im_radii){8 * IM_ONE, 8 * IM_ONE, 8 * IM_ONE, 8 * IM_ONE}, white);
    if ((at(8, 12) & 255) != 37 || (at(8, 13) & 255) != 126) return 35;
    int r = test_templates();
    if (r != 0) return r;
    r = test_tree_and_layout();
    if (r != 0) return r;
    r = test_clips_and_layers();
    if (r != 0) return r;
    return test_visual_states_and_animation();
}
