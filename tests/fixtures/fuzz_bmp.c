/* Impossible SDK: libFuzzer + ASan + UBSan harness for parts/bmp/bmp.c. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "bmp.h"

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
    im_bmp_alloc alloc = {fuzz_alloc, fuzz_free, NULL};
    im_bmp_info info;
    (void)im_bmp_probe(data, size, &lim, &info);

    im_image img;
    im_image_error err = im_bmp_decode(data, size, IM_PIXEL_RGBA8, &lim, &alloc, &img);
    if (err == IM_IMAGE_OK) {
        if (img.pixels) alloc.free(alloc.ctx, img.pixels, img.byte_size);
        im_image img_bgra;
        im_image_error err2 = im_bmp_decode(data, size, IM_PIXEL_BGRA8, &lim, &alloc, &img_bgra);
        if (err2 != IM_IMAGE_OK) abort();
        if (img_bgra.pixels) alloc.free(alloc.ctx, img_bgra.pixels, img_bgra.byte_size);
    } else {
        if (img.pixels != NULL || img.width != 0u || img.height != 0u || img.byte_size != 0u) {
            abort();
        }
    }
    return 0;
}
