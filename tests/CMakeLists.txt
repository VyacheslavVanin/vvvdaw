find_package(Qt6 REQUIRED COMPONENTS Test)

# Register a Qt Test executable. Usage:
#   vvvdaw_add_test(<ctest-name> <sources...>)
# Links Qt6::Test by default; callers add the project libraries they need.
# The executable is named vvvdaw_test_<ctest-name-without-test_->, e.g.
# vvvdaw_add_test(test_gui_buspanel ...) builds vvvdaw_test_gui_buspanel.
function(vvvdaw_add_test ctest_name)
    string(REPLACE "test_" "" suffix "${ctest_name}")
    set(target vvvdaw_test_${suffix})
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE Qt6::Test)
    add_test(NAME ${ctest_name} COMMAND ${target})
endfunction()

# --- Core ---

vvvdaw_add_test(test_core test_core.cpp)
target_link_libraries(vvvdaw_test_core PRIVATE vvvdaw_core)

# --- Model (split per area) ---

foreach(model_test IN ITEMS project bus clips events track templates)
    vvvdaw_add_test(test_model_${model_test} test_model/test_model_${model_test}.cpp)
    target_link_libraries(vvvdaw_test_model_${model_test} PRIVATE vvvdaw_model vvvdaw_core)
endforeach()

# --- Commands (split per area) ---

foreach(cmd_test IN ITEMS track bus instrument project event midi undo)
    vvvdaw_add_test(test_commands_${cmd_test} test_commands/test_commands_${cmd_test}.cpp)
    target_link_libraries(vvvdaw_test_commands_${cmd_test} PRIVATE
        vvvdaw_commands vvvdaw_model vvvdaw_core)
endforeach()

# --- Audio ---

vvvdaw_add_test(test_audio test_audio.cpp)
target_link_libraries(vvvdaw_test_audio PRIVATE vvvdaw_audio vvvdaw_model vvvdaw_core)

# --- MIDI ---

vvvdaw_add_test(test_midi test_midi.cpp)
target_link_libraries(vvvdaw_test_midi PRIVATE
    vvvdaw_audio vvvdaw_midi vvvdaw_model vvvdaw_core)

# --- LV2 backend ---

vvvdaw_add_test(test_lv2 test_lv2.cpp)
target_link_libraries(vvvdaw_test_lv2 PRIVATE vvvdaw_model vvvdaw_core)
set_tests_properties(test_lv2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- GUI (split per area) ---

foreach(gui_test IN ITEMS mainwindow trackview waveform buspanel plugins pianoroll ruler)
    vvvdaw_add_test(test_gui_${gui_test} test_gui/test_gui_${gui_test}.cpp ../resources.qrc)
    target_link_libraries(vvvdaw_test_gui_${gui_test} PRIVATE
        vvvdaw_gui vvvdaw_audio vvvdaw_model vvvdaw_core)
    set_tests_properties(test_gui_${gui_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()