#ifndef VIEW_H
#define VIEW_H

#include "server.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>

struct dgde_view {
  struct wl_list link;
  struct dgde_server *server;
  struct wlr_xdg_surface *xdg_surface;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener request_move;
  struct wl_listener request_resize;
  bool mapped;
  int x, y;
};

struct dgde_view *desktop_view_at(struct dgde_server *server, double lx,
                                  double ly, struct wlr_surface **surface,
                                  double *sx, double *sy);

void focus_view(struct dgde_view *view, struct wlr_surface *surface);

void begin_interactive(struct dgde_view *view, enum dgde_cursor_mode mode,
                       uint32_t edges);

#endif
