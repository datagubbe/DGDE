#define _POSIX_C_SOURCE 200809L

#include "workspace.h"
#include "cursor.h"
#include "decorations.h"
#include "server.h"
#include "view.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

struct dgde_workspace {
  const char *name;

  // binary tree of views
  struct node *root;

  struct wlr_seat *seat;
};

struct node {
  struct dgde_view *view;
  struct node *left;
  struct node *right;
  struct node *parent;
  struct wlr_box geom;
};

static struct wlr_box with_borders(const struct wlr_box *geom) {

  struct wlr_box with_borders = {0};
  uint32_t left_margin = DECORATION_SIZE[3];
  uint32_t top_margin = DECORATION_SIZE[0];
  uint32_t right_margin = DECORATION_SIZE[1];
  uint32_t bottom_margin = DECORATION_SIZE[2];
  with_borders.x = geom->x + left_margin;
  with_borders.width = geom->width - (left_margin + right_margin);
  with_borders.y = geom->y + (top_margin);
  with_borders.height = geom->height - (top_margin + bottom_margin);

  return with_borders;
}

static void child_geom(const struct wlr_box *parent_geom,
                       struct wlr_box *child_geoms[2]) {
  uint32_t new_width = parent_geom->width;
  uint32_t xoffset = 0;
  uint32_t new_height = parent_geom->height;
  uint32_t yoffset = 0;
  if (parent_geom->height > parent_geom->width) {
    new_height = parent_geom->height / 2;
    yoffset = new_height;
  } else {
    new_width = parent_geom->width / 2;
    xoffset = new_width;
  }

  *child_geoms[0] = *parent_geom;
  child_geoms[0]->width = new_width;
  child_geoms[0]->height = new_height;

  *child_geoms[1] = *parent_geom;
  child_geoms[1]->x = parent_geom->x + xoffset;
  child_geoms[1]->y = parent_geom->y + yoffset;
  child_geoms[1]->width = new_width;
  child_geoms[1]->height = new_height;
}

static struct node *split_node(struct node *parent) {
  // if there is no view in the node, just use that one
  if (parent->view == NULL) {
    return parent;
  }

  // otherwise, split the node downwards in the tree, moving the current view to
  // the left side of the tree
  parent->left = calloc(1, sizeof(struct node));
  parent->left->view = parent->view;
  parent->left->parent = parent;

  parent->right = calloc(1, sizeof(struct node));
  parent->right->parent = parent;

  struct wlr_box *results[2] = {&parent->left->geom, &parent->right->geom};
  child_geom(&parent->geom, results);

  parent->view = NULL;

  return parent->right;
}

static void resize_view(struct node *node, void *unused) {
  struct dgde_view *view = node->view;

  struct wlr_box geom = with_borders(&node->geom);

  dgde_view_set_position(view,
                         (struct dgde_view_position){.x = geom.x, .y = geom.y});
  dgde_view_set_size(view, (struct dgde_view_size){
                               .height = geom.height,
                               .width = geom.width,
                           });
}

typedef void (*iter_nodes_fn)(struct node *node, void *data);

static void iter_nodes(struct node *root, iter_nodes_fn fn, void *userdata) {
  struct node *curr = root;
  struct node *prev = NULL;
  while (curr != NULL) {

    // leaf node (has view)
    if (curr->view != NULL) {
      fn(curr, userdata);

      prev = curr;
      curr = curr->parent;
    } else {
      struct node *origin = prev;
      prev = curr;
      if (origin == curr->left) {
        // we came to the parent from the left -> go right next
        curr = curr->right;
      } else if (origin == curr->right) {
        // we came from the parent from the right -> we are done with that
        // subtree so go up
        curr = curr->parent;
      } else {
        // we came from the parent so continue down the left side
        curr = curr->left;
      }
    }
  }
}

static void iter_all_nodes(struct node *root, iter_nodes_fn fn,
                           void *userdata) {
  struct node *curr = root;
  struct node *prev = NULL;
  while (curr != NULL) {

    // leaf node (has view)
    if (curr->view != NULL) {
      fn(curr, userdata);

      prev = curr;
      curr = curr->parent;
    } else {
      struct node *origin = prev;
      prev = curr;
      if (origin == curr->left) {
        // we came to the parent from the left -> go right next
        curr = curr->right;
      } else if (origin == curr->right) {
        // we came from the parent from the right -> we are done with that
        // subtree so go up
        curr = curr->parent;
      } else {
        // we came from the parent so continue down the left side
        fn(curr, userdata);
        curr = curr->left;
      }
    }
  }
}

static void find_largest(struct node *node, void *data) {
  struct node **biggest = data;

  if (*biggest == NULL ||
      node->geom.height * node->geom.width >
          (*biggest)->geom.height * (*biggest)->geom.width) {
    *biggest = node;
  }
}

static struct node *insert_node(struct node *root, struct dgde_view *view) {
  // find the largest leaf node
  struct node *largest_leaf = NULL;
  iter_nodes(root, find_largest, &largest_leaf);

  // there are no leaf nodes before the first window is inserted
  if (largest_leaf == NULL) {
    largest_leaf = root;
  }

  struct node *new_node = split_node(largest_leaf);
  new_node->view = view;

  return new_node;
}

struct dgde_workspace *dgde_workspace_create(const char *display_name,
                                             struct wlr_seat *seat,
                                             const uint32_t width,
                                             const uint32_t height) {
  struct dgde_workspace *ws = calloc(1, sizeof(struct dgde_workspace));
  ws->name = strdup(display_name);
  ws->seat = seat;
  ws->root = calloc(1, sizeof(struct node));
  ws->root->geom = (struct wlr_box){
      .x = 0,
      .y = 0,
      .width = width,
      .height = height,
  };

  return ws;
}

static void resize_node(struct node *node, void *data) {
  if (node->view != NULL) {
    resize_view(node, NULL);
    return;
  }

  struct wlr_box *results[2] = {&node->left->geom, &node->right->geom};
  child_geom(&node->geom, results);
}

void dgde_workspace_resize(struct dgde_workspace *workspace,
                           const uint32_t width, const uint32_t height) {
  workspace->root->geom =
      (struct wlr_box){.x = 0, .y = 0, .width = width, .height = height};

  iter_all_nodes(workspace->root, resize_node, NULL);
}

void dgde_workspace_destroy(struct dgde_workspace *workspace) {
  // TODO: free all views
  free(workspace);
}

struct node_intersection {
  struct node *node;
  struct dgde_cursor_position *pos;
};

void node_intersects(struct node *node, void *data) {
  struct node_intersection *d = data;

  if (wlr_box_contains_point(&node->geom, d->pos->x, d->pos->y)) {
  }
}

void dgde_workspace_on_cursor_motion(struct dgde_workspace *workspace,
                                     struct dgde_cursor *cursor,
                                     struct wlr_event_pointer_motion *event) {

  uint32_t time = event->time_msec;

  // find the view under the cursor
  double sx, sy;
  struct wlr_seat *seat = workspace->seat;
  struct dgde_cursor_position pos = dgde_cursor_position(cursor);
  struct node_intersection intersection_result = (struct node_intersection){
      .node = NULL,
      .pos = &pos,
  };
  iter_nodes(workspace->root, node_intersects, &intersection_result);

  struct dgde_view *view = NULL;
  if (intersection_result.node != NULL) {
    view = intersection_result.node->view;
  }

  if (!view) {
    /* If there's no view under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * around the screen, not over any views. */
    dgde_cursor_set_image(cursor, "left_ptr");
  } else {
    struct wlr_surface *surface =
        dgde_view_surface_at(view, pos.x, pos.y, &sx, &sy);
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

void dgde_workspace_on_cursor_button(struct dgde_workspace *workspace,
                                     struct dgde_cursor *cursor,
                                     struct wlr_event_pointer_button *event) {
  struct dgde_cursor_position pos = dgde_cursor_position(cursor);
  struct dgde_view *view = NULL;

  struct node_intersection intersection_result = (struct node_intersection){
      .node = NULL,
      .pos = &pos,
  };
  iter_nodes(workspace->root, node_intersects, &intersection_result);

  if (intersection_result.node != NULL) {
    view = intersection_result.node->view;
  }

  if (view != NULL && event->state == WLR_BUTTON_PRESSED) {
    // focus the client if the button was pressed
    dgde_view_focus(view);
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

static void render_node(struct node *node, void *data) {
  struct rdata *rdata = data;

  struct wlr_box b = {
      .x = node->geom.x,
      .y = node->geom.y,
      .width = node->geom.width,
      .height = node->geom.height,
  };

  // TODO: Configuration for this
  float base[4] = {0.f, 0.5f, 0.f, 1.f};
  float text[4] = {1.f, 1.f, 1.f, 1.f};
  float dark[4];
  darken(base, dark, 0.4);

  float light[4];
  lighten(base, light, 0.2);

  const float *colors[4] = {base, text, dark, light};
  decorate_window(&b, rdata->renderer, rdata->output->transform_matrix, colors);
  dgde_view_render(node->view, rdata->output, rdata->layout, &rdata->now);
}

void dgde_workspace_render(struct dgde_workspace *workspace,
                           struct wlr_renderer *renderer,
                           struct wlr_output *output,
                           struct wlr_output_layout *layout,
                           struct timespec now) {

  struct rdata data = {
      .now = now, .layout = layout, .output = output, .renderer = renderer};
  iter_nodes(workspace->root, render_node, &data);
}

void dgde_workspace_add_view(struct dgde_workspace *workspace,
                             struct wlr_xdg_surface *surface) {
  /* Allocate a view for this surface */
  struct dgde_view *view = dgde_view_create(surface, workspace->seat);

  // Add it to the list of views.
  wlr_log(WLR_DEBUG, "inserting new view into tree on workspace %s",
          workspace->name);
  insert_node(workspace->root, view);

  // tree is dirty, need to resize all windows
  iter_nodes(workspace->root, resize_view, NULL);
}
