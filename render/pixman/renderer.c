#include "render/pixman.h"

#include <wlr/render/interface.h>
#include <wlr/util/log.h>

#include <stdlib.h>
#include <assert.h>

static const struct wlr_renderer_impl renderer_impl;

struct wlr_pixman_renderer *pixman_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_pixman_renderer *)wlr_renderer;
}

static void pixman_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct wlr_pixman_renderer *renderer = pixman_get_renderer(wlr_renderer);
	(void)renderer;
	assert(false && "todo pixman_begin");
}

static void pixman_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	assert(false && "todo pixman_clear");
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
	assert(false && "todo pixman_texture_from_pixels");
}

static const struct wlr_drm_format_set *pixman_get_dmabuf_render_formats(
		struct wlr_renderer *wlr_renderer) {
	assert(false && "todo pixman_get_dmabug_render_formats");
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
/*
	.destroy = pixman_destroy,
	.bind_buffer = pixman_bind_buffer,
	.end = pixman_end,
	.resource_is_wl_drm_buffer = pixman_resource_is_wl_drm_buffer,
	.wl_drm_buffer_get_size = pixman_wl_drm_buffer_get_size,
	.get_dmabuf_texture_formats = pixman_get_dmabuf_texture_formats,
	.get_dmabuf_render_formats = pixman_get_dmabuf_render_formats,
	.preferred_read_format = pixman_preferred_read_format,
	.read_pixels = pixman_read_pixels,
	.texture_from_wl_drm = pixman_texture_from_wl_drm,
	.texture_from_dmabuf = pixman_texture_from_dmabuf,
	.init_wl_display = pixman_init_wl_display,
	.blit_dmabuf = pixman_blit_dmabuf,
*/
};

struct wlr_renderer *wlr_pixman_renderer_create(int drm_fd) {
	struct wlr_pixman_renderer *renderer =
		calloc(1, sizeof(struct wlr_pixman_renderer));
	if (renderer == NULL) {
		return NULL;
	}
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

	wlr_log(WLR_INFO, "Creating pixman renderer");

	return &renderer->wlr_renderer;
}

