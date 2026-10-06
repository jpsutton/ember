# M0 spike 1: mpv's video plane under a transparent Qt Quick window.
qt_add_executable(ember-plane-spike main.cpp)
qt_add_qml_module(ember-plane-spike
  URI EmberPlaneSpike
  VERSION 1.0
  QML_FILES Main.qml
)
target_link_libraries(ember-plane-spike PRIVATE Qt6::Gui Qt6::Quick ember_playerplugin)
