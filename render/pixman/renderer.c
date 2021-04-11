#include "render/pixman.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <pixman.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>

#include "types/wlr_buffer.h"

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
	return texture->format->has_alpha;
}

static void texture_destroy(struct wlr_texture *wlr_texture) {
	if (wlr_texture == NULL) {
		return;
	}
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);

	pixman_image_unref(texture->image);
	free(texture->data);
	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.is_opaque = texture_is_opaque,
	.destroy = texture_destroy,
};

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

	uint32_t drm_format;
	struct wlr_dmabuf_attributes dmabuf = {0};
	struct wlr_shm_attributes shm = {0};
	if (wlr_buffer_get_dmabuf(wlr_buffer, &dmabuf)) {
		drm_format = dmabuf.format;
	} else if (wlr_buffer_get_shm(wlr_buffer, &shm)) {
		drm_format = shm.format;
	} else {
		goto error_buffer;
	}

	uint32_t format = get_pixman_format_from_drm(drm_format);

	void *data = NULL;
	size_t stride;
	if (!buffer_get_data_ptr(wlr_buffer, &data, &stride)) {
		wlr_log(WLR_ERROR, "Failed to get buffer data");
		goto error_buffer;
	}

	wlr_log(WLR_INFO, "width %zu, height %zu", (size_t)wlr_buffer->width,
			(size_t)wlr_buffer->height);
	wlr_log(WLR_INFO, "data %p, stride %zu", data, stride);

	buffer->image = pixman_image_create_bits(format, wlr_buffer->width,
			wlr_buffer->height, data, stride);
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
	renderer->width = width;
	renderer->height = height;
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
			0, 0, 0, renderer->width, renderer->height);

	pixman_image_unref(fill);
}

static void pixman_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	wlr_log(WLR_ERROR, "todo pixman_scissor");
}

static void matrix_to_pixman_transform(struct pixman_transform *transform,
		const float mat[static 9]) {
	struct pixman_f_transform ftr;
	ftr.m[0][0] = mat[0];
	ftr.m[0][1] = mat[1];
	ftr.m[0][2] = mat[2];
	ftr.m[1][0] = mat[3];
	ftr.m[1][1] = mat[4];
	ftr.m[1][2] = mat[5];
	ftr.m[2][0] = mat[6];
	ftr.m[2][1] = mat[7];
	ftr.m[2][2] = mat[8];

	pixman_transform_from_pixman_f_transform(transform, &ftr);
}

static bool pixman_render_subtexture_with_matrix(
		struct wlr_renderer *wlr_renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *fbox, const float matrix[static 9],
		float alpha) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	struct pixman_color mask_colour = {0};
	mask_colour.alpha = 0xFFFF * alpha;
	pixman_image_t *mask = pixman_image_create_solid_fill(&mask_colour);

	struct pixman_transform transform = {0};

	wlr_matrix_scale((float*)matrix, 1.0 / fbox->width, 1.0 / fbox->height);
	matrix_to_pixman_transform(&transform, matrix);
	pixman_transform_invert(&transform, &transform);

	pixman_image_set_transform(texture->image, &transform);

	pixman_image_composite32(PIXMAN_OP_OVER, texture->image, mask,
			buffer->image, 0, 0, 0, 0, 0, 0, renderer->width,
			renderer->height);

	return true;
}

static void pixman_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	wlr_log(WLR_ERROR, "todo pixman_render_quad_with_matrix");
}

static const uint32_t *pixman_get_shm_texture_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	return get_pixman_drm_formats(len);
}

static const struct wlr_drm_format_set *pixman_get_shm_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	return &renderer->drm_formats;
}

static struct wlr_texture *pixman_texture_from_pixels(
		struct wlr_renderer *wlr_renderer, uint32_t drm_format,
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

	texture->format = drm_get_pixel_format_info(drm_format);
	assert(texture->format);

	texture->pixman_format = get_pixman_format_from_drm(drm_format);
	if (texture->pixman_format == 0) {
		wlr_log(WLR_ERROR, "Unsupported pixel format 0x%"PRIX32, drm_format);
		free(texture);
		return NULL;
	}

	// TODO: avoid this copy
	texture->data = malloc(stride * height);
	if (texture->data == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(texture);
		return NULL;
	}
	memcpy(texture->data, data, stride * height);

	texture->image = pixman_image_create_bits_no_clear(texture->pixman_format,
			width, height, texture->data, stride);
	if (!texture->image) {
		wlr_log(WLR_ERROR, "Failed to create pixman image");
		free(texture->data);
		free(texture);
		return NULL;
	}

	return &texture->wlr_texture;
}

static const struct wlr_drm_format_set *pixman_get_dmabuf_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	return &renderer->drm_formats;
}

static bool pixman_bind_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
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
	.get_shm_texture_formats = pixman_get_shm_texture_formats,
	.get_shm_render_formats = pixman_get_shm_render_formats,
	.texture_from_pixels = pixman_texture_from_pixels,
	.get_dmabuf_render_formats = pixman_get_dmabuf_render_formats,
	.bind_buffer = pixman_bind_buffer,
};

struct wlr_renderer *wlr_pixman_renderer_create(void) {
	struct wlr_pixman_renderer *renderer =
		calloc(1, sizeof(struct wlr_pixman_renderer));
	if (renderer == NULL) {
		return NULL;
	}

	wlr_log(WLR_INFO, "Creating pixman renderer");
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	wl_list_init(&renderer->buffers);

	size_t len = 0;
	const uint32_t *formats = get_pixman_drm_formats(&len);

	for (size_t i = 0; i < len; ++i) {
		wlr_drm_format_set_add(&renderer->drm_formats, formats[i],
				DRM_FORMAT_MOD_LINEAR);
	}

	return &renderer->wlr_renderer;
}
