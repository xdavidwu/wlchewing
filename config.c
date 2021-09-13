#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct option long_options[] = {
	{"start-with-english",		no_argument,	NULL,	'e'},
	{"dock",		required_argument,	NULL,	'd'},
	{"font",		required_argument,	NULL,	'f'},
	{"top",				no_argument,	NULL,	't'},
	{"text-color",		required_argument,	NULL,	'T'},
	{"background-color",	required_argument,	NULL,	'b'},
	{"selection-color",	required_argument,	NULL,	's'},
	{"selection-text-color",required_argument,	NULL,	'S'},
	{"no-tray-icon",		no_argument,	NULL,	'n'},
	{"force-default-keymap",	no_argument,	NULL,	1},
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
	"      --force-default-keymap\tForce to use xkbcommon default keymap to\n"
	"\t\t\t\ttranslate keycodes for libchewing\n"
	"  -t, --top\t\t\tAnchor candidate panel to top instead of bottom\n"
	"  -T, --text-color=COLOR\tSet candidate panel text color\n"
	"  -b, --background-color=COLOR\tSet candidate panel background color\n"
	"  -s, --selection-color=COLOR\tSet candidate panel selection highlight color\n"
	"  -S, --selection-text-color=COLOR\n"
	"\t\t\t\tSet candidate panel selection text color\n"
	"  -n, --no-tray-icon\t\tDisable tray icon.\n"
	"\n"
	"COLOR is color specified as either #RRGGBB or #RRGGBBAA.\n";

struct wlchewing_config *config_new() {
	struct wlchewing_config *config = calloc(1,
		sizeof(struct wlchewing_config));
	if (!config) {
		return NULL;
	}
	config->text_color[0] = 1.0;
	config->text_color[1] = 1.0;
	config->text_color[2] = 1.0;
	config->text_color[3] = 1.0;
	config->selection_text_color[0] = 1.0;
	config->selection_text_color[1] = 1.0;
	config->selection_text_color[2] = 1.0;
	config->selection_text_color[3] = 1.0;
	config->background_color[3] = 1.0;
	config->selection_color[0] = 0.25;
	config->selection_color[1] = 0.25;
	config->selection_color[2] = 0.25;
	config->selection_color[3] = 1.0;
	config->tray_icon = true;
	return config;
}

static int decode_color(const char *str, double *rgba) {
	int r, g, b, a;
	char dummy;
	if (sscanf(str, "#%2x%2x%2x%2x%c", &r, &g, &b, &a, &dummy) == 4) {
		rgba[0] = r / 255.0;
		rgba[1] = g / 255.0;
		rgba[2] = b / 255.0;
		rgba[3] = a / 255.0;
		return 0;
	} else if (sscanf(str, "#%2x%2x%2x%c", &r, &g, &b, &dummy) == 3) {
		rgba[0] = r / 255.0;
		rgba[1] = g / 255.0;
		rgba[2] = b / 255.0;
		return 0;
	}
	return -EINVAL;
}

int config_read_opts(int argc, char *argv[], struct wlchewing_config *config) {
	int opt;
	while ((opt = getopt_long(argc, argv, "ed:f:tT:b:s:S:n", long_options, NULL)) != -1) {
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
		case 'T':
			if (decode_color(optarg, config->text_color) < 0) {
				fprintf(stderr, help, argv[0]);
				return -EINVAL;
			}
			break;
		case 'b':
			if (decode_color(optarg, config->background_color) < 0) {
				fprintf(stderr, help, argv[0]);
				return -EINVAL;
			}
			break;
		case 's':
			if (decode_color(optarg, config->selection_color) < 0) {
				fprintf(stderr, help, argv[0]);
				return -EINVAL;
			}
			break;
		case 'S':
			if (decode_color(optarg, config->selection_text_color) < 0) {
				fprintf(stderr, help, argv[0]);
				return -EINVAL;
			}
			break;
		case 'n':
			config->tray_icon = false;
		case 1:
			config->chewing_use_xkb_default = true;
			break;
		}
	}
	return 0;
}
