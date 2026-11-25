/* Impossible UI: 2D affine transforms in Q30 signed 64-bit fixed point. Portable. */
#include "transform.h"

/* Exact Q30 sine table for integer degrees 0..90: round(sin(deg * pi / 180) * 2^30). */
static const uint32_t SIN_TABLE_Q30[91] = {
    0u, 18739379u, 37473049u, 56195305u, 74900443u, 93582766u,
    112236583u, 130856211u, 149435979u, 167970228u, 186453311u, 204879599u,
    223243478u, 241539355u, 259761657u, 277904834u, 295963357u, 313931728u,
    331804471u, 349576144u, 367241333u, 384794656u, 402230767u, 419544355u,
    436730145u, 453782903u, 470697435u, 487468587u, 504091252u, 520560366u,
    536870912u, 553017922u, 568996477u, 584801711u, 600428808u, 615873009u,
    631129609u, 646193961u, 661061475u, 675727625u, 690187940u, 704438018u,
    718473518u, 732290163u, 745883746u, 759250125u, 772385229u, 785285058u,
    797945680u, 810363241u, 822533958u, 834454122u, 846120104u, 857528349u,
    868675383u, 879557810u, 890172315u, 900515665u, 910584710u, 920376381u,
    929887697u, 939115760u, 948057759u, 956710970u, 965072759u, 973140576u,
    980911966u, 988384560u, 995556083u, 1002424350u, 1008987269u, 1015242840u,
    1021189159u, 1026824413u, 1032146887u, 1037154959u, 1041847103u, 1046221891u,
    1050277989u, 1054014162u, 1057429273u, 1060522280u, 1063292242u, 1065738315u,
    1067859754u, 1069655912u, 1071126243u, 1072270298u, 1073087729u, 1073578288u,
    1073741824u
};

/* pi / 180 in Q40 fixed point: round((pi / 180) * 2^40) = 19190098069. */
#define IM_DEG_TO_RAD_Q40 19190098069LL

static bool coeff_in_range(__int128 v) {
    return v >= -((__int128)IM_TR_MAX_COEFF) && v <= ((__int128)IM_TR_MAX_COEFF);
}

static __int128 round_shift_right_128(__int128 v, int shift) {
    __int128 half = (__int128)1 << (shift - 1);
    if (v >= 0) return (v + half) >> shift;
    return -(((-v) + half) >> shift);
}

static unsigned __int128 udiv128(unsigned __int128 num, unsigned __int128 den) {
    if (den == 0 || num < den) return 0;
    if (den <= UINT64_MAX && num <= UINT64_MAX) return (uint64_t)num / (uint64_t)den;
    unsigned __int128 q = 0, r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1u);
        if (r >= den) {
            r -= den;
            q |= ((unsigned __int128)1 << i);
        }
    }
    return q;
}

int64_t im_sdiv128_i64(__int128 num, int64_t den) {
    if (den == 0) return 0;
    bool neg = false;
    unsigned __int128 un, ud;
    if (num < 0) { un = (unsigned __int128)(-(num + 1)) + 1u; neg = !neg; } else { un = (unsigned __int128)num; }
    if (den < 0) { ud = (unsigned __int128)(-(den + 1)) + 1u; neg = !neg; } else { ud = (unsigned __int128)den; }
    unsigned __int128 q = udiv128(un, ud);
    return neg ? -(int64_t)q : (int64_t)q;
}

static __int128 round_div_128(__int128 num, __int128 den) {
    bool neg = false;
    unsigned __int128 un, ud;
    if (num < 0) { un = (unsigned __int128)(-(num + 1)) + 1u; neg = !neg; } else { un = (unsigned __int128)num; }
    if (den < 0) { ud = (unsigned __int128)(-(den + 1)) + 1u; neg = !neg; } else { ud = (unsigned __int128)den; }
    unsigned __int128 q = udiv128(un + (ud >> 1), ud);
    return neg ? -(__int128)q : (__int128)q;
}

static int64_t sin_first_quadrant(int64_t deg_fx) {
    int64_t k = (deg_fx + (IM_ONE / 2)) >> 16;
    if (k < 0) k = 0;
    if (k > 90) k = 90;
    int64_t rem_fx = deg_fx - (k * (int64_t)IM_ONE);
    int64_t s0 = (int64_t)SIN_TABLE_Q30[k];
    int64_t c0 = (int64_t)SIN_TABLE_Q30[90 - k];
    if (rem_fx == 0) return s0;

    /* Keep 4 extra guard bits (Q34) on d so intermediate rounding never stacks above 1 Q30 LSB. */
    int64_t d_q34 = (int64_t)round_shift_right_128((__int128)rem_fx * IM_DEG_TO_RAD_Q40, 22);
    int64_t d2_q34 = (int64_t)round_shift_right_128((__int128)d_q34 * d_q34, 34);
    int64_t d3_q34 = (int64_t)round_shift_right_128((__int128)d2_q34 * d_q34, 34);
    int64_t sin_d_q34 = d_q34 - (d3_q34 / 6);
    int64_t cos_d_q34 = ((int64_t)1 << 34) - ((d2_q34 + 1) >> 1);

    __int128 res = round_shift_right_128((__int128)s0 * cos_d_q34 + (__int128)c0 * sin_d_q34, 34);
    if (res > IM_TR_ONE) res = IM_TR_ONE;
    if (res < -IM_TR_ONE) res = -IM_TR_ONE;
    return (int64_t)res;
}

int64_t im_sin_deg(im_fixed angle_deg) {
    const int64_t full = 360LL * IM_ONE;
    const int64_t half = 180LL * IM_ONE;
    const int64_t qtr = 90LL * IM_ONE;
    int64_t a = (int64_t)angle_deg % full;
    if (a < 0) a += full;
    bool neg = false;
    if (a >= half) {
        a -= half;
        neg = true;
    }
    if (a > qtr) {
        a = half - a;
    }
    int64_t s = sin_first_quadrant(a);
    return neg ? -s : s;
}

int64_t im_cos_deg(im_fixed angle_deg) {
    const int64_t full = 360LL * IM_ONE;
    const int64_t qtr = 90LL * IM_ONE;
    int64_t a = ((int64_t)angle_deg + qtr) % full;
    if (a < 0) a += full;
    return im_sin_deg((im_fixed)a);
}

bool im_tan_deg(im_fixed angle_deg, int64_t *out_q30) {
    if (!out_q30) return false;
    int64_t s = im_sin_deg(angle_deg);
    int64_t c = im_cos_deg(angle_deg);
    if (c == 0) return false;
    __int128 q = round_div_128((__int128)s * IM_TR_ONE, (__int128)c);
    if (!coeff_in_range(q)) return false;
    *out_q30 = (int64_t)q;
    return true;
}

im_transform im_transform_identity(void) {
    return (im_transform){IM_TR_ONE, 0, 0, IM_TR_ONE, 0, 0};
}

im_transform im_transform_translate(im_fixed tx, im_fixed ty) {
    return (im_transform){IM_TR_ONE, 0, 0, IM_TR_ONE, im_fixed_to_q30(tx), im_fixed_to_q30(ty)};
}

im_transform im_transform_scale(im_fixed sx, im_fixed sy) {
    return (im_transform){im_fixed_to_q30(sx), 0, 0, im_fixed_to_q30(sy), 0, 0};
}

im_transform im_transform_scale_at(im_fixed sx, im_fixed sy, im_fixed cx, im_fixed cy) {
    int64_t qs_x = im_fixed_to_q30(sx);
    int64_t qs_y = im_fixed_to_q30(sy);
    int64_t qc_x = im_fixed_to_q30(cx);
    int64_t qc_y = im_fixed_to_q30(cy);
    int64_t dx = (int64_t)round_shift_right_128((__int128)qc_x * (IM_TR_ONE - qs_x), IM_TR_SHIFT);
    int64_t dy = (int64_t)round_shift_right_128((__int128)qc_y * (IM_TR_ONE - qs_y), IM_TR_SHIFT);
    return (im_transform){qs_x, 0, 0, qs_y, dx, dy};
}

im_transform im_transform_rotate(im_fixed angle_deg) {
    int64_t s = im_sin_deg(angle_deg);
    int64_t c = im_cos_deg(angle_deg);
    /* Row-vector clockwise rotation in screen coords (+X right, +Y down):
       x' = x*cos - y*sin, y' = x*sin + y*cos => m11=c, m12=s, m21=-s, m22=c. */
    return (im_transform){c, s, -s, c, 0, 0};
}

im_transform im_transform_rotate_at(im_fixed angle_deg, im_fixed cx, im_fixed cy) {
    int64_t s = im_sin_deg(angle_deg);
    int64_t c = im_cos_deg(angle_deg);
    int64_t qc_x = im_fixed_to_q30(cx);
    int64_t qc_y = im_fixed_to_q30(cy);
    int64_t dx = qc_x - (int64_t)round_shift_right_128((__int128)qc_x * c - (__int128)qc_y * s, IM_TR_SHIFT);
    int64_t dy = qc_y - (int64_t)round_shift_right_128((__int128)qc_x * s + (__int128)qc_y * c, IM_TR_SHIFT);
    return (im_transform){c, s, -s, c, dx, dy};
}

bool im_transform_skew(im_fixed angle_x_deg, im_fixed angle_y_deg, im_transform *out) {
    if (!out) return false;
    int64_t tx = 0, ty = 0;
    if (!im_tan_deg(angle_x_deg, &tx)) return false;
    if (!im_tan_deg(angle_y_deg, &ty)) return false;
    *out = (im_transform){IM_TR_ONE, ty, tx, IM_TR_ONE, 0, 0};
    return true;
}

bool im_transform_multiply(const im_transform *a, const im_transform *b, im_transform *out) {
    if (!a || !b || !out) return false;
    __int128 m11 = round_shift_right_128((__int128)a->m11 * b->m11 + (__int128)a->m12 * b->m21, IM_TR_SHIFT);
    __int128 m12 = round_shift_right_128((__int128)a->m11 * b->m12 + (__int128)a->m12 * b->m22, IM_TR_SHIFT);
    __int128 m21 = round_shift_right_128((__int128)a->m21 * b->m11 + (__int128)a->m22 * b->m21, IM_TR_SHIFT);
    __int128 m22 = round_shift_right_128((__int128)a->m21 * b->m12 + (__int128)a->m22 * b->m22, IM_TR_SHIFT);
    __int128 dx = round_shift_right_128((__int128)a->dx * b->m11 + (__int128)a->dy * b->m21, IM_TR_SHIFT) + b->dx;
    __int128 dy = round_shift_right_128((__int128)a->dx * b->m12 + (__int128)a->dy * b->m22, IM_TR_SHIFT) + b->dy;
    if (!coeff_in_range(m11) || !coeff_in_range(m12) ||
        !coeff_in_range(m21) || !coeff_in_range(m22) ||
        !coeff_in_range(dx) || !coeff_in_range(dy)) {
        return false;
    }
    out->m11 = (int64_t)m11;
    out->m12 = (int64_t)m12;
    out->m21 = (int64_t)m21;
    out->m22 = (int64_t)m22;
    out->dx = (int64_t)dx;
    out->dy = (int64_t)dy;
    return true;
}

bool im_transform_composite(im_fixed center_x, im_fixed center_y,
                            im_fixed scale_x, im_fixed scale_y,
                            im_fixed skew_x_deg, im_fixed skew_y_deg,
                            im_fixed rotation_deg,
                            im_fixed translate_x, im_fixed translate_y,
                            im_transform *out) {
    if (!out) return false;
    im_transform skew;
    if (!im_transform_skew(skew_x_deg, skew_y_deg, &skew)) return false;

    int64_t qcx = im_fixed_to_q30(center_x);
    int64_t qcy = im_fixed_to_q30(center_y);
    im_transform cur = {IM_TR_ONE, 0, 0, IM_TR_ONE, -qcx, -qcy};
    im_transform sc = im_transform_scale(scale_x, scale_y);
    im_transform rot = im_transform_rotate(rotation_deg);
    im_transform back = {IM_TR_ONE, 0, 0, IM_TR_ONE, qcx, qcy};
    im_transform tr = im_transform_translate(translate_x, translate_y);

    im_transform tmp;
    if (!im_transform_multiply(&cur, &sc, &tmp)) return false;
    if (!im_transform_multiply(&tmp, &skew, &cur)) return false;
    if (!im_transform_multiply(&cur, &rot, &tmp)) return false;
    if (!im_transform_multiply(&tmp, &back, &cur)) return false;
    if (!im_transform_multiply(&cur, &tr, &tmp)) return false;
    *out = tmp;
    return true;
}

bool im_transform_invert(const im_transform *t, im_transform *out) {
    if (!t || !out) return false;
    if (!coeff_in_range(t->m11) || !coeff_in_range(t->m12) ||
        !coeff_in_range(t->m21) || !coeff_in_range(t->m22) ||
        !coeff_in_range(t->dx) || !coeff_in_range(t->dy)) {
        return false;
    }
    __int128 det = (__int128)t->m11 * t->m22 - (__int128)t->m12 * t->m21;
    if (det == 0) return false;

    /* m_inv = adj(m) * 2^60 / det (result in Q30).
       Each |m_ij| <= 2^58 so |m_ij| * 2^60 <= 2^118 < 2^127: never overflows __int128. */
    const __int128 q60 = (__int128)1 << (2 * IM_TR_SHIFT);
    __int128 im11 = round_div_128((__int128)t->m22 * q60, det);
    __int128 im12 = round_div_128(-((__int128)t->m12 * q60), det);
    __int128 im21 = round_div_128(-((__int128)t->m21 * q60), det);
    __int128 im22 = round_div_128((__int128)t->m11 * q60, det);
    if (!coeff_in_range(im11) || !coeff_in_range(im12) ||
        !coeff_in_range(im21) || !coeff_in_range(im22)) {
        return false;
    }
    __int128 idx = -round_shift_right_128((__int128)t->dx * im11 + (__int128)t->dy * im21, IM_TR_SHIFT);
    __int128 idy = -round_shift_right_128((__int128)t->dx * im12 + (__int128)t->dy * im22, IM_TR_SHIFT);
    if (!coeff_in_range(idx) || !coeff_in_range(idy)) return false;

    out->m11 = (int64_t)im11;
    out->m12 = (int64_t)im12;
    out->m21 = (int64_t)im21;
    out->m22 = (int64_t)im22;
    out->dx = (int64_t)idx;
    out->dy = (int64_t)idy;
    return true;
}

bool im_transform_point_q30(const im_transform *t, int64_t qx, int64_t qy, int64_t *out_qx, int64_t *out_qy) {
    if (!t || !out_qx || !out_qy) return false;
    __int128 rx = round_shift_right_128((__int128)qx * t->m11 + (__int128)qy * t->m21, IM_TR_SHIFT) + t->dx;
    __int128 ry = round_shift_right_128((__int128)qx * t->m12 + (__int128)qy * t->m22, IM_TR_SHIFT) + t->dy;
    if (!coeff_in_range(rx) || !coeff_in_range(ry)) return false;
    *out_qx = (int64_t)rx;
    *out_qy = (int64_t)ry;
    return true;
}

bool im_transform_point(const im_transform *t, im_fixed x, im_fixed y, im_fixed *out_x, im_fixed *out_y) {
    if (!t || !out_x || !out_y) return false;
    int64_t qx = 0, qy = 0;
    if (!im_transform_point_q30(t, im_fixed_to_q30(x), im_fixed_to_q30(y), &qx, &qy)) return false;
    __int128 fx = round_shift_right_128((__int128)qx, IM_TR_SHIFT - 16);
    __int128 fy = round_shift_right_128((__int128)qy, IM_TR_SHIFT - 16);
    if (fx < INT32_MIN || fx > INT32_MAX || fy < INT32_MIN || fy > INT32_MAX) return false;
    *out_x = (im_fixed)fx;
    *out_y = (im_fixed)fy;
    return true;
}

static bool q30_to_fixed_floor(int64_t q, im_fixed *out) {
    const int64_t shift = IM_TR_SHIFT - 16;
    const int64_t step = 1LL << shift;
    int64_t f = (q >= 0) ? (q >> shift) : -(((-q) + step - 1) >> shift);
    if (f < INT32_MIN || f > INT32_MAX) return false;
    *out = (im_fixed)f;
    return true;
}

static bool q30_to_fixed_ceil(int64_t q, im_fixed *out) {
    const int64_t shift = IM_TR_SHIFT - 16;
    const int64_t step = 1LL << shift;
    int64_t c = (q >= 0) ? ((q + step - 1) >> shift) : -((-q) >> shift);
    if (c < INT32_MIN || c > INT32_MAX) return false;
    *out = (im_fixed)c;
    return true;
}

bool im_transform_rect_bounds(const im_transform *t, im_rect rect, im_rect *out) {
    if (!t || !out) return false;
    if (rect.left >= rect.right || rect.top >= rect.bottom) return false;
    int64_t cx[4] = {
        im_fixed_to_q30(rect.left), im_fixed_to_q30(rect.right),
        im_fixed_to_q30(rect.right), im_fixed_to_q30(rect.left)
    };
    int64_t cy[4] = {
        im_fixed_to_q30(rect.top), im_fixed_to_q30(rect.top),
        im_fixed_to_q30(rect.bottom), im_fixed_to_q30(rect.bottom)
    };
    int64_t min_x = INT64_MAX, min_y = INT64_MAX;
    int64_t max_x = INT64_MIN, max_y = INT64_MIN;
    for (int i = 0; i < 4; i++) {
        int64_t rx = 0, ry = 0;
        if (!im_transform_point_q30(t, cx[i], cy[i], &rx, &ry)) return false;
        if (rx < min_x) min_x = rx;
        if (rx > max_x) max_x = rx;
        if (ry < min_y) min_y = ry;
        if (ry > max_y) max_y = ry;
    }
    im_rect res;
    if (!q30_to_fixed_floor(min_x, &res.left) ||
        !q30_to_fixed_floor(min_y, &res.top) ||
        !q30_to_fixed_ceil(max_x, &res.right) ||
        !q30_to_fixed_ceil(max_y, &res.bottom)) {
        return false;
    }
    *out = res;
    return true;
}
