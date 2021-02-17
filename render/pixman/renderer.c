#include "render/pixman.h"

#include <wlr/render/interface.h>
#include <wlr/util/log.h>

#include <pixman.h>
#include <wayland-server.h>
#include <drm_fourcc.h>

#include <stdlib.h>
#include <assert.h>

static const struct wlr_renderer_impl renderer_impl;

static struct wlr_pixman_renderer *get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_pixman_renderer *)wlr_renderer;
}

static struct wlr_pixman_buffer *get_buffer(
		struct wlr_pixman_renderer *renderer, struct wlr_buffer *wlr_buffer) {
	struct wlr_pixman_buffer *buffer;
	wl_list_for_each(buffer, &renderer->buffers, link) {
		if (buffer->buffer == wlr_buffer) {
			return buffer;
		}
	}
	return NULL;
}

static const struct wlr_texture_impl texture_impl;

static struct wlr_pixman_texture *get_texture(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture->impl == &texture_impl);
	return (struct wlr_pixman_texture *)wlr_texture;
}

static bool texture_is_opaque(struct wlr_texture *wlr_texture) {
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);

	return texture->format == WL_SHM_FORMAT_XRGB8888;
}

static bool texture_write_pixels(struct wlr_texture *wlr_texture,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		const void *data) {
	assert(false && "todo texture_write_pixels");
	return false;
}

static bool texture_to_dmabuf(struct wlr_texture *wlr_texture,
		struct wlr_dmabuf_attributes *attribs) {
	assert(false && "todo texture_to_dmabuf");
	return false;
}

static void texture_destroy(struct wlr_texture *wlr_texture) {
	if (wlr_texture == NULL) {
		return;
	}
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);

	pixman_image_unref(texture->image);
	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.is_opaque = texture_is_opaque,
	.write_pixels = texture_write_pixels,
	.to_dmabuf = texture_to_dmabuf,
	.destroy = texture_destroy,
};

static uint32_t get_pixman_format_from_wl(enum wl_shm_format wl_fmt) {
	uint32_t fmt = 0;
	switch(wl_fmt) {
	case WL_SHM_FORMAT_ARGB8888:
		fmt = PIXMAN_a8r8g8b8;
		break;
	case WL_SHM_FORMAT_XRGB8888:
		fmt = PIXMAN_x8r8g8b8;
		break;
	default:
		// TODO: wl_shm_format to string?
		wlr_log(WLR_ERROR, "Unsupported wl_shm_format");
		break;
	}

	return fmt;
}

struct wlr_pixman_texture *pixman_create_texture(
		struct wlr_texture *wlr_texture, struct wlr_pixman_renderer *renderer);

static void handle_destroy_buffer(struct wl_listener *listener, void *data) {
	struct wlr_pixman_buffer *buffer =
		wl_container_of(listener, buffer, buffer_destroy);
	wl_list_remove(&buffer->link);
	wl_list_remove(&buffer->buffer_destroy.link);

	pixman_image_unref(buffer->image);

	free(buffer);
}

static struct wlr_pixman_buffer *create_buffer(
		struct wlr_pixman_renderer *renderer, struct wlr_buffer *wlr_buffer) {
	struct wlr_pixman_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	buffer->buffer = wlr_buffer;
	buffer->renderer = renderer;

	struct wlr_dmabuf_attributes dmabuf = {0};
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &dmabuf)) {
		goto error_buffer;
	}

	uint32_t format;
	switch (dmabuf.format) {
	case DRM_FORMAT_XRGB8888:
		format = PIXMAN_x8r8g8b8;
		break;
	case DRM_FORMAT_ARGB8888:
		format = PIXMAN_a8r8g8b8;
		break;
	default:
		wlr_log(WLR_ERROR, "Unsupported DRM format 0x%x\n", dmabuf.format);
		goto error_buffer;
	}

	// TODO: ensure wlr_buffer is a drm dumb buffer
	struct wlr_drm_dumb_buffer *drm_dumb_buffer =
		(struct wlr_drm_dumb_buffer *)wlr_buffer;

	buffer->image = pixman_image_create_bits(format, wlr_buffer->width,
			wlr_buffer->height, drm_dumb_buffer->data,
			drm_dumb_buffer->stride);
	if (!buffer->image) {
		wlr_log(WLR_ERROR, "Failed to allocate pixman image\n");
		goto error_buffer;
	}

	buffer->buffer_destroy.notify = handle_destroy_buffer;
	wl_signal_add(&wlr_buffer->events.destroy, &buffer->buffer_destroy);

	wl_list_insert(&renderer->buffers, &buffer->link);

	wlr_log(WLR_DEBUG, "Created pixman buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;

error_buffer:
	free(buffer);
	return NULL;
}

static void pixman_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	renderer->viewport_width = width;
	renderer->viewport_height = height;
}

static void pixman_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	const struct pixman_color colour = {
		.red = color[0] * 0xFFFF,
		.green = color[1] * 0xFFFF,
		.blue = color[2] * 0xFFFF,
		.alpha = color[3] * 0xFFFF,
	};

	pixman_image_t *fill = pixman_image_create_solid_fill(&colour);

	pixman_image_composite32(PIXMAN_OP_SRC, fill, NULL, buffer->image, 0, 0, 0,
			0, 0, 0, renderer->viewport_width, renderer->viewport_height);

	pixman_image_unref(fill);
}

static void pixman_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	assert(false && "todo pixman_scissor");
}

static bool pixman_render_subtexture_with_matrix(
		struct wlr_renderer *wlr_renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *box, const float matrix[static 9],
		float alpha) {
	assert(false && "todo pixman_render_subtexture_with_matrix");
}

static void pixman_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	assert(false && "todo pixman_render_quad_with_matrix");
}

static void pixman_render_ellipse_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	assert(false && "todo pixman_render_ellipse_with_matrix");
}

static const enum wl_shm_format *pixman_get_shm_texture_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	assert(false && "todo pixman_get_shm_texture_formats");
}

static struct wlr_texture *pixman_texture_from_pixels(
		struct wlr_renderer *wlr_renderer, enum wl_shm_format wl_fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data) {


	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_texture *texture =
		calloc(1, sizeof(struct wlr_pixman_texture));
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate pixman texture");
		return NULL;
	}

	wlr_texture_init(&texture->wlr_texture, &texture_impl, width, height);
	texture->renderer = renderer;
	texture->format = get_pixman_format_from_wl(wl_fmt);
	texture->image = pixman_image_create_bits_no_clear(texture->format,
			width, height, (void*)data, stride);

	if (!texture->image) {
		wlr_log(WLR_ERROR, "Failed to create pixman image");
		free(texture);
		return NULL;
	}


	return &texture->wlr_texture;
}

static const struct wlr_drm_format_set *pixman_get_dmabuf_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	return &renderer->dmabuf_render_formats;
}

static bool pixman_bind_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	wlr_log(WLR_INFO, "Pixman bind buffer");

	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);

	if (renderer->current_buffer != NULL) {
		wlr_buffer_unlock(renderer->current_buffer->buffer);
		renderer->current_buffer = NULL;
	}

	if (wlr_buffer == NULL) {
		return true;
	}

	struct wlr_pixman_buffer *buffer = get_buffer(renderer, wlr_buffer);
	if (buffer == NULL) {
		buffer = create_buffer(renderer, wlr_buffer);
	}
	if (buffer == NULL) {
		return false;
	}

	wlr_buffer_lock(wlr_buffer);
	renderer->current_buffer = buffer;

	return true;
}

static const struct wlr_renderer_impl renderer_impl = {
	.begin = pixman_begin,
	.clear = pixman_clear,
	.scissor = pixman_scissor,
	.render_subtexture_with_matrix = pixman_render_subtexture_with_matrix,
	.render_quad_with_matrix = pixman_render_quad_with_matrix,
	.render_ellipse_with_matrix = pixman_render_ellipse_with_matrix,
	.get_shm_texture_formats = pixman_get_shm_texture_formats,
	.texture_from_pixels = pixman_texture_from_pixels,
	.get_dmabuf_render_formats = pixman_get_dmabuf_render_formats,
	.bind_buffer = pixman_bind_buffer,
};

static void init_dmabuf_formats(struct wlr_pixman_renderer *renderer) {
	static const int fmts[] = {
		DRM_FORMAT_ARGB8888,
		DRM_FORMAT_XRGB8888,
	};

	static const size_t len = sizeof(fmts) / sizeof(fmts[0]);
	for (size_t i = 0; i < len; ++i) {
		wlr_drm_format_set_add(&renderer->dmabuf_render_formats, fmts[i],
				0);
	}
}

struct wlr_renderer *wlr_pixman_renderer_create(int drm_fd) {
	struct wlr_pixman_renderer *renderer =
		calloc(1, sizeof(struct wlr_pixman_renderer));
	if (renderer == NULL) {
		return NULL;
	}

	wlr_log(WLR_INFO, "Creating pixman renderer");
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	wl_list_init(&renderer->buffers);

	init_dmabuf_formats(renderer);

	return &renderer->wlr_renderer;
}

