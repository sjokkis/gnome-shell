/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef __SHELL_DRAWING_H__
#define __SHELL_DRAWING_H__

#include <clutter/clutter.h>
#include "st.h"

G_BEGIN_DECLS

/* Note that these correspond to StSide */
typedef enum {
  SHELL_POINTER_UP,
  SHELL_POINTER_RIGHT,
  SHELL_POINTER_DOWN,
  SHELL_POINTER_LEFT
} ShellPointerDirection;

void shell_draw_box_pointer (StDrawingArea         *area,
                             ShellPointerDirection  direction);

void shell_draw_clock (StDrawingArea       *area,
	               int                  hour,
	               int                  minute);

guint shell_add_hook_paint_red_border (ClutterActor *actor);

G_END_DECLS

#endif /* __SHELL_GLOBAL_H__ */
