#include <impossible/base.h>
#include <windows.h>

/* The linker's entry point: there is no C runtime. */
__attribute__((noreturn)) void impossible_start(void) {
    ExitProcess((UINT)im_main());
}

void *im_alloc(size_t bytes) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

void im_free(void *memory) {
    if (memory) HeapFree(GetProcessHeap(), 0, memory);
}

/* Steps over one argument. A backslash before a quote makes it an ordinary
   character, except in the program name, where Windows has no escapes. */
static const wchar_t *skip(const wchar_t *at, bool escapes) {
    size_t slashes = 0;
    for (bool quoted = false; *at && (quoted || (*at != L' ' && *at != L'\t')); at++) {
        if (*at == L'"' && !(escapes && slashes % 2)) quoted = !quoted;
        slashes = *at == L'\\' ? slashes + 1 : 0;
    }
    return at;
}

bool im_has_argument(const wchar_t *argument) {
    const wchar_t *at = skip(GetCommandLineW(), false);
    while (*at) {
        while (*at == L' ' || *at == L'\t') at++;
        const wchar_t *end = at, *want = argument;
        while (*want && *end == *want) end++, want++;
        if (end != at && !*want && (!*end || *end == L' ' || *end == L'\t')) return true;
        at = skip(at, true);
    }
    return false;
}
