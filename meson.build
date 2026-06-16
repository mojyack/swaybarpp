project(
    'swaybarpp',
    ['cpp', 'c'],
    version: '1.0.0',
    default_options: ['warning_level=3', 'werror=false', 'cpp_std=c++23'],
    meson_version: '>=1.1',
)
add_project_arguments('-Wfatal-errors', language: 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

subdir('submodules/towl/src')  # towl_files, towl_deps
subdir('submodules/tinyjson/src')  # tinyjson_files

deps = towl_deps + [
    dependency('cairo'),
    dependency('pangocairo'),
    dependency('libpipewire-0.3'),
]

executable(
    'swaybarpp',
    files(
        'src/main.cpp',
        'src/mod.cpp',
        'src/mods/battery.cpp',
        'src/mods/cpu_usage.cpp',
        'src/mods/datetime.cpp',
        'src/mods/file.cpp',
        'src/mods/loadavg.cpp',
        'src/mods/network.cpp',
        'src/mods/padding.cpp',
        'src/mods/pipewire.cpp',
        'src/mods/ram_usage.cpp',
        'src/mods/temp.cpp',
        'src/mods/workspace.cpp',
        'src/sway_ipc.cpp',
        'src/window.cpp',
    ) + towl_files + tinyjson_files,
    dependencies: deps,
    install: true,
)
