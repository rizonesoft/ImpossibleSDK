/* The routines the compiler calls on its own for copies and clears. Built with
   -fno-builtin, so the loops are not turned back into calls to themselves. */
#include <impossible/base.h>

void *memcpy(void *restrict to, const void *restrict from, size_t bytes) {
    unsigned char *d = to;
    const unsigned char *s = from;
    for (size_t i = 0; i < bytes; i++) d[i] = s[i];
    return to;
}

void *memmove(void *to, const void *from, size_t bytes) {
    unsigned char *d = to;
    const unsigned char *s = from;
    if ((uintptr_t)d < (uintptr_t)s) {
        for (size_t i = 0; i < bytes; i++) d[i] = s[i];
    } else {
        while (bytes--) d[bytes] = s[bytes];
    }
    return to;
}

void *memset(void *to, int value, size_t bytes) {
    unsigned char *d = to;
    for (size_t i = 0; i < bytes; i++) d[i] = (unsigned char)value;
    return to;
}

int memcmp(const void *a, const void *b, size_t bytes) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < bytes; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
