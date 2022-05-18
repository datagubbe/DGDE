#ifndef VIEW_TREE_H
#define VIEW_TREE_H

#include "server.h"
#include <stdint.h>

struct view_node;
struct view;

struct view_node *view_tree_create(const uint32_t width, const uint32_t height);
void view_tree_destroy(struct view_node *root);

void view_tree_resize(struct view_node *root, const uint32_t width,
                      const uint32_t height);

struct view_node *view_tree_insert_view(struct view_node *root,
                                        struct view *view);

void view_tree_remove_view(struct view_node *root, const struct view *view);

typedef void (*iter_nodes_fn)(struct view_node *node, void *data);
void view_tree_iter_nodes(struct view_node *root, iter_nodes_fn fn,
                          void *userdata);
void view_tree_iter_all_nodes(struct view_node *root, iter_nodes_fn fn,
                              void *userdata);

const struct view *view_tree_node_view(const struct view_node *node);
const struct wlr_box *view_tree_node_geom(const struct view_node *node);

#endif
