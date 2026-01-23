project('imgui', 'cpp',
  version: '1.91.4',
  default_options: [
    'cpp_std=c++17',
    'warning_level=0',
  ],
)

sdl3_dep = dependency('sdl3', required: true)

imgui_inc = include_directories('.', 'backends')

imgui_sources = files(
  'imgui.cpp',
  'imgui_draw.cpp',
  'imgui_tables.cpp',
  'imgui_widgets.cpp',
  'backends/imgui_impl_sdl3.cpp',
  'backends/imgui_impl_sdlrenderer3.cpp',
)

imgui_lib = static_library('imgui',
  imgui_sources,
  include_directories: imgui_inc,
  dependencies: [sdl3_dep],
  pic: true,
)

imgui_dep = declare_dependency(
  link_with: imgui_lib,
  include_directories: imgui_inc,
  dependencies: [sdl3_dep],
)
