#ifndef VIEW_H
#define VIEW_H

#include "server.h"
#include "src/cursor.h"

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>

struct view {
  struct wlr_xdg_surface *xdg_surface;
  struct wlr_seat *seat;

  struct {
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
  } event_handlers;

  struct {
    struct wl_signal destroy;
  } events;

  bool mapped;
  bool floating;
  int x, y;
};

struct wlr_xdg_surface;

struct view *view_create(struct wlr_xdg_surface *surface,
                         struct wlr_seat *seat);

void view_destroy(struct view *view);

struct view *view_at(struct wl_list *views, double lx, double ly, double *sx,
                     double *sy);

struct wlr_surface *view_surface_at(struct view *view, double lx, double ly,
                                    double *sx, double *sy);

void view_focus(const struct view *view);
bool view_is_focused(const struct view *view);
bool view_is_mapped(const struct view *view);

struct wlr_box view_geometry(const struct view *view);
void view_set_geometry(struct view *view, const struct wlr_box *geom);

void view_render(const struct view *view, struct wlr_output *output,
                 struct wlr_output_layout *output_layout,
                 const struct timespec *now);

#endif
