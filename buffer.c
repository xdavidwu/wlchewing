#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "wlchewing.h"
#include "xmem.h"

static void handle_release(void *data, struct wl_buffer *wl_buffer) {
	struct wlchewing_buffer *buffer = data;
	buffer->available = true;
}

static const struct wl_buffer_listener buffer_listener = {
	.release	= handle_release,
};

static void mktempname(char *template) {
	struct timespec ts;
	unsigned long r;

	clock_gettime(CLOCK_REALTIME, &ts);
	r = (ts.tv_nsec * 65537) ^ ((uintptr_t)&ts / 16 + (uintptr_t)template);
	for (int i = 0; i < 6; i++, r >>= 5)
		template[i] = 'A' + ( r & 15 ) + ( r & 16 ) * 2;
}

struct wlchewing_buffer *buffer_new(struct wl_shm *shm,
		uint32_t width, uint32_t height, uint32_t scale) {
	struct wlchewing_buffer *buffer = xcalloc(1, sizeof(struct wlchewing_buffer));
	buffer->width = width;
	buffer->height = height;
	buffer->scale = scale;
	buffer->shm = shm;

	char *template = xstrdup("/wlchewing-XXXXXX");
	mktempname(&template[11]);
	int fd = shm_open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		wlchewing_perr("Failed to shm_open");
		free(buffer);
		return NULL;
	}
	shm_unlink(template);
	free(template);

	off_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32,
		width * scale);
	buffer->size = (height * scale) * stride;
	int ret = ftruncate(fd, buffer->size);
	if (ret == -1) {
		wlchewing_err("Failed to ftruncate");
		free(buffer);
		return NULL;
	}
	buffer->data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (buffer->data == MAP_FAILED) {
		wlchewing_err("Failed to mmap %ld", buffer->size);
		close(fd);
		free(buffer);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, buffer->size);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0, width * scale,
		height * scale, stride, WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->cairo_surface = cairo_image_surface_create_for_data(
		buffer->data, CAIRO_FORMAT_ARGB32, width * scale,
		height * scale, stride);
	buffer->cairo = cairo_create(buffer->cairo_surface);
	cairo_scale(buffer->cairo, scale, scale);
	return buffer;
}

void buffer_destroy(struct wlchewing_buffer *buffer) {
	wl_buffer_destroy(buffer->wl_buffer);
	cairo_destroy(buffer->cairo);
	cairo_surface_destroy(buffer->cairo_surface);
	munmap(buffer->data, buffer->size);
	free(buffer);
}

struct wl_list *buffer_pool_new(struct wl_shm *shm,
		uint32_t width, uint32_t height, uint32_t scale) {
	struct wl_list *pool = xcalloc(1, sizeof(struct wl_list));
	wl_list_init(pool);
	struct wlchewing_buffer *buffer = buffer_new(shm, width, height, scale);
	buffer->available = true;
	if (buffer == NULL) {
		free(pool);
		wlchewing_err("Failed to create first buffer for buffer pool");
		return NULL;
	}
	wl_list_insert(pool, &buffer->link);
	return pool;
}

struct wlchewing_buffer *buffer_pool_get_buffer(struct wl_list *pool) {
	struct wlchewing_buffer *cur_buffer, *last_buffer;
	wl_list_for_each(cur_buffer, pool, link) {
		if (cur_buffer->available) {
			cur_buffer->available = false;
			return cur_buffer;
		}
		last_buffer = cur_buffer;
	}
	struct wlchewing_buffer *new_buffer = buffer_new(last_buffer->shm,
		last_buffer->width, last_buffer->height, last_buffer->scale);
	wl_list_insert(&last_buffer->link, &new_buffer->link);
	return new_buffer;
}

void buffer_pool_destroy(struct wl_list *pool) {
	struct wlchewing_buffer *cur_buffer, *tmp;
	wl_list_for_each_safe(cur_buffer, tmp, pool, link) {
		wl_list_remove(&cur_buffer->link);
		buffer_destroy(cur_buffer);
	}
	free(pool);
}
