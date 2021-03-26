wlr_files += files('backend.c')

backends = get_option('backends')

foreach backend : ['drm', 'noop', 'headless', 'libinput', 'x11', 'wayland']
  if backend in backends or 'auto' in backends
    subdir(backend)
  endif
endforeach

subdir('multi')

subdir('session')
