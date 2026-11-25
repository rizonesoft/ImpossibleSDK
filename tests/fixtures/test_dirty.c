/* Tests for D01 T01 s9: dirty rectangles, frame scheduler, and Windows partial WM_PAINT. */
#include <impossible/base.h>
#include <impossible/win_host.h>
#include <impossible/window.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "animate.h"
#include "button.h"
#include "dirty.h"
#include "draw.h"
#include "frame.h"
#include "host.h"
#include "layer.h"
#include "layout.h"
#include "states.h"
#include "theme.h"
#include "tree.h"

#define FX(x) ((im_fixed)((x) * IM_ONE))

static int g_failures = 0;

static void write_str(DWORD std_handle, const char *s) {
    DWORD len = 0;
    while (s[len]) len++;
    DWORD written = 0;
    WriteFile(GetStdHandle(std_handle), s, len, &written, NULL);
}

static void fail_msg(const char *msg) {
    write_str(STD_ERROR_HANDLE, "FAIL: ");
    write_str(STD_ERROR_HANDLE, msg);
    write_str(STD_ERROR_HANDLE, "\n");
    g_failures++;
}

#define CHECK(cond, msg) do { \
    if (!(cond)) fail_msg(msg); \
} while (0)

static bool bytes_eq(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return false;
    }
    return true;
}

static void *heap_alloc_zero(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

static void heap_free_ptr(void *ptr) {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

typedef struct test_alloc_ctx {
    bool fail_next;
} test_alloc_ctx;

static void *test_alloc_fn(void *ctx, size_t size) {
    test_alloc_ctx *a = (test_alloc_ctx *)ctx;
    if (a && a->fail_next) {
        a->fail_next = false;
        return NULL;
    }
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

static void *test_realloc_fn(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    (void)old_size;
    test_alloc_ctx *a = (test_alloc_ctx *)ctx;
    if (a && a->fail_next) {
        a->fail_next = false;
        return NULL;
    }
    if (!ptr) return HeapAlloc(GetProcessHeap(), 0, new_size ? new_size : 1);
    return HeapReAlloc(GetProcessHeap(), 0, ptr, new_size ? new_size : 1);
}

static void test_free_fn(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

static im_palette make_dark_palette(void) {
    im_palette p;
    memset(&p, 0, sizeof(p));
    p.theme = IM_DARK;
    return p;
}

static uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t color_for_id(uint32_t id) {
    static const uint32_t palette[8] = {
        0xFFE53935u, 0xFF1E88E5u, 0xFF43A047u, 0xFFFB8C00u,
        0xFF8E24AAu, 0xFF00ACC1u, 0xFFFDD835u, 0xFFD81B60u
    };
    return palette[id & 7u];
}

typedef struct leaf_table {
    im_node *nodes[12];
    uint32_t colors[12];
} leaf_table;

static uint32_t lookup_leaf_color(const leaf_table *tab, const im_node *node) {
    for (int i = 0; i < 12; i++) {
        if (tab->nodes[i] == node) return tab->colors[i];
    }
    return 0;
}

static void render_node_recursive(im_surface *surf, const leaf_table *tab, const im_node *node) {
    if (!node || node->visibility == IM_VIS_COLLAPSED) return;
    if (node->kind == IM_ELEM_BORDER && node->visibility == IM_VIS_VISIBLE) {
        uint32_t argb = lookup_leaf_color(tab, node);
        if (argb != 0) {
            im_radii r = {0, 0, 0, 0};
            im_fill_argb(surf, node->absolute_rect, r, argb);
        }
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        render_node_recursive(surf, tab, node->children[i]);
    }
}

static void render_full(im_surface *surf, const leaf_table *tab, const im_node *root, uint32_t bg) {
    im_rect full = {0, 0, FX(surf->width), FX(surf->height)};
    im_radii r = {0, 0, 0, 0};
    im_fill_argb(surf, full, r, bg);
    render_node_recursive(surf, tab, root);
}

static void render_partial(im_surface *surf, im_layer_ctx *layers, const leaf_table *tab,
                           const im_node *root, const im_dirty_list *dirty, uint32_t bg) {
    im_rect full = {0, 0, FX(surf->width), FX(surf->height)};
    im_radii r = {0, 0, 0, 0};
    for (uint32_t i = 0; i < dirty->count; i++) {
        if (!im_dirty_push_clip(layers, dirty->rects[i])) {
            render_full(surf, tab, root, bg);
            return;
        }
        im_fill_argb(surf, full, r, bg);
        render_node_recursive(surf, tab, root);
        im_layer_pop_clip(layers);
    }
}

static im_node *make_leaf(im_tree *tree, uint32_t *rng) {
    im_node *n = im_tree_create_node(tree, IM_ELEM_BORDER, NULL);
    n->h_align = IM_ALIGN_START;
    n->v_align = IM_ALIGN_START;
    n->width = FX(10 + (int)(rng_next(rng) % 28));
    n->height = FX(8 + (int)(rng_next(rng) % 24));
    n->margin = (im_thickness){FX((int)(rng_next(rng) % 72)), FX((int)(rng_next(rng) % 52)), 0, 0};
    return n;
}

/* 1. 10,000 random tree mutations: partial redraw vs full redraw byte-for-byte identical. */
static void test_random_tree_mutations(void) {
    const int W = 128, H = 96;
    const uint32_t BG = 0xFF181A20u;
    uint32_t *pix_part = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    uint32_t *pix_full = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    CHECK(pix_part && pix_full, "alloc framebuffers");
    if (!pix_part || !pix_full) return;

    test_alloc_ctx actx = {false};
    im_allocator alloc = {test_alloc_fn, test_realloc_fn, test_free_fn, &actx};
    im_palette pal = make_dark_palette();

    im_surface s_part = {pix_part, W, H, W, pal, IM_ONE, NULL};
    im_surface s_full = {pix_full, W, H, W, pal, IM_ONE, NULL};
    im_layer_ctx layers_part;
    im_layer_ctx layers_full;
    CHECK(im_layer_init(&layers_part, &s_part, alloc, 1024 * 1024), "init part layers");
    CHECK(im_layer_init(&layers_full, &s_full, alloc, 1024 * 1024), "init full layers");

    im_tree tree;
    im_tree_init(&tree, alloc);
    im_node *root = im_tree_create_node(&tree, IM_ELEM_GRID, "root");
    im_node *group_a = im_tree_create_node(&tree, IM_ELEM_GRID, "group_a");
    im_node *group_b = im_tree_create_node(&tree, IM_ELEM_GRID, "group_b");
    group_a->h_align = IM_ALIGN_START;
    group_a->v_align = IM_ALIGN_START;
    group_b->h_align = IM_ALIGN_START;
    group_b->v_align = IM_ALIGN_START;
    im_tree_append_child(&tree, root, group_a);
    im_tree_append_child(&tree, root, group_b);

    leaf_table tab;
    memset(&tab, 0, sizeof(tab));
    uint32_t rng = 0x5EED1234u;

    for (int i = 0; i < 6; i++) {
        im_node *n = make_leaf(&tree, &rng);
        im_tree_append_child(&tree, (i & 1) ? group_a : group_b, n);
        tab.nodes[i] = n;
        tab.colors[i] = color_for_id((uint32_t)i);
    }

    im_dirty_list dirty;
    im_dirty_init(&dirty, W, H);
    im_layout_run(&tree, root, FX(W), FX(H), IM_ONE);
    im_dirty_collect_tree(&dirty, &tree, root, IM_ONE);
    render_full(&s_part, &tab, root, BG);
    render_full(&s_full, &tab, root, BG);
    im_dirty_clear(&dirty);

    int mismatches = 0;
    for (int iter = 0; iter < 10000; iter++) {
        uint32_t op = rng_next(&rng) % 8u;
        int idx = (int)(rng_next(&rng) % 12u);

        if (op == 0) {
            if (!tab.nodes[idx]) {
                im_node *n = make_leaf(&tree, &rng);
                im_tree_append_child(&tree, (rng_next(&rng) & 1u) ? group_a : group_b, n);
                tab.nodes[idx] = n;
                tab.colors[idx] = color_for_id(rng_next(&rng));
            }
        } else if (op == 1) {
            if (tab.nodes[idx]) {
                im_tree_destroy_subtree(&tree, tab.nodes[idx]);
                tab.nodes[idx] = NULL;
                tab.colors[idx] = 0;
            }
        } else if (op == 2) {
            if (tab.nodes[idx]) {
                im_node *old_n = tab.nodes[idx];
                im_node *parent = old_n->parent;
                uint32_t cidx = 0;
                bool found = false;
                for (uint32_t k = 0; k < parent->child_count; k++) {
                    if (parent->children[k] == old_n) {
                        cidx = k;
                        found = true;
                        break;
                    }
                }
                if (found) {
                    im_node *new_n = make_leaf(&tree, &rng);
                    im_node *removed = im_tree_replace_child(&tree, parent, cidx, new_n);
                    if (removed) {
                        im_tree_destroy_subtree(&tree, removed);
                        tab.nodes[idx] = new_n;
                        tab.colors[idx] = color_for_id(rng_next(&rng));
                    } else {
                        im_tree_destroy_subtree(&tree, new_n);
                    }
                }
            }
        } else if (op == 3) {
            im_node *g = (rng_next(&rng) & 1u) ? group_a : group_b;
            im_node_set_margin(g, (im_thickness){FX((int)(rng_next(&rng) % 24)), FX((int)(rng_next(&rng) % 16)), 0, 0});
        } else if (op == 4) {
            if (tab.nodes[idx]) {
                im_node_set_size(tab.nodes[idx], FX(8 + (int)(rng_next(&rng) % 30)), FX(8 + (int)(rng_next(&rng) % 24)));
                im_node_set_margin(tab.nodes[idx], (im_thickness){FX((int)(rng_next(&rng) % 72)), FX((int)(rng_next(&rng) % 52)), 0, 0});
            }
        } else if (op == 5) {
            im_node *target = ((rng_next(&rng) % 4u) == 0u)
                ? ((rng_next(&rng) & 1u) ? group_a : group_b)
                : tab.nodes[idx];
            if (target) {
                target->visibility = (target->visibility == IM_VIS_COLLAPSED)
                    ? IM_VIS_VISIBLE : IM_VIS_COLLAPSED;
                im_tree_invalidate_measure(target);
            }
        } else if (op == 6) {
            if (tab.nodes[idx]) {
                tab.colors[idx] = color_for_id(rng_next(&rng));
                im_tree_invalidate_paint(tab.nodes[idx]);
            }
        } else {
            if (tab.nodes[idx]) {
                im_node *n = tab.nodes[idx];
                im_node *dst = (n->parent == group_a) ? group_b : group_a;
                im_tree_remove_child(&tree, n->parent, n);
                im_tree_append_child(&tree, dst, n);
            }
        }

        im_layout_run(&tree, root, FX(W), FX(H), IM_ONE);
        im_dirty_collect_tree(&dirty, &tree, root, IM_ONE);
        render_partial(&s_part, &layers_part, &tab, root, &dirty, BG);
        render_full(&s_full, &tab, root, BG);
        im_dirty_clear(&dirty);

        if (!bytes_eq(pix_part, pix_full, (size_t)(W * H) * sizeof(uint32_t))) {
            mismatches++;
            break;
        }
    }

    CHECK(mismatches == 0, "10,000 random tree mutations partial redraw matches full redraw byte-for-byte");

    im_tree_destroy_subtree(&tree, root);
    im_layer_destroy(&layers_part);
    im_layer_destroy(&layers_full);
    heap_free_ptr(pix_part);
    heap_free_ptr(pix_full);
}

/* 2. Dirty-list merging, distant controls, scaling, scale-change, clip refusal. */
static void test_dirty_list_properties(void) {
    const int W = 200, H = 64;
    uint32_t *pix = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    CHECK(pix != NULL, "alloc distant buffer");
    if (!pix) return;
    for (int i = 0; i < W * H; i++) pix[i] = 0x00112233u;

    test_alloc_ctx actx = {false};
    im_allocator alloc = {test_alloc_fn, test_realloc_fn, test_free_fn, &actx};
    im_palette pal = make_dark_palette();
    im_surface surf = {pix, W, H, W, pal, IM_ONE, NULL};
    im_layer_ctx layers;
    CHECK(im_layer_init(&layers, &surf, alloc, 1024 * 1024), "init distant layers");

    im_dirty_list d;
    im_dirty_init(&d, W, H);
    CHECK(im_dirty_add(&d, (im_irect){4, 4, 24, 24}), "add left control");
    CHECK(im_dirty_add(&d, (im_irect){170, 30, 194, 54}), "add right control");
    CHECK(d.count == 2, "two distant controls remain 2 dirty rects");
    for (uint32_t i = 0; i < d.count; i++) {
        CHECK(im_dirty_push_clip(&layers, d.rects[i]), "push distant clip");
        im_fill_argb(&surf, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFFAA5500u);
        im_layer_pop_clip(&layers);
    }
    bool middle_untouched = true;
    for (int y = 0; y < H && middle_untouched; y++) {
        for (int x = 30; x < 165; x++) {
            if (pix[y * W + x] != 0x00112233u) {
                middle_untouched = false;
                break;
            }
        }
    }
    CHECK(middle_untouched, "intervening pixels between two distant controls remain untouched");
    CHECK(pix[10 * W + 10] == 0x00AA5500u, "left control painted");
    CHECK(pix[40 * W + 180] == 0x00AA5500u, "right control painted");

    /* 17-rect merge picks the known minimum-waste pair and breaks ties deterministically. */
    im_dirty_list m;
    im_dirty_init(&m, 1000, 1000);
    for (int i = 0; i < 15; i++) {
        int x = i * 50;
        CHECK(im_dirty_add(&m, (im_irect){x, 0, x + 10, 10}), "add spaced rect");
    }
    CHECK(im_dirty_add(&m, (im_irect){800, 500, 810, 510}), "add pair A");
    CHECK(im_dirty_add(&m, (im_irect){810, 500, 820, 510}), "add pair B (17th)");
    CHECK(m.count == IM_DIRTY_MAX_RECTS, "17 rects merged to 16");
    bool found_merged_pair = false;
    for (uint32_t i = 0; i < m.count; i++) {
        if (m.rects[i].left == 800 && m.rects[i].top == 500 &&
            m.rects[i].right == 820 && m.rects[i].bottom == 510) {
            found_merged_pair = true;
        }
    }
    CHECK(found_merged_pair, "17th rect merged zero-waste adjacent pair");

    /* Deterministic tie-break: two identical-waste runs produce identical rect arrays. */
    im_dirty_list t1, t2;
    im_dirty_init(&t1, 1000, 1000);
    im_dirty_init(&t2, 1000, 1000);
    for (int i = 0; i < 17; i++) {
        im_irect r = {i * 30, 0, i * 30 + 10, 10};
        im_dirty_add(&t1, r);
        im_dirty_add(&t2, r);
    }
    CHECK(t1.count == IM_DIRTY_MAX_RECTS && t2.count == IM_DIRTY_MAX_RECTS, "tie-break counts are 16");
    CHECK(bytes_eq(t1.rects, t2.rects, sizeof(t1.rects)), "tie-break merge is deterministic");

    /* Empty and fully-clipped rects schedule zero work. */
    im_dirty_list e;
    im_dirty_init(&e, 100, 100);
    CHECK(!im_dirty_add(&e, (im_irect){10, 10, 10, 20}), "zero-width ignored");
    CHECK(!im_dirty_add(&e, (im_irect){10, 20, 20, 10}), "inverted rect ignored");
    CHECK(!im_dirty_add(&e, (im_irect){-50, -50, 0, 0}), "offscreen negative ignored");
    CHECK(!im_dirty_add(&e, (im_irect){100, 100, 150, 150}), "offscreen positive ignored");
    CHECK(e.count == 0, "empty and fully-clipped rects produce count 0");

    /* 1,000 random rects merge to <= 16 and cover 100% of every input rect. */
    im_dirty_list big;
    im_dirty_init(&big, 640, 480);
    im_irect inputs[1000];
    uint32_t rng = 0x1234ABCDu;
    for (int i = 0; i < 1000; i++) {
        int32_t x = (int32_t)(rng_next(&rng) % 700u) - 30;
        int32_t y = (int32_t)(rng_next(&rng) % 540u) - 30;
        int32_t rw = 1 + (int32_t)(rng_next(&rng) % 45u);
        int32_t rh = 1 + (int32_t)(rng_next(&rng) % 35u);
        inputs[i] = (im_irect){x, y, x + rw, y + rh};
        im_dirty_add(&big, inputs[i]);
    }
    CHECK(big.count <= IM_DIRTY_MAX_RECTS, "1,000 random rects stay <= 16");
    bool all_covered = true;
    for (int i = 0; i < 1000; i++) {
        if (!im_dirty_covers(&big, inputs[i])) {
            all_covered = false;
            break;
        }
    }
    CHECK(all_covered, "1,000 random rects are 100% covered after merging");

    /* Fractional partly-offscreen rect at 150% DPI matches independent expected device-pixel box. */
    im_dirty_list sc;
    im_dirty_init(&sc, 100, 80);
    im_rect lrect = {
        -2 * IM_ONE - (IM_ONE / 2),
        10 * IM_ONE + (IM_ONE / 4),
        70 * IM_ONE + (IM_ONE / 4),
        60 * IM_ONE + (IM_ONE / 2)
    };
    im_fixed scale_150 = IM_ONE + (IM_ONE / 2);
    CHECK(im_dirty_add_scaled(&sc, lrect, scale_150), "add scaled fractional rect");
    CHECK(sc.count == 1, "scaled fractional rect count is 1");
    CHECK(sc.rects[0].left == 0 && sc.rects[0].top == 15 &&
          sc.rects[0].right == 100 && sc.rects[0].bottom == 80,
          "fractional partly-offscreen 150% rect rounds outward and clips accurately");

    /* Scale change in im_dirty_collect_tree marks full viewport dirty. */
    im_tree stree;
    im_tree_init(&stree, alloc);
    im_node *sroot = im_tree_create_node(&stree, IM_ELEM_BORDER, NULL);
    sroot->width = FX(40);
    sroot->height = FX(20);
    im_layout_run(&stree, sroot, FX(100), FX(80), IM_ONE);
    im_dirty_list sdirty;
    im_dirty_init(&sdirty, 100, 80);
    im_dirty_collect_tree(&sdirty, &stree, sroot, IM_ONE);
    im_dirty_clear(&sdirty);
    im_dirty_collect_tree(&sdirty, &stree, sroot, scale_150);
    CHECK(sdirty.count == 1 && sdirty.rects[0].left == 0 && sdirty.rects[0].top == 0 &&
          sdirty.rects[0].right == 100 && sdirty.rects[0].bottom == 80,
          "scale change triggers full viewport dirty rect");
    im_tree_destroy_subtree(&stree, sroot);

    /* Clip-push refusal preserves outer clip and does not lose pending damage. */
    CHECK(im_layer_push_clip_rect(&layers, (im_rect){FX(10), FX(10), FX(50), FX(50)}), "push outer clip");
    while (layers.clips.count < layers.clips.cap) {
        CHECK(im_layer_push_clip_rect(&layers, (im_rect){FX(10), FX(10), FX(50), FX(50)}), "fill clip cap");
    }
    uint32_t before_count = layers.clips.count;
    actx.fail_next = true;
    CHECK(!im_dirty_push_clip(&layers, (im_irect){20, 20, 40, 40}), "dirty push clip refuses on OOM");
    CHECK(layers.clips.count == before_count, "outer clip stack untouched after push refusal");
    while (layers.clips.count > 0) im_layer_pop_clip(&layers);

    im_layer_destroy(&layers);
    heap_free_ptr(pix);
}

/* 3. Demand-driven frame scheduler tests. */
typedef struct sched_probe {
    int requests;
    int cancels;
    uint32_t last_delay;
} sched_probe;

static void probe_request(void *ctx, uint32_t delay_ms) {
    sched_probe *p = (sched_probe *)ctx;
    p->requests++;
    p->last_delay = delay_ms;
}

static void probe_cancel(void *ctx) {
    sched_probe *p = (sched_probe *)ctx;
    p->cancels++;
}

static void test_frame_scheduler(void) {
    im_frame_scheduler sched;
    im_frame_init(&sched, 16);
    im_dirty_list empty;
    im_dirty_init(&empty, 100, 100);
    sched_probe p = {0, 0, 0};

    for (uint32_t t = 0; t <= 10000; t += 16) {
        im_frame_step(&sched, &empty, false, t, probe_request, probe_cancel, &p);
    }
    CHECK(p.requests == 0, "idle scheduler emits 0 frame requests over 10,000 ms");

    im_frame_mark_dirty(&sched);
    CHECK(im_frame_needs_tick(&sched, &empty, false), "dirty scheduler needs tick");
    uint64_t token = im_frame_begin_paint(&sched);
    im_frame_mark_dirty(&sched);
    im_frame_on_painted(&sched, token, 100);
    CHECK(im_frame_needs_tick(&sched, &empty, false), "damage queued during paint is preserved");
    uint64_t token2 = im_frame_begin_paint(&sched);
    im_frame_on_painted(&sched, token2, 116);
    CHECK(!im_frame_needs_tick(&sched, &empty, false), "clean after painting latest generation");

    im_frame_init(&sched, 16);
    p.requests = 0;
    p.cancels = 0;
    CHECK(im_frame_step(&sched, &empty, true, 1000, probe_request, probe_cancel, &p), "animating requests frame");
    CHECK(p.requests == 1, "first animation request issued");
    im_frame_on_painted(&sched, im_frame_begin_paint(&sched), 1016);
    CHECK(im_frame_step(&sched, &empty, false, 1032, probe_request, probe_cancel, &p), "final settled frame requested");
    CHECK(p.requests == 2, "final settled frame triggered request");
    im_frame_on_painted(&sched, im_frame_begin_paint(&sched), 1032);
    CHECK(!im_frame_step(&sched, &empty, false, 1048, probe_request, probe_cancel, &p), "idle after final settled frame");
    CHECK(p.requests == 2, "no further requests after final settled frame");

    im_palette pal = make_dark_palette();
    im_state_machine sm;
    im_states_init(&sm, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
    im_states_go_to(&sm, "Off", 0);
    im_states_go_to(&sm, "On", 10);
    CHECK(im_states_is_animating(&sm, 20), "toggle switch animates at 20 ms");

    im_frame_init(&sched, 16);
    p.requests = 0;
    p.cancels = 0;
    im_frame_step(&sched, &empty, im_states_is_animating(&sm, 20), 20, probe_request, probe_cancel, &p);
    im_frame_on_painted(&sched, im_frame_begin_paint(&sched), 20);
    im_states_set_reduced_motion(&sm, true, 30);
    CHECK(!im_states_is_animating(&sm, 30), "reduced_motion snaps animation immediately");
    CHECK(im_frame_step(&sched, &empty, im_states_is_animating(&sm, 30), 30, probe_request, probe_cancel, &p),
          "reduced_motion snap still requests final settled frame");
    im_frame_on_painted(&sched, im_frame_begin_paint(&sched), 30);
    CHECK(!im_frame_step(&sched, &empty, im_states_is_animating(&sm, 46), 46, probe_request, probe_cancel, &p),
          "scheduler goes idle after reduced_motion final settled frame");
}

/* 4 & 5. Real Windows window tests: WM_MOUSEMOVE partial hover, animation tick stop,
   partial band blit, resize stride change, and pre-open / post-close refusal. */
typedef struct win_test_state {
    int phase;
    int paint_count;
    int tick_count;
    int hover_paint_count;
    int legacy_full_count;
    int band_paint_count;
    int resize_paint_count;
    bool first_was_full;
    bool hover_was_partial;
    bool hover_pixels_outside_untouched;
    bool legacy_was_full;
    bool band_was_partial;
    bool band_outside_intact;
    bool resize_was_full;
    bool resize_pixels_match;
    bool anim_woke_and_stopped;
    bool btn1_hovered;
    uint32_t anim_start_tick;
    im_host host;
    im_state_machine sm;
    im_frame_scheduler sched;
    im_dirty_list dirty;
} win_test_state;

static uint32_t asym_pixel(int x, int y) {
    uint32_t r = (uint32_t)((x * 3 + y * 5) & 0xFF);
    uint32_t g = (uint32_t)((x * 7 + y * 2 + 40) & 0xFF);
    uint32_t b = (uint32_t)((x * 11 + y * 13 + 80) & 0xFF);
    return (r << 16) | (g << 8) | b;
}

static void win_sched_request(void *ctx, uint32_t delay_ms) {
    im_window *w = (im_window *)ctx;
    im_window_request_frame(w, delay_ms);
}

static void win_sched_cancel(void *ctx) {
    im_window *w = (im_window *)ctx;
    im_window_cancel_frame(w);
}

static void win_paint(im_window *w, im_canvas *c) {
    win_test_state *st = (win_test_state *)w->user;
    st->paint_count++;

    if (st->phase == 0) {
        st->first_was_full = c->full_redraw;
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                c->pixels[y * c->width + x] = asym_pixel(x, y);
            }
        }
        st->phase = 1;
        PostMessageW((HWND)w->native, WM_MOUSEMOVE, 0, MAKELPARAM(40, 32));
        return;
    }

    if (st->phase == 1) {
        st->hover_paint_count++;
        st->hover_was_partial = (!c->full_redraw &&
            c->dirty.left >= 20 && c->dirty.top >= 20 &&
            c->dirty.right <= 100 && c->dirty.bottom <= 52);
        for (int y = c->dirty.top; y < c->dirty.bottom; y++) {
            for (int x = c->dirty.left; x < c->dirty.right; x++) {
                c->pixels[y * c->width + x] = 0x0022AAFFu;
            }
        }
        st->hover_pixels_outside_untouched =
            (c->pixels[10 * c->width + 10] == asym_pixel(10, 10)) &&
            (c->pixels[32 * c->width + 160] == asym_pixel(160, 32));

        st->phase = 2;
        PostMessageW((HWND)w->native, WM_KEYDOWN, 'A', 0);
        return;
    }

    if (st->phase == 2) {
        st->legacy_full_count++;
        st->legacy_was_full = c->full_redraw;
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                c->pixels[y * c->width + x] = asym_pixel(x, y);
            }
        }
        st->phase = 3;
        im_window_invalidate(w, 16, 70, 180, 90);
        return;
    }

    if (st->phase == 3) {
        st->band_paint_count++;
        st->band_was_partial = (!c->full_redraw &&
            c->dirty.left == 16 && c->dirty.top == 70 &&
            c->dirty.right == 180 && c->dirty.bottom == 90);
        for (int y = c->dirty.top; y < c->dirty.bottom; y++) {
            for (int x = c->dirty.left; x < c->dirty.right; x++) {
                c->pixels[y * c->width + x] = 0x00FF3366u;
            }
        }
        st->band_outside_intact =
            (c->pixels[69 * c->width + 50] == asym_pixel(50, 69)) &&
            (c->pixels[90 * c->width + 50] == asym_pixel(50, 90)) &&
            (c->pixels[80 * c->width + 15] == asym_pixel(15, 80)) &&
            (c->pixels[80 * c->width + 180] == asym_pixel(180, 80));

        st->phase = 4;
        SetWindowPos((HWND)w->native, NULL, 0, 0, 320, 200,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    if (st->phase == 4) {
        st->resize_paint_count++;
        st->resize_was_full = c->full_redraw;
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                c->pixels[y * c->width + x] = asym_pixel(x, y);
            }
        }
        bool match = true;
        for (int y = 0; y < c->height && match; y += 7) {
            for (int x = 0; x < c->width; x += 11) {
                if (c->pixels[y * c->width + x] != asym_pixel(x, y)) {
                    match = false;
                    break;
                }
            }
        }
        st->resize_pixels_match = match;

        st->phase = 5;
        st->anim_start_tick = im_host_now_ms(&st->host);
        im_states_go_to(&st->sm, "On", 0);
        im_frame_step(&st->sched, &st->dirty, im_states_is_animating(&st->sm, 0),
                      0, win_sched_request, win_sched_cancel, w);
        return;
    }

    if (st->phase == 5) {
        uint64_t tok = im_frame_begin_paint(&st->sched);
        uint32_t elapsed = im_host_now_ms(&st->host) - st->anim_start_tick;
        bool anim = im_states_is_animating(&st->sm, elapsed);
        for (int y = c->dirty.top; y < c->dirty.bottom; y++) {
            for (int x = c->dirty.left; x < c->dirty.right; x++) {
                c->pixels[y * c->width + x] = 0x00445566u;
            }
        }
        im_frame_on_painted(&st->sched, tok, elapsed);
        if (!im_frame_step(&st->sched, &st->dirty, anim, elapsed,
                           win_sched_request, win_sched_cancel, w)) {
            st->anim_woke_and_stopped = (st->tick_count > 0 && !w->frame_timer_active);
            st->phase = 6;
            PostMessageW((HWND)w->native, WM_CLOSE, 0, 0);
        }
    }
}

static bool win_event(im_window *w, const im_event *e) {
    win_test_state *st = (win_test_state *)w->user;
    if (st->phase == 1 && e->kind == IM_POINTER_MOVE) {
        if (e->x >= 20 && e->x < 100 && e->y >= 20 && e->y < 52 && !st->btn1_hovered) {
            st->btn1_hovered = true;
            im_window_invalidate(w, 20, 20, 100, 52);
            return true;
        }
        return false;
    }
    if (st->phase == 2 && e->kind == IM_KEY_DOWN) {
        return true;
    }
    return false;
}

static void win_tick(im_window *w) {
    win_test_state *st = (win_test_state *)w->user;
    st->tick_count++;
    im_window_invalidate(w, 0, 0, 32, 32);
}

static void test_windows_partial_paint_and_lifecycle(void) {
    win_test_state st;
    memset(&st, 0, sizeof(st));
    im_palette pal = make_dark_palette();
    im_states_init(&st.sm, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
    im_states_go_to(&st.sm, "Off", 0);
    im_frame_init(&st.sched, 16);
    im_dirty_init(&st.dirty, 240, 140);

    im_window w;
    memset(&w, 0, sizeof(w));
    w.title = L"ImpossibleSDK Dirty Test";
    w.width = 240;
    w.height = 140;
    w.user = &st;
    w.paint = win_paint;
    w.event = win_event;
    w.tick = win_tick;
    im_win_host_init(&st.host, &w);

    CHECK(!im_window_invalidate(&w, 0, 0, 10, 10), "invalidate before open returns false");
    im_dirty_rect one = {0, 0, 10, 10};
    CHECK(!im_window_invalidate_rects(&w, &one, 1), "invalidate_rects before open returns false");
    CHECK(!im_window_request_frame(&w, 16), "request_frame before open returns false");

    CHECK(im_window_run(&w, false), "im_window_run succeeds");

    CHECK(w.native == NULL, "window->native cleared to NULL after close");
    CHECK(!im_window_invalidate(&w, 0, 0, 10, 10), "invalidate after close returns false");
    CHECK(!im_window_request_frame(&w, 16), "request_frame after close returns false");

    CHECK(st.first_was_full, "first paint has full_redraw == true");
    CHECK(st.hover_paint_count >= 1 && st.hover_was_partial,
          "WM_MOUSEMOVE hover produced partial dirty rect bounded to button 1");
    CHECK(st.hover_pixels_outside_untouched,
          "WM_MOUSEMOVE hover left pixels outside button 1 untouched");
    CHECK(st.legacy_full_count >= 1 && st.legacy_was_full,
          "legacy event handler returning true without explicit damage gets full_redraw == true");
    CHECK(st.band_paint_count >= 1 && st.band_was_partial,
          "explicit horizontal band invalidation sets full_redraw == false with exact rect");
    CHECK(st.band_outside_intact,
          "explicit horizontal band paint leaves surrounding pixels intact");
    CHECK(st.resize_paint_count >= 1 && st.resize_was_full && st.resize_pixels_match,
          "stride-changing resize triggers full_redraw == true with matching pixels");
    CHECK(st.anim_woke_and_stopped,
          "real im_window animation woke via im_window_request_frame and stopped when settled");
}

int im_main(void) {
    test_random_tree_mutations();
    test_dirty_list_properties();
    test_frame_scheduler();
    test_windows_partial_paint_and_lifecycle();
    if (g_failures != 0) {
        write_str(STD_ERROR_HANDLE, "test_dirty: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_dirty: ALL PASSED\n");
    return 0;
}
