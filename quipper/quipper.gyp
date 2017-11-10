{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'openssl',
        'protobuf',
      ],
    },
    'include_dirs': [
      'compat/cros',
      '.',
    ],
  },
  'targets': [
    {
      'target_name': 'common',
      'type': 'static_library',
      'sources': [
        'address_mapper.cc',
        'binary_data_utils.cc',
        'buffer_reader.cc',
        'buffer_writer.cc',
        'compat/log_level.cc',
        'data_reader.cc',
        'data_writer.cc',
        'dso.cc',
        'file_reader.cc',
        'file_utils.cc',
        'huge_page_deducer.cc',
        'perf_data_utils.cc',
        'perf_option_parser.cc',
        'perf_parser.cc',
        'perf_protobuf_io.cc',
        'perf_reader.cc',
        'perf_recorder.cc',
        'perf_serializer.cc',
        'perf_stat_parser.cc',
        'run_command.cc',
        'sample_info_reader.cc',
        'scoped_temp_path.cc',
        'string_utils.cc',
      ],
      'dependencies': [
        'perf_data_proto',
        'perf_stat_proto',
      ],
      'export_dependent_settings': [
        'perf_data_proto',
        'perf_stat_proto',
      ],
      'link_settings': {
        'libraries': ['-lelf'],
      },
    },
    {
      'target_name': 'conversion_utils',
      'type': 'static_library',
      'sources': [
        'conversion_utils.cc',
      ],
      'dependencies': [
        'common',
      ],
    },
    {
      'target_name': 'common_test',
      'type': 'static_library',
      'sources': [
        'dso_test_utils.cc',
        'perf_test_files.cc',
        'test_perf_data.cc',
        'test_utils.cc',
      ],
      'dependencies': [
        'common',
      ],
      'export_dependent_settings': [
        'common',
      ],
    },
    {
      'target_name': 'perf_data_proto',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '.',
        'proto_out_dir': 'include',
      },
      'sources': [
        '<(proto_in_dir)/perf_data.proto',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    {
      'target_name': 'perf_stat_proto',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '.',
        'proto_out_dir': 'include',
      },
      'sources': [
        '<(proto_in_dir)/perf_stat.proto',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    {
      'target_name': 'quipper',
      'type': 'executable',
      'dependencies': [
        'common',
      ],
      'sources': [
        'quipper.cc',
      ],
    },
    {
      'target_name': 'perf_converter',
      'type': 'executable',
      'dependencies': [
        'common',
        'conversion_utils',
      ],
      'sources': [
        'perf_converter.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'integration_tests',
          'type': 'executable',
          'dependencies': [
            'common',
            'common_test',
            'conversion_utils',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'conversion_utils_test.cc',
            'test_runner.cc',
          ],
        },
        {
          'target_name': 'perf_recorder_test',
          'type': 'executable',
          'dependencies': [
            'common',
            'common_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'perf_recorder_test.cc',
          ],
        },
        {
          'target_name': 'unit_tests',
          'type': 'executable',
          'dependencies': [
            'common',
            'common_test',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'address_mapper_test.cc',
            'binary_data_utils_test.cc',
            'buffer_reader_test.cc',
            'buffer_writer_test.cc',
            'dso_test.cc',
            'file_reader_test.cc',
            'huge_page_deducer_test.cc',
            'perf_data_utils_test.cc',
            'perf_option_parser_test.cc',
            'perf_parser_test.cc',
            'perf_reader_test.cc',
            'perf_serializer_test.cc',
            'perf_stat_parser_test.cc',
            'run_command_test.cc',
            'sample_info_reader_test.cc',
            'scoped_temp_path_test.cc',
            'test_runner.cc',
          ],
          'variables': {
            'deps': [
              'libcap',
            ],
          },
        },
      ],
    }],
  ],
}
