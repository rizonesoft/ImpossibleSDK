/* Impossible SDK: start-up, memory and console. Tier 0. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Every program defines this. Its result is the exit code. */
int im_main(void);

/* Zeroed memory, or NULL when there is none. */
void *im_alloc(size_t bytes);

/* Gives memory of im_alloc back. im_free(NULL) does nothing. */
void im_free(void *memory);

/* True when the command line has this exact argument, unquoted. */
bool im_has_argument(const wchar_t *argument);

/* Writes UTF-8 text to standard output; false when it could not all be written. */
bool im_print(const char *text);

/* The four memory routines are as C defines them, and the compiler calls them too. This one copies between places that do not overlap. */
void *memcpy(void *restrict to, const void *restrict from, size_t bytes);
void *memmove(void *to, const void *from, size_t bytes); /* Copies between places that may overlap. */
void *memset(void *to, int value, size_t bytes); /* Fills with one byte. */
int memcmp(const void *a, const void *b, size_t bytes); /* Compares byte by byte: below, at or above 0. */
