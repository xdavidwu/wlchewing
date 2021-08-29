#ifndef WLCHEWING_H
#define WLCHEWING_H

#include <chewing.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "bottom-panel.h"
#include "config.h"
#include "sni.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

struct wlchewing_keysym {
	uint32_t key;
	struct wl_list link;
};

struct wlchewing_state {
	struct wlchewing_config *config;

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
	PangoLayout *bottom_panel_text_layout;
	uint32_t bottom_panel_text_height;
	struct wlchewing_buffer *bottom_panel_test_buffer;

	struct wlchewing_sni *sni;

	ChewingContext *chewing;
	bool forwarding;
	bool shift_only;

	struct xkb_context *xkb_context;
	struct xkb_state *xkb_state;
	char *xkb_keymap_string;
	uint32_t last_key;
	int32_t kb_delay, kb_rate;
	int timer_fd;

	struct wl_list pending_handled_keysyms; // wlchewing_keysym
	struct wl_list press_sent_keysyms; // wlchewing_keysym
	int32_t millis_offset;

	int32_t serial;
};

void im_setup(struct wlchewing_state *state);

void im_destory(struct wlchewing_state *state);

int im_key_press(struct wlchewing_state *state, uint32_t key);

void im_release_all_keys(struct wlchewing_state *state);

#define wlchewing_err(fmt, ...) fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define KEY_HANDLE_FORWARD	(1 << 0)
#define KEY_HANDLE_ARM_TIMER	(1 << 1)

#endif
