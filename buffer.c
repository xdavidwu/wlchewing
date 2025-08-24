#include "cairo.h"
#define _GNU_SOURCE // memfd

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "wlchewing.h"
#include "xmem.h"

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	struct wlchewing_buffer *buffer = data;
	buffer->available = true;
}

static const struct wl_buffer_listener buffer_listener = {
	.release	= buffer_release,
};

struct wlchewing_buffer *buffer_new(struct wl_shm *shm,
		uint32_t width, uint32_t height, uint32_t scale) {
	struct wlchewing_buffer *buffer = xcalloc(1, sizeof(struct wlchewing_buffer));
	buffer->available = true;

	int fd = memfd_create("", MFD_CLOEXEC);
	if (fd < 0) {
		wlchewing_perr("Failed to create anonymous file for buffer");
		free(buffer);
		return NULL;
	}

	uint32_t widthpx = width * scale, heightpx = height * scale;
	off_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, widthpx);
	buffer->size = heightpx * stride;
	int ret = ftruncate(fd, buffer->size);
	if (ret == -1) {
		wlchewing_err("Failed to ftruncate");
		free(buffer);
		return NULL;
	}
	buffer->data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (buffer->data == MAP_FAILED) {
		wlchewing_perr("Failed to mmap %ld", buffer->size);
		close(fd);
		free(buffer);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, buffer->size);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0, widthpx,
		heightpx, stride, WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		buffer->data, CAIRO_FORMAT_ARGB32, widthpx, heightpx, stride);
	buffer->cairo = cairo_create(surface);
	cairo_surface_destroy(surface);
	cairo_scale(buffer->cairo, scale, scale);
	return buffer;
}

void buffer_destroy(struct wlchewing_buffer *buffer) {
	wl_buffer_destroy(buffer->wl_buffer);
	cairo_destroy(buffer->cairo);
	munmap(buffer->data, buffer->size);
	free(buffer);
}

struct wlchewing_buffer_pool *buffer_pool_new(struct wl_shm *shm,
		uint32_t width, uint32_t height, uint32_t scale) {
	struct wlchewing_buffer_pool *pool = xcalloc(1, sizeof(struct wlchewing_buffer_pool));
	pool->shm = shm;
	pool->width = width;
	pool->height = height;
	pool->scale = scale;
	wl_list_init(&pool->buffers);
	return pool;
}

struct wlchewing_buffer *buffer_pool_get_buffer(struct wlchewing_buffer_pool *pool) {
	struct wlchewing_buffer *cur_buffer, *last_buffer = NULL;
	wl_list_for_each(cur_buffer, &pool->buffers, link) {
		if (cur_buffer->available) {
			cur_buffer->available = false;
			return cur_buffer;
		}
		last_buffer = cur_buffer;
	}
	struct wlchewing_buffer *new_buffer = buffer_new(pool->shm,
		pool->width, pool->height, pool->scale);
	if (new_buffer == NULL) {
		free(pool);
		wlchewing_err("Failed to create new buffer for buffer pool");
		return NULL;
	}
	new_buffer->available = false;
	wl_list_insert(last_buffer ? &last_buffer->link : &pool->buffers, &new_buffer->link);
	return new_buffer;
}

void buffer_pool_destroy(struct wlchewing_buffer_pool *pool) {
	struct wlchewing_buffer *cur_buffer, *tmp;
	wl_list_for_each_safe(cur_buffer, tmp, &pool->buffers, link) {
		wl_list_remove(&cur_buffer->link);
		buffer_destroy(cur_buffer);
	}
	free(pool);
}
