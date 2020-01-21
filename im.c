#include <errno.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "wlchewing.h"

static int32_t get_millis() {
	struct timespec spec;
	clock_gettime(CLOCK_MONOTONIC, &spec);
	return spec.tv_sec * 1000 + spec.tv_nsec / (1000 * 1000);
}

static void noop() {
	// no-op
}

bool im_key_press(struct wlchewing_state *state, xkb_keysym_t keysym) {
	// return false if unhandled, need forwarding
	if (xkb_state_mod_name_is_active(state->xkb_state, XKB_MOD_NAME_CTRL,
			XKB_STATE_MODS_EFFECTIVE) > 0) {
		if (keysym == XKB_KEY_space) {
			// Chinese / English(forwarding) toggle
			state->forwarding = !state->forwarding;
			chewing_Reset(state->chewing);
			if (state->bottom_panel) {
				bottom_panel_destroy(state->bottom_panel);
				state->bottom_panel = NULL;
			}
			zwp_input_method_v2_set_preedit_string(
				state->input_method, "", 0, 0);
			zwp_input_method_v2_commit(state->input_method,
				state->serial);
			wl_display_roundtrip(state->display);
			return true;
		}
		return false;
	}
	if (xkb_state_mod_name_is_active(state->xkb_state, XKB_MOD_NAME_ALT,
			XKB_STATE_MODS_EFFECTIVE) > 0 ||
			xkb_state_mod_name_is_active(state->xkb_state,
			XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
		// Alt and Logo are not used by us
		return false;
	}

	if (state->forwarding) {
		return false;
	}
	if (state->bottom_panel) {
		bool need_update = false;
		switch(keysym){
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			chewing_cand_choose_by_index(state->chewing,
				state->bottom_panel->selected_index);
			chewing_cand_close(state->chewing);
			bottom_panel_destroy(state->bottom_panel);
			state->bottom_panel = NULL;
			need_update = true;
			break;
		case XKB_KEY_Left:
		case XKB_KEY_KP_Left:
			if (state->bottom_panel->selected_index > 0) {
				state->bottom_panel->selected_index--;
				bottom_panel_render(state->bottom_panel,
					state->chewing);
				wl_display_roundtrip(state->display);
			}
			break;
		case XKB_KEY_Right:
		case XKB_KEY_KP_Right:
			if (state->bottom_panel->selected_index
					< chewing_cand_TotalChoice(
						state->chewing) - 1) {
				state->bottom_panel->selected_index++;
				bottom_panel_render(state->bottom_panel,
					state->chewing);
				wl_display_roundtrip(state->display);
			}
			break;
		case XKB_KEY_Up:
		case XKB_KEY_KP_Up:
			chewing_cand_close(state->chewing);
			bottom_panel_destroy(state->bottom_panel);
			state->bottom_panel = NULL;
			break;
		case XKB_KEY_Down:
		case XKB_KEY_KP_Down:
			if (chewing_cand_list_has_next(state->chewing)) {
				chewing_cand_list_next(state->chewing);
			} else {
				chewing_cand_list_first(state->chewing);
			}
			state->bottom_panel->selected_index = 0;
			bottom_panel_render(state->bottom_panel,
				state->chewing);
			wl_display_roundtrip(state->display);
			break;
		default:
			// no-op
			break;
		}
		if (!need_update) {
			// We grabs all the keys when panel is there,
			// as if it has the focus.
			return true;
		}
	} else {
		bool chewing_handled = true;
		switch(keysym){
		case XKB_KEY_BackSpace:
			chewing_handle_Backspace(state->chewing);
			break;
		case XKB_KEY_Delete:
		case XKB_KEY_KP_Delete:
			chewing_handle_Del(state->chewing);
			break;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			chewing_handle_Enter(state->chewing);
			break;
		case XKB_KEY_Left:
		case XKB_KEY_KP_Left:
			chewing_handle_Left(state->chewing);
			break;
		case XKB_KEY_Right:
		case XKB_KEY_KP_Right:
			chewing_handle_Right(state->chewing);
			break;
		case XKB_KEY_Down:
		case XKB_KEY_KP_Down:
			chewing_handled = false;
			chewing_cand_open(state->chewing);
			if (chewing_cand_TotalChoice(state->chewing)) {
				state->bottom_panel = bottom_panel_new(state);
				bottom_panel_render(state->bottom_panel,
					state->chewing);
				wl_display_roundtrip(state->display);
				return true;
			}
			break;
		default:
			chewing_handle_Default(state->chewing,
				(char)xkb_keysym_to_utf32(keysym));
		}
		if (!chewing_handled ||
				chewing_keystroke_CheckIgnore(state->chewing)) {
			return false;
		}
	}

	const char *precommit = chewing_buffer_String_static(state->chewing);
	const char *bopomofo = chewing_bopomofo_String_static(state->chewing);
	int chewing_cursor = chewing_cursor_Current(state->chewing);
	// chewing_cursor here is counted in utf-8 characters, not in bytes
	int byte_cursor = 0;
	for (int i = 0; i < chewing_cursor ; i++) {
		uint8_t byte = precommit[byte_cursor];
		if (!(byte & 0x80)) {
			byte_cursor += 1;
		}
		else {
			while (byte & 0x80) {
				byte <<= 1;
				byte_cursor++;
			}
		}
	}
	char *preedit = calloc(strlen(precommit) + strlen(bopomofo) + 1,
		sizeof(char));
	strncat(preedit, precommit, byte_cursor);
	strcat(preedit, bopomofo);
	strcat(preedit, &precommit[byte_cursor]);
	zwp_input_method_v2_set_preedit_string(state->input_method, preedit,
		byte_cursor, byte_cursor + strlen(bopomofo));
	free(preedit);
	if (chewing_commit_Check(state->chewing)) {
		zwp_input_method_v2_commit_string(state->input_method, 
			chewing_commit_String_static(state->chewing));
	}
	zwp_input_method_v2_commit(state->input_method, state->serial);
	wl_display_roundtrip(state->display);
	return true;
}

static void handle_key(void *data, struct zwp_input_method_keyboard_grab_v2
		*zwp_input_method_keyboard_grab_v2, uint32_t serial,
		uint32_t time, uint32_t key, uint32_t key_state) {
	struct wlchewing_state *state = data;
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(state->xkb_state,
		key + 8);
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (!im_key_press(state, keysym)){
			zwp_virtual_keyboard_v1_key(state->virtual_keyboard,
				time, key, key_state);
			// record press sent keys,
			// to pop pending release on deactivate
			struct wlchewing_keysym *newkey = calloc(1,
				sizeof(struct wlchewing_keysym));
			newkey->key = key;
			wl_list_insert(&state->press_sent_keysyms,
				&newkey->link);
			// update the millis offset so we can make it more
			// accurate when we pop pending release
			state->millis_offset = get_millis() - time;
			wl_display_roundtrip(state->display);
		} else {
			// record that we should not forward key release
			struct wlchewing_keysym *newkey = calloc(1,
				sizeof(struct wlchewing_keysym));
			newkey->key = key;
			wl_list_insert(&state->pending_handled_keysyms,
				&newkey->link);
			if (state->kb_rate != 0) {
				state->last_keysym = keysym;
				struct itimerspec timer_spec = {
					.it_interval = {
						.tv_sec = 0,
						.tv_nsec = 1000 * 1000 * 1000 /
							state->kb_rate,
					},
					.it_value = {
						.tv_sec = 0,
						.tv_nsec = state->kb_delay *
							1000 * 1000,
					},
				};
				if (timerfd_settime(state->timer_fd, 0,
						&timer_spec, NULL) == -1) {
					wlchewing_err("Failed to arm timer: %s",
						strerror(errno));
				}
			}
		}
	} else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		// find if we should not forward key release
		struct wlchewing_keysym *mkeysym, *tmp;
		wl_list_for_each_safe(mkeysym, tmp,
				&state->pending_handled_keysyms, link) {
			if (mkeysym->key == key) {
				wl_list_remove(&mkeysym->link);
				free(mkeysym);
				if (keysym == state->last_keysym) {
					state->last_keysym = 0;
					struct itimerspec timer_disarm = {0};
					if (timerfd_settime(state->timer_fd, 0,
							&timer_disarm,
							NULL) == -1) {
						wlchewing_err(
							"Failed to disarm timer: %s",
							strerror(errno));
					}
				}
				return;
			}
		}
		zwp_virtual_keyboard_v1_key(state->virtual_keyboard, time, key,
			key_state);
		wl_list_for_each_safe(mkeysym, tmp,
				&state->press_sent_keysyms, link) {
			if (mkeysym->key == key) {
				wl_list_remove(&mkeysym->link);
				free(mkeysym);
				break;
			}
		}
		wl_display_roundtrip(state->display);
	}
}

static void handle_modifiers(void *data, struct zwp_input_method_keyboard_grab_v2
		*zwp_input_method_keyboard_grab_v2, uint32_t serial,
		uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	struct wlchewing_state *state = data;
	xkb_state_update_mask(state->xkb_state, mods_depressed, mods_latched,
		mods_locked, 0, 0, group);
	// forward modifiers
	zwp_virtual_keyboard_v1_modifiers(state->virtual_keyboard,
		mods_depressed, mods_latched, mods_locked, group);
}

static void handle_keymap(void *data, struct zwp_input_method_keyboard_grab_v2
		*zwp_input_method_keyboard_grab_v2, uint32_t format,
		int32_t fd, uint32_t size) {
	struct wlchewing_state *state = data;
	char *keymap_string = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (state->xkb_keymap_string == NULL ||
			strcmp(state->xkb_keymap_string, keymap_string) != 0) {
		struct xkb_keymap *keymap = xkb_keymap_new_from_string(
			state->xkb_context, keymap_string,
			XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
		xkb_state_unref(state->xkb_state);
		state->xkb_state = xkb_state_new(keymap);
		free(state->xkb_keymap_string);
		state->xkb_keymap_string = strdup(keymap_string);
		xkb_keymap_unref(keymap);
		// forward keymap
		zwp_virtual_keyboard_v1_keymap(state->virtual_keyboard,
			format, fd, size);
	}
	munmap(keymap_string, size);
}

static void handle_repeat_info(void *data,
		struct zwp_input_method_keyboard_grab_v2
		*zwp_input_method_keyboard_grab_v2, int32_t rate,
		int32_t delay) {
	struct wlchewing_state *state = data;
	state->kb_delay = delay;
	state->kb_rate = rate;
}

static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
	.key = handle_key,
	.modifiers = handle_modifiers,
	.keymap = handle_keymap,
	.repeat_info = handle_repeat_info,
};

static void handle_activate(void *data,
		struct zwp_input_method_v2 *zwp_input_method_v2) {
	struct wlchewing_state *state = data;
	state->pending_activate = true;
}

static void handle_deactivate(void *data,
		struct zwp_input_method_v2 *zwp_input_method_v2) {
	struct wlchewing_state *state = data;
	state->pending_activate = false;
}

static void handle_unavailable(void *data,
		struct zwp_input_method_v2 *zwp_input_method_v2) {
	struct wlchewing_state *state = data;
	wlchewing_err("IM unavailable");
	im_destory(state);
	exit(EXIT_FAILURE);
}

static void handle_done(void *data,
		struct zwp_input_method_v2 *zwp_input_method_v2) {
	struct wlchewing_state *state = data;
	state->serial++;
	if (state->pending_activate && !state->activated) {
		state->kb_grab = zwp_input_method_v2_grab_keyboard(
			state->input_method);
		zwp_input_method_keyboard_grab_v2_add_listener(state->kb_grab,
			&grab_listener, state);
		state->activated = state->pending_activate;

		if (!state->kb_grab) {
			wlchewing_err("Failed to grab");
			exit(EXIT_FAILURE);
		}
	} else if (!state->pending_activate && state->activated) {
		zwp_input_method_keyboard_grab_v2_release(state->kb_grab);
		state->kb_grab = NULL;
		if (state->bottom_panel) {
			bottom_panel_destroy(state->bottom_panel);
			state->bottom_panel = NULL;
		}
		chewing_Reset(state->chewing);

		struct wlchewing_keysym *mkeysym, *tmp;
		wl_list_for_each_safe(mkeysym, tmp,
				&state->press_sent_keysyms, link) {
			zwp_virtual_keyboard_v1_key(state->virtual_keyboard,
				get_millis() - state->millis_offset,
				mkeysym->key, WL_KEYBOARD_KEY_STATE_RELEASED);
			wl_list_remove(&mkeysym->link);
			free(mkeysym);
		}
	}
	state->activated = state->pending_activate;
	wl_display_roundtrip(state->display);
}

static const struct zwp_input_method_v2_listener im_listener = {
	.activate = handle_activate,
	.deactivate = handle_deactivate,
	.surrounding_text = noop,
	.text_change_cause = noop,
	.content_type = noop,
	.done = handle_done,
	.unavailable = handle_unavailable,
};

/* TODO for adding panel with input-method support
 * currently only panel with wlr-layer-shell
 */

void im_setup(struct wlchewing_state *state) {
	state->input_method = zwp_input_method_manager_v2_get_input_method(
		state->input_method_manager, state->seat);
	zwp_input_method_v2_add_listener(state->input_method, &im_listener, state);

	state->virtual_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
		state->virtual_keyboard_manager, state->seat);

	state->chewing = chewing_new();
	state->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	wl_list_init(&state->pending_handled_keysyms);
	wl_list_init(&state->press_sent_keysyms);

	wl_display_roundtrip(state->display);
}

void im_destory(struct wlchewing_state *state) {
	chewing_delete(state->chewing);
	xkb_state_unref(state->xkb_state);
	xkb_context_unref(state->xkb_context);
	zwp_input_method_v2_destroy(state->input_method);
	if (state->bottom_panel) {
		bottom_panel_destroy(state->bottom_panel);
		state->bottom_panel = NULL;
	}
}
