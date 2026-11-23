/* Exits 0, or with the number of the first check that failed. */
#include <impossible/file.h>
#include <impossible/text.h>

/* More than one page of locals, so the compiler calls the stack probe. */
__attribute__((noinline)) static int deep(int seed) {
    volatile unsigned char page[300000];
    for (size_t i = 0; i < sizeof page; i += 512) page[i] = (unsigned char)(seed + (int)i);
    return page[512 * 100] == (unsigned char)(seed + 51200);
}

static bool count_entry(void *user, const char *name, bool folder) {
    (void)name, (void)folder;
    ++*(int *)user;
    return true;
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

    int count;
    char **args = im_args(&count);
    if (!args || count != 6) return 8;
    if (!im_equal(args[1], "--first") || !im_equal(args[2], "not inside quotes") || !im_equal(args[3], "a\"b")) return 9;
    if (!im_equal(args[4], "tail\\") || !im_equal(args[5], "--last") || args[6]) return 10;
    im_args_free(args);

    wchar_t *wide = im_wide("a\xC3\xA9\xF0\x9F\x9A\x80\xFF");
    if (!wide || wide[1] != 0xE9 || wide[2] != 0xD83D || wide[3] != 0xDE80 || wide[4] != 0xFFFD || wide[5]) return 11;
    char *back = im_utf8(wide);
    if (!back || !im_equal(back, "a\xC3\xA9\xF0\x9F\x9A\x80\xEF\xBF\xBD")) return 12;
    im_free(wide);
    im_free(back);
    im_text text = {0};
    im_add_number(&text, 7, 3), im_add(&text, "-"), im_add_number(&text, 18446744073709551615ull, 0);
    if (!im_equal(text.data, "007-18446744073709551615")) return 13;
    im_text_free(&text);

    im_text folder = {0};
    im_add(&folder, "im-selftest-"), im_add_number(&folder, im_now(), 0);
    im_text file = {0};
    im_add(&file, folder.data), im_add(&file, "\\caf\xC3\xA9.txt");
    char *data;
    size_t length;
    if (im_read(file.data, 10, &data, &length) != IM_MISSING) return 14;
    if (im_make_folder(folder.data) != IM_OK || im_make_folder(folder.data) != IM_OK) return 15;
    if (im_write(file.data, "one", 3, false, "a") != IM_OK || im_write(file.data, "two", 3, false, "b") != IM_EXISTS) return 16;
    if (im_read(file.data, 3, &data, &length) != IM_OK || length != 3 || !im_equal(data, "one")) return 17;
    im_free(data);
    if (im_read(file.data, 2, &data, &length) != IM_TOO_LARGE) return 18;
    if (im_write(file.data, "", 0, true, "c") != IM_OK || im_read(file.data, 3, &data, &length) != IM_OK || length) return 19;
    im_free(data);
    int entries = 0;
    if (im_list(folder.data, count_entry, &entries) != IM_OK || entries != 1) return 20; /* no temporary file is left */
    if (im_delete(file.data) != IM_OK || im_exists(file.data)) return 21;
    return 0;
}
