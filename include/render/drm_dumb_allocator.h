#ifndef RENDER_DRM_DUMB_ALLOCATOR_H
#define RENDER_DRM_DUMB_ALLOCATOR_H

#include "render/allocator.h"

#include <wlr/types/wlr_buffer.h>

struct wlr_drm_dumb_buffer {
	struct wlr_buffer base;
	struct wl_list link; // wlr_drm_dumb_allocator::buffers

	int drm_fd;
	struct wlr_dmabuf_attributes dmabuf;

	uint32_t format;
	uint32_t handle;
	uint32_t stride;
	uint32_t width, height;

	uint64_t size;
	void *data;
};

struct wlr_drm_dumb_allocator {
	struct wlr_allocator base;
	struct wl_list buffers; // wlr_drm_dumb_buffer::link
	int drm_fd;
};

struct wlr_drm_dumb_allocator *wlr_drm_dumb_allocator_create(int drm_fd);

#endif
