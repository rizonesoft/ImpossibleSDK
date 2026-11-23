/* Impossible SDK: files and folders, with UTF-8 paths. Tier 0. */
#pragma once
#include <impossible/base.h>

typedef enum im_result { IM_OK, IM_EXISTS, IM_MISSING, IM_TOO_LARGE, IM_FAILED } im_result;

/* The whole file in new memory, with a zero after it. Refuses more than most bytes. */
im_result im_read(const char *path, size_t most, char **data, size_t *length);

/* Writes a file so that it is there complete or not at all. With replace false
   it makes the file once: whoever is second gets IM_EXISTS. The unique part
   names the temporary file and must differ between callers. */
im_result im_write(const char *path, const void *data, size_t length, bool replace, const char *unique);

im_result im_make_folder(const char *path); /* IM_OK too when it is there */
im_result im_move(const char *from, const char *to);
im_result im_delete(const char *path);
bool im_exists(const char *path);

/* Calls each for every entry but . and .., until it gives false. */
im_result im_list(const char *path, bool (*each)(void *user, const char *name, bool folder), void *user);

/* All of standard input, or IM_TOO_LARGE. */
im_result im_read_input(size_t most, char **data, size_t *length);

uint64_t im_now(void); /* microseconds since 1970, UTC */
