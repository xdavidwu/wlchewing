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
	.leave = noop,
};

static void bottom_panel_configure(struct wlchewing_bottom_panel *panel,
		struct wlchewing_state *state){
	printf("render scale %d\n", panel->scale);
	struct wlchewing_buffer *buffer = buffer_new(state->shm,
		panel->width, panel->height, panel->scale);
	assert(buffer);

	panel->layout = pango_cairo_create_layout(buffer->cairo);
	if (state->config->font) {
		PangoFontDescription *desc =
			pango_font_description_from_string(state->config->font);
		pango_layout_set_font_description(panel->layout, desc);
		pango_font_description_free(desc);
	}

	pango_layout_set_text(panel->layout, "哈嘍 PangoCairo", -1);
	int width, height;
	pango_layout_get_size(panel->layout, &width, &height);
	panel->height = height / PANGO_SCALE;

	wl_surface_attach(panel->wl_surface, buffer->wl_buffer, 0, 0);
	zwlr_layer_surface_v1_set_size(panel->layer_surface, 0, panel->height);

	if (state->config->dock == DOCK_DOCK) {
		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, panel->height);
	} else if (state->config->dock == DOCK_YEILD) {
		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, 0);
	} else {
		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, -1);
	}

	wl_surface_commit(panel->wl_surface);
	wl_display_roundtrip(state->display);
	wl_surface_set_buffer_scale(panel->wl_surface, panel->scale);
	buffer_destroy(buffer);
}

static int render_cand(struct wlchewing_bottom_panel *panel,
		struct wlchewing_buffer *buffer, const char *text, bool selected) {
	pango_layout_set_text(panel->layout, text, -1);
	int width, height;
	pango_layout_get_size(panel->layout, &width, &height);
	width /= PANGO_SCALE;

	if (selected) {
		cairo_set_source_rgba(buffer->cairo,
			panel->config->selection_color[0],
			panel->config->selection_color[1],
			panel->config->selection_color[2],
			panel->config->selection_color[3]);
		cairo_rectangle(buffer->cairo, 0, 0, width + 8, buffer->height);
		cairo_fill(buffer->cairo);

	}

	const double *text_color = selected ?
		panel->config->selection_text_color :
		panel->config->text_color;
	cairo_set_source_rgba(buffer->cairo, text_color[0], text_color[1],
		text_color[2], text_color[3]);
	cairo_move_to(buffer->cairo, 4, 0);
	pango_cairo_show_layout(buffer->cairo, panel->layout);
	return width + 8;
}

struct wlchewing_bottom_panel *bottom_panel_new(
		struct wlchewing_state *state) {
	struct wlchewing_bottom_panel *panel = calloc(1,
		sizeof(struct wlchewing_bottom_panel));
	if (panel == NULL) {
		wlchewing_err("Failed to calloc for bottom panel");
		return NULL;
	}
	panel->config = state->config;
	panel->scale = 1;
	panel->height = 1;
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

	// set font, scale and height
	bottom_panel_configure(panel, state);

	panel->buffer_pool = buffer_pool_new(state->shm,
		panel->width, panel->height, panel->scale);
	return panel;
}

void bottom_panel_destroy(struct wlchewing_bottom_panel *panel) {
	zwlr_layer_surface_v1_destroy(panel->layer_surface);
	buffer_pool_destroy(panel->buffer_pool);
	g_object_unref(panel->layout);
	free(panel);
}

void bottom_panel_render(struct wlchewing_bottom_panel *panel,
		ChewingContext *ctx) {
	int total = chewing_cand_TotalChoice(ctx);
	assert(panel->selected_index < total);

	struct wlchewing_buffer *buffer = buffer_pool_get_buffer(
		panel->buffer_pool);
	cairo_t *cairo = buffer->cairo;
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, panel->config->background_color[0],
		panel->config->background_color[1],
		panel->config->background_color[2],
		panel->config->background_color[3]);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
	printf("selected %d\n", panel->selected_index);

	int offset = 0, total_offset = 0;
	offset = render_cand(panel, buffer,
		chewing_cand_string_by_index_static(ctx,
		panel->selected_index), true);
	for (int i = panel->selected_index + 1; i < total; i++) {
		cairo_translate(cairo, offset, 0);
		total_offset += offset;
		offset = render_cand(panel, buffer,
			chewing_cand_string_by_index_static(ctx, i), false);
	}
	cairo_translate(cairo, -total_offset, 0);
	cairo_restore(cairo);
	wl_surface_attach(panel->wl_surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(panel->wl_surface, 0, 0,
		buffer->width * buffer->scale, buffer->height * buffer->scale);
	wl_surface_commit(panel->wl_surface);
}
