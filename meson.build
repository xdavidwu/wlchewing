project('wlchewing',
  'c',
  license: 'MIT',
  version: '0.2.2',
  meson_version: '>=1.8.0',
)

wl_client = dependency('wayland-client')
wl_mod = import('wayland')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
chewing = dependency('chewing')
xkbcommon = dependency('xkbcommon')
systemd = dependency('libsystemd')
cc = meson.get_compiler('c')
rt = cc.find_library('rt', required: false)

protocols = [
  'protocols' / 'input-method-unstable-v2.xml',
  wl_mod.find_protocol(
    'text-input',
    state: 'unstable',
    version: 3,
  ),
  'protocols' / 'virtual-keyboard-unstable-v1.xml',
  'protocols' / 'wlr-layer-shell-unstable-v1.xml',
  wl_mod.find_protocol('xdg-shell'),
]
protocols_sources = wl_mod.scan_xml(protocols, client: true, server: false)

sources = [
  protocols_sources,
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
    rt,
    systemd,
    wl_client,
  ], install: true)

install_data(
  ['icons' / 'wlchewing-bopomofo.svg', 'icons' / 'wlchewing-eng.svg'],
  install_dir : 'share' / 'icons' / 'hicolor' / 'scalable' / 'apps')
