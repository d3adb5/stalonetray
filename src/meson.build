# Everything but main.c: linked into both the stalonetray binary and the
# unit-test binaries so we don't get two definitions of main(). Use files()
# so the resulting File objects resolve regardless of the meson.build that
# consumes them (tests/meson.build pulls these in too).
core_sources = files(
  'debug.c',
  'drag.c',
  'embed.c',
  'icons.c',
  'image.c',
  'layout.c',
  'order.c',
  'scrollbars.c',
  'settings.c',
  'tray.c',
  'wmh.c',
  'xembed.c',
  'xinerama.c',
  'xutils.c',
)

if get_option('native_kde').enabled()
  core_sources += files('kde_tray.c')
endif

project_sources += core_sources + [files('main.c')]
