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
add_project_arguments('-DIC_VERSION="' + meson.project_version() + '"', language: ['c', 'cpp'])

# Add feature test macros for POSIX functions
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

# Dependencies
sdl3_dep = dependency('sdl3', required: true)
sdl3_image_dep = dependency('SDL3_image', required: false, method: 'pkg-config')
if not sdl3_image_dep.found()
  sdl3_image_dep = cc.find_library('SDL3_image', required: false)
endif
imlib2_dep = dependency('imlib2', required: false)
sdl3_ttf_dep = dependency('SDL3_ttf', required: true)
libzip_dep = dependency('libzip', required: true)
gio_dep = dependency('gio-2.0', version: '>=2.72', required: true)
fontconfig_dep = dependency('fontconfig', required: true)
gtk_dep = dependency('gtk+-3.0', required: true)
add_project_arguments('-DIC_LOCALE_DIR="' + join_paths(get_option('prefix'), get_option('localedir')) + '"', language: ['c', 'cpp'])
subdir('po')

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

if image_backend_opt == 'imlib2' and not imlib2_dep.found()
  error('image_backend=imlib2 requested but Imlib2 was not found')
endif

if image_backend_opt == 'auto' and not freeimage_dep.found() and not sdl3_image_dep.found() and not imlib2_dep.found()
  error('No image backend found. Install FreeImage, SDL3_image, and/or Imlib2')
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
  gio_dep,
  fontconfig_dep,
  gtk_dep,
  m_dep,
]

if freeimage_dep.found() and image_backend_opt != 'sdl_image'
  add_project_arguments('-DHAVE_FREEIMAGE', language: ['c', 'cpp'])
  dependencies += [freeimage_dep]
  # Some distros (e.g. Arch jxrlib) ship libjxrglue.so without a NEEDED entry
  # for libjpegxr, so FreeImage's transitive link fails unless we add it.
  # --no-as-needed keeps it in DT_NEEDED so the runtime loader resolves it too.
  if cc.find_library('jpegxr', required: false).found()
    dependencies += [declare_dependency(
      link_args: '-Wl,--push-state,--no-as-needed,-ljpegxr,--pop-state')]
  endif
endif

if sdl3_image_dep.found()
  add_project_arguments('-DHAVE_SDL_IMAGE3', language: ['c', 'cpp'])
  dependencies += [sdl3_image_dep]
endif

if imlib2_dep.found()
  add_project_arguments('-DHAVE_IMLIB2', language: ['c', 'cpp'])
  dependencies += [imlib2_dep]
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
  'src/linux_desktop.c',
  'src/overlay.c',
  'src/pixel_shader.c',
  'src/gpu_shader.c',
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

# GPU pixel shaders: compile GLSL -> SPIR-V at build time and embed as a C header.
glslc_prog = find_program('glslc', required: false)
spv_embed_h = []
if glslc_prog.found()
  # [source path, output filename, shader stage flag]
  shader_specs = [
    ['shaders/fullscreen.vert.glsl', 'fullscreen.vert.spv', 'vertex'],
    ['shaders/invert.frag.glsl',     'invert.frag.spv',     'fragment'],
    ['shaders/scanlines.frag.glsl',  'scanlines.frag.spv',  'fragment'],
    ['shaders/vignette.frag.glsl',   'vignette.frag.spv',   'fragment'],
    ['shaders/sepia.frag.glsl',      'sepia.frag.spv',      'fragment'],
    ['shaders/pixelate.frag.glsl',   'pixelate.frag.spv',   'fragment'],
    ['shaders/mirror.frag.glsl',     'mirror.frag.spv',     'fragment']
  ]
  spv_targets = []
  foreach spec : shader_specs
    spv_targets += custom_target('spv_' + spec[1],
      input: spec[0],
      output: spec[1],
      command: [glslc_prog, '-fshader-stage=' + spec[2],
                '--target-env=vulkan1.2', '-O',
                '-o', '@OUTPUT@', '@INPUT@'])
  endforeach
  spv_embed_h = custom_target('gpu_shaders_embed_h',
    input: spv_targets,
    output: 'gpu_shaders_embed.h',
    command: [find_program('python3'), files('scripts/embed_spv.py'),
              '@OUTPUT@', '@INPUT@'])
  add_project_arguments('-DHAVE_GPU_SHADERS', language: ['c', 'cpp'])
endif

# Executable
ic_exe = executable('ic',
  sources,
  spv_embed_h,
  include_directories: inc_dirs,
  dependencies: dependencies,
  install: true,
)
test('cli-help', ic_exe, args: ['--help'])
test('cli-version', ic_exe, args: ['--version'])
foreach cli_case : [
  ['cli-invalid-monitor', ['--monitor', 'wrong']],
  ['cli-unknown-option', ['--unknown']],
  ['cli-extra-file', ['first.cbz', 'second.cbz']],
]
  if meson.version().version_compare('>=1.11.0')
    test(cli_case[0], ic_exe, args: cli_case[1], expected_fail: true)
  else
    test(cli_case[0], ic_exe, args: cli_case[1], should_fail: true)
  endif
endforeach

test_objects = []
excluded_test_sources = files('src/main.c', 'src/comic_viewer.c', 'src/clipboard.c')
foreach source : sources
  if source not in excluded_test_sources
    test_objects += ic_exe.extract_objects(source)
  endif
endforeach
safety_tests = executable('safety-tests',
  'tests/safety.c',
  objects: test_objects,
  include_directories: inc_dirs,
  dependencies: dependencies,
)
test('safety', safety_tests,
  env: ['SDL_VIDEODRIVER=dummy', 'IC_IMAGE_BACKEND=auto'], timeout: 60)
xvfb = find_program('xvfb-run', required: false)
if xvfb.found() and get_option('b_sanitize') == 'none'
  test('native-controls', xvfb, args: ['-a', safety_tests], depends: translations[0],
    env: ['SDL_VIDEODRIVER=dummy', 'IC_TEST_NATIVE=1', 'LANGUAGE=fr', 'LC_ALL=fr_FR.UTF-8',
          'IC_TEST_LOCALE_DIR=' + join_paths(meson.current_build_dir(), 'po')], timeout: 60)
endif
if freeimage_dep.found() and image_backend_opt != 'sdl_image' and sdl3_image_dep.found()
  test('safety-sdl-image', safety_tests,
    env: ['SDL_VIDEODRIVER=dummy', 'IC_IMAGE_BACKEND=sdl_image'], timeout: 60)
endif

# Install desktop file
install_data('ic.desktop',
  rename: 'io.github.mozvip.ic.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications')
)

# Install icon
image_tool = find_program('magick', 'convert', required: true)
custom_target('desktop-icon', input: 'ic.png', output: 'io.github.mozvip.ic.png',
  command: [image_tool, '@INPUT@', '-resize', '256x256', '-background', 'none',
            '-gravity', 'center', '-extent', '256x256', '@OUTPUT@'],
  install: true, install_dir: join_paths(get_option('datadir'), 'icons/hicolor/256x256/apps'))
install_data('io.github.mozvip.ic.metainfo.xml',
  install_dir: join_paths(get_option('datadir'), 'metainfo'))

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
  'Imlib2': imlib2_dep.found(),
  'SDL3_ttf': sdl3_ttf_dep.found(),
  'libzip': libzip_dep.found(),
  'FreeImage': freeimage_dep.found(),
  'Image backend option': image_backend_opt,
  'MuPDF': mupdf_dep.found(),
}, section: 'Dependencies', bool_yn: true)
