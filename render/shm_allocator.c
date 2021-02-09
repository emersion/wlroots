#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "render/shm_allocator.h"
#include "util/shm.h"

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_shm_buffer *shm_buffer_from_buffer(
		struct wlr_buffer *wlr_buffer) {
	assert(wlr_buffer->impl == &buffer_impl);
	return (struct wlr_shm_buffer *)wlr_buffer;
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct wlr_shm_buffer *buffer = shm_buffer_from_buffer(wlr_buffer);
	close(buffer->fd);
	free(buffer);
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
};

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_allocator, int width, int height,
		const struct wlr_drm_format *format) {
	struct wlr_shm_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &buffer_impl, width, height);

	// TODO: consider using a single file for multiple buffers
	int stride = width; // TODO: align
	size_t size = stride * height;
	buffer->fd = allocate_shm_file(size);
	if (buffer->fd < 0) {
		free(buffer);
		return NULL;
	}

	return &buffer->base;
}

static void allocator_destroy(struct wlr_allocator *wlr_allocator) {
	free(wlr_allocator);
}

static const struct wlr_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
};

struct wlr_shm_allocator *wlr_shm_allocator_create(void) {
	struct wlr_shm_allocator *allocator = calloc(1, sizeof(*allocator));
	if (allocator == NULL) {
		return NULL;
	}
	wlr_allocator_init(&allocator->base, &allocator_impl);

	return allocator;
}
