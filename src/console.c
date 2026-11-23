#include <impossible/base.h>
#include <windows.h>

/* Decodes one UTF-8 character; a malformed byte becomes U+FFFD. */
static uint32_t decode(const unsigned char **at) {
    const unsigned char *s = *at;
    uint32_t lead = *s++, code = lead, least = 0;
    int more = 0;
    if (lead >= 0xC2 && lead <= 0xDF) code = lead & 0x1F, more = 1, least = 0x80;
    else if (lead >= 0xE0 && lead <= 0xEF) code = lead & 0x0F, more = 2, least = 0x800;
    else if (lead >= 0xF0 && lead <= 0xF4) code = lead & 0x07, more = 3, least = 0x10000;
    else if (lead >= 0x80) code = 0xFFFD;
    const unsigned char *after_lead = s;
    for (; more; more--, s++) {
        if ((*s & 0xC0) != 0x80) {
            *at = after_lead;
            return 0xFFFD;
        }
        code = code << 6 | (*s & 0x3F);
    }
    if (code < least || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        *at = after_lead;
        return 0xFFFD;
    }
    *at = s;
    return code;
}

bool im_print(const char *text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode, done;
    if (!out || out == INVALID_HANDLE_VALUE) return false;
    if (!GetConsoleMode(out, &mode)) { /* a file or a pipe takes the bytes as they are */
        size_t left = 0;
        while (text[left]) left++;
        while (left) {
            DWORD part = left > 0x10000 ? 0x10000 : (DWORD)left;
            if (!WriteFile(out, text, part, &done, NULL) || !done) return false;
            text += done, left -= done;
        }
        return true;
    }
    const unsigned char *at = (const unsigned char *)text;
    while (*at) {
        wchar_t wide[512];
        DWORD count = 0;
        while (*at && count < 510) {
            uint32_t code = decode(&at);
            if (code >= 0x10000) {
                code -= 0x10000;
                wide[count++] = (wchar_t)(0xD800 + (code >> 10));
                wide[count++] = (wchar_t)(0xDC00 + (code & 0x3FF));
            } else {
                wide[count++] = (wchar_t)code;
            }
        }
        for (DWORD sent = 0; sent < count; sent += done)
            if (!WriteConsoleW(out, wide + sent, count - sent, &done, NULL) || !done) return false;
    }
    return true;
}
