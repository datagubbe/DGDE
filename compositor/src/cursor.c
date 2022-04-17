#include "cursor.h"
#include "wayland-server-core.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xcursor_manager.h>

#define MAX_CURSOR_HANDLERS 16

struct dgde_cursor {
  struct wlr_cursor *inner;
  struct wlr_xcursor_manager *xcursor;

  struct wl_listener motion;
  struct wl_listener motion_absolute;
  struct wl_listener button;
  struct wl_listener axis;
  struct wl_listener frame;

  struct dgde_cursor_handler *handlers[MAX_CURSOR_HANDLERS];
  uint32_t num_handlers;
};

void on_motion(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits a _relative_
   * pointer motion event (i.e. a delta) */
  struct dgde_cursor *cursor = wl_container_of(listener, cursor, motion);
  struct wlr_event_pointer_motion *event = data;
  /* The cursor doesn't move unless we tell it to. The cursor automatically
   * handles constraining the motion to the output layout, as well as any
   * special configuration applied for the specific input device which
   * generated the event. You can pass NULL for the device if you want to move
   * the cursor around without any input. */
  wlr_cursor_move(cursor->inner, event->device, event->delta_x, event->delta_y);

  // ☎️
  for (uint32_t i = 0, e = cursor->num_handlers; i < e; ++i) {
    struct dgde_cursor_handler *handler = cursor->handlers[i];
    if (handler->motion != NULL) {
      handler->motion(handler->userdata, event);
    }
  }
}

void on_motion_absolute(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an _absolute_
   * motion event, from 0..1 on each axis. This happens, for example, when
   * wlroots is running under a Wayland window rather than KMS+DRM, and you
   * move the mouse over the window. You could enter the window from any edge,
   * so we have to warp the mouse there. There is also some hardware which
   * emits these events. */
  struct dgde_cursor *cursor =
      wl_container_of(listener, cursor, motion_absolute);
  struct wlr_event_pointer_motion_absolute *event = data;
  wlr_cursor_warp_absolute(cursor->inner, event->device, event->x, event->y);
  // ☎️
  for (uint32_t i = 0, e = cursor->num_handlers; i < e; ++i) {
    struct dgde_cursor_handler *handler = cursor->handlers[i];
    if (handler->motion_absolute != NULL) {
      handler->motion_absolute(handler->userdata, event);
    }
  }
}

void on_button(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits a button
   * event. */
  struct dgde_cursor *cursor = wl_container_of(listener, cursor, button);
  struct wlr_event_pointer_button *event = data;
  // TODO: do something here?
  // ☎️
  for (uint32_t i = 0, e = cursor->num_handlers; i < e; ++i) {
    struct dgde_cursor_handler *handler = cursor->handlers[i];
    if (handler->button != NULL) {
      handler->button(handler->userdata, event);
    }
  }
}

void on_axis(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an axis event,
   * for example when you move the scroll wheel. */
  struct dgde_cursor *cursor = wl_container_of(listener, cursor, axis);
  struct wlr_event_pointer_axis *event = data;
  // TODO: something?

  // ☎️
  for (uint32_t i = 0, e = cursor->num_handlers; i < e; ++i) {
    struct dgde_cursor_handler *handler = cursor->handlers[i];
    if (handler->axis != NULL) {
      handler->axis(handler->userdata, event);
    }
  }
}

void on_frame(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an axis event,
   * for example when you move the scroll wheel. */
  struct dgde_cursor *cursor = wl_container_of(listener, cursor, frame);
  // TODO: something?

  // ☎️
  for (uint32_t i = 0, e = cursor->num_handlers; i < e; ++i) {
    struct dgde_cursor_handler *handler = cursor->handlers[i];
    if (handler->frame != NULL) {
      handler->frame(handler->userdata);
    }
  }
}

struct dgde_cursor *
dgde_cursor_create(struct wlr_output_layout *output_layout) {
  struct dgde_cursor *cursor =
      (struct dgde_cursor *)malloc(sizeof(struct dgde_cursor));

  cursor->inner = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor->inner, output_layout);

  /* Creates an xcursor manager, another wlroots utility which loads up
   * Xcursor themes to source cursor images from and makes sure that cursor
   * images are available at all scale factors on the screen (necessary for
   * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
  cursor->xcursor = wlr_xcursor_manager_create(NULL, 24);
  wlr_xcursor_manager_load(cursor->xcursor, 1);

  cursor->num_handlers = 0;

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

  return cursor;
}

struct dgde_cursor_position dgde_cursor_position(struct dgde_cursor *cursor) {
  return (struct dgde_cursor_position){.x = cursor->inner->x,
                                       .y = cursor->inner->y};
}

void dgde_cursor_new_pointer(struct dgde_cursor *cursor,
                             struct wlr_input_device *device) {
  /* We don't do anything special with pointers. All of our pointer handling
   * is proxied through wlr_cursor. On another compositor, you might take this
   * opportunity to do libinput configuration on the device to set
   * acceleration, etc. */
  wlr_cursor_attach_input_device(cursor->inner, device);
}

void dgde_cursor_set_surface(
    struct dgde_cursor *cursor,
    struct wlr_seat_pointer_request_set_cursor_event *event) {
  wlr_cursor_set_surface(cursor->inner, event->surface, event->hotspot_x,
                         event->hotspot_y);
}

void dgde_cursor_add_handler(struct dgde_cursor *cursor,
                             struct dgde_cursor_handler *handler) {
  cursor->handlers[cursor->num_handlers++] = handler;
}

void dgde_cursor_destroy(struct dgde_cursor *cursor) {
  wlr_xcursor_manager_destroy(cursor->xcursor);
  wlr_cursor_destroy(cursor->inner);
}

void dgde_cursor_set_image(struct dgde_cursor *cursor, const char *image) {
  wlr_xcursor_manager_set_cursor_image(cursor->xcursor, image, cursor->inner);
}
