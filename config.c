#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

static const struct option long_options[] = {
	{"start-with-english",	no_argument,	NULL,	'e'},
	{0},
};

static const char help[] = "Usage: %s [OPTIONS]...\n"
	"\n"
	"\t-e, --start-with-english\tStart with English mode\n";

struct wlchewing_config *config_new() {
	struct wlchewing_config *config = calloc(1,
		sizeof(struct wlchewing_config));
	if (!config) {
		return NULL;
	}
	return config;
}

int config_read_opts(int argc, char *argv[], struct wlchewing_config *config) {
	int opt;
	while ((opt = getopt_long(argc, argv, "e", long_options, NULL)) != -1) {
		if (opt == '?') {
			fprintf(stderr, help, argv[0]);
			return -EINVAL;
		}
		switch (opt) {
		case 'e':
			config->start_eng = true;
			break;
		}
	}
	return 0;
}
