/* Impossible SDK: libFuzzer + ASan + UBSan harness for parts/png/png.c. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "png.h"

static void *fuzz_alloc(void *ctx, size_t size) {
    (void)ctx;
    return malloc(size ? size : 1u);
}

static void fuzz_free(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    free(ptr);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    im_image_limits lim = {2048u, 2048u, 4u * 1024u * 1024u};
    im_png_alloc alloc = {fuzz_alloc, fuzz_free, NULL};
    im_png_info info;
    (void)im_png_probe(data, size, &lim, &info);

    im_image img;
    im_pixel_format fmt = (size > 0u && (data[size - 1u] & 1u)) ? IM_PIXEL_BGRA8 : IM_PIXEL_RGBA8;
    im_image_error err = im_png_decode(data, size, fmt, &lim, &alloc, &img);
    if (err == IM_IMAGE_OK) {
        if (img.pixels) alloc.free(alloc.ctx, img.pixels, img.byte_size);
    } else {
        if (img.pixels != NULL || img.width != 0u || img.height != 0u || img.byte_size != 0u) {
            abort();
        }
    }
    return 0;
}
