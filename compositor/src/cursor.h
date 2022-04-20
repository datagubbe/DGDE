#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>

struct dgde_cursor;

enum dgde_cursor_mode {
  DgdeCursor_Passthrough,
  DgdeCursor_Move,
  DgdeCursor_Resize,
};

struct dgde_cursor_position {
  double x;
  double y;
};

typedef void (*dgde_cursor_motion_cb)(void *,
                                      struct wlr_event_pointer_motion *);
typedef void (*dgde_cursor_motion_absolute_cb)(
    void *, struct wlr_event_pointer_motion_absolute *);
typedef void (*dgde_cursor_button_cb)(void *,
                                      struct wlr_event_pointer_button *);
typedef void (*dgde_cursor_axis_cb)(void *, struct wlr_event_pointer_axis *);
typedef void (*dgde_cursor_frame_cb)(void *);

struct dgde_cursor_handler {
  void *userdata;
  dgde_cursor_motion_cb motion;
  dgde_cursor_motion_absolute_cb motion_absolute;
  dgde_cursor_button_cb button;
  dgde_cursor_axis_cb axis;
  dgde_cursor_frame_cb frame;
};

struct dgde_cursor *dgde_cursor_create(struct wlr_output_layout *output_layout,
                                       struct wlr_seat *seat);

void dgde_cursor_new_pointer(struct dgde_cursor *cursor,
                             struct wlr_input_device *device);
void dgde_cursor_set_surface(
    struct dgde_cursor *cursor,
    struct wlr_seat_pointer_request_set_cursor_event *event);

struct dgde_cursor_position
dgde_cursor_position(const struct dgde_cursor *cursor);

enum dgde_cursor_mode dgde_cursor_mode(const struct dgde_cursor *cursor);
enum dgde_cursor_mode dgde_cursor_set_mode(struct dgde_cursor *cursor,
                                           enum dgde_cursor_mode mode);
enum dgde_cursor_mode dgde_cursor_reset_mode(struct dgde_cursor *cursor);

void dgde_cursor_add_handler(struct dgde_cursor *cursor,
                             const struct dgde_cursor_handler *handler);

void dgde_cursor_set_image(struct dgde_cursor *cursor, const char *image);
void dgde_cursor_destroy(struct dgde_cursor *cursor);

#endif
