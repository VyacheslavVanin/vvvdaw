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
