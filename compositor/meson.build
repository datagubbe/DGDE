project(
  'dgde-compositor',
  'c',
  default_options: [
    'c_std=c11',
    'werror=true'
  ]
)

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
  'dgde',
  [
    'src/main.c',
    'src/cursor.c',
    'src/view.c',
    'src/server.c',
    'src/keyboard.c',
    'src/workspace/workspace.c',
    'src/workspace/view_tree.c',
    'src/decorations.c',
    xdg_shell_header,
  ],
  include_directories: include_directories('src'),
  dependencies: [wlroots, wayland, libudev, pixman, xkbcommon],
  c_args: '-DWLR_USE_UNSTABLE',
  install: true
)
