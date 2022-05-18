#ifndef SERVER_H
#define SERVER_H

#include "cursor.h"
#include "wayland-util.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>

struct server {
  struct wl_display *wl_display;
  struct wlr_backend *backend;
  struct wlr_renderer *renderer;

  struct wlr_xdg_shell *xdg_shell;
  struct wl_listener new_xdg_surface;

  struct cursor *cursor;
  struct {
    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;
  } cursor_events;

  struct wlr_seat *seat;
  struct wl_listener new_input;
  struct wl_listener request_set_selection;
  struct wl_list keyboards;

  struct wlr_output_layout *output_layout;
  struct wl_list outputs;
  struct wl_listener new_output;
};

struct output {
  struct wl_list link;

  struct server *server;
  struct wlr_output *wlr_output;

  struct wl_listener frame;
  struct wl_listener mode;

  struct workspace *workspaces[16];
  uint32_t num_workspaces;
  uint32_t active_workspace;
};

struct server *server_create(const char *seat_name);
void server_destroy(struct server *server);

const char *server_attach_socket(struct server *server);
void server_run(struct server *server);

#endif
