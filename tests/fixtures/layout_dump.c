/* Dumps ui/layout.c computed Rect and DesiredSize for the 5-scale layout oracle cases as JSON. */
#include <impossible/base.h>
#include "layout.h"

/* Simple bump/free allocator backed by a static arena for the console fixture. */
static uint8_t arena[256 * 1024];
static size_t arena_used = 0;

static void *arena_alloc(void *ctx, size_t size) {
    (void)ctx;
    size = (size + 7u) & ~(size_t)7u;
    if (arena_used + size > sizeof(arena)) return NULL;
    void *p = &arena[arena_used];
    arena_used += size;
    return p;
}

static void *arena_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    void *n = arena_alloc(ctx, new_size);
    if (!n) return NULL;
    if (ptr && old_size > 0) {
        size_t copy = old_size < new_size ? old_size : new_size;
        uint8_t *d = (uint8_t *)n;
        const uint8_t *s = (const uint8_t *)ptr;
        for (size_t i = 0; i < copy; i++) d[i] = s[i];
    }
    return n;
}

static void arena_free(void *ctx, void *ptr, size_t size) {
    (void)ctx; (void)ptr; (void)size;
}

static void out_str(const char *s) {
    im_print(s);
}

static void out_int(int64_t v) {
    char buf[32];
    int len = 0;
    if (v < 0) { out_str("-"); v = -v; }
    if (v == 0) { out_str("0"); return; }
    while (v > 0) { buf[len++] = (char)('0' + (v % 10)); v /= 10; }
    char rev[33];
    for (int i = 0; i < len; i++) rev[i] = buf[len - 1 - i];
    rev[len] = '\0';
    im_print(rev);
}

/* Print a 16.16 fixed-point number rounded to 3 decimal places. */
static void out_fixed(im_fixed f) {
    int64_t scaled = (int64_t)f * 1000;
    int64_t rounded = scaled >= 0 ? (scaled + 32768) / 65536 : -((-scaled + 32768) / 65536);
    if (rounded < 0) { out_str("-"); rounded = -rounded; }
    out_int(rounded / 1000);
    int64_t frac = rounded % 1000;
    if (frac != 0) {
        out_str(".");
        char d[4] = {
            (char)('0' + (frac / 100)),
            (char)('0' + ((frac / 10) % 10)),
            (char)('0' + (frac % 10)),
            '\0',
        };
        int n = 3;
        while (n > 1 && d[n - 1] == '0') n--;
        d[n] = '\0';
        im_print(d);
    }
}

static im_fixed fx(int v) { return (im_fixed)(v * IM_ONE); }

static void emit_node_json(const im_node *node, bool *first) {
    if (!node) return;
    if (node->name && node->name[0]) {
        if (!*first) out_str(",");
        *first = false;
        out_str("\""); out_str(node->name); out_str("\":{");
        out_str("\"Visibility\":\""); out_str(node->visibility == IM_VIS_COLLAPSED ? "Collapsed" : "Visible"); out_str("\",");
        out_str("\"Rect\":{\"X\":"); out_fixed(node->absolute_rect.left);
        out_str(",\"Y\":"); out_fixed(node->absolute_rect.top);
        out_str(",\"Width\":"); out_fixed(node->absolute_rect.right - node->absolute_rect.left);
        out_str(",\"Height\":"); out_fixed(node->absolute_rect.bottom - node->absolute_rect.top);
        out_str("},\"DesiredSize\":{\"Width\":"); out_fixed(node->desired_size.width);
        out_str(",\"Height\":"); out_fixed(node->desired_size.height);
        out_str("}}");
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        emit_node_json(node->children[i], first);
    }
}

static void emit_case(const char *base, const char *tag, im_fixed scale, im_node *root) {
    out_str("\""); out_str(base); out_str("-"); out_str(tag); out_str("\":{");
    out_str("\"scale\":"); out_fixed(scale);
    out_str(",\"nodes\":{");
    bool first = true;
    emit_node_json(root, &first);
    out_str("}}");
}

int im_main(void) {
    static const struct { const char *tag; im_fixed scale; } scales[5] = {
        {"s100", 65536},
        {"s125", 81920},
        {"s150", 98304},
        {"s200", 131072},
        {"s300", 196608},
    };
    im_palette pal = {IM_DARK, {0}};
    im_allocator alloc = {arena_alloc, arena_realloc, arena_free, NULL};
    out_str("{");
    bool first_case = true;

    for (int si = 0; si < 5; si++) {
        im_fixed sc = scales[si].scale;
        const char *tag = scales[si].tag;

        /* 1. grid-mixed */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *g = im_tree_create_node(&t, IM_ELEM_GRID, "GridRoot");
            im_node_set_padding(g, (im_thickness){fx(3), fx(5), fx(7), fx(9)});
            im_node_set_border_thickness(g, (im_thickness){fx(1), fx(2), fx(3), fx(4)});
            im_node_set_grid_spacing(g, fx(5), fx(7));
            im_grid_def rows[4] = {
                {IM_GRID_PIXEL, fx(21), 0, IM_FIXED_INF},
                {IM_GRID_AUTO, 0, fx(15), IM_FIXED_INF},
                {IM_GRID_STAR, fx(1), fx(10), fx(50)},
                {IM_GRID_STAR, fx(2), 0, IM_FIXED_INF},
            };
            im_grid_def cols[3] = {
                {IM_GRID_PIXEL, fx(25), 0, IM_FIXED_INF},
                {IM_GRID_AUTO, 0, 0, fx(40)},
                {IM_GRID_STAR, fx(1), 0, IM_FIXED_INF},
            };
            im_tree_set_grid_rows(&t, g, rows, 4);
            im_tree_set_grid_cols(&t, g, cols, 3);
            im_node *c0 = im_tree_create_node(&t, IM_ELEM_BORDER, "CellPix");
            im_node_set_grid_cell(c0, 0, 0, 1, 1);
            im_node *c1 = im_tree_create_node(&t, IM_ELEM_BORDER, "CellSpanCol");
            im_node_set_grid_cell(c1, 0, 1, 1, 2);
            im_node *c2 = im_tree_create_node(&t, IM_ELEM_BORDER, "CellSpanRow");
            im_node_set_grid_cell(c2, 1, 0, 2, 1);
            im_node *c3 = im_tree_create_node(&t, IM_ELEM_BORDER, "CellAuto");
            im_node_set_grid_cell(c3, 1, 1, 1, 1);
            im_node_set_size(c3, fx(28), fx(19));
            im_node_set_alignment(c3, IM_ALIGN_START, IM_ALIGN_START);
            im_node *c4 = im_tree_create_node(&t, IM_ELEM_BORDER, "CellStar");
            im_node_set_grid_cell(c4, 3, 2, 1, 1);
            im_tree_append_child(&t, g, c0);
            im_tree_append_child(&t, g, c1);
            im_tree_append_child(&t, g, c2);
            im_tree_append_child(&t, g, c3);
            im_tree_append_child(&t, g, c4);
            t.root = g;
            im_layout_run(&t, g, fx(160), fx(120), sc);
            if (!first_case) out_str(",");
            first_case = false;
            emit_case("grid-mixed", tag, sc, g);
        }

        /* 2. grid-unbounded */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *st = im_tree_create_node(&t, IM_ELEM_STACK_PANEL, "OuterStack");
            im_node_set_stack(st, IM_ORIENT_VERTICAL, fx(4));
            im_node *g = im_tree_create_node(&t, IM_ELEM_GRID, "InnerGrid");
            im_node_set_grid_spacing(g, fx(3), 0);
            im_grid_def rows[2] = {
                {IM_GRID_STAR, fx(1), 0, IM_FIXED_INF},
                {IM_GRID_STAR, fx(2), 0, IM_FIXED_INF},
            };
            im_tree_set_grid_rows(&t, g, rows, 2);
            im_node *ra = im_tree_create_node(&t, IM_ELEM_BORDER, "RowA");
            im_node_set_grid_cell(ra, 0, 0, 1, 1);
            im_node_set_size(ra, fx(50), fx(17));
            im_node_set_alignment(ra, IM_ALIGN_START, IM_ALIGN_STRETCH);
            im_node *rb = im_tree_create_node(&t, IM_ELEM_BORDER, "RowB");
            im_node_set_grid_cell(rb, 1, 0, 1, 1);
            im_node_set_size(rb, fx(70), fx(23));
            im_node_set_alignment(rb, IM_ALIGN_START, IM_ALIGN_STRETCH);
            im_tree_append_child(&t, g, ra);
            im_tree_append_child(&t, g, rb);
            im_tree_append_child(&t, st, g);
            t.root = st;
            im_layout_run(&t, st, fx(160), fx(120), sc);
            out_str(",");
            emit_case("grid-unbounded", tag, sc, st);
        }

        /* 3. stack-vert */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *st = im_tree_create_node(&t, IM_ELEM_STACK_PANEL, "VStack");
            im_node_set_stack(st, IM_ORIENT_VERTICAL, fx(7));
            im_node_set_padding(st, (im_thickness){fx(4), fx(6), fx(8), fx(10)});
            im_node *a = im_tree_create_node(&t, IM_ELEM_BORDER, "VFirst");
            im_node_set_size(a, IM_FIXED_AUTO, fx(15));
            im_node *b = im_tree_create_node(&t, IM_ELEM_BORDER, "VMidCollapsed");
            im_node_set_size(b, IM_FIXED_AUTO, fx(30));
            im_node_set_visibility(b, IM_VIS_COLLAPSED);
            im_node *c = im_tree_create_node(&t, IM_ELEM_BORDER, "VLast");
            im_node_set_size(c, IM_FIXED_AUTO, fx(19));
            im_tree_append_child(&t, st, a);
            im_tree_append_child(&t, st, b);
            im_tree_append_child(&t, st, c);
            t.root = st;
            im_layout_run(&t, st, fx(140), fx(100), sc);
            out_str(",");
            emit_case("stack-vert", tag, sc, st);
        }

        /* 4. stack-horiz */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *st = im_tree_create_node(&t, IM_ELEM_STACK_PANEL, "HStack");
            im_node_set_stack(st, IM_ORIENT_HORIZONTAL, fx(5));
            im_node *a = im_tree_create_node(&t, IM_ELEM_BORDER, "HTop");
            im_node_set_size(a, fx(21), fx(17));
            im_node_set_alignment(a, IM_ALIGN_STRETCH, IM_ALIGN_START);
            im_node *b = im_tree_create_node(&t, IM_ELEM_BORDER, "HCenter");
            im_node_set_size(b, fx(25), fx(19));
            im_node_set_alignment(b, IM_ALIGN_STRETCH, IM_ALIGN_CENTER);
            im_node *c = im_tree_create_node(&t, IM_ELEM_BORDER, "HBottom");
            im_node_set_size(c, fx(29), fx(23));
            im_node_set_alignment(c, IM_ALIGN_STRETCH, IM_ALIGN_END);
            im_tree_append_child(&t, st, a);
            im_tree_append_child(&t, st, b);
            im_tree_append_child(&t, st, c);
            t.root = st;
            im_layout_run(&t, st, fx(160), fx(80), sc);
            out_str(",");
            emit_case("stack-horiz", tag, sc, st);
        }

        /* 5. border-round */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *g = im_tree_create_node(&t, IM_ELEM_GRID, "RoundGrid");
            im_grid_def cols[2] = {
                {IM_GRID_PIXEL, fx(60), 0, IM_FIXED_INF},
                {IM_GRID_PIXEL, fx(60), 0, IM_FIXED_INF},
            };
            im_tree_set_grid_cols(&t, g, cols, 2);

            im_node *br = im_tree_create_node(&t, IM_ELEM_BORDER, "RoundedBorder");
            im_node_set_grid_cell(br, 0, 0, 1, 1);
            im_node_set_size(br, fx(40), fx(40));
            im_node_set_border_thickness(br, (im_thickness){fx(1), fx(2), fx(3), fx(1)});
            im_node_set_padding(br, (im_thickness){fx(2), fx(3), fx(2), fx(3)});
            im_node_set_margin(br, (im_thickness){fx(3), fx(5), fx(7), fx(9)});
            im_node_set_alignment(br, IM_ALIGN_START, IM_ALIGN_START);
            im_node_set_layout_rounding(br, true);
            im_node *brc = im_tree_create_node(&t, IM_ELEM_BORDER, "RoundedChild");
            im_node_set_size(brc, fx(11), fx(11));
            im_node_set_alignment(brc, IM_ALIGN_CENTER, IM_ALIGN_CENTER);
            im_node_set_layout_rounding(brc, true);
            im_tree_append_child(&t, br, brc);

            im_node *bu = im_tree_create_node(&t, IM_ELEM_BORDER, "UnroundedBorder");
            im_node_set_grid_cell(bu, 0, 1, 1, 1);
            im_node_set_size(bu, fx(40), fx(40));
            im_node_set_border_thickness(bu, (im_thickness){fx(1), fx(2), fx(3), fx(1)});
            im_node_set_padding(bu, (im_thickness){fx(2), fx(3), fx(2), fx(3)});
            im_node_set_margin(bu, (im_thickness){fx(3), fx(5), fx(7), fx(9)});
            im_node_set_alignment(bu, IM_ALIGN_START, IM_ALIGN_START);
            im_node_set_layout_rounding(bu, false);
            im_node *buc = im_tree_create_node(&t, IM_ELEM_BORDER, "UnroundedChild");
            im_node_set_size(buc, fx(11), fx(11));
            im_node_set_alignment(buc, IM_ALIGN_CENTER, IM_ALIGN_CENTER);
            im_node_set_layout_rounding(buc, false);
            im_tree_append_child(&t, bu, buc);

            im_tree_append_child(&t, g, br);
            im_tree_append_child(&t, g, bu);
            t.root = g;
            im_layout_run(&t, g, fx(120), fx(60), sc);
            out_str(",");
            emit_case("border-round", tag, sc, g);
        }

        /* 6. presenter-rtl */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *g = im_tree_create_node(&t, IM_ELEM_GRID, "RtlRoot");
            im_node_set_flow_direction(g, IM_FLOW_RTL);
            im_node *cp = im_tree_create_node(&t, IM_ELEM_CONTENT_PRESENTER, "RtlPresenter");
            im_node_set_size(cp, fx(100), fx(50));
            im_node_set_margin(cp, (im_thickness){fx(3), fx(4), fx(11), fx(6)});
            im_node_set_padding(cp, (im_thickness){fx(5), fx(3), fx(9), fx(7)});
            im_node_set_border_thickness(cp, (im_thickness){fx(1), fx(2), fx(3), fx(4)});
            im_node_set_alignment(cp, IM_ALIGN_START, IM_ALIGN_START);
            im_node_set_content_alignment(cp, IM_ALIGN_START, IM_ALIGN_END);
            im_node *ch = im_tree_create_node(&t, IM_ELEM_BORDER, "RtlChild");
            im_node_set_size(ch, fx(23), fx(13));
            im_tree_append_child(&t, cp, ch);
            im_tree_append_child(&t, g, cp);
            t.root = g;
            im_layout_run(&t, g, fx(160), fx(80), sc);
            out_str(",");
            emit_case("presenter-rtl", tag, sc, g);
        }

        /* 7. button-child */
        {
            arena_used = 0;
            im_tree t; im_tree_init(&t, alloc);
            im_node *frame = im_tree_create_node(&t, IM_ELEM_GRID, NULL);
            im_node *btn = im_tree_instantiate_template(&t, &pal, IM_TEMPLATE_BUTTON, "BtnRoot");
            im_node_set_size(btn, fx(110), fx(36));
            im_node_set_padding(btn, (im_thickness){fx(5), fx(3), fx(13), fx(7)});
            im_node_set_alignment(btn, IM_ALIGN_CENTER, IM_ALIGN_CENTER);
            im_node_set_content_alignment(btn, IM_ALIGN_START, IM_ALIGN_END);
            im_node *cp = im_tree_find_by_name(btn, "ContentPresenter");
            im_node *ch = im_tree_create_node(&t, IM_ELEM_BORDER, "BtnChild");
            im_node_set_size(ch, fx(31), fx(13));
            im_tree_append_child(&t, cp, ch);
            im_tree_append_child(&t, frame, btn);
            t.root = frame;
            im_layout_run(&t, frame, fx(160), fx(64), sc);
            out_str(",");
            emit_case("button-child", tag, sc, frame);
        }
    }
    out_str("}\n");
    return 0;
}
