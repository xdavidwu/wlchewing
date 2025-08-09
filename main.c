#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "bottom-panel.h"
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
		&wl_seat_interface, 0, // not used directly yet, request the latest one
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

static void noop() {
	// no-op
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = (typeof(registry_listener.global_remove))noop,
};

static void output_scale(void *data, struct wl_output *output, int32_t scale) {
	int32_t *user_data = xcalloc(sizeof(int32_t), 1);
	*user_data = scale;
	wl_output_set_user_data(output, user_data);
}

static const struct wl_output_listener output_listener = {
	.scale		= output_scale,
	.geometry	= (typeof(output_listener.geometry))noop,
	.mode		= (typeof(output_listener.mode))noop,
	.done		= (typeof(output_listener.done))noop
};

static inline int must_errno(int ret, const char *op) {
	if (ret < 0) {
		wlchewing_perr("Failed to %s", op);
		exit(EXIT_FAILURE);
	}
	return ret;
}

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
			if (wl_display_dispatch(state->display) == -1) {
				break;
			}
		} else if (event_caught.data.fd == state->timer_fd) {
			uint64_t count = 0;
			read(state->timer_fd, &count, sizeof(uint64_t));
			im_key_press(state, state->last_key);
		} else if (state->config->tray_icon && event_caught.data.fd == bus_fd) {
			sd_bus_process(state->sni->bus, NULL);
		}
	}
	return EXIT_SUCCESS;
}
