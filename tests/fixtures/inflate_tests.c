/* Tier 0 tests for D02 T01 s2: parts/checksum and parts/inflate. */
#include <impossible/base.h>
#include <impossible/file.h>
#include <windows.h>
#include "checksum.h"
#include "inflate.h"

static int g_fails = 0;

static void write_str(const char *s) {
    DWORD n = 0;
    while (s[n]) ++n;
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

static void write_i64(int64_t v) {
    char buf[32];
    int p = 0;
    uint64_t u = (v < 0) ? (buf[p++] = '-', (uint64_t)(-(v + 1)) + 1u) : (uint64_t)v;
    char rev[24];
    int r = 0;
    do { rev[r++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
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

/* ---- Deterministic self-contained tests (RFC 1951/1950/1952 & zlib vectors) ---- */

static void test_checksum_vectors(void) {
    /* Standard IEEE 802.3 CRC-32 ("123456789" -> 0xCBF43926) and RFC 1950 Adler-32 ("Wikipedia" -> 0x11E60398) */
    const uint8_t s1[] = "123456789";
    check(im_crc32(NULL, 0) == 0u, "CRC-32 empty == 0");
    check(im_crc32(s1, 9) == 0xCBF43926u, "CRC-32 '123456789' == 0xCBF43926");
    uint32_t c = im_crc32_init();
    for (size_t i = 0; i < 9; ++i) c = im_crc32_update(c, s1 + i, 1);
    check(c == 0xCBF43926u, "CRC-32 byte-by-byte matches whole");

    const uint8_t s2[] = "Wikipedia";
    check(im_adler32(NULL, 0) == 1u, "Adler-32 empty == 1");
    check(im_adler32(s2, 9) == 0x11E60398u, "Adler-32 'Wikipedia' == 0x11E60398");
    uint32_t a = im_adler32_init();
    for (size_t i = 0; i < 9; ++i) a = im_adler32_update(a, s2 + i, 1);
    check(a == 0x11E60398u, "Adler-32 byte-by-byte matches whole");
}

static void test_zlib_vectors_and_edge_cases(void) {
    /* 1. Upstream zlib hello vector: "hello, hello!" */
    static const uint8_t hello_z[] = {
        0x78, 0x9C, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0xD7, 0x51, 0x28, 0xCF, 0x2F, 0xCA, 0x49, 0x51, 0x04, 0x00, 0x20, 0x5E, 0x04, 0x8A
    };
    /* Wait: let's verify standard zlib "hello, world!" (0x78 0x9c ...). Above is "hello, world" - check exact bytes! */
    static const uint8_t hello_world_z[] = {
        0x78, 0x9C, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0xD7, 0x51, 0x28, 0xCF, 0x2F, 0xCA, 0x49, 0x51, 0x04, 0x00,
        0x21, 0xFE, 0x04, 0xAA
    };
    (void)hello_z;
    uint8_t out[64];
    size_t out_len = 0;
    im_inflate_status st = im_inflate_all(IM_INFLATE_ZLIB, hello_world_z, sizeof(hello_world_z), out, sizeof(out), false, 0, &out_len);
    check(st == IM_INFLATE_OK && out_len == 13u, "zlib vector 'hello, world!' decodes with OK");

    /* 2. Exact output_limit == 13 succeeds, and sentinel byte at out[13] stays untouched! */
    out[13] = 0xAAu;
    st = im_inflate_all(IM_INFLATE_ZLIB, hello_world_z, sizeof(hello_world_z), out, sizeof(out), true, 13u, &out_len);
    check(st == IM_INFLATE_OK && out_len == 13u && out[13] == 0xAAu, "Exact limit == 13 succeeds without touching sentinel");

    /* 3. output_limit == 12 returns IM_INFLATE_LIMIT with sentinel at out[12] untouched! */
    out[12] = 0xBBu;
    st = im_inflate_all(IM_INFLATE_ZLIB, hello_world_z, sizeof(hello_world_z), out, sizeof(out), true, 12u, &out_len);
    check(st == IM_INFLATE_LIMIT && out_len == 12u && out[12] == 0xBBu, "Limit == 12 stops with IM_INFLATE_LIMIT and sentinel intact");

    /* 4. Missing final trailer byte returns IM_INFLATE_TRUNCATED (never IM_INFLATE_OK!). */
    st = im_inflate_all(IM_INFLATE_ZLIB, hello_world_z, sizeof(hello_world_z) - 1u, out, sizeof(out), false, 0, &out_len);
    check(st == IM_INFLATE_TRUNCATED, "Missing final Adler-32 trailer byte returns IM_INFLATE_TRUNCATED");

    /* 5. Corrupt Adler-32 at exact limit returns IM_INFLATE_BAD_CHECKSUM (not OK). */
    uint8_t bad_z[sizeof(hello_world_z)];
    for (size_t i = 0; i < sizeof(hello_world_z); ++i) bad_z[i] = hello_world_z[i];
    bad_z[sizeof(bad_z) - 1u] ^= 1u;
    st = im_inflate_all(IM_INFLATE_ZLIB, bad_z, sizeof(bad_z), out, sizeof(out), true, 13u, &out_len);
    check(st == IM_INFLATE_BAD_CHECKSUM, "Corrupt Adler-32 at exact limit returns IM_INFLATE_BAD_CHECKSUM");

    /* 6. Zlib FDICT flag returns IM_INFLATE_NEED_DICT. */
    static const uint8_t fdict_z[] = { 0x78, 0xBB, 0x00, 0x00, 0x00, 0x01 };
    st = im_inflate_all(IM_INFLATE_ZLIB, fdict_z, sizeof(fdict_z), out, sizeof(out), false, 0, &out_len);
    check(st == IM_INFLATE_NEED_DICT, "Zlib FDICT flag refused with IM_INFLATE_NEED_DICT");

    /* 7. Empty stream with limit == 0 succeeds; non-empty stream with limit == 0 returns IM_INFLATE_LIMIT! */
    static const uint8_t empty_raw[] = { 0x03, 0x00 }; /* BFINAL=1, BTYPE=1 (fixed), EOB */
    st = im_inflate_all(IM_INFLATE_RAW, empty_raw, sizeof(empty_raw), out, sizeof(out), true, 0u, &out_len);
    check(st == IM_INFLATE_OK && out_len == 0u, "Empty stream succeeds at limit == 0");
    st = im_inflate_all(IM_INFLATE_ZLIB, hello_world_z, sizeof(hello_world_z), out, sizeof(out), true, 0u, &out_len);
    check(st == IM_INFLATE_LIMIT && out_len == 0u, "Non-empty stream refused at limit == 0");

    /* 8. Invalid block type 3 and bad stored NLEN refused with IM_INFLATE_BAD_DATA. */
    static const uint8_t bad_btype[] = { 0x07 };
    static const uint8_t bad_nlen[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c' };
    check(im_inflate_all(IM_INFLATE_RAW, bad_btype, sizeof(bad_btype), out, sizeof(out), false, 0, &out_len) == IM_INFLATE_BAD_DATA,
          "Block type 3 refused");
    check(im_inflate_all(IM_INFLATE_RAW, bad_nlen, sizeof(bad_nlen), out, sizeof(out), false, 0, &out_len) == IM_INFLATE_BAD_DATA,
          "Bad stored NLEN refused");

    /* 9. Unused trailing bytes after raw/zlib stream remain unconsumed! */
    uint8_t with_trail[sizeof(hello_world_z) + 4u];
    for (size_t i = 0; i < sizeof(hello_world_z); ++i) with_trail[i] = hello_world_z[i];
    with_trail[sizeof(hello_world_z) + 0] = 0xDE;
    with_trail[sizeof(hello_world_z) + 1] = 0xAD;
    with_trail[sizeof(hello_world_z) + 2] = 0xBE;
    with_trail[sizeof(hello_world_z) + 3] = 0xEF;
    im_inflate inf;
    im_inflate_init(&inf, IM_INFLATE_ZLIB, false, 0);
    size_t iu = 0, ou = 0;
    st = im_inflate_step(&inf, with_trail, sizeof(with_trail), true, &iu, out, sizeof(out), &ou);
    check(st == IM_INFLATE_OK && iu == sizeof(hello_world_z) && ou == 13u,
          "Trailing bytes after zlib stream remain unconsumed");
}

/* ---- Binary batch mode driven by tests/test_parts.py (stdin -> stdout) ----
   Record format:
     u32 op:
       1 = CRC32+Adler32 check: u32 len, bytes[len] -> replies u32 crc32, u32 adler32
       2 = Inflate whole + 1-byte-stream check: u8 fmt, u8 flags (bit0=has_limit, bit1=stream_1by1),
           u32 limit, u32 out_cap, u32 in_len, bytes[in_len]
           -> replies u32 status, u32 in_used, u32 out_len, bytes[out_len]
       3 = Bomb test (streaming discard into 4KB scratch): u8 fmt, u32 limit, u32 in_len, bytes[in_len]
           -> replies u32 status, u64 total_out
*/

static bool read_exact(HANDLE h, void *dst, DWORD n) {
    uint8_t *p = (uint8_t *)dst;
    DWORD got = 0;
    while (got < n) {
        DWORD chunk = 0;
        if (!ReadFile(h, p + got, n - got, &chunk, NULL) || chunk == 0) return false;
        got += chunk;
    }
    return true;
}

static void write_exact(HANDLE h, const void *src, DWORD n) {
    const uint8_t *p = (const uint8_t *)src;
    DWORD sent = 0;
    while (sent < n) {
        DWORD chunk = 0;
        if (!WriteFile(h, p + sent, n - sent, &chunk, NULL) || chunk == 0) return;
        sent += chunk;
    }
}

static int run_batch(void) {
    HANDLE hi = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    static uint8_t in_buf[2 * 1024 * 1024];
    static uint8_t out_buf[2 * 1024 * 1024];
    static im_inflate inf;

    uint32_t op = 0;
    while (read_exact(hi, &op, 4)) {
        if (op == 1u) {
            uint32_t len = 0;
            if (!read_exact(hi, &len, 4) || len > sizeof(in_buf)) return 2;
            if (len > 0 && !read_exact(hi, in_buf, len)) return 2;
            uint32_t res[2] = { im_crc32(in_buf, len), im_adler32(in_buf, len) };
            write_exact(ho, res, 8);
        } else if (op == 2u) {
            uint8_t hdr[2];
            uint32_t parms[3]; /* limit, out_cap, in_len */
            if (!read_exact(hi, hdr, 2) || !read_exact(hi, parms, 12)) return 2;
            im_inflate_format fmt = (im_inflate_format)hdr[0];
            bool has_lim = (hdr[1] & 1u) != 0u;
            bool byte_by_byte = (hdr[1] & 2u) != 0u;
            uint32_t limit = parms[0], out_cap = parms[1], in_len = parms[2];
            if (in_len > sizeof(in_buf) || out_cap > sizeof(out_buf)) return 3;
            if (in_len > 0 && !read_exact(hi, in_buf, in_len)) return 2;

            im_inflate_init(&inf, fmt, has_lim, limit);
            im_inflate_status st = IM_INFLATE_OK;
            size_t total_in = 0, total_out = 0;
            if (!byte_by_byte) {
                st = im_inflate_step(&inf, in_buf, in_len, true, &total_in, out_buf, out_cap, &total_out);
            } else {
                for (;;) {
                    size_t in_avail = (total_in < in_len) ? 1u : 0u;
                    bool eof = (total_in + in_avail >= in_len);
                    size_t out_avail = (total_out < out_cap) ? 1u : 0u;
                    size_t iu = 0, ou = 0;
                    st = im_inflate_step(&inf, in_buf + total_in, in_avail, eof, &iu, out_buf + total_out, out_avail, &ou);
                    total_in += iu;
                    total_out += ou;
                    if (st == IM_INFLATE_NEED_INPUT) {
                        if (eof && iu == 0) { st = IM_INFLATE_TRUNCATED; break; }
                        continue;
                    }
                    if (st == IM_INFLATE_OUT_FULL) {
                        if (total_out >= out_cap) break;
                        continue;
                    }
                    break;
                }
            }
            uint32_t rep[3] = { (uint32_t)st, (uint32_t)total_in, (uint32_t)total_out };
            write_exact(ho, rep, 12);
            if (total_out > 0) write_exact(ho, out_buf, (DWORD)total_out);
        } else if (op == 3u) {
            uint8_t fmt_b = 0;
            uint32_t parms[2]; /* limit, in_len */
            if (!read_exact(hi, &fmt_b, 1) || !read_exact(hi, parms, 8)) return 2;
            uint32_t limit = parms[0], in_len = parms[1];
            if (in_len > sizeof(in_buf)) return 3;
            if (in_len > 0 && !read_exact(hi, in_buf, in_len)) return 2;
            im_inflate_init(&inf, (im_inflate_format)fmt_b, true, limit);
            uint8_t scratch[4096];
            size_t ip = 0;
            im_inflate_status st = IM_INFLATE_NEED_INPUT;
            for (;;) {
                size_t iu = 0, ou = 0;
                st = im_inflate_step(&inf, in_buf + ip, in_len - ip, true, &iu, scratch, sizeof(scratch), &ou);
                ip += iu;
                if (st != IM_INFLATE_OUT_FULL) break;
            }
            uint32_t st32 = (uint32_t)st;
            uint64_t tout = inf.total_out;
            write_exact(ho, &st32, 4);
            write_exact(ho, &tout, 8);
        } else {
            return 4;
        }
    }
    return 0;
}

static bool has_batch_arg(void) {
    const wchar_t *cmd = GetCommandLineW();
    while (*cmd) {
        if (cmd[0] == L'-' && cmd[1] == L'-' && cmd[2] == L'b' && cmd[3] == L'a' &&
            cmd[4] == L't' && cmd[5] == L'c' && cmd[6] == L'h') return true;
        ++cmd;
    }
    return false;
}

int im_main(void) {
    if (has_batch_arg()) return run_batch();
    test_checksum_vectors();
    test_zlib_vectors_and_edge_cases();
    if (g_fails == 0) {
        write_str("inflate_tests: ALL PASSED\n");
        return 0;
    }
    write_str("inflate_tests: FAILED count=");
    write_i64(g_fails);
    write_str("\n");
    return 1;
}
