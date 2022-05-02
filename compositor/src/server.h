#ifndef SERVER_H
#define SERVER_H

#include "cursor.h"
#include "wayland-util.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>

struct dgde_server;

struct dgde_server *dgde_server_create(const char *seat_name);
const char *dgde_server_attach_socket(struct dgde_server *server);
void dgde_server_run(struct dgde_server *server);
void dgde_server_destroy(struct dgde_server *server);

#endif
