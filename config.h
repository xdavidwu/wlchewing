#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

enum dock_option {
	DOCK_NO = 0,
	DOCK_YEILD,
	DOCK_DOCK,
};

struct wlchewing_config {
	enum dock_option dock;
	const char *font;
	double text_color[4];
	double background_color[4];
	double selection_color[4];
	double selection_text_color[4];
	bool start_eng;
	bool anchor_top;
	bool tray_icon;
	bool key_hint;
	bool chewing_use_xkb_default;
};

struct wlchewing_config *config_new();

int config_read_opts(int argc, char *argv[], struct wlchewing_config *config);

#endif
