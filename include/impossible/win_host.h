/* Impossible SDK: Windows host callback table initializer. Tier 1. */
#pragma once
#include <impossible/window.h>

/* Forward declaration of the portable host table defined in ui/host.h. */
struct im_host;

/* Fill a portable im_host table with the Windows Tier 0/1 memory, monotonic clock,
   and cursor callbacks bound to an optional im_window (retained across WM_SETCURSOR). */
void im_win_host_init(struct im_host *host, im_window *window);
