/* Impossible UI: demand-driven frame scheduling with generation-based damage acknowledgement. Portable. */
#include "frame.h"

void im_frame_init(im_frame_scheduler *sched, uint32_t min_interval_ms) {
    if (!sched) return;
    sched->generation = 0u;
    sched->painted_generation = 0u;
    sched->was_animating = false;
    sched->needs_final_frame = false;
    sched->pending_request = false;
    sched->last_request_ms = 0u;
    sched->min_interval_ms = min_interval_ms;
}

void im_frame_mark_dirty(im_frame_scheduler *sched) {
    if (!sched) return;
    sched->generation++;
}

bool im_frame_needs_tick(const im_frame_scheduler *sched, const im_dirty_list *dirty, bool animating) {
    if (!sched) return false;
    if (sched->generation > sched->painted_generation) return true;
    if (dirty && dirty->count > 0u) return true;
    if (animating) return true;
    if (sched->was_animating && !animating) return true;
    if (sched->needs_final_frame) return true;
    return false;
}

uint64_t im_frame_begin_paint(const im_frame_scheduler *sched) {
    if (!sched) return 0u;
    return sched->generation;
}

void im_frame_on_painted(im_frame_scheduler *sched, uint64_t painted_generation, uint32_t now_ms) {
    if (!sched) return;
    if (painted_generation > sched->painted_generation) {
        sched->painted_generation = painted_generation;
    }
    if (sched->painted_generation >= sched->generation) {
        sched->needs_final_frame = false;
    }
    sched->pending_request = false;
    sched->last_request_ms = now_ms;
}

bool im_frame_step(im_frame_scheduler *sched, const im_dirty_list *dirty, bool animating, uint32_t now_ms,
    im_frame_request_fn request_fn, im_frame_cancel_fn cancel_fn, void *ctx) {
    if (!sched) return false;

    /* When an animation transitions from running to settled (natural end or reduced_motion snap),
       advance the generation and guarantee one final paint so the settled visual state always reaches the screen. */
    if (sched->was_animating && !animating) {
        sched->generation++;
        sched->needs_final_frame = true;
    }
    sched->was_animating = animating;

    bool has_damage = (sched->generation > sched->painted_generation) || (dirty && dirty->count > 0u) || sched->needs_final_frame;
    if (!has_damage && !animating) {
        if (sched->pending_request && cancel_fn) {
            cancel_fn(ctx);
        }
        sched->pending_request = false;
        return false;
    }

    uint32_t elapsed = im_anim_elapsed_ms(sched->last_request_ms, now_ms);
    if (!sched->pending_request || elapsed >= sched->min_interval_ms) {
        uint32_t delay_ms = has_damage ? 0u : sched->min_interval_ms;
        bool accepted = request_fn ? request_fn(ctx, delay_ms) : true;
        if (!accepted) {
            sched->pending_request = false;
            return false;
        }
        sched->pending_request = true;
        sched->last_request_ms = now_ms;
        return true;
    }
    return false;
}
