qt_add_executable(ember main.cpp)

kconfig_add_kcfg_files(ember GENERATE_MOC app/EmberSettings.kcfgc)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(ember
  URI Ember
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/components/Backdrop.qml
    qml/components/DetailsPane.qml
    qml/components/ItemRow.qml
    qml/components/Keyboard.qml
    qml/components/MenuList.qml
    qml/components/PopupMenu.qml
    qml/components/StatusIcon.qml
    qml/components/TextEntry.qml
    qml/pages/HomePage.qml
    qml/pages/InfoPage.qml
    qml/pages/LibraryPage.qml
    qml/pages/LibraryPickerPage.qml
    qml/pages/PlayerPage.qml
    qml/pages/SearchPage.qml
    qml/pages/ServerPage.qml
    qml/pages/SettingsPage.qml
    qml/pages/SignInPage.qml
  SOURCES
    app/Mpris.h app/Mpris.cpp
    app/RemoteKeys.h app/RemoteKeys.cpp
    app/SystemIntegration.h app/SystemIntegration.cpp
    jellyfin/ApiClient.h jellyfin/ApiClient.cpp
    jellyfin/Items.h jellyfin/Items.cpp
    jellyfin/Session.h jellyfin/Session.cpp
    models/ItemDetails.h models/ItemDetails.cpp
    models/ItemListModel.h models/ItemListModel.cpp
    playback/Playback.h playback/Playback.cpp
)

target_compile_definitions(ember PRIVATE EMBER_VERSION="${PROJECT_VERSION}")
target_compile_options(ember PRIVATE -Wall -Wextra)
# The QML type registrations include each header by its file name.
target_include_directories(ember PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/app" "${CMAKE_CURRENT_SOURCE_DIR}/jellyfin" "${CMAKE_CURRENT_SOURCE_DIR}/models"
  "${CMAKE_CURRENT_SOURCE_DIR}/playback" "${CMAKE_CURRENT_SOURCE_DIR}/player")
target_link_libraries(ember PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::DBus
  KF6::ConfigCore KF6::ConfigGui KF6::CoreAddons KF6::DBusAddons
  QCoro6::Core QCoro6::Network
  PkgConfig::MPV
  ember_player ember_playerplugin)

install(TARGETS ember RUNTIME DESTINATION bin)
install(FILES ../dist/org.couchbox.ember.desktop DESTINATION share/applications)
install(FILES ../dist/org.couchbox.ember.svg DESTINATION share/icons/hicolor/scalable/apps)
