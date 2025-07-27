#include <assert.h>
#include <stdbool.h>

#include "wlchewing.h"
#include "buffer.h"
#include "bottom-panel.h"

static void noop() {
	// no-op
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *wlr_layer_surface,
		uint32_t serial, uint32_t w, uint32_t h) {
	printf("resize %d %d\n", w, h);
	struct wlchewing_bottom_panel *panel = data;
	panel->width = w;
	panel->height = h;
	zwlr_layer_surface_v1_ack_configure(wlr_layer_surface, serial);
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *wlr_layer_surface) {
	struct wlchewing_bottom_panel *panel = data;
	bottom_panel_destroy(panel);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void surface_enter(void *data, struct wl_surface *wl_surface,
		struct wl_output *output) {
	struct wlchewing_bottom_panel *panel = data;
	panel->scale = *((int32_t *)wl_output_get_user_data(output));
	printf("scale %d\n", panel->scale);
}

static const struct wl_surface_listener surface_listener = {
	.enter = surface_enter,
	.leave = (void (*)(void *, struct wl_surface *, struct wl_output *))noop,
};

static void bottom_panel_configure(struct wlchewing_state *state,
		struct wlchewing_bottom_panel *panel){
	wl_surface_attach(panel->wl_surface,
		state->bottom_panel_test_buffer->wl_buffer, 0, 0);
	zwlr_layer_surface_v1_set_size(panel->layer_surface, 0, panel->height);

	if (state->config->dock == DOCK_DOCK) {
		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface,
			panel->height);
	} else if (state->config->dock == DOCK_YEILD) {
		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, 0);
	} else {
		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, -1);
	}

	wl_surface_commit(panel->wl_surface);
	wl_display_roundtrip(state->display);
	wl_surface_set_buffer_scale(panel->wl_surface, panel->scale);
}

static const int cand_padding = 4;

static int render_cand(struct wlchewing_state *state,
		struct wlchewing_buffer *buffer, const char *text, bool selected) {
	pango_layout_set_text(state->bottom_panel_text_layout, text, -1);
	int width;
	pango_layout_get_pixel_size(state->bottom_panel_text_layout, &width, NULL);

	if (selected) {
		cairo_set_source_rgba(buffer->cairo,
			state->config->selection_color[0],
			state->config->selection_color[1],
			state->config->selection_color[2],
			state->config->selection_color[3]);
		cairo_rectangle(buffer->cairo, 0, 0, width + cand_padding * 2, buffer->height);
		cairo_fill(buffer->cairo);
	}

	const double *text_color = selected ?
		state->config->selection_text_color :
		state->config->text_color;
	cairo_set_source_rgba(buffer->cairo, text_color[0], text_color[1],
		text_color[2], text_color[3]);
	cairo_move_to(buffer->cairo, cand_padding, 0);
	pango_cairo_show_layout(buffer->cairo, state->bottom_panel_text_layout);
	return width + cand_padding * 2;
}

int bottom_panel_init(struct wlchewing_state *state) {
	state->bottom_panel_test_buffer = buffer_new(state->shm, 1, 1, 1);
	assert(state->bottom_panel_test_buffer);

	state->bottom_panel_text_layout = pango_cairo_create_layout(
		state->bottom_panel_test_buffer->cairo);
	assert(state->bottom_panel_text_layout);
	if (state->config->font) {
		PangoFontDescription *desc =
			pango_font_description_from_string(state->config->font);
		pango_layout_set_font_description(state->bottom_panel_text_layout, desc);
		pango_font_description_free(desc);
	}

	pango_layout_set_text(state->bottom_panel_text_layout,
		pango_language_get_sample_string(
			pango_language_from_string("zh-tw")), -1);
	int height;
	pango_layout_get_pixel_size(state->bottom_panel_text_layout, NULL, &height);
	state->bottom_panel_text_height = height;
	return 0;
}

struct wlchewing_bottom_panel *bottom_panel_new(struct wlchewing_state *state) {
	assert(state->bottom_panel_text_layout);
	struct wlchewing_bottom_panel *panel = calloc(1,
		sizeof(struct wlchewing_bottom_panel));
	assert(panel);
	panel->scale = 1;
	panel->height = state->bottom_panel_text_height;
	panel->width = 1;
	panel->wl_surface = wl_compositor_create_surface(state->compositor);
	assert(panel->wl_surface);
	wl_surface_add_listener(panel->wl_surface, &surface_listener, panel);
	panel->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		state->layer_shell, panel->wl_surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "input-method-panel");
	assert(panel->layer_surface);

	zwlr_layer_surface_v1_add_listener(panel->layer_surface,
		&layer_surface_listener, panel);
	zwlr_layer_surface_v1_set_anchor(panel->layer_surface,
		(state->config->anchor_top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP :
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(panel->layer_surface, 0, panel->height);
	wl_surface_commit(panel->wl_surface);
	wl_display_roundtrip(state->display);

	// set height, get/set width and scale
	bottom_panel_configure(state, panel);

	panel->buffer_pool = buffer_pool_new(state->shm,
		panel->width, panel->height, panel->scale);
	return panel;
}

void bottom_panel_destroy(struct wlchewing_bottom_panel *panel) {
	zwlr_layer_surface_v1_destroy(panel->layer_surface);
	wl_surface_destroy(panel->wl_surface);
	buffer_pool_destroy(panel->buffer_pool);
	free(panel);
}

void bottom_panel_render(struct wlchewing_state *state) {
	int total = chewing_cand_TotalChoice(state->chewing);
	assert(state->bottom_panel->selected_index < total);

	struct wlchewing_buffer *buffer = buffer_pool_get_buffer(
		state->bottom_panel->buffer_pool);
	cairo_t *cairo = buffer->cairo;
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, state->config->background_color[0],
		state->config->background_color[1],
		state->config->background_color[2],
		state->config->background_color[3]);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);

	int offset = 0, total_offset = 0;
	offset = render_cand(state, buffer,
		chewing_cand_string_by_index_static(state->chewing,
		state->bottom_panel->selected_index), true);
		total_offset += offset;
	for (int i = state->bottom_panel->selected_index + 1; i < total &&
			total_offset < state->bottom_panel->width; i++) {
		cairo_translate(cairo, offset, 0);
		offset = render_cand(state, buffer,
			chewing_cand_string_by_index_static(state->chewing, i),
			false);
		total_offset += offset;
	}
	cairo_restore(cairo);
	wl_surface_attach(state->bottom_panel->wl_surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(state->bottom_panel->wl_surface, 0, 0,
		buffer->width * buffer->scale, buffer->height * buffer->scale);
	wl_surface_commit(state->bottom_panel->wl_surface);
}
