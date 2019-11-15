#include <assert.h>

#include "wlchewing.h"
#include "bottom-panel.h"

static const struct wl_output_listener output_listener;

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct wlchewing_state *state = data;
	if (strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
		state->input_method_manager = wl_registry_bind(registry, name,
			&zwp_input_method_manager_v2_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *output = wl_registry_bind(registry, name,
			&wl_output_interface, 3);
		wl_output_add_listener(output, &output_listener, NULL);
	}
}

static void noop() {
	// no-op
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = noop,
};

static void output_scale(void *data, struct wl_output *output, int32_t scale) {
	int32_t *user_data = malloc(sizeof(int32_t));
	assert(user_data);
	*user_data = scale;
	wl_output_set_user_data(output, user_data);
}

static const struct wl_output_listener output_listener = {
	.scale = output_scale,
	.geometry = noop,
	.mode = noop,
	.done = noop
};

int main(int argc, char **argv) {
	struct wlchewing_state *state = calloc(1,
		sizeof(struct wlchewing_state));
	if (state == NULL) {
		wlchewing_err("Failed to calloc state");
		return EXIT_FAILURE;
	}
	state->display = wl_display_connect(NULL);
	if (state->display == NULL) {
		wlchewing_err("Failed to create display");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(state->display);
	wl_registry_add_listener(registry, &registry_listener, state);
	wl_display_dispatch(state->display);
	wl_display_roundtrip(state->display);

	if (state->layer_shell == NULL || state->shm == NULL
			|| state->compositor == NULL
			|| state->input_method_manager == NULL) {
		/* TODO for adding panel with input-method support
		 * currently only panel with wlr-layer-shell
		 */
		wlchewing_err("Required wayland interface not available");
		return EXIT_FAILURE;
	}

	im_setup(state);
	
	while (wl_display_dispatch(state->display) != -1) {
		// dispatch loop
	}
	return EXIT_SUCCESS;
}
