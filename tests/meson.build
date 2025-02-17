fs = import('fs')

fs.copyfile('json_files/4KB.json', '4KB.json')
fs.copyfile('json_files/40KB.json', '40KB.json')
fs.copyfile('json_files/400B.json', '400B.json')
fs.copyfile('json_files/400KB.json', '400KB.json')
fs.copyfile('json_files/basic_json.json', 'basic_json.json')
fs.copyfile('json_files/basic_json_with_large_num.json', 'basic_json_with_large_num.json')
fs.copyfile('json_files/broad_tree.json', 'broad_tree.json')
fs.copyfile('json_files/deep_tree.json', 'deep_tree.json')
fs.copyfile('json_files/full_json_schema.json', 'full_json_schema.json')
fs.copyfile('json_files/full_json_schema_2.json', 'full_json_schema_2.json')
fs.copyfile('json_files/hello_world.json', 'hello_world.json')
fs.copyfile('json_files/lots_of_numbers.json', 'lots_of_numbers.json')
fs.copyfile('json_files/lots_of_strings.json', 'lots_of_strings.json')
fs.copyfile('json_files/project_lock.json', 'project_lock.json')
fs.copyfile('json_files/special_num_format.json', 'special_num_format.json')

stream_test_sources = [
    'json_test_buffered.c',
    'json_test_core.c',
    'json_test_files.c',
    'json_tests.c',
    'json_util_compare.c',
    'json_util_file.c'
]

json_tests = executable('json_tests', stream_test_sources,
           c_args: test_args,
           include_directories: test_headers,
           link_with: json_stream_lib,
           dependencies: [check_dep, cjson_dep]
)

test(
    'json_rivulet_tests',
    json_tests,
    workdir: meson.current_build_dir()
)