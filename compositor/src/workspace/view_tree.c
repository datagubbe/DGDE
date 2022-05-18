#include "view_tree.h"
#include "decorations.h"
#include "view.h"

#include <stdint.h>
#include <stdlib.h>

struct view_node {
  struct view *view;
  struct view_node *left;
  struct view_node *right;
  struct view_node *parent;
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

static void resize_view(struct view_node *node, void *unused) {
  struct view *view = node->view;
  struct wlr_box geom = with_borders(&node->geom);
  view_set_geometry(view, &geom);
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

static void resize_node(struct view_node *node, void *data) {
  if (view_tree_node_view(node) != NULL) {
    resize_view(node, NULL);
    return;
  }

  struct wlr_box *results[2] = {&node->left->geom, &node->right->geom};
  child_geom(&node->geom, results);
}

static struct view_node *split_node(struct view_node *parent) {
  // if there is no view in the node, just use that one
  if (parent->view == NULL) {
    return parent;
  }

  // otherwise, split the node downwards in the tree, moving the current view to
  // the left side of the tree
  parent->left = calloc(1, sizeof(struct view_node));
  parent->left->view = parent->view;
  parent->left->parent = parent;

  parent->right = calloc(1, sizeof(struct view_node));
  parent->right->parent = parent;

  struct wlr_box *results[2] = {&parent->left->geom, &parent->right->geom};
  child_geom(&parent->geom, results);

  parent->view = NULL;

  return parent->right;
}

static void free_node(struct view_node *node, void *_unused) {
  // views should be destroyed through other means
  free(node);
}

struct view_node *view_tree_create(const uint32_t width,
                                   const uint32_t height) {
  struct view_node *root = calloc(1, sizeof(struct view_node));
  root->geom = (struct wlr_box){
      .x = 0,
      .y = 0,
      .width = width,
      .height = height,
  };

  return root;
}

void view_tree_destroy(struct view_node *root) {
  view_tree_iter_all_nodes(root, free_node, NULL);
}

void view_tree_resize(struct view_node *root, const uint32_t width,
                      const uint32_t height) {
  root->geom =
      (struct wlr_box){.x = 0, .y = 0, .width = width, .height = height};

  view_tree_iter_all_nodes(root, resize_node, NULL);
}

void view_tree_iter_nodes(struct view_node *root, iter_nodes_fn fn,
                          void *userdata) {
  struct view_node *curr = root;
  struct view_node *prev = NULL;
  while (curr != NULL) {

    // leaf node (has view)
    if (curr->view != NULL) {
      fn(curr, userdata);

      prev = curr;
      curr = curr->parent;
    } else {
      struct view_node *origin = prev;
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

void view_tree_iter_all_nodes(struct view_node *root, iter_nodes_fn fn,
                              void *userdata) {
  struct view_node *curr = root;
  struct view_node *prev = NULL;
  while (curr != NULL) {

    // leaf node (has view)
    if (curr->view != NULL) {
      fn(curr, userdata);

      prev = curr;
      curr = curr->parent;
    } else {
      struct view_node *origin = prev;
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

static void find_largest(struct view_node *node, void *data) {
  struct view_node **biggest = data;

  if (*biggest == NULL ||
      node->geom.height * node->geom.width >
          (*biggest)->geom.height * (*biggest)->geom.width) {
    *biggest = node;
  }
}

struct view_node *view_tree_insert_view(struct view_node *root,
                                        struct view *view) {
  // find the largest leaf node
  struct view_node *largest_leaf = NULL;
  view_tree_iter_nodes(root, find_largest, &largest_leaf);

  // there are no leaf nodes before the first window is inserted
  if (largest_leaf == NULL) {
    largest_leaf = root;
  }

  struct view_node *new_node = split_node(largest_leaf);
  new_node->view = view;

  // tree is dirty, need to refresh
  view_tree_iter_nodes(root, resize_view, NULL);

  return new_node;
}

struct node_find_result {
  struct view_node *node;
  const struct view *view;
};

static void find_view(struct view_node *node, void *data) {
  struct node_find_result *search = data;

  if (node->view == search->view) {
    search->node = node;
  }
}

void view_tree_remove_view(struct view_node *root, const struct view *view) {
  // find the view
  struct node_find_result res = {.node = NULL, .view = view};
  view_tree_iter_nodes(root, find_view, &res);

  if (res.node != NULL) {
    // move the view on the other side up the tree
    if (res.node == res.node->parent->left) {
      res.node->parent->view = res.node->parent->right->view;
    } else {
      res.node->parent->view = res.node->parent->left->view;
    }

    free(res.node->parent->left);
    free(res.node->parent->right);
    res.node->parent->left = res.node->parent->right = NULL;
  }
}

const struct view *view_tree_node_view(const struct view_node *node) {
  return node->view;
}

const struct wlr_box *view_tree_node_geom(const struct view_node *node) {
  return &node->geom;
}
