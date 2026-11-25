/* Impossible UI: versioned, sized host callback table wrappers and safe defaults. Portable. */
#include "host.h"

static bool str_eq(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static bool host_header_ok(const im_host *host) {
    return host != NULL && host->version >= 1u && host->version <= IM_HOST_VERSION && host->size >= IM_HOST_MIN_SIZE;
}

static uint32_t host_effective_size(const im_host *host) {
    if (!host_header_ok(host)) return 0u;
    if (host->version == 1u && host->size > IM_HOST_V1_SIZE) return IM_HOST_V1_SIZE;
    return host->size;
}

#define HOST_HAS(h, field) \
    (host_effective_size(h) >= (uint32_t)(offsetof(im_host, field) + sizeof((h)->field)) && (h)->field != NULL)

static bool host_has_memory_unit(const im_host *host) {
    return HOST_HAS(host, alloc) && HOST_HAS(host, realloc) && HOST_HAS(host, free);
}

im_host_result im_host_accept(const im_host *host, const char **out_reason) {
    if (!host) {
        if (out_reason) *out_reason = "host is NULL";
        return IM_HOST_INVALID;
    }
    if (host->version == 0u || host->version > IM_HOST_VERSION) {
        if (out_reason) *out_reason = "incompatible host version";
        return IM_HOST_INVALID;
    }
    if (host->size < IM_HOST_MIN_SIZE) {
        if (out_reason) *out_reason = "host size smaller than minimum header";
        return IM_HOST_INVALID;
    }
    if (out_reason) *out_reason = NULL;
    return IM_HOST_OK;
}

im_host_result im_host_require(const im_host *host, const char *callback_name, const char **out_missing_name) {
    if (!callback_name) {
        if (out_missing_name) *out_missing_name = NULL;
        return IM_HOST_INVALID;
    }
    if (im_host_accept(host, NULL) != IM_HOST_OK) {
        if (out_missing_name) *out_missing_name = callback_name;
        return IM_HOST_INVALID;
    }

    bool ok = false;
    const char *missing = callback_name;

    if (str_eq(callback_name, "memory")) {
        if (!HOST_HAS(host, alloc)) missing = "alloc";
        else if (!HOST_HAS(host, realloc)) missing = "realloc";
        else if (!HOST_HAS(host, free)) missing = "free";
        else ok = true;
    } else if (str_eq(callback_name, "alloc")) {
        ok = host_has_memory_unit(host);
        if (!ok && HOST_HAS(host, alloc)) missing = !HOST_HAS(host, realloc) ? "realloc" : "free";
    } else if (str_eq(callback_name, "realloc")) {
        ok = host_has_memory_unit(host);
        if (!ok && HOST_HAS(host, realloc)) missing = !HOST_HAS(host, alloc) ? "alloc" : "free";
    } else if (str_eq(callback_name, "free")) {
        ok = host_has_memory_unit(host);
        if (!ok && HOST_HAS(host, free)) missing = !HOST_HAS(host, alloc) ? "alloc" : "realloc";
    } else if (str_eq(callback_name, "now_ms")) {
        ok = HOST_HAS(host, now_ms);
    } else if (str_eq(callback_name, "clipboard_get_utf8")) {
        ok = HOST_HAS(host, clipboard_get_utf8);
    } else if (str_eq(callback_name, "clipboard_set_utf8")) {
        ok = HOST_HAS(host, clipboard_set_utf8);
    } else if (str_eq(callback_name, "set_cursor")) {
        ok = HOST_HAS(host, set_cursor);
    } else if (str_eq(callback_name, "caret_blink_ms")) {
        ok = HOST_HAS(host, caret_blink_ms);
    } else if (str_eq(callback_name, "ime_set_rect")) {
        ok = HOST_HAS(host, ime_set_rect);
    } else if (str_eq(callback_name, "open_url")) {
        ok = HOST_HAS(host, open_url);
    } else if (str_eq(callback_name, "play_sound")) {
        ok = HOST_HAS(host, play_sound);
    } else if (str_eq(callback_name, "file_pick")) {
        ok = HOST_HAS(host, file_pick);
    } else if (str_eq(callback_name, "announce")) {
        ok = HOST_HAS(host, announce);
    } else {
        if (out_missing_name) *out_missing_name = callback_name;
        return IM_HOST_INVALID;
    }

    if (!ok) {
        if (out_missing_name) *out_missing_name = missing;
        return IM_HOST_UNSUPPORTED;
    }
    if (out_missing_name) *out_missing_name = NULL;
    return IM_HOST_OK;
}

static void *null_alloc(void *ctx, size_t size) {
    (void)ctx;
    (void)size;
    return NULL;
}

static void *null_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size) {
    (void)ctx;
    (void)ptr;
    (void)old_size;
    (void)new_size;
    return NULL;
}

static void null_free(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)ptr;
    (void)size;
}

im_allocator im_host_allocator(const im_host *host) {
    if (host_has_memory_unit(host)) {
        return (im_allocator){host->alloc, host->realloc, host->free, host->ctx};
    }
    return (im_allocator){null_alloc, null_realloc, null_free, NULL};
}

void *im_host_alloc(const im_host *host, size_t size) {
    if (!host_has_memory_unit(host) || size == 0u) return NULL;
    return host->alloc(host->ctx, size);
}

void *im_host_realloc(const im_host *host, void *ptr, size_t old_size, size_t new_size) {
    if (!host_has_memory_unit(host)) return NULL;
    return host->realloc(host->ctx, ptr, old_size, new_size);
}

void im_host_free(const im_host *host, void *ptr, size_t size) {
    if (!ptr || !host_has_memory_unit(host)) return;
    host->free(host->ctx, ptr, size);
}

uint32_t im_host_now_ms(const im_host *host) {
    if (!HOST_HAS(host, now_ms)) return 0u;
    return host->now_ms(host->ctx);
}

im_host_result im_host_clipboard_get_utf8(const im_host *host, char *buf, size_t cap, size_t *out_len) {
    if (out_len) *out_len = 0u;
    if (buf && cap > 0u) buf[0] = '\0';
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!HOST_HAS(host, clipboard_get_utf8)) return IM_HOST_UNSUPPORTED;
    im_host_result res = host->clipboard_get_utf8(host->ctx, buf, cap, out_len);
    if (res != IM_HOST_OK) {
        if (out_len) *out_len = 0u;
        if (buf && cap > 0u) buf[0] = '\0';
    }
    return res;
}

im_host_result im_host_clipboard_set_utf8(const im_host *host, const char *utf8, size_t len) {
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!utf8 && len > 0u) return IM_HOST_INVALID;
    if (!HOST_HAS(host, clipboard_set_utf8)) return IM_HOST_UNSUPPORTED;
    return host->clipboard_set_utf8(host->ctx, utf8, len);
}

im_host_result im_host_set_cursor(const im_host *host, im_cursor cursor) {
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!HOST_HAS(host, set_cursor)) return IM_HOST_UNSUPPORTED;
    return host->set_cursor(host->ctx, cursor);
}

im_host_result im_host_caret_blink_ms(const im_host *host, uint32_t *out_ms) {
    if (out_ms) *out_ms = IM_HOST_DEFAULT_CARET_BLINK_MS;
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!HOST_HAS(host, caret_blink_ms)) return IM_HOST_UNSUPPORTED;
    return host->caret_blink_ms(host->ctx, out_ms);
}

im_host_result im_host_ime_set_rect(const im_host *host, im_rect rect) {
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!HOST_HAS(host, ime_set_rect)) return IM_HOST_UNSUPPORTED;
    return host->ime_set_rect(host->ctx, rect);
}

im_host_result im_host_open_url(const im_host *host, const char *utf8_url) {
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!utf8_url) return IM_HOST_INVALID;
    if (!HOST_HAS(host, open_url)) return IM_HOST_UNSUPPORTED;
    return host->open_url(host->ctx, utf8_url);
}

im_host_result im_host_play_sound(const im_host *host, im_sound_id sound) {
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!HOST_HAS(host, play_sound)) return IM_HOST_UNSUPPORTED;
    return host->play_sound(host->ctx, sound);
}

im_host_result im_host_file_pick(const im_host *host, im_file_pick_kind kind, char *utf8_path, size_t cap, size_t *out_len) {
    if (out_len) *out_len = 0u;
    if (utf8_path && cap > 0u) utf8_path[0] = '\0';
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!HOST_HAS(host, file_pick)) return IM_HOST_UNSUPPORTED;
    im_host_result res = host->file_pick(host->ctx, kind, utf8_path, cap, out_len);
    if (res != IM_HOST_OK) {
        if (out_len) *out_len = 0u;
        if (utf8_path && cap > 0u) utf8_path[0] = '\0';
    }
    return res;
}

im_host_result im_host_announce(const im_host *host, const char *utf8_text) {
    if (!host_header_ok(host)) return IM_HOST_INVALID;
    if (!utf8_text) return IM_HOST_INVALID;
    if (!HOST_HAS(host, announce)) return IM_HOST_UNSUPPORTED;
    return host->announce(host->ctx, utf8_text);
}
