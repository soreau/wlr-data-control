project(
	'wlr-data-control',
	'c',
)

wayland_client = dependency('wayland-client')

subdir('protocols')

sources = [
	'src/main.c'
]

dependencies = [
	wayland_client,
	client_protos,
]

executable(
	'wlr-data-control-demo',
	'main.c',
	dependencies: dependencies,
	install: true,
)
