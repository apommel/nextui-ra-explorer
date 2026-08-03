project('ra-explorer-pak', 'c',
  version: '1.0.0',
  default_options: [
    'warning_level=2',
    'werror=true'
  ]
)

# Find dependencies
apostrophe_dep = dependency('apostrophe', required: true)
curl_dep = dependency('libcurl', required: true,
  default_options: ['tool=disabled', 'tests=disabled', 'unittests=disabled'])
cjson_dep = dependency('libcjson', required: true,
  default_options: ['tests=false'])
# libm is part of libc on macOS but separate on Linux
m_dep = meson.get_compiler('c').find_library('m', required: false)

inc_dir = include_directories('src')

# Source files
sources = files(
  'src/app/main.c',
  'src/app/icon_loader.c',
  'src/app/settings.c',
  'src/app/views.c',
  'src/ra_api/response_cache.c',
  'src/ra_api/client.c',
  'src/ra_api/game.c',
  'src/ra_api/images.c',
  'src/ra_api/user.c',
  'src/util/file.c',
  'src/util/paths.c',
)

# Executable
executable('ra-explorer',
  sources,
  include_directories: inc_dir,
  dependencies: [apostrophe_dep, curl_dep, cjson_dep, m_dep],
  install: true
)
