project('json_stream', 'c',
        version : '1.0.0',
        default_options : ['warning_level=3', 'c_std=c2x'])

# These arguments are only used to build the shared library
# not the executables that use the library.
lib_args = ['-DBUILDING_MESON_LIBRARY']
headers = include_directories('include')
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

stream_test_sources = [
    'tests/json_test_buffered.c',
    'tests/json_test_core.c',
    'tests/json_test_files.c',
    'tests/json_tests.c',
    'tests/json_util_compare.c',
    'tests/json_util_file.c'
]

executable('json_tests', stream_test_sources,
    include_directories: include_directories(['include', 'tests']),
    c_args: lib_args,
    link_with: json_stream_lib,
    dependencies: [check_dep, cjson_dep]
)

subdir('tests')