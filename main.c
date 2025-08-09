#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "xmem.h"
#include "wlchewing.h"
#include "bottom-panel.h"
#include "sni.h"

struct wlchewing_state global_state = {0};

static void handle_signal(int signo) {
	im_release_all_keys(&global_state);
	wl_display_roundtrip(global_state.display);
	raise(signo);
}

static const struct wl_output_listener output_listener;

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct wlchewing_state *state = data;
	if (strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
		state->input_method_manager = wl_registry_bind(registry, name,
			&zwp_input_method_manager_v2_interface, 1);
	} else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		state->virtual_keyboard_manager = wl_registry_bind(registry, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
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
	.global_remove = (void (*)(void *, struct wl_registry *, uint32_t))noop,
};

static void output_scale(void *data, struct wl_output *output, int32_t scale) {
	int32_t *user_data = xcalloc(sizeof(int32_t), 1);
	*user_data = scale;
	wl_output_set_user_data(output, user_data);
}

static const struct wl_output_listener output_listener = {
	.scale = output_scale,
	.geometry = (void (*)(void *, struct wl_output *, int32_t,  int32_t,  int32_t,  int32_t,  int32_t,  const char *, const char *, int32_t))noop,
	.mode = (void (*)(void *, struct wl_output *, uint32_t,  int32_t,  int32_t,  int32_t))noop,
	.done = (void (*)(void *, struct wl_output *))noop
};

int main(int argc, char *argv[]) {
	struct wlchewing_state *state = &global_state;
	state->config = config_new();
	if (config_read_opts(argc, argv, state->config) < 0) {
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
			|| state->input_method_manager == NULL
			|| state->virtual_keyboard_manager == NULL) {
		wlchewing_err("Required wayland interface not available");
		return EXIT_FAILURE;
	}

	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		wlchewing_err("Failed to create epoll: %s",
			strerror(errno));
		return EXIT_FAILURE;
	}

	int display_fd = wl_display_get_fd(state->display);
	struct epoll_event display_epoll = {
		.events = EPOLLIN,
		.data = {
			.fd = display_fd,
		},
	};
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, display_fd, &display_epoll)
			== -1) {
		wlchewing_err("Failed to add display epoll: %s",
			strerror(errno));
		return EXIT_FAILURE;
	}

	state->timer_fd = timerfd_create(CLOCK_MONOTONIC,
		TFD_NONBLOCK | TFD_CLOEXEC);
	if (state->timer_fd < 0) {
		wlchewing_err("Failed to create timer: %s", strerror(errno));
		return EXIT_FAILURE;
	}
	struct epoll_event timer_epoll = {
		.events = EPOLLIN | EPOLLET,
		.data = {
			.fd = state->timer_fd,
		},
	};
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, state->timer_fd, &timer_epoll) < 0) {
		wlchewing_err("Failed to add timer epoll: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	int bus_fd;
	if (state->config->tray_icon) {
		state->sni = xcalloc(1, sizeof(struct wlchewing_sni));
		bus_fd = sni_setup(state->sni);
		if (bus_fd < 0) {
			wlchewing_err("Failed to setup dbus: %s", strerror(-bus_fd));
			return EXIT_FAILURE;
		}
		struct epoll_event bus_epoll = {
			.events = EPOLLIN,
			.data = {
				.fd = bus_fd,
			},
		};
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, bus_fd, &bus_epoll) < 0) {
			wlchewing_err("Failed to add bus epoll: %s", strerror(errno));
			return EXIT_FAILURE;
		}
	}

	im_setup(state);

	struct sigaction sa;
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;

	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		wlchewing_err("Failed to set SIGTERM handler: %s", strerror(errno));
		return EXIT_FAILURE;
	}
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		wlchewing_err("Failed to set SIGINT handler: %s", strerror(errno));
		return EXIT_FAILURE;
	}

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
