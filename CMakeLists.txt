cmake_minimum_required(VERSION 3.16)
project(zhuyin_ime CXX)

option(ZHUYIN_BUILD_TESTS "Build the cross-platform engine's unit tests / demo CLI" ON)

enable_testing()

add_subdirectory(engine)

if(WIN32)
    add_subdirectory(windows-ime)
endif()
