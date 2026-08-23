cmake_minimum_required(VERSION 3.16)
project(vvvdaw VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(LILV REQUIRED IMPORTED_TARGET lilv-0)
pkg_check_modules(SORD REQUIRED IMPORTED_TARGET sord-0)
pkg_check_modules(SRATOM REQUIRED IMPORTED_TARGET sratom-0)
pkg_check_modules(SOUNDTOUCH REQUIRED IMPORTED_TARGET soundtouch)
pkg_check_modules(RTMIDI IMPORTED_TARGET rtmidi)
find_package(X11 REQUIRED)

set(APP_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Static libraries (layered: core -> midi -> plugin -> model -> commands/audio -> gui) ---

add_library(vvvdaw_core STATIC
    src/core/Settings.cpp
    src/core/UndoStack.cpp
)
target_include_directories(vvvdaw_core PUBLIC ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw_core PUBLIC Qt6::Core)

add_library(vvvdaw_midi STATIC
    src/midi/MidiInputManager.cpp
    src/midi/MidiOutputManager.cpp
)
target_include_directories(vvvdaw_midi PUBLIC ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw_midi PUBLIC vvvdaw_core)
if(RTMIDI_FOUND)
    target_compile_definitions(vvvdaw_midi PUBLIC HAVE_RTMIDI=1)
    target_link_libraries(vvvdaw_midi PUBLIC PkgConfig::RTMIDI)
endif()

add_library(vvvdaw_plugin STATIC
    src/plugin/LV2Instance.cpp
    src/plugin/LV2UIHost.cpp
    src/plugin/PluginChain.cpp
    src/plugin/PluginManager.cpp
    src/plugin/VST3Instance.cpp
)
target_include_directories(vvvdaw_plugin PUBLIC ${APP_INCLUDE_DIRS} /usr/include/vst3sdk)
target_link_directories(vvvdaw_plugin PUBLIC /usr/lib/x86_64-linux-gnu/vst3sdk)
target_link_libraries(vvvdaw_plugin PUBLIC
    vvvdaw_midi
    Qt6::Widgets
    PkgConfig::LILV
    PkgConfig::SORD
    PkgConfig::SRATOM
    X11::X11
    -Wl,--start-group
    sdk_hosting
    sdk_common
    pluginterfaces
    base
    -Wl,--end-group
    dl
)

add_library(vvvdaw_model STATIC
    src/model/AudioBus.cpp
    src/model/AudioClip.cpp
    src/model/AudioEvent.cpp
    src/model/Instrument.cpp
    src/model/MidiClip.cpp
    src/model/MidiEvent.cpp
    src/model/Track.cpp
    src/model/Project.cpp
    src/model/TemplateStore.cpp
)
target_include_directories(vvvdaw_model PUBLIC ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw_model PUBLIC
    vvvdaw_plugin
    PkgConfig::SNDFILE
)

add_library(vvvdaw_commands STATIC
    src/commands/TrackCommands.cpp
    src/commands/BusCommands.cpp
    src/commands/ProjectCommands.cpp
    src/commands/EventCommands.cpp
    src/commands/MidiCommands.cpp
    src/commands/InstrumentCommands.cpp
    src/commands/PluginCommands.cpp
    src/commands/SnapshotCommand.cpp
)
target_include_directories(vvvdaw_commands PUBLIC ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw_commands PUBLIC
    vvvdaw_model
    vvvdaw_plugin
)

add_library(vvvdaw_audio STATIC
    src/audio/AudioEngine.cpp
    src/audio/AudioEngineBus.cpp
    src/audio/AudioEngineEvent.cpp
    src/audio/AudioEngineInstrument.cpp
    src/audio/AudioEngineMetronome.cpp
    src/audio/AudioEngineMidi.cpp
    src/audio/MidiRecorder.cpp
    src/audio/RecordingManager.cpp
    src/audio/StreamingManager.cpp
    src/audio/TimeStretch.cpp
)
target_include_directories(vvvdaw_audio PUBLIC ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw_audio PUBLIC
    vvvdaw_model
    vvvdaw_midi
    vvvdaw_plugin
    PkgConfig::PORTAUDIO
    PkgConfig::SNDFILE
    PkgConfig::SOUNDTOUCH
)

add_library(vvvdaw_gui STATIC
    src/gui/MainWindow.cpp
    src/gui/MainWindowMenus.cpp
    src/gui/MainWindowPanels.cpp
    src/gui/MainWindowRuler.cpp
    src/gui/MainWindowTrackRows.cpp
    src/gui/MainWindowTransport.cpp
    src/gui/MainWindowWindows.cpp
    src/gui/TransportPanel.cpp
    src/gui/TrackPanelWidget.cpp
    src/gui/PanSlider.cpp
    src/gui/BusPanelWidget.cpp
    src/gui/BusSendsWidget.cpp
    src/gui/BusLevelMeter.cpp
    src/gui/BusColorBar.cpp
    src/gui/InstrumentPanelWidget.cpp
    src/gui/ChannelRoutingDialog.cpp
    src/gui/TrackViewWidget.cpp
    src/gui/PianoRollWidget.cpp
    src/gui/PianoRollWindow.cpp
    src/gui/VelocityEditorWidget.cpp
    src/gui/WaveformPainter.cpp
    src/gui/TimelineRuler.cpp
    src/gui/MeasureRuler.cpp
    src/gui/RangeRulerWidget.cpp
    src/gui/TempoWidget.cpp
    src/gui/SettingsDialog.cpp
    src/gui/StartDialog.cpp
    src/gui/PluginListWidget.cpp
    src/gui/PluginWindow.cpp
    src/gui/PluginParameterWidget.cpp
    src/gui/RotaryKnob.cpp
)
target_include_directories(vvvdaw_gui PUBLIC ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw_gui PUBLIC
    vvvdaw_commands
    vvvdaw_audio
    vvvdaw_model
    vvvdaw_plugin
    Qt6::Widgets
)

# --- Application ---

add_executable(vvvdaw
    resources.qrc
    src/main.cpp
)
target_include_directories(vvvdaw PRIVATE ${APP_INCLUDE_DIRS})
target_link_libraries(vvvdaw PRIVATE
    vvvdaw_gui
    vvvdaw_audio
    vvvdaw_commands
    vvvdaw_model
    vvvdaw_plugin
    vvvdaw_midi
    vvvdaw_core
    Qt6::Widgets
)
target_link_options(vvvdaw PRIVATE -rdynamic)

# --- Tests ---

enable_testing()
add_subdirectory(tests)
