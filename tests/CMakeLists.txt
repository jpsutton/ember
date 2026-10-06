find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(test items remotekeys session guide)
  qt_add_executable(tst_${test} tst_${test}.cpp)
  target_link_libraries(tst_${test} PRIVATE Qt6::Test Qt6::GuiPrivate ember_lib)
  target_compile_definitions(tst_${test} PRIVATE EMBER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_test(NAME ${test} COMMAND tst_${test})
  set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
