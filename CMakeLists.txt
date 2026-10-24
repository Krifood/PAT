cmake_minimum_required(VERSION 3.20)

project(ParamAnalysisTool
  VERSION 0.1.2
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
  list(PREPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
endif()

# Chinese source strings and C++20 feature detection must behave identically on MSVC.
if(MSVC)
  add_compile_options(/utf-8 /Zc:__cplusplus /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(PAT_BUILD_GUI "Build the PAT desktop application" ON)
option(PAT_ENABLE_SERIAL "Enable serial acquisition" ON)
option(PAT_BUILD_TESTS "Build core tests" ON)
option(PAT_STRICT_WARNINGS "Enable compiler warnings" ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

if(PAT_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Concurrent)
endif()

if(PAT_ENABLE_SERIAL)
  find_package(Qt6 REQUIRED COMPONENTS SerialPort)
endif()

add_library(pat_core
  src/core/CaptureSession.cpp
  src/core/DataSession.cpp
  src/core/FormatDefinition.cpp
  src/core/FormatDocument.cpp
  src/core/FrameExporter.cpp
  src/core/InputReader.cpp
  src/core/IndexedMixedReader.cpp
  src/core/AlignedMixedReader.cpp
  src/core/LiveDataSession.cpp
  src/core/MixedDataSession.cpp
  src/core/RecordDecoder.cpp
  src/core/RecordParser.cpp
  src/core/SerialFrameAssembler.cpp
  src/core/SeriesProcessing.cpp
  src/core/TrajectoryData.cpp
  src/core/UnifiedLogWriter.cpp
  src/core/VmcLogReader.cpp
)

target_include_directories(pat_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pat_core
  PUBLIC
    Qt6::Core
)

if(PAT_STRICT_WARNINGS)
  if(MSVC)
    target_compile_options(pat_core PRIVATE /W4)
  else()
    target_compile_options(pat_core PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

if(PAT_BUILD_GUI)
  set(PAT_UI_SOURCES
    src/ui/AppTheme.cpp
    src/ui/AboutDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/MixedLoadController.cpp
    src/ui/ChartArea.cpp
    src/ui/CompactValueAxis.cpp
    src/ui/DisplayGroupManager.cpp
    src/ui/FormatEditorDialog.cpp
    src/ui/SignalChartView.cpp
    src/ui/SignalTreeController.cpp
    src/ui/SignalTreeWidget.cpp
    src/ui/SignalProcessingDialogs.cpp
    src/ui/TrajectoryView.cpp
    src/ui/TimeRangeControl.cpp
    src/ui/app_resources.qrc
  )
  add_executable(pat_app WIN32 src/ui/main.cpp ${PAT_UI_SOURCES})
  target_include_directories(pat_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(pat_app PRIVATE pat_core Qt6::Widgets Qt6::Charts Qt6::Concurrent)
  target_compile_definitions(pat_app PRIVATE PAT_ENABLE_QT_CHARTS=1
    PAT_APP_VERSION="${PROJECT_VERSION}")
endif()

if(PAT_ENABLE_SERIAL)
  add_library(pat_serial
    src/serial/SerialCaptureWorker.cpp
  )
  target_include_directories(pat_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(pat_serial PUBLIC pat_core Qt6::SerialPort)
  if(PAT_BUILD_GUI)
    target_link_libraries(pat_app PRIVATE pat_serial)
    target_compile_definitions(pat_app PRIVATE PAT_ENABLE_SERIAL=1)
  endif()
endif()

if(PAT_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(pat_core_tests tests/CoreTests.cpp)
  target_link_libraries(pat_core_tests PRIVATE pat_core Qt6::Test)
  target_compile_definitions(pat_core_tests PRIVATE PAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME pat_core_tests COMMAND pat_core_tests)
  if(PAT_ENABLE_SERIAL AND UNIX)
    add_executable(pat_serial_tests tests/SerialWorkerTests.cpp)
    target_link_libraries(pat_serial_tests PRIVATE pat_serial Qt6::Test)
    target_compile_definitions(pat_serial_tests PRIVATE PAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT APPLE)
      target_link_libraries(pat_serial_tests PRIVATE util)
    endif()
    add_test(NAME pat_serial_tests COMMAND pat_serial_tests)
    if(PAT_BUILD_GUI)
      add_executable(pat_ui_tests tests/UiTests.cpp ${PAT_UI_SOURCES})
      target_include_directories(pat_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
      target_link_libraries(pat_ui_tests PRIVATE pat_serial Qt6::Widgets Qt6::Charts Qt6::Concurrent Qt6::Test)
      target_compile_definitions(pat_ui_tests PRIVATE PAT_ENABLE_QT_CHARTS=1 PAT_ENABLE_SERIAL=1
        PAT_APP_VERSION="${PROJECT_VERSION}"
        PAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
      if(NOT APPLE)
        target_link_libraries(pat_ui_tests PRIVATE util)
      endif()
      add_test(NAME pat_ui_tests COMMAND pat_ui_tests)
      set_tests_properties(pat_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
  endif()
endif()

if(PAT_BUILD_GUI AND PAT_STRICT_WARNINGS)
  if(MSVC)
    target_compile_options(pat_app PRIVATE /W4)
  else()
    target_compile_options(pat_app PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

if(PAT_BUILD_GUI)
  if(APPLE)
    # Some QtGui builds link QtDBus; keep it in the app's deploy graph when present.
    find_package(Qt6 COMPONENTS DBus QUIET)
    if(TARGET Qt6::DBus)
      target_link_libraries(pat_app PRIVATE Qt6::DBus)
    endif()
    get_filename_component(PAT_QT_LIB_DIR "${Qt6_DIR}/../.." ABSOLUTE)
    set_target_properties(pat_app PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "PAT"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.krifood.pat"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      INSTALL_RPATH "${PAT_QT_LIB_DIR}"
    )
    install(TARGETS pat_app BUNDLE DESTINATION .)
    qt_generate_deploy_app_script(
      TARGET pat_app
      OUTPUT_SCRIPT pat_deploy_script
      DEPLOY_TOOL_OPTIONS "-libpath=${PAT_QT_LIB_DIR}"
    )
    install(SCRIPT ${pat_deploy_script})
    # A second pass resolves dependencies of Qt plugins copied by the first pass.
    find_program(PAT_MACDEPLOYQT macdeployqt HINTS "${PAT_QT_LIB_DIR}/../bin" REQUIRED)
    set(PAT_MAC_CODESIGN_IDENTITY "-" CACHE STRING
      "macOS signing identity; '-' performs an ad hoc development signature")
    install(CODE "
      execute_process(
        COMMAND \"${PAT_MACDEPLOYQT}\" \"\${CMAKE_INSTALL_PREFIX}/pat_app.app\"
                -always-overwrite \"-libpath=${PAT_QT_LIB_DIR}\"
        RESULT_VARIABLE pat_deploy_status
      )
      if(NOT pat_deploy_status EQUAL 0)
        message(FATAL_ERROR \"Second macdeployqt pass failed\")
      endif()
      execute_process(
        COMMAND /usr/bin/codesign --force --deep --sign \"${PAT_MAC_CODESIGN_IDENTITY}\"
                \"\${CMAKE_INSTALL_PREFIX}/pat_app.app\"
        RESULT_VARIABLE pat_sign_status
      )
      if(NOT pat_sign_status EQUAL 0)
        message(FATAL_ERROR \"macOS bundle signing failed\")
      endif()
    ")
  elseif(WIN32)
    # Keep the executable beside its DLLs in bin. Qt's deploy script also
    # copies plugins and the compiler runtime so the installed tree can run on
    # a machine without a Qt SDK.
    install(TARGETS pat_app RUNTIME DESTINATION bin)
    qt_generate_deploy_app_script(
      TARGET pat_app
      OUTPUT_SCRIPT pat_deploy_script
      NO_TRANSLATIONS
      DEPLOY_TOOL_OPTIONS --compiler-runtime
    )
    install(SCRIPT ${pat_deploy_script})
  else()
    install(TARGETS pat_app RUNTIME DESTINATION bin)
  endif()
endif()
