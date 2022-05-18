#include "view.h"
#include "src/cursor.h"
#include "wayland-server-core.h"
#include "wayland-util.h"

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

static void xdg_surface_map(struct wl_listener *listener, void *data) {
  /* Called when the surface is mapped, or ready to display on-screen. */
  struct view *view = wl_container_of(listener, view, event_handlers.map);
  view->mapped = true;
  view_focus(view);
}

static void xdg_surface_unmap(struct wl_listener *listener, void *data) {
  /* Called when the surface is unmapped, and should no longer be shown. */
  struct view *view = wl_container_of(listener, view, event_handlers.unmap);
  view->mapped = false;
}

static void xdg_surface_destroy(struct wl_listener *listener, void *data) {
  /* Called when the surface is destroyed and should never be shown again. */
  struct view *view = wl_container_of(listener, view, event_handlers.destroy);

  wl_signal_emit(&view->events.destroy, view);

  view_destroy(view);
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
                                      void *data) {
  /* This event is raised when a client would like to begin an interactive
   * move, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provied serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */

  // do nothing for now
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
                                        void *data) {
  /* This event is raised when a client would like to begin an interactive
   * resize, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provied serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  // do nothing for now
}

struct view *view_create(struct wlr_xdg_surface *surface,
                         struct wlr_seat *seat) {
  struct view *view = calloc(1, sizeof(struct view));
  view->xdg_surface = surface;
  view->seat = seat;
  view->mapped = false;
  view->floating = false; // TODO: ?

  // internal events
  view->event_handlers.map.notify = xdg_surface_map;
  wl_signal_add(&view->xdg_surface->events.map, &view->event_handlers.map);
  view->event_handlers.unmap.notify = xdg_surface_unmap;
  wl_signal_add(&view->xdg_surface->events.unmap, &view->event_handlers.unmap);
  view->event_handlers.destroy.notify = xdg_surface_destroy;
  wl_signal_add(&view->xdg_surface->events.destroy,
                &view->event_handlers.destroy);

  struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
  view->event_handlers.request_move.notify = xdg_toplevel_request_move;
  wl_signal_add(&toplevel->events.request_move,
                &view->event_handlers.request_move);
  view->event_handlers.request_resize.notify = xdg_toplevel_request_resize;
  wl_signal_add(&toplevel->events.request_resize,
                &view->event_handlers.request_resize);

  wl_signal_init(&view->events.destroy);

  return view;
}

void view_destroy(struct view *view) { free(view); }

void view_focus(const struct view *view) {
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

struct wlr_surface *view_surface_at(struct view *view, double lx, double ly,
                                    double *sx, double *sy) {

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

struct view *view_at(struct wl_list *views, double lx, double ly, double *sx,
                     double *sy) {
  /* This iterates over all of our surfaces and attempts to find one under the
   * cursor. This relies on server->views being ordered from top-to-bottom. */

  return NULL;
}

void view_set_geometry(struct view *view, const struct wlr_box *geom) {
  view->x = geom->x;
  view->y = geom->y;
  wlr_xdg_toplevel_set_size(view->xdg_surface, geom->width, geom->height);
}

struct wlr_box view_geometry(const struct view *view) {
  struct wlr_box b;
  wlr_xdg_surface_get_geometry(view->xdg_surface, &b);

  b.x = view->x;
  b.y = view->y;

  return b;
}

bool view_is_mapped(const struct view *view) { return view->mapped; }

bool view_is_focused(const struct view *view) {
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
  const struct view *view;
  const struct timespec *when;
};

static void render_surface(struct wlr_surface *surface, int sx, int sy,
                           void *data) {
  struct render_data *rdata = data;
  const struct view *view = rdata->view;
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
  struct wlr_box view_geom = view_geometry(view);
  wlr_output_layout_output_coords(output_layout, output, &ox, &oy);
  ox += view_geom.x + sx, oy += view_geom.y + sy;

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

void view_render(const struct view *view, struct wlr_output *output,
                 struct wlr_output_layout *output_layout,
                 const struct timespec *now) {
  if (!view->mapped) {
    return;
  }

  struct render_data rdata = {
      .output_layout = output_layout,
      .output = output,
      .view = view,
      .renderer = view->xdg_surface->surface->renderer,
      .when = now,
  };

  // This handles subsurfaces and popup surfaces as well
  wlr_xdg_surface_for_each_surface(view->xdg_surface, render_surface, &rdata);
}
