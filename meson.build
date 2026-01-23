project('ic', 'c',
  version: '1.0.0',
  license: 'Apache-2.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'buildtype=debug',
  ],
)

# Project description
project_description = 'Image Comic Viewer - A lightweight, fast comic book reader for Linux'

# Compiler setup
cc = meson.get_compiler('c')

# Add feature test macros for POSIX functions
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

# Dependencies
sdl3_dep = dependency('sdl3', required: true)
sdl3_ttf_dep = dependency('SDL3_ttf', required: true)
libzip_dep = dependency('libzip', required: true)

# FreeImage may not have pkg-config, try to find it as a library
freeimage_dep = dependency('freeimage', required: false)
if not freeimage_dep.found()
  freeimage_dep = cc.find_library('freeimage', required: true)
endif

m_dep = cc.find_library('m', required: true)

# Collect all dependencies
dependencies = [
  sdl3_dep,
  sdl3_ttf_dep,
  libzip_dep,
  freeimage_dep,
  m_dep,
]

# Include directories
inc_dirs = include_directories('include')

# Source files
sources = files(
  'src/clipboard.c',
  'src/comic_loaders_cbr.c',
  'src/comic_loaders_cbz.c',
  'src/comic_loaders_dir.c',
  'src/comic_loaders_pdf.c',
  'src/comic_loaders_utils.c',
  'src/comic_loaders.c',
  'src/comic_viewer.c',
  'src/edges.c',
  'src/file_browser.c',
  'src/image_loader.c',
  'src/image_processor.c',
  'src/main.c',
  'src/overlay.c',
  'src/process_utils.c',
  'src/progress_bar.c',
  'src/progress_indicator.c',
  'src/render.c',
  'src/upscale.c',
)

# Optional Dear ImGui overlay (C++).
imgui_opt = get_option('imgui')
if imgui_opt.enabled()
  add_languages('cpp', required: true, native: false)
  add_project_arguments('-DIC_WITH_IMGUI', language: 'c')
  add_project_arguments('-DIC_WITH_IMGUI', language: 'cpp')

  imgui_sp = subproject('imgui', required: true)
  imgui_dep = imgui_sp.get_variable('imgui_dep')

  sources += files('src/imgui_layer.cpp')
  dependencies += [imgui_dep]
endif

# Executable
executable('ic',
  sources,
  include_directories: inc_dirs,
  dependencies: dependencies,
  install: true,
)

# Install desktop file
install_data('ic.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications')
)

# Install icon
install_data('ic.png',
  install_dir: join_paths(get_option('datadir'), 'pixmaps')
)

# Install headers (optional, if you want to make this a library later)
# install_headers(
#   'include/comic_viewer.h',
#   'include/comic_loaders.h',
#   # ... other headers
#   subdir: 'ic'
# )

# Summary
summary({
  'prefix': get_option('prefix'),
  'bindir': get_option('bindir'),
  'Build type': get_option('buildtype'),
}, section: 'Directories')

summary({
  'SDL3': sdl3_dep.found(),
  'SDL3_ttf': sdl3_ttf_dep.found(),
  'libzip': libzip_dep.found(),
  'FreeImage': freeimage_dep.found(),
}, section: 'Dependencies', bool_yn: true)
