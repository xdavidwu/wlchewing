#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct wlchewing_config {
	bool start_eng;
};

struct wlchewing_config *config_new();

int config_read_opts(int argc, char *argv[], struct wlchewing_config *config);

#endif
