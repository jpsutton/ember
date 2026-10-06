# Development tool, not installed.
find_package(Qt6 REQUIRED COMPONENTS DBus)
qt_add_executable(ember-shot main.cpp)
target_link_libraries(ember-shot PRIVATE Qt6::Core Qt6::Gui Qt6::DBus)
