/* Impossible UI: the button, as WinUI's DefaultButtonStyle and AccentButtonStyle draw it. */
#pragma once
#include "draw.h"

/* The button's style and the state it is drawn in. */
typedef enum im_button_style { IM_BUTTON, IM_BUTTON_ACCENT, IM_BUTTON_SUBTLE } im_button_style;
typedef enum im_button_state { IM_REST, IM_HOVER, IM_PRESSED, IM_DISABLED } im_button_state;

/* Draws the button's ground and border, without text. */
void im_draw_button(im_surface *surface, im_rect bounds, im_button_style style, im_button_state state);
