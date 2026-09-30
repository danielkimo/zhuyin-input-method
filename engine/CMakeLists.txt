cmake_minimum_required(VERSION 3.16)

add_library(zhuyin_engine STATIC
    src/keyboard.cpp
    src/syllable_composer.cpp
    src/dictionary.cpp
    src/decoder.cpp
)
target_include_directories(zhuyin_engine PUBLIC include)
target_compile_features(zhuyin_engine PUBLIC cxx_std_17)

if(BUILD_TESTING OR ZHUYIN_BUILD_TESTS)
    add_executable(zhuyin_engine_tests tests/engine_tests.cpp)
    target_link_libraries(zhuyin_engine_tests PRIVATE zhuyin_engine)
    target_compile_definitions(zhuyin_engine_tests PRIVATE
        ZHUYIN_DICTIONARY_PATH="${CMAKE_SOURCE_DIR}/data/dictionary.tsv")
    add_test(NAME zhuyin_engine_tests COMMAND zhuyin_engine_tests)

    add_executable(zhuyin_demo_cli tests/demo_cli.cpp)
    target_link_libraries(zhuyin_demo_cli PRIVATE zhuyin_engine)
endif()
