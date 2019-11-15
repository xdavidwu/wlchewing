#ifndef BOTTOM_PANEL_H
#define BOTTOM_PANEL_H

#include <pango/pangocairo.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct wlchewing_state;
struct wlchewing_buffer;

struct wlchewing_bottom_panel {
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_surface *wl_surface;
	struct wl_list *buffer_pool;
	uint32_t width, height;
	int32_t scale;
	PangoLayout *layout;
	int selected_index;
};

struct wlchewing_bottom_panel *bottom_panel_new(struct wlchewing_state *state);

void bottom_panel_destroy(struct wlchewing_bottom_panel *panel);

void bottom_panel_render(struct wlchewing_bottom_panel *panel,
	ChewingContext *ctx);

#endif
