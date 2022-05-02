#define _POSIX_C_SOURCE 200112L

#include "server.h"
#include "cursor.h"
#include "keyboard.h"
#include "view.h"

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

struct dgde_server {
  struct wl_display *wl_display;
  struct wlr_backend *backend;
  struct wlr_renderer *renderer;

  struct wlr_xdg_shell *xdg_shell;
  struct wl_listener new_xdg_surface;
  struct wl_list views;

  struct dgde_cursor *cursor;

  struct wlr_seat *seat;
  struct wl_listener new_input;
  struct wl_listener request_set_selection;
  struct wl_list keyboards;
  struct dgde_view *grabbed_view;
  double grab_x, grab_y;
  struct wlr_box grab_geobox;
  uint32_t resize_edges;

  struct wlr_output_layout *output_layout;
  struct wl_list outputs;
  struct wl_listener new_output;
};

struct dgde_output {
  struct wl_list link;
  struct dgde_server *server;
  struct wlr_output *wlr_output;
  struct wl_listener frame;
};

struct view_elem {
  struct dgde_view *view;
  struct wl_list link;
};

static void process_cursor_move(struct dgde_server *server, uint32_t time) {
  /* Move the grabbed view to the new position. */
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  dgde_view_set_position(
      server->grabbed_view,
      (struct dgde_view_position){.x = (pos.x - server->grab_x),
                                  .y = (pos.y - server->grab_y)});
}

static void process_cursor_resize(struct dgde_server *server, uint32_t time) {
  /*
   * Resizing the grabbed view can be a little bit complicated, because we
   * could be resizing from any corner or edge. This not only resizes the view
   * on one or two axes, but can also move the view if you resize from the top
   * or left edges (or top-left corner).
   *
   * Note that I took some shortcuts here. In a more fleshed-out compositor,
   * you'd wait for the client to prepare a buffer at the new size, then
   * commit any movement that was prepared.
   */
  struct dgde_view *view = server->grabbed_view;
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  double border_x = pos.x - server->grab_x;
  double border_y = pos.y - server->grab_y;
  int new_left = server->grab_geobox.x;
  int new_right = server->grab_geobox.x + server->grab_geobox.width;
  int new_top = server->grab_geobox.y;
  int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

  if (server->resize_edges & WLR_EDGE_TOP) {
    new_top = border_y;
    if (new_top >= new_bottom) {
      new_top = new_bottom - 1;
    }
  } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
    new_bottom = border_y;
    if (new_bottom <= new_top) {
      new_bottom = new_top + 1;
    }
  }
  if (server->resize_edges & WLR_EDGE_LEFT) {
    new_left = border_x;
    if (new_left >= new_right) {
      new_left = new_right - 1;
    }
  } else if (server->resize_edges & WLR_EDGE_RIGHT) {
    new_right = border_x;
    if (new_right <= new_left) {
      new_right = new_left + 1;
    }
  }

  struct wlr_box geo_box = dgde_view_geometry(view);
  dgde_view_set_position(view,
                         (struct dgde_view_position){.x = new_left = geo_box.x,
                                                     .y = new_top = geo_box.y});
  int new_width = new_right - new_left;
  int new_height = new_bottom - new_top;
  dgde_view_set_size(
      view, (struct dgde_view_size){.width = new_width, .height = new_height});
}

static void process_cursor_motion(struct dgde_server *server,
                                  struct wlr_event_pointer_motion *event) {
  /* If the mode is non-passthrough, delegate to those functions. */
  uint32_t time = event->time_msec;
  enum dgde_cursor_mode mode = dgde_cursor_mode(server->cursor);
  if (mode == DgdeCursor_Move) {
    process_cursor_move(server, time);
    return;
  } else if (mode == DgdeCursor_Resize) {
    process_cursor_resize(server, time);
    return;
  }

  /* Otherwise, find the view under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server->seat;
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  struct dgde_view *view = dgde_view_at(&server->views, pos.x, pos.y, &sx, &sy);

  if (!view) {
    /* If there's no view under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * around the screen, not over any views. */
    dgde_cursor_set_image(server->cursor, "left_ptr");
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

static void process_cursor_motion_absolute(
    struct dgde_server *server,
    struct wlr_event_pointer_motion_absolute *event) {

  // create a fake relative motion
  struct wlr_event_pointer_motion ev = {
      .delta_x = 0,
      .delta_y = 0,
      .time_msec = event->time_msec,
      .device = event->device,
  };
  process_cursor_motion(server, &ev);
}

static void process_cursor_button(struct dgde_server *server,
                                  struct wlr_event_pointer_button *event) {

  /* Notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
                                 event->state);
  double sx, sy;
  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  struct view_elem *view_elem;
  struct dgde_view *view = NULL;
  wl_list_for_each(view_elem, &server->views, link) {
    if (dgde_view_surface_at(view_elem->view, pos.x, pos.y, &sx, &sy) != NULL) {
      view = view_elem->view;
      break;
    }
  }

  if (view != NULL && event->state == WLR_BUTTON_PRESSED) {
    // focus the client if the button was pressed
    dgde_view_focus(view);

    // move the view to the front
    wl_list_remove(&view_elem->link);
    wl_list_insert(&server->views, &view_elem->link);
  }
}

static void process_cursor_axis(struct dgde_server *server,
                                struct wlr_event_pointer_axis *event) {
  /* Notify the client with pointer focus of the axis event. */
  wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
                               event->orientation, event->delta,
                               event->delta_discrete, event->source);
}

static void process_cursor_frame(struct dgde_server *server) {
  /* Notify the client with pointer focus of the frame event. */
  wlr_seat_pointer_notify_frame(server->seat);
}

static bool handle_keybinding(struct dgde_server *server, xkb_keysym_t sym) {
  /*
   * Here we handle compositor keybindings. This is when the compositor is
   * processing keys, rather than passing them on to the client for its own
   * processing.
   */
  switch (sym) {
  case XKB_KEY_1:
    wl_display_terminate(server->wl_display);
    break;
  default:
    return false;
  }
  return true;
}

static void begin_interactive(void *data, struct dgde_view *view,
                              enum dgde_cursor_mode mode, uint32_t edges) {
  /* This function sets up an interactive move or resize operation, where the
   * compositor stops propagating pointer events to clients and instead
   * consumes them itself, to move or resize windows. */
  struct dgde_server *server = data;

  if (!dgde_view_is_focused(view)) {
    /* Deny move/resize requests from unfocused clients. */
    return;
  }

  server->grabbed_view = view;
  dgde_cursor_set_mode(server->cursor, mode);

  struct dgde_cursor_position pos = dgde_cursor_position(server->cursor);
  struct dgde_view_position view_pos = dgde_view_position(view);

  if (mode == DgdeCursor_Move) {
    server->grab_x = pos.x - view_pos.x;
    server->grab_y = pos.y - view_pos.y;
  } else {
    struct wlr_box geo_box = dgde_view_geometry(view);

    double border_x = (view_pos.x + geo_box.x) +
                      ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
    double border_y = (view_pos.y + geo_box.y) +
                      ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
    server->grab_x = pos.x - border_x;
    server->grab_y = pos.y - border_y;

    server->grab_geobox = geo_box;
    server->grab_geobox.x += view_pos.x;
    server->grab_geobox.y += view_pos.y;

    server->resize_edges = edges;
  }
}

static void output_frame(struct wl_listener *listener, void *data) {
  /* This function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate (e.g. 60Hz). */
  struct dgde_output *output = wl_container_of(listener, output, frame);
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

  /* Each subsequent window we render is rendered on top of the last. Because
   * our view list is ordered front-to-back, we iterate over it backwards. */
  struct view_elem *view;
  wl_list_for_each_reverse(view, &output->server->views, link) {
    dgde_view_render(view->view, output->wlr_output,
                     output->server->output_layout, &now);
  }

  /* Hardware cursors are rendered by the GPU on a separate plane, and can be
   * moved around without re-rendering what's beneath them - which is more
   * efficient. However, not all hardware supports hardware cursors. For this
   * reason, wlroots provides a software fallback, which we ask it to render
   * here. wlr_cursor handles configuring hardware vs software cursors for you,
   * and this function is a no-op when hardware cursors are in use. */
  wlr_output_render_software_cursors(output->wlr_output, NULL);

  /* Conclude rendering and swap the buffers, showing the final frame
   * on-screen. */
  wlr_renderer_end(renderer);
  wlr_output_commit(output->wlr_output);
}

static void new_output(struct wl_listener *listener, void *data) {
  /* This event is rasied by the backend when a new output (aka a display or
   * monitor) becomes available. */
  struct dgde_server *server = wl_container_of(listener, server, new_output);
  struct wlr_output *wlr_output = data;

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

  /* Allocates and configures our state for this output */
  struct dgde_output *output = calloc(1, sizeof(struct dgde_output));
  output->wlr_output = wlr_output;
  output->server = server;
  /* Sets up a listener for the frame notify event. */
  output->frame.notify = output_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);
  wl_list_insert(&server->outputs, &output->link);

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
  struct dgde_server *server =
      wl_container_of(listener, server, new_xdg_surface);
  struct wlr_xdg_surface *xdg_surface = data;
  if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    return;
  }

  /* Allocate a view for this surface */
  struct dgde_view *view = dgde_view_create(xdg_surface, server->seat);
  dgde_view_add_interaction_handler(view, begin_interactive, server);

  // Add it to the list of views. TODO: remove when view is destroyed
  struct view_elem *elem = calloc(1, sizeof(struct view_elem));
  elem->view = view;
  wl_list_insert(&server->views, &elem->link);
}

static void new_input(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new input device becomes
   * available. */
  struct dgde_server *server = wl_container_of(listener, server, new_input);
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
    dgde_cursor_new_pointer(server->cursor, device);
    break;
  default:
    break;
  }
  /* We need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. In TinyWL we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
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
  struct dgde_server *server =
      wl_container_of(listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

struct dgde_server *dgde_server_create(const char *seat_name) {

  struct dgde_server *server = calloc(1, sizeof(struct dgde_server));
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

  // set up an empty list of views and start listening to xdg-shell events
  wl_list_init(&server->views);
  server->xdg_shell = wlr_xdg_shell_create(server->wl_display);
  server->new_xdg_surface.notify = new_xdg_surface;
  wl_signal_add(&server->xdg_shell->events.new_surface,
                &server->new_xdg_surface);

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

  // create a cursor
  struct dgde_cursor *cursor =
      dgde_cursor_create(server->output_layout, server->seat);
  struct dgde_cursor_handler cursor_handler = {
      .button = (dgde_cursor_button_cb)process_cursor_button,
      .motion = (dgde_cursor_motion_cb)process_cursor_motion,
      .motion_absolute =
          (dgde_cursor_motion_absolute_cb)process_cursor_motion_absolute,
      .axis = (dgde_cursor_axis_cb)process_cursor_axis,
      .frame = (dgde_cursor_frame_cb)process_cursor_frame,
      .userdata = server};
  dgde_cursor_add_handler(cursor, &cursor_handler);

  server->cursor = cursor;
  return server;
}

const char *dgde_server_attach_socket(struct dgde_server *server) {

  /* Add a Unix socket to the Wayland display. */
  return wl_display_add_socket_auto(server->wl_display);
}

void dgde_server_run(struct dgde_server *server) {

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if (!wlr_backend_start(server->backend)) {
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->wl_display);

    return;
  }

  wl_display_run(server->wl_display);
}

void dgde_server_destroy(struct dgde_server *server) {
  wl_display_destroy_clients(server->wl_display);
  wl_display_destroy(server->wl_display);

  free(server);
}
