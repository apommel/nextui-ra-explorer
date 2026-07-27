project('ra-explorer-pak', 'c',
  version: '0.1.0',
  default_options: [
    'warning_level=2',
    'werror=true'
  ]
)

# Find dependencies
apostrophe_dep = dependency('apostrophe', required: true)
curl_dep = dependency('libcurl', required: true)
cjson_dep = dependency('libcjson', required: true)

inc_dir = include_directories('src')

# Source files
sources = files(
  'src/app/main.c',
  'src/app/settings.c',
  'src/app/views.c',
  'src/ra_api/client.c',
  'src/ra_api/images.c',
  'src/ra_api/user.c',
  'src/util/paths.c',
)

# Executable
executable('ra-explorer',
  sources,
  include_directories: inc_dir,
  dependencies: [apostrophe_dep, curl_dep, cjson_dep],
  install: true
)
