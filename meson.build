project('wlchewing-experiments',['c'])

wl_client = dependency('wayland-client')
wl_protocols = dependency('wayland-protocols')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
chewing = dependency('chewing')
xkbcommon = dependency('xkbcommon')
cc = meson.get_compiler('c')
rt = cc.find_library('rt', required: false)
scanner = find_program('wayland-scanner')
scanner_private_code = generator(scanner, output: '@BASENAME@-protocol.c', arguments: ['private-code', '@INPUT@', '@OUTPUT@'])
scanner_client_header = generator(scanner, output: '@BASENAME@-client-protocol.h', arguments: ['client-header', '@INPUT@', '@OUTPUT@'])

wl_protocols_dir = wl_protocols.get_pkgconfig_variable('pkgdatadir')
xdg_shell = wl_protocols_dir + '/stable/xdg-shell/xdg-shell.xml'
text_input_path = wl_protocols_dir + '/unstable/text-input/text-input-unstable-v3.xml'
protocols_src=[
  scanner_private_code.process('input-method-unstable-v2.xml'),
  scanner_private_code.process(text_input_path),
  scanner_private_code.process('wlr-layer-shell-unstable-v1.xml'),
  scanner_private_code.process(xdg_shell)
]
protocols_headers=[
  scanner_client_header.process('input-method-unstable-v2.xml'),
  scanner_client_header.process(text_input_path),
  scanner_client_header.process('wlr-layer-shell-unstable-v1.xml'),
  scanner_client_header.process(xdg_shell)
]
lib_protocols = static_library('protocols', protocols_src + protocols_headers, dependencies: wl_client)
protocols_dep = declare_dependency(link_with: lib_protocols, sources: protocols_headers)

executable('wlchewing', ['main.c', 'config.c', 'bottom-panel.c', 'buffer.c', 'im.c'], dependencies: [chewing, xkbcommon, cairo, pangocairo, protocols_dep, rt], install: true)
