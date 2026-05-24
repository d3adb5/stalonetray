test_settings = executable(
  'test_settings',
  ['test_settings.c'] + core_sources,
  c_args: build_args,
  dependencies: _dependencies + [cmocka_dep],
)

test('settings', test_settings)
