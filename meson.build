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
    dependency('coop', version : ['>=1.3.0', '<1.4.0'])
]

executable(
    'swaybarpp',
    files(
        'src/bar/main.cpp',
        'src/bar/mod.cpp',
        'src/bar/mods/battery.cpp',
        'src/bar/mods/cpu-usage.cpp',
        'src/bar/mods/datetime.cpp',
        'src/bar/mods/file.cpp',
        'src/bar/mods/loadavg.cpp',
        'src/bar/mods/network.cpp',
        'src/bar/mods/padding.cpp',
        'src/bar/mods/pipewire.cpp',
        'src/bar/mods/ram-usage.cpp',
        'src/bar/mods/temp.cpp',
        'src/bar/mods/workspace.cpp',
        'src/bar/sway-ipc.cpp',
        'src/bar/window.cpp',
        'src/cairo-util.cpp',
        'src/shm-buffer.cpp',
        'src/volume.cpp',
    ) + towl_files + tinyjson_files,
    dependencies: deps,
    install: true,
)

slider_files = files(
    'src/cairo-util.cpp',
    'src/shm-buffer.cpp',
    'src/slider/app.cpp',
    'src/slider/window.cpp',
)

executable(
    'swaybarpp-volume',
    files(
        'src/volume.cpp',
        'src/slider/volume-slider.cpp',
    ) + slider_files + towl_files + tinyjson_files,
    dependencies: deps,
    install: true,
)

executable(
    'swaybarpp-brightness',
    files(
        'src/slider/brightness-slider.cpp',
    ) + slider_files + towl_files,
    dependencies: deps,
    install: true,
)

subdir('submodules/crypto-utils/src')  # crypto_utils_deps

executable(
    'swaybarpp-lock',
    files(
        'src/cairo-util.cpp',
        'src/crypto/sha.cpp',
        'src/lock/main.cpp',
        'src/lock/passwd-file.cpp',
        'src/lock/window.cpp',
        'src/shm-buffer.cpp',
    ) + towl_files + tinyjson_files,
    dependencies: deps + crypto_utils_deps,
    install: true,
)

executable(
    'swaybarpp-lock-passwd',
    files(
        'src/crypto/sha.cpp',
        'src/lock/passwd-file.cpp',
        'src/lock/passwd.cpp',
    ),
    dependencies: crypto_utils_deps,
    install: true,
)
