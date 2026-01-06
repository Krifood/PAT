cmake_minimum_required(VERSION 3.20)

project(ParamAnalysisTool
  VERSION 0.1
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(PAT_ENABLE_QT_CHARTS "Enable Qt Charts plotting support" ON)
option(PAT_STRICT_WARNINGS "Enable compiler warnings" ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

if(PAT_ENABLE_QT_CHARTS)
  find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Charts)
endif()

add_library(pat_core
  src/core/format_definition.cpp
  src/core/record_parser.cpp
)

target_include_directories(pat_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pat_core
  PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
)

if(PAT_STRICT_WARNINGS)
  if(MSVC)
    target_compile_options(pat_core PRIVATE /W4)
  else()
    target_compile_options(pat_core PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

add_executable(pat_app
  src/ui/main.cpp
  src/ui/main_window.cpp
)

target_include_directories(pat_app
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pat_app
  PRIVATE
    pat_core
    Qt${QT_VERSION_MAJOR}::Widgets
)

if(PAT_ENABLE_QT_CHARTS)
  target_link_libraries(pat_app PRIVATE Qt${QT_VERSION_MAJOR}::Charts)
  target_compile_definitions(pat_app PRIVATE PAT_ENABLE_QT_CHARTS=1)
endif()

if(PAT_STRICT_WARNINGS)
  if(MSVC)
    target_compile_options(pat_app PRIVATE /W4)
  else()
    target_compile_options(pat_app PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

install(TARGETS pat_app)
