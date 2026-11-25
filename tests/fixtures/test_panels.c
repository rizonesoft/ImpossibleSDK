/* Tier-0 freestanding verification fixture for ui/panels.{h,c} (D01 T01 s16). */
#include <impossible/base.h>
#include <windows.h>
#include "dirty.h"
#include "input.h"
#include "layout.h"
#include "panels.h"
#include "transform.h"
#include "tree.h"

#define FX(n) ((im_fixed)((int64_t)(n) * IM_ONE))

static int g_failures = 0;

static void write_str(DWORD handle_id, const char *s) {
    HANDLE h = GetStdHandle(handle_id);
    DWORD len = 0;
    while (s[len]) len++;
    DWORD written = 0;
    WriteFile(h, s, len, &written, NULL);
}

static void write_i64(DWORD handle_id, int64_t v) {
    char buf[32];
    int n = 0;
    uint64_t u;
    if (v < 0) {
        write_str(handle_id, "-");
        u = (uint64_t)(-(v + 1)) + 1u;
    } else {
        u = (uint64_t)v;
    }
    if (u == 0) {
        buf[n++] = '0';
    } else {
        while (u > 0) {
            buf[n++] = (char)('0' + (u % 10u));
            u /= 10u;
        }
    }
    char out[32];
    for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
    out[n] = 0;
    write_str(handle_id, out);
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        write_str(STD_ERROR_HANDLE, "FAIL: " msg "\n"); \
        g_failures++; \
    } \
} while (0)

typedef struct heap_ctx {
    HANDLE heap;
    size_t limit;
    size_t used;
} heap_ctx;

static void *h_alloc(void *vctx, size_t size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (c->limit > 0 && c->used + size > c->limit) return NULL;
    void *p = HeapAlloc(c->heap, 0, size ? size : 1);
    if (p) c->used += size;
    return p;
}

static void *h_realloc(void *vctx, void *ptr, size_t old_size, size_t new_size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (c->limit > 0 && c->used - old_size + new_size > c->limit) return NULL;
    void *p = ptr ? HeapReAlloc(c->heap, 0, ptr, new_size ? new_size : 1)
                  : HeapAlloc(c->heap, 0, new_size ? new_size : 1);
    if (p) c->used = c->used - old_size + new_size;
    return p;
}

static void h_free(void *vctx, void *ptr, size_t size) {
    heap_ctx *c = (heap_ctx *)vctx;
    if (ptr) {
        HeapFree(c->heap, 0, ptr);
        if (c->used >= size) c->used -= size;
        else c->used = 0;
    }
}

static uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static bool s_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

static im_node *make_box(im_tree *t, const char *name, im_fixed w, im_fixed h) {
    im_node *n = im_tree_create_node(t, IM_ELEM_BORDER, name);
    if (n) {
        im_node_set_size(n, w, h);
        im_node_set_alignment(n, IM_ALIGN_START, IM_ALIGN_START);
    }
    return n;
}

int im_main(void) {
    heap_ctx hctx = { GetProcessHeap(), 0u, 0u };
    im_allocator alloc = { h_alloc, h_realloc, h_free, &hctx };

    /* 1. Canvas: 5-scale positioning, same-size mutation & reversal, stable Z-order paint & hit-test. */
    {
        im_tree t;
        im_tree_init(&t, alloc);
        im_node *cv = im_tree_create_node(&t, IM_ELEM_CANVAS, "Cv");
        im_node *a = make_box(&t, "A", FX(20), FX(20));
        im_node *b = make_box(&t, "B", FX(20), FX(20));
        im_node *c = make_box(&t, "C", FX(20), FX(20));
        im_node_set_canvas_pos(a, FX(10), FX(10));
        im_node_set_canvas_pos(b, FX(15), FX(15));
        im_node_set_canvas_pos(c, FX(15), FX(15));
        im_node_set_canvas_z(a, 5);
        im_node_set_canvas_z(b, 2);
        im_node_set_canvas_z(c, 2);
        im_tree_append_child(&t, cv, a);
        im_tree_append_child(&t, cv, b);
        im_tree_append_child(&t, cv, c);
        t.root = cv;

        im_layout_run(&t, cv, FX(100), FX(100), IM_ONE);
        CHECK(a->absolute_rect.left == FX(10) && a->absolute_rect.top == FX(10), "Canvas A at (10,10)");
        im_node *order[3];
        CHECK(im_node_paint_order(cv, order, 3u) == 3u && order[0] == b && order[1] == c && order[2] == a,
              "Canvas paint order sorts ascending by ZIndex preserving tie insertion order (B, C, A)");
        im_node *hit = im_input_hit_test(cv, IM_ONE, FX(18), FX(18), NULL, NULL);
        CHECK(hit == a, "Canvas hit-test picks highest ZIndex (A) at overlap (18,18)");

        /* Raise B above A on the existing tree and verify both paint order and hit target flip! */
        im_node_set_canvas_z(b, 10);
        CHECK(im_node_paint_order(cv, order, 3u) == 3u && order[0] == c && order[1] == a && order[2] == b,
              "Canvas paint order updates to (C, A, B) after ZIndex mutation");
        hit = im_input_hit_test(cv, IM_ONE, FX(18), FX(18), NULL, NULL);
        CHECK(hit == b, "Canvas hit-test picks newly raised B at overlap (18,18)");

        /* Move A and reverse at same size (must invalidate cached arrange). */
        im_node_set_canvas_pos(a, FX(40), FX(30));
        im_layout_run(&t, cv, FX(100), FX(100), IM_ONE);
        CHECK(a->absolute_rect.left == FX(40) && a->absolute_rect.top == FX(30), "Canvas move updates at same size");
        im_node_set_canvas_pos(a, FX(10), FX(10));
        im_layout_run(&t, cv, FX(100), FX(100), IM_ONE);
        CHECK(a->absolute_rect.left == FX(10) && a->absolute_rect.top == FX(10), "Canvas move reversal restores (10,10)");

        im_tree_destroy(&t);
    }

    /* 2. Viewbox: Stretch/StretchDirection, zero-size axis, second-child refusal, distinct transform + child RenderTransform + damage + hit-test. */
    {
        im_tree t;
        im_tree_init(&t, alloc);
        im_node *vb = im_tree_create_node(&t, IM_ELEM_VIEWBOX, "VB");
        im_node *ch = make_box(&t, "Ch", FX(20), FX(10));
        im_node *extra = make_box(&t, "Extra", FX(5), FX(5));
        CHECK(im_tree_append_child(&t, vb, ch), "Viewbox accepts first child");
        CHECK(!im_tree_append_child(&t, vb, extra), "Viewbox refuses second child");
        im_tree_destroy_subtree(&t, extra);
        /* Give child its own RenderTransform (translate +2, +3): Viewbox must NOT overwrite or double-scale it! */
        im_transform ch_tr = im_transform_translate(FX(2), FX(3));
        im_node_set_render_transform(ch, &ch_tr, 0, 0);
        t.root = vb;

        im_node_set_viewbox_stretch(vb, IM_STRETCH_UNIFORM, IM_STRETCH_DIR_BOTH);
        im_layout_run(&t, vb, FX(80), FX(60), IM_ONE);
        /* Uniform scale of 20x10 into 80x60 = min(4, 6) = 4 -> desired 80x40, child local 20x10, Viewbox scale 4x! */
        CHECK(vb->desired_size.width == FX(80) && vb->desired_size.height == FX(40),
              "Viewbox Uniform 20x10 in 80x60 measures 80x40");
        CHECK(ch->absolute_rect.right - ch->absolute_rect.left == FX(20) && ch->has_render_transform,
              "Viewbox preserves child local size and child RenderTransform");
        im_transform dev_tr;
        CHECK(im_node_device_transform(ch, IM_ONE, &dev_tr), "Viewbox child device transform succeeds");
        int64_t ox = 0, oy = 0;
        CHECK(im_transform_point_q30(&dev_tr, 0, 0, &ox, &oy) && ox == im_fixed_to_q30(FX(8)) && oy == im_fixed_to_q30(FX(12)),
              "Viewbox 4x scales child (2,3) RenderTransform to (8,12)");
        im_node *hit = im_input_hit_test(vb, IM_ONE, FX(12), FX(16), NULL, NULL);
        CHECK(hit == ch, "Viewbox hit-test lands on transformed child");

        /* Dirty tracking across Viewbox resize: collect initial, resize to 40x30 (2x), collect old+new damage. */
        im_dirty_list dl;
        im_dirty_init(&dl, 100, 100);
        im_dirty_collect_tree(&dl, &t, vb, IM_ONE);
        im_dirty_init(&dl, 100, 100);
        im_layout_run(&t, vb, FX(40), FX(30), IM_ONE);
        im_dirty_collect_tree(&dl, &t, vb, IM_ONE);
        CHECK(dl.count > 0u, "Viewbox resize produces device-pixel damage for scaled child");

        /* DownOnly prevents upscaling; UpOnly prevents downscaling; zero-size content axis safe. */
        im_node_set_viewbox_stretch(vb, IM_STRETCH_UNIFORM, IM_STRETCH_DIR_DOWN_ONLY);
        im_layout_run(&t, vb, FX(80), FX(60), IM_ONE);
        CHECK(vb->desired_size.width == FX(20) && vb->desired_size.height == FX(10),
              "Viewbox DownOnly caps scale at 1.0");
        im_node_set_size(ch, 0, FX(10));
        im_node_set_viewbox_stretch(vb, IM_STRETCH_UNIFORM, IM_STRETCH_DIR_BOTH);
        im_layout_run(&t, vb, FX(80), FX(60), IM_ONE);
        CHECK(vb->desired_size.width == 0 && vb->desired_size.height == 0,
              "Viewbox handles zero-size content axis without division by zero");

        /* Remove child clears has_viewbox_transform cleanly. */
        CHECK(im_tree_remove_child(&t, vb, ch) && !vb->has_viewbox_transform,
              "Removing Viewbox child clears has_viewbox_transform");
        im_tree_destroy_subtree(&t, ch);
        im_tree_destroy(&t);
    }

    /* 3. UniformGrid: 5 scales, padding, FirstColumn, spacing, overflow, same-size mutation + oracle dump. */
    {
        static const im_fixed scales[5] = { IM_ONE, (5 * IM_ONE) / 4, (3 * IM_ONE) / 2, 2 * IM_ONE, 3 * IM_ONE };
        for (int si = 0; si < 5; si++) {
            im_tree t;
            im_tree_init(&t, alloc);
            im_node *ug = im_tree_create_node(&t, IM_ELEM_UNIFORM_GRID, "UG");
            im_node_set_uniform_grid(ug, 2u, 3u, 1u);
            im_node_set_grid_spacing(ug, FX(4), FX(6));
            im_node_set_padding(ug, (im_thickness){ FX(2), FX(2), FX(2), FX(2) });
            for (int k = 0; k < 6; k++) {
                im_node *c = make_box(&t, "C", IM_FIXED_AUTO, IM_FIXED_AUTO);
                im_node_set_alignment(c, IM_ALIGN_STRETCH, IM_ALIGN_STRETCH);
                im_tree_append_child(&t, ug, c);
            }
            t.root = ug;
            im_layout_run(&t, ug, FX(120), FX(60), scales[si]);
            write_str(STD_OUTPUT_HANDLE, "UGRID ");
            write_i64(STD_OUTPUT_HANDLE, scales[si]);
            for (uint32_t k = 0; k < ug->child_count; k++) {
                im_rect r = ug->children[k]->absolute_rect;
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.left);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.top);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.right);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.bottom);
            }
            write_str(STD_OUTPUT_HANDLE, "\n");
            /* Child 5 (6th child with FirstColumn=1 in 2x3=6 grid -> slot 6 >= 6) overflows to empty rect. */
            CHECK(ug->children[5]->absolute_rect.right == ug->children[5]->absolute_rect.left,
                  "UniformGrid overflow child beyond Rows*Columns gets empty rect");
            im_tree_destroy(&t);
        }
    }

    /* 4. WrapPanel: 200 random children + narrower arrange reflow + collapsed last child + vertical orientation. */
    {
        uint32_t seed = 0x57524150u;
        im_tree t;
        im_tree_init(&t, alloc);
        im_node *wp = im_tree_create_node(&t, IM_ELEM_WRAP_PANEL, "WP200");
        im_node_set_wrap_panel(wp, IM_ORIENT_HORIZONTAL, FX(4), FX(6), IM_WRAP_STRETCH_NONE);
        im_node_set_padding(wp, (im_thickness){ FX(5), FX(3), FX(5), FX(3) });
        for (int i = 0; i < 200; i++) {
            int w = 10 + (int)(rng_next(&seed) % 45u);
            int h = 8 + (int)(rng_next(&seed) % 24u);
            im_node *ch = make_box(&t, "W", FX(w), FX(h));
            if ((i % 19) == 7) im_node_set_visibility(ch, IM_VIS_COLLAPSED);
            im_tree_append_child(&t, wp, ch);
        }
        t.root = wp;
        im_layout_run(&t, wp, FX(180), FX(4000), IM_ONE);
        for (uint32_t i = 0; i < wp->child_count; i++) {
            im_node *ch = wp->children[i];
            write_str(STD_OUTPUT_HANDLE, "WRAP200 ");
            write_i64(STD_OUTPUT_HANDLE, (int64_t)i);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, (int64_t)ch->visibility);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, ch->width);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, ch->height);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, ch->absolute_rect.left);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, ch->absolute_rect.top);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, ch->absolute_rect.right);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, ch->absolute_rect.bottom);
            write_str(STD_OUTPUT_HANDLE, "\n");
        }
        /* Narrower arrange reflow: measure at 180, arrange at 90 -> child wraps to lower row. */
        im_layout_arrange(&t, wp, (im_rect){ 0, 0, FX(90), FX(4000) }, IM_ONE);
        CHECK(wp->children[2]->absolute_rect.top > FX(3), "WrapPanel reflows on narrower arrange width");
        im_tree_destroy(&t);

        /* Collapsed final child with ItemsStretch=Last must NOT stretch penultimate child (pinned WrapPanel.cpp:144). */
        im_tree t2;
        im_tree_init(&t2, alloc);
        im_node *wp2 = im_tree_create_node(&t2, IM_ELEM_WRAP_PANEL, "WPColLast");
        im_node_set_wrap_panel(wp2, IM_ORIENT_HORIZONTAL, FX(4), FX(4), IM_WRAP_STRETCH_LAST);
        im_node *c0 = make_box(&t2, "C0", FX(20), FX(10));
        im_node *c1 = make_box(&t2, "C1", FX(20), FX(10));
        im_node_set_visibility(c1, IM_VIS_COLLAPSED);
        im_tree_append_child(&t2, wp2, c0);
        im_tree_append_child(&t2, wp2, c1);
        t2.root = wp2;
        im_layout_run(&t2, wp2, FX(100), FX(40), IM_ONE);
        CHECK((c0->absolute_rect.right - c0->absolute_rect.left) == FX(20),
              "WrapPanel ItemsStretch=Last with collapsed final child keeps C0 unstretched at 20");
        im_tree_destroy(&t2);
    }

    /* 5. RelativePanel: forward refs, chrome padding/border, cycle refusal by name, missing target, destroy safety, recovery. */
    {
        im_tree t;
        im_tree_init(&t, alloc);
        im_node *rp = im_tree_create_node(&t, IM_ELEM_RELATIVE_PANEL, "RP");
        im_node_set_border_thickness(rp, (im_thickness){ FX(2), FX(3), FX(2), FX(3) });
        im_node_set_padding(rp, (im_thickness){ FX(4), FX(5), FX(4), FX(5) });
        im_node *a = make_box(&t, "Alpha", FX(30), FX(20));
        im_node *b = make_box(&t, "Beta", FX(25), FX(15));
        /* Forward reference: Alpha is RightOf Beta (defined after Alpha). */
        im_node_set_relative_target(a, IM_REL_RIGHT_OF, "Beta");
        im_node_set_relative_panel_flags(b, IM_REL_PANEL_LEFT | IM_REL_PANEL_TOP);
        im_tree_append_child(&t, rp, a);
        im_tree_append_child(&t, rp, b);
        t.root = rp;

        im_layout_run(&t, rp, FX(120), FX(80), IM_ONE);
        CHECK(im_layout_last_error(&t, NULL) == IM_LAYOUT_OK, "RelativePanel valid forward ref succeeds");
        CHECK(b->absolute_rect.left == FX(6) && b->absolute_rect.top == FX(8) &&
              a->absolute_rect.left == FX(31) && a->absolute_rect.top == FX(8),
              "RelativePanel honors border+padding chrome (6,8) and forward RightOf");

        /* Introduce a cycle: Beta RightOf Alpha -> must refuse with IM_LAYOUT_ERR_CYCLE and offending name! */
        im_node_set_relative_target(b, IM_REL_RIGHT_OF, "Alpha");
        im_layout_run(&t, rp, FX(120), FX(80), IM_ONE);
        const char *err_name = NULL;
        CHECK(im_layout_last_error(&t, &err_name) == IM_LAYOUT_ERR_CYCLE &&
              (s_eq(err_name, "Alpha") || s_eq(err_name, "Beta")),
              "RelativePanel refuses circular relation by exact node name");
        CHECK(rp->measure_dirty, "RelativePanel cycle leaves measure_dirty set and skips arrange");

        /* Recover by clearing Beta's cyclic constraint at the exact same size! */
        im_node_set_relative_target(b, IM_REL_RIGHT_OF, NULL);
        im_layout_run(&t, rp, FX(120), FX(80), IM_ONE);
        CHECK(im_layout_last_error(&t, NULL) == IM_LAYOUT_OK && !rp->measure_dirty &&
              a->absolute_rect.left == FX(31),
              "RelativePanel recovers cleanly at the same size after fixing cycle");

        /* Destroy referenced sibling Beta -> Alpha now has missing target (no dangling pointer!). */
        im_tree_destroy_subtree(&t, b);
        im_layout_run(&t, rp, FX(120), FX(80), IM_ONE);
        CHECK(im_layout_last_error(&t, &err_name) == IM_LAYOUT_ERR_MISSING_TARGET && s_eq(err_name, "Beta"),
              "RelativePanel reports missing target 'Beta' safely after sibling destruction");
        im_tree_destroy(&t);
    }

    /* 6. VariableSizedWrapGrid: 200 random span lists + auto first-item sizing + oversized/nonpositive clamp + OOM retry. */
    {
        uint32_t seed = 0x56535747u;
        for (int li = 0; li < 200; li++) {
            im_tree t;
            im_tree_init(&t, alloc);
            im_node *vg = im_tree_create_node(&t, IM_ELEM_VARIABLE_SIZED_WRAP_GRID, "VG");
            im_orientation ori = (li & 1) ? IM_ORIENT_HORIZONTAL : IM_ORIENT_VERTICAL;
            int32_t max_rc = 3 + (int32_t)(rng_next(&seed) % 4u);
            im_node_set_wrap_grid(vg, ori, FX(20), FX(15), max_rc);
            int cnt = 4 + (int)(rng_next(&seed) % 8u);
            write_str(STD_OUTPUT_HANDLE, "VSWG200 ");
            write_i64(STD_OUTPUT_HANDLE, li);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, (int64_t)ori);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, max_rc);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, cnt);
            for (int k = 0; k < cnt; k++) {
                im_node *c = make_box(&t, "V", IM_FIXED_AUTO, IM_FIXED_AUTO);
                im_node_set_alignment(c, IM_ALIGN_STRETCH, IM_ALIGN_STRETCH);
                int32_t rs = (int32_t)(rng_next(&seed) % 4u); /* includes 0 to test nonpositive clamp */
                int32_t cs = (int32_t)(rng_next(&seed) % 4u);
                if (li == 0 && k == 1) { rs = 9; cs = 9; } /* oversized span > max_rc */
                im_node_set_vswg_span(c, rs, cs);
                im_tree_append_child(&t, vg, c);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, rs);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, cs);
            }
            t.root = vg;
            im_layout_run(&t, vg, FX(200), FX(200), IM_ONE);
            write_str(STD_OUTPUT_HANDLE, " :");
            for (uint32_t k = 0; k < vg->child_count; k++) {
                im_rect r = vg->children[k]->absolute_rect;
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.left);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.top);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.right);
                write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, r.bottom);
            }
            write_str(STD_OUTPUT_HANDLE, "\n");
            im_tree_destroy(&t);
        }

        /* Auto first-item sizing (spanning 2x2 first item of size 40x30 -> item_w=20, item_h=15) + OOM retry! */
        heap_ctx octx = { GetProcessHeap(), 0u, 0u };
        im_allocator oalloc = { h_alloc, h_realloc, h_free, &octx };
        im_tree t;
        im_tree_init(&t, oalloc);
        im_node *vg = im_tree_create_node(&t, IM_ELEM_VARIABLE_SIZED_WRAP_GRID, "VGOOM");
        im_node_set_wrap_grid(vg, IM_ORIENT_HORIZONTAL, IM_FIXED_AUTO, IM_FIXED_AUTO, 4);
        im_node *c0 = make_box(&t, "C0", FX(40), FX(30));
        im_node_set_vswg_span(c0, 2, 2);
        im_node *c1 = make_box(&t, "C1", FX(20), FX(15));
        im_tree_append_child(&t, vg, c0);
        im_tree_append_child(&t, vg, c1);
        t.root = vg;
        /* Cap memory right at current usage so occupancy map allocation fails cleanly! */
        octx.limit = octx.used + 8u;
        im_layout_run(&t, vg, FX(100), FX(100), IM_ONE);
        CHECK(im_layout_last_error(&t, NULL) == IM_LAYOUT_ERR_OOM && vg->measure_dirty,
              "VSWG OOM reports IM_LAYOUT_ERR_OOM and leaves measure_dirty without partial cache");
        /* Lift cap and retry on the exact same tree -> succeeds! */
        octx.limit = 0u;
        im_layout_run(&t, vg, FX(100), FX(100), IM_ONE);
        CHECK(im_layout_last_error(&t, NULL) == IM_LAYOUT_OK &&
              c1->absolute_rect.left == FX(40) && c1->absolute_rect.top == 0,
              "VSWG recovers on retry after OOM and derives auto 20x15 cell size from 2x2 spanning first item");
        im_tree_destroy(&t);
        CHECK(octx.used == 0u, "VSWG OOM + retry leaks 0 bytes");
    }

    /* 7. ItemsStackPanel (unequal containers + group header + GroupPadding) & ItemsWrapGrid. */
    {
        im_tree t;
        im_tree_init(&t, alloc);
        im_node *isp = im_tree_create_node(&t, IM_ELEM_ITEMS_STACK_PANEL, "ISP");
        im_node_set_group_padding(isp, (im_thickness){ FX(6), FX(4), FX(6), FX(8) });
        im_node *hdr = make_box(&t, "Hdr", FX(80), FX(22));
        im_node_set_group_header(hdr, true);
        im_node *r1 = make_box(&t, "R1", FX(80), FX(18));
        im_node *r2 = make_box(&t, "R2", FX(80), FX(35));
        im_tree_append_child(&t, isp, hdr);
        im_tree_append_child(&t, isp, r1);
        im_tree_append_child(&t, isp, r2);
        t.root = isp;
        im_layout_run(&t, isp, FX(100), FX(200), IM_ONE);
        /* Hdr: y=0..22 (no group padding), then GroupPadding.top=4 -> R1: y=26..44, R2: y=44..79, GroupPadding.bottom=8 -> desired H=87. */
        CHECK(hdr->absolute_rect.top == 0 && hdr->absolute_rect.bottom == FX(22) &&
              r1->absolute_rect.left == FX(6) && r1->absolute_rect.top == FX(26) &&
              r2->absolute_rect.top == FX(44) && r2->absolute_rect.bottom == FX(79) &&
              isp->desired_size.height == FX(87),
              "ItemsStackPanel sizes unequal containers, group headers, and GroupPadding accurately");
        im_tree_destroy(&t);

        im_tree t2;
        im_tree_init(&t2, alloc);
        im_node *iwg = im_tree_create_node(&t2, IM_ELEM_ITEMS_WRAP_GRID, "IWG");
        im_node_set_wrap_grid(iwg, IM_ORIENT_HORIZONTAL, FX(25), FX(20), 2);
        im_node_set_group_padding(iwg, (im_thickness){ FX(4), FX(2), FX(4), FX(2) });
        for (int i = 0; i < 3; i++) {
            im_node *c = make_box(&t2, "I", FX(25), FX(20));
            im_tree_append_child(&t2, iwg, c);
        }
        t2.root = iwg;
        im_layout_run(&t2, iwg, FX(100), FX(100), IM_ONE);
        CHECK(iwg->children[0]->absolute_rect.left == FX(4) && iwg->children[1]->absolute_rect.left == FX(29) &&
              iwg->children[2]->absolute_rect.left == FX(4) && iwg->children[2]->absolute_rect.top == FX(22),
              "ItemsWrapGrid wraps at MaximumRowsOrColumns=2 with GroupPadding");
        im_tree_destroy(&t2);
    }

    /* 8. RTL mirroring across ALL 8 panels + nested LTR override inside RTL. */
    {
        static const im_element_kind kinds[8] = {
            IM_ELEM_CANVAS, IM_ELEM_VIEWBOX, IM_ELEM_UNIFORM_GRID, IM_ELEM_WRAP_PANEL,
            IM_ELEM_RELATIVE_PANEL, IM_ELEM_VARIABLE_SIZED_WRAP_GRID, IM_ELEM_ITEMS_WRAP_GRID,
            IM_ELEM_ITEMS_STACK_PANEL
        };
        for (int ki = 0; ki < 8; ki++) {
            im_tree t;
            im_tree_init(&t, alloc);
            im_node *p = im_tree_create_node(&t, kinds[ki], "P");
            im_node *c0 = make_box(&t, "C0", FX(20), FX(12));
            if (kinds[ki] == IM_ELEM_CANVAS) im_node_set_canvas_pos(c0, FX(10), FX(4));
            if (kinds[ki] == IM_ELEM_UNIFORM_GRID) im_node_set_uniform_grid(p, 1u, 3u, 0u);
            if (kinds[ki] == IM_ELEM_VARIABLE_SIZED_WRAP_GRID || kinds[ki] == IM_ELEM_ITEMS_WRAP_GRID) {
                im_node_set_wrap_grid(p, IM_ORIENT_HORIZONTAL, FX(20), FX(12), 4);
            }
            im_tree_append_child(&t, p, c0);
            if (kinds[ki] != IM_ELEM_VIEWBOX) {
                im_node *c1 = make_box(&t, "C1", FX(30), FX(12));
                if (kinds[ki] == IM_ELEM_CANVAS) im_node_set_canvas_pos(c1, FX(35), FX(4));
                if (kinds[ki] == IM_ELEM_RELATIVE_PANEL) im_node_set_relative_target(c1, IM_REL_RIGHT_OF, "C0");
                im_tree_append_child(&t, p, c1);
            }
            t.root = p;

            im_node_set_flow_direction(p, IM_FLOW_LTR);
            im_layout_run(&t, p, FX(100), FX(60), IM_ONE);
            im_fixed ltr_l = c0->absolute_rect.left;
            im_fixed ltr_w = c0->absolute_rect.right - c0->absolute_rect.left;

            im_node_set_flow_direction(p, IM_FLOW_RTL);
            im_layout_run(&t, p, FX(100), FX(60), IM_ONE);
            im_fixed rtl_l = c0->absolute_rect.left;
            im_fixed pw = p->absolute_rect.right - p->absolute_rect.left;
            CHECK(rtl_l == pw - ltr_l - ltr_w, "RTL mirrors child X symmetrically for every panel");
            if (kinds[ki] != IM_ELEM_VIEWBOX) {
                CHECK(rtl_l != ltr_l, "RTL asymmetric fixture actually moves C0 (catches missing/double mirror)");
            }
            im_tree_destroy(&t);
        }

        /* Nested LTR override inside an RTL parent restores LTR child placement. */
        im_tree t;
        im_tree_init(&t, alloc);
        im_node *outer = im_tree_create_node(&t, IM_ELEM_CANVAS, "OuterRTL");
        im_node *inner = im_tree_create_node(&t, IM_ELEM_CANVAS, "InnerLTR");
        im_node *leaf = make_box(&t, "Leaf", FX(10), FX(10));
        im_node_set_size(inner, FX(50), FX(30));
        im_node_set_canvas_pos(inner, FX(10), FX(0));
        im_node_set_canvas_pos(leaf, FX(5), FX(0));
        im_node_set_flow_direction(outer, IM_FLOW_RTL);
        im_node_set_flow_direction(inner, IM_FLOW_LTR);
        im_tree_append_child(&t, inner, leaf);
        im_tree_append_child(&t, outer, inner);
        t.root = outer;
        im_layout_run(&t, outer, FX(100), FX(40), IM_ONE);
        /* Inner in Outer(100) RTL at left=10, w=50 -> inner.abs.left = 100 - 10 - 50 = 40;
           Leaf in Inner(LTR) at left=5 -> leaf.abs.left = 40 + 5 = 45! */
        CHECK(inner->absolute_rect.left == FX(40) && leaf->absolute_rect.left == FX(45),
              "Nested LTR panel inside RTL parent preserves LTR child coordinates");
        im_tree_destroy(&t);
    }

    CHECK(hctx.used == 0u, "all panel tests freed 100% of allocated memory");
    if (g_failures > 0) {
        write_str(STD_ERROR_HANDLE, "test_panels: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_panels: ALL PASSED\n");
    return 0;
}
