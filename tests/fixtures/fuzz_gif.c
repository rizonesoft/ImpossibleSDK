/* Impossible SDK: libFuzzer + ASan + UBSan harness for parts/gif/gif.c. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "gif.h"

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
    im_image_limits lim = {1024u, 1024u, 4u * 1024u * 1024u};
    im_gif_alloc alloc = {fuzz_alloc, fuzz_free, NULL};
    im_gif_info info;
    (void)im_gif_probe(data, size, &lim, &info);

    im_image img;
    im_image_error e1 = im_gif_decode(data, size, IM_PIXEL_RGBA8, &lim, &alloc, &img);
    if (e1 == IM_IMAGE_OK) {
        if (img.pixels) alloc.free(alloc.ctx, img.pixels, img.byte_size);
        im_image img_b;
        if (im_gif_decode(data, size, IM_PIXEL_BGRA8, &lim, &alloc, &img_b) != IM_IMAGE_OK) abort();
        if (img_b.pixels) alloc.free(alloc.ctx, img_b.pixels, img_b.byte_size);
    } else if (img.pixels != NULL || img.width != 0u || img.height != 0u || img.byte_size != 0u) {
        abort();
    }

    im_gif_anim anim;
    im_image_error e2 = im_gif_decode_anim(data, size, IM_PIXEL_RGBA8, &lim, 32u, &alloc, &anim);
    if (e2 == IM_IMAGE_OK) {
        im_gif_anim_free(&alloc, &anim);
        im_gif_anim anim_b;
        if (im_gif_decode_anim(data, size, IM_PIXEL_BGRA8, &lim, 32u, &alloc, &anim_b) != IM_IMAGE_OK) abort();
        im_gif_anim_free(&alloc, &anim_b);
    } else if (anim.frames != NULL || anim.frame_count != 0u || anim.width != 0u || anim.height != 0u) {
        abort();
    }
    return 0;
}
