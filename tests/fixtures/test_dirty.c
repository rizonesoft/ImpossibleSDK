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

static bool render_partial(im_surface *surf, im_layer_ctx *layers, const leaf_table *tab,
                           const im_node *root, const im_dirty_list *dirty, uint32_t bg) {
    im_rect full = {0, 0, FX(surf->width), FX(surf->height)};
    im_radii r = {0, 0, 0, 0};
    for (uint32_t i = 0; i < dirty->count; i++) {
        if (!im_dirty_push_clip(layers, dirty->rects[i])) {
            return false;
        }
        im_fill_argb(surf, full, r, bg);
        render_node_recursive(surf, tab, root);
        im_layer_pop_clip(layers);
    }
    return true;
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

/* 1. Tree init/reattach regressions and 10,000 random tree mutations. */
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

    /* Regression: im_tree_init must zero pending_damage_count and pending_damage_overflow. */
    im_tree dirty_storage;
    dirty_storage.pending_damage_count = 17u;
    dirty_storage.pending_damage_overflow = true;
    CHECK(im_tree_init(&dirty_storage, alloc), "im_tree_init on dirty stack storage");
    CHECK(dirty_storage.pending_damage_count == 0u && !dirty_storage.pending_damage_overflow,
          "im_tree_init clears pending_damage_count and pending_damage_overflow");

    /* Regression: detach -> collect -> clear -> reattach at same bounds produces damage,
       and replace_child + destroy_subtree does not double-queue bounds. */
    {
        im_node *r = im_tree_create_node(&dirty_storage, IM_ELEM_GRID, "r");
        im_node *p = im_tree_create_node(&dirty_storage, IM_ELEM_BORDER, "p");
        p->h_align = IM_ALIGN_START;
        p->v_align = IM_ALIGN_START;
        p->width = FX(20);
        p->height = FX(20);
        p->margin = (im_thickness){FX(10), FX(10), 0, 0};
        im_tree_append_child(&dirty_storage, r, p);
        im_layout_run(&dirty_storage, r, FX(W), FX(H), IM_ONE);
        im_dirty_list dl;
        im_dirty_init(&dl, W, H);
        im_dirty_collect_tree(&dl, &dirty_storage, r, IM_ONE);
        im_dirty_clear(&dl);

        im_tree_remove_child(&dirty_storage, r, p);
        im_layout_run(&dirty_storage, r, FX(W), FX(H), IM_ONE);
        im_dirty_collect_tree(&dl, &dirty_storage, r, IM_ONE);
        CHECK(dl.count > 0u, "detach produces damage");
        im_dirty_clear(&dl);

        im_tree_append_child(&dirty_storage, r, p);
        im_layout_run(&dirty_storage, r, FX(W), FX(H), IM_ONE);
        im_dirty_collect_tree(&dl, &dirty_storage, r, IM_ONE);
        CHECK(dl.count > 0u && im_dirty_covers(&dl, (im_irect){10, 10, 30, 30}),
              "reattach at same bounds produces damage covering control");

        /* replace_child + destroy_subtree queues old bounds once, not twice. */
        im_dirty_clear(&dl);
        im_node *q = im_tree_create_node(&dirty_storage, IM_ELEM_BORDER, "q");
        q->h_align = IM_ALIGN_START;
        q->v_align = IM_ALIGN_START;
        q->width = FX(20);
        q->height = FX(20);
        im_node *removed = im_tree_replace_child(&dirty_storage, r, 0, q);
        CHECK(removed == p && dirty_storage.pending_damage_count == 1u, "replace queues old bounds once");
        im_tree_destroy_subtree(&dirty_storage, removed);
        CHECK(dirty_storage.pending_damage_count == 1u, "destroy after replace does not double-queue bounds");
        im_tree_destroy_subtree(&dirty_storage, r);
    }

    im_surface s_part = {pix_part, W, H, W, pal, IM_ONE, NULL, NULL};
    im_surface s_full = {pix_full, W, H, W, pal, IM_ONE, NULL, NULL};
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
        CHECK(render_partial(&s_part, &layers_part, &tab, root, &dirty, BG), "partial render succeeds");
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

/* 2. Dirty-list merging, overlap waste, scaling, large coords, and clip refusal retry. */
static void test_dirty_list_properties(void) {
    const int W = 200, H = 64;
    uint32_t *pix = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    uint32_t *ref = (uint32_t *)heap_alloc_zero((size_t)(W * H) * sizeof(uint32_t));
    CHECK(pix && ref, "alloc distant buffer");
    if (!pix || !ref) return;
    for (int i = 0; i < W * H; i++) {
        pix[i] = 0x00112233u;
        ref[i] = 0x00112233u;
    }

    test_alloc_ctx actx = {false};
    im_allocator alloc = {test_alloc_fn, test_realloc_fn, test_free_fn, &actx};
    im_palette pal = make_dark_palette();
    im_surface surf = {pix, W, H, W, pal, IM_ONE, NULL, NULL};
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

    /* Regression: overlapping 10x10 rects offset by (2,2) have 8px waste, so must NOT merge early. */
    im_dirty_list ov;
    im_dirty_init(&ov, 100, 100);
    CHECK(im_dirty_add(&ov, (im_irect){0, 0, 10, 10}), "add overlap A");
    CHECK(im_dirty_add(&ov, (im_irect){2, 2, 12, 12}), "add overlap B");
    CHECK(ov.count == 2u, "overlapping rects with non-zero bounding waste stay separate when slots are free");

    /* Regression: at capacity (17th rect), intersection area must be subtracted in waste math.
       Overlapping 20x20 pair offset by (10,10) has true waste 900-(400+400-100)=200 (buggy: 100).
       Disjoint 10x10 pair with 15px gap has true waste 350-200=150 -> must merge the disjoint pair! */
    im_dirty_list cap_ov;
    im_dirty_init(&cap_ov, 4000, 1000);
    for (int i = 0; i < 13; i++) {
        int x = i * 200;
        CHECK(im_dirty_add(&cap_ov, (im_irect){x, 0, x + 10, 10}), "add well-spaced rect");
    }
    CHECK(im_dirty_add(&cap_ov, (im_irect){500, 200, 510, 210}), "add disjoint A");
    CHECK(im_dirty_add(&cap_ov, (im_irect){0, 200, 20, 220}), "add overlap pair A");
    CHECK(im_dirty_add(&cap_ov, (im_irect){10, 210, 30, 230}), "add overlap pair B");
    CHECK(im_dirty_add(&cap_ov, (im_irect){525, 200, 535, 210}), "add disjoint B (17th)");
    CHECK(cap_ov.count == IM_DIRTY_MAX_RECTS, "capacity overlap test stays at 16");
    bool merged_disjoint_not_overlap = false;
    for (uint32_t i = 0; i < cap_ov.count; i++) {
        if (cap_ov.rects[i].left == 500 && cap_ov.rects[i].top == 200 &&
            cap_ov.rects[i].right == 535 && cap_ov.rects[i].bottom == 210) {
            merged_disjoint_not_overlap = true;
        }
    }
    CHECK(merged_disjoint_not_overlap, "capacity merge subtracts intersection area and picks lower-waste disjoint pair");

    /* 17-rect merge picks the known minimum-waste pair. */
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

    /* Deterministic tie-break: equal-waste pairs must merge the lowest (0, 1) pair [0, 40) x [0, 10). */
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
    CHECK(t1.rects[0].left == 0 && t1.rects[0].top == 0 && t1.rects[0].right == 40 && t1.rects[0].bottom == 10,
          "tie-break selects lowest (0, 1) pair");

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

    /* Fractional scaling at 150% DPI: both unclipped right/bottom ceil AND partly-offscreen clip. */
    im_dirty_list sc;
    im_dirty_init(&sc, 100, 80);
    im_fixed scale_150 = IM_ONE + (IM_ONE / 2);
    /* (10.25, 10.25)-(20.25, 20.25) * 1.5 = (15.375, 15.375)-(30.375, 30.375) => [15, 31) x [15, 31). */
    im_rect inner_frac = {
        10 * IM_ONE + (IM_ONE / 4),
        10 * IM_ONE + (IM_ONE / 4),
        20 * IM_ONE + (IM_ONE / 4),
        20 * IM_ONE + (IM_ONE / 4)
    };
    CHECK(im_dirty_add_scaled(&sc, inner_frac, scale_150), "add unclipped fractional 150% rect");
    CHECK(sc.count == 1 && sc.rects[0].left == 15 && sc.rects[0].top == 15 &&
          sc.rects[0].right == 31 && sc.rects[0].bottom == 31,
          "unclipped 150% fractional rect floors left/top and ceils right/bottom");

    im_dirty_clear(&sc);
    im_rect lrect = {
        -2 * IM_ONE - (IM_ONE / 2),
        10 * IM_ONE + (IM_ONE / 4),
        70 * IM_ONE + (IM_ONE / 4),
        60 * IM_ONE + (IM_ONE / 2)
    };
    CHECK(im_dirty_add_scaled(&sc, lrect, scale_150), "add partly-offscreen scaled rect");
    CHECK(sc.count == 1 && sc.rects[0].left == 0 && sc.rects[0].top == 15 &&
          sc.rects[0].right == 100 && sc.rects[0].bottom == 80,
          "partly-offscreen 150% rect rounds outward and clips accurately");

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

    /* Large device coordinates >= 32768 must be refused cleanly instead of wrapping to empty clip. */
    CHECK(!im_dirty_push_clip(&layers, (im_irect){0, 0, 32768, 100}),
          "im_dirty_push_clip refuses right >= 32768 instead of overflowing 16.16");

    /* Clip-push refusal with pending damage extending outside outer clip:
       verify outer clip is preserved, pending damage survives, and retry paints exact pixels. */
    for (int i = 0; i < W * H; i++) {
        pix[i] = 0x00112233u;
        ref[i] = 0x00112233u;
    }
    im_dirty_list pending;
    im_dirty_init(&pending, W, H);
    im_dirty_add(&pending, (im_irect){20, 10, 120, 50}); /* extends beyond outer clip [10, 50) */
    CHECK(im_layer_push_clip_rect(&layers, (im_rect){FX(10), FX(10), FX(50), FX(50)}), "push outer clip");
    while (layers.clips.count < layers.clips.cap) {
        CHECK(im_layer_push_clip_rect(&layers, (im_rect){FX(10), FX(10), FX(50), FX(50)}), "fill clip cap");
    }
    uint32_t before_count = layers.clips.count;
    actx.fail_next = true;
    CHECK(!im_dirty_push_clip(&layers, pending.rects[0]), "dirty push clip refuses on OOM");
    CHECK(layers.clips.count == before_count, "outer clip stack untouched after push refusal");
    CHECK(pending.count == 1u, "pending damage remains intact after clip-push refusal");
    while (layers.clips.count > 0) im_layer_pop_clip(&layers);

    /* Retry with outer clip popped: full pending rect [20, 120) x [10, 50) paints cleanly. */
    CHECK(im_dirty_push_clip(&layers, pending.rects[0]), "retry clip push succeeds");
    im_fill_argb(&surf, (im_rect){0, 0, FX(W), FX(H)}, (im_radii){0, 0, 0, 0}, 0xFF33CC66u);
    im_layer_pop_clip(&layers);
    im_dirty_clear(&pending);
    for (int y = 10; y < 50; y++) {
        for (int x = 20; x < 120; x++) {
            ref[y * W + x] = 0x0033CC66u;
        }
    }
    CHECK(bytes_eq(pix, ref, (size_t)(W * H) * sizeof(uint32_t)),
          "retry after clip-push refusal paints exact pending damage outside former outer clip");

    im_layer_destroy(&layers);
    heap_free_ptr(pix);
    heap_free_ptr(ref);
}

/* 3. Demand-driven frame scheduler tests. */
typedef struct sched_probe {
    int requests;
    int cancels;
    uint32_t last_delay;
    bool fail_next_request;
} sched_probe;

static bool probe_request(void *ctx, uint32_t delay_ms) {
    sched_probe *p = (sched_probe *)ctx;
    if (p->fail_next_request) {
        p->fail_next_request = false;
        return false;
    }
    p->requests++;
    p->last_delay = delay_ms;
    return true;
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
    sched_probe p = {0, 0, 0, false};

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

    /* Regression: failed host request must not strand pending_request. */
    im_frame_mark_dirty(&sched);
    p.fail_next_request = true;
    CHECK(!im_frame_step(&sched, &empty, false, 200, probe_request, probe_cancel, &p),
          "step returns false when host refuses frame request");
    CHECK(!sched.pending_request, "pending_request remains false after host refusal");
    CHECK(im_frame_step(&sched, &empty, false, 200, probe_request, probe_cancel, &p),
          "retry succeeds immediately after host refusal");
    im_frame_on_painted(&sched, im_frame_begin_paint(&sched), 200);

    /* Regression: im_frame_needs_tick recognizes settlement before step,
       and a stale paint token taken before settlement cannot clear the final-frame obligation. */
    im_frame_init(&sched, 16);
    p.requests = 0;
    p.cancels = 0;
    CHECK(im_frame_step(&sched, &empty, true, 1000, probe_request, probe_cancel, &p), "animating requests frame");
    uint64_t stale_tok = im_frame_begin_paint(&sched);
    CHECK(im_frame_needs_tick(&sched, &empty, false), "needs_tick recognizes animation settlement before step");
    CHECK(im_frame_step(&sched, &empty, false, 1016, probe_request, probe_cancel, &p), "settlement requests final frame");
    /* Stale paint token from before settlement finishes now: must NOT clear final-frame obligation. */
    im_frame_on_painted(&sched, stale_tok, 1016);
    CHECK(im_frame_needs_tick(&sched, &empty, false), "stale paint token cannot acknowledge newer settlement");
    /* Fresh paint token taken after settlement completes and settles scheduler. */
    uint64_t settled_tok = im_frame_begin_paint(&sched);
    im_frame_on_painted(&sched, settled_tok, 1032);
    CHECK(!im_frame_step(&sched, &empty, false, 1048, probe_request, probe_cancel, &p), "idle after final settled frame");

    /* Reduced-motion snap mid-animation delivers final settled frame, then stops. */
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
    CHECK(im_frame_needs_tick(&sched, &empty, im_states_is_animating(&sm, 30)),
          "needs_tick sees reduced_motion snap immediately");
    CHECK(im_frame_step(&sched, &empty, im_states_is_animating(&sm, 30), 30, probe_request, probe_cancel, &p),
          "reduced_motion snap requests final settled frame");
    im_frame_on_painted(&sched, im_frame_begin_paint(&sched), 30);
    CHECK(!im_frame_step(&sched, &empty, im_states_is_animating(&sm, 46), 46, probe_request, probe_cancel, &p),
          "scheduler goes idle after reduced_motion final settled frame");
}

/* Dynamically loaded GDI/User32 pixel readback (via Tier 0 LoadLibraryW/GetProcAddress)
   to observe actual presented window pixels after SetDIBitsToDevice, independent of w->buffer. */
typedef HDC (WINAPI *fn_GetDC)(HWND);
typedef int (WINAPI *fn_ReleaseDC)(HWND, HDC);
typedef HDC (WINAPI *fn_CreateCompatibleDC)(HDC);
typedef HBITMAP (WINAPI *fn_CreateDIBSection)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
typedef HGDIOBJ (WINAPI *fn_SelectObject)(HDC, HGDIOBJ);
typedef BOOL (WINAPI *fn_BitBlt)(HDC, int, int, int, int, HDC, int, int, DWORD);
typedef BOOL (WINAPI *fn_DeleteObject)(HGDIOBJ);
typedef BOOL (WINAPI *fn_DeleteDC)(HDC);
typedef BOOL (WINAPI *fn_GdiFlush)(void);
typedef LRESULT (WINAPI *fn_SendMessageW)(HWND, UINT, WPARAM, LPARAM);

static bool read_presented_pixels(HWND hwnd, int w, int h, uint32_t *out) {
    if (!hwnd || w <= 0 || h <= 0 || !out) return false;
    HMODULE u32 = LoadLibraryW(L"user32.dll");
    HMODULE g32 = LoadLibraryW(L"gdi32.dll");
    if (!u32 || !g32) return false;
    fn_GetDC pGetDC = (fn_GetDC)(void *)GetProcAddress(u32, "GetDC");
    fn_ReleaseDC pReleaseDC = (fn_ReleaseDC)(void *)GetProcAddress(u32, "ReleaseDC");
    fn_CreateCompatibleDC pCreateCompatibleDC = (fn_CreateCompatibleDC)(void *)GetProcAddress(g32, "CreateCompatibleDC");
    fn_CreateDIBSection pCreateDIBSection = (fn_CreateDIBSection)(void *)GetProcAddress(g32, "CreateDIBSection");
    fn_SelectObject pSelectObject = (fn_SelectObject)(void *)GetProcAddress(g32, "SelectObject");
    fn_BitBlt pBitBlt = (fn_BitBlt)(void *)GetProcAddress(g32, "BitBlt");
    fn_DeleteObject pDeleteObject = (fn_DeleteObject)(void *)GetProcAddress(g32, "DeleteObject");
    fn_DeleteDC pDeleteDC = (fn_DeleteDC)(void *)GetProcAddress(g32, "DeleteDC");
    fn_GdiFlush pGdiFlush = (fn_GdiFlush)(void *)GetProcAddress(g32, "GdiFlush");
    if (!pGetDC || !pReleaseDC || !pCreateCompatibleDC || !pCreateDIBSection ||
        !pSelectObject || !pBitBlt || !pDeleteObject || !pDeleteDC || !pGdiFlush) {
        return false;
    }
    pGdiFlush();
    HDC hdc = pGetDC(hwnd);
    if (!hdc) return false;
    HDC memdc = pCreateCompatibleDC(hdc);
    if (!memdc) { pReleaseDC(hwnd, hdc); return false; }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = pCreateDIBSection(memdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        pDeleteDC(memdc);
        pReleaseDC(hwnd, hdc);
        return false;
    }
    HGDIOBJ old = pSelectObject(memdc, bmp);
    BOOL blt_ok = pBitBlt(memdc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    pGdiFlush();
    if (blt_ok) {
        const uint32_t *src = (const uint32_t *)bits;
        for (int i = 0; i < w * h; i++) out[i] = src[i] & 0x00FFFFFFu;
    }
    pSelectObject(memdc, old);
    pDeleteObject(bmp);
    pDeleteDC(memdc);
    pReleaseDC(hwnd, hdc);
    return blt_ok != 0;
}

static bool send_msg_sync(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HMODULE u32 = LoadLibraryW(L"user32.dll");
    if (!u32) return false;
    fn_SendMessageW pSendMessageW = (fn_SendMessageW)(void *)GetProcAddress(u32, "SendMessageW");
    if (!pSendMessageW) return false;
    (void)pSendMessageW(hwnd, msg, wp, lp);
    return true;
}

/* Write a solid sentinel directly to the window DC without touching w->buffer,
   so a real exposure restore can be distinguished from an unblitted stale gap. */
static bool write_dc_sentinel(HWND hwnd, int left, int top, int right, int bottom, uint32_t rgb) {
    int rw = right - left, rh = bottom - top;
    if (!hwnd || rw <= 0 || rh <= 0) return false;
    HMODULE u32 = LoadLibraryW(L"user32.dll");
    HMODULE g32 = LoadLibraryW(L"gdi32.dll");
    if (!u32 || !g32) return false;
    fn_GetDC pGetDC = (fn_GetDC)(void *)GetProcAddress(u32, "GetDC");
    fn_ReleaseDC pReleaseDC = (fn_ReleaseDC)(void *)GetProcAddress(u32, "ReleaseDC");
    fn_GdiFlush pGdiFlush = (fn_GdiFlush)(void *)GetProcAddress(g32, "GdiFlush");
    if (!pGetDC || !pReleaseDC || !pGdiFlush) return false;
    uint32_t *buf = (uint32_t *)heap_alloc_zero((size_t)(rw * rh) * sizeof(uint32_t));
    if (!buf) return false;
    for (int i = 0; i < rw * rh; i++) buf[i] = rgb;
    HDC hdc = pGetDC(hwnd);
    if (!hdc) { heap_free_ptr(buf); return false; }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = rw;
    bi.bmiHeader.biHeight = -rh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    int rows = SetDIBitsToDevice(hdc, left, top, (DWORD)rw, (DWORD)rh, 0, 0, 0, (UINT)rh, buf, &bi, DIB_RGB_COLORS);
    pGdiFlush();
    pReleaseDC(hwnd, hdc);
    heap_free_ptr(buf);
    return rows == rh;
}

/* 4 & 5. Real Windows window tests: WM_MOUSEMOVE exact hover, disjoint rects + gap exposure,
   nested reentrant event, stale delayed timer vs replacement, independent exposure,
   presented-pixel DC readback, resize stride change, real animation wake/stop, and lifecycle refusal. */
typedef struct win_test_state {
    int phase;
    int paint_count;
    int tick_count;
    int stale_ticks_observed;
    bool first_was_full;
    bool first_screen_verified;
    bool hover_exact_rect;
    bool hover_pixels_outside_untouched;
    bool disjoint_preserved;
    bool disjoint_gap_sentinel_seen;
    bool disjoint_gap_screen_verified;
    bool nested_send_ok;
    bool nested_inner_ran;
    bool nested_outer_got_full;
    bool legacy_was_full;
    bool band_was_partial;
    bool band_screen_verified;
    bool exposure_was_partial;
    bool exposure_screen_verified;
    bool resize_was_full;
    bool resize_screen_verified;
    bool stale_timer_ignored;
    bool replacement_timer_fired;
    bool anim_woke_and_stopped;
    bool btn1_hovered;
    bool req_zero_ok;
    bool cancel_while_open_ok;
    bool waiting_replacement_tick;
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

static bool win_sched_request(void *ctx, uint32_t delay_ms) {
    im_window *w = (im_window *)ctx;
    return im_window_request_frame(w, delay_ms);
}

static void win_sched_cancel(void *ctx) {
    im_window *w = (im_window *)ctx;
    (void)im_window_cancel_frame(w);
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
        /* Arm a 0 ms wake-up (check return value) and immediately cancel it. */
        st->req_zero_ok = im_window_request_frame(w, 0);
        st->cancel_while_open_ok = im_window_cancel_frame(w);
        st->phase = 1;
        PostMessageW((HWND)w->native, WM_KEYDOWN, '1', 0);
        return;
    }

    if (st->phase == 1) {
        st->stale_ticks_observed = st->tick_count;
        st->hover_exact_rect = (!c->full_redraw && c->dirty_count == 1 &&
            c->dirty.left == 20 && c->dirty.top == 20 &&
            c->dirty.right == 100 && c->dirty.bottom == 52);
        for (int y = c->dirty.top; y < c->dirty.bottom; y++) {
            for (int x = c->dirty.left; x < c->dirty.right; x++) {
                c->pixels[y * c->width + x] = 0x0022AAFFu;
            }
        }
        st->hover_pixels_outside_untouched =
            (c->pixels[10 * c->width + 10] == asym_pixel(10, 10)) &&
            (c->pixels[32 * c->width + 160] == asym_pixel(160, 32));

        /* Phase 2: corrupt the presented DC gap with a magenta sentinel (without touching w->buffer),
           verify the sentinel is visible on screen, then invalidate two disjoint controls PLUS the gap. */
        st->phase = 2;
        if (write_dc_sentinel((HWND)w->native, 60, 40, 100, 70, 0x00FF00FFu)) {
            int lw = c->width, lh = c->height;
            uint32_t *chk = (uint32_t *)heap_alloc_zero((size_t)(lw * lh) * sizeof(uint32_t));
            if (chk && read_presented_pixels((HWND)w->native, lw, lh, chk)) {
                st->disjoint_gap_sentinel_seen = (chk[55 * lw + 80] == 0x00FF00FFu);
            }
            heap_free_ptr(chk);
        }
        im_dirty_rect pair[2] = {{4, 4, 24, 24}, {180, 90, 210, 120}};
        im_window_invalidate_rects(w, pair, 2);
        RECT gap = {60, 40, 100, 70};
        InvalidateRect((HWND)w->native, &gap, FALSE);
        return;
    }

    if (st->phase == 2) {
        st->disjoint_preserved = (!c->full_redraw && c->dirty_count == 2 &&
            c->dirty_rects[0].left == 4 && c->dirty_rects[0].top == 4 &&
            c->dirty_rects[0].right == 24 && c->dirty_rects[0].bottom == 24 &&
            c->dirty_rects[1].left == 180 && c->dirty_rects[1].top == 90 &&
            c->dirty_rects[1].right == 210 && c->dirty_rects[1].bottom == 120);
        for (int i = 0; i < c->dirty_count; i++) {
            im_dirty_rect r = c->dirty_rects[i];
            for (int y = r.top; y < r.bottom; y++) {
                for (int x = r.left; x < r.right; x++) {
                    c->pixels[y * c->width + x] = 0x0077DD44u;
                }
            }
        }
        st->phase = 3;
        PostMessageW((HWND)w->native, WM_KEYDOWN, '2', 0);
        return;
    }

    if (st->phase == 3) {
        /* Nested reentrant event: inner invalidated small rect, outer returned true -> must be full_redraw. */
        st->nested_outer_got_full = c->full_redraw;
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                c->pixels[y * c->width + x] = asym_pixel(x, y);
            }
        }
        st->phase = 4;
        PostMessageW((HWND)w->native, WM_KEYDOWN, 'A', 0);
        return;
    }

    if (st->phase == 4) {
        st->legacy_was_full = c->full_redraw;
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                c->pixels[y * c->width + x] = asym_pixel(x, y);
            }
        }
        st->phase = 5;
        im_window_invalidate(w, 16, 70, 180, 90);
        return;
    }

    if (st->phase == 5) {
        st->band_was_partial = (!c->full_redraw &&
            c->dirty.left == 16 && c->dirty.top == 70 &&
            c->dirty.right == 180 && c->dirty.bottom == 90);
        for (int y = c->dirty.top; y < c->dirty.bottom; y++) {
            for (int x = c->dirty.left; x < c->dirty.right; x++) {
                c->pixels[y * c->width + x] = 0x00FF3366u;
            }
        }
        st->phase = 6;
        PostMessageW((HWND)w->native, WM_KEYDOWN, 'B', 0);
        return;
    }

    if (st->phase == 6) {
        st->exposure_was_partial = (!c->full_redraw && c->dirty_count == 1 &&
            c->dirty.left == 30 && c->dirty.top == 20 &&
            c->dirty.right == 90 && c->dirty.bottom == 50);
        for (int y = c->dirty.top; y < c->dirty.bottom; y++) {
            for (int x = c->dirty.left; x < c->dirty.right; x++) {
                c->pixels[y * c->width + x] = 0x00AABBCCu;
            }
        }
        st->phase = 7;
        PostMessageW((HWND)w->native, WM_KEYDOWN, 'C', 0);
        return;
    }

    if (st->phase == 7) {
        st->resize_was_full = c->full_redraw;
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                c->pixels[y * c->width + x] = asym_pixel(x, y);
            }
        }
        st->phase = 8;
        PostMessageW((HWND)w->native, WM_KEYDOWN, 'D', 0);
        return;
    }

    if (st->phase == 9) {
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
            st->anim_woke_and_stopped = (st->tick_count > 1 && !w->frame_timer_active);
            st->phase = 10;
            PostMessageW((HWND)w->native, WM_CLOSE, 0, 0);
        }
    }
}

static bool win_event(im_window *w, const im_event *e) {
    win_test_state *st = (win_test_state *)w->user;
    if (st->phase == 1 && e->kind == IM_KEY_DOWN && e->key == '1') {
        /* Read presented pixels from window DC after first paint. */
        int lw = w->last_width, lh = w->last_height;
        uint32_t *scr = (uint32_t *)heap_alloc_zero((size_t)(lw * lh) * sizeof(uint32_t));
        if (scr && read_presented_pixels((HWND)w->native, lw, lh, scr)) {
            st->first_screen_verified =
                (scr[10 * lw + 10] == asym_pixel(10, 10)) &&
                (scr[80 * lw + 120] == asym_pixel(120, 80));
        }
        heap_free_ptr(scr);
        PostMessageW((HWND)w->native, WM_MOUSEMOVE, 0, MAKELPARAM(40, 32));
        return false;
    }
    if (st->phase == 1 && e->kind == IM_POINTER_MOVE) {
        if (e->x >= 20 && e->x < 100 && e->y >= 20 && e->y < 52 && !st->btn1_hovered) {
            st->btn1_hovered = true;
            im_window_invalidate(w, 20, 20, 100, 52);
            return true;
        }
        return false;
    }
    if (st->phase == 3 && e->kind == IM_KEY_DOWN && e->key == '2') {
        /* Read presented pixels after disjoint + gap exposure paint. */
        int lw = w->last_width, lh = w->last_height;
        uint32_t *scr = (uint32_t *)heap_alloc_zero((size_t)(lw * lh) * sizeof(uint32_t));
        if (scr && read_presented_pixels((HWND)w->native, lw, lh, scr)) {
            st->disjoint_gap_screen_verified =
                (scr[12 * lw + 12] == 0x0077DD44u) &&
                (scr[100 * lw + 190] == 0x0077DD44u) &&
                (scr[55 * lw + 80] == asym_pixel(80, 55));
        }
        heap_free_ptr(scr);
        /* Trigger nested reentrant event test. */
        PostMessageW((HWND)w->native, WM_KEYDOWN, 'N', 0);
        return false;
    }
    if (st->phase == 3 && e->kind == IM_KEY_DOWN && e->key == 'N') {
        /* Synchronously invoke inner event that invalidates a small rect, then return true for full repaint. */
        st->nested_send_ok = send_msg_sync((HWND)w->native, WM_MOUSEMOVE, 0, MAKELPARAM(50, 30));
        return true;
    }
    if (st->phase == 3 && e->kind == IM_POINTER_MOVE) {
        st->nested_inner_ran = true;
        im_window_invalidate(w, 20, 20, 40, 40);
        return true;
    }
    if (st->phase == 4 && e->kind == IM_KEY_DOWN && e->key == 'A') {
        return true;
    }
    if (st->phase == 6 && e->kind == IM_KEY_DOWN && e->key == 'B') {
        int lw = w->last_width, lh = w->last_height;
        uint32_t *scr = (uint32_t *)heap_alloc_zero((size_t)(lw * lh) * sizeof(uint32_t));
        if (scr && read_presented_pixels((HWND)w->native, lw, lh, scr)) {
            st->band_screen_verified =
                (scr[80 * lw + 50] == 0x00FF3366u) &&
                (scr[69 * lw + 50] == asym_pixel(50, 69)) &&
                (scr[90 * lw + 50] == asym_pixel(50, 90)) &&
                (scr[80 * lw + 15] == asym_pixel(15, 80)) &&
                (scr[80 * lw + 180] == asym_pixel(180, 80));
        }
        heap_free_ptr(scr);
        RECT exp_rc = {30, 20, 90, 50};
        InvalidateRect((HWND)w->native, &exp_rc, FALSE);
        return false;
    }
    if (st->phase == 7 && e->kind == IM_KEY_DOWN && e->key == 'C') {
        int lw = w->last_width, lh = w->last_height;
        uint32_t *scr = (uint32_t *)heap_alloc_zero((size_t)(lw * lh) * sizeof(uint32_t));
        if (scr && read_presented_pixels((HWND)w->native, lw, lh, scr)) {
            st->exposure_screen_verified =
                (scr[35 * lw + 60] == 0x00AABBCCu) &&
                (scr[19 * lw + 60] == asym_pixel(60, 19));
        }
        heap_free_ptr(scr);
        SetWindowPos((HWND)w->native, NULL, 0, 0, 320, 200,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return false;
    }
    if (st->phase == 8 && e->kind == IM_KEY_DOWN && e->key == 'D') {
        int lw = w->last_width, lh = w->last_height;
        uint32_t *scr = (uint32_t *)heap_alloc_zero((size_t)(lw * lh) * sizeof(uint32_t));
        bool match = (scr != NULL && lw > 0 && lh > 0 &&
                      read_presented_pixels((HWND)w->native, lw, lh, scr));
        for (int y = 0; y < lh && match; y += 7) {
            for (int x = 0; x < lw; x += 11) {
                if (scr[y * lw + x] != asym_pixel(x, y)) {
                    match = false;
                    break;
                }
            }
        }
        heap_free_ptr(scr);
        st->resize_screen_verified = match;

        /* Test stale delayed WM_TIMER against a replacement timer. */
        int before_ticks = st->tick_count;
        bool r1 = im_window_request_frame(w, 20);
        uint32_t old_tok = w->frame_token;
        bool r2 = im_window_request_frame(w, 30);
        uint32_t new_tok = w->frame_token;
        st->waiting_replacement_tick = true;
        /* Deliver stale WM_TIMER synchronously for old_tok: must NOT call tick or cancel new_tok. */
        bool sent_stale = send_msg_sync((HWND)w->native, WM_TIMER, (WPARAM)old_tok, 0);
        st->stale_timer_ignored = (r1 && r2 && sent_stale && new_tok != old_tok &&
            st->tick_count == before_ticks && w->frame_timer_active && w->frame_token == new_tok);
        return false;
    }
    return false;
}

static void win_tick(im_window *w) {
    win_test_state *st = (win_test_state *)w->user;
    st->tick_count++;
    if (st->waiting_replacement_tick) {
        st->waiting_replacement_tick = false;
        st->replacement_timer_fired = true;
        /* Start Phase 9: real im_state_machine animation. */
        st->phase = 9;
        st->anim_start_tick = im_host_now_ms(&st->host);
        im_states_go_to(&st->sm, "On", 0);
        im_frame_step(&st->sched, &st->dirty, im_states_is_animating(&st->sm, 0),
                      0, win_sched_request, win_sched_cancel, w);
        return;
    }
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
    CHECK(!im_window_cancel_frame(&w), "cancel_frame before open returns false");

    CHECK(im_window_run(&w, false), "im_window_run succeeds");

    CHECK(w.native == NULL, "window->native cleared to NULL after close");
    CHECK(!im_window_invalidate(&w, 0, 0, 10, 10), "invalidate after close returns false");
    CHECK(!im_window_request_frame(&w, 16), "request_frame after close returns false");
    CHECK(!im_window_cancel_frame(&w), "cancel_frame after close returns false");

    CHECK(st.first_was_full && st.first_screen_verified,
          "first paint sets full_redraw == true and presented DC pixels match");
    CHECK(st.req_zero_ok && st.cancel_while_open_ok && st.stale_ticks_observed == 0,
          "cancelled 0 ms wake-up succeeded, cancelled cleanly, and never invoked tick");
    CHECK(st.hover_exact_rect,
          "WM_MOUSEMOVE hover produced exact button 1 dirty rect [20, 100) x [20, 52)");
    CHECK(st.hover_pixels_outside_untouched,
          "WM_MOUSEMOVE hover left pixels outside button 1 untouched");
    CHECK(st.disjoint_preserved && st.disjoint_gap_sentinel_seen && st.disjoint_gap_screen_verified,
          "disjoint rects preserved AND sentinel-corrupted gap exposure restored on presented DC");
    CHECK(st.nested_send_ok && st.nested_inner_ran && st.nested_outer_got_full,
          "nested explicit damage inside inner event runs and does not suppress outer full repaint");
    CHECK(st.legacy_was_full,
          "legacy event handler returning true without explicit damage gets full_redraw == true");
    CHECK(st.band_was_partial && st.band_screen_verified,
          "explicit horizontal band paint sets exact rect and presented DC pixels match");
    CHECK(st.exposure_was_partial && st.exposure_screen_verified,
          "independent exposure repaint updates only exposed rect and presented DC pixels match");
    CHECK(st.resize_was_full && st.resize_screen_verified,
          "stride-changing resize triggers full_redraw == true and presented DC pixels match");
    CHECK(st.stale_timer_ignored && st.replacement_timer_fired,
          "stale delayed WM_TIMER ignored without cancelling replacement timer which fires");
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
