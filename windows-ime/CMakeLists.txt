if(WIN32)
  add_library(zhuyin_ime SHARED
    CandidateWindow.cpp
    CandidateWindow.h
    ClassFactory.cpp
    ClassFactory.h
    dllmain.cpp
    Globals.h
    Registrar.cpp
    Registrar.h
    TextService.cpp
    TextService.h
    ZhuyinIME.def
  )

  target_include_directories(zhuyin_ime
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include
  )

  target_link_libraries(zhuyin_ime
    PRIVATE
      zhuyin_engine
      gdi32
      imm32
      msctf
      ole32
      oleaut32
      user32
      uuid
  )

  target_compile_definitions(zhuyin_ime PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  set_target_properties(zhuyin_ime PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    OUTPUT_NAME "zhuyin_ime"
  )

  if(MSVC)
    target_link_options(zhuyin_ime PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ZhuyinIME.def")
  endif()

  # Stage the generated dictionary right next to the built DLL so
  # register.bat / manual testing works without a separate packaging step.
  add_custom_command(TARGET zhuyin_ime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/../data/dictionary.tsv"
      "$<TARGET_FILE_DIR:zhuyin_ime>/dictionary.tsv"
  )
endif()
