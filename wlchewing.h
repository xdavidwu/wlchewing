#ifndef WLCHEWING_H
#define WLCHEWING_H

#include <chewing.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "bottom-panel.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

struct wlchewing_keysym {
	uint32_t key;
	struct wl_list link;
};

struct wlchewing_state {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;

	struct zwp_input_method_manager_v2 *input_method_manager;
	struct zwp_input_method_v2 *input_method;
	struct zwp_input_method_keyboard_grab_v2 *kb_grab;
	bool pending_activate;
	bool activated;

	struct zwp_virtual_keyboard_manager_v1 *virtual_keyboard_manager;
	struct zwp_virtual_keyboard_v1 *virtual_keyboard;

	struct zwlr_layer_shell_v1 *layer_shell;
	struct wlchewing_bottom_panel *bottom_panel;

	ChewingContext *chewing;
	bool forwarding;
	bool eng_shift;

	struct xkb_context *xkb_context;
	struct xkb_state *xkb_state;
	char *xkb_keymap_string;
	xkb_keysym_t last_keysym;
	int32_t kb_delay, kb_rate;
	int timer_fd;

	struct wl_list pending_handled_keysyms; // wlchewing_keysym
	struct wl_list press_sent_keysyms; // wlchewing_keysym
	int32_t millis_offset;

	int32_t serial;
};

void im_setup(struct wlchewing_state *state);

void im_destory(struct wlchewing_state *state);

bool im_key_press(struct wlchewing_state *state, xkb_keysym_t keysym);

#define wlchewing_err(fmt, ...) fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#endif
