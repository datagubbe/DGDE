#define _POSIX_C_SOURCE 200112L

#include "server.h"
#include "cursor.h"
#include "keyboard.h"
#include "view.h"
#include "workspace/workspace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

static void setup_workspaces(struct server *server, struct output *output,
                             uint32_t num_workspaces,
                             const char *name_pattern) {
  output->num_workspaces = num_workspaces > 16 ? 16 : num_workspaces;
  char buf[256];

  int width, height;
  wlr_output_effective_resolution(output->wlr_output, &width, &height);

  for (uint32_t i = 0, e = output->num_workspaces; i < e; ++i) {
    snprintf(buf, 256, name_pattern, i);
    wlr_log(WLR_DEBUG,
            "creating workspace \"%s\" on output \"%s\" (%p) with geom %dx%d",
            buf, output->wlr_output->description, output, width, height);
    output->workspaces[i] = workspace_create(buf, server->seat, width, height);
  }
}

static struct output *
dgde_output_from_wlr_output(const struct wlr_output *output,
                            const struct wl_list outputs) {
  struct output *res = NULL;
  wl_list_for_each(res, &outputs, link) {
    if (res->wlr_output == output) {
      return res;
    }
  }

  return NULL;
}

static void cursor_motion(const struct server *server,
                          const struct wlr_event_pointer_motion *event) {
  struct cursor_position pos = cursor_position(server->cursor);

  struct wlr_output *wlr_output =
      wlr_output_layout_output_at(server->output_layout, pos.x, pos.y);
  if (wlr_output != NULL) {

    struct output *res =
        dgde_output_from_wlr_output(wlr_output, server->outputs);
    if (res != NULL && res->num_workspaces > 0) {
      struct workspace *ws = res->workspaces[res->active_workspace];
      workspace_on_cursor_motion(ws, server->cursor, event);
    }
  }
}

static void process_cursor_motion(struct wl_listener *listener, void *data) {
  // only send event to current workspace
  struct server *server =
      wl_container_of(listener, server, cursor_events.motion);
  struct wlr_event_pointer_motion *event = data;

  cursor_motion(server, event);
}

static void process_cursor_motion_absolute(struct wl_listener *listener,
                                           void *data) {

  struct server *server =
      wl_container_of(listener, server, cursor_events.motion_absolute);
  struct wlr_event_pointer_motion_absolute *event = data;

  // create a fake relative motion
  struct wlr_event_pointer_motion ev = {
      .delta_x = 0,
      .delta_y = 0,
      .time_msec = event->time_msec,
      .device = event->device,
  };
  cursor_motion(server, &ev);
}

static void process_cursor_button(struct wl_listener *listener, void *data) {

  struct server *server =
      wl_container_of(listener, server, cursor_events.button);
  struct wlr_event_pointer_button *event = data;

  /* Notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
                                 event->state);
}

static void process_cursor_axis(struct wl_listener *listener, void *data) {

  struct server *server = wl_container_of(listener, server, cursor_events.axis);
  struct wlr_event_pointer_axis *event = data;

  /* Notify the client with pointer focus of the axis event. */
  wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
                               event->orientation, event->delta,
                               event->delta_discrete, event->source);
}

static void process_cursor_frame(struct wl_listener *listener, void *data) {
  struct server *server =
      wl_container_of(listener, server, cursor_events.frame);
  /* Notify the client with pointer focus of the frame event. */
  wlr_seat_pointer_notify_frame(server->seat);
}

static bool handle_keybinding(struct server *server, xkb_keysym_t sym) {
  /*
   * Here we handle compositor keybindings. This is when the compositor is
   * processing keys, rather than passing them on to the client for its own
   * processing.
   */
  switch (sym) {
  case XKB_KEY_1:
    wl_display_terminate(server->wl_display);
    break;
  case XKB_KEY_2:
    if (fork() == 0) {
      execlp("color", "color", "-c", "red", NULL);
    }
    break;
  case XKB_KEY_3:
    if (fork() == 0) {
      execlp("color", "color", "-c", "green", NULL);
    }
    break;

  case XKB_KEY_4:
    if (fork() == 0) {
      execlp("color", "color", "-c", "blue", NULL);
    }
    break;

  default:
    return false;
  }
  return true;
}

static void output_frame(struct wl_listener *listener, void *data) {
  /* This function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate (e.g. 60Hz). */
  struct output *output = wl_container_of(listener, output, frame);
  struct wlr_renderer *renderer = output->server->renderer;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  /* wlr_output_attach_render makes the OpenGL context current. */
  if (!wlr_output_attach_render(output->wlr_output, NULL)) {
    return;
  }
  /* The "effective" resolution can change if you rotate your outputs. */
  int width, height;
  wlr_output_effective_resolution(output->wlr_output, &width, &height);
  /* Begin the renderer (calls glViewport and some other GL sanity checks) */
  wlr_renderer_begin(renderer, width, height);

  float color[4] = {0.3, 0.3, 0.3, 1.0};
  wlr_renderer_clear(renderer, color);

  struct workspace *ws = output->workspaces[output->active_workspace];
  workspace_render(ws, output->server->renderer, output->wlr_output,
                   output->server->output_layout, now);

  /* Hardware cursors are rendered by the GPU on a separate plane, and can
   * be moved around without re-rendering what's beneath them - which is
   * more efficient. However, not all hardware supports hardware cursors.
   * For this reason, wlroots provides a software fallback, which we ask it
   * to render here. wlr_cursor handles configuring hardware vs software
   * cursors for you,
   * and this function is a no-op when hardware cursors are in use. */
  wlr_output_render_software_cursors(output->wlr_output, NULL);

  /* Conclude rendering and swap the buffers, showing the final frame
   * on-screen. */
  wlr_renderer_end(renderer);
  wlr_output_commit(output->wlr_output);
}

static void output_mode(struct wl_listener *listener, void *data) {
  struct output *output = wl_container_of(listener, output, mode);
  struct wlr_output *wlr_output = data;

  int width, height;
  wlr_output_effective_resolution(output->wlr_output, &width, &height);

  wlr_log(WLR_DEBUG, "new output mode for %s: %dx%d", wlr_output->description,
          width, height);

  for (uint32_t i = 0, e = output->num_workspaces; i < e; ++i) {
    workspace_resize(output->workspaces[i], width, height);
  }
}

static void new_output(struct wl_listener *listener, void *data) {
  /* This event is rasied by the backend when a new output (aka a display or
   * monitor) becomes available. */
  struct server *server = wl_container_of(listener, server, new_output);
  struct wlr_output *wlr_output = data;

  wlr_log(WLR_DEBUG, "new output: %s", wlr_output->description);

  /* Allocates and configures our state for this output */
  struct output *output = calloc(1, sizeof(struct output));
  output->wlr_output = wlr_output;
  output->server = server;

  setup_workspaces(server, output, 4, "Workspace %d");

  /* Sets up a listener for the frame notify event. */
  output->frame.notify = output_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);
  output->mode.notify = output_mode;
  wl_signal_add(&wlr_output->events.mode, &output->mode);

  wl_list_insert(&server->outputs, &output->link);

  /* Some backends don't have modes. DRM+KMS does, and we need to set a mode
   * before we can use the output. The mode is a tuple of (width, height,
   * refresh rate), and each monitor supports only a specific set of modes. We
   * just pick the monitor's preferred mode, a more sophisticated compositor
   * would let the user configure it. */
  if (!wl_list_empty(&wlr_output->modes)) {
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    wlr_output_set_mode(wlr_output, mode);
    wlr_output_enable(wlr_output, true);
    if (!wlr_output_commit(wlr_output)) {
      return;
    }
  }

  /* Adds this to the output layout. The add_auto function arranges outputs
   * from left-to-right in the order they appear. A more sophisticated
   * compositor would let the user configure the arrangement of outputs in the
   * layout.
   *
   * The output layout utility automatically adds a wl_output global to the
   * display, which Wayland clients can see to find out information about the
   * output (such as DPI, scale factor, manufacturer, etc).
   */
  wlr_output_layout_add_auto(server->output_layout, wlr_output);
}

static void new_xdg_surface(struct wl_listener *listener, void *data) {
  /* This event is raised when wlr_xdg_shell receives a new xdg surface from a
   * client, either a toplevel (application window) or popup. */
  struct server *server = wl_container_of(listener, server, new_xdg_surface);
  struct wlr_xdg_surface *xdg_surface = data;
  if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    return;
  }

  struct output *o = wl_container_of(server->outputs.next, o, link);

  struct workspace *workspace = o->workspaces[o->active_workspace];
  wlr_log(WLR_DEBUG, "inserting view into workspace %d on output %s (%p)",
          o->active_workspace, o->wlr_output->description, o);

  workspace_add_view(workspace, xdg_surface);
}

static void new_input(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new input device becomes
   * available. */
  struct server *server = wl_container_of(listener, server, new_input);
  struct wlr_input_device *device = data;
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD: {
    struct dgde_keyboard *keyboard = dgde_keyboard_create(device, server->seat);
    dgde_keyboard_add_handler(keyboard, (keybind_handler)handle_keybinding,
                              server, WLR_MODIFIER_ALT);

    dgde_keyboard_add_to_list(keyboard, &server->keyboards);
    break;
  }
  case WLR_INPUT_DEVICE_POINTER:
    cursor_new_pointer(server->cursor, device);
    break;
  default:
    break;
  }

  // let the seat know what we can do (have pointer by default even if there are
  // no pointer hardware devices)
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&server->keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_set_selection(struct wl_listener *listener,
                                       void *data) {
  /* This event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in tinywl we always honor
   */
  struct server *server =
      wl_container_of(listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

struct server *server_create(const char *seat_name) {

  struct server *server = calloc(1, sizeof(struct server));
  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, manging Wayland globals, and so on. */
  server->wl_display = wl_display_create();

  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. */
  server->backend = wlr_backend_autocreate(server->wl_display);

  /* If we don't provide a renderer, autocreate makes a GLES2 renderer for us.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  server->renderer = wlr_backend_get_renderer(server->backend);
  wlr_renderer_init_wl_display(server->renderer, server->wl_display);

  /* This creates some hands-off wlroots interfaces. The compositor is
   * necessary for clients to allocate surfaces and the data device manager
   * handles the clipboard. Each of these wlroots interfaces has room for you
   * to dig your fingers in and play with their behavior if you want. Note that
   * the clients cannot set the selection directly without compositor approval,
   * see the handling of the request_set_selection event below.*/
  wlr_compositor_create(server->wl_display, server->renderer);
  wlr_data_device_manager_create(server->wl_display);

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  server->output_layout = wlr_output_layout_create();

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&server->outputs);
  server->new_output.notify = new_output;
  wl_signal_add(&server->backend->events.new_output, &server->new_output);

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&server->keyboards);
  server->new_input.notify = new_input;
  wl_signal_add(&server->backend->events.new_input, &server->new_input);
  server->seat = wlr_seat_create(server->wl_display, seat_name);
  server->request_set_selection.notify = seat_request_set_selection;
  wl_signal_add(&server->seat->events.request_set_selection,
                &server->request_set_selection);

  server->xdg_shell = wlr_xdg_shell_create(server->wl_display);
  server->new_xdg_surface.notify = new_xdg_surface;
  wl_signal_add(&server->xdg_shell->events.new_surface,
                &server->new_xdg_surface);

  // create a cursor
  struct cursor *cursor = cursor_create(server->output_layout, server->seat);

  server->cursor_events.motion.notify = process_cursor_motion;
  wl_signal_add(&cursor->events.motion, &server->cursor_events.motion);

  server->cursor_events.motion_absolute.notify = process_cursor_motion_absolute;
  wl_signal_add(&cursor->events.motion_absolute,
                &server->cursor_events.motion_absolute);

  server->cursor_events.button.notify = process_cursor_button;
  wl_signal_add(&cursor->events.button, &server->cursor_events.button);

  server->cursor_events.axis.notify = process_cursor_axis;
  wl_signal_add(&cursor->events.axis, &server->cursor_events.axis);

  server->cursor_events.frame.notify = process_cursor_frame;
  wl_signal_add(&cursor->events.frame, &server->cursor_events.frame);

  server->cursor = cursor;
  return server;
}

const char *server_attach_socket(struct server *server) {

  /* Add a Unix socket to the Wayland display. */
  return wl_display_add_socket_auto(server->wl_display);
}

void server_run(struct server *server) {

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if (!wlr_backend_start(server->backend)) {
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->wl_display);

    return;
  }

  wl_display_run(server->wl_display);
}

void server_destroy(struct server *server) {
  wl_display_destroy_clients(server->wl_display);
  wl_display_destroy(server->wl_display);

  free(server);
}
