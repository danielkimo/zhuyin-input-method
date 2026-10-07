cmake_minimum_required(VERSION 3.16)

# Statically link the MSVC C/C++ runtime so the built zhuyin_ime.dll runs on
# a machine that doesn't have the Visual C++ Redistributable installed (e.g.
# a fresh Windows PC with no Visual Studio / dev tools at all) -- this must
# be set before project() so it applies to every target.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(zhuyin_ime CXX)

option(ZHUYIN_BUILD_TESTS "Build the cross-platform engine's unit tests / demo CLI" ON)

enable_testing()

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(engine)

if(WIN32)
    add_subdirectory(windows-ime)
endif()
