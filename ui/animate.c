/* Impossible UI: fixed-point animation curves, keyframe tracks and brush transitions. Portable. */
#include "animate.h"

uint32_t im_anim_elapsed_ms(uint32_t start_ms, uint32_t now_ms) {
    uint32_t delta = now_ms - start_ms;
    if (delta >= 0x80000000u) return 0u;
    return delta;
}

static im_fixed clamp_unit(im_fixed v) {
    if (v < 0) return 0;
    if (v > IM_ONE) return IM_ONE;
    return v;
}

im_fixed im_spline_eval_ratio(im_fixed x1, im_fixed y1, im_fixed x2, im_fixed y2, uint32_t num, uint32_t den) {
    if (den == 0u || num >= den) return IM_ONE;
    if (num == 0u) return 0;

    im_fixed cx1 = clamp_unit(x1);
    im_fixed cx2 = clamp_unit(x2);

    /* Fast path for exact linear identity spline. */
    if (cx1 == y1 && cx2 == y2) {
        return (im_fixed)((((uint64_t)num << 16) + (den / 2u)) / den);
    }

    /* Represent target x in Q30 [0, 2^30] without pre-quantizing num/den to 16.16. */
    const uint64_t q30 = 1ULL << 30;
    uint64_t target = (((uint64_t)num << 30) + ((uint64_t)den >> 1)) / (uint64_t)den;
    if (target >= q30) return IM_ONE;

    /* Monotonic 30-step bisection solving B_x(u) = target in Q30. */
    uint64_t lo = 0;
    uint64_t hi = q30;
    for (int i = 0; i < 30; i++) {
        uint64_t u = (lo + hi) >> 1;
        uint64_t om = q30 - u;
        uint64_t u2 = (u * u) >> 30;
        uint64_t u3 = (u2 * u) >> 30;
        uint64_t om2 = (om * om) >> 30;
        uint64_t c1 = 3ULL * ((om2 * u) >> 30);
        uint64_t c2 = 3ULL * ((om * u2) >> 30);
        uint64_t bx = ((c1 * (uint64_t)cx1 + 32768ULL) >> 16)
                    + ((c2 * (uint64_t)cx2 + 32768ULL) >> 16)
                    + u3;
        if (bx < target) {
            lo = u;
        } else {
            hi = u;
        }
    }

    uint64_t u = (lo + hi) >> 1;
    uint64_t om = q30 - u;
    uint64_t u2 = (u * u) >> 30;
    uint64_t u3 = (u2 * u) >> 30;
    uint64_t om2 = (om * om) >> 30;
    int64_t c1 = (int64_t)(3ULL * ((om2 * u) >> 30));
    int64_t c2 = (int64_t)(3ULL * ((om * u2) >> 30));
    int64_t t1 = c1 * (int64_t)y1;
    int64_t t2 = c2 * (int64_t)y2;
    int64_t by_q30 = (t1 >= 0 ? (t1 + 32768) / 65536 : (t1 - 32768) / 65536)
                   + (t2 >= 0 ? (t2 + 32768) / 65536 : (t2 - 32768) / 65536)
                   + (int64_t)u3;
    if (by_q30 >= 0) {
        return (im_fixed)((by_q30 + (1LL << 13)) >> 14);
    }
    return (im_fixed)-((-by_q30 + (1LL << 13)) >> 14);
}

im_fixed im_spline_eval(im_fixed x1, im_fixed y1, im_fixed x2, im_fixed y2, im_fixed t) {
    if (t <= 0) return 0;
    if (t >= IM_ONE) return IM_ONE;
    return im_spline_eval_ratio(x1, y1, x2, y2, (uint32_t)t, (uint32_t)IM_ONE);
}

im_fixed im_lerp_fixed_ratio(im_fixed from, im_fixed to, uint32_t num, uint32_t den) {
    if (den == 0u || num >= den) return to;
    if (num == 0u) return from;
    int64_t delta = (int64_t)to - (int64_t)from;
    int64_t prod = delta * (int64_t)num;
    int64_t half = (int64_t)(den >> 1);
    int64_t step = prod >= 0 ? (prod + half) / (int64_t)den : (prod - half) / (int64_t)den;
    return (im_fixed)((int64_t)from + step);
}

im_fixed im_lerp_fixed(im_fixed from, im_fixed to, im_fixed progress) {
    if (progress <= 0) return from;
    if (progress >= IM_ONE) return to;
    int64_t delta = (int64_t)to - (int64_t)from;
    int64_t prod = delta * (int64_t)progress;
    int64_t step = prod >= 0 ? (prod + (IM_ONE / 2)) >> 16 : -((-prod + (IM_ONE / 2)) >> 16);
    return (im_fixed)((int64_t)from + step);
}

static uint32_t lerp_chan_ratio(uint32_t c0, uint32_t c1, uint32_t num, uint32_t den) {
    int64_t delta = (int64_t)c1 - (int64_t)c0;
    int64_t prod = delta * (int64_t)num;
    int64_t half = (int64_t)(den >> 1);
    int64_t step = prod >= 0 ? (prod + half) / (int64_t)den : (prod - half) / (int64_t)den;
    int64_t v = (int64_t)c0 + step;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint32_t)v;
}

uint32_t im_lerp_argb_ratio(uint32_t from_argb, uint32_t to_argb, uint32_t num, uint32_t den) {
    if (den == 0u || num >= den) return to_argb;
    if (num == 0u) return from_argb;
    uint32_t a = lerp_chan_ratio((from_argb >> 24) & 0xFFu, (to_argb >> 24) & 0xFFu, num, den);
    uint32_t r = lerp_chan_ratio((from_argb >> 16) & 0xFFu, (to_argb >> 16) & 0xFFu, num, den);
    uint32_t g = lerp_chan_ratio((from_argb >> 8) & 0xFFu, (to_argb >> 8) & 0xFFu, num, den);
    uint32_t b = lerp_chan_ratio(from_argb & 0xFFu, to_argb & 0xFFu, num, den);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t im_lerp_argb(uint32_t from_argb, uint32_t to_argb, im_fixed progress) {
    if (progress <= 0) return from_argb;
    if (progress >= IM_ONE) return to_argb;
    return im_lerp_argb_ratio(from_argb, to_argb, (uint32_t)progress, (uint32_t)IM_ONE);
}

uint32_t im_template_value_to_argb(const im_template_value *val, const im_palette *palette) {
    if (!val || !palette) return 0u;
    if (val->kind == IM_TVAL_COLOR) {
        if (palette->theme == IM_DARK) return (uint32_t)val->a;
        if (palette->theme == IM_LIGHT) return (uint32_t)val->b;
        if (val->d >= 0 && val->d < IM_SLOT_COUNT) return palette->slot[val->d];
        return (uint32_t)val->c;
    }
    if (val->kind == IM_TVAL_BRUSH) {
        if (val->a >= 0 && val->a < IM_BRUSH_COUNT) {
            return im_brush_over(palette, (enum im_brush_id)val->a, 0, IM_ONE, IM_ONE);
        }
    }
    return 0u;
}

im_fixed im_template_value_to_fixed(const im_template_value *val, const im_palette *palette, im_fixed fallback) {
    if (!val) return fallback;
    if (val->kind == IM_TVAL_FIXED) return val->a;
    if (val->kind == IM_TVAL_THEME_VALUE && palette) {
        if (val->a >= 0 && val->a < IM_VALUE_COUNT) {
            return im_value(palette, (enum im_value_id)val->a)[0];
        }
    }
    if (val->kind == IM_TVAL_INT) return (im_fixed)((int64_t)val->a << 16);
    return fallback;
}

uint32_t im_anim_duration_ms(const im_template_anim *anim, const im_template_keyframe *keyframes) {
    if (!anim) return 0u;
    switch (anim->kind) {
    case IM_ANIM_DOUBLE:
        return anim->duration_ms;
    case IM_ANIM_OBJECT_KEYFRAMES:
    case IM_ANIM_DOUBLE_KEYFRAMES:
    case IM_ANIM_COLOR_KEYFRAMES: {
        uint32_t max_t = 0u;
        if (keyframes) {
            for (uint16_t i = 0; i < anim->kf_count; i++) {
                uint32_t kt = keyframes[anim->kf_start + i].key_time_ms;
                if (kt > max_t) max_t = kt;
            }
        }
        return max_t;
    }
    case IM_ANIM_REPOSITION_THEME:
        return anim->duration_ms ? anim->duration_ms : IM_REPOSITION_DURATION_MS;
    case IM_ANIM_FADE_IN_THEME:
        return anim->duration_ms ? anim->duration_ms : IM_FADE_IN_DURATION_MS;
    case IM_ANIM_FADE_OUT_THEME:
        return anim->duration_ms ? anim->duration_ms : IM_FADE_OUT_DURATION_MS;
    case IM_ANIM_DRAG_OVER_THEME:
        return anim->duration_ms ? anim->duration_ms : IM_DRAG_OVER_DURATION_MS;
    case IM_ANIM_DRAG_ITEM_THEME:
        return anim->duration_ms ? anim->duration_ms : IM_DRAG_ITEM_DURATION_MS;
    case IM_ANIM_DROP_TARGET_THEME:
        return anim->duration_ms ? anim->duration_ms : IM_DROP_TARGET_DURATION_MS;
    }
    return 0u;
}

im_fixed im_anim_eval_fixed(const im_template_anim *anim, const im_template_keyframe *keyframes, const im_palette *palette,
    im_fixed start_val, uint32_t start_ms, uint32_t now_ms, bool reduced_motion) {
    if (!anim) return start_val;
    uint32_t elapsed = im_anim_elapsed_ms(start_ms, now_ms);

    switch (anim->kind) {
    case IM_ANIM_DOUBLE: {
        im_fixed from = anim->has_from ? im_template_value_to_fixed(&anim->from_val, palette, start_val) : start_val;
        im_fixed to = anim->has_to ? im_template_value_to_fixed(&anim->to_val, palette, from) : from;
        if (reduced_motion || anim->duration_ms == 0u || elapsed >= anim->duration_ms) return to;
        return im_lerp_fixed_ratio(from, to, elapsed, anim->duration_ms);
    }
    case IM_ANIM_DOUBLE_KEYFRAMES: {
        if (!keyframes || anim->kf_count == 0u) return start_val;
        const im_template_keyframe *kfs = &keyframes[anim->kf_start];
        if (reduced_motion) {
            return im_template_value_to_fixed(&kfs[anim->kf_count - 1u].value, palette, start_val);
        }
        im_fixed prev_val = start_val;
        uint32_t prev_t = 0u;
        for (uint16_t i = 0; i < anim->kf_count; i++) {
            const im_template_keyframe *kf = &kfs[i];
            im_fixed kf_val = im_template_value_to_fixed(&kf->value, palette, prev_val);
            if (elapsed < kf->key_time_ms) {
                uint32_t seg_dur = kf->key_time_ms - prev_t;
                uint32_t seg_el = elapsed - prev_t;
                if (kf->kind == IM_KF_DISCRETE || seg_dur == 0u) {
                    return prev_val;
                }
                if (kf->kind == IM_KF_SPLINE_DOUBLE) {
                    im_fixed p = im_spline_eval_ratio(kf->spline[0], kf->spline[1], kf->spline[2], kf->spline[3], seg_el, seg_dur);
                    return im_lerp_fixed(prev_val, kf_val, p);
                }
                return im_lerp_fixed_ratio(prev_val, kf_val, seg_el, seg_dur);
            }
            prev_val = kf_val;
            prev_t = kf->key_time_ms;
        }
        return prev_val;
    }
    case IM_ANIM_REPOSITION_THEME: {
        uint32_t dur = im_anim_duration_ms(anim, keyframes);
        if (reduced_motion || dur == 0u || elapsed >= dur) return 0;
        im_fixed p = im_spline_eval_ratio(IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2, elapsed, dur);
        return im_lerp_fixed(start_val, 0, p);
    }
    case IM_ANIM_FADE_IN_THEME: {
        uint32_t dur = im_anim_duration_ms(anim, keyframes);
        if (reduced_motion || dur == 0u || elapsed >= dur) return IM_ONE;
        return im_lerp_fixed_ratio(start_val, IM_ONE, elapsed, dur);
    }
    case IM_ANIM_FADE_OUT_THEME: {
        uint32_t dur = im_anim_duration_ms(anim, keyframes);
        if (reduced_motion || dur == 0u || elapsed >= dur) return 0;
        return im_lerp_fixed_ratio(start_val, 0, elapsed, dur);
    }
    case IM_ANIM_DRAG_OVER_THEME: {
        uint32_t dur = im_anim_duration_ms(anim, keyframes);
        im_fixed mag = anim->has_to ? im_template_value_to_fixed(&anim->to_val, palette, 0) : 0;
        im_fixed to = (anim->direction == IM_DIR_LEFT || anim->direction == IM_DIR_TOP) ? -mag : mag;
        if (reduced_motion || dur == 0u || elapsed >= dur) return to;
        im_fixed p = im_spline_eval_ratio(IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2, elapsed, dur);
        return im_lerp_fixed(start_val, to, p);
    }
    case IM_ANIM_DRAG_ITEM_THEME: {
        uint32_t dur = im_anim_duration_ms(anim, keyframes);
        if (reduced_motion || dur == 0u || elapsed >= dur) return IM_DRAG_ITEM_SCALE;
        im_fixed p = im_spline_eval_ratio(IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2, elapsed, dur);
        return im_lerp_fixed(start_val, IM_DRAG_ITEM_SCALE, p);
    }
    case IM_ANIM_DROP_TARGET_THEME: {
        uint32_t dur = im_anim_duration_ms(anim, keyframes);
        if (reduced_motion || dur == 0u || elapsed >= dur) return IM_ONE;
        im_fixed p = im_spline_eval_ratio(IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2, elapsed, dur);
        return im_lerp_fixed(start_val, IM_ONE, p);
    }
    case IM_ANIM_OBJECT_KEYFRAMES:
    case IM_ANIM_COLOR_KEYFRAMES:
        break;
    }
    return start_val;
}

uint32_t im_anim_eval_color(const im_template_anim *anim, const im_template_keyframe *keyframes, const im_palette *palette,
    uint32_t start_argb, uint32_t start_ms, uint32_t now_ms, bool reduced_motion) {
    if (!anim || !keyframes || anim->kf_count == 0u) return start_argb;
    const im_template_keyframe *kfs = &keyframes[anim->kf_start];
    if (reduced_motion) {
        return im_template_value_to_argb(&kfs[anim->kf_count - 1u].value, palette);
    }
    uint32_t elapsed = im_anim_elapsed_ms(start_ms, now_ms);
    uint32_t prev_argb = start_argb;
    uint32_t prev_t = 0u;
    for (uint16_t i = 0; i < anim->kf_count; i++) {
        const im_template_keyframe *kf = &kfs[i];
        uint32_t kf_argb = im_template_value_to_argb(&kf->value, palette);
        if (elapsed < kf->key_time_ms) {
            uint32_t seg_dur = kf->key_time_ms - prev_t;
            uint32_t seg_el = elapsed - prev_t;
            if (kf->kind == IM_KF_DISCRETE || seg_dur == 0u) {
                return prev_argb;
            }
            return im_lerp_argb_ratio(prev_argb, kf_argb, seg_el, seg_dur);
        }
        prev_argb = kf_argb;
        prev_t = kf->key_time_ms;
    }
    return prev_argb;
}

bool im_anim_eval_object(const im_template_anim *anim, const im_template_keyframe *keyframes,
    uint32_t start_ms, uint32_t now_ms, bool reduced_motion, im_template_value *out_val) {
    if (!anim || !keyframes || anim->kf_count == 0u || !out_val) return false;
    const im_template_keyframe *kfs = &keyframes[anim->kf_start];
    if (reduced_motion) {
        *out_val = kfs[anim->kf_count - 1u].value;
        return true;
    }
    uint32_t elapsed = im_anim_elapsed_ms(start_ms, now_ms);
    bool found = false;
    for (uint16_t i = 0; i < anim->kf_count; i++) {
        if (elapsed >= kfs[i].key_time_ms) {
            *out_val = kfs[i].value;
            found = true;
        } else {
            break;
        }
    }
    return found;
}

void im_brush_transition_init(im_brush_transition *bt, enum im_brush_id initial_brush, uint32_t initial_argb, uint32_t duration_ms) {
    if (!bt) return;
    bt->active = false;
    bt->target_brush = initial_brush;
    bt->from_argb = initial_argb;
    bt->to_argb = initial_argb;
    bt->start_ms = 0u;
    bt->duration_ms = duration_ms;
}

void im_brush_transition_set(im_brush_transition *bt, enum im_brush_id new_brush, uint32_t new_argb, uint32_t now_ms, bool reduced_motion) {
    if (!bt) return;
    uint32_t current_argb = im_brush_transition_sample(bt, now_ms, reduced_motion);
    bt->target_brush = new_brush;
    bt->from_argb = current_argb;
    bt->to_argb = new_argb;
    bt->start_ms = now_ms;
    if (reduced_motion || bt->duration_ms == 0u || current_argb == new_argb) {
        bt->from_argb = new_argb;
        bt->active = false;
    } else {
        bt->active = true;
    }
}

uint32_t im_brush_transition_sample(const im_brush_transition *bt, uint32_t now_ms, bool reduced_motion) {
    if (!bt) return 0u;
    if (!bt->active || reduced_motion || bt->duration_ms == 0u) return bt->to_argb;
    uint32_t elapsed = im_anim_elapsed_ms(bt->start_ms, now_ms);
    if (elapsed >= bt->duration_ms) return bt->to_argb;
    return im_lerp_argb_ratio(bt->from_argb, bt->to_argb, elapsed, bt->duration_ms);
}

bool im_brush_transition_is_active(const im_brush_transition *bt, uint32_t now_ms, bool reduced_motion) {
    if (!bt || !bt->active || reduced_motion || bt->duration_ms == 0u) return false;
    return im_anim_elapsed_ms(bt->start_ms, now_ms) < bt->duration_ms;
}
