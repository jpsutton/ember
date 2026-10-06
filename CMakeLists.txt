cmake_minimum_required(VERSION 3.28)
project(ember VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GuiPrivate only for the QPA header that hands out a window's wl_surface;
# see src/player/plane/README.md.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui GuiPrivate Quick)
qt_standard_project_setup(REQUIRES 6.8)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl egl)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

add_subdirectory(src/player)
add_subdirectory(spikes/plane)
