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

add_executable(vvvdaw
    resources.qrc
    src/main.cpp
    src/core/Settings.cpp
    src/core/UndoStack.cpp
    src/commands/TrackCommands.cpp
    src/commands/BusCommands.cpp
    src/commands/ProjectCommands.cpp
    src/commands/EventCommands.cpp
    src/commands/PluginCommands.cpp
    src/commands/SnapshotCommand.cpp
    src/audio/AudioEngine.cpp
    src/audio/RecordingManager.cpp
    src/audio/StreamingManager.cpp
    src/model/AudioClip.cpp
    src/model/AudioEvent.cpp
    src/model/Track.cpp
    src/model/Project.cpp
    src/plugin/VST3Instance.cpp
    src/plugin/PluginChain.cpp
    src/plugin/PluginManager.cpp
    src/gui/MainWindow.cpp
    src/gui/TransportPanel.cpp
    src/gui/TrackPanelWidget.cpp
    src/gui/BusPanelWidget.cpp
    src/gui/TrackViewWidget.cpp
    src/gui/WaveformPainter.cpp
    src/gui/TimelineRuler.cpp
    src/gui/MeasureRuler.cpp
    src/gui/TempoWidget.cpp
    src/gui/SettingsDialog.cpp
    src/gui/PluginListWidget.cpp
    src/gui/PluginWindow.cpp
    src/gui/PluginParameterWidget.cpp
)

target_include_directories(vvvdaw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    /usr/include/vst3sdk
)

target_link_directories(vvvdaw PRIVATE
    /usr/lib/x86_64-linux-gnu/vst3sdk
)

target_link_libraries(vvvdaw PRIVATE
    Qt6::Widgets
    PkgConfig::PORTAUDIO
    PkgConfig::SNDFILE
    -Wl,--start-group
    sdk_hosting
    sdk_common
    pluginterfaces
    base
    -Wl,--end-group
    dl
)
