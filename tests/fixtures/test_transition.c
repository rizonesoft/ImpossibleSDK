/* Tier-0 freestanding verification fixture for ui/transition.{h,c} (D01 T01 s15). */
#include <impossible/base.h>
#include <windows.h>
#include "dirty.h"
#include "input.h"
#include "layout.h"
#include "transition.h"
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
    char buf[32], out[32];
    int n = 0;
    uint64_t u;
    if (v < 0) { write_str(handle_id, "-"); u = (uint64_t)(-(v + 1)) + 1u; }
    else u = (uint64_t)v;
    if (u == 0) buf[n++] = '0';
    else while (u > 0) { buf[n++] = (char)('0' + (u % 10u)); u /= 10u; }
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
    size_t used;
} heap_ctx;

static void *h_alloc(void *vctx, size_t size) {
    heap_ctx *c = (heap_ctx *)vctx;
    void *p = HeapAlloc(c->heap, 0, size ? size : 1);
    if (p) c->used += size;
    return p;
}

static void *h_realloc(void *vctx, void *ptr, size_t old_size, size_t new_size) {
    heap_ctx *c = (heap_ctx *)vctx;
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

static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

int im_main(void) {
    heap_ctx hctx = { GetProcessHeap(), 0u };
    im_allocator alloc = { h_alloc, h_realloc, h_free, &hctx };

    /* 1. Lookup by name and kind; unknown name returns NULL. */
    CHECK(im_transition_spec_by_name("EntranceThemeTransition") != NULL, "EntranceThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("PopupThemeTransition") != NULL, "PopupThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("AddDeleteThemeTransition") != NULL, "AddDeleteThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("RepositionThemeTransition") != NULL, "RepositionThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("ReorderThemeTransition") != NULL, "ReorderThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("ContentThemeTransition") != NULL, "ContentThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("NavigationThemeTransition") != NULL, "NavigationThemeTransition lookup succeeds");
    CHECK(im_transition_spec_by_name("BogusThemeTransition") == NULL, "Unknown ThemeTransition name returns NULL");

    /* 2. Dump all 10 named transition kinds at 10 instants each for independent double-precision PVL oracle verification! */
    for (int ki = 0; ki < IM_TRANS_KIND_COUNT; ki++) {
        im_transition tr;
        if (ki == IM_TRANS_REPOSITION || ki == IM_TRANS_REORDER) {
            im_transition_start_offset(&tr, (im_transition_kind)ki, FX(80), FX(-40), 0, 0, 1000u, 0u, false);
        } else {
            im_transition_start(&tr, (im_transition_kind)ki, 1000u, 0u, false);
        }
        uint32_t dur = tr.move_duration_ms > tr.opacity_duration_ms ? tr.move_duration_ms : tr.opacity_duration_ms;
        for (uint32_t step = 0; step < 10u; step++) {
            uint32_t dt = (dur * step) / 9u;
            im_transition_sample s = im_transition_sample_at(&tr, 1000u + dt);
            write_str(STD_OUTPUT_HANDLE, "TRANS ");
            write_i64(STD_OUTPUT_HANDLE, ki);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, dt);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, s.translate_x);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, s.translate_y);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, s.scale);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, s.opacity);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, s.origin_x);
            write_str(STD_OUTPUT_HANDLE, " "); write_i64(STD_OUTPUT_HANDLE, s.origin_y);
            write_str(STD_OUTPUT_HANDLE, "\n");
        }
    }

    /* 3. 500 random mid-flight interruptions & reversals: zero discontinuity at interrupt instant + bounded 1-frame step! */
    {
        im_transition tr;
        im_transition_start_offset(&tr, IM_TRANS_REPOSITION, FX(0), FX(0), FX(100), FX(0), 0u, 0u, false);
        uint32_t seed = 0x72A45115u;
        uint32_t t = 0u;
        im_transition_sample prev = im_transition_sample_at(&tr, t);
        /* Max 1-frame (16ms) PVL displacement for 100-DIP span is < 28 DIPs (initial spline slope ~2.8x linear). */
        int64_t max_frame_step = FX(30);
        for (int i = 0; i < 500; i++) {
            uint32_t dt = 8u + (rng_next(&seed) % 9u); /* 8..16 ms per frame */
            t += dt;
            im_transition_sample cur = im_transition_sample_at(&tr, t);
            CHECK(abs64((int64_t)cur.translate_x - (int64_t)prev.translate_x) <= max_frame_step,
                  "Random transition frame step stays bounded within single-frame distance");
            if ((rng_next(&seed) & 3u) == 0u) {
                im_fixed next_x = FX((int32_t)(rng_next(&seed) % 101u));
                if ((rng_next(&seed) & 1u) == 0u) {
                    im_transition_reverse(&tr, t);
                } else {
                    im_transition_retarget(&tr, next_x, 0, IM_ONE, IM_ONE, t);
                }
                im_transition_sample after = im_transition_sample_at(&tr, t);
                CHECK(after.translate_x == cur.translate_x && after.opacity == cur.opacity,
                      "Mid-flight interruption/reversal has zero discontinuity at interrupt timestamp");
                prev = after;
            } else {
                prev = cur;
            }
        }
        /* Reversal must progress toward new endpoint and settle cleanly to active==false. */
        im_transition_start_offset(&tr, IM_TRANS_REPOSITION, FX(0), FX(0), FX(100), FX(0), 10000u, 0u, false);
        im_transition_sample mid = im_transition_sample_at(&tr, 10080u);
        im_transition_reverse(&tr, 10080u);
        im_transition_sample rev_at = im_transition_sample_at(&tr, 10080u);
        im_transition_sample rev_later = im_transition_sample_at(&tr, 10160u);
        im_transition_sample rev_end = im_transition_sample_at(&tr, 10500u);
        CHECK(rev_at.translate_x == mid.translate_x && rev_later.translate_x < rev_at.translate_x &&
              rev_end.translate_x == 0 && !rev_end.active && !im_transition_is_active(&tr, 10500u),
              "Reversed transition preserves continuity, moves back toward 0, and settles inactive");
    }

    /* 4. Reduced motion: first frame IS the last, live toggle snaps active transition, re-enabling leaves settled settled! */
    {
        im_transition tr;
        im_transition_start(&tr, IM_TRANS_ENTRANCE, 500u, 2u, true);
        im_transition_sample first = im_transition_sample_at(&tr, 500u);
        CHECK(!first.active && first.translate_x == 0 && first.translate_y == 0 && first.opacity == IM_ONE,
              "Reduced motion starts settled so the first frame is the last");

        im_transition tr2;
        im_transition_start(&tr2, IM_TRANS_ENTRANCE, 1000u, 0u, false);
        CHECK(im_transition_is_active(&tr2, 1050u), "Normal entrance transition is active at 50ms");
        CHECK(im_transition_set_reduced_motion(&tr2, true, 1050u), "Live reduced-motion toggle snaps active transition");
        im_transition_sample snapped = im_transition_sample_at(&tr2, 1050u);
        CHECK(!snapped.active && snapped.translate_x == 0 && snapped.opacity == IM_ONE,
              "Snapped transition immediately reaches final position and opacity");
        CHECK(!im_transition_set_reduced_motion(&tr2, false, 1060u) && !im_transition_is_active(&tr2, 1060u),
              "Toggling reduced motion back off leaves settled transition settled");
    }

    /* 5. Connected node collections, authored RenderTransform+Opacity preservation, and detached exit-ghost ownership! */
    {
        im_tree t; im_tree_init(&t, alloc);
        im_node *list = im_tree_create_node(&t, IM_ELEM_STACK_PANEL, "List");
        im_transition_decl d_ent = { IM_ELEM_ENTRANCE_THEME_TRANSITION, FX(60), FX(10), true, true, true, 0 };
        im_transition_decl d_add = { IM_ELEM_ADD_DELETE_THEME_TRANSITION, 0, 0, false, false, false, 0 };
        im_transition_decl d_rep = { IM_ELEM_REPOSITION_THEME_TRANSITION, 0, 0, false, false, true, 0 };
        im_transition_decl d_reo = { IM_ELEM_REORDER_THEME_TRANSITION, 0, 0, false, false, false, 0 };
        im_transition_decl d_pop = { IM_ELEM_POPUP_THEME_TRANSITION, 0, FX(32), false, true, false, 0 };
        im_transition_decl d_cnt = { IM_ELEM_CONTENT_THEME_TRANSITION, FX(24), 0, true, false, false, 0 };
        im_transition_decl d_nav = { IM_ELEM_NAVIGATION_THEME_TRANSITION, 0, 0, false, false, false, IM_ELEM_DRILL_IN_NAV_INFO };
        CHECK(im_node_add_transition_decl(list, &d_ent) && im_node_add_transition_decl(list, &d_add) &&
              im_node_add_transition_decl(list, &d_rep) && im_node_add_transition_decl(list, &d_reo) &&
              im_node_add_transition_decl(list, &d_pop) && im_node_add_transition_decl(list, &d_cnt) &&
              im_node_add_transition_decl(list, &d_nav),
              "All 7 named transition declarations attach cleanly to owning node");

        im_node *item = im_tree_create_node(&t, IM_ELEM_BORDER, "Item0");
        im_node_set_size(item, FX(40), FX(20));
        im_node_set_alignment(item, IM_ALIGN_START, IM_ALIGN_START);
        im_transform authored = im_transform_translate(FX(5), FX(0));
        im_node_set_render_transform(item, &authored, 0, 0);
        item->opacity = IM_ONE / 2; /* Authored 0.5 opacity */
        im_tree_append_child(&t, list, item);
        t.root = list;
        im_layout_run(&t, list, FX(200), FX(100), IM_ONE);

        /* Trigger entrance transition: composes with authored translate(+5, 0) & opacity 0.5 without overwriting them! */
        im_transition tr;
        CHECK(im_transition_on_enter(list, &tr, 0u, 1u, false) && tr.delay_ms == 33u && tr.from_x == FX(60),
              "im_transition_on_enter uses per-declaration FromHorizontalOffset=60 and stagger delay=33ms");
        CHECK(im_transition_apply_to_node(&tr, item, 33u), "Transition active at start of stagger window");
        CHECK(item->has_render_transform && item->opacity == IM_ONE / 2 && im_node_effective_opacity(item) == 0,
              "Active transition preserves authored RenderTransform and Opacity while composing effective opacity");
        im_transform dev_tr;
        int64_t ox = 0, oy = 0;
        CHECK(im_node_device_transform(item, IM_ONE, &dev_tr) && im_transform_point_q30(&dev_tr, 0, 0, &ox, &oy) &&
              ox == im_fixed_to_q30(FX(65)),
              "Device transform composes authored translate(+5) + transition translate(+60) = +65");
        /* Settle transition -> clears transition_transform/opacity and restores pure authored +5 and 0.5! */
        CHECK(!im_transition_apply_to_node(&tr, item, 1000u) && !item->has_transition_transform &&
              im_node_effective_opacity(item) == IM_ONE / 2 && item->has_render_transform,
              "Settled transition clears presentation state and restores pure authored transform and opacity");

        /* Popup, Content, Navigation DrillIn/DrillOut, Reposition, Reorder triggers from owning declarations. */
        im_transition tr_pop, tr_cnt, tr_nav_in, tr_nav_out, tr_rep, tr_reo;
        CHECK(im_transition_on_popup_open(list, &tr_pop, 0u, false) && tr_pop.from_y == FX(32), "Popup uses declared FromVerticalOffset");
        CHECK(im_transition_on_content_change(list, &tr_cnt, 0u, false) && tr_cnt.from_x == FX(24), "Content uses declared HorizontalOffset");
        CHECK(im_transition_on_navigate(list, &tr_nav_in, false, 0u, false) && tr_nav_in.kind == IM_TRANS_DRILL_IN &&
              tr_nav_in.origin_x == IM_ONE / 2 && tr_nav_in.origin_y == IM_ONE / 2,
              "Forward DrillIn navigation scales around element center (0.5, 0.5)");
        CHECK(im_transition_on_navigate(list, &tr_nav_out, true, 0u, false) && tr_nav_out.kind == IM_TRANS_DRILL_OUT,
              "Back navigation selects DrillOut");
        CHECK(im_transition_on_reposition(list, &tr_rep, (im_rect){0, 0, FX(40), FX(20)}, (im_rect){FX(50), 0, FX(90), FX(20)}, 0u, 1u, false) &&
              tr_rep.from_x == FX(-50), "Reposition computes delta old - new");
        CHECK(im_transition_on_reorder(list, &tr_reo, (im_rect){0, FX(20), FX(40), FX(40)}, (im_rect){0, 0, FX(40), FX(20)}, 0u, false) &&
              tr_reo.from_y == FX(20), "Reorder computes delta old - new");

        /* Outgoing delete visual: snapshot into ghosts, IMMEDIATELY destroy item subtree, step ghost & verify hit-test + dirty cleanup! */
        im_transition_ghosts ghosts;
        for (size_t gi = 0; gi < sizeof(ghosts); gi++) ((uint8_t *)&ghosts)[gi] = 0u;
        CHECK(im_transition_on_delete(list, &ghosts, item, 42u, 0xFF336699u, 0u, false), "Delete snapshots outgoing visual");
        im_tree_destroy_subtree(&t, item); /* Immediate destruction while exit animation is active! */
        CHECK(im_input_hit_test(list, IM_ONE, FX(10), FX(10), NULL, NULL) == list,
              "Destroyed outgoing item stops participating in hit-testing immediately");
        im_dirty_list dl;
        im_dirty_init(&dl, 200, 100);
        CHECK(im_transition_ghosts_step(&ghosts, &dl, IM_ONE, 50u) && dl.count == 1u,
              "Active exit ghost steps safely after node destruction and emits dirty rect");
        /* Cancel ghost (e.g. reinsertion) emits cleanup dirty rect and deactivates cleanly! */
        im_dirty_init(&dl, 200, 100);
        CHECK(im_transition_ghosts_cancel(&ghosts, 42u, &dl) && dl.count == 1u &&
              !im_transition_ghosts_step(&ghosts, &dl, IM_ONE, 80u),
              "Cancelling exit ghost emits cleanup dirty rect and clears active slot");
        im_tree_destroy(&t);
    }

    CHECK(hctx.used == 0u, "all theme transition tests freed 100% of allocated memory");
    if (g_failures > 0) {
        write_str(STD_ERROR_HANDLE, "test_transition: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_transition: ALL PASSED\n");
    return 0;
}
