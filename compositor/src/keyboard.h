#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct dgde_keyboard;
struct wlr_seat;
struct wlr_input_device;
struct wl_list;

struct dgde_keyboard *dgde_keyboard_create(struct wlr_input_device *device,
                                           struct wlr_seat *seat);

typedef bool (*keybind_handler)(void *, xkb_keysym_t);
void dgde_keyboard_add_handler(struct dgde_keyboard *keyboard,
                               keybind_handler handler, void *userdata,
                               uint32_t modifiers);

void dgde_keyboard_add_to_list(struct dgde_keyboard *keyboard,
                               struct wl_list *list);

#endif
