/* Tier 0 tests for D06 T01 s2: parts/deflate (streaming DEFLATE, zlib, gzip). */
#include <impossible/base.h>
#include <windows.h>
#include "checksum.h"
#include "deflate.h"
#include "inflate.h"

_Static_assert(sizeof(im_deflate) < 512u * 1024u, "im_deflate must stay under 512 KiB");
_Static_assert(sizeof(im_deflate) + sizeof(im_inflate) < 1000000u, "deflate + inflate must stay under 1 MB decimal");

static int g_fails = 0;
static im_deflate g_def1;
static im_deflate g_def2;
static im_inflate g_inf;

static void write_str(const char *s) {
    DWORD n = 0;
    while (s[n]) ++n;
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

static void write_u64(uint64_t u) {
    char rev[24];
    int r = 0;
    do { rev[r++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
    char buf[24];
    int p = 0;
    while (r > 0) buf[p++] = rev[--r];
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)p, &w, NULL);
}

static void check(bool cond, const char *msg) {
    if (!cond) {
        ++g_fails;
        write_str("FAIL: ");
        write_str(msg);
        write_str("\n");
    }
}

static bool read_exact(HANDLE h, uint8_t *dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        DWORD chunk = (n - got) > 0x100000u ? 0x100000u : (DWORD)(n - got);
        DWORD r = 0;
        if (!ReadFile(h, dst + got, chunk, &r, NULL) || r == 0) return false;
        got += r;
    }
    return true;
}

static bool write_exact(HANDLE h, const uint8_t *src, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        DWORD chunk = (n - sent) > 0x100000u ? 0x100000u : (DWORD)(n - sent);
        DWORD w = 0;
        if (!WriteFile(h, src + sent, chunk, &w, NULL) || w == 0) return false;
        sent += w;
    }
    return true;
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void test_api_contracts(void) {
    uint8_t out[64];
    size_t in_used = 99, out_used = 99, out_len = 99;
    const uint8_t sample[] = "hello deflate world";

    check(im_deflate_init(NULL, IM_DEFLATE_ZLIB, IM_DEFLATE_BALANCED) == IM_DEFLATE_BAD_ARG,
          "init NULL state -> BAD_ARG");
    check(im_deflate_init(&g_def1, (im_deflate_format)9, IM_DEFLATE_BALANCED) == IM_DEFLATE_BAD_ARG,
          "init invalid format -> BAD_ARG");
    check(im_deflate_init(&g_def1, IM_DEFLATE_ZLIB, (im_deflate_effort)9) == IM_DEFLATE_BAD_ARG,
          "init invalid effort -> BAD_ARG");

    check(im_deflate_init(&g_def1, IM_DEFLATE_ZLIB, IM_DEFLATE_BALANCED) == IM_DEFLATE_OK,
          "init valid -> OK");
    check(im_deflate_step(NULL, sample, 5, true, &in_used, out, sizeof(out), &out_used) == IM_DEFLATE_BAD_ARG &&
          in_used == 0 && out_used == 0, "step NULL state -> BAD_ARG with 0/0 counts");
    check(im_deflate_step(&g_def1, NULL, 5, true, &in_used, out, sizeof(out), &out_used) == IM_DEFLATE_BAD_ARG &&
          in_used == 0 && out_used == 0, "step NULL in with in_len>0 -> BAD_ARG");
    check(im_deflate_step(&g_def1, sample, 5, true, &in_used, NULL, 5, &out_used) == IM_DEFLATE_BAD_ARG &&
          in_used == 0 && out_used == 0, "step NULL out with out_cap>0 -> BAD_ARG");

    /* out_cap == 0 while header pending -> OUT_FULL with 0 output. */
    check(im_deflate_step(&g_def1, sample, 5, false, &in_used, NULL, 0, &out_used) == IM_DEFLATE_OUT_FULL &&
          in_used == 0 && out_used == 0, "step out_cap==0 with pending header -> OUT_FULL 0/0");

    /* Complete the stream, then verify post-OK calls. */
    im_deflate_status rc = im_deflate_step(&g_def1, sample, sizeof(sample) - 1u, true, &in_used,
                                           out, sizeof(out), &out_used);
    check(rc == IM_DEFLATE_OK && in_used == sizeof(sample) - 1u && out_used > 6u,
          "step completes small zlib stream");
    check(im_deflate_step(&g_def1, NULL, 0, true, &in_used, out, sizeof(out), &out_used) == IM_DEFLATE_OK &&
          in_used == 0 && out_used == 0, "post-OK empty call -> OK 0/0");
    check(im_deflate_step(&g_def1, sample, 1, true, &in_used, out, sizeof(out), &out_used) == IM_DEFLATE_BAD_ARG &&
          in_used == 0 && out_used == 0, "post-OK non-empty call -> BAD_ARG 0/0");

    /* One-shot undersized output buffer reports OUT_FULL and writes no byte past out_cap. */
    out[5] = 0xCCu;
    rc = im_deflate_all(&g_def1, IM_DEFLATE_ZLIB, IM_DEFLATE_BALANCED,
                        sample, sizeof(sample) - 1u, out, 5u, &out_len);
    check(rc == IM_DEFLATE_OUT_FULL && out_len == 5u && out[5] == 0xCCu,
          "im_deflate_all undersized buffer -> OUT_FULL without overrun");

    /* NULL/0 empty input succeeds in all 3 formats. */
    for (int f = 0; f < 3; ++f) {
        rc = im_deflate_all(&g_def1, (im_deflate_format)f, IM_DEFLATE_FAST,
                            NULL, 0, out, sizeof(out), &out_len);
        check(rc == IM_DEFLATE_OK && out_len > 0u, "empty input succeeds in all formats");
        uint8_t dec[16];
        size_t dec_len = 99;
        im_inflate_status irc = im_inflate_all((im_inflate_format)f, out, out_len, dec, sizeof(dec),
                                               false, 0, &dec_len);
        check(irc == IM_INFLATE_OK && dec_len == 0u, "empty output inflates back to 0 bytes");
    }
}

static uint8_t g_big_in[96u * 1024u];
static uint8_t g_big_out1[128u * 1024u];
static uint8_t g_big_out2[128u * 1024u];
static uint8_t g_big_dec[96u * 1024u];

static bool stream_compress(im_deflate *st, im_deflate_format fmt, im_deflate_effort effort,
                            const uint8_t *in, size_t in_len, size_t in_chunk, size_t out_chunk,
                            bool separate_eof, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (im_deflate_init(st, fmt, effort) != IM_DEFLATE_OK) return false;
    size_t ip = 0, op = 0;
    bool sent_eof = false;
    for (;;) {
        size_t avail = in_len - ip;
        size_t give = avail > in_chunk ? in_chunk : avail;
        bool eof_now = false;
        if (separate_eof) {
            eof_now = (avail == 0u);
        } else {
            eof_now = (ip + give == in_len);
        }
        size_t cap_now = out_cap - op;
        if (cap_now > out_chunk) cap_now = out_chunk;
        size_t ui = 0, uo = 0;
        im_deflate_status rc = im_deflate_step(
            st, (give > 0u) ? (in + ip) : NULL, give, eof_now, &ui,
            (cap_now > 0u) ? (out + op) : NULL, cap_now, &uo);
        if (ui > give || uo > cap_now) return false;
        ip += ui;
        op += uo;
        if (rc == IM_DEFLATE_OK) {
            *out_len = op;
            return ip == in_len;
        }
        if (rc == IM_DEFLATE_NEED_INPUT) {
            if (ui == 0u && uo == 0u && eof_now) return false;
            if (eof_now && ip == in_len) sent_eof = true;
            (void)sent_eof;
            continue;
        }
        if (rc == IM_DEFLATE_OUT_FULL) {
            if (op == out_cap) return false;
            continue;
        }
        return false;
    }
}

static void test_window_slide_and_boundary_determinism(void) {
    /* 1. Oldest valid match (dist == 32768) + first position after a slide + stored-block fallback spanning slide. */
    uint32_t rng = 0x12345678u;
    for (size_t i = 0; i < sizeof(g_big_in); ++i) {
        g_big_in[i] = (uint8_t)(xorshift32(&rng) & 0xFFu);
    }
    /* Copy 258 bytes from offset 0 to offset 32768 (exact dist == 32768). */
    for (size_t i = 0; i < 258u; ++i) g_big_in[32768u + i] = g_big_in[i];
    /* Copy 258 bytes from offset 33000 to offset 65274 (right at slide boundary) and 65536 (just after slide). */
    for (size_t i = 0; i < 258u; ++i) {
        g_big_in[65274u + i] = g_big_in[33000u + i];
        g_big_in[65536u + i] = g_big_in[33000u + i];
    }

    for (int e = 0; e < 3; ++e) {
        size_t len1 = 0, len2 = 0, dec_len = 0;
        im_deflate_status rc = im_deflate_all(&g_def1, IM_DEFLATE_ZLIB, (im_deflate_effort)e,
                                              g_big_in, sizeof(g_big_in), g_big_out1, sizeof(g_big_out1), &len1);
        check(rc == IM_DEFLATE_OK && len1 > 0u, "slide+stored one-shot succeeds");

        /* Stream with 137-byte input chunks, 1-byte output buffer, and separate EOF call! */
        bool ok = stream_compress(&g_def2, IM_DEFLATE_ZLIB, (im_deflate_effort)e,
                                  g_big_in, sizeof(g_big_in), 137u, 1u, true,
                                  g_big_out2, sizeof(g_big_out2), &len2);
        bool eq = ok && (len1 == len2);
        for (size_t i = 0; eq && i < len1; ++i) {
            if (g_big_out1[i] != g_big_out2[i]) eq = false;
        }
        check(eq, "slide+stored streaming 137B/1B + separate EOF is byte-identical to one-shot");

        im_inflate_status irc = im_inflate_all(IM_INFLATE_ZLIB, g_big_out1, len1,
                                               g_big_dec, sizeof(g_big_dec), false, 0, &dec_len);
        bool dec_eq = (irc == IM_INFLATE_OK && dec_len == sizeof(g_big_in));
        for (size_t i = 0; dec_eq && i < sizeof(g_big_in); ++i) {
            if (g_big_dec[i] != g_big_in[i]) dec_eq = false;
        }
        check(dec_eq, "slide+stored round-trips byte-for-byte");
    }

    /* 2. Exact block-boundary EOF schedule parity (32000 bytes and 64000 bytes) + split 258-byte match. */
    static const size_t k_sizes[3] = { 32000u, 32768u, 64000u };
    for (size_t si = 0; si < 3u; ++si) {
        size_t n = k_sizes[si];
        for (size_t i = 0; i < n; ++i) {
            g_big_in[i] = (uint8_t)((i * 7u + (i / 19u)) & 0x3Fu);
        }
        /* Plant a 258-byte match right across offset 500 -> 1000. */
        for (size_t i = 0; i < 258u; ++i) g_big_in[1000u + i] = g_big_in[500u + i];

        for (int e = 0; e < 3; ++e) {
            size_t len1 = 0, len2 = 0;
            im_deflate_status rc = im_deflate_all(&g_def1, IM_DEFLATE_RAW, (im_deflate_effort)e,
                                                  g_big_in, n, g_big_out1, sizeof(g_big_out1), &len1);
            check(rc == IM_DEFLATE_OK, "exact-boundary one-shot succeeds");
            bool ok = stream_compress(&g_def2, IM_DEFLATE_RAW, (im_deflate_effort)e,
                                      g_big_in, n, 113u, 1u, true,
                                      g_big_out2, sizeof(g_big_out2), &len2);
            bool eq = ok && (len1 == len2);
            for (size_t i = 0; eq && i < len1; ++i) {
                if (g_big_out1[i] != g_big_out2[i]) eq = false;
            }
            check(eq, "exact-boundary separate-EOF + 1B out matches one-shot byte-for-byte");
        }
    }
}

/* Stream 10 GiB (10,737,418,240 bytes) through im_deflate_step -> im_inflate_step in fixed < 1 MB memory,
   independently verifying source CRC-32, decoded CRC-32, full 64-bit byte count, and gzip trailer. */
static uint8_t g_10g_src[65536];
static uint8_t g_10g_cmp[65536];
static uint8_t g_10g_dec[65536];

static void test_stream_10gb(void) {
    /* Total fixed memory used by compressor + inflater + I/O scratch:
       im_deflate (~320 KiB) + im_inflate (~36 KiB) + 3*64 KiB = ~548 KiB < 1 MB! */
    const uint64_t k_target = 10ULL * 1024ULL * 1024ULL * 1024ULL; /* 10,737,418,240 bytes */
    /* Build a mixed 64 KiB block: 64-byte header + structured repeating pattern so deflate exercises
       both literals and long matches while streaming 10 GiB in a few seconds. */
    for (size_t i = 0; i < sizeof(g_10g_src); ++i) {
        g_10g_src[i] = (uint8_t)(((i & 255u) * 37u + 11u) & 0xFFu);
    }

    check(im_deflate_init(&g_def1, IM_DEFLATE_GZIP, IM_DEFLATE_FAST) == IM_DEFLATE_OK,
          "10GB: deflate_init OK");
    im_inflate_init(&g_inf, IM_INFLATE_GZIP, false, 0);

    uint64_t fed = 0;
    uint64_t decoded = 0;
    uint64_t comp_bytes = 0;
    uint32_t ref_adler = im_adler32_init();
    uint32_t dec_adler = im_adler32_init();
    uint8_t tail8[8] = {0};
    uint64_t chunk_id = 0;
    bool def_done = false;
    bool inf_done = false;

    while (!def_done) {
        size_t give = 0;
        bool is_eof = false;
        if (fed < k_target) {
            uint64_t rem = k_target - fed;
            give = rem > sizeof(g_10g_src) ? sizeof(g_10g_src) : (size_t)rem;
            /* Vary the first 8 bytes of every chunk so every 64 KiB block is distinct. */
            write_u32_le(g_10g_src, (uint32_t)(chunk_id & 0xFFFFFFFFu));
            write_u32_le(g_10g_src + 4u, (uint32_t)(chunk_id >> 32));
            is_eof = (fed + give == k_target);
        } else {
            is_eof = true;
        }

        size_t chunk_off = 0;
        for (;;) {
            size_t ui = 0, uo = 0;
            im_deflate_status drc = im_deflate_step(
                &g_def1,
                (give > chunk_off) ? (g_10g_src + chunk_off) : NULL,
                give - chunk_off,
                is_eof,
                &ui,
                g_10g_cmp,
                sizeof(g_10g_cmp),
                &uo);
            if (ui > 0u) {
                ref_adler = im_adler32_update(ref_adler, g_10g_src + chunk_off, ui);
                chunk_off += ui;
                fed += ui;
            }
            if (uo > 0u || (drc == IM_DEFLATE_OK && !inf_done)) {
                if (uo >= 8u) {
                    for (size_t k = 0; k < 8u; ++k) tail8[k] = g_10g_cmp[uo - 8u + k];
                } else if (uo > 0u) {
                    for (size_t k = 0; k < 8u - uo; ++k) tail8[k] = tail8[k + uo];
                    for (size_t k = 0; k < uo; ++k) tail8[8u - uo + k] = g_10g_cmp[k];
                }
                comp_bytes += uo;

                /* Feed compressed bytes directly into im_inflate_step (verifies gzip CRC-32 & ISIZE). */
                size_t coff = 0;
                while (coff < uo || (drc == IM_DEFLATE_OK && !inf_done)) {
                    size_t iui = 0, iuo = 0;
                    im_inflate_status irc = im_inflate_step(
                        &g_inf,
                        (coff < uo) ? (g_10g_cmp + coff) : NULL,
                        uo - coff,
                        (drc == IM_DEFLATE_OK),
                        &iui,
                        g_10g_dec,
                        sizeof(g_10g_dec),
                        &iuo);
                    coff += iui;
                    if (iuo > 0u) {
                        dec_adler = im_adler32_update(dec_adler, g_10g_dec, iuo);
                        decoded += iuo;
                    }
                    if (irc == IM_INFLATE_OK) {
                        inf_done = true;
                        break;
                    }
                    if (irc == IM_INFLATE_NEED_INPUT) break;
                    if (irc != IM_INFLATE_OUT_FULL) {
                        check(false, "10GB: inflate failed mid-stream");
                        return;
                    }
                }
            }
            if (drc == IM_DEFLATE_OK) {
                def_done = true;
                break;
            }
            if (drc == IM_DEFLATE_NEED_INPUT) {
                if (chunk_off == give) break;
            } else if (drc != IM_DEFLATE_OUT_FULL) {
                check(false, "10GB: deflate returned error");
                return;
            }
        }
        ++chunk_id;
    }

    uint32_t trailer_isize = read_u32_le(tail8 + 4u);
    uint32_t expected_isize = (uint32_t)(k_target & 0xFFFFFFFFu);

    check(def_done && inf_done, "10GB: both deflate and inflate finished cleanly");
    check(fed == k_target, "10GB: fed exact 10,737,418,240 bytes");
    check(decoded == k_target, "10GB: inflated exact 10,737,418,240 bytes");
    check(dec_adler == ref_adler, "10GB: decoded Adler-32 matches independently accumulated source Adler-32");
    check(trailer_isize == expected_isize, "10GB: gzip trailer ISIZE matches total mod 2^32");
    check(comp_bytes < k_target / 4u, "10GB: output was genuinely compressed");
    write_str("10GB_OK compressed=");
    write_u64(comp_bytes);
    write_str(" state_bytes=");
    write_u64((uint64_t)sizeof(im_deflate));
    write_str("\n");
}

static uint8_t g_batch_in[4u * 1024u * 1024u];
static uint8_t g_batch_out[5u * 1024u * 1024u];

static int run_batch(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    uint8_t hdr[16];
    while (read_exact(hin, hdr, 16u)) {
        uint8_t op = hdr[0];
        im_deflate_format fmt = (im_deflate_format)hdr[1];
        im_deflate_effort eff = (im_deflate_effort)hdr[2];
        uint8_t flags = hdr[3];
        uint32_t in_chunk = read_u32_le(hdr + 4u);
        uint32_t out_chunk = read_u32_le(hdr + 8u);
        uint32_t in_len = read_u32_le(hdr + 12u);
        if (in_len > sizeof(g_batch_in)) return 2;
        if (in_len > 0u && !read_exact(hin, g_batch_in, in_len)) return 3;

        im_deflate_status rc = IM_DEFLATE_BAD_ARG;
        size_t out_len = 0;
        if (op == 0u) {
            rc = im_deflate_all(&g_def1, fmt, eff,
                                in_len ? g_batch_in : NULL, in_len,
                                g_batch_out, sizeof(g_batch_out), &out_len);
        } else if (op == 1u) {
            bool ok = stream_compress(&g_def1, fmt, eff,
                                      in_len ? g_batch_in : NULL, in_len,
                                      in_chunk ? in_chunk : 1u, out_chunk ? out_chunk : 1u,
                                      (flags & 1u) != 0u,
                                      g_batch_out, sizeof(g_batch_out), &out_len);
            rc = ok ? IM_DEFLATE_OK : IM_DEFLATE_BAD_ARG;
        }
        uint8_t resp[8];
        write_u32_le(resp, (uint32_t)rc);
        write_u32_le(resp + 4u, (uint32_t)out_len);
        if (!write_exact(hout, resp, 8u)) return 4;
        if (out_len > 0u && !write_exact(hout, g_batch_out, out_len)) return 5;
    }
    return 0;
}

static bool arg_contains(const wchar_t *cmd, const wchar_t *needle) {
    if (!cmd || !needle) return false;
    for (size_t i = 0; cmd[i]; ++i) {
        size_t j = 0;
        while (needle[j] && cmd[i + j] == needle[j]) ++j;
        if (!needle[j]) return true;
    }
    return false;
}

int im_main(void) {
    const wchar_t *cmd = GetCommandLineW();
    if (arg_contains(cmd, L"--batch")) {
        return run_batch();
    }
    if (arg_contains(cmd, L"--stream10g")) {
        test_stream_10gb();
        if (g_fails == 0) {
            write_str("deflate_tests 10GB: ALL PASSED\n");
            return 0;
        }
        return 1;
    }

    test_api_contracts();
    test_window_slide_and_boundary_determinism();
    if (g_fails == 0) {
        write_str("deflate_tests: ALL PASSED\n");
        return 0;
    }
    write_str("deflate_tests: FAILED\n");
    return 1;
}
