#ifndef SNI_H
#define SNI_H

#include <stdint.h>
#include <systemd/sd-bus.h>

struct wlchewing_sni {
	sd_bus *bus;
	sd_bus_slot *slot;
	sd_bus_slot *signal_slot;

	const char *category, *id, *title, *status;
	uint32_t window_id;
	const char *icon_name;

	const char *service_name;
	bool english;
};

int sni_set_icon(struct wlchewing_sni *sni, bool english);
int sni_setup(struct wlchewing_sni *sni);

#endif
