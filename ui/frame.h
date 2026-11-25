/* Impossible UI: demand-driven frame scheduling with generation-based damage acknowledgement. Portable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "animate.h"
#include "dirty.h"

/* Host callback invoked when a frame must be scheduled (delay_ms == 0 means immediate). */
typedef void (*im_frame_request_fn)(void *ctx, uint32_t delay_ms);

/* Host callback invoked when an idle window cancels any previously armed frame timer. */
typedef void (*im_frame_cancel_fn)(void *ctx);

/* Demand-driven frame scheduler: wakes the host only while damage or animation is pending. */
typedef struct im_frame_scheduler {
    uint64_t generation;
    uint64_t painted_generation;
    bool was_animating;
    bool needs_final_frame;
    bool pending_request;
    uint32_t last_request_ms;
    uint32_t min_interval_ms;
} im_frame_scheduler;

/* Initialize a frame scheduler with the given minimum animation frame interval in milliseconds. */
void im_frame_init(im_frame_scheduler *sched, uint32_t min_interval_ms);

/* Record that new damage or state change occurred, advancing the damage generation counter. */
void im_frame_mark_dirty(im_frame_scheduler *sched);

/* Return true if the window currently owes a frame (pending damage, dirty rects, animation, or final settled frame). */
bool im_frame_needs_tick(const im_frame_scheduler *sched, const im_dirty_list *dirty, bool animating);

/* Snapshot the current damage generation at the start of a paint pass (pass the returned token to im_frame_on_painted). */
uint64_t im_frame_begin_paint(const im_frame_scheduler *sched);

/* Acknowledge that the paint pass rendered up to painted_generation, preserving any damage queued during paint. */
void im_frame_on_painted(im_frame_scheduler *sched, uint64_t painted_generation, uint32_t now_ms);

/* Advance scheduler state at now_ms: calls request_fn only when a frame is owed; calls cancel_fn when idle. */
bool im_frame_step(im_frame_scheduler *sched, const im_dirty_list *dirty, bool animating, uint32_t now_ms,
    im_frame_request_fn request_fn, im_frame_cancel_fn cancel_fn, void *ctx);
