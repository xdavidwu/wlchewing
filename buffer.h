#ifndef BUFFER_H
#define BUFFER_H

#include <cairo.h>
#include <sys/types.h>
#include <wayland-client.h>

struct wlchewing_buffer {
	uint32_t width, height;
	int32_t scale;
	off_t size;

	void *data;
	struct wl_buffer *wl_buffer;
	cairo_surface_t *cairo_surface;
	cairo_t *cairo;
	bool available;

	struct wl_list link;
	struct wl_shm *shm;
};

struct wlchewing_buffer *buffer_new(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t scale);

void buffer_destroy(struct wlchewing_buffer *buffer);


struct wl_list *buffer_pool_new(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t scale);

struct wlchewing_buffer *buffer_pool_get_buffer(struct wl_list *pool);

void buffer_pool_destroy(struct wl_list *pool);

#endif
