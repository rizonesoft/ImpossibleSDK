/* Fixture decoder exercising parts/image/image.h and im_image_check_limits.
   Exits 0, or with the number of the first check that failed. */
#include <impossible/base.h>
#include "image.h"

/* Minimal fixture image format: 4-byte magic "IMFX", u32 LE width, u32 LE height, u8 format (0=BGRA8, 1=RGBA8),
   followed by width * height * 4 raw pixel bytes in top-to-bottom row order. */
typedef void *(*fixture_alloc_fn)(size_t bytes, void *ctx);

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static bool bytes_eq(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void zero_bytes(void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

static void fill_bytes(void *dst, uint8_t v, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = v;
}

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static im_image_error decode_fixture(const uint8_t *data, size_t len, const im_image_limits *limits,
                                     fixture_alloc_fn alloc, void *alloc_ctx, im_image *out) {
    if (!out) {
        return IM_IMAGE_BAD_ARG;
    }
    zero_bytes(out, sizeof(*out));
    if (!data || !alloc) {
        return IM_IMAGE_BAD_ARG;
    }
    if (len < 13u) {
        return IM_IMAGE_TRUNCATED;
    }
    static const uint8_t magic[4] = {'I', 'M', 'F', 'X'};
    if (!bytes_eq(data, magic, 4)) {
        return IM_IMAGE_INVALID;
    }
    uint32_t w = read_u32_le(data + 4);
    uint32_t h = read_u32_le(data + 8);
    uint8_t fmt = data[12];
    if (fmt != (uint8_t)IM_PIXEL_BGRA8 && fmt != (uint8_t)IM_PIXEL_RGBA8) {
        return IM_IMAGE_UNSUPPORTED;
    }

    size_t needed = 0;
    im_image_error err = im_image_check_limits(w, h, limits, &needed);
    if (err != IM_IMAGE_OK) {
        return err;
    }
    if (len - 13u < needed) {
        return IM_IMAGE_TRUNCATED;
    }

    uint8_t *buf = (uint8_t *)alloc(needed, alloc_ctx);
    if (!buf) {
        return IM_IMAGE_NO_MEMORY;
    }
    copy_bytes(buf, data + 13u, needed);
    out->width = w;
    out->height = h;
    out->stride = w * 4u;
    out->format = (im_pixel_format)fmt;
    out->pixels = buf;
    out->byte_size = needed;
    return IM_IMAGE_OK;
}

typedef struct counting_alloc {
    int calls;
    bool fail;
    uint8_t pool[256];
} counting_alloc;

static void *test_alloc(size_t bytes, void *ctx) {
    counting_alloc *ca = (counting_alloc *)ctx;
    ca->calls++;
    if (ca->fail || bytes > sizeof(ca->pool)) {
        return NULL;
    }
    return ca->pool;
}

static bool check_zeroed(const im_image *img) {
    return img->width == 0 && img->height == 0 && img->stride == 0 && img->format == 0 && img->pixels == NULL && img->byte_size == 0;
}

static void write_header(uint8_t *dst, uint32_t w, uint32_t h, uint8_t fmt) {
    dst[0] = 'I'; dst[1] = 'M'; dst[2] = 'F'; dst[3] = 'X';
    dst[4] = (uint8_t)(w & 0xFFu);
    dst[5] = (uint8_t)((w >> 8) & 0xFFu);
    dst[6] = (uint8_t)((w >> 16) & 0xFFu);
    dst[7] = (uint8_t)((w >> 24) & 0xFFu);
    dst[8] = (uint8_t)(h & 0xFFu);
    dst[9] = (uint8_t)((h >> 8) & 0xFFu);
    dst[10] = (uint8_t)((h >> 16) & 0xFFu);
    dst[11] = (uint8_t)((h >> 24) & 0xFFu);
    dst[12] = fmt;
}

#define CHECK(n, cond) do { if (!(cond)) return (n); } while (0)

int im_main(void) {
    /* 1-8. Public error names. */
    CHECK(1, str_eq(im_image_error_name(IM_IMAGE_OK), "IM_IMAGE_OK"));
    CHECK(2, str_eq(im_image_error_name(IM_IMAGE_BAD_ARG), "IM_IMAGE_BAD_ARG"));
    CHECK(3, str_eq(im_image_error_name(IM_IMAGE_INVALID), "IM_IMAGE_INVALID"));
    CHECK(4, str_eq(im_image_error_name(IM_IMAGE_TRUNCATED), "IM_IMAGE_TRUNCATED"));
    CHECK(5, str_eq(im_image_error_name(IM_IMAGE_UNSUPPORTED), "IM_IMAGE_UNSUPPORTED"));
    CHECK(6, str_eq(im_image_error_name(IM_IMAGE_TOO_LARGE), "IM_IMAGE_TOO_LARGE"));
    CHECK(7, str_eq(im_image_error_name(IM_IMAGE_NO_MEMORY), "IM_IMAGE_NO_MEMORY"));
    CHECK(8, str_eq(im_image_error_name((im_image_error)99), "IM_IMAGE_UNKNOWN"));

    /* 9-15. Direct im_image_check_limits cases: NULL, zero, exact limit, byte budget, overflow. */
    size_t bytes = 123;
    CHECK(9, im_image_check_limits(10, 10, NULL, NULL) == IM_IMAGE_BAD_ARG);
    CHECK(10, im_image_check_limits(0, 10, NULL, &bytes) == IM_IMAGE_INVALID && bytes == 0);
    CHECK(11, im_image_check_limits(10, 0, NULL, &bytes) == IM_IMAGE_INVALID && bytes == 0);

    im_image_limits exact_lim = {64, 64, 64u * 64u * 4u};
    CHECK(12, im_image_check_limits(64, 64, &exact_lim, &bytes) == IM_IMAGE_OK && bytes == 16384u);
    /* Width or height exceeding caller limit while byte size (260) fits well inside 16384. */
    CHECK(29, im_image_check_limits(65, 1, &exact_lim, &bytes) == IM_IMAGE_TOO_LARGE && bytes == 0);
    CHECK(30, im_image_check_limits(1, 65, &exact_lim, &bytes) == IM_IMAGE_TOO_LARGE && bytes == 0);

    /* Dimensions fit individually (100 <= 4096), but 100*100*4 = 40000 > 16384 byte budget. */
    im_image_limits byte_lim = {4096, 4096, 16384u};
    CHECK(13, im_image_check_limits(100, 100, &byte_lim, &bytes) == IM_IMAGE_TOO_LARGE && bytes == 0);

    /* Arithmetic overflow with limits high enough to reach stride and byte-size multiplication. */
    im_image_limits huge_lim = {UINT32_MAX, UINT32_MAX, SIZE_MAX};
    CHECK(14, im_image_check_limits(0x40000000u, 1u, &huge_lim, &bytes) == IM_IMAGE_TOO_LARGE && bytes == 0);
    im_image_limits u32_byte_lim = {UINT32_MAX, UINT32_MAX, (size_t)UINT32_MAX};
    CHECK(15, im_image_check_limits(0x3FFFFFFFu, 0xFFFFFFFFu, &u32_byte_lim, &bytes) == IM_IMAGE_TOO_LARGE && bytes == 0);

    /* 16-20. Asymmetric multi-row (3x2) fixture decode with distinct channels and alpha. */
    uint8_t valid_buf[13 + 3 * 2 * 4];
    write_header(valid_buf, 3, 2, (uint8_t)IM_PIXEL_BGRA8);
    static const uint8_t expected_pixels[24] = {
        /* row 0: 3 distinct BGRA pixels */
        0x10, 0x20, 0x30, 0xFF,   0x40, 0x50, 0x60, 0x80,   0x70, 0x80, 0x90, 0x40,
        /* row 1: 3 distinct BGRA pixels */
        0xA1, 0xB2, 0xC3, 0xD4,   0x11, 0x22, 0x33, 0x00,   0xFE, 0xDC, 0xBA, 0x98,
    };
    copy_bytes(valid_buf + 13, expected_pixels, sizeof(expected_pixels));

    counting_alloc ca;
    zero_bytes(&ca, sizeof(ca));
    im_image img;
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(16, decode_fixture(valid_buf, sizeof(valid_buf), NULL, test_alloc, &ca, &img) == IM_IMAGE_OK);
    CHECK(17, ca.calls == 1);
    CHECK(18, img.width == 3 && img.height == 2 && img.stride == 12);
    CHECK(19, img.format == IM_PIXEL_BGRA8 && img.byte_size == 24);
    CHECK(20, img.pixels != NULL && bytes_eq(img.pixels, expected_pixels, 24));

    /* 21-28. Refusal cases: zero allocation attempts and zeroed output struct. */
    uint8_t hdr[13];

    /* 21. Dimensions fit individually, exceed byte budget -> IM_IMAGE_TOO_LARGE, 0 allocs. */
    write_header(hdr, 100, 100, (uint8_t)IM_PIXEL_BGRA8);
    zero_bytes(&ca, sizeof(ca));
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(21, decode_fixture(hdr, sizeof(hdr), &byte_lim, test_alloc, &ca, &img) == IM_IMAGE_TOO_LARGE && ca.calls == 0 && check_zeroed(&img));

    /* 22. Overflowing header with huge limits -> IM_IMAGE_TOO_LARGE, 0 allocs. */
    write_header(hdr, 0x40000000u, 1u, (uint8_t)IM_PIXEL_RGBA8);
    zero_bytes(&ca, sizeof(ca));
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(22, decode_fixture(hdr, sizeof(hdr), &huge_lim, test_alloc, &ca, &img) == IM_IMAGE_TOO_LARGE && ca.calls == 0 && check_zeroed(&img));

    /* 23. Truncated payload -> IM_IMAGE_TRUNCATED, 0 allocs. */
    zero_bytes(&ca, sizeof(ca));
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(23, decode_fixture(valid_buf, sizeof(valid_buf) - 1u, NULL, test_alloc, &ca, &img) == IM_IMAGE_TRUNCATED && ca.calls == 0 && check_zeroed(&img));

    /* 24. Bad magic -> IM_IMAGE_INVALID, 0 allocs. */
    uint8_t bad_magic[13] = {'N', 'O', 'P', 'E', 1, 0, 0, 0, 1, 0, 0, 0, 0};
    zero_bytes(&ca, sizeof(ca));
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(24, decode_fixture(bad_magic, sizeof(bad_magic), NULL, test_alloc, &ca, &img) == IM_IMAGE_INVALID && ca.calls == 0 && check_zeroed(&img));

    /* 25. Unsupported format -> IM_IMAGE_UNSUPPORTED, 0 allocs. */
    write_header(hdr, 1, 1, 9);
    zero_bytes(&ca, sizeof(ca));
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(25, decode_fixture(hdr, sizeof(hdr), NULL, test_alloc, &ca, &img) == IM_IMAGE_UNSUPPORTED && ca.calls == 0 && check_zeroed(&img));

    /* 26. Allocator failure -> IM_IMAGE_NO_MEMORY, zeroed output. */
    zero_bytes(&ca, sizeof(ca));
    ca.fail = true;
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(26, decode_fixture(valid_buf, sizeof(valid_buf), NULL, test_alloc, &ca, &img) == IM_IMAGE_NO_MEMORY && ca.calls == 1 && check_zeroed(&img));

    /* 27-28. NULL args -> IM_IMAGE_BAD_ARG, zeroed output. */
    fill_bytes(&img, 0x5A, sizeof(img));
    CHECK(27, decode_fixture(NULL, 0, NULL, test_alloc, &ca, &img) == IM_IMAGE_BAD_ARG && check_zeroed(&img));
    CHECK(28, decode_fixture(valid_buf, sizeof(valid_buf), NULL, test_alloc, &ca, NULL) == IM_IMAGE_BAD_ARG);

    return 0;
}
