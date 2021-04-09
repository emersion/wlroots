renderers = get_option('renderers')
egl = dependency('egl', required: 'gles2' in renderers)

wlr_files += files(
	'allocator.c',
	'dmabuf.c',
	'drm_format_set.c',
	'gbm_allocator.c',
	'pixel_format.c',
	'swapchain.c',
	'wlr_renderer.c',
	'wlr_texture.c',
)

if egl.found()
	wlr_deps += egl
	wlr_files += files('egl.c')
endif

if 'gles2' in renderers or 'auto' in renderers
	subdir('gles2')
endif
