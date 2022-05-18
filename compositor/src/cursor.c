#include "cursor.h"
#include "wayland-server-core.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xcursor_manager.h>

static void on_motion(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits a _relative_
   * pointer motion event (i.e. a delta) */
  struct cursor *cursor = wl_container_of(listener, cursor, motion);
  struct wlr_event_pointer_motion *event = data;
  /* The cursor doesn't move unless we tell it to. The cursor automatically
   * handles constraining the motion to the output layout, as well as any
   * special configuration applied for the specific input device which
   * generated the event. You can pass NULL for the device if you want to move
   * the cursor around without any input. */
  wlr_cursor_move(cursor->inner, event->device, event->delta_x, event->delta_y);

  // ☎️
  wl_signal_emit(&cursor->events.motion, data);
}

static void on_motion_absolute(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an _absolute_
   * motion event, from 0..1 on each axis. This happens, for example, when
   * wlroots is running under a Wayland window rather than KMS+DRM, and you
   * move the mouse over the window. You could enter the window from any edge,
   * so we have to warp the mouse there. There is also some hardware which
   * emits these events. */
  struct cursor *cursor = wl_container_of(listener, cursor, motion_absolute);
  struct wlr_event_pointer_motion_absolute *event = data;
  wlr_cursor_warp_absolute(cursor->inner, event->device, event->x, event->y);

  wl_signal_emit(&cursor->events.motion_absolute, data);
}

static void on_button(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits a button
   * event. */
  struct cursor *cursor = wl_container_of(listener, cursor, button);

  // ☎️
  wl_signal_emit(&cursor->events.button, data);
}

static void on_axis(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an axis event,
   * for example when you move the scroll wheel. */
  struct cursor *cursor = wl_container_of(listener, cursor, axis);
  // TODO: something?

  // ☎️
  wl_signal_emit(&cursor->events.axis, data);
}

static void on_frame(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an axis event,
   * for example when you move the scroll wheel. */
  struct cursor *cursor = wl_container_of(listener, cursor, frame);
  // TODO: something?

  // ☎️
  wl_signal_emit(&cursor->events.frame, data);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
  struct cursor *cursor = wl_container_of(listener, cursor, request_cursor);
  /* This event is rasied by the seat when a client provides a cursor image */
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat_client *focused_client =
      cursor->seat->pointer_state.focused_client;
  /* This can be sent by any client, so we check to make sure this one is
   * actually has pointer focus first. */
  if (focused_client == event->seat_client) {
    /* Once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. It will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs. */
    cursor_set_surface(cursor, event);
  }
}

struct cursor *cursor_create(struct wlr_output_layout *output_layout,
                             struct wlr_seat *seat) {
  struct cursor *cursor = calloc(1, sizeof(struct cursor));

  cursor->inner = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor->inner, output_layout);

  /* Creates an xcursor manager, another wlroots utility which loads up
   * Xcursor themes to source cursor images from and makes sure that cursor
   * images are available at all scale factors on the screen (necessary for
   * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
  cursor->xcursor = wlr_xcursor_manager_create(NULL, 24);
  wlr_xcursor_manager_load(cursor->xcursor, 1);

  cursor->seat = seat;

  // set up all internal events
  cursor->motion.notify = on_motion;
  wl_signal_add(&cursor->inner->events.motion, &cursor->motion);

  cursor->motion_absolute.notify = on_motion_absolute;
  wl_signal_add(&cursor->inner->events.motion_absolute,
                &cursor->motion_absolute);

  cursor->button.notify = on_button;
  wl_signal_add(&cursor->inner->events.button, &cursor->button);

  cursor->axis.notify = on_axis;
  wl_signal_add(&cursor->inner->events.axis, &cursor->axis);

  cursor->frame.notify = on_frame;
  wl_signal_add(&cursor->inner->events.frame, &cursor->frame);

  cursor->request_cursor.notify = seat_request_cursor;
  wl_signal_add(&seat->events.request_set_cursor, &cursor->request_cursor);

  wl_signal_init(&cursor->events.axis);
  wl_signal_init(&cursor->events.button);
  wl_signal_init(&cursor->events.frame);
  wl_signal_init(&cursor->events.motion);
  wl_signal_init(&cursor->events.motion_absolute);

  return cursor;
}

struct cursor_position cursor_position(const struct cursor *cursor) {
  return (struct cursor_position){.x = cursor->inner->x, .y = cursor->inner->y};
}

void cursor_new_pointer(struct cursor *cursor,
                        struct wlr_input_device *device) {
  /* We don't do anything special with pointers. All of our pointer handling
   * is proxied through wlr_cursor. On another compositor, you might take this
   * opportunity to do libinput configuration on the device to set
   * acceleration, etc. */
  wlr_cursor_attach_input_device(cursor->inner, device);
}

void cursor_set_surface(
    struct cursor *cursor,
    struct wlr_seat_pointer_request_set_cursor_event *event) {
  wlr_cursor_set_surface(cursor->inner, event->surface, event->hotspot_x,
                         event->hotspot_y);
}

void cursor_destroy(struct cursor *cursor) {
  wlr_xcursor_manager_destroy(cursor->xcursor);
  wlr_cursor_destroy(cursor->inner);
}

void cursor_set_image(struct cursor *cursor, const char *image) {
  wlr_xcursor_manager_set_cursor_image(cursor->xcursor, image, cursor->inner);
}
