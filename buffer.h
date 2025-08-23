#ifndef BUFFER_H
#define BUFFER_H

#include <cairo.h>
#include <sys/types.h>
#include <wayland-client.h>

struct wlchewing_buffer {
	void *data;
	off_t size;
	struct wl_buffer *wl_buffer;
	cairo_t *cairo;
	bool available;

	struct wl_list link;
};

struct wlchewing_buffer_pool {
	uint32_t width, height;
	int32_t scale;

	struct wl_shm *shm;

	struct wl_list buffers; // struct wlchewing_buffer
};

struct wlchewing_buffer *buffer_new(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t scale);

void buffer_destroy(struct wlchewing_buffer *buffer);


struct wlchewing_buffer_pool *buffer_pool_new(struct wl_shm *shm,
	uint32_t width, uint32_t height, uint32_t scale);

struct wlchewing_buffer *buffer_pool_get_buffer(struct wlchewing_buffer_pool *pool);

void buffer_pool_destroy(struct wlchewing_buffer_pool *pool);

#endif
