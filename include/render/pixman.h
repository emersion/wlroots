#ifndef RENDER_PIXMAN_H
#define RENDER_PIXMAN_H

#include "render/drm_dumb_allocator.h"

#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/drm_format_set.h>

struct wlr_pixman_buffer;

struct wlr_pixman_renderer {
	struct wlr_renderer wlr_renderer;

	struct wl_list buffers; // wlr_pixman_buffer.link

	struct wlr_pixman_buffer *current_buffer;
	uint32_t viewport_width, viewport_height;

	struct wlr_drm_format_set dmabuf_render_formats;
};

struct wlr_pixman_buffer {
	struct wlr_buffer *buffer;
	struct wlr_pixman_renderer *renderer;

	pixman_image_t *image;

	struct wl_listener buffer_destroy;
	struct wl_list link; // wlr_pixman_renderer.buffers
};

#endif
