find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(vvvdaw_test_core test_core.cpp)
target_link_libraries(vvvdaw_test_core PRIVATE vvvdaw_core Qt6::Test)
add_test(NAME test_core COMMAND vvvdaw_test_core)

add_executable(vvvdaw_test_model test_model.cpp)
target_link_libraries(vvvdaw_test_model PRIVATE vvvdaw_model vvvdaw_core Qt6::Test)
add_test(NAME test_model COMMAND vvvdaw_test_model)

add_executable(vvvdaw_test_commands test_commands.cpp)
target_link_libraries(vvvdaw_test_commands PRIVATE vvvdaw_commands vvvdaw_model vvvdaw_core Qt6::Test)
add_test(NAME test_commands COMMAND vvvdaw_test_commands)

add_executable(vvvdaw_test_audio test_audio.cpp)
target_link_libraries(vvvdaw_test_audio PRIVATE vvvdaw_audio vvvdaw_model vvvdaw_core Qt6::Test)
add_test(NAME test_audio COMMAND vvvdaw_test_audio)

add_executable(vvvdaw_test_lv2 test_lv2.cpp)
target_link_libraries(vvvdaw_test_lv2 PRIVATE vvvdaw_model vvvdaw_core Qt6::Test)
add_test(NAME test_lv2 COMMAND vvvdaw_test_lv2)
set_tests_properties(test_lv2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vvvdaw_test_gui test_gui.cpp ../resources.qrc)
target_link_libraries(vvvdaw_test_gui PRIVATE
    vvvdaw_gui vvvdaw_audio vvvdaw_model vvvdaw_core Qt6::Test)
add_test(NAME test_gui COMMAND vvvdaw_test_gui)
set_tests_properties(test_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
