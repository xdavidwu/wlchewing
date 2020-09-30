#include "wlchewing.h"
#include "sni.h"
#include <unistd.h>

static const sd_bus_vtable service_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Category", "s", NULL, offsetof(struct wlchewing_sni, category),
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Id", "s", NULL, offsetof(struct wlchewing_sni, id),
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Title", "s", NULL, offsetof(struct wlchewing_sni, title),
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Status", "s", NULL, offsetof(struct wlchewing_sni, status),
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("WindowId", "u", NULL, offsetof(struct wlchewing_sni, window_id),
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("IconName", "s", NULL, offsetof(struct wlchewing_sni, icon_name),
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_SIGNAL("NewIcon", "", 0),
	SD_BUS_VTABLE_END
};

static int handle_owner_changed(sd_bus_message *m, void *userdata,
		sd_bus_error *ret_error) {
	struct wlchewing_sni *sni = userdata;
	int res;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	char *service, *old_owner, *new_owner;
	res = sd_bus_message_read(m, "sss", &service, &old_owner, &new_owner);
	if (res < 0) {
		wlchewing_err("Failed to parse NameOwnerChanged message: %s",
			strerror(-res));
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

int sni_set_icon(struct wlchewing_sni *sni, bool english) {
	if (english == sni->english) {
		return 0;
	}
	sni->english = english;
	sni->icon_name = english ? "wlchewing-eng" : "wlchewing-bopomofo";
	int res;
	res = sd_bus_emit_signal(sni->bus, "/StatusNotifierItem",
			"org.freedesktop.StatusNotifierItem", "NewIcon", "");
	if (res < 0) {
		wlchewing_err("Failed to emit NewIcon: %s", strerror(-res));
		return res;
	}
	return 0;
}

int sni_setup(struct wlchewing_sni *sni) {
	sni->category = "SystemServices";
	sni->id = "wlchewing";
	sni->title = "Wayland Chinese zhuyin input method with libchewing";
	sni->status = "Active";
	sni->icon_name = "wlchewing-bopomofo";
	int res;
	char buf[64];
	res = sd_bus_open_user(&sni->bus);
	if (res < 0) {
		wlchewing_err("Failed to open bus connection: %s",
			strerror(-res));
		return res;
	}
	sprintf(buf, "org.freedesktop.StatusNotifierItem-%ld-%d", (long)getpid(), 1);
	sni->service_name = strdup(buf);
	res = sd_bus_add_object_vtable(sni->bus, &sni->slot, "/StatusNotifierItem",
			"org.freedesktop.StatusNotifierItem", service_vtable, sni);
	if (res < 0) {
		wlchewing_err("Failed to add object: %s", strerror(-res));
		return res;
	}
	res = sd_bus_request_name(sni->bus, buf, 0);
	if (res < 0) {
		wlchewing_err("Failed to request name: %s", strerror(-res));
		return res;
	}
	res = sd_bus_match_signal(sni->bus, &sni->signal_slot,
			"org.freedesktop.DBus", "/org/freedesktop/DBus",
			"org.freedesktop.DBus", "NameOwnerChanged",
			handle_owner_changed, sni);
	if (res < 0) {
		wlchewing_err("Failed to listen to NameOwnerChanged: %s",
			strerror(-res));
		return res;
	}
	sd_bus_error err = SD_BUS_ERROR_NULL;
	res = sd_bus_call_method(sni->bus, "org.freedesktop.StatusNotifierWatcher",
			"/StatusNotifierWatcher", "org.freedesktop.StatusNotifierWatcher",
			"RegisterStatusNotifierItem", &err, NULL, "s",
			sni->service_name);
	if (res < 0) {
		wlchewing_err("Failed to register sni: %s: %s", err.name, err.message);
	}
	return sd_bus_get_fd(sni->bus);
}
