#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct option long_options[] = {
	{"start-with-english",	no_argument,	NULL,	'e'},
	{"dock",	required_argument,	NULL,	'd'},
	{"font",	required_argument,	NULL,	'f'},
	{"top",			no_argument,	NULL,	't'},
	{0},
};

static const char help[] = "Usage: %s [OPTIONS]...\n"
	"\n"
	"  -e, --start-with-english\tStart with English mode\n"
	"  -d, --dock=(dock|yield|no)\tSet candidate panel behavior, default to dock\n"
	"\t\t\t\t  dock\tConfigure as a dock\n"
	"\t\t\t\t\tDo not overlap with any surfaces\n"
	"\t\t\t\t  yield\tYield to existing dock surface\n"
	"\t\t\t\t\tMay be put on top of normal surfaces\n"
	"\t\t\t\t  no\tDo not either dock or yield\n"
	"\t\t\t\t\tMay be put on top of any sufaces\n"
	"  -f, --font=FONT\t\tPango font description to use\n"
	"  -t, --top\t\t\tAnchor candidate panel to top instead of bottom\n";

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
	while ((opt = getopt_long(argc, argv, "ed:f:t", long_options, NULL)) != -1) {
		if (opt == '?') {
			fprintf(stderr, help, argv[0]);
			return -EINVAL;
		}
		switch (opt) {
		case 'e':
			config->start_eng = true;
			break;
		case 'd':
			if (!strcmp(optarg, "dock")) {
				config->dock = DOCK_DOCK;
			} else if (!strcmp(optarg, "yield")) {
				config->dock = DOCK_YEILD;
			} else if (!strcmp(optarg, "no")) {
				config->dock = DOCK_NO;
			} else {
				fprintf(stderr, help, argv[0]);
				return -EINVAL;
			}
			break;
		case 'f':
			config->font = optarg;
			break;
		case 't':
			config->anchor_top = true;
			break;
		}
	}
	return 0;
}
