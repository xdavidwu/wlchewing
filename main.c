#include <assert.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "bottom-panel.h"
#include "errors.h"
#include "sni.h"
#include "wlchewing.h"
#include "xmem.h"

struct wlchewing_state global_state = {0};

struct global_map_el {
	const struct wl_interface *interface;
	uint32_t version;
	void **dest;
} globals[] = {
	{
		&wl_compositor_interface, 4,
		(void **)&global_state.wl_globals.compositor,
	},
	{
		&wl_shm_interface, 1,
		(void **)&global_state.wl_globals.shm,
	},
	{
		&wl_seat_interface, 5,
		(void **)&global_state.wl_globals.seat,
	},
	{
		&zwp_input_method_manager_v2_interface, 1,
		(void **)&global_state.wl_globals.input_method_manager,
	},
	{
		&zwp_virtual_keyboard_manager_v1_interface, 1,
		(void **)&global_state.wl_globals.virtual_keyboard_manager,
	},
	{
		&zwlr_layer_shell_v1_interface, 1,
		(void **)&global_state.wl_globals.layer_shell,
	},
	{NULL},
};

static void handle_signal(int signo) {
	im_release_all_keys(&global_state);
	wl_display_roundtrip(global_state.display);
	raise(signo);
}

static const struct wl_output_listener output_listener;

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct global_map_el *el = globals;
	while (el->interface != NULL) {
		if (strcmp(interface, el->interface->name) == 0) {
			*el->dest = wl_registry_bind(
				registry, name, el->interface,
				el->version ? el->version : version);
			return;
		}
		el++;
	}
	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *output = wl_registry_bind(registry, name,
			&wl_output_interface, 3);
		wl_output_add_listener(output, &output_listener, NULL);
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = (typeof(registry_listener.global_remove))noop,
};

static void output_scale(void *data, struct wl_output *output, int32_t scale) {
	int32_t *user_data = xcalloc(1, sizeof(int32_t));
	*user_data = scale;
	wl_output_set_user_data(output, user_data);
}

static const struct wl_output_listener output_listener = {
	.scale		= output_scale,
	.geometry	= (typeof(output_listener.geometry))noop,
	.mode		= (typeof(output_listener.mode))noop,
	.done		= (typeof(output_listener.done))noop
};

static void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete) {
	struct wlchewing_state *state = data;
	state->has_discrete = true;
	im_candidates_move_by(state, discrete);
}

static void pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (button == BTN_MIDDLE || button == BTN_RIGHT) {
			im_commit_candidate(data, 0);
		}
	}
}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	if (axis < 2) {
		struct wlchewing_state *state = data;
		state->pending_axis[axis] = value;
	}
}

static double pixels_per_detent = 15;

// deal with the frame for continuous sources
// treat unknown (unreported) as continuous (but still with discrete check)
// perhaps we should also check for value120?
static void pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	struct wlchewing_state *state = data;
	if (!state->has_discrete &&
			state->pending_source != WL_POINTER_AXIS_SOURCE_WHEEL &&
			state->pending_source != WL_POINTER_AXIS_SOURCE_WHEEL_TILT) {
		if (state->acc_source == state->pending_source) {
			state->acc_axis[0] +=
				wl_fixed_to_double(state->pending_axis[0]);
			state->acc_axis[1] +=
				wl_fixed_to_double(state->pending_axis[1]);
		} else {
			state->acc_axis[0] =
				wl_fixed_to_double(state->pending_axis[0]);
			state->acc_axis[1] =
				wl_fixed_to_double(state->pending_axis[1]);
			state->acc_source = state->pending_source;
		}
		int detents[2] = {
			state->acc_axis[0] / pixels_per_detent,
			state->acc_axis[1] / pixels_per_detent,
		};
		state->acc_axis[0] -= pixels_per_detent * detents[0];
		state->acc_axis[1] -= pixels_per_detent * detents[1];
		im_candidates_move_by(state, detents[0] + detents[1]);
	}

	state->pending_axis[WL_POINTER_AXIS_VERTICAL_SCROLL] = 0;
	state->pending_axis[WL_POINTER_AXIS_HORIZONTAL_SCROLL] = 0;
	state->pending_source = WL_POINTER_AXIS_SOURCE_CONTINUOUS;
	state->has_discrete = false;
}

static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
	if (axis < 2) {
		struct wlchewing_state *state = data;
		state->acc_axis[axis] = 0;
	}
}

static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
	struct wlchewing_state *state = data;
	state->pending_source = axis_source;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter	= (typeof(pointer_listener.enter))noop,
	.leave	= (typeof(pointer_listener.leave))noop,
	.motion	= (typeof(pointer_listener.motion))noop,
	.button	= pointer_button,
	.axis	= pointer_axis,
	.frame	= pointer_frame,
	.axis_source	= pointer_axis_source,
	.axis_stop	= pointer_axis_stop,
	.axis_discrete	= pointer_axis_discrete,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
	struct wlchewing_state *state = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
		state->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(state->pointer, &pointer_listener, state);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && state->pointer) {
		wl_pointer_release(state->pointer);
		state->pointer = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities	= seat_capabilities,
	.name		= (typeof(seat_listener.name))noop,
};

int main(int argc, char *argv[]) {
	struct wlchewing_state *state = &global_state;
	state->config = config_new();
	if (config_read_opts(argc, argv, state->config) < 0) {
		return EXIT_FAILURE;
	}

	state->display = wl_display_connect(NULL);
	if (state->display == NULL) {
		wlchewing_err("Failed to connect to Wayland");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(state->display);
	wl_registry_add_listener(registry, &registry_listener, state);
	wl_display_roundtrip(state->display);

	struct global_map_el *el = globals;
	while (el->interface != NULL) {
		if (*el->dest == NULL) {
			wlchewing_err("Required Wayland interface not available: %s, version %d", el->interface->name, el->version);
			return EXIT_FAILURE;
		}
		el++;
	}

	wl_seat_add_listener(state->wl_globals.seat, &seat_listener, state);
	wl_display_roundtrip(state->display);

	int epoll_fd = must_errno(epoll_create1(EPOLL_CLOEXEC), "setup epoll");

	int display_fd = wl_display_get_fd(state->display);
	struct epoll_event display_epoll = {
		.events = EPOLLIN,
		.data = {
			.fd = display_fd,
		},
	};
	must_errno(
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, display_fd, &display_epoll),
		"watch Wayland event"
	);

	state->timer_fd = must_errno(
		timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC),
		"create timer"
	);
	struct epoll_event timer_epoll = {
		.events = EPOLLIN | EPOLLET,
		.data = {
			.fd = state->timer_fd,
		},
	};
	must_errno(
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, state->timer_fd, &timer_epoll),
		"watch timer event"
	);

	int bus_fd;
	if (state->config->tray_icon) {
		state->sni = xcalloc(1, sizeof(struct wlchewing_sni));
		bus_fd = must_errno(sni_setup(state->sni), "setup dbus");
		struct epoll_event bus_epoll = {
			.events = EPOLLIN,
			.data = {
				.fd = bus_fd,
			},
		};
		must_errno(
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, bus_fd, &bus_epoll),
			"watch dbus event"
		);
	}

	im_setup(state);

	struct sigaction sa;
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;

	must_errno(sigaction(SIGTERM, &sa, NULL), "set SIGTERM handler");
	must_errno(sigaction(SIGINT, &sa, NULL), "set SIGINT handler");

	struct epoll_event event_caught;
	while (epoll_wait(epoll_fd, &event_caught, 1, -1)) {
		if (event_caught.data.fd == display_fd) {
			must_errno(
				wl_display_dispatch(state->display),
				"process Wayland events"
			);
		} else if (event_caught.data.fd == state->timer_fd) {
			uint64_t count = 0;
			must_errno(
				read(state->timer_fd, &count, sizeof(uint64_t)),
				"read from timer"
			);
			im_key_press(state, state->last_key);
		} else if (state->config->tray_icon && event_caught.data.fd == bus_fd) {
			must_errno(
				errnoify(sd_bus_process(state->sni->bus, NULL)),
				"process dbus message"
			);
		}
	}
	return EXIT_SUCCESS;
}
