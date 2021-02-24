#include <gbm.h>
#include <libliftoff.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

struct atomic {
	drmModeAtomicReq *req;
	bool failed;
};

static void atomic_begin(struct atomic *atom) {
	memset(atom, 0, sizeof(*atom));

	atom->req = drmModeAtomicAlloc();
	if (!atom->req) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		atom->failed = true;
		return;
	}
}

static bool atomic_commit(struct atomic *atom,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_drm_backend *drm = conn->backend;
	if (atom->failed) {
		return false;
	}

	int ret = drmModeAtomicCommit(drm->fd, atom->req, flags, drm);
	if (ret) {
		wlr_drm_conn_log_errno(conn, WLR_ERROR, "Atomic %s failed (%s)",
			(flags & DRM_MODE_ATOMIC_TEST_ONLY) ? "test" : "commit",
			(flags & DRM_MODE_ATOMIC_ALLOW_MODESET) ? "modeset" : "pageflip");
		return false;
	}

	return true;
}

static void atomic_finish(struct atomic *atom) {
	drmModeAtomicFree(atom->req);
}

static void atomic_add(struct atomic *atom, uint32_t id, uint32_t prop, uint64_t val) {
	if (!atom->failed && drmModeAtomicAddProperty(atom->req, id, prop, val) < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to add atomic DRM property");
		atom->failed = true;
	}
}

static bool create_mode_blob(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, uint32_t *blob_id) {
	if (!crtc->pending.active) {
		*blob_id = 0;
		return true;
	}

	if (drmModeCreatePropertyBlob(drm->fd, &crtc->pending.mode->drm_mode,
			sizeof(drmModeModeInfo), blob_id)) {
		wlr_log_errno(WLR_ERROR, "Unable to create mode property blob");
		return false;
	}

	return true;
}

static bool create_gamma_lut_blob(struct wlr_drm_backend *drm,
		size_t size, const uint16_t *lut, uint32_t *blob_id) {
	if (size == 0) {
		*blob_id = 0;
		return true;
	}

	struct drm_color_lut *gamma = malloc(size * sizeof(struct drm_color_lut));
	if (gamma == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate gamma table");
		return false;
	}

	const uint16_t *r = lut;
	const uint16_t *g = lut + size;
	const uint16_t *b = lut + 2 * size;
	for (size_t i = 0; i < size; i++) {
		gamma[i].red = r[i];
		gamma[i].green = g[i];
		gamma[i].blue = b[i];
	}

	if (drmModeCreatePropertyBlob(drm->fd, gamma,
			size * sizeof(struct drm_color_lut), blob_id) != 0) {
		wlr_log_errno(WLR_ERROR, "Unable to create gamma LUT property blob");
		free(gamma);
		return false;
	}
	free(gamma);

	return true;
}

static void commit_blob(struct wlr_drm_backend *drm,
		uint32_t *current, uint32_t next) {
	if (*current == next) {
		return;
	}
	if (*current != 0) {
		drmModeDestroyPropertyBlob(drm->fd, *current);
	}
	*current = next;
}

static void rollback_blob(struct wlr_drm_backend *drm,
		uint32_t *current, uint32_t next) {
	if (*current == next) {
		return;
	}
	if (next != 0) {
		drmModeDestroyPropertyBlob(drm->fd, next);
	}
}

static void plane_disable(struct atomic *atom, struct wlr_drm_plane *plane) {
	struct liftoff_layer *layer = plane->liftoff_layer;
	liftoff_layer_set_property(layer, "FB_ID", 0);
}

static void set_plane_props(struct atomic *atom, struct wlr_drm_backend *drm,
		struct wlr_drm_plane *plane, uint32_t crtc_id, int32_t x, int32_t y) {
	struct wlr_drm_fb *fb = plane_get_next_fb(plane);
	if (fb == NULL) {
		wlr_log(WLR_ERROR, "Failed to acquire FB");
		atom->failed = true;
		return;
	}

	uint32_t width = gbm_bo_get_width(fb->bo);
	uint32_t height = gbm_bo_get_height(fb->bo);

	// The src_* properties are in 16.16 fixed point
	struct liftoff_layer *layer = plane->liftoff_layer;
	liftoff_layer_set_property(layer, "SRC_X", 0);
	liftoff_layer_set_property(layer, "SRC_Y", 0);
	liftoff_layer_set_property(layer, "SRC_W", (uint64_t)width << 16);
	liftoff_layer_set_property(layer, "SRC_H", (uint64_t)height << 16);
	liftoff_layer_set_property(layer, "CRTC_X", (uint64_t)x);
	liftoff_layer_set_property(layer, "CRTC_Y", (uint64_t)y);
	liftoff_layer_set_property(layer, "CRTC_W", width);
	liftoff_layer_set_property(layer, "CRTC_H", height);
	liftoff_layer_set_property(layer, "FB_ID", fb->id);
}

static bool atomic_crtc_commit(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_output *output = &conn->output;
	struct wlr_drm_crtc *crtc = conn->crtc;

	uint32_t mode_id = crtc->mode_id;
	if (crtc->pending_modeset) {
		if (!create_mode_blob(drm, crtc, &mode_id)) {
			return false;
		}
	}

	uint32_t gamma_lut = crtc->gamma_lut;
	if (output->pending.committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		// Fallback to legacy gamma interface when gamma properties are not
		// available (can happen on older Intel GPUs that support gamma but not
		// degamma).
		if (crtc->props.gamma_lut == 0) {
			if (!drm_legacy_crtc_set_gamma(drm, crtc,
					output->pending.gamma_lut_size,
					output->pending.gamma_lut)) {
				return false;
			}
		} else {
			if (!create_gamma_lut_blob(drm, output->pending.gamma_lut_size,
					output->pending.gamma_lut, &gamma_lut)) {
				return false;
			}
		}
	}

	bool prev_vrr_enabled =
		output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED;
	bool vrr_enabled = prev_vrr_enabled;
	if ((output->pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED) &&
			drm_connector_supports_vrr(conn)) {
		vrr_enabled = output->pending.adaptive_sync_enabled;
	}

	if (crtc->pending_modeset) {
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	} else {
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	}

	struct atomic atom;
	atomic_begin(&atom);
	atomic_add(&atom, conn->id, conn->props.crtc_id,
		crtc->pending.active ? crtc->id : 0);
	if (crtc->pending_modeset && crtc->pending.active &&
			conn->props.link_status != 0) {
		atomic_add(&atom, conn->id, conn->props.link_status,
			DRM_MODE_LINK_STATUS_GOOD);
	}
	atomic_add(&atom, crtc->id, crtc->props.mode_id, mode_id);
	atomic_add(&atom, crtc->id, crtc->props.active, crtc->pending.active);
	if (crtc->pending.active) {
		if (crtc->props.gamma_lut != 0) {
			atomic_add(&atom, crtc->id, crtc->props.gamma_lut, gamma_lut);
		}
		if (crtc->props.vrr_enabled != 0) {
			atomic_add(&atom, crtc->id, crtc->props.vrr_enabled, vrr_enabled);
		}
		set_plane_props(&atom, drm, crtc->primary, crtc->id, 0, 0);
		if (crtc->cursor) {
			if (drm_connector_is_cursor_visible(conn)) {
				set_plane_props(&atom, drm, crtc->cursor, crtc->id,
					conn->cursor_x, conn->cursor_y);
			} else {
				plane_disable(&atom, crtc->cursor);
			}
		}
	} else {
		plane_disable(&atom, crtc->primary);
		if (crtc->cursor) {
			plane_disable(&atom, crtc->cursor);
		}
	}

	bool ok = liftoff_output_apply(crtc->liftoff, atom.req, flags);
	ok = ok && atomic_commit(&atom, conn, flags);
	atomic_finish(&atom);

	if (crtc->pending.active) {
		ok = ok && liftoff_layer_get_plane_id(crtc->primary->liftoff_layer) != 0;
		if (crtc->cursor && drm_connector_is_cursor_visible(conn)) {
			ok = ok && liftoff_layer_get_plane_id(crtc->cursor->liftoff_layer) != 0;
		}
	}

	if (ok && !(flags & DRM_MODE_ATOMIC_TEST_ONLY)) {
		commit_blob(drm, &crtc->mode_id, mode_id);
		commit_blob(drm, &crtc->gamma_lut, gamma_lut);

		if (vrr_enabled != prev_vrr_enabled) {
			output->adaptive_sync_status = vrr_enabled ?
				WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED :
				WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED;
			wlr_drm_conn_log(conn, WLR_DEBUG, "VRR %s",
				vrr_enabled ? "enabled" : "disabled");
		}
	} else {
		rollback_blob(drm, &crtc->mode_id, mode_id);
		rollback_blob(drm, &crtc->gamma_lut, gamma_lut);
	}

	return ok;
}

const struct wlr_drm_interface atomic_iface = {
	.crtc_commit = atomic_crtc_commit,
};
