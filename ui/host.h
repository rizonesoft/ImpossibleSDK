/* Impossible UI: versioned, sized host callback table for all system-owned services. Portable. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "tree.h"

/* Current host callback table ABI version (v1: memory + time; v2: full 10-domain table). */
#define IM_HOST_VERSION 2u

/* Default WinUI/Windows caret blink period in milliseconds when host does not supply caret_blink_ms. */
#define IM_HOST_DEFAULT_CARET_BLINK_MS 530u

/* Result status returned by host callback wrappers and im_host_require. */
typedef enum im_host_result {
    IM_HOST_OK = 0,
    IM_HOST_UNSUPPORTED,
    IM_HOST_INVALID,
    IM_HOST_FAILED,
    IM_HOST_CANCELLED
} im_host_result;

/* Standard pointer cursor shapes requested by portable controls. */
typedef enum im_cursor {
    IM_CURSOR_ARROW = 0,
    IM_CURSOR_IBEAM,
    IM_CURSOR_HAND,
    IM_CURSOR_SIZE_WE,
    IM_CURSOR_SIZE_NS,
    IM_CURSOR_WAIT
} im_cursor;

/* System UI sound cues requested by portable controls. */
typedef enum im_sound_id {
    IM_SOUND_NONE = 0,
    IM_SOUND_INVOKE,
    IM_SOUND_FOCUS,
    IM_SOUND_SHOW,
    IM_SOUND_HIDE,
    IM_SOUND_GO_BACK
} im_sound_id;

/* File picker mode requested by portable controls or dialogs. */
typedef enum im_file_pick_kind {
    IM_FILE_PICK_OPEN = 0,
    IM_FILE_PICK_SAVE,
    IM_FILE_PICK_FOLDER
} im_file_pick_kind;

/* Versioned and sized host callback table filled by Windows (src/host.c) or Impossible OS.
   Caller owns output buffers passed to clipboard_get_utf8 and file_pick; host borrows input
   UTF-8 strings (clipboard_set_utf8, open_url, announce) only for the duration of the call. */
typedef struct im_host {
    uint32_t version;
    uint32_t size;
    void *ctx;

    /* v1 prefix: memory (all three must be present as a unit) and monotonic millisecond clock. */
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t new_size);
    void (*free)(void *ctx, void *ptr, size_t size);
    uint32_t (*now_ms)(void *ctx);

    /* v2 additions: clipboard, cursor, caret timing, IME, links, sound, files, screen reader. */
    im_host_result (*clipboard_get_utf8)(void *ctx, char *buf, size_t cap, size_t *out_len);
    im_host_result (*clipboard_set_utf8)(void *ctx, const char *utf8, size_t len);
    im_host_result (*set_cursor)(void *ctx, im_cursor cursor);
    im_host_result (*caret_blink_ms)(void *ctx, uint32_t *out_ms);
    im_host_result (*ime_set_rect)(void *ctx, im_rect rect);
    im_host_result (*open_url)(void *ctx, const char *utf8_url);
    im_host_result (*play_sound)(void *ctx, im_sound_id sound);
    im_host_result (*file_pick)(void *ctx, im_file_pick_kind kind, char *utf8_path, size_t cap, size_t *out_len);
    im_host_result (*announce)(void *ctx, const char *utf8_text);
} im_host;

/* Minimum readable table prefix (header: version, size, ctx). */
#define IM_HOST_MIN_SIZE ((uint32_t)(offsetof(im_host, ctx) + sizeof(((im_host *)0)->ctx)))

/* Byte size of the v1 host prefix (header + alloc, realloc, free, now_ms). */
#define IM_HOST_V1_SIZE ((uint32_t)(offsetof(im_host, now_ms) + sizeof(((im_host *)0)->now_ms)))

/* Validate that host has a supported version (1..IM_HOST_VERSION) and readable header size. */
im_host_result im_host_accept(const im_host *host, const char **out_reason);

/* Check whether a named callback (or "memory" unit) is present within host->size and non-NULL.
   On refusal, sets *out_missing_name to the missing callback name and returns IM_HOST_UNSUPPORTED or IM_HOST_INVALID. */
im_host_result im_host_require(const im_host *host, const char *callback_name, const char **out_missing_name);

/* Return an im_allocator backed by host's memory callbacks; fails safely if any of alloc/realloc/free is absent. */
im_allocator im_host_allocator(const im_host *host);

/* Allocate zeroed bytes through host; returns NULL if host or any of its three memory callbacks is absent. */
void *im_host_alloc(const im_host *host, size_t size);

/* Resize memory through host (ptr==NULL allocates, new_size==0 frees); returns NULL if memory unit is absent. */
void *im_host_realloc(const im_host *host, void *ptr, size_t old_size, size_t new_size);

/* Release memory through host; safe no-op if host, ptr, or memory unit is absent. */
void im_host_free(const im_host *host, void *ptr, size_t size);

/* Return monotonic milliseconds from host, or 0 if now_ms is absent. */
uint32_t im_host_now_ms(const im_host *host);

/* Read UTF-8 clipboard text into caller buffer; sets *out_len=0 and NUL-terminates on unsupported/error. */
im_host_result im_host_clipboard_get_utf8(const im_host *host, char *buf, size_t cap, size_t *out_len);

/* Write UTF-8 text to host clipboard; returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_clipboard_set_utf8(const im_host *host, const char *utf8, size_t len);

/* Request pointer cursor shape from host; returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_set_cursor(const im_host *host, im_cursor cursor);

/* Query caret blink period in ms; sets *out_ms=IM_HOST_DEFAULT_CARET_BLINK_MS and returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_caret_blink_ms(const im_host *host, uint32_t *out_ms);

/* Inform host input method of active caret/composition rectangle; returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_ime_set_rect(const im_host *host, im_rect rect);

/* Ask host to open a UTF-8 URL; returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_open_url(const im_host *host, const char *utf8_url);

/* Ask host to play a UI sound cue; returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_play_sound(const im_host *host, im_sound_id sound);

/* Ask host to open a file/folder picker into caller buffer; sets *out_len=0 and NUL-terminates on unsupported/cancel. */
im_host_result im_host_file_pick(const im_host *host, im_file_pick_kind kind, char *utf8_path, size_t cap, size_t *out_len);

/* Send a UTF-8 accessibility announcement to host screen reader; returns IM_HOST_UNSUPPORTED if absent. */
im_host_result im_host_announce(const im_host *host, const char *utf8_text);
