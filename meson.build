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

# Dear ImGui overlay is always enabled (requires a C++ compiler).
add_languages('cpp', required: true, native: false)

# Add feature test macros for POSIX functions
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

# Dependencies
sdl3_dep = dependency('sdl3', required: true)
sdl3_image_dep = dependency('SDL3_image', required: false)
sdl3_ttf_dep = dependency('SDL3_ttf', required: true)
libzip_dep = dependency('libzip', required: true)

# Optional image backends
freeimage_dep = dependency('freeimage', required: false)
if not freeimage_dep.found()
  freeimage_dep = cc.find_library('freeimage', required: false)
endif

image_backend_opt = get_option('image_backend')

if image_backend_opt == 'freeimage' and not freeimage_dep.found()
  error('image_backend=freeimage requested but FreeImage was not found')
endif

if image_backend_opt == 'sdl_image' and not sdl3_image_dep.found()
  error('image_backend=sdl_image requested but SDL3_image was not found')
endif

if image_backend_opt == 'auto' and not freeimage_dep.found() and not sdl3_image_dep.found()
  error('No image backend found. Install FreeImage and/or SDL3_image')
endif

# MuPDF (optional – enables the MuPDF PDF rendering backend)
mupdf_dep = dependency('mupdf', required: false)
if not mupdf_dep.found()
  mupdf_dep = cc.find_library('mupdf', required: false)
endif
mupdf_third_dep = cc.find_library('mupdf-third', required: false)

m_dep = cc.find_library('m', required: true)

# Collect all dependencies
dependencies = [
  sdl3_dep,
  sdl3_ttf_dep,
  libzip_dep,
  m_dep,
]

if freeimage_dep.found() and image_backend_opt != 'sdl_image'
  add_project_arguments('-DHAVE_FREEIMAGE', language: ['c', 'cpp'])
  dependencies += [freeimage_dep]
endif

if sdl3_image_dep.found()
  add_project_arguments('-DHAVE_SDL_IMAGE3', language: ['c', 'cpp'])
  dependencies += [sdl3_image_dep]
endif

if mupdf_dep.found()
  add_project_arguments('-DHAVE_MUPDF', language: ['c', 'cpp'])
  dependencies += [mupdf_dep]
  if mupdf_third_dep.found()
    dependencies += [mupdf_third_dep]
  endif
endif

# Include directories
inc_dirs = include_directories('include')

# Source files
sources = files(
  'src/clipboard.c',
  'src/comic_loaders_cbr.c',
  'src/comic_loaders_cbz.c',
  'src/comic_loaders_dir.c',
  'src/comic_loaders_pdf.c',
  'src/comic_loaders_pdf_poppler.c',
  'src/comic_loaders_pdf_mupdf.c',
  'src/comic_loaders_utils.c',
  'src/comic_loaders.c',
  'src/comic_viewer.c',
  'src/edges.c',
  'src/file_browser_imgui.cpp',
  'src/image_loader.c',
  'src/image_processor.c',
  'src/main.c',
  'src/overlay.c',
  'src/process_utils.c',
  'src/progress_bar.c',
  'src/progress_indicator.c',
  'src/render.c',
  'src/temp_utils.c',
  'src/upscale.c',
  'src/imgui_layer.cpp',
)

imgui_sp = subproject('imgui', required: true)
imgui_dep = imgui_sp.get_variable('imgui_dep')
dependencies += [imgui_dep]

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
  'SDL3_image': sdl3_image_dep.found(),
  'SDL3_ttf': sdl3_ttf_dep.found(),
  'libzip': libzip_dep.found(),
  'FreeImage': freeimage_dep.found(),
  'Image backend option': image_backend_opt,
  'MuPDF': mupdf_dep.found(),
}, section: 'Dependencies', bool_yn: true)
