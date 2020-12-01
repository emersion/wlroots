#ifndef BACKEND_H
#define BACKEND_H

#include <wlr/backend.h>

int backend_get_drm_fd(struct wlr_backend *backend);
struct wlr_allocator *backend_get_allocator(struct wlr_backend *backend);

#endif
