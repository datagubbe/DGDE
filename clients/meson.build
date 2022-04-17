project(
  'dgde-clients',
  'c',
  default_options: [
    'c_std=c11',
  ]
)

wayland = dependency('wayland-client')
wayland_egl = dependency('wayland-egl')
wayland_protocols = dependency('wayland-protocols')
egl = dependency('egl')
gles = dependency('glesv2')

protocols_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')
xdg_shell_header = custom_target(
  'xdg-shell-protocol.h',
  input: join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml'),
  output: 'xdg-shell-protocol.h',
  command: ['wayland-scanner', 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_impl = custom_target(
  'xdg-shell-protocol.c',
  input: join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml'),
  output: 'xdg-shell-protocol.c',
  command: ['wayland-scanner', 'private-code', '@INPUT@', '@OUTPUT@'],
)

executable(
  'color',
  [
    'src/color.c',
    xdg_shell_header,
    xdg_shell_impl
  ],
  dependencies: [wayland, egl, gles, wayland_egl],
  install: true
)
