cmake_minimum_required(VERSION 3.16)
project(CircleBeats CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# raylib: use an installed one if present, otherwise download it.
find_package(raylib 5.0 QUIET)
if (NOT raylib_FOUND)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()

# RtMidi: cross-platform MIDI (CoreMIDI / ALSA / WinMM).
# We download it but compile RtMidi.cpp ourselves: its own CMakeLists defines an
# "uninstall" target that clashes with raylib's. (SOURCE_SUBDIR points at a folder
# with no CMakeLists, so FetchContent only downloads.)
FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
    SOURCE_SUBDIR no-cmake-here)
FetchContent_MakeAvailable(rtmidi)

add_library(rtmidi_lib STATIC ${rtmidi_SOURCE_DIR}/RtMidi.cpp)
target_include_directories(rtmidi_lib PUBLIC ${rtmidi_SOURCE_DIR})
if (APPLE)
    target_compile_definitions(rtmidi_lib PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtmidi_lib PUBLIC
        "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
elseif (WIN32)
    target_compile_definitions(rtmidi_lib PRIVATE __WINDOWS_MM__)
    target_link_libraries(rtmidi_lib PUBLIC winmm)
else ()
    # Linux: sudo apt install libasound2-dev
    find_package(ALSA REQUIRED)
    target_compile_definitions(rtmidi_lib PRIVATE __LINUX_ALSA__)
    target_link_libraries(rtmidi_lib PUBLIC ALSA::ALSA pthread)
endif ()

add_executable(circlebeats main.cpp midi.cpp)
target_link_libraries(circlebeats raylib rtmidi_lib)
