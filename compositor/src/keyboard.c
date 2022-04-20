#include "keyboard.h"
#include "wayland-util.h"

#include <stdint.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>

#define MAX_HANDLERS 16

struct dgde_keyboard {
  struct wlr_seat *seat;
  struct wlr_input_device *device;

  struct wl_listener modifiers;
  struct wl_listener key;

  struct wl_list link;

  keybind_handler handler_functions[MAX_HANDLERS];
  void *handler_userdatas[MAX_HANDLERS];
  uint32_t handler_modifiers[MAX_HANDLERS];
  uint32_t num_handlers;
};

static void handle_modifiers(struct wl_listener *listener, void *data) {
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
  wlr_seat_set_keyboard(keyboard->seat, keyboard->device);
  /* Send modifiers to the client. */
  wlr_seat_keyboard_notify_modifiers(keyboard->seat,
                                     &keyboard->device->keyboard->modifiers);
}

static void handle_key(struct wl_listener *listener, void *data) {
  /* This event is raised when a key is pressed or released. */
  struct dgde_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct wlr_event_keyboard_key *event = data;
  struct wlr_seat *seat = keyboard->seat;

  /* Translate libinput keycode -> xkbcommon */
  uint32_t keycode = event->keycode + 8;
  /* Get a list of keysyms based on the keymap for this keyboard */
  const xkb_keysym_t *syms;
  int nsyms = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
                                     keycode, &syms);

  bool handled = false;
  uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
  for (uint32_t h = 0, e = keyboard->num_handlers; h < e && !handled; ++h) {
    uint32_t wanted_modifiers = keyboard->handler_modifiers[h];
    if ((modifiers & wanted_modifiers) &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      keybind_handler handler_function = keyboard->handler_functions[h];
      void *userdata = keyboard->handler_userdatas[h];

      for (int i = 0; i < nsyms; i++) {
        handled = handler_function(userdata, syms[i]);
      }
    }
  }

  if (!handled) {
    /* Otherwise, we pass it along to the client. */
    wlr_seat_set_keyboard(seat, keyboard->device);
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                 event->state);
  }
}

struct dgde_keyboard *dgde_keyboard_create(struct wlr_input_device *device,
                                           struct wlr_seat *seat) {
  struct dgde_keyboard *keyboard = calloc(1, sizeof(struct dgde_keyboard));
  keyboard->device = device;
  keyboard->seat = seat;

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
  keyboard->modifiers.notify = handle_modifiers;
  wl_signal_add(&device->keyboard->events.modifiers, &keyboard->modifiers);
  keyboard->key.notify = handle_key;
  wl_signal_add(&device->keyboard->events.key, &keyboard->key);

  wlr_seat_set_keyboard(keyboard->seat, device);

  return keyboard;
}

void dgde_keyboard_add_to_list(struct dgde_keyboard *keyboard,
                               struct wl_list *list) {
  wl_list_insert(list, &keyboard->link);
}

void dgde_keyboard_add_handler(struct dgde_keyboard *keyboard,
                               keybind_handler handler, void *userdata,
                               uint32_t modifiers) {
  if (keyboard->num_handlers < MAX_HANDLERS) {
    keyboard->handler_functions[keyboard->num_handlers] = handler;
    keyboard->handler_userdatas[keyboard->num_handlers] = userdata;
    keyboard->handler_modifiers[keyboard->num_handlers] = modifiers;

    ++keyboard->num_handlers;
  }
}
