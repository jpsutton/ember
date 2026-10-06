# wp_viewporter client code, generated from the system's wayland-protocols.
set(VIEWPORTER_XML "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
set(PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTOCOL_DIR}")
add_custom_command(
  OUTPUT "${PROTOCOL_DIR}/viewporter-client-protocol.h"
  COMMAND "${WAYLAND_SCANNER}" client-header "${VIEWPORTER_XML}" "${PROTOCOL_DIR}/viewporter-client-protocol.h"
  DEPENDS "${VIEWPORTER_XML}")
add_custom_command(
  OUTPUT "${PROTOCOL_DIR}/viewporter-protocol.c"
  COMMAND "${WAYLAND_SCANNER}" private-code "${VIEWPORTER_XML}" "${PROTOCOL_DIR}/viewporter-protocol.c"
  DEPENDS "${VIEWPORTER_XML}")

qt_add_library(ember_player STATIC)
qt_add_qml_module(ember_player
  URI Ember.Player
  VERSION 1.0
  SOURCES
    MpvVideo.h MpvVideo.cpp
    plane/MpvCore.h plane/MpvCore.cpp
    plane/PlaneRenderExecutor.h plane/PlaneRenderExecutor.cpp
    plane/WaylandVideoPlane.h plane/WaylandVideoPlane.cpp
    plane/mpv_player_common.h
    "${PROTOCOL_DIR}/viewporter-client-protocol.h"
    "${PROTOCOL_DIR}/viewporter-protocol.c"
)
target_include_directories(ember_player PRIVATE "${PROTOCOL_DIR}")
target_compile_options(ember_player PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
target_link_libraries(ember_player
  PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick PkgConfig::MPV PkgConfig::WAYLAND)
