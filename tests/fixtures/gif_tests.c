/* Impossible SDK: GIF decoder unit tests + persistent --batch test runner (Tier 0, Windows). */
#include <impossible/base.h>
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gif.h"

static int g_fails = 0;

static void write_str(const char *s) {
    DWORD n = 0;
    while (s[n]) n++;
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

static void check(bool cond, const char *msg) {
    if (!cond) {
        write_str("FAIL: ");
        write_str(msg);
        write_str("\n");
        g_fails++;
    }
}

typedef struct alloc_rec {
    void *ptr;
    size_t size;
} alloc_rec;

typedef struct track_alloc {
    HANDLE heap;
    size_t alloc_calls;
    size_t free_calls;
    size_t live_blocks;
    size_t live_bytes;
    size_t mismatch_frees;
    int fail_on_ordinal; /* -1 = never fail */
    alloc_rec recs[2048];
    size_t rec_count;
} track_alloc;

static void *t_alloc(void *ctx, size_t size) {
    track_alloc *t = (track_alloc *)ctx;
    int ord = (int)t->alloc_calls;
    t->alloc_calls++;
    if (t->fail_on_ordinal >= 0 && ord == t->fail_on_ordinal) return NULL;
    void *p = HeapAlloc(t->heap, 0, size ? size : 1u);
    if (p) {
        t->live_blocks++;
        t->live_bytes += size;
        if (t->rec_count < 2048u) {
            t->recs[t->rec_count].ptr = p;
            t->recs[t->rec_count].size = size;
            t->rec_count++;
        }
    }
    return p;
}

static void t_free(void *ctx, void *ptr, size_t size) {
    track_alloc *t = (track_alloc *)ctx;
    if (!ptr) return;
    t->free_calls++;
    bool found = false;
    for (size_t i = 0; i < t->rec_count; i++) {
        if (t->recs[i].ptr == ptr) {
            if (t->recs[i].size != size) t->mismatch_frees++;
            t->recs[i] = t->recs[--t->rec_count];
            found = true;
            break;
        }
    }
    if (!found) t->mismatch_frees++;
    t->live_blocks--;
    t->live_bytes -= size;
    HeapFree(t->heap, 0, ptr);
}

static bool image_is_zero(const im_image *img) {
    return img->width == 0u && img->height == 0u && img->stride == 0u &&
           img->format == IM_PIXEL_BGRA8 && img->pixels == NULL && img->byte_size == 0u;
}

static bool anim_is_zero(const im_gif_anim *a) {
    return a->width == 0u && a->height == 0u && a->frame_count == 0u &&
           a->frame_cap == 0u && a->loop_count == 0u && !a->has_loop_count && a->frames == NULL;
}

static void run_unit_tests(void) {
    HANDLE heap = GetProcessHeap();
    static track_alloc tr;
    tr.heap = heap;
    tr.alloc_calls = 0; tr.free_calls = 0; tr.live_blocks = 0; tr.live_bytes = 0;
    tr.mismatch_frees = 0; tr.fail_on_ordinal = -1; tr.rec_count = 0;
    im_gif_alloc alloc = {t_alloc, t_free, &tr};

    /* 1. Largest GIF header (65535 x 65535) against 16 MiB limit -> IM_IMAGE_TOO_LARGE, 0 allocs. */
    uint8_t max_hdr[13] = {
        'G', 'I', 'F', '8', '9', 'a',
        0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x80u, 0x00u, 0x00u
    };
    im_image_limits lim16 = {65535u, 65535u, 16u * 1024u * 1024u};
    im_gif_info info;
    info.width = 999u;
    check(im_gif_probe(max_hdr, 13u, &lim16, &info) == IM_IMAGE_TOO_LARGE, "max header probe -> TOO_LARGE");
    check(info.width == 0u && info.byte_size == 0u, "probe zeroes out_info on failure");

    im_image img;
    img.width = 123u;
    img.pixels = (uint8_t *)(uintptr_t)0x1234u;
    check(im_gif_decode(max_hdr, 13u, IM_PIXEL_RGBA8, &lim16, &alloc, &img) == IM_IMAGE_TOO_LARGE, "max header decode -> TOO_LARGE");
    im_gif_anim anim;
    anim.frame_count = 9u;
    check(im_gif_decode_anim(max_hdr, 13u, IM_PIXEL_RGBA8, &lim16, 0u, &alloc, &anim) == IM_IMAGE_TOO_LARGE, "max header anim -> TOO_LARGE");
    check(tr.alloc_calls == 0u, "max header refusals make 0 allocations");
    check(image_is_zero(&img) && anim_is_zero(&anim), "refusals zero prefilled outputs");

    /* 2. Independent width, height, byte-budget limits (0 allocs), exact 13B probe boundary, NULL limits, len==0. */
    uint8_t h100[13] = {'G', 'I', 'F', '8', '7', 'a', 100u, 0u, 50u, 0u, 0x81u, 1u, 0u};
    im_image_limits lim_w = {99u, 1000u, 16u * 1024u * 1024u};
    im_image_limits lim_h = {1000u, 49u, 16u * 1024u * 1024u};
    im_image_limits lim_b = {1000u, 1000u, 100u * 50u * 4u - 1u};
    im_image_limits lim_exact = {100u, 50u, 100u * 50u * 4u};
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, &lim_w, &alloc, &img) == IM_IMAGE_TOO_LARGE, "width limit");
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, &lim_h, &alloc, &img) == IM_IMAGE_TOO_LARGE, "height limit");
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, &lim_b, &alloc, &img) == IM_IMAGE_TOO_LARGE, "byte-budget limit");
    check(tr.alloc_calls == 0u, "width/height/byte refusals make 0 allocations");

    check(im_gif_probe(h100, 12u, NULL, &info) == IM_IMAGE_TRUNCATED, "12B probe -> TRUNCATED");
    check(im_gif_probe(h100, 13u, &lim_exact, &info) == IM_IMAGE_OK, "13B probe -> OK at exact limit");
    check(info.width == 100u && info.height == 50u && info.has_gct && info.gct_size == 4u &&
          info.bg_index == 1u && !info.is_gif89a && info.byte_size == 20000u, "probe fields accurate");
    check(im_gif_probe(h100, 13u, NULL, &info) == IM_IMAGE_OK, "probe NULL limits OK");
    check(im_gif_probe(h100, 0u, NULL, &info) == IM_IMAGE_TRUNCATED, "non-NULL len 0 -> TRUNCATED");

    /* 3. Public argument validation. */
    im_gif_alloc bad_a1 = {NULL, t_free, &tr};
    im_gif_alloc bad_a2 = {t_alloc, NULL, &tr};
    check(im_gif_probe(NULL, 0u, NULL, &info) == IM_IMAGE_BAD_ARG, "probe NULL data -> BAD_ARG");
    check(im_gif_probe(h100, 13u, NULL, NULL) == IM_IMAGE_BAD_ARG, "probe NULL out -> BAD_ARG");
    check(im_gif_decode(NULL, 13u, IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_BAD_ARG, "decode NULL data -> BAD_ARG");
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, NULL, &alloc, NULL) == IM_IMAGE_BAD_ARG, "decode NULL out -> BAD_ARG");
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, NULL, NULL, &img) == IM_IMAGE_BAD_ARG, "decode NULL alloc -> BAD_ARG");
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, NULL, &bad_a1, &img) == IM_IMAGE_BAD_ARG, "decode NULL alloc fn -> BAD_ARG");
    check(im_gif_decode(h100, 13u, IM_PIXEL_RGBA8, NULL, &bad_a2, &img) == IM_IMAGE_BAD_ARG, "decode NULL free fn -> BAD_ARG");
    check(im_gif_decode(h100, 13u, (im_pixel_format)99, NULL, &alloc, &img) == IM_IMAGE_BAD_ARG, "decode bad format -> BAD_ARG");
    check(im_gif_decode_anim(h100, 13u, IM_PIXEL_RGBA8, NULL, 0u, &alloc, NULL) == IM_IMAGE_BAD_ARG, "anim NULL out -> BAD_ARG");

    /* 4. Minimal valid 1x1 GIF89a (pixel R=0x12, G=0x34, B=0x56): OOM ordinals across single & anim, 0 leaks & 0 size mismatches. */
    uint8_t one[35] = {
        'G','I','F','8','9','a',
        1u, 0u, 1u, 0u, 0x80u, 0u, 0u,
        0x12u, 0x34u, 0x56u, 0x00u, 0x00u, 0x00u, /* 2-entry GCT */
        0x2Cu, 0u, 0u, 0u, 0u, 1u, 0u, 1u, 0u, 0x00u,
        2u, 2u, 0x44u, 0x01u, 0u, /* min_cs=2, LZW: Clear(4), idx 0, EOI(5) */
        0x3Bu
    };
    for (int ord = 0; ord < 2; ord++) {
        tr.alloc_calls = 0u;
        tr.fail_on_ordinal = ord;
        img.width = 77u;
        check(im_gif_decode(one, sizeof(one), IM_PIXEL_RGBA8, NULL, &alloc, &img) == IM_IMAGE_NO_MEMORY, "single OOM -> NO_MEMORY");
        check(image_is_zero(&img) && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u, "single OOM 0 leaks");
    }
    /* Sweep ALL allocation ordinals on a 5-frame disposal-3 GIF (covers lzw, canvas, prev_canvas,
       frame-array initial + growth realloc at frame 4, and per-frame pixel allocations). */
    uint8_t five[13 + 6 + 5 * (8 + 15) + 1];
    size_t fp = 0u;
    static const uint8_t five_hdr[19] = {
        'G','I','F','8','9','a', 1u, 0u, 1u, 0u, 0x80u, 0u, 0u,
        0x12u, 0x34u, 0x56u, 0xAAu, 0xBBu, 0xCCu
    };
    for (size_t i = 0; i < 19u; i++) five[fp++] = five_hdr[i];
    for (int f = 0; f < 5; f++) {
        static const uint8_t gce_d3[8] = {0x21u, 0xF9u, 4u, 0x0Cu, 1u, 0u, 0u, 0u}; /* disposal=3 */
        static const uint8_t img_1x1[15] = {0x2Cu, 0u, 0u, 0u, 0u, 1u, 0u, 1u, 0u, 0u, 2u, 2u, 0x44u, 0x01u, 0u};
        for (size_t i = 0; i < 8u; i++) five[fp++] = gce_d3[i];
        for (size_t i = 0; i < 15u; i++) five[fp++] = img_1x1[i];
    }
    five[fp++] = 0x3Bu;

    tr.alloc_calls = 0u; tr.fail_on_ordinal = -1; tr.mismatch_frees = 0u; tr.rec_count = 0u;
    check(im_gif_decode_anim(five, fp, IM_PIXEL_RGBA8, NULL, 0u, &alloc, &anim) == IM_IMAGE_OK, "5-frame d3 baseline OK");
    size_t total_allocs = tr.alloc_calls;
    im_gif_anim_free(&alloc, &anim);
    check(total_allocs >= 9u && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u, "5-frame d3 baseline clean");
    for (size_t ord = 0u; ord < total_allocs; ord++) {
        tr.alloc_calls = 0u; tr.fail_on_ordinal = (int)ord;
        tr.live_blocks = 0u; tr.live_bytes = 0u; tr.mismatch_frees = 0u; tr.rec_count = 0u;
        anim.frame_count = 77u;
        check(im_gif_decode_anim(five, fp, IM_PIXEL_RGBA8, NULL, 0u, &alloc, &anim) == IM_IMAGE_NO_MEMORY, "5-frame OOM -> NO_MEMORY");
        check(anim_is_zero(&anim) && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u, "5-frame OOM 0 leaks & exact sizes");
    }
    tr.fail_on_ordinal = -1;

    /* Success + exact free size check on both single and anim */
    check(im_gif_decode(one, sizeof(one), IM_PIXEL_BGRA8, NULL, &alloc, &img) == IM_IMAGE_OK, "1x1 BGRA8 OK");
    check(img.pixels[0] == 0x56u && img.pixels[1] == 0x34u && img.pixels[2] == 0x12u && img.pixels[3] == 0xFFu, "1x1 BGRA8 bytes");
    alloc.free(alloc.ctx, img.pixels, img.byte_size);
    check(tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u, "single free exact");

    check(im_gif_decode_anim(one, sizeof(one), IM_PIXEL_RGBA8, NULL, 0u, &alloc, &anim) == IM_IMAGE_OK, "1x1 anim RGBA8 OK");
    check(anim.frame_count == 1u && !anim.has_loop_count && anim.loop_count == 0u, "1x1 anim meta");
    im_gif_anim_free(&alloc, &anim);
    check(anim_is_zero(&anim) && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u, "anim free exact");
}

static bool read_exact(HANDLE h, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n) {
        DWORD rd = 0;
        if (!ReadFile(h, p + got, (DWORD)(n - got), &rd, NULL) || rd == 0) return false;
        got += rd;
    }
    return true;
}

static bool write_exact(HANDLE h, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n) {
        DWORD wr = 0;
        if (!WriteFile(h, p + sent, (DWORD)(n - sent), &wr, NULL) || wr == 0) return false;
        sent += wr;
    }
    return true;
}

static void run_batch(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE heap = GetProcessHeap();
    static track_alloc tr;
    tr.heap = heap;
    tr.fail_on_ordinal = -1;
    im_gif_alloc alloc = {t_alloc, t_free, &tr};

    uint8_t *in_buf = NULL;
    size_t in_cap = 0;

    for (;;) {
        /* [0]=op (0=quit, 1=single RGBA8, 2=single BGRA8, 3=anim RGBA8, 4=anim BGRA8, 5=probe, 6=prefixes),
           [1]=len, [2]=max_frames, [3]=max_bytes (0=default) */
        uint32_t hdr[4];
        if (!read_exact(hin, hdr, sizeof(hdr))) break;
        uint32_t op = hdr[0];
        uint32_t len = hdr[1];
        uint32_t max_frames = hdr[2];
        uint32_t max_bytes = hdr[3];
        if (op == 0u) break;
        if (len > in_cap) {
            if (in_buf) HeapFree(heap, 0, in_buf);
            in_buf = (uint8_t *)HeapAlloc(heap, 0, len ? len : 1u);
            in_cap = len ? len : 1u;
        }
        if (len > 0u && !read_exact(hin, in_buf, len)) break;

        im_image_limits lim = {0u, 0u, (size_t)max_bytes};
        const im_image_limits *lim_ptr = (max_bytes > 0u) ? &lim : NULL;

        if (op == 1u || op == 2u) {
            im_pixel_format fmt = (op == 2u) ? IM_PIXEL_BGRA8 : IM_PIXEL_RGBA8;
            im_image img;
            img.width = 0xDEADu;
            tr.live_blocks = 0u; tr.live_bytes = 0u; tr.mismatch_frees = 0u; tr.rec_count = 0u;
            im_image_error err = im_gif_decode(in_buf, len, fmt, lim_ptr, &alloc, &img);
            uint32_t resp[6];
            resp[0] = (uint32_t)err;
            resp[1] = img.width;
            resp[2] = img.height;
            resp[3] = img.stride;
            resp[4] = (uint32_t)img.byte_size;
            if (err == IM_IMAGE_OK) {
                uint32_t leak = (tr.live_blocks == 1u && tr.live_bytes == img.byte_size &&
                                 tr.mismatch_frees == 0u && img.format == fmt) ? 0u : 1u;
                resp[5] = leak;
                write_exact(hout, resp, sizeof(resp));
                if (img.byte_size > 0u) write_exact(hout, img.pixels, img.byte_size);
                alloc.free(alloc.ctx, img.pixels, img.byte_size);
            } else {
                resp[5] = (image_is_zero(&img) && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u) ? 0u : 1u;
                write_exact(hout, resp, sizeof(resp));
            }
        } else if (op == 3u || op == 4u) {
            im_pixel_format fmt = (op == 4u) ? IM_PIXEL_BGRA8 : IM_PIXEL_RGBA8;
            im_gif_anim anim;
            anim.frame_count = 0xDEADu;
            tr.live_blocks = 0u; tr.live_bytes = 0u; tr.mismatch_frees = 0u; tr.rec_count = 0u;
            /* When max_bytes == 0xFFFFFFFFu, test non-NULL limits with max_bytes == 0! */
            im_image_limits lim_zero = {0u, 0u, 0u};
            const im_image_limits *ap = (max_bytes == 0xFFFFFFFFu) ? &lim_zero : lim_ptr;
            im_image_error err = im_gif_decode_anim(in_buf, len, fmt, ap, max_frames, &alloc, &anim);
            uint32_t resp[7];
            resp[0] = (uint32_t)err;
            resp[1] = anim.width;
            resp[2] = anim.height;
            resp[3] = anim.frame_count;
            resp[4] = anim.has_loop_count ? 1u : 0u;
            resp[5] = anim.loop_count;
            if (err == IM_IMAGE_OK) {
                /* Verify all frame buffers + frames array are released cleanly with exact sizes! */
                resp[6] = (tr.mismatch_frees == 0u) ? 0u : 1u;
                write_exact(hout, resp, sizeof(resp));
                for (uint32_t i = 0u; i < anim.frame_count; i++) {
                    const im_gif_frame *fr = &anim.frames[i];
                    uint32_t fhdr[12] = {
                        fr->delay_ms, (uint32_t)fr->disposal, fr->user_input ? 1u : 0u,
                        (uint32_t)fr->left, (uint32_t)fr->top,
                        (uint32_t)fr->frame_width, (uint32_t)fr->frame_height,
                        fr->image.width, fr->image.height, (uint32_t)fr->image.format,
                        fr->image.stride, (uint32_t)fr->image.byte_size
                    };
                    write_exact(hout, fhdr, sizeof(fhdr));
                    if (fr->image.byte_size > 0u) write_exact(hout, fr->image.pixels, fr->image.byte_size);
                }
                im_gif_anim_free(&alloc, &anim);
                uint32_t post_free = (anim_is_zero(&anim) && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u) ? 0u : 1u;
                write_exact(hout, &post_free, sizeof(post_free));
            } else {
                resp[6] = (anim_is_zero(&anim) && tr.live_blocks == 0u && tr.live_bytes == 0u && tr.mismatch_frees == 0u) ? 0u : 1u;
                write_exact(hout, resp, sizeof(resp));
            }
        } else if (op == 5u) {
            im_gif_info info;
            info.width = 0xDEADu;
            im_image_error err = im_gif_probe(in_buf, len, lim_ptr, &info);
            uint32_t resp[7] = {
                (uint32_t)err, info.width, info.height,
                info.has_gct ? 1u : 0u, (uint32_t)info.gct_size,
                info.is_gif89a ? 1u : 0u, (uint32_t)info.byte_size
            };
            write_exact(hout, resp, sizeof(resp));
        } else if (op == 6u) {
            /* Exhaustive proper-prefix check for BOTH single & anim decode:
               max_frames param here is `allowed_ok_plen` (0xFFFFFFFFu = none allowed). */
            uint32_t allowed_ok = max_frames;
            uint32_t bad = 0u;
            uint32_t first_bad = 0xFFFFFFFFu;
            for (uint32_t plen = 0u; plen < len; plen++) {
                im_image img;
                img.width = 99u;
                tr.live_blocks = 0u; tr.live_bytes = 0u; tr.mismatch_frees = 0u; tr.rec_count = 0u;
                im_image_error e1 = im_gif_decode(in_buf, plen, IM_PIXEL_RGBA8, NULL, &alloc, &img);
                if (e1 == IM_IMAGE_OK) {
                    if (plen != allowed_ok) { if (bad == 0u) first_bad = plen; bad++; }
                    alloc.free(alloc.ctx, img.pixels, img.byte_size);
                    if (tr.live_blocks != 0u || tr.mismatch_frees != 0u) { if (bad == 0u) first_bad = plen; bad++; }
                } else if (!image_is_zero(&img) || tr.live_blocks != 0u || tr.live_bytes != 0u || tr.mismatch_frees != 0u) {
                    if (bad == 0u) first_bad = plen;
                    bad++;
                }

                im_gif_anim anim;
                anim.frame_count = 99u;
                tr.live_blocks = 0u; tr.live_bytes = 0u; tr.mismatch_frees = 0u; tr.rec_count = 0u;
                im_image_error e2 = im_gif_decode_anim(in_buf, plen, IM_PIXEL_RGBA8, NULL, 0u, &alloc, &anim);
                if (e2 == IM_IMAGE_OK) {
                    if (plen != allowed_ok) { if (bad == 0u) first_bad = plen; bad++; }
                    im_gif_anim_free(&alloc, &anim);
                    if (tr.live_blocks != 0u || tr.mismatch_frees != 0u) { if (bad == 0u) first_bad = plen; bad++; }
                } else if (!anim_is_zero(&anim) || tr.live_blocks != 0u || tr.live_bytes != 0u || tr.mismatch_frees != 0u) {
                    if (bad == 0u) first_bad = plen;
                    bad++;
                }
            }
            uint32_t resp[2] = {bad, first_bad};
            write_exact(hout, resp, sizeof(resp));
        }
    }
    if (in_buf) HeapFree(heap, 0, in_buf);
}

int im_main(void) {
    const wchar_t *cmd = GetCommandLineW();
    bool batch = false;
    if (cmd) {
        for (const wchar_t *p = cmd; *p; p++) {
            if (p[0] == L'-' && p[1] == L'-' && p[2] == L'b' && p[3] == L'a' &&
                p[4] == L't' && p[5] == L'c' && p[6] == L'h') {
                batch = true;
                break;
            }
        }
    }
    if (batch) {
        run_batch();
        return 0;
    }
    run_unit_tests();
    if (g_fails == 0) {
        write_str("gif_tests: ALL PASSED\n");
        return 0;
    }
    return 1;
}
