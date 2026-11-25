/* Impossible SDK: native Linux libFuzzer harness for parts/ico/read.c. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "ico.h"

static void *fuzz_alloc(void *ctx, size_t size) {
    (void)ctx;
    if (size == 0u || size > 4u * 1024u * 1024u) return NULL;
    return malloc(size);
}

static void fuzz_free(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    free(ptr);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    im_ico_type type = IM_ICO_TYPE_ICON;
    uint16_t count = 0u;
    if (im_ico_probe(data, size, &type, &count) != IM_IMAGE_OK) return 0;

    im_ico_entry_info entries[IM_ICO_MAX_ENTRIES];
    uint16_t list_count = 0u;
    if (im_ico_list(data, size, &type, entries, IM_ICO_MAX_ENTRIES, &list_count) != IM_IMAGE_OK) {
        return 0;
    }

    uint32_t scale = (size > 0u) ? (100u + (uint32_t)(data[size - 1u] & 3u) * 25u) : 100u;
    int32_t chosen = im_ico_choose_entry_scaled(entries, list_count, 32u, 32u, scale, 32u);

    im_image_limits lim = { 256u, 256u, 256u * 1024u };
    im_ico_alloc alloc = { fuzz_alloc, fuzz_free, NULL };

    uint16_t to_decode[2];
    uint16_t n_dec = 1u;
    to_decode[0] = 0u;
    if (chosen > 0 && chosen < (int32_t)list_count) {
        to_decode[n_dec++] = (uint16_t)chosen;
    } else if (list_count > 1u) {
        to_decode[n_dec++] = (uint16_t)(list_count - 1u);
    }

    for (uint16_t k = 0; k < n_dec; ++k) {
        im_image img = {0};
        uint8_t *inv = NULL;
        size_t inv_len = 0u;
        im_ico_entry_info info;
        if (im_ico_decode_entry(data, size, to_decode[k], IM_PIXEL_BGRA8, &lim, &alloc,
                                &img, &inv, &inv_len, &info) == IM_IMAGE_OK) {
            if (img.pixels) alloc.free(alloc.ctx, img.pixels, img.byte_size);
            if (inv) alloc.free(alloc.ctx, inv, inv_len);
        }
    }
    return 0;
}
