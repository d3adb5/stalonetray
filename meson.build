project(
  'stalonetray',
  'c',
  version: '1.0.0',
  default_options: ['warning_level=3', 'c_std=gnu23'],
  meson_version: '>=1.1.0',
)

build_args = [
  '-O2',
  '-Wno-missing-braces',
  '-DPROGNAME="@0@"'.format(meson.project_name()),
  '-DVERSION="@0@"'.format(meson.project_version()),
]

project_sources = []

# == Dependencies & Optional Features ==========================================

dependencies = {
  'x11': dependency('x11', required: true),
  'xinerama': dependency('xinerama', required: get_option('xinerama')),
  'xpm': dependency('xpm', required: get_option('xpm')),
}

assert(dependencies['x11'].found(), 'Xlib is required to build stalonetray')

# == Sources & Headers =========================================================

subdir('src')

# == Build Options =============================================================

feature_list = []

if get_option('xinerama').enabled()
  build_args += ['-D_ST_WITH_XINERAMA']
  feature_list += ['xinerama']
endif

if get_option('xpm').enabled()
  build_args += ['-D_ST_WITH_XPM']
  feature_list += ['xpm']
endif

if get_option('native_kde').enabled()
  build_args += ['-D_ST_WITH_NATIVE_KDE']
  feature_list += ['native_kde']
endif

if get_option('trace_events')
  build_args += ['-D_ST_TRACE_EVENTS']
  feature_list += ['trace_events']
endif

if get_option('delay_embedding_confirmation')
  build_args += ['-D_ST_DELAY_EMBEDDING_CONFIRMATION']
  feature_list += ['delay_embedding_confirmation']
endif

if get_option('dump_window_information')
  build_args += ['-D_ST_DUMP_WINDOW_INFORMATION']
  feature_list += ['dump_window_information']
endif

if get_option('exit_gracefully')
  build_args += ['-D_ST_EXIT_GRACEFULLY']
  feature_list += ['exit_gracefully']
endif

build_args += ['-DFEATURE_LIST="@0@"'.format(' '.join(feature_list))]

# == Targets ===================================================================

# TODO: Replace this with dict.values() when meson 1.10 is released.
_dependencies = []

foreach _, dep : dependencies
  _dependencies += [dep]
endforeach

executable(
  'stalonetray',
  project_sources,
  c_args: build_args,
  dependencies: _dependencies,
  install: true,
)

# == Documentation =============================================================

gen_manpage = find_program('generate-manpage.sh')

custom_target(
  'manpage',
  input: 'stalonetray.xml.in',
  output: 'stalonetray.1',
  command: [gen_manpage, '@INPUT@', '@OUTPUT@', meson.project_version()],
  install: true,
  install_dir: join_paths(get_option('prefix'), get_option('mandir'), 'man1'),
)
