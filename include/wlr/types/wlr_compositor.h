#ifndef WLR_TYPES_WLR_COMPOSITOR_H
#define WLR_TYPES_WLR_COMPOSITOR_H

#include <wayland-server.h>
#include <wlr/render/render.h>

struct wlr_compositor {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wlr_renderer *renderer;
	struct wl_list surfaces;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal create_surface;
	} events;
};

void wlr_compositor_destroy(struct wlr_compositor *wlr_compositor);
struct wlr_compositor *wlr_compositor_create(struct wl_display *display,
		struct wlr_renderer *renderer);

#endif
