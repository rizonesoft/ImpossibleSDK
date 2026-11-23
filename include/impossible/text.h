/* Impossible SDK: UTF-8 and UTF-16, program arguments and text building. Tier 0. */
#pragma once
#include <impossible/base.h>

/* New memory, zero ended; NULL without memory. A malformed unit becomes U+FFFD. */
wchar_t *im_wide(const char *text);
char *im_utf8(const wchar_t *text);

size_t im_length(const char *text);
bool im_equal(const char *a, const char *b);

/* The command line as UTF-8, by Windows' own quoting rules, program name first.
   Free with im_args_free. NULL without memory. */
char **im_args(int *count);
void im_args_free(char **args);

/* Text that grows. After a failed allocation it stays as it was and failed is set. */
typedef struct im_text {
    char *data; /* zero ended once anything was added */
    size_t length, capacity;
    bool failed;
} im_text;
void im_add(im_text *text, const char *more);
void im_add_bytes(im_text *text, const void *more, size_t bytes);
void im_add_number(im_text *text, uint64_t value, int least_digits);
void im_text_free(im_text *text);
