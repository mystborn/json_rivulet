project('json_stream', 'c',
        version : '1.0.0',
        default_options : ['warning_level=3', 'c_std=c2x'])

# These arguments are only used to build the shared library
# not the executables that use the library.
test_args = ['-Wno-gnu-zero-variadic-macro-arguments']
lib_args = test_args + ['-DBUILDING_MESON_LIBRARY']
headers = include_directories('include')
test_headers = include_directories(['include', 'tests'])
sources = [
#    'src/bit_stack.c',
    'src/bit_stack2.c',
    'src/json_stream.c',
]

check_dep = dependency('check')
cjson_dep = dependency('libcjson')

json_stream_lib = shared_library(
  'json_stream',
  sources,
  include_directories: headers,
  install: true,
  c_args: lib_args)

subdir('tests')