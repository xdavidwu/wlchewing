#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "errors.h"
#include "sni.h"
#include "wlchewing.h"

static const char *category	= "SystemServices";
static const char *id		= "wlchewing";
static const char *title	= "Chinese zhuyin input method";
static const char *status	= "Active";

static int get_icon_name(sd_bus *bus, const char *path, const char *interface,
		const char *property, sd_bus_message *reply, void *data,
		sd_bus_error *ret_error) {
	struct wlchewing_state *state = data;
	return sd_bus_message_append_basic(reply, 's',
		state->forwarding ? "wlchewing-eng" : "wlchewing-bopomofo");
}

static int activate(sd_bus_message *m, void *data, sd_bus_error *ret_error) {
	struct wlchewing_state *state = data;
	im_mode_switch(state, !state->forwarding);
	return 0;
}

static_assert(sizeof(size_t) >= sizeof(uintptr_t));
static const sd_bus_vtable service_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Category",	"s", NULL, (uintptr_t)&category,
		SD_BUS_VTABLE_PROPERTY_CONST | SD_BUS_VTABLE_ABSOLUTE_OFFSET),
	SD_BUS_PROPERTY("Id",		"s", NULL, (uintptr_t)&id,
		SD_BUS_VTABLE_PROPERTY_CONST | SD_BUS_VTABLE_ABSOLUTE_OFFSET),
	SD_BUS_PROPERTY("Title",	"s", NULL, (uintptr_t)&title,
		SD_BUS_VTABLE_PROPERTY_CONST | SD_BUS_VTABLE_ABSOLUTE_OFFSET),
	SD_BUS_PROPERTY("Status",	"s", NULL, (uintptr_t)&status,
		SD_BUS_VTABLE_PROPERTY_CONST | SD_BUS_VTABLE_ABSOLUTE_OFFSET),
	SD_BUS_PROPERTY("IconName",	"s", get_icon_name, 0, 0),
	SD_BUS_METHOD("Activate", "ii", "", activate, 0),
	SD_BUS_SIGNAL("NewIcon", "", 0),
	SD_BUS_VTABLE_END
};

static int name_owner_changed(sd_bus_message *m, void *data,
		sd_bus_error *ret_error) {
	struct wlchewing_sni *sni = data;
	int res;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	const char *service;
	res = errnoify(sd_bus_message_read(m, "sss", &service, NULL, NULL));
	if (res < 0) {
		wlchewing_perr("Failed to parse NameOwnerChanged message");
	}
	if (!strcmp(service, "org.freedesktop.StatusNotifierWatcher")) {
		res = sd_bus_call_method(sni->bus,
			"org.freedesktop.StatusNotifierWatcher",
			"/StatusNotifierWatcher",
			"org.freedesktop.StatusNotifierWatcher",
			"RegisterStatusNotifierItem", &err, NULL, "s",
			sni->service_name);
		if (res < 0) {
			wlchewing_err("Failed to register sni: %s: %s",
				err.name, err.message);
		}
	}
	return 0;
}

int sni_notify_new_icon(struct wlchewing_sni *sni) {
	if (sni == NULL) {
		return 0;
	}
	int res;
	res = errnoify(sd_bus_emit_signal(sni->bus, "/StatusNotifierItem",
			"org.freedesktop.StatusNotifierItem", "NewIcon", ""));
	if (res < 0) {
		wlchewing_perr("Failed to emit NewIcon");
		return res;
	}
	return 0;
}

int sni_setup(struct wlchewing_state *state) {
	struct wlchewing_sni *sni = state->sni;
	assert(sni != NULL);
	int res;
	res = errnoify(sd_bus_open_user(&sni->bus));
	if (res < 0) {
		wlchewing_perr("Failed to open bus connection");
		return res;
	}
	sprintf(sni->service_name, "org.freedesktop.StatusNotifierItem-%ld-1", (long)getpid());
	res = errnoify(sd_bus_add_object_vtable(sni->bus, NULL,
		"/StatusNotifierItem", "org.freedesktop.StatusNotifierItem",
		service_vtable, state));
	if (res < 0) {
		wlchewing_perr("Failed to add object");
		return res;
	}
	res = errnoify(sd_bus_request_name(sni->bus, sni->service_name, 0));
	if (res < 0) {
		wlchewing_perr("Failed to request name");
		return res;
	}
	res = errnoify(sd_bus_match_signal(sni->bus, NULL,
		"org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "NameOwnerChanged",
		name_owner_changed, sni));
	if (res < 0) {
		wlchewing_perr("Failed to listen to NameOwnerChanged");
		return res;
	}
	sd_bus_error err = SD_BUS_ERROR_NULL;
	res = sd_bus_call_method(sni->bus, "org.freedesktop.StatusNotifierWatcher",
		"/StatusNotifierWatcher", "org.freedesktop.StatusNotifierWatcher",
		"RegisterStatusNotifierItem", &err, NULL, "s", sni->service_name);
	if (res < 0) {
		wlchewing_err("Failed to register sni: %s: %s", err.name, err.message);
	}
	return errnoify(sd_bus_get_fd(sni->bus));
}
