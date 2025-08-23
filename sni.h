#ifndef SNI_H
#define SNI_H

#include <systemd/sd-bus.h>

struct wlchewing_state;

struct wlchewing_sni {
	sd_bus *bus;
	char service_name[64];
};

int sni_notify_new_icon(struct wlchewing_sni *sni);
int sni_setup(struct wlchewing_state *state);

#endif
