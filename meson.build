project('dgde', 'c')

wlroots = dependency('wlroots')
wayland = dependency('wayland-server')
wayland_protocols = dependency('wayland-protocols')
libudev = dependency('libudev')
pixman = dependency('pixman-1')
xkbcommon = dependency('xkbcommon')

protocols_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')
xdg_shell_header = custom_target(
  'xdg-shell-protocol.h',
  input: join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml'),
  output: 'xdg-shell-protocol.h',
  command: ['wayland-scanner', 'server-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'tinywl',
  ['src/main.c', xdg_shell_header],
  dependencies: [wlroots, wayland, libudev, pixman, xkbcommon],
  c_args: '-DWLR_USE_UNSTABLE',
  install: true
)
