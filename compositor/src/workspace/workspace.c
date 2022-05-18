#define _POSIX_C_SOURCE 200809L

#include "workspace.h"
#include "cursor.h"
#include "decorations.h"
#include "server.h"
#include "view.h"
#include "view_tree.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

struct workspace *workspace_create(const char *display_name,
                                   struct wlr_seat *seat, const uint32_t width,
                                   const uint32_t height) {
  struct workspace *ws = calloc(1, sizeof(struct workspace));
  ws->name = strdup(display_name);
  ws->seat = seat;
  ws->tree = view_tree_create(width, height);
  return ws;
}

void workspace_resize(struct workspace *workspace, const uint32_t width,
                      const uint32_t height) {

  view_tree_resize(workspace->tree, width, height);
}

void workspace_destroy(struct workspace *workspace) {
  view_tree_destroy(workspace->tree);
  free(workspace);
}

struct node_intersection {
  const struct view_node *node;
  struct cursor_position *pos;
};

void node_intersects(struct view_node *node, void *data) {
  struct node_intersection *d = data;

  if (wlr_box_contains_point(view_tree_node_geom(node), d->pos->x, d->pos->y)) {
  }
}

void workspace_on_cursor_motion(struct workspace *workspace,
                                struct cursor *cursor,
                                const struct wlr_event_pointer_motion *event) {

  uint32_t time = event->time_msec;

  // find the view under the cursor
  double sx, sy;
  struct wlr_seat *seat = workspace->seat;
  struct cursor_position pos = cursor_position(cursor);
  struct node_intersection intersection_result = (struct node_intersection){
      .node = NULL,
      .pos = &pos,
  };
  view_tree_iter_nodes(workspace->tree, node_intersects, &intersection_result);

  const struct view *view = NULL;
  if (intersection_result.node != NULL) {
    view = view_tree_node_view(intersection_result.node);
  }

  if (!view) {
    /* If there's no view under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * around the screen, not over any views. */
    cursor_set_image(cursor, "left_ptr");
  } else {
    struct wlr_surface *surface =
        view_surface_at((struct view *)view, pos.x, pos.y, &sx, &sy);
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
    /* Clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it. */
    wlr_seat_pointer_clear_focus(seat);
  }
}

void workspace_on_cursor_button(struct workspace *workspace,
                                struct cursor *cursor,
                                struct wlr_event_pointer_button *event) {
  struct cursor_position pos = cursor_position(cursor);
  const struct view *view = NULL;

  struct node_intersection intersection_result = (struct node_intersection){
      .node = NULL,
      .pos = &pos,
  };
  view_tree_iter_nodes(workspace->tree, node_intersects, &intersection_result);

  if (intersection_result.node != NULL) {
    view = view_tree_node_view(intersection_result.node);
  }

  if (view != NULL && event->state == WLR_BUTTON_PRESSED) {
    // focus the client if the button was pressed
    view_focus(view);
  }
}

struct rdata {
  struct wlr_output *output;
  struct wlr_output_layout *layout;
  struct wlr_renderer *renderer;
  struct timespec now;
};

static void darken(const float in[4], float *result, float amount) {
  result[0] = in[0] * (1.f - amount);
  result[1] = in[1] * (1.f - amount);
  result[2] = in[2] * (1.f - amount);
  result[3] = in[3];
}

static void lighten(const float in[4], float *result, float amount) {
  result[0] = in[0] + (1.f * amount);
  result[1] = in[1] + (1.f * amount);
  result[2] = in[2] + (1.f * amount);
  result[3] = in[3];
}

static void render_node(struct view_node *node, void *data) {
  struct rdata *rdata = data;

  // TODO: Configuration for this
  float base[4] = {0.f, 0.5f, 0.f, 1.f};
  float text[4] = {1.f, 1.f, 1.f, 1.f};
  float dark[4];
  darken(base, dark, 0.4);

  float light[4];
  lighten(base, light, 0.2);

  const float *colors[4] = {base, text, dark, light};
  decorate_window(view_tree_node_geom(node), rdata->renderer,
                  rdata->output->transform_matrix, colors);
  view_render(view_tree_node_view(node), rdata->output, rdata->layout,
              &rdata->now);
}

void workspace_render(struct workspace *workspace,
                      struct wlr_renderer *renderer, struct wlr_output *output,
                      struct wlr_output_layout *layout, struct timespec now) {

  struct rdata data = {
      .now = now, .layout = layout, .output = output, .renderer = renderer};
  view_tree_iter_nodes(workspace->tree, render_node, &data);
}

static void on_view_destroyed(struct wl_listener *listener, void *data) {
  struct workspace *workspace =
      wl_container_of(listener, workspace, event_handlers.view_destroyed);
  struct view *view = data;

  view_tree_remove_view(workspace->tree, view);
}

void workspace_add_view(struct workspace *workspace,
                        struct wlr_xdg_surface *surface) {
  struct view *view = view_create(surface, workspace->seat);

  workspace->event_handlers.view_destroyed.notify = on_view_destroyed;
  wl_signal_add(&view->events.destroy,
                &workspace->event_handlers.view_destroyed);

  // Add it to the list of views.
  view_tree_insert_view(workspace->tree, view);
}
