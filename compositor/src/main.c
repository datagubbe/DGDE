#define _POSIX_C_SOURCE 200112L
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "cursor.h"
#include "server.h"
#include "view.h"

struct dgde_output {
  struct wl_list link;
  struct dgde_server *server;
  struct wlr_output *wlr_output;
  struct wl_listener frame;
};

struct dgde_keyboard {
  struct wl_list link;
  struct dgde_server *server;
  struct wlr_input_device *device;

  struct wl_listener modifiers;
  struct wl_listener key;
};

static void keyboard_handle_modifiers(struct wl_listener *listener,
                                      void *data) {
  /* This event is raised when a modifier key, such as shift or alt, is
   * pressed. We simply communicate this to the client. */
  struct dgde_keyboard *keyboard =
      wl_container_of(listener, keyboard, modifiers);
  /*
   * A seat can only have one keyboard, but this is a limitation of the
   * Wayland protocol - not wlroots. We assign all connected keyboards to the
   * same seat. You can swap out the underlying wlr_keyboard like this and
   * wlr_seat handles this transparently.
   */
  wlr_seat_set_keyboard(keyboard->server->seat, keyboard->device);
  /* Send modifiers to the client. */
  wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                     &keyboard->device->keyboard->modifiers);
}

static bool handle_keybinding(struct dgde_server *server, xkb_keysym_t sym) {
  /*
   * Here we handle compositor keybindings. This is when the compositor is
   * processing keys, rather than passing them on to the client for its own
   * processing.
   *
   * This function assumes Alt is held down.
   */
  switch (sym) {
  case XKB_KEY_Escape:
    wl_display_terminate(server->wl_display);
    break;
  case XKB_KEY_F1:
    /* Cycle to the next view */
    if (wl_list_length(&server->views) < 2) {
      break;
    }
    struct dgde_view *current_view =
        wl_container_of(server->views.next, current_view, link);
    struct dgde_view *next_view =
        wl_container_of(current_view->link.next, next_view, link);
    focus_view(next_view, next_view->xdg_surface->surface);
    /* Move the previous view to the end of the list */
    wl_list_remove(&current_view->link);
    wl_list_insert(server->views.prev, &current_view->link);
    break;
  default:
    return false;
  }
  return true;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
  /* This event is raised when a key is pressed or released. */
  struct dgde_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct dgde_server *server = keyboard->server;
  struct wlr_event_keyboard_key *event = data;
  struct wlr_seat *seat = server->seat;

  /* Translate libinput keycode -> xkbcommon */
  uint32_t keycode = event->keycode + 8;
  /* Get a list of keysyms based on the keymap for this keyboard */
  const xkb_keysym_t *syms;
  int nsyms = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
                                     keycode, &syms);

  bool handled = false;
  uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
  if ((modifiers & WLR_MODIFIER_ALT) &&
      event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    /* If alt is held down and this button was _pressed_, we attempt to
     * process it as a compositor keybinding. */
    for (int i = 0; i < nsyms; i++) {
      handled = handle_keybinding(server, syms[i]);
    }
  }

  if (!handled) {
    /* Otherwise, we pass it along to the client. */
    wlr_seat_set_keyboard(seat, keyboard->device);
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                 event->state);
  }
}

static void server_new_keyboard(struct dgde_server *server,
                                struct wlr_input_device *device) {
  struct dgde_keyboard *keyboard = calloc(1, sizeof(struct dgde_keyboard));
  keyboard->server = server;
  keyboard->device = device;

  /* We need to prepare an XKB keymap and assign it to the keyboard. This
   * assumes the defaults (e.g. layout = "us"). */
  struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap *keymap =
      xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_set_keymap(device->keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(device->keyboard, 25, 600);

  /* Here we set up listeners for keyboard events. */
  keyboard->modifiers.notify = keyboard_handle_modifiers;
  wl_signal_add(&device->keyboard->events.modifiers, &keyboard->modifiers);
  keyboard->key.notify = keyboard_handle_key;
  wl_signal_add(&device->keyboard->events.key, &keyboard->key);

  wlr_seat_set_keyboard(server->seat, device);

  /* And add the keyboard to our list of keyboards */
  wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_input(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new input device becomes
   * available. */
  struct dgde_server *server = wl_container_of(listener, server, new_input);
  struct wlr_input_device *device = data;
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    server_new_keyboard(server, device);
    break;
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

static void seat_request_cursor(struct wl_listener *listener, void *data) {
  struct dgde_server *server =
      wl_container_of(listener, server, request_cursor);
  /* This event is rasied by the seat when a client provides a cursor image */
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat_client *focused_client =
      server->seat->pointer_state.focused_client;
  /* This can be sent by any client, so we check to make sure this one is
   * actually has pointer focus first. */
  if (focused_client == event->seat_client) {
    /* Once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. It will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs. */
    dgde_cursor_set_surface(server->cursor, event);
  }
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

/* Used to move all of the data necessary to render a surface from the top-level
 * frame handler to the per-surface render function. */
struct render_data {
  struct wlr_output *output;
  struct wlr_renderer *renderer;
  struct dgde_view *view;
  struct timespec *when;
};

static void render_surface(struct wlr_surface *surface, int sx, int sy,
                           void *data) {
  /* This function is called for every surface that needs to be rendered. */
  struct render_data *rdata = data;
  struct dgde_view *view = rdata->view;
  struct wlr_output *output = rdata->output;

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
  wlr_output_layout_output_coords(view->server->output_layout, output, &ox,
                                  &oy);
  ox += view->x + sx, oy += view->y + sy;

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
  struct dgde_view *view;
  wl_list_for_each_reverse(view, &output->server->views, link) {
    if (!view->mapped) {
      /* An unmapped view should not be rendered. */
      continue;
    }
    struct render_data rdata = {
        .output = output->wlr_output,
        .view = view,
        .renderer = renderer,
        .when = &now,
    };
    /* This calls our render_surface function for each surface among the
     * xdg_surface's toplevel and popups. */
    wlr_xdg_surface_for_each_surface(view->xdg_surface, render_surface, &rdata);
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

static void server_new_output(struct wl_listener *listener, void *data) {
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

static void xdg_surface_map(struct wl_listener *listener, void *data) {
  /* Called when the surface is mapped, or ready to display on-screen. */
  struct dgde_view *view = wl_container_of(listener, view, map);
  view->mapped = true;
  focus_view(view, view->xdg_surface->surface);
}

static void xdg_surface_unmap(struct wl_listener *listener, void *data) {
  /* Called when the surface is unmapped, and should no longer be shown. */
  struct dgde_view *view = wl_container_of(listener, view, unmap);
  view->mapped = false;
}

static void xdg_surface_destroy(struct wl_listener *listener, void *data) {
  /* Called when the surface is destroyed and should never be shown again. */
  struct dgde_view *view = wl_container_of(listener, view, destroy);
  wl_list_remove(&view->link);
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
  begin_interactive(view, DgdeCursor_Move, 0);
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
  begin_interactive(view, DgdeCursor_Resize, event->edges);
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
  /* This event is raised when wlr_xdg_shell receives a new xdg surface from a
   * client, either a toplevel (application window) or popup. */
  struct dgde_server *server =
      wl_container_of(listener, server, new_xdg_surface);
  struct wlr_xdg_surface *xdg_surface = data;
  if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    return;
  }

  /* Allocate a tinywl_view for this surface */
  struct dgde_view *view = calloc(1, sizeof(struct dgde_view));
  view->server = server;
  view->xdg_surface = xdg_surface;

  /* Listen to the various events it can emit */
  view->map.notify = xdg_surface_map;
  wl_signal_add(&xdg_surface->events.map, &view->map);
  view->unmap.notify = xdg_surface_unmap;
  wl_signal_add(&xdg_surface->events.unmap, &view->unmap);
  view->destroy.notify = xdg_surface_destroy;
  wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

  /* cotd */
  struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;
  view->request_move.notify = xdg_toplevel_request_move;
  wl_signal_add(&toplevel->events.request_move, &view->request_move);
  view->request_resize.notify = xdg_toplevel_request_resize;
  wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

  /* Add it to the list of views. */
  wl_list_insert(&server->views, &view->link);
}

int main(int argc, char *argv[]) {
  wlr_log_init(WLR_DEBUG, NULL);
  char *startup_cmd = NULL;
  int c;
  while ((c = getopt(argc, argv, "s:h")) != -1) {
    switch (c) {
    case 's':
      startup_cmd = optarg;
      break;
    default:
      printf("Usage: %s [-s startup command]\n", argv[0]);
      return 0;
    }
  }
  if (optind < argc) {
    printf("Usage: %s [-s startup command]\n", argv[0]);
    return 0;
  }

  struct dgde_server server;
  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, manging Wayland globals, and so on. */
  server.wl_display = wl_display_create();

  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. */
  server.backend = wlr_backend_autocreate(server.wl_display);

  /* If we don't provide a renderer, autocreate makes a GLES2 renderer for us.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  server.renderer = wlr_backend_get_renderer(server.backend);
  wlr_renderer_init_wl_display(server.renderer, server.wl_display);

  /* This creates some hands-off wlroots interfaces. The compositor is
   * necessary for clients to allocate surfaces and the data device manager
   * handles the clipboard. Each of these wlroots interfaces has room for you
   * to dig your fingers in and play with their behavior if you want. Note that
   * the clients cannot set the selection directly without compositor approval,
   * see the handling of the request_set_selection event below.*/
  wlr_compositor_create(server.wl_display, server.renderer);
  wlr_data_device_manager_create(server.wl_display);

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  server.output_layout = wlr_output_layout_create();

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&server.outputs);
  server.new_output.notify = server_new_output;
  wl_signal_add(&server.backend->events.new_output, &server.new_output);

  /* Set up our list of views and the xdg-shell. The xdg-shell is a Wayland
   * protocol which is used for application windows. For more detail on
   * shells, refer to my article:
   *
   * https://drewdevault.com/2018/07/29/Wayland-shells.html
   */
  wl_list_init(&server.views);
  server.xdg_shell = wlr_xdg_shell_create(server.wl_display);
  server.new_xdg_surface.notify = server_new_xdg_surface;
  wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);

  // create a cursor
  struct dgde_cursor *cursor = dgde_cursor_create(server.output_layout);
  struct dgde_cursor_handler cursor_handler = {
      .button = (dgde_cursor_button_cb)process_cursor_button,
      .motion = (dgde_cursor_motion_cb)process_cursor_motion,
      .motion_absolute =
          (dgde_cursor_motion_absolute_cb)process_cursor_motion_absolute,
      .axis = (dgde_cursor_axis_cb)process_cursor_axis,
      .frame = (dgde_cursor_frame_cb)process_cursor_frame,
      .userdata = &server};
  dgde_cursor_add_handler(cursor, &cursor_handler);

  server.cursor = cursor;
  server.cursor_mode = DgdeCursor_Passthrough;

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&server.keyboards);
  server.new_input.notify = server_new_input;
  wl_signal_add(&server.backend->events.new_input, &server.new_input);
  server.seat = wlr_seat_create(server.wl_display, "seat0");
  server.request_cursor.notify = seat_request_cursor;
  wl_signal_add(&server.seat->events.request_set_cursor,
                &server.request_cursor);
  server.request_set_selection.notify = seat_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection,
                &server.request_set_selection);

  /* Add a Unix socket to the Wayland display. */
  const char *socket = wl_display_add_socket_auto(server.wl_display);
  if (!socket) {
    wlr_backend_destroy(server.backend);
    return 1;
  }

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if (!wlr_backend_start(server.backend)) {
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 1;
  }

  /* Set the WAYLAND_DISPLAY environment variable to our socket and run the
   * startup command if requested. */
  setenv("WAYLAND_DISPLAY", socket, true);
  if (startup_cmd) {
    if (fork() == 0) {
      execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
    }
  }
  /* Run the Wayland event loop. This does not return until you exit the
   * compositor. Starting the backend rigged up all of the necessary event
   * loop configuration to listen to libinput events, DRM events, generate
   * frame events at the refresh rate, and so on. */
  wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
  wl_display_run(server.wl_display);

  /* Once wl_display_run returns, we shut down the server. */
  dgde_cursor_destroy(server.cursor);
  wl_display_destroy_clients(server.wl_display);
  wl_display_destroy(server.wl_display);
  return 0;
}