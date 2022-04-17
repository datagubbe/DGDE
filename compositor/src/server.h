#ifndef SERVER_H
#define SERVER_H

#include "cursor.h"
#include "wayland-util.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>

struct dgde_server {
  struct wl_display *wl_display;
  struct wlr_backend *backend;
  struct wlr_renderer *renderer;

  struct wlr_xdg_shell *xdg_shell;
  struct wl_listener new_xdg_surface;
  struct wl_list views;

  struct dgde_cursor *cursor;
  enum dgde_cursor_mode cursor_mode;

  struct wlr_seat *seat;
  struct wl_listener new_input;
  struct wl_listener request_cursor;
  struct wl_listener request_set_selection;
  struct wl_list keyboards;
  struct dgde_view *grabbed_view;
  double grab_x, grab_y;
  struct wlr_box grab_geobox;
  uint32_t resize_edges;

  struct wlr_output_layout *output_layout;
  struct wl_list outputs;
  struct wl_listener new_output;
};

void process_cursor_motion(struct dgde_server *server,
                           struct wlr_event_pointer_motion *event);

void process_cursor_motion_absolute(
    struct dgde_server *server,
    struct wlr_event_pointer_motion_absolute *event);

void process_cursor_button(struct dgde_server *server,
                           struct wlr_event_pointer_button *event);

void process_cursor_axis(struct dgde_server *server,
                         struct wlr_event_pointer_axis *event);

void process_cursor_frame(struct dgde_server *server);

#endif
