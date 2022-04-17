#include "server.h"
#include "cursor.h"
#include "view.h"
#include "wlr/util/edges.h"

#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

void process_cursor_move(struct dgde_server *server, uint32_t time) {
  /* Move the grabbed view to the new position. */
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  server->grabbed_view->x = pos.x - server->grab_x;
  server->grabbed_view->y = pos.y - server->grab_y;
}

void process_cursor_resize(struct dgde_server *server, uint32_t time) {
  /*
   * Resizing the grabbed view can be a little bit complicated, because we
   * could be resizing from any corner or edge. This not only resizes the view
   * on one or two axes, but can also move the view if you resize from the top
   * or left edges (or top-left corner).
   *
   * Note that I took some shortcuts here. In a more fleshed-out compositor,
   * you'd wait for the client to prepare a buffer at the new size, then
   * commit any movement that was prepared.
   */
  struct dgde_view *view = server->grabbed_view;
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  double border_x = pos.x - server->grab_x;
  double border_y = pos.y - server->grab_y;
  int new_left = server->grab_geobox.x;
  int new_right = server->grab_geobox.x + server->grab_geobox.width;
  int new_top = server->grab_geobox.y;
  int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

  if (server->resize_edges & WLR_EDGE_TOP) {
    new_top = border_y;
    if (new_top >= new_bottom) {
      new_top = new_bottom - 1;
    }
  } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
    new_bottom = border_y;
    if (new_bottom <= new_top) {
      new_bottom = new_top + 1;
    }
  }
  if (server->resize_edges & WLR_EDGE_LEFT) {
    new_left = border_x;
    if (new_left >= new_right) {
      new_left = new_right - 1;
    }
  } else if (server->resize_edges & WLR_EDGE_RIGHT) {
    new_right = border_x;
    if (new_right <= new_left) {
      new_right = new_left + 1;
    }
  }

  struct wlr_box geo_box;
  wlr_xdg_surface_get_geometry(view->xdg_surface, &geo_box);
  view->x = new_left - geo_box.x;
  view->y = new_top - geo_box.y;

  int new_width = new_right - new_left;
  int new_height = new_bottom - new_top;
  wlr_xdg_toplevel_set_size(view->xdg_surface, new_width, new_height);
}

void process_cursor_motion(struct dgde_server *server,
                           struct wlr_event_pointer_motion *event) {
  /* If the mode is non-passthrough, delegate to those functions. */
  uint32_t time = event->time_msec;
  if (server->cursor_mode == DgdeCursor_Move) {
    process_cursor_move(server, time);
    return;
  } else if (server->cursor_mode == DgdeCursor_Resize) {
    process_cursor_resize(server, time);
    return;
  }

  /* Otherwise, find the view under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *surface = NULL;
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  struct dgde_view *view =
      desktop_view_at(server, pos.x, pos.y, &surface, &sx, &sy);

  if (!view) {
    /* If there's no view under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * around the screen, not over any views. */
    dgde_cursor_set_image(server->cursor, "left_ptr");
  }
  if (surface) {
    bool focus_changed = seat->pointer_state.focused_surface != surface;
    /*
     * "Enter" the surface if necessary. This lets the client know that the
     * cursor has entered one of its surfaces.
     *
     * Note that this gives the surface "pointer focus", which is distinct
     * from keyboard focus. You get pointer focus by moving the pointer over
     * a window.
     */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    if (!focus_changed) {
      /* The enter event contains coordinates, so we only need to notify
       * on motion if the focus did not change. */
      wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    }
  } else {
    /* Clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it. */
    wlr_seat_pointer_clear_focus(seat);
  }
}

void process_cursor_motion_absolute(
    struct dgde_server *server,
    struct wlr_event_pointer_motion_absolute *event) {

  // create a fake relative motion
  struct wlr_event_pointer_motion ev = {
      .delta_x = 0,
      .delta_y = 0,
      .time_msec = event->time_msec,
      .device = event->device,
  };
  process_cursor_motion(server, &ev);
}

void process_cursor_button(struct dgde_server *server,
                           struct wlr_event_pointer_button *event) {

  /* Notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
                                 event->state);
  double sx, sy;
  struct wlr_surface *surface;
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  struct dgde_view *view =
      desktop_view_at(server, pos.x, pos.y, &surface, &sx, &sy);
  if (event->state == WLR_BUTTON_RELEASED) {
    /* If you released any buttons, we exit interactive move/resize mode. */
    server->cursor_mode = DgdeCursor_Passthrough;
  } else {
    /* Focus that client if the button was _pressed_ */
    focus_view(view, surface);
  }
}

void process_cursor_axis(struct dgde_server *server,
                         struct wlr_event_pointer_axis *event) {
  /* Notify the client with pointer focus of the axis event. */
  wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
                               event->orientation, event->delta,
                               event->delta_discrete, event->source);
}

void process_cursor_frame(struct dgde_server *server) {
  /* Notify the client with pointer focus of the frame event. */
  wlr_seat_pointer_notify_frame(server->seat);
}
