#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "xmem.h"

static const struct option long_options[] = {
	{"start-with-english",	no_argument,		NULL,	'e'},
	{"dock",		required_argument,	NULL,	'd'},
	{"font",		required_argument,	NULL,	'f'},
	{"top",			no_argument,		NULL,	't'},
	{"text-color",		required_argument,	NULL,	'T'},
	{"background-color",	required_argument,	NULL,	'b'},
	{"selection-color",	required_argument,	NULL,	's'},
	{"selection-text-color",required_argument,	NULL,	'S'},
	{"no-tray-icon",	no_argument,		NULL,	'n'},
	{"force-default-keymap",no_argument,		NULL,	1},
	{"no-num-key-hint",	no_argument,		NULL,	2},
	{0},
};

static constexpr char help[] = "\
Usage: %s [OPTIONS]...\n\
\n\
  -e, --start-with-english      Start with English mode\n\
  -d, --dock=(dock|yield|no)    Set candidate panel behavior, default to dock\n\
                                  dock  Configure as a dock\n\
                                        Do not overlap with any surfaces\n\
                                  yield Yield to existing dock surface\n\
                                        May be put on top of normal surfaces\n\
                                  no    Do not either dock or yield\n\
                                        May be put on top of any sufaces\n\
  -f, --font=FONT               Pango font description to use\n\
      --force-default-keymap    Force to use xkbcommon default keymap to\n\
                                translate keycodes for libchewing\n\
  -t, --top                     Anchor candidate panel to top instead of bottom\n\
  -T, --text-color=COLOR        Set candidate panel text color\n\
  -b, --background-color=COLOR  Set candidate panel background color\n\
  -s, --selection-color=COLOR   Set candidate panel selection highlight color\n\
  -S, --selection-text-color=COLOR\n\
                                Set candidate panel selection text color\n\
  -n, --no-tray-icon            Disable tray icon\n\
      --no-num-key-hint         Disable number key display on candidate panel\n\
\n\
COLOR is color specified as either #RRGGBB or #RRGGBBAA.\n";

static constexpr struct wlchewing_config defaults = {
	.text_color		= {1.0, 1.0, 1.0, 1.0},
	.selection_text_color	= {1.0, 1.0, 1.0, 1.0},
	.background_color	= {0.0, 0.0, 0.0, 1.0},
	.selection_color	= {0.25, 0.25, 0.25, 1.0},
	.tray_icon		= true,
	.key_hint		= true,
};

struct wlchewing_config *config_new() {
	struct wlchewing_config *config = xcalloc(1,
		sizeof(struct wlchewing_config));
	*config = defaults;
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
		case 'b':
		case 's':
		case 'S':
			if (decode_color(optarg,
					opt == 'T' ? config->text_color :
					opt == 'b' ? config->background_color :
					opt == 's' ? config->selection_color :
					config->selection_text_color // S
					) < 0) {
				fprintf(stderr, help, argv[0]);
				return -EINVAL;
			}
			break;
		case 'n':
			config->tray_icon = false;
			break;
		case 1:
			config->chewing_use_xkb_default = true;
			break;
		case 2:
			config->key_hint = false;
			break;
		}
	}
	return 0;
}
