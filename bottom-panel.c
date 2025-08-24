#include <assert.h>
#include <pango/pangocairo.h>

#include "bottom-panel.h"
#include "buffer.h"
#include "wlchewing.h"
#include "xmem.h"

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t w, uint32_t h) {
	struct wlchewing_bottom_panel *panel = data;
	panel->width = w;
	panel->height = h;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *layer_surface) {
	struct wlchewing_bottom_panel *panel = data;
	bottom_panel_destroy(panel);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure	= layer_surface_configure,
	.closed		= layer_surface_closed,
};

static void surface_preferred_buffer_scale(void *data,
		struct wl_surface *surface, int32_t scale) {
	struct wlchewing_bottom_panel *panel = data;
	panel->scale = scale;
}

static void surface_enter(void *data, struct wl_surface *surface,
		struct wl_output *output) {
	struct wlchewing_bottom_panel *panel = data;
	panel->subpixel = (uintptr_t)wl_output_get_user_data(output);
}

static const struct wl_surface_listener surface_listener = {
	.enter			= surface_enter,
	.leave			= (typeof(surface_listener.leave))noop,
	.preferred_buffer_scale	= surface_preferred_buffer_scale,
	// subpixel order alreay considered via wl_output.geometry
	.preferred_buffer_transform =
		(typeof(surface_listener.preferred_buffer_transform))noop,
};

static constexpr int cand_padding = 4;

static int render_cand(struct wlchewing_state *state,
		struct wlchewing_buffer *buffer, const char *text, int index) {
	char hint[2] = {
		(state->config.key_hint && index < 10) ?
			index == 9 ? '0' : '1' + index : 0,
		0
	};
	int width, hint_width = 0;
	if (hint[0]) {
		pango_layout_set_text(state->bottom_panel_key_hint_layout, hint, -1);
		pango_layout_get_pixel_size(state->bottom_panel_key_hint_layout, &hint_width, NULL);
	}
	pango_layout_set_text(state->bottom_panel_text_layout, text, -1);
	pango_layout_get_pixel_size(state->bottom_panel_text_layout, &width, NULL);
	width += hint_width;

	const int cell_width = width + cand_padding * 2;
	if (!index) {
		cairo_set_source_rgba(buffer->cairo,
			state->config.selection_color[0],
			state->config.selection_color[1],
			state->config.selection_color[2],
			state->config.selection_color[3]);
		cairo_rectangle(buffer->cairo, 0, 0, cell_width, state->bottom_panel->buffer_pool->height);
		cairo_fill(buffer->cairo);
	}

	const double *text_color = !index ?
		state->config.selection_text_color :
		state->config.text_color;
	cairo_set_source_rgba(buffer->cairo, text_color[0], text_color[1],
		text_color[2], text_color[3]);
	cairo_move_to(buffer->cairo, cand_padding, 0);
	if (hint[0]) {
		pango_cairo_show_layout(buffer->cairo, state->bottom_panel_key_hint_layout);
		cairo_move_to(buffer->cairo, cand_padding + hint_width, 0);
	}
	pango_cairo_show_layout(buffer->cairo, state->bottom_panel_text_layout);
	return cell_width;
}

int bottom_panel_init(struct wlchewing_state *state) {
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cairo = cairo_create(surface);
	cairo_surface_destroy(surface);
	// fontmap, etc. from cairo 
	state->bottom_panel_text_layout = pango_cairo_create_layout(cairo);
	assert(state->bottom_panel_text_layout);
	cairo_destroy(cairo);

	if (state->config.font) {
		PangoFontDescription *desc =
			pango_font_description_from_string(state->config.font);
		pango_layout_set_font_description(state->bottom_panel_text_layout, desc);
		pango_font_description_free(desc);
	}

	state->bottom_panel_key_hint_layout = pango_layout_copy(state->bottom_panel_text_layout);
	PangoAttrList *attrs = pango_attr_list_from_string("line-height 1.1\nscale 0.75");
	pango_layout_set_attributes(state->bottom_panel_key_hint_layout, attrs);
	pango_attr_list_unref(attrs);

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
	struct wlchewing_bottom_panel *panel = xcalloc(1,
		sizeof(struct wlchewing_bottom_panel));
	panel->height = state->bottom_panel_text_height;
	panel->width = 1;
	panel->scale = 1;
	panel->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	panel->wl_surface = wl_compositor_create_surface(state->wl_globals.compositor);
	assert(panel->wl_surface);
	wl_surface_add_listener(panel->wl_surface, &surface_listener, panel);
	panel->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		state->wl_globals.layer_shell, panel->wl_surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "input-method-panel");
	assert(panel->layer_surface);

	zwlr_layer_surface_v1_add_listener(panel->layer_surface,
		&layer_surface_listener, panel);
	zwlr_layer_surface_v1_set_anchor(panel->layer_surface,
		(state->config.anchor_top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP :
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(panel->layer_surface, 0, panel->height);
	wl_surface_commit(panel->wl_surface);
	// obtain width (and probably height) via layer_surface configure
	// compositors may also send preferred_buffer_scale here
	wl_display_roundtrip(state->display);

	zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface,
		state->config.dock == DOCK_DOCK ? panel->height :
		state->config.dock == DOCK_YEILD ? 0 : -1);
	wl_surface_set_buffer_scale(panel->wl_surface, panel->scale);
	panel->buffer_pool = buffer_pool_new(state->wl_globals.shm,
		panel->width, panel->height, panel->scale);

	return panel;
}

void bottom_panel_destroy(struct wlchewing_bottom_panel *panel) {
	zwlr_layer_surface_v1_destroy(panel->layer_surface);
	wl_surface_destroy(panel->wl_surface);
	buffer_pool_destroy(panel->buffer_pool);
	free(panel);
}

cairo_subpixel_order_t buffer_subpixel_to_cairo[] = {
	[WL_OUTPUT_SUBPIXEL_UNKNOWN]		= CAIRO_SUBPIXEL_ORDER_DEFAULT,
	[WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB]	= CAIRO_SUBPIXEL_ORDER_RGB,
	[WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR]	= CAIRO_SUBPIXEL_ORDER_BGR,
	[WL_OUTPUT_SUBPIXEL_VERTICAL_RGB]	= CAIRO_SUBPIXEL_ORDER_VRGB,
	[WL_OUTPUT_SUBPIXEL_VERTICAL_BGR]	= CAIRO_SUBPIXEL_ORDER_VBGR,
};

void bottom_panel_render(struct wlchewing_state *state) {
	int total = chewing_cand_TotalChoice(state->chewing);
	assert(state->bottom_panel->selected_index < total);

	struct wlchewing_bottom_panel *panel = state->bottom_panel;
	struct wlchewing_buffer_pool *pool = panel->buffer_pool;
	struct wlchewing_buffer *buffer = buffer_pool_get_buffer(pool);
	cairo_t *cairo = buffer->cairo;
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, state->config.background_color[0],
		state->config.background_color[1],
		state->config.background_color[2],
		state->config.background_color[3]);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);

	cairo_font_options_t *opt = cairo_font_options_create();
	if (panel->subpixel == WL_OUTPUT_SUBPIXEL_NONE) {
		cairo_font_options_set_antialias(opt, CAIRO_ANTIALIAS_GRAY);
	} else {
		cairo_font_options_set_antialias(opt, CAIRO_ANTIALIAS_SUBPIXEL);
		cairo_font_options_set_subpixel_order(opt,
			buffer_subpixel_to_cairo[panel->subpixel]);
	}
	cairo_set_font_options(cairo, opt);
	cairo_font_options_destroy(opt);
	pango_cairo_update_layout(cairo, state->bottom_panel_text_layout);
	pango_cairo_update_layout(cairo, state->bottom_panel_key_hint_layout);

	int offset = 0, total_offset = 0;
	for (int i = 0; i < total - state->bottom_panel->selected_index &&
			total_offset < pool->width; i++) {
		cairo_translate(cairo, offset, 0);
		offset = render_cand(state, buffer,
			chewing_cand_string_by_index_static(state->chewing,
				i + panel->selected_index), i);
		total_offset += offset;
	}
	cairo_restore(cairo);

	wl_surface_attach(panel->wl_surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(panel->wl_surface, 0, 0,
		pool->width * pool->scale, pool->height * pool->scale);
	wl_surface_commit(panel->wl_surface);
	wl_display_roundtrip(state->display);

	// a configure or preferred_buffer_scale changes
	if (panel->width != pool->width || panel->height != pool->height ||
			panel->scale != pool->scale) {
		buffer_pool_destroy(state->bottom_panel->buffer_pool);

		zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface,
			state->config.dock == DOCK_DOCK ? panel->height :
			state->config.dock == DOCK_YEILD ? 0 : -1);
		wl_surface_set_buffer_scale(panel->wl_surface, panel->scale);
		state->bottom_panel->buffer_pool = buffer_pool_new(
			state->wl_globals.shm,
			panel->width, panel->height, panel->scale);
		bottom_panel_render(state);
	}
}
