#include <impossible/base.h>

int im_main(void) {
    return im_print("Hello from Impossible SDK: caf\xC3\xA9 \xF0\x9F\x9A\x80\n") ? 0 : 1;
}
