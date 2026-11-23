/* Draws every case of the reference program and writes the pixels to standard
   output, one 160 x 64 frame after another. The accent colours come in as
   arguments would, but fixed here: tests pass them through the file below. */
#include <impossible/base.h>
#include <windows.h>
#include "button.h"
#include "accents.h"

enum { W = 160, H = 64 };
static uint32_t pixels[W * H];

int im_main(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    static const im_fixed pad = IM_ONE;
    for (int theme = 0; theme < 2; theme++)
        for (int style = 0; style < 2; style++)
            for (int state = 0; state < 4; state++) {
                im_surface surface = {pixels, W, H, W, {theme ? IM_LIGHT : IM_DARK, ACCENTS}, IM_ONE};
                for (int i = 0; i < W * H; i++) pixels[i] = theme ? 0xF3F3F3 : 0x202020;
                im_draw_button(&surface, (im_rect){20 * pad, 16 * pad, 140 * pad, 48 * pad}, (im_button_style)style, (im_button_state)state);
                DWORD done;
                if (!WriteFile(out, pixels, sizeof pixels, &done, NULL) || done != sizeof pixels) return 1;
            }
    return 0;
}
