#include "view.h"
#include "src/cursor.h"
#include "wayland-util.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#define MAX_HANDLERS 16

struct dgde_view {
  struct wlr_xdg_surface *xdg_surface;
  struct wlr_seat *seat;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener request_move;
  struct wl_listener request_resize;

  bool mapped;
  bool floating;
  int x, y;

  dgde_view_interaction_handler handler_functions[MAX_HANDLERS];
  void *handler_userdatas[MAX_HANDLERS];
  uint32_t num_handlers;
};

static void xdg_surface_map(struct wl_listener *listener, void *data) {
  /* Called when the surface is mapped, or ready to display on-screen. */
  struct dgde_view *view = wl_container_of(listener, view, map);
  view->mapped = true;
  dgde_view_focus(view);
}

static void xdg_surface_unmap(struct wl_listener *listener, void *data) {
  /* Called when the surface is unmapped, and should no longer be shown. */
  struct dgde_view *view = wl_container_of(listener, view, unmap);
  view->mapped = false;
}

static void xdg_surface_destroy(struct wl_listener *listener, void *data) {
  /* Called when the surface is destroyed and should never be shown again. */
  struct dgde_view *view = wl_container_of(listener, view, destroy);
  free(view);
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
                                      void *data) {
  /* This event is raised when a client would like to begin an interactive
   * move, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provied serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct dgde_view *view = wl_container_of(listener, view, request_move);
  for (uint32_t h = 0, e = view->num_handlers; h < e; ++h) {
    view->handler_functions[h](view->handler_userdatas, view, DgdeCursor_Move,
                               0);
  }
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
                                        void *data) {
  /* This event is raised when a client would like to begin an interactive
   * resize, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provied serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct wlr_xdg_toplevel_resize_event *event = data;
  struct dgde_view *view = wl_container_of(listener, view, request_resize);
  for (uint32_t h = 0, e = view->num_handlers; h < e; ++h) {
    view->handler_functions[h](view->handler_userdatas, view, DgdeCursor_Resize,
                               event->edges);
  }
}

struct dgde_view *dgde_view_create(struct wlr_xdg_surface *surface,
                                   struct wlr_seat *seat) {
  struct dgde_view *view = calloc(1, sizeof(struct dgde_view));
  view->xdg_surface = surface;
  view->seat = seat;
  view->mapped = false;
  view->floating = false; // TODO: ?

  // internal events
  view->map.notify = xdg_surface_map;
  wl_signal_add(&view->xdg_surface->events.map, &view->map);
  view->unmap.notify = xdg_surface_unmap;
  wl_signal_add(&view->xdg_surface->events.unmap, &view->unmap);
  view->destroy.notify = xdg_surface_destroy;
  wl_signal_add(&view->xdg_surface->events.destroy, &view->destroy);

  struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
  view->request_move.notify = xdg_toplevel_request_move;
  wl_signal_add(&toplevel->events.request_move, &view->request_move);
  view->request_resize.notify = xdg_toplevel_request_resize;
  wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

  return view;
}

void dgde_view_focus(const struct dgde_view *view) {
  /* Note: this function only deals with keyboard focus. */
  struct wlr_seat *seat = view->seat;
  struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
  if (prev_surface == view->xdg_surface->surface) {
    /* Don't re-focus an already focused surface. */
    return;
  }

  if (prev_surface) {
    /*
     * Deactivate the previously focused surface. This lets the client know
     * it no longer has focus and the client will repaint accordingly, e.g.
     * stop displaying a caret.
     */
    struct wlr_xdg_surface *previous =
        wlr_xdg_surface_from_wlr_surface(seat->keyboard_state.focused_surface);
    wlr_xdg_toplevel_set_activated(previous, false);
  }

  /* Activate the new surface */
  wlr_xdg_toplevel_set_activated(view->xdg_surface, true);

  /*
   * Tell the seat to have the keyboard enter this surface. wlroots will keep
   * track of this and automatically send key events to the appropriate
   * clients without additional work on your part.
   */
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  wlr_seat_keyboard_notify_enter(seat, view->xdg_surface->surface,
                                 keyboard->keycodes, keyboard->num_keycodes,
                                 &keyboard->modifiers);
}

struct wlr_surface *dgde_view_surface_at(struct dgde_view *view, double lx,
                                         double ly, double *sx, double *sy) {

  /*
   * XDG toplevels may have nested surfaces, such as popup windows for context
   * menus or tooltips. This function tests if any of those are underneath the
   * coordinates lx and ly (in output Layout Coordinates). If so, it sets the
   * surface pointer to that wlr_surface and the sx and sy coordinates to the
   * coordinates relative to that surface's top-left corner.
   */
  double view_sx = lx - view->x;
  double view_sy = ly - view->y;

  double _sx, _sy;
  struct wlr_surface *surface = wlr_xdg_surface_surface_at(
      view->xdg_surface, view_sx, view_sy, &_sx, &_sy);
  if (surface != NULL) {
    *sx = _sx;
    *sy = _sy;
    return surface;
  }

  return NULL;
}

struct dgde_view *dgde_view_at(struct wl_list *views, double lx, double ly,
                               double *sx, double *sy) {
  /* This iterates over all of our surfaces and attempts to find one under the
   * cursor. This relies on server->views being ordered from top-to-bottom. */

  return NULL;
}

void dgde_view_add_interaction_handler(struct dgde_view *view,
                                       dgde_view_interaction_handler handler,
                                       void *userdata) {
  if (view->num_handlers < MAX_HANDLERS) {
    view->handler_functions[view->num_handlers] = handler;
    view->handler_userdatas[view->num_handlers] = userdata;

    ++view->num_handlers;
  }
}

struct dgde_view_position dgde_view_position(const struct dgde_view *view) {
  return (struct dgde_view_position){.x = view->x, .y = view->y};
}

void dgde_view_set_position(struct dgde_view *view,
                            struct dgde_view_position position) {
  view->x = position.x;
  view->y = position.y;
}

struct wlr_box dgde_view_geometry(const struct dgde_view *view) {
  struct wlr_box b;
  wlr_xdg_surface_get_geometry(view->xdg_surface, &b);

  return b;
}

void dgde_view_set_size(struct dgde_view *view, struct dgde_view_size size) {
  wlr_xdg_toplevel_set_size(view->xdg_surface, size.width, size.height);
}

bool dgde_view_is_mapped(const struct dgde_view *view) { return view->mapped; }

bool dgde_view_is_focused(const struct dgde_view *view) {
  return view->xdg_surface->surface ==
         wlr_surface_get_root_surface(
             view->seat->pointer_state.focused_surface);
}

/* Used to move all of the data necessary to render a surface from the top-level
 * frame handler to the per-surface render function. */
struct render_data {
  struct wlr_output_layout *output_layout;
  struct wlr_output *output;
  struct wlr_renderer *renderer;
  const struct dgde_view *view;
  const struct timespec *when;
};

static void render_surface(struct wlr_surface *surface, int sx, int sy,
                           void *data) {
  /* This function is called for every surface that needs to be rendered. */
  struct render_data *rdata = data;
  const struct dgde_view *view = rdata->view;
  struct wlr_output *output = rdata->output;
  struct wlr_output_layout *output_layout = rdata->output_layout;

  /* We first obtain a wlr_texture, which is a GPU resource. wlroots
   * automatically handles negotiating these with the client. The underlying
   * resource could be an opaque handle passed from the client, or the client
   * could have sent a pixel buffer which we copied to the GPU, or a few other
   * means. You don't have to worry about this, wlroots takes care of it. */
  struct wlr_texture *texture = wlr_surface_get_texture(surface);
  if (texture == NULL) {
    return;
  }

  /* The view has a position in layout coordinates. If you have two displays,
   * one next to the other, both 1080p, a view on the rightmost display might
   * have layout coordinates of 2000,100. We need to translate that to
   * output-local coordinates, or (2000 - 1920). */
  double ox = 0, oy = 0;
  struct dgde_view_position view_pos = dgde_view_position(view);
  wlr_output_layout_output_coords(output_layout, output, &ox, &oy);
  ox += view_pos.x + sx, oy += view_pos.y + sy;

  /* We also have to apply the scale factor for HiDPI outputs. This is only
   * part of the puzzle, TinyWL does not fully support HiDPI. */
  struct wlr_box box = {
      .x = ox * output->scale,
      .y = oy * output->scale,
      .width = surface->current.width * output->scale,
      .height = surface->current.height * output->scale,
  };

  /*
   * Those familiar with OpenGL are also familiar with the role of matricies
   * in graphics programming. We need to prepare a matrix to render the view
   * with. wlr_matrix_project_box is a helper which takes a box with a desired
   * x, y coordinates, width and height, and an output geometry, then
   * prepares an orthographic projection and multiplies the necessary
   * transforms to produce a model-view-projection matrix.
   *
   * Naturally you can do this any way you like, for example to make a 3D
   * compositor.
   */
  float matrix[9];
  enum wl_output_transform transform =
      wlr_output_transform_invert(surface->current.transform);
  wlr_matrix_project_box(matrix, &box, transform, 0, output->transform_matrix);

  /* This takes our matrix, the texture, and an alpha, and performs the actual
   * rendering on the GPU. */
  wlr_render_texture_with_matrix(rdata->renderer, texture, matrix, 1);

  /* This lets the client know that we've displayed that frame and it can
   * prepare another one now if it likes. */
  wlr_surface_send_frame_done(surface, rdata->when);
}

void dgde_view_render(const struct dgde_view *view, struct wlr_output *output,
                      struct wlr_output_layout *output_layout,
                      const struct timespec *now) {
  if (!view->mapped) {
    /* An unmapped view should not be rendered. */
    return;
  }
  struct render_data rdata = {
      .output_layout = output_layout,
      .output = output,
      .view = view,
      .renderer = view->xdg_surface->surface->renderer,
      .when = now,
  };
  /* This calls our render_surface function for each surface among the
   * xdg_surface's toplevel and popups. */
  wlr_xdg_surface_for_each_surface(view->xdg_surface, render_surface, &rdata);
}
