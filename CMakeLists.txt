cmake_minimum_required(VERSION 3.28)
project(ember VERSION 0.1.15 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GuiPrivate only for the QPA header that hands out a window's wl_surface;
# see src/player/plane/README.md.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui GuiPrivate Quick Network DBus WebSockets)
find_package(KF6Config 6.0 REQUIRED)
find_package(KF6CoreAddons 6.0 REQUIRED)
find_package(KF6DBusAddons 6.0 REQUIRED)
find_package(KF6WindowSystem 6.0 REQUIRED)
find_package(QCoro6 0.10 REQUIRED COMPONENTS Core Network)
qcoro_enable_coroutines()
qt_standard_project_setup(REQUIRES 6.8)
# QML modules land under build/qml/<URI path>, where qmllint and the engine
# expect them.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl egl)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

option(EMBER_BUILD_TESTS "Build the unit tests" ON)
option(EMBER_BUILD_DEV_TOOLS "Build the plane spike and the screenshot tool" OFF)

add_subdirectory(src/player)
add_subdirectory(src)

if(EMBER_BUILD_DEV_TOOLS)
  add_subdirectory(spikes/plane)
  add_subdirectory(tools/ember-shot)
endif()

if(EMBER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
