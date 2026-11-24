/* Exits 0, or with the number of the first check that failed. */
#include <impossible/base.h>
#include "button.h"
#include "template.h"

enum { W = 64, H = 64 };
static uint32_t pixels[W * H];
static im_surface surface = {pixels, W, H, W, {IM_DARK, {0}}, IM_ONE};

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

    const im_template *sub = &im_templates[IM_TEMPLATE_BUTTON_SUBTLE];
    if (!has_style_setter_brush(sub, "Background", IM_BRUSH_SubtleButtonBackground)) return 48;
    if (!has_state_prop_brush(sub, "PointerOver", "ContentPresenter", "Background", IM_BRUSH_SubtleButtonBackgroundPointerOver)) return 49;
    if (!has_state_prop_brush(sub, "Pressed", "ContentPresenter", "Background", IM_BRUSH_SubtleButtonBackgroundPressed)) return 50;

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
    return test_templates();
}
