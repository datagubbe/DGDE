#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "cursor.h"
#include "server.h"

#include <stdint.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

struct workspace {
  const char *name;

  // binary tree of views
  struct view_node *tree;
  struct wlr_seat *seat;

  struct {
    struct wl_listener view_destroyed;
  } event_handlers;
};

struct output;

struct workspace *workspace_create(const char *display_name,
                                   struct wlr_seat *seat, const uint32_t width,
                                   const uint32_t height);
void workspace_destroy(struct workspace *workspace);

void workspace_resize(struct workspace *workspace, const uint32_t width,
                      const uint32_t height);

void workspace_on_cursor_motion(struct workspace *workspace,
                                struct cursor *cursor,
                                const struct wlr_event_pointer_motion *event);

void workspace_on_cursor_button(struct workspace *workspace,
                                struct cursor *cursor,
                                struct wlr_event_pointer_button *event);

void workspace_render(struct workspace *workspace,
                      struct wlr_renderer *renderer, struct wlr_output *output,
                      struct wlr_output_layout *layout, struct timespec now);

void workspace_add_view(struct workspace *workspace,
                        struct wlr_xdg_surface *surface);

#endif
