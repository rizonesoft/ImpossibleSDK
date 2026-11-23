/* Exits 0, or with the number of the first check that failed. */
#include <impossible/base.h>

/* More than one page of locals, so the compiler calls the stack probe. */
__attribute__((noinline)) static int deep(int seed) {
    volatile unsigned char page[300000];
    for (size_t i = 0; i < sizeof page; i += 512) page[i] = (unsigned char)(seed + (int)i);
    return page[512 * 100] == (unsigned char)(seed + 51200);
}

int im_main(void) {
    unsigned char bytes[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!deep(3)) return 1;
    memmove(bytes + 2, bytes, 6);
    if (memcmp(bytes, (unsigned char[]){0, 1, 0, 1, 2, 3, 4, 5}, 8)) return 2;
    memmove(bytes, bytes + 2, 6);
    if (memcmp(bytes, (unsigned char[]){0, 1, 2, 3, 4, 5, 4, 5}, 8)) return 3;
    unsigned char *memory = im_alloc(100000);
    if (!memory || memory[0] || memory[99999]) return 4;
    im_free(memory);
    im_free(NULL);
    if (!im_has_argument(L"--last") || !im_has_argument(L"--first")) return 5;
    if (im_has_argument(L"--las") || im_has_argument(L"--lastly") || im_has_argument(L"")) return 6;
    if (im_has_argument(L"inside")) return 7;
    return 0;
}
