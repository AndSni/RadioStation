cmake_minimum_required(VERSION 3.21)
project(RadioStation VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Sql Svg Test)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0
  gstreamer-app-1.0
  gstreamer-audio-1.0
  gstreamer-tag-1.0
  gstreamer-pbutils-1.0
  gstreamer-controller-1.0
)

# ReplayGainAnalyzer writes REPLAYGAIN_TRACK_GAIN/_PEAK tags into audio files
# (see src/audio/ReplayGainAnalyzer.cpp) via TagLib rather than GStreamer's
# own id3v2mux/flactag muxers -- confirmed empirically that both muxers
# silently drop ReplayGain tags on write even though GStreamer's own read
# path (id3demux/flacparse, used during normal playback) handles them fine.
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)

# Vendored single-header library (MIT-0/Unlicense — see third_party/miniaudio/miniaudio.h's
# embedded license text) rather than CMake FetchContent, so the build doesn't depend on
# network access to GitHub at configure time.
find_package(Threads REQUIRED)
add_library(miniaudio STATIC third_party/miniaudio/miniaudio_impl.cpp)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)
target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/db)
add_subdirectory(src/scheduler)
add_subdirectory(src/audio)
add_subdirectory(src/ui)
add_subdirectory(src/app)
add_subdirectory(tests/core)
add_subdirectory(tests/audio)
add_subdirectory(tests/scheduler)
add_subdirectory(tests/db)
add_subdirectory(tests/ui)

# Linux desktop integration. Only affects `cmake --install` / packaging — the
# normal in-tree build+run and ctest workflow never touches these.
include(GNUInstallDirs)
install(TARGETS rs_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/radiostation.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES RadioStation_icon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME radiostation.svg)
