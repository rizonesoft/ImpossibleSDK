/* Dumps fixed-point spline and compiled ToggleSwitch track samples for exact double-precision comparison. */
#include <impossible/base.h>
#include "animate.h"
#include "states.h"

static void put_u32(uint32_t v) {
    char buf[16];
    int n = 0;
    if (v == 0u) {
        buf[n++] = '0';
    } else {
        while (v > 0u) {
            buf[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    char out[16];
    for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
    out[n] = '\0';
    im_print(out);
}

static void put_i32(int32_t v) {
    if (v < 0) {
        im_print("-");
        put_u32((uint32_t)(-(int64_t)v));
    } else {
        put_u32((uint32_t)v);
    }
}

int im_main(void) {
    im_palette pal = {IM_DARK, {0}};
    im_state_machine ts;
    im_states_init(&ts, IM_TEMPLATE_TOGGLE_SWITCH, &pal);
    im_states_go_to(&ts, "Normal", 0u);
    /* Settle Normal at 12 * IM_ONE, then press -> 17 * IM_ONE over 83 ms with spline {0,0,0,1}. */
    im_states_go_to(&ts, "Pressed", 1000u);

    const uint32_t ts83[] = {0, 1, 2, 5, 10, 17, 21, 33, 41, 55, 62, 75, 82, 83};
    for (size_t i = 0; i < sizeof(ts83) / sizeof(ts83[0]); i++) {
        im_fixed s = im_spline_eval_ratio(0, 0, 0, 65536, ts83[i], 83u);
        im_fixed asc = im_states_sample_fixed(&ts, "SwitchKnobOn", "Width", 1000u + ts83[i], 0);
        im_print("ASC ");
        put_u32(ts83[i]);
        im_print(" ");
        put_i32(s);
        im_print(" ");
        put_i32(asc);
        im_print("\n");
    }

    /* Return Pressed (17 * IM_ONE) -> Normal (12 * IM_ONE) over 83 ms with spline {0,0,0,1}. */
    im_states_go_to(&ts, "Normal", 2000u);
    for (size_t i = 0; i < sizeof(ts83) / sizeof(ts83[0]); i++) {
        im_fixed desc = im_states_sample_fixed(&ts, "SwitchKnobOn", "Width", 2000u + ts83[i], 0);
        im_print("DESC ");
        put_u32(ts83[i]);
        im_print(" ");
        put_i32(desc);
        im_print("\n");
    }

    /* PVL 367 ms Reposition spline (0.1, 0.9, 0.2, 1.0). */
    const uint32_t ts367[] = {0, 1, 16, 33, 66, 100, 150, 200, 250, 300, 350, 367};
    for (size_t i = 0; i < sizeof(ts367) / sizeof(ts367[0]); i++) {
        im_fixed s = im_spline_eval_ratio(IM_PVL_SPLINE_X1, IM_PVL_SPLINE_Y1, IM_PVL_SPLINE_X2, IM_PVL_SPLINE_Y2, ts367[i], 367u);
        im_print("PVL ");
        put_u32(ts367[i]);
        im_print(" ");
        put_i32(s);
        im_print("\n");
    }
    return 0;
}
