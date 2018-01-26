#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface_layers.h>
#include <wlr/render.h>
#include <wlr/util/log.h>
#include "surface-layers-protocol.h"

static const char *surface_layers_role = "layer_surface";

#define WLR_LAYER_SURFACE_INPUT_DEVICE_COUNT 3

#define WLR_LAYER_SURFACE_ANCHOR_LEFT_RIGHT \
	(LAYER_SURFACE_ANCHOR_LEFT | LAYER_SURFACE_ANCHOR_RIGHT)
#define WLR_LAYER_SURFACE_ANCHOR_TOP_BOTTOM \
	(LAYER_SURFACE_ANCHOR_TOP | LAYER_SURFACE_ANCHOR_BOTTOM)

static void layer_surface_set_interactivity(struct wl_client *client,
		struct wl_resource *resource, uint32_t input_types,
		uint32_t exclusive_types) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);

	if (input_types >> WLR_LAYER_SURFACE_INPUT_DEVICE_COUNT) {
		wl_resource_post_error(resource,
			LAYER_SURFACE_ERROR_INVALID_INPUT_DEVICE,
			"Unknown input device in input_types");
		return;
	}
	if (exclusive_types >> WLR_LAYER_SURFACE_INPUT_DEVICE_COUNT) {
		wl_resource_post_error(resource,
			LAYER_SURFACE_ERROR_INVALID_INPUT_DEVICE,
			"Unknown input device in exclusive_types");
		return;
	}

	surface->pending->input_types = input_types;
	surface->pending->exclusive_types = exclusive_types;
	surface->pending->invalid |= WLR_LAYER_SURFACE_INVALID_INTERACTIVITY;
}

static void layer_surface_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);

	if (anchor >> 4) {
		wl_resource_post_error(resource, LAYER_SURFACE_ERROR_INVALID_ANCHOR,
			"Unknown anchor");
		return;
	}

	surface->pending->anchor = anchor;
	surface->pending->invalid |= WLR_LAYER_SURFACE_INVALID_ANCHOR;
}

static void layer_surface_set_exclusive_zone(struct wl_client *client,
		struct wl_resource *resource, uint32_t zone) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	surface->pending->exclusive_zone = zone;
	surface->pending->invalid |= WLR_LAYER_SURFACE_INVALID_EXCLUSIVE_ZONE;
}

static void layer_surface_set_margin(struct wl_client *client,
		struct wl_resource *resource, int32_t horizontal, int32_t vertical) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	surface->pending->margin_horizontal = horizontal;
	surface->pending->margin_vertical = vertical;
	surface->pending->invalid |= WLR_LAYER_SURFACE_INVALID_MARGIN;
}

void wlr_layer_surface_get_box(struct wlr_layer_surface *layer_surface,
		struct wlr_box *box) {
	int width = layer_surface->surface->current->width;
	int height = layer_surface->surface->current->height;
	struct wlr_layer_surface_state *state = layer_surface->current;

	int output_width, output_height;
	wlr_output_effective_resolution(layer_surface->output, &output_width,
		&output_height);

	if (state->anchor & LAYER_SURFACE_ANCHOR_LEFT) {
		box->x = state->margin_horizontal;
	} else if (state->anchor & LAYER_SURFACE_ANCHOR_RIGHT) {
		box->x = output_width - width - state->margin_horizontal;
	} else {
		box->x = (double)(output_width - width) / 2;
	}
	if (state->anchor & LAYER_SURFACE_ANCHOR_TOP) {
		box->y = state->margin_vertical;
	} else if (state->anchor & LAYER_SURFACE_ANCHOR_BOTTOM) {
		box->y = output_height - height - state->margin_vertical;
	} else {
		box->y = (double)(output_height - height) / 2;
	}

	if ((state->anchor & WLR_LAYER_SURFACE_ANCHOR_LEFT_RIGHT) ==
			WLR_LAYER_SURFACE_ANCHOR_LEFT_RIGHT) {
		box->width = output_width - 2*state->margin_horizontal;
	} else {
		box->width = width;
	}
	if ((state->anchor & WLR_LAYER_SURFACE_ANCHOR_TOP_BOTTOM) ==
			WLR_LAYER_SURFACE_ANCHOR_TOP_BOTTOM) {
		box->height = output_height - 2*state->margin_vertical;
	} else {
		box->height = height;
	}
}

void wlr_layer_surface_configure(struct wlr_layer_surface *layer_surface,
		int32_t width, int32_t height) {
	if (layer_surface->surface->current->width == width &&
			layer_surface->surface->current->height == height &&
			layer_surface->configured) {
		return;
	}
	layer_surface_send_configure(layer_surface->resource, width, height);
}

static void layer_surface_destroy(struct wlr_layer_surface *surface) {
	wl_signal_emit(&surface->events.destroy, surface);
	wl_list_remove(&surface->surface_destroy_listener.link);
	wl_list_remove(&surface->surface_commit_listener.link);
	wl_list_remove(&surface->output_destroy_listener.link);
	wl_list_remove(&surface->link);
	wl_resource_set_user_data(surface->resource, NULL);
	free(surface->current);
	free(surface->pending);
	free(surface);
}

static void layer_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_layer_surface *layer_surface =
		wl_resource_get_user_data(resource);
	if (layer_surface != NULL) {
		layer_surface_destroy(layer_surface);
	}
}

static const struct layer_surface_interface layer_surface_impl = {
	.set_interactivity = layer_surface_set_interactivity,
	.set_anchor = layer_surface_set_anchor,
	.set_exclusive_zone = layer_surface_set_exclusive_zone,
	.set_margin = layer_surface_set_margin,
};

static void handle_layer_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, surface_commit_listener);
	struct wlr_layer_surface_state *state = layer_surface->current;
	struct wlr_layer_surface_state *next = layer_surface->pending;

	if (next->invalid & WLR_LAYER_SURFACE_INVALID_INTERACTIVITY) {
		state->input_types = next->input_types;
		state->exclusive_types = next->exclusive_types;
	}
	if (next->invalid & WLR_LAYER_SURFACE_INVALID_ANCHOR) {
		state->anchor = next->anchor;
	}
	if (next->invalid & WLR_LAYER_SURFACE_INVALID_EXCLUSIVE_ZONE) {
		state->exclusive_zone = next->exclusive_zone;
	}
	if (next->invalid & WLR_LAYER_SURFACE_INVALID_MARGIN) {
		state->margin_horizontal = next->margin_horizontal;
		state->margin_vertical = next->margin_vertical;
	}

	state->invalid |= next->invalid;
	next->invalid = 0;

	if (!layer_surface->configured && layer_surface->surface->texture->valid) {
		layer_surface->configured = true;
	}

	wl_signal_emit(&layer_surface->events.commit, layer_surface);
}

static void handle_layer_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, surface_destroy_listener);
	layer_surface_destroy(layer_surface);
}

static void handle_layer_surface_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, output_destroy_listener);
	layer_surface_destroy(layer_surface);
}

static void surface_layers_get_layer_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *output_resource, uint32_t layer) {
	struct wlr_surface_layers *surface_layers =
		wl_resource_get_user_data(resource);
	struct wlr_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wlr_output *output = wl_resource_get_user_data(output_resource);

	if (layer > SURFACE_LAYERS_LAYER_INPUT) {
		wl_resource_post_error(resource, SURFACE_LAYERS_ERROR_INVALID_LAYER,
			"Unknown layer");
		return;
	}
	if (surface->current->buffer != NULL) {
		wl_resource_post_error(resource,
			SURFACE_LAYERS_ERROR_ALREADY_CONSTRUCTED,
			"Surface has a buffer attached or committed");
		return;
	}
	if (wlr_surface_set_role(surface, surface_layers_role, resource,
			SURFACE_LAYERS_ERROR_ROLE)) {
		return;
	}

	struct wlr_layer_surface *layer_surface =
		calloc(1, sizeof(struct wlr_layer_surface));
	if (layer_surface == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	layer_surface->resource = wl_resource_create(client,
		&layer_surface_interface, wl_resource_get_version(resource), id);
	wlr_log(L_DEBUG, "new layer_surface %p (res %p)", layer_surface,
		layer_surface->resource);
	wl_resource_set_implementation(layer_surface->resource, &layer_surface_impl,
		layer_surface, layer_surface_resource_destroy);

	layer_surface->surface_layers = surface_layers;
	layer_surface->surface = surface;
	layer_surface->output = output;
	layer_surface->layer = layer;

	layer_surface->current = calloc(1, sizeof(struct wlr_layer_surface_state));
	if (layer_surface->current == NULL) {
		free(layer_surface);
		wl_client_post_no_memory(client);
		return;
	}
	layer_surface->pending = calloc(1, sizeof(struct wlr_layer_surface_state));
	if (layer_surface->pending == NULL) {
		free(layer_surface->current);
		free(layer_surface);
		wl_client_post_no_memory(client);
		return;
	}

	wl_signal_init(&layer_surface->events.destroy);
	wl_signal_init(&layer_surface->events.commit);

	wl_signal_add(&layer_surface->surface->events.destroy,
		&layer_surface->surface_destroy_listener);
	layer_surface->surface_destroy_listener.notify =
		handle_layer_surface_destroy;
	wl_signal_add(&layer_surface->surface->events.commit,
		&layer_surface->surface_commit_listener);
	layer_surface->surface_commit_listener.notify =
		handle_layer_surface_commit;
	wl_signal_add(&layer_surface->output->events.destroy,
		&layer_surface->output_destroy_listener);
	layer_surface->output_destroy_listener.notify =
		handle_layer_surface_output_destroy;

	bool inserted = false;
	struct wlr_layer_surface *ls;
	wl_list_for_each_reverse(ls, &surface_layers->surfaces, link) {
		if (ls->layer <= layer_surface->layer) {
			inserted = true;
			wl_list_insert(&ls->link, &layer_surface->link);
			break;
		}
	}
	if (!inserted) {
		wl_list_insert(&surface_layers->surfaces, &layer_surface->link);
	}

	wl_signal_emit(&surface_layers->events.new_surface, layer_surface);
}

static const struct surface_layers_interface surface_layers_impl = {
	.get_layer_surface = surface_layers_get_layer_surface,
};

static void surface_layers_bind(struct wl_client *wl_client,
		void *_surface_layers, uint32_t version, uint32_t id) {
	struct wlr_surface_layers *surface_layers = _surface_layers;
	assert(wl_client && surface_layers);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&surface_layers_interface, version, id);
	wl_resource_set_implementation(wl_resource, &surface_layers_impl,
		surface_layers, NULL);
}

struct wlr_surface_layers *wlr_surface_layers_create(
		struct wl_display *display) {
	struct wlr_surface_layers *surface_layers =
		calloc(1, sizeof(struct wlr_surface_layers));
	if (!surface_layers) {
		return NULL;
	}

	struct wl_global *wl_global = wl_global_create(display,
		&surface_layers_interface, 1, surface_layers, surface_layers_bind);
	if (!wl_global) {
		free(surface_layers);
		return NULL;
	}
	surface_layers->wl_global = wl_global;

	wl_list_init(&surface_layers->surfaces);

	wl_signal_init(&surface_layers->events.new_surface);

	return surface_layers;
}

void wlr_surface_layers_destroy(struct wlr_surface_layers *surface_layers) {
	if (!surface_layers) {
		return;
	}
	struct wlr_layer_surface *layer_surface, *tmp;
	wl_list_for_each_safe(layer_surface, tmp, &surface_layers->surfaces, link) {
		layer_surface_destroy(layer_surface);
	}
	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(surface_layers->wl_global);
	free(surface_layers);
}
