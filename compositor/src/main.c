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

  struct dgde_server *server = dgde_server_create("seat0");
  const char *socket = dgde_server_attach_socket(server);
  if (!socket) {
    fprintf(stderr, "failed to create Wayland socket\n");
    dgde_server_destroy(server);
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

  wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
  dgde_server_run(server);

  wlr_log(WLR_INFO, "Shutting down dgde compositor...\n");
  dgde_server_destroy(server);
  return 0;
}
