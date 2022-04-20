#include "wayland-client-core.h"
#include "wayland-egl-core.h"
#include <bits/getopt_core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-server.h>
#include <xdg-shell-protocol.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct window {
  struct wl_surface *surface;
  struct wl_compositor *compositor;

  struct xdg_wm_base *xdg_base;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  struct {
    struct wl_egl_window *window;
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
  } egl_context;
};

static void draw(struct window *window, uint32_t color) {

  EGLSurface egl_surface = window->egl_context.surface;
  EGLDisplay egl_display = window->egl_context.display;
  EGLContext egl_context = window->egl_context.context;
  if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
    fprintf(stderr, "Made current failed\n");
    return;
  }

  glClearColor((color >> 16) / 255.f, ((color >> 8) & 0xff) / 255.f,
               (color & 0xff) / 255.f, 1.f);

  glClear(GL_COLOR_BUFFER_BIT);
  glFlush();

  eglSwapBuffers(egl_display, egl_surface);
}

static void create_window(struct window *window) {

  window->egl_context.window = wl_egl_window_create(window->surface, 480, 360);
  if (window->egl_context.window == EGL_NO_SURFACE) {
    fprintf(stderr, "Failed to create EGL window\n");
    return;
  }

  window->egl_context.surface = eglCreateWindowSurface(
      window->egl_context.display, window->egl_context.config,
      window->egl_context.window, NULL);
}

static void init_egl(struct wl_display *display, struct window *window) {
  EGLint major, minor, count, n, size;
  EGLConfig *configs;
  int i;
  EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_ES2_BIT,
                             EGL_NONE};

  static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                           EGL_NONE};

  window->egl_context.display = eglGetDisplay((EGLNativeDisplayType)display);
  if (window->egl_context.display == EGL_NO_DISPLAY) {
    fprintf(stderr, "Can't create egl display\n");
    return;
  } else {
    fprintf(stderr, "Created egl display\n");
  }

  if (eglInitialize(window->egl_context.display, &major, &minor) != EGL_TRUE) {
    fprintf(stderr, "Can't initialise egl display\n");
    exit(1);
  }
  printf("EGL major: %d, minor %d\n", major, minor);

  eglGetConfigs(window->egl_context.display, NULL, 0, &count);
  printf("EGL has %d configs\n", count);

  configs = calloc(count, sizeof *configs);

  eglChooseConfig(window->egl_context.display, config_attribs, configs, count,
                  &n);

  for (i = 0; i < n; i++) {
    eglGetConfigAttrib(window->egl_context.display, configs[i], EGL_BUFFER_SIZE,
                       &size);
    printf("Buffer size for config %d is %d\n", i, size);
    eglGetConfigAttrib(window->egl_context.display, configs[i], EGL_RED_SIZE,
                       &size);
    printf("Red size for config %d is %d\n", i, size);

    // just choose the first one
    window->egl_context.config = configs[i];
    break;
  }

  window->egl_context.context =
      eglCreateContext(window->egl_context.display, window->egl_context.config,
                       EGL_NO_CONTEXT, context_attribs);
}

static void global_registry_handler(void *data, struct wl_registry *registry,
                                    uint32_t id, const char *interface,
                                    uint32_t version) {
  printf("Got a registry event for %s id %d\n", interface, id);
  struct window *window = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    window->compositor =
        wl_registry_bind(registry, id, &wl_compositor_interface, 1);
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    window->xdg_base =
        wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
  }
}

static void global_registry_remover(void *data, struct wl_registry *registry,
                                    uint32_t id) {
  printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler, global_registry_remover};

static void get_server_references(struct wl_display *display,
                                  struct window *window) {

  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, window);

  wl_display_roundtrip(display);

  if (window->compositor == NULL || window->xdg_base == NULL) {
    fprintf(stderr, "Can't find compositor or xdg-shell\n");
    exit(1);
  } else {
    fprintf(stderr, "Found compositor and shell\n");
  }
}

static void handle_ping(void *data, struct xdg_wm_base *xdg_base,
                        uint32_t serial) {
  xdg_wm_base_pong(xdg_base, serial);
}

static void handle_configure(void *data, struct xdg_toplevel *toplevel,
                             int32_t width, int32_t height,
                             struct wl_array *states) {
  if ((width == height) == 0) {
    return;
  }

  struct window *window = data;
  wl_egl_window_resize(window->egl_context.window, width, height, 0, 0);
}

static void handle_surface_configure(void *data, struct xdg_surface *surface,
                                     uint32_t serial) {
  xdg_surface_ack_configure(surface, serial);
}

static uint32_t color_from_str(const char *color) {
  if (strncmp(color, "green", 5) == 0) {
    return 0x00ff00;
  } else if (strncmp(color, "blue", 4) == 0) {
    return 0x0000ff;
  } else if (strncmp(color, "red", 3) == 0) {
    return 0xff0000;
  } else if (strncmp(color, "white", 3) == 0) {
    return 0xffffff;
  } else if (strncmp(color, "black", 3) == 0) {
    return 0x0; // NULL, is that you?
  } else {
    return strtol(color, NULL, 16);
  }
}

void print_usage(const char *program_name) {
  printf("usage: %s [-c color name [red, green, blue, black, white]]\n",
         program_name);
}

int main(int argc, char **argv) {

  uint32_t color = 0;
  int c;
  while ((c = getopt(argc, argv, "c:h")) != -1) {
    switch (c) {
    case 'c':
      color = color_from_str(optarg);
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (optind < argc) {
    printf("Unknown arguments\n");
    print_usage(argv[0]);
    return 1;
  }

  struct wl_display *display = wl_display_connect(NULL);
  if (display == NULL) {
    fprintf(stderr, "Can't connect to display\n");
    exit(1);
  }
  printf("connected to display\n");

  struct window window;
  get_server_references(display, &window);

  window.surface = wl_compositor_create_surface(window.compositor);
  if (window.surface == NULL) {
    fprintf(stderr, "Can't create surface\n");
    return 1;
  } else {
    fprintf(stderr, "Created surface\n");
  }

  struct xdg_wm_base_listener wm_base_listener = {.ping = handle_ping};
  xdg_wm_base_add_listener(window.xdg_base, &wm_base_listener, NULL);

  struct xdg_surface_listener xdg_surface_listener = {
      .configure = handle_surface_configure,
  };
  window.xdg_surface =
      xdg_wm_base_get_xdg_surface(window.xdg_base, window.surface);
  xdg_surface_add_listener(window.xdg_surface, &xdg_surface_listener, NULL);

  struct xdg_toplevel_listener xdg_toplevel_listener = {.configure =
                                                            handle_configure};
  window.xdg_toplevel = xdg_surface_get_toplevel(window.xdg_surface);
  xdg_toplevel_add_listener(window.xdg_toplevel, &xdg_toplevel_listener,
                            &window);

  xdg_toplevel_set_title(window.xdg_toplevel, "This is a color!");

  // recieve the configure events for the xdg surface
  wl_surface_commit(window.surface);
  wl_display_roundtrip(display);

  init_egl(display, &window);
  create_window(&window);

  draw(&window, color);
  while (wl_display_dispatch(display) != -1) {
    draw(&window, color);
  }

  wl_display_disconnect(display);
  printf("disconnected from display\n");

  exit(0);
}
