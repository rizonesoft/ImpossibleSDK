#include <impossible/text.h>
#include <windows.h>

size_t im_length(const char *text) {
    size_t length = 0;
    while (text[length]) length++;
    return length;
}

bool im_equal(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* Reads one character; a malformed byte is U+FFFD and only that byte is used. */
static uint32_t read_utf8(const unsigned char **at) {
    const unsigned char *s = *at;
    uint32_t lead = *s++, code = lead, least = 0;
    int more = 0;
    if (lead >= 0xC2 && lead <= 0xDF) code = lead & 0x1F, more = 1, least = 0x80;
    else if (lead >= 0xE0 && lead <= 0xEF) code = lead & 0x0F, more = 2, least = 0x800;
    else if (lead >= 0xF0 && lead <= 0xF4) code = lead & 0x07, more = 3, least = 0x10000;
    else if (lead >= 0x80) code = 0xFFFD;
    *at = s;
    for (; more; more--, s++) {
        if ((*s & 0xC0) != 0x80) return 0xFFFD;
        code = code << 6 | (*s & 0x3F);
    }
    if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return 0xFFFD;
    *at = s;
    return code;
}

wchar_t *im_wide(const char *text) {
    wchar_t *wide = im_alloc((im_length(text) + 1) * sizeof *wide); /* never more units than bytes */
    if (!wide) return NULL;
    wchar_t *out = wide;
    for (const unsigned char *at = (const unsigned char *)text; *at;) {
        uint32_t code = read_utf8(&at);
        if (code >= 0x10000) {
            *out++ = (wchar_t)(0xD800 + ((code - 0x10000) >> 10));
            *out++ = (wchar_t)(0xDC00 + ((code - 0x10000) & 0x3FF));
        } else {
            *out++ = (wchar_t)code;
        }
    }
    return wide;
}

char *im_utf8(const wchar_t *text) {
    size_t units = 0;
    while (text[units]) units++;
    if (units > SIZE_MAX / 3 - 1) return NULL;
    char *bytes = im_alloc(units * 3 + 1), *out = bytes;
    if (!bytes) return NULL;
    for (size_t i = 0; i < units; i++) {
        uint32_t code = text[i];
        if (code >= 0xD800 && code <= 0xDBFF && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
            code = 0x10000 + ((code - 0xD800) << 10) + (text[++i] - 0xDC00u);
        else if (code >= 0xD800 && code <= 0xDFFF)
            code = 0xFFFD;
        if (code < 0x80) {
            *out++ = (char)code;
        } else if (code < 0x800) {
            *out++ = (char)(0xC0 | code >> 6), *out++ = (char)(0x80 | (code & 63));
        } else if (code < 0x10000) {
            *out++ = (char)(0xE0 | code >> 12), *out++ = (char)(0x80 | (code >> 6 & 63)), *out++ = (char)(0x80 | (code & 63));
        } else {
            *out++ = (char)(0xF0 | code >> 18), *out++ = (char)(0x80 | (code >> 12 & 63));
            *out++ = (char)(0x80 | (code >> 6 & 63)), *out++ = (char)(0x80 | (code & 63));
        }
    }
    return bytes;
}

/* One argument into out, which is as long as the command line. The rules are
   the C runtime's: backslashes are special only before a quote, and two quotes
   inside quotes are one. The program name has no escapes. */
static const wchar_t *argument(const wchar_t *at, wchar_t *out, bool name) {
    bool quoted = false;
    for (;;) {
        size_t slashes = 0;
        if (!name) {
            while (*at == L'\\') slashes++, at++;
            if (*at != L'"') {
                while (slashes) *out++ = L'\\', slashes--;
            } else {
                for (; slashes >= 2; slashes -= 2) *out++ = L'\\';
                if (slashes) {
                    *out++ = *at++;
                    continue;
                }
            }
        }
        if (*at == L'"') {
            if (!name && quoted && at[1] == L'"') *out++ = L'"', at += 2;
            else quoted = !quoted, at++;
        } else if (!*at || (!quoted && (*at == L' ' || *at == L'\t'))) {
            break;
        } else {
            *out++ = *at++;
        }
    }
    *out = 0;
    return at;
}

void im_args_free(char **args) {
    for (char **arg = args; args && *arg; arg++) im_free(*arg);
    im_free(args);
}

char **im_args(int *count) {
    const wchar_t *at = GetCommandLineW();
    size_t length = 0;
    while (at[length]) length++;
    wchar_t *one = im_alloc((length + 1) * sizeof *one);
    char **args = im_alloc((length / 2 + 2) * sizeof *args);
    int found = 0;
    if (one && args) {
        bool name = true;
        while (name || *at) {
            at = argument(at, one, name);
            name = false;
            if (!(args[found] = im_utf8(one))) goto failed;
            found++;
            while (*at == L' ' || *at == L'\t') at++;
        }
        im_free(one);
        *count = found;
        return args;
    }
failed:
    im_free(one);
    im_args_free(args);
    return NULL;
}

void im_add_bytes(im_text *text, const void *more, size_t bytes) {
    if (text->failed) return;
    if (bytes > SIZE_MAX / 4 - text->length) {
        text->failed = true;
        return;
    }
    size_t need = text->length + bytes + 1;
    if (need > text->capacity) {
        size_t capacity = need * 2 < 256 ? 256 : need * 2;
        char *data = im_alloc(capacity);
        if (!data) {
            text->failed = true;
            return;
        }
        if (text->data) memcpy(data, text->data, text->length);
        memcpy(data + text->length, more, bytes); /* before the old memory goes: more may be in it */
        im_free(text->data);
        text->data = data;
        text->capacity = capacity;
    } else {
        memmove(text->data + text->length, more, bytes);
    }
    text->length += bytes;
    text->data[text->length] = 0;
}

void im_add(im_text *text, const char *more) {
    im_add_bytes(text, more, im_length(more));
}

void im_add_number(im_text *text, uint64_t value, int least_digits) {
    char digits[24];
    int at = sizeof digits;
    do digits[--at] = (char)('0' + value % 10), value /= 10;
    while ((value || (int)sizeof digits - at < least_digits) && at);
    im_add_bytes(text, digits + at, sizeof digits - (size_t)at);
}

void im_text_free(im_text *text) {
    im_free(text->data);
    *text = (im_text){0};
}
