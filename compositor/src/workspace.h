#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "cursor.h"
#include "src/server.h"

#include <stdint.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

struct dgde_workspace;
struct dgde_output;

struct dgde_workspace *dgde_workspace_create(const char *display_name,
                                             struct wlr_seat *seat,
                                             const uint32_t width,
                                             const uint32_t height);

void dgde_workspace_resize(struct dgde_workspace *workspace,
                           const uint32_t width, const uint32_t height);

void dgde_workspace_destroy(struct dgde_workspace *workspace);

void dgde_workspace_on_cursor_motion(struct dgde_workspace *workspace,
                                     struct dgde_cursor *cursor,
                                     struct wlr_event_pointer_motion *event);

void dgde_workspace_on_cursor_button(struct dgde_workspace *workspace,
                                     struct dgde_cursor *cursor,
                                     struct wlr_event_pointer_button *event);

void dgde_workspace_render(struct dgde_workspace *workspace,
                           struct wlr_renderer *renderer,
                           struct wlr_output *output,
                           struct wlr_output_layout *layout,
                           struct timespec now);

void dgde_workspace_add_view(struct dgde_workspace *workspace,
                             struct wlr_xdg_surface *surface);

#endif
