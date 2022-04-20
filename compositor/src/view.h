#ifndef VIEW_H
#define VIEW_H

#include "server.h"
#include "src/cursor.h"

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>

struct dgde_view;
struct wlr_xdg_surface;

struct dgde_view_position {
  int x;
  int y;
};

struct dgde_view_size {
  int width;
  int height;
};

struct dgde_view *dgde_view_create(struct wlr_xdg_surface *surface,
                                   struct wlr_seat *seat);

struct dgde_view *dgde_view_at(struct wl_list *views, double lx, double ly,
                               double *sx, double *sy);

struct wlr_surface *dgde_view_surface_at(struct dgde_view *view, double lx,
                                         double ly, double *sx, double *sy);

void dgde_view_focus(const struct dgde_view *view);
bool dgde_view_is_focused(const struct dgde_view *view);
bool dgde_view_is_mapped(const struct dgde_view *view);

typedef void (*dgde_view_interaction_handler)(void *, struct dgde_view *,
                                              enum dgde_cursor_mode, uint32_t);
void dgde_view_add_interaction_handler(struct dgde_view *view,
                                       dgde_view_interaction_handler handler,
                                       void *userdata);

struct dgde_view_position dgde_view_position(const struct dgde_view *view);
void dgde_view_set_position(struct dgde_view *view,
                            struct dgde_view_position position);

struct wlr_box dgde_view_geometry(const struct dgde_view *view);
void dgde_view_set_size(struct dgde_view *view, struct dgde_view_size size);

void dgde_view_render(const struct dgde_view *view, struct wlr_output *output,
                      struct wlr_output_layout *output_layout,
                      const struct timespec *now);

#endif
