project('wlchewing',
  'c',
  license: 'MIT',
  version: '0.2.1',
)

wl_client = dependency('wayland-client')
wl_protocols = dependency('wayland-protocols')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
chewing = dependency('chewing')
xkbcommon = dependency('xkbcommon')
systemd = dependency('libsystemd')
cc = meson.get_compiler('c')
rt = cc.find_library('rt', required: false)
scanner = find_program('wayland-scanner')
scanner_private_code = generator(scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_client_header = generator(scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'])

wl_protocols_dir = wl_protocols.get_pkgconfig_variable('pkgdatadir')
protocols = [
  'protocols' / 'input-method-unstable-v2.xml',
  wl_protocols_dir / 'unstable' / 'text-input' /'text-input-unstable-v3.xml',
  'protocols' / 'virtual-keyboard-unstable-v1.xml',
  'protocols' / 'wlr-layer-shell-unstable-v1.xml',
  wl_protocols_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
]
lib_protocols = static_library('protocols',
  scanner_private_code.process(protocols), dependencies: [wl_client])
protocols_dep = declare_dependency(link_with: lib_protocols,
  sources: scanner_client_header.process(protocols))

sources = [
  'bottom-panel.c',
  'buffer.c',
  'config.c',
  'im.c',
  'main.c',
  'sni.c',
]

executable('wlchewing', sources,
  dependencies: [
    chewing,
    xkbcommon,
    cairo,
    pangocairo,
    protocols_dep,
    rt,
    systemd,
  ], install: true)

install_data(
  ['icons' / 'wlchewing-bopomofo.svg', 'icons' / 'wlchewing-eng.svg'],
  install_dir : 'share' / 'icons' / 'hicolor' / 'scalable' / 'apps')
