#define _POSIX_C_SOURCE 200112L
#include "cursor.h"
#include "keyboard.h"
#include "server.h"
#include "view.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <wlr/util/log.h>

int main(int argc, char *argv[]) {
  wlr_log_init(WLR_DEBUG, NULL);
  struct server *server = server_create("seat0");
  const char *socket = server_attach_socket(server);
  if (!socket) {
    fprintf(stderr, "failed to create Wayland socket\n");
    server_destroy(server);
    return 1;
  }

  setenv("WAYLAND_DISPLAY", socket, true);
  wlr_log(WLR_INFO, "Running dgde compositor on WAYLAND_DISPLAY=%s", socket);

  server_run(server);

  wlr_log(WLR_INFO, "Shutting down dgde compositor...\n");
  server_destroy(server);
  return 0;
}
