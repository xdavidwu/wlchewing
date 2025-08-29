#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "wlchewing.h"
#include "xmem.h"

static const struct itimerspec timer_disarm = {0};

static inline int32_t get_millis() {
	struct timespec spec;
	clock_gettime(CLOCK_MONOTONIC, &spec);
	return spec.tv_sec * 1000 + spec.tv_nsec / (1000 * 1000);
}

static void vte_hack(struct wlchewing_state *state);

static int count_utf8_bytes(const char *s, int codepoints) {
	int byte_cursor = 0;
	for (int i = 0; i < codepoints; i++) {
		uint8_t byte = s[byte_cursor];
		if (!(byte & 0x80)) {
			byte_cursor += 1;
		} else {
			while (byte & 0x80) {
				byte <<= 1;
				byte_cursor++;
			}
		}
	}
	return byte_cursor;
}

static void im_update(struct wlchewing_state *state) {
	const char *precommit = chewing_buffer_String_static(state->chewing);
	const char *bopomofo = chewing_bopomofo_String_static(state->chewing);

	int cursor = count_utf8_bytes(precommit,
		chewing_cursor_Current(state->chewing));
	int bopomofo_length = strlen(bopomofo);
	int preedit_length = strlen(precommit) + bopomofo_length;
	char *preedit = xcalloc(preedit_length + 1, sizeof(char));
	strncat(preedit, precommit, cursor);
	strcat(preedit, bopomofo);
	strcat(preedit, &precommit[cursor]);
	zwp_input_method_v2_set_preedit_string(state->input_method, preedit,
		cursor, cursor + bopomofo_length);
	free(preedit);

	if (chewing_commit_Check(state->chewing)) {
		zwp_input_method_v2_commit_string(state->input_method, 
			chewing_commit_String_static(state->chewing));
		chewing_ack(state->chewing);
	}

	zwp_input_method_v2_commit(state->input_method, state->serial);
	wl_display_roundtrip(state->display);

	if (!preedit_length) {
		vte_hack(state);
	}
}

void im_commit_candidate(struct wlchewing_state *state, int offset) {
	if (!state->bottom_panel) {
		return;
	}
	int index = state->bottom_panel->selected_index + offset;
	if (index >= chewing_cand_TotalChoice(state->chewing)) {
		return;
	}
	chewing_cand_choose_by_index(state->chewing, index);
	chewing_cand_close(state->chewing);
	bottom_panel_destroy(state->bottom_panel);
	state->bottom_panel = NULL;
	im_update(state);
	return;
}

void im_candidates_move_by(struct wlchewing_state *state, int diff) {
	if (!state->bottom_panel) {
		return;
	}
	int to = state->bottom_panel->selected_index + diff;
	if (to < 0) {
		to = 0;
	} else {
		int max = chewing_cand_TotalChoice(state->chewing) - 1;
		if (to > max) {
			to = max;
		}
	}
	if (state->bottom_panel->selected_index != to) {
		state->bottom_panel->selected_index = to;
		bottom_panel_render(state);
	}
}

void im_reset(struct wlchewing_state *state) {
	if (state->bottom_panel) {
		bottom_panel_destroy(state->bottom_panel);
		state->bottom_panel = NULL;
	}
	chewing_Reset(state->chewing);
}

void im_mode_switch(struct wlchewing_state *state, bool forwarding) {
	if (state->forwarding == forwarding) {
		return;
	}
	if (forwarding) {
		// toggling to English, do commit and reset
		if (chewing_buffer_Check(state->chewing)) {
			chewing_commit_preedit_buf(state->chewing);
			zwp_input_method_v2_commit_string(state->input_method,
				chewing_commit_String_static(state->chewing));
			chewing_ack(state->chewing);
		}
		im_reset(state);
		zwp_input_method_v2_set_preedit_string(state->input_method, "",
			0, 0);
		zwp_input_method_v2_commit(state->input_method, state->serial);
		wl_display_roundtrip(state->display);
		vte_hack(state);
	}
	state->forwarding = forwarding;
	sni_notify_new_icon(state->sni);
}

enum press_action im_key_press(struct wlchewing_state *state, uint32_t key) {
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(state->xkb_state,
		key + 8);

	if (xkb_state_mod_name_is_active(state->xkb_state, XKB_MOD_NAME_CTRL,
			XKB_STATE_MODS_EFFECTIVE) > 0) {
		if (keysym == XKB_KEY_space) {
			im_mode_switch(state, !state->forwarding);
			return PRESS_ARM_TIMER;
		}
		return PRESS_FORWARD;
	}
	if (xkb_state_mod_name_is_active(state->xkb_state, XKB_MOD_NAME_ALT,
			XKB_STATE_MODS_EFFECTIVE) > 0 ||
			xkb_state_mod_name_is_active(state->xkb_state,
			XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
		// Alt and Logo are not used by us
		return PRESS_FORWARD;
	}
	state->shift_only = keysym == XKB_KEY_Shift_L ||
		keysym == XKB_KEY_Shift_R;

	if (state->forwarding) {
		return PRESS_FORWARD;
	}

	if (state->bottom_panel) {
		switch (keysym) {
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			im_commit_candidate(state, 0);
			break;
		case XKB_KEY_1 ... XKB_KEY_9:
			im_commit_candidate(state, keysym - XKB_KEY_1);
			break;
		case XKB_KEY_KP_1 ... XKB_KEY_KP_9:
			im_commit_candidate(state, keysym - XKB_KEY_KP_1);
			break;
		case XKB_KEY_0:
		case XKB_KEY_KP_0:
			im_commit_candidate(state, 9);
			break;
		case XKB_KEY_Left:
		case XKB_KEY_KP_Left:
			im_candidates_move_by(state, -1);
			break;
		case XKB_KEY_Right:
		case XKB_KEY_KP_Right:
			im_candidates_move_by(state, 1);
			break;
		case XKB_KEY_Page_Up:
		case XKB_KEY_KP_Page_Up:
			im_candidates_move_by(state, -10);
			break;
		case XKB_KEY_Page_Down:
		case XKB_KEY_KP_Page_Down:
			im_candidates_move_by(state, 10);
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
			bottom_panel_render(state);
			break;
		default:
			// no-op
			break;
		}
		// We grabs all the keys when panel is there,
		// as if it has the focus.
		return PRESS_ARM_TIMER;
	}

	bool handled = true;
	switch (keysym) {
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
	case XKB_KEY_Home:
		chewing_handle_Home(state->chewing);
		break;
	case XKB_KEY_End:
		chewing_handle_End(state->chewing);
		break;
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		chewing_cand_open(state->chewing);
		if (chewing_cand_TotalChoice(state->chewing)) {
			state->bottom_panel = bottom_panel_new(state);
			bottom_panel_render(state);
			return PRESS_ARM_TIMER;
		}
		chewing_cand_close(state->chewing);
		handled = false;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		// consume if dirty
		handled = chewing_buffer_Check(state->chewing) ||
			chewing_bopomofo_Check(state->chewing);
		break;
	default:
		// printable characters
		if (keysym >= XKB_KEY_space &&
				keysym <= XKB_KEY_asciitilde) {
			chewing_handle_Default(state->chewing,
				(char)xkb_keysym_to_utf32(keysym));
		}
	}
	if (!handled || chewing_keystroke_CheckIgnore(state->chewing)) {
		return PRESS_FORWARD;
	}

	im_update(state);
	return PRESS_ARM_TIMER;
}

static void keyboard_grab_key(void *data,
		struct zwp_input_method_keyboard_grab_v2 *keyboard_grab,
		uint32_t serial, uint32_t time,
		uint32_t key, uint32_t key_state) {
	struct wlchewing_state *state = data;
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		struct wlchewing_keysym *newkey;
		switch (im_key_press(state, key)) {
		case PRESS_FORWARD:
			zwp_virtual_keyboard_v1_key(state->virtual_keyboard,
				time, key, key_state);
			// record press sent keys,
			// to pop pending release on deactivate
			newkey = xcalloc(1, sizeof(struct wlchewing_keysym));
			newkey->key = key;
			wl_list_insert(&state->press_sent_keysyms,
				&newkey->link);
			// update translation of our clock to keyboard_grab
			state->millis_offset = get_millis() - time;
			wl_display_roundtrip(state->display);
			break;
		case PRESS_ARM_TIMER:
			// record that we should not forward key release
			newkey = xcalloc(1, sizeof(struct wlchewing_keysym));
			newkey->key = key;
			wl_list_insert(&state->pending_handled_keysyms,
				&newkey->link);
			if (state->repeat_info.it_interval.tv_nsec != 0) {
				state->last_key = key;
				if (timerfd_settime(state->timerfd, 0,
						&state->repeat_info, NULL) == -1) {
					wlchewing_perr("Failed to arm timer");
				}
			}
			break;
		case PRESS_CONSUME:
		}
	} else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		xkb_keysym_t keysym = xkb_state_key_get_one_sym(
				state->xkb_state, key + 8);
		if ((keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R) &&
				state->shift_only) {
			state->shift_only = false;
			im_mode_switch(state, !state->forwarding);
			return;
		}

		// find if we should not forward key release
		struct wlchewing_keysym *mkeysym, *tmp;
		wl_list_for_each_safe(mkeysym, tmp,
				&state->pending_handled_keysyms, link) {
			if (mkeysym->key == key) {
				wl_list_remove(&mkeysym->link);
				free(mkeysym);
				if (key == state->last_key) {
					state->last_key = 0;
					if (timerfd_settime(state->timerfd, 0,
							&timer_disarm,
							NULL) == -1) {
						wlchewing_perr(
							"Failed to disarm timer");
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
			}
		}
		wl_display_roundtrip(state->display);
	}
}

static void keyboard_grab_modifiers(void *data,
		struct zwp_input_method_keyboard_grab_v2 *keyboard_grab,
		uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	struct wlchewing_state *state = data;
	xkb_state_update_mask(state->xkb_state, mods_depressed, mods_latched,
		mods_locked, 0, 0, group);
	// forward modifiers
	zwp_virtual_keyboard_v1_modifiers(state->virtual_keyboard,
		mods_depressed, mods_latched, mods_locked, group);
	wl_display_roundtrip(state->display);
}

static void keyboard_grab_keymap(void *data,
		struct zwp_input_method_keyboard_grab_v2 *keyboard_grab,
		uint32_t format, int32_t fd, uint32_t size) {
	struct wlchewing_state *state = data;
	char *keymap = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (state->keymap == NULL || state->keymap_size != size ||
			strncmp(state->keymap, keymap, size) != 0) {
		if (!state->config.chewing_use_xkb_default) {
			struct xkb_keymap *xkb_keymap =
				xkb_keymap_new_from_buffer(state->xkb_context,
					keymap, size, XKB_KEYMAP_FORMAT_TEXT_V1,
					XKB_KEYMAP_COMPILE_NO_FLAGS);
			xkb_state_unref(state->xkb_state);
			state->xkb_state = xkb_state_new(xkb_keymap);
			xkb_keymap_unref(xkb_keymap);
		}
		munmap(state->keymap, state->keymap_size);
		state->keymap = keymap;
		state->keymap_size = size;
		// forward keymap
		zwp_virtual_keyboard_v1_keymap(state->virtual_keyboard,
			format, fd, size);
		wl_display_roundtrip(state->display);
	}
	close(fd);
}

static void keyboard_grab_repeat_info(void *data,
		struct zwp_input_method_keyboard_grab_v2 *keyboard_grab,
		int32_t rate, int32_t delay) {
	struct wlchewing_state *state = data;
	state->repeat_info = (struct itimerspec) {
		.it_interval = {
			.tv_nsec = rate ? 1000 * 1000 * 1000 / rate : 0, 
		},
		.it_value = {
			.tv_sec = delay / 1000,
			.tv_nsec = (delay % 1000) * 1000 * 1000,
		},
	};
}

static const struct zwp_input_method_keyboard_grab_v2_listener
		keyboard_grab_listener = {
	.key		= keyboard_grab_key,
	.modifiers	= keyboard_grab_modifiers,
	.keymap		= keyboard_grab_keymap,
	.repeat_info	= keyboard_grab_repeat_info,
};

static void input_method_activate(void *data,
		struct zwp_input_method_v2 *input_method) {
	struct wlchewing_state *state = data;
	state->pending_activate = true;
}

static void input_method_deactivate(void *data,
		struct zwp_input_method_v2 *input_method) {
	struct wlchewing_state *state = data;
	state->pending_activate = false;
}

static void input_method_unavailable(void *data,
		struct zwp_input_method_v2 *input_method) {
	struct wlchewing_state *state = data;
	wlchewing_err("IM unavailable");
	im_destory(state);
	exit(EXIT_FAILURE);
}

static void input_method_done(void *data,
		struct zwp_input_method_v2 *input_method) {
	struct wlchewing_state *state = data;
	state->serial++;
	if (state->pending_activate && !state->activated) {
		state->keyboard_grab = zwp_input_method_v2_grab_keyboard(
			state->input_method);
		// sanity check if compositor doesn't really impl it
		if (!state->keyboard_grab) {
			wlchewing_err("Failed to grab");
			exit(EXIT_FAILURE);
		}
		zwp_input_method_keyboard_grab_v2_add_listener(
			state->keyboard_grab, &keyboard_grab_listener, state);
	} else if (!state->pending_activate && state->activated) {
		zwp_input_method_keyboard_grab_v2_release(state->keyboard_grab);
		state->keyboard_grab = NULL;
		im_reset(state);
		im_release_all_keys(state);
	}
	state->activated = state->pending_activate;
	wl_display_roundtrip(state->display);
}

static const struct zwp_input_method_v2_listener input_method_listener = {
	.activate		= input_method_activate,
	.deactivate		= input_method_deactivate,
	.surrounding_text	=
		(typeof(input_method_listener.surrounding_text))noop,
	.text_change_cause	=
		(typeof(input_method_listener.text_change_cause))noop,
	.content_type		=
		(typeof(input_method_listener.content_type))noop,
	.done			= input_method_done,
	.unavailable		= input_method_unavailable,
};

void im_release_all_keys(struct wlchewing_state *state) {
	struct wlchewing_keysym *mkeysym, *tmp;
	wl_list_for_each_safe(mkeysym, tmp, &state->press_sent_keysyms, link) {
		zwp_virtual_keyboard_v1_key(state->virtual_keyboard,
			get_millis() - state->millis_offset,
			mkeysym->key, WL_KEYBOARD_KEY_STATE_RELEASED);
		wl_list_remove(&mkeysym->link);
		free(mkeysym);
	}
}

void im_setup(struct wlchewing_state *state) {
	state->forwarding = state->config.start_eng;
	sni_notify_new_icon(state->sni);

	state->input_method = zwp_input_method_manager_v2_get_input_method(
		state->wl_globals.input_method_manager, state->wl_globals.seat);
	zwp_input_method_v2_add_listener(state->input_method,
		&input_method_listener, state);

	state->virtual_keyboard =
		zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
			state->wl_globals.virtual_keyboard_manager,
			state->wl_globals.seat);

	state->chewing = chewing_new();
	state->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(
		state->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	state->xkb_state = xkb_state_new(keymap);
	xkb_keymap_unref(keymap);
	wl_list_init(&state->pending_handled_keysyms);
	wl_list_init(&state->press_sent_keysyms);

	wl_display_roundtrip(state->display);

	bottom_panel_init(state);
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

static void vte_hack(struct wlchewing_state *state) {
	zwp_input_method_v2_destroy(state->input_method);
	state->input_method = zwp_input_method_manager_v2_get_input_method(
		state->wl_globals.input_method_manager, state->wl_globals.seat);
	state->serial = 0;
	zwp_input_method_v2_add_listener(state->input_method,
		&input_method_listener, state);
	wl_display_roundtrip(state->display);
}
