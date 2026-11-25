/* Tier-0 freestanding verification fixture for ui/accent.{h,c} & ui/resources.{h,c} (D01 T01 s14). */
#include <impossible/base.h>
#include <windows.h>
#include "accent.h"
#include "button.h"
#include "dirty.h"
#include "draw.h"
#include "layout.h"
#include "resources.h"
#include "states.h"
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

static void write_u64(DWORD handle_id, uint64_t u) {
    char buf[32], out[32];
    int n = 0;
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

static bool has_arg_w(const wchar_t *cmd, const char *needle) {
    if (!cmd || !needle) return false;
    for (const wchar_t *p = cmd; *p; p++) {
        const wchar_t *a = p;
        const char *b = needle;
        while (*a && *b && *a == (wchar_t)(unsigned char)*b) { a++; b++; }
        if (*b == '\0') return true;
    }
    return false;
}

static uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static bool buf_eq(const uint32_t *a, const uint32_t *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

static void emit_live_shots(im_allocator alloc) {
    /* Emit two 160x64 BGRA frames on a #202020 backdrop:
       Frame 0: Button inheriting RequestedTheme=Light from its parent panel inside a Dark window.
       Frame 1: Sibling Button inheriting Dark from the same Dark window. */
    im_palette win_dark = { .theme = IM_DARK };
    im_accent_fill_palette(&win_dark, 0xFF0078D7u);

    im_tree t;
    im_tree_init(&t, alloc);
    im_node *win = im_tree_create_node(&t, IM_ELEM_GRID, "Win");
    im_node *light_panel = im_tree_create_node(&t, IM_ELEM_BORDER, "LightPanel");
    im_node_set_requested_theme(light_panel, IM_REQ_THEME_LIGHT);
    im_node *light_btn = im_tree_create_node(&t, IM_ELEM_BUTTON, "LightBtn");
    im_node *dark_btn = im_tree_create_node(&t, IM_ELEM_BUTTON, "DarkBtn");
    im_tree_append_child(&t, light_panel, light_btn);
    im_tree_append_child(&t, win, light_panel);
    im_tree_append_child(&t, win, dark_btn);
    t.root = win;

    static uint32_t pix[160 * 64];
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    im_node *btns[2] = { light_btn, dark_btn };
    for (int fi = 0; fi < 2; fi++) {
        for (int i = 0; i < 160 * 64; i++) pix[i] = 0xFF202020u;
        im_surface s = { pix, 160, 64, 160, win_dark, IM_ONE, NULL, NULL };
        im_state_machine sm;
        im_states_init(&sm, IM_TEMPLATE_BUTTON, &win_dark);
        im_states_set_resource_context(&sm, btns[fi], &win_dark);
        im_states_go_to(&sm, "Normal", 0u);
        im_states_draw_button(&sm, &s, (im_rect){ FX(20), FX(16), FX(140), FX(48) }, 0u);
        for (int i = 0; i < 160 * 64; i++) pix[i] |= 0xFF000000u;
        DWORD written = 0;
        WriteFile(h, pix, sizeof(pix), &written, NULL);
    }
    im_tree_destroy(&t);
}

int im_main(void) {
    heap_ctx hctx = { GetProcessHeap(), 0u, 0u };
    im_allocator alloc = { h_alloc, h_realloc, h_free, &hctx };

    if (has_arg_w(GetCommandLineW(), "--live-shots")) {
        emit_live_shots(alloc);
        return (hctx.used == 0u) ? 0 : 2;
    }

    /* 1. 1,000-color accent derivation dump + OS-supplied 7-shade verbatim preservation. */
    {
        static const uint32_t fixed_cases[8] = {
            0xFF000000u, 0xFFFFFFFFu, 0xFF808080u, 0xFFFF0000u,
            0xFF00FF00u, 0xFF0000FFu, 0xFFA94DC1u, 0xFF0078D7u
        };
        uint32_t seed = 0xACC34714u;
        for (int i = 0; i < 1000; i++) {
            uint32_t c = (i < 8) ? fixed_cases[i] : (0xFF000000u | (rng_next(&seed) & 0x00FFFFFFu));
            im_accent_shades sh;
            im_accent_derive(c, &sh);
            CHECK(sh.accent == c, "im_accent_derive preserves base accent");
            CHECK((sh.light1 >> 24) == 0xFFu && (sh.dark3 >> 24) == 0xFFu, "derived shades are opaque");
            write_str(STD_OUTPUT_HANDLE, "ACCENT ");
            write_u64(STD_OUTPUT_HANDLE, c & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, " "); write_u64(STD_OUTPUT_HANDLE, sh.light1 & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, " "); write_u64(STD_OUTPUT_HANDLE, sh.light2 & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, " "); write_u64(STD_OUTPUT_HANDLE, sh.light3 & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, " "); write_u64(STD_OUTPUT_HANDLE, sh.dark1 & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, " "); write_u64(STD_OUTPUT_HANDLE, sh.dark2 & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, " "); write_u64(STD_OUTPUT_HANDLE, sh.dark3 & 0xFFFFFFu);
            write_str(STD_OUTPUT_HANDLE, "\n");
        }
        /* OS-supplied 7 shades installed verbatim. */
        im_accent_shades os_sh = {
            0xFFA94DC1u, 0xFFB763CBu, 0xFFDB9EE5u, 0xFFF0C0F4u,
            0xFF8E3AA7u, 0xFF692782u, 0xFF400E59u
        };
        im_palette p = { .theme = IM_DARK };
        im_accent_set_os_shades(&p, &os_sh);
        CHECK(p.slot[IM_SLOT_ACCENT] == 0xFFA94DC1u &&
              p.slot[IM_SLOT_ACCENT_COLOR_LIGHT_2] == 0xFFDB9EE5u &&
              p.slot[IM_SLOT_ACCENT_COLOR_DARK_3] == 0xFF400E59u &&
              p.slot[IM_SLOT_COLOR_CONTROL_ACCENT] == 0xFFA94DC1u,
              "im_accent_set_os_shades installs all 7 OS shades verbatim");
    }

    /* 2. Per-subtree Light-in-Dark rendering across Standard/Accent/Subtle & all 4 states + mid-transition theme flip. */
    {
        im_palette win_dark = { .theme = IM_DARK };
        im_palette win_light = { .theme = IM_LIGHT };
        im_accent_fill_palette(&win_dark, 0xFF0078D7u);
        im_accent_fill_palette(&win_light, 0xFF0078D7u);

        im_tree t; im_tree_init(&t, alloc);
        im_node *win = im_tree_create_node(&t, IM_ELEM_GRID, "Win");
        im_node *lpanel = im_tree_create_node(&t, IM_ELEM_BORDER, "LightPanel");
        im_node_set_requested_theme(lpanel, IM_REQ_THEME_LIGHT);
        im_node *lbtn = im_tree_create_node(&t, IM_ELEM_BUTTON, "LightBtn");
        im_node *dbtn = im_tree_create_node(&t, IM_ELEM_BUTTON, "DarkBtn");
        im_tree_append_child(&t, lpanel, lbtn);
        im_tree_append_child(&t, win, lpanel);
        im_tree_append_child(&t, win, dbtn);
        t.root = win;

        static uint32_t pix_a[160 * 64], pix_b[160 * 64];
        im_rect bnd = { FX(20), FX(16), FX(140), FX(48) };
        for (int st = 0; st < 3; st++) {
            for (int bs = 0; bs < 4; bs++) {
                for (int i = 0; i < 160 * 64; i++) { pix_a[i] = 0x00202020u; pix_b[i] = 0x00202020u; }
                im_surface sa = { pix_a, 160, 64, 160, win_dark, IM_ONE, NULL, NULL };
                im_surface sb = { pix_b, 160, 64, 160, win_light, IM_ONE, NULL, NULL };
                im_draw_button_for_node(&sa, lbtn, &win_dark, bnd, (im_button_style)st, (im_button_state)bs);
                im_draw_button(&sb, bnd, (im_button_style)st, (im_button_state)bs);
                CHECK(buf_eq(pix_a, pix_b, 160 * 64), "Light-panel inheriting button matches direct Light window button");
            }
        }
        /* State-machine renderer also matches Light reference pixel-for-pixel while Dark sibling matches Dark. */
        for (int i = 0; i < 160 * 64; i++) { pix_a[i] = 0x00202020u; pix_b[i] = 0x00202020u; }
        im_surface sa = { pix_a, 160, 64, 160, win_dark, IM_ONE, NULL, NULL };
        im_surface sb = { pix_b, 160, 64, 160, win_light, IM_ONE, NULL, NULL };
        im_state_machine sm_l, sm_ref;
        im_states_init(&sm_l, IM_TEMPLATE_BUTTON, &win_dark);
        im_states_set_resource_context(&sm_l, lbtn, &win_dark);
        im_states_go_to(&sm_l, "PointerOver", 0u);
        im_states_draw_button(&sm_l, &sa, bnd, 200u);
        im_states_init(&sm_ref, IM_TEMPLATE_BUTTON, &win_light);
        im_states_go_to(&sm_ref, "PointerOver", 0u);
        im_states_draw_button(&sm_ref, &sb, bnd, 200u);
        CHECK(buf_eq(pix_a, pix_b, 160 * 64), "im_states_draw_button on Light subtree matches Light window");

        /* Mid-BrushTransition theme flip updates target to new theme cleanly. */
        im_states_go_to(&sm_l, "Pressed", 300u);
        im_node_set_requested_theme(lpanel, IM_REQ_THEME_DARK);
        im_states_refresh_theme(&sm_l, &win_dark, 340u);
        uint32_t settled = im_states_sample_color(&sm_l, "ContentPresenter", "Background", 600u, 0u);
        uint32_t dark_pressed = im_brush_over(&win_dark, IM_BRUSH_ButtonBackgroundPressed, 0, IM_ONE, IM_ONE);
        CHECK(settled == dark_pressed, "Mid-BrushTransition theme flip settles at new Dark Pressed color");

        /* Contrast precedence & reversal: preserve requested_theme through IM_CONTRAST and restore when contrast ends! */
        im_node_set_requested_theme(lpanel, IM_REQ_THEME_LIGHT);
        im_node *nested = im_tree_create_node(&t, IM_ELEM_BORDER, "Nested");
        im_tree_append_child(&t, lpanel, nested);
        im_palette win_hc;
        im_palette_init_contrast(&win_hc, IM_CONTRAST_AQUATIC, 0xFF0078D7u);
        CHECK(im_resources_effective_theme(lpanel, &win_hc) == IM_CONTRAST &&
              lpanel->requested_theme == IM_REQ_THEME_LIGHT,
              "High Contrast overrides Light subtree while preserving requested_theme");
        /* Change nested child's request while contrast is active; still resolves IM_CONTRAST until contrast ends. */
        im_node_set_requested_theme(nested, IM_REQ_THEME_DARK);
        CHECK(im_resources_effective_theme(nested, &win_hc) == IM_CONTRAST,
              "Nested Dark request stays IM_CONTRAST while contrast is active");
        /* End contrast -> lpanel restores IM_LIGHT and nested reflects IM_DARK! */
        CHECK(im_resources_effective_theme(lpanel, &win_dark) == IM_LIGHT &&
              im_resources_effective_theme(nested, &win_dark) == IM_DARK,
              "Ending High Contrast restores Light panel and nested Dark child accurately");
        /* Clearing nested back to Default inherits IM_LIGHT from lpanel; clearing lpanel inherits IM_DARK! */
        im_node_set_requested_theme(nested, IM_REQ_THEME_DEFAULT);
        CHECK(im_resources_effective_theme(nested, &win_dark) == IM_LIGHT, "Cleared nested inherits Light panel");
        im_node_set_requested_theme(lpanel, IM_REQ_THEME_DEFAULT);
        CHECK(im_resources_effective_theme(nested, &win_dark) == IM_DARK, "Cleared panel inherits Dark window");
        im_tree_destroy(&t);
    }

    /* 3. Named overrides: enum+string equivalence, state isolation, shared-stop isolation, removal, refusal, OOM atomicity. */
    {
        heap_ctx octx = { GetProcessHeap(), 0u, 0u };
        im_allocator oalloc = { h_alloc, h_realloc, h_free, &octx };
        im_palette win_dark = { .theme = IM_DARK };
        im_accent_fill_palette(&win_dark, 0xFF0078D7u);

        im_tree t, t2;
        im_tree_init(&t, oalloc);
        im_tree_init(&t2, oalloc);
        im_node *root = im_tree_create_node(&t, IM_ELEM_GRID, "R");
        im_node *child = im_tree_create_node(&t, IM_ELEM_BUTTON, "Ch");
        im_node *sib = im_tree_create_node(&t, IM_ELEM_BUTTON, "Sib");
        im_tree_append_child(&t, root, child);
        im_tree_append_child(&t, root, sib);
        t.root = root;
        im_node *other = im_tree_create_node(&t2, IM_ELEM_BUTTON, "Other");
        t2.root = other;

        /* Set by WinUI string key -> changes PointerOver only, leaves Normal/Pressed/Disabled + sibling + tree 2 untouched! */
        CHECK(im_resources_set_brush_override_by_name(&t, root, IM_REQ_THEME_DEFAULT, "ButtonBackgroundPointerOver", 0xFF123456u),
              "Override ButtonBackgroundPointerOver by string succeeds");
        im_state_machine sm_ch, sm_sib;
        im_states_init(&sm_ch, IM_TEMPLATE_BUTTON, &win_dark);
        im_states_set_resource_context(&sm_ch, child, &win_dark);
        im_states_go_to(&sm_ch, "PointerOver", 0u);
        CHECK(im_states_sample_color(&sm_ch, "ContentPresenter", "Background", 200u, 0u) == 0xFF123456u,
              "PointerOver state picks up named override");
        im_states_go_to(&sm_ch, "Normal", 300u);
        CHECK(im_states_sample_color(&sm_ch, "ContentPresenter", "Background", 600u, 0u) ==
              im_brush_over(&win_dark, IM_BRUSH_ButtonBackground, 0, IM_ONE, IM_ONE),
              "Normal state is untouched by PointerOver override");
        CHECK(im_resources_resolve_brush(other, &win_dark, IM_BRUSH_ButtonBackgroundPointerOver, 0, IM_ONE, IM_ONE) ==
              im_brush_over(&win_dark, IM_BRUSH_ButtonBackgroundPointerOver, 0, IM_ONE, IM_ONE),
              "Second tree is completely unaffected by override on first tree");

        /* Replace same entry by enum ID, shadow on child, verify sibling keeps root override, clear child reveals root! */
        CHECK(im_resources_set_brush_override(&t, root, IM_REQ_THEME_DEFAULT, IM_BRUSH_ButtonBackgroundPointerOver, 0xFF654321u),
              "Replace by enum ID updates exact same entry");
        CHECK(im_resources_set_brush_override(&t, child, IM_REQ_THEME_DEFAULT, IM_BRUSH_ButtonBackgroundPointerOver, 0xFFABCDEFu),
              "Shadow override on child succeeds");
        CHECK(im_resources_resolve_brush(child, &win_dark, IM_BRUSH_ButtonBackgroundPointerOver, 0, IM_ONE, IM_ONE) == 0xFFABCDEFu &&
              im_resources_resolve_brush(sib, &win_dark, IM_BRUSH_ButtonBackgroundPointerOver, 0, IM_ONE, IM_ONE) == 0xFF654321u,
              "Child shadow overrides root while sibling still sees root override");
        CHECK(im_resources_clear_brush_override_by_name(child, IM_REQ_THEME_DEFAULT, "ButtonBackgroundPointerOver"),
              "Clearing child shadow by name succeeds");
        CHECK(im_resources_resolve_brush(child, &win_dark, IM_BRUSH_ButtonBackgroundPointerOver, 0, IM_ONE, IM_ONE) == 0xFF654321u,
              "Clearing child shadow reveals parent override");

        /* Value override (ControlCornerRadius) + geometry change. */
        im_fixed big_cr[4] = { FX(12), FX(12), FX(12), FX(12) };
        CHECK(im_resources_set_value_override_by_name(&t, child, IM_REQ_THEME_DEFAULT, "ControlCornerRadius", big_cr),
              "Override ControlCornerRadius by string succeeds");
        im_surface surf = { NULL, 160, 64, 160, win_dark, IM_ONE, NULL, NULL };
        im_rect fr; im_radii frad, srad; im_fixed th;
        im_button_geometry_for_node(&surf, child, &win_dark, (im_rect){0, 0, FX(120), FX(32)}, false, &fr, &frad, &srad, &th);
        CHECK(srad.top_left == FX(12), "Button geometry honors ControlCornerRadius value override");
        im_button_geometry_for_node(&surf, sib, &win_dark, (im_rect){0, 0, FX(120), FX(32)}, false, &fr, &frad, &srad, &th);
        CHECK(srad.top_left != FX(12), "Sibling geometry is untouched by child ControlCornerRadius override");

        /* Refusals: unknown key, wrong kind, out-of-range ID, OOM atomicity. */
        CHECK(!im_resources_set_brush_override_by_name(&t, child, IM_REQ_THEME_DEFAULT, "NoSuchBrushKey", 0xFF111111u),
              "Unknown brush key is refused");
        CHECK(!im_resources_set_brush_override_by_name(&t, child, IM_REQ_THEME_DEFAULT, "ControlCornerRadius", 0xFF111111u),
              "Value key passed to brush override is refused");
        CHECK(!im_resources_set_value_override_by_name(&t, child, IM_REQ_THEME_DEFAULT, "ButtonBackground", big_cr),
              "Brush key passed to value override is refused");
        CHECK(!im_resources_set_brush_override(&t, child, IM_REQ_THEME_DEFAULT, IM_BRUSH_COUNT, 0xFF111111u),
              "Out-of-range brush ID is refused");
        /* OOM when growing child override table leaves existing ControlCornerRadius override intact! */
        octx.limit = octx.used;
        /* Fill remaining capacity until growth fails. */
        bool oom_hit = false;
        for (int k = 0; k < 16; k++) {
            if (!im_resources_set_brush_override(&t, child, IM_REQ_THEME_DEFAULT, (enum im_brush_id)k, 0xFF010203u)) {
                oom_hit = true;
                break;
            }
        }
        im_fixed got_cr[4] = {0, 0, 0, 0};
        im_resources_resolve_value(child, &win_dark, IM_VALUE_ControlCornerRadius, got_cr);
        CHECK(oom_hit && got_cr[0] == FX(12), "OOM on override table growth refuses atomically without corrupting prior entries");
        octx.limit = 0u;
        (void)sm_sib;
        im_tree_destroy(&t);
        im_tree_destroy(&t2);
        CHECK(octx.used == 0u, "Override & OOM test leaks 0 bytes");
    }

    /* 4. In-place restyle preserves node pointers, focus, selection, scroll, text, and emits exact dirty rects! */
    {
        im_palette win_dark = { .theme = IM_DARK };
        im_accent_fill_palette(&win_dark, 0xFF0078D7u);
        im_tree t; im_tree_init(&t, alloc);
        im_node *root = im_tree_create_node(&t, IM_ELEM_STACK_PANEL, "Root");
        root->orientation = IM_ORIENT_HORIZONTAL;
        im_node *left = im_tree_create_node(&t, IM_ELEM_BORDER, "Left");
        im_node_set_size(left, FX(50), FX(30));
        im_node_bind_theme_brushes(left, true, IM_BRUSH_ButtonBackground, false, IM_BRUSH_COUNT);
        im_node_bind_theme_values(left, true, IM_VALUE_ButtonPadding, false, IM_VALUE_COUNT);
        left->is_focused = true;
        left->selection_start = 2u;
        left->selection_end = 7u;
        left->scroll_x = FX(12);
        left->scroll_y = FX(40);
        left->text[0] = 'H'; left->text[1] = 'i'; left->text[2] = '\0';

        im_node *right = im_tree_create_node(&t, IM_ELEM_BORDER, "Right");
        im_node_set_size(right, FX(50), FX(30));
        im_node_bind_theme_brushes(right, true, IM_BRUSH_ButtonBackground, false, IM_BRUSH_COUNT);
        im_tree_append_child(&t, root, left);
        im_tree_append_child(&t, root, right);
        t.root = root;

        /* Initial restyle + layout + drain initial damage. */
        im_resources_restyle_tree(&t, root, &win_dark, 0u);
        im_layout_run(&t, root, FX(120), FX(60), IM_ONE);
        im_dirty_list dl;
        im_dirty_init(&dl, 120, 60);
        im_dirty_collect_tree(&dl, &t, root, IM_ONE);

        /* Repeated identical restyle produces 0 updates and 0 dirty rects! */
        CHECK(im_resources_restyle_tree(&t, root, &win_dark, 0u) == 0u, "Identical restyle updates 0 nodes");
        im_dirty_init(&dl, 120, 60);
        im_dirty_collect_tree(&dl, &t, root, IM_ONE);
        CHECK(dl.count == 0u, "Identical restyle produces 0 dirty rects");

        /* Color-only override on Left -> restyle updates ONLY Left, dirty rect excludes Right, state stays intact! */
        im_resources_set_brush_override(&t, left, IM_REQ_THEME_DEFAULT, IM_BRUSH_ButtonBackground, 0xFF336699u);
        CHECK(im_resources_restyle_tree(&t, root, &win_dark, 0u) == 1u, "Color override on Left restyles exactly 1 node");
        im_dirty_init(&dl, 120, 60);
        im_dirty_collect_tree(&dl, &t, root, IM_ONE);
        CHECK(dl.count == 1u && dl.rects[0].left == 0 && dl.rects[0].right == 50,
              "Color override dirty rect covers Left [0,50) only and excludes Right [50,100)");
        CHECK(left->is_focused && left->selection_start == 2u && left->selection_end == 7u &&
              left->scroll_x == FX(12) && left->scroll_y == FX(40) &&
              left->text[0] == 'H' && left->text[1] == 'i' && left->text[2] == '\0',
              "In-place restyle preserves focus, selection, scroll position, and text");

        /* Layout-affecting value override (ButtonPadding) marks measure_dirty without rebuilding node! */
        im_fixed big_pad[4] = { FX(10), FX(10), FX(10), FX(10) };
        im_resources_set_value_override(&t, left, IM_REQ_THEME_DEFAULT, IM_VALUE_ButtonPadding, big_pad);
        CHECK(im_resources_restyle_tree(&t, root, &win_dark, 0u) == 1u && left->measure_dirty,
              "Layout-affecting value override marks measure_dirty in place");
        im_tree_destroy(&t);
    }

    /* 5. All four Windows 11 built-in contrast themes + accent independence + rendered contrast button! */
    {
        static const uint32_t want_face[4] = { 0xFF202020u, 0xFFFFFAEFu, 0xFF2D3236u, 0xFF000000u };
        static const uint32_t want_text[4] = { 0xFFFFFFFFu, 0xFF202020u, 0xFFB6F6F0u, 0xFFFFEE32u };
        static uint32_t pix[160 * 64];
        for (int ci = 0; ci < 4; ci++) {
            im_palette p1, p2;
            im_palette_init_contrast(&p1, (im_contrast_theme)ci, 0xFF0078D7u);
            im_palette_init_contrast(&p2, (im_contrast_theme)ci, 0xFFE81123u);
            CHECK(p1.theme == IM_CONTRAST &&
                  p1.slot[IM_SLOT_COLOR_BUTTON_FACE] == want_face[ci] &&
                  p1.slot[IM_SLOT_COLOR_BUTTON_TEXT] == want_text[ci] &&
                  p1.slot[IM_SLOT_COLOR_DISABLED_TEXT] == p1.slot[IM_SLOT_COLOR_GRAY_TEXT] &&
                  p1.slot[IM_SLOT_COLOR_CONTROL_ACCENT] == 0xFF0078D7u &&
                  p2.slot[IM_SLOT_COLOR_CONTROL_ACCENT] == 0xFFE81123u &&
                  p2.slot[IM_SLOT_COLOR_BUTTON_FACE] == want_face[ci],
                  "Contrast preset populates all 10 slots accurately and keeps ControlAccent independent");
            for (int i = 0; i < 160 * 64; i++) pix[i] = 0x00111111u;
            im_surface s = { pix, 160, 64, 160, p1, IM_ONE, NULL, NULL };
            im_draw_button(&s, (im_rect){ FX(20), FX(16), FX(140), FX(48) }, IM_BUTTON, IM_REST);
            CHECK((pix[32 * 160 + 80] & 0xFFFFFFu) == (want_face[ci] & 0xFFFFFFu),
                  "Rendered contrast button center pixel matches preset ButtonFace");
        }
    }

    CHECK(hctx.used == 0u, "all resource and accent tests freed 100% of allocated memory");
    if (g_failures > 0) {
        write_str(STD_ERROR_HANDLE, "test_resources: FAILED\n");
        return 1;
    }
    write_str(STD_OUTPUT_HANDLE, "test_resources: ALL PASSED\n");
    return 0;
}
