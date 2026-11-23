/* Impossible SDK: UTF-8 and UTF-16, program arguments and text building. Tier 0. */
#pragma once
#include <impossible/base.h>

/* From UTF-8 to Windows' UTF-16: new memory, zero ended, for im_free; NULL
   without memory. A malformed unit becomes U+FFFD. */
wchar_t *im_wide(const char *text);
char *im_utf8(const wchar_t *text); /* The other way, by the same rules. */

/* The bytes before the zero. */
size_t im_length(const char *text);

/* Whether two texts have the same bytes. */
bool im_equal(const char *a, const char *b);

/* The command line as UTF-8, by Windows' own quoting rules, program name first.
   Free with im_args_free. NULL without memory. */
char **im_args(int *count);
void im_args_free(char **args); /* Gives the memory of im_args back. */

/* Text that grows. After a failed allocation it stays as it was and failed is set. */
typedef struct im_text {
    char *data; /* zero ended once anything was added */
    size_t length, capacity;
    bool failed;
} im_text;

/* Adds text to the end. */
void im_add(im_text *text, const char *more);
void im_add_bytes(im_text *text, const void *more, size_t bytes); /* Adds bytes, zeros too. */
void im_add_number(im_text *text, uint64_t value, int least_digits); /* Adds a number, with zeros in front up to 24 digits. */
void im_text_free(im_text *text); /* Gives the memory back and starts again as empty. */
