#ifndef CURSOR_H
#define CURSOR_H

#include "wayland-server-core.h"
#include <stdint.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>

struct cursor {
  struct wlr_cursor *inner;
  struct wlr_xcursor_manager *xcursor;
  struct wlr_seat *seat;

  struct wl_listener axis;
  struct wl_listener button;
  struct wl_listener frame;
  struct wl_listener motion;
  struct wl_listener motion_absolute;
  struct wl_listener request_cursor;

  struct {
    struct wl_signal axis;
    struct wl_signal button;
    struct wl_signal frame;
    struct wl_signal motion;
    struct wl_signal motion_absolute;
  } events;
};

struct cursor_position {
  double x;
  double y;
};

struct cursor *cursor_create(struct wlr_output_layout *output_layout,
                             struct wlr_seat *seat);

void cursor_new_pointer(struct cursor *cursor, struct wlr_input_device *device);
void cursor_set_surface(
    struct cursor *cursor,
    struct wlr_seat_pointer_request_set_cursor_event *event);

struct cursor_position cursor_position(const struct cursor *cursor);

void cursor_set_image(struct cursor *cursor, const char *image);
void cursor_destroy(struct cursor *cursor);

#endif
