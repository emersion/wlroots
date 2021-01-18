#ifndef RENDER_PIXMAN_H
#define RENDER_PIXMAN_H

#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_pixman_renderer {
	struct wlr_renderer wlr_renderer;
};

struct wlr_pixman_renderer *pixman_get_renderer(
	struct wlr_renderer *wlr_renderer);

#endif
