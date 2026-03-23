cmake_minimum_required(VERSION 3.20)
project(EmbedR VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EMBEDR_BUILD_TESTS "Build EmbedR tests" ${PROJECT_IS_TOP_LEVEL})
option(EMBEDR_BUILD_DOCS "Build EmbedR docs target" ${PROJECT_IS_TOP_LEVEL})
option(EMBEDR_USE_SYSTEM_NLOHMANN_JSON "Use preinstalled nlohmann_json instead of FetchContent" OFF)
if (UNIX AND NOT APPLE)
  execute_process(
    COMMAND R RHOME
    OUTPUT_VARIABLE _EMBEDR_RHOME_OUTPUT
    RESULT_VARIABLE _EMBEDR_RHOME_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(_EMBEDR_RHOME_RESULT EQUAL 0 AND NOT "${_EMBEDR_RHOME_OUTPUT}" STREQUAL "")
    message(STATUS "Detected R home: ${_EMBEDR_RHOME_OUTPUT}")
    set(R_HOME "${_EMBEDR_RHOME_OUTPUT}")
  else()
    message(WARNING "Could not detect R home. You may need to set R_HOME environment variable or CMake cache variable.")
  endif()
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
  if(EMBEDR_USE_SYSTEM_NLOHMANN_JSON)
    find_package(nlohmann_json 3.11.0 CONFIG REQUIRED)
  else()
    include(FetchContent)
    FetchContent_Declare(
      nlohmann_json
      URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
    FetchContent_GetProperties(nlohmann_json SOURCE_DIR nlohmann_json_SOURCE_DIR)
    set(EMBEDR_NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/include")
  endif()
endif()

if(NOT DEFINED EMBEDR_NLOHMANN_JSON_INCLUDE_DIR)
  get_target_property(_EMBEDR_NLOHMANN_JSON_INTERFACE_DIRS nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
  foreach(_dir IN LISTS _EMBEDR_NLOHMANN_JSON_INTERFACE_DIRS)
    if(IS_DIRECTORY "${_dir}" AND EXISTS "${_dir}/nlohmann/json.hpp")
      set(EMBEDR_NLOHMANN_JSON_INCLUDE_DIR "${_dir}")
      break()
    endif()
  endforeach()
endif()

if(NOT DEFINED EMBEDR_NLOHMANN_JSON_INCLUDE_DIR)
  message(FATAL_ERROR "Could not determine the nlohmann_json include directory.")
endif()

set(R_HOME "" CACHE PATH "Path to R home (for example /Library/Frameworks/R.framework/Resources)")
if(NOT R_HOME AND DEFINED ENV{R_HOME} AND NOT "$ENV{R_HOME}" STREQUAL "")
  set(R_HOME "$ENV{R_HOME}")
endif()
if(WIN32 AND NOT R_HOME)
  set(_R_HOME_WIN_HINTS
    "C:/Program Files/R/R-4.6.0"
    "C:/Program Files/R/R-4.5.0"
    "C:/Program Files/R/R-4.4.0"
    "C:/Program Files/R/R-4.3.0"
    "C:/Program Files/R/R-4.2.0"
  )
  foreach(_hint IN LISTS _R_HOME_WIN_HINTS)
    if(EXISTS "${_hint}")
      set(R_HOME "${_hint}")
      break()
    endif()
  endforeach()
endif()

find_path(R_INCLUDE_DIR
  NAMES Rembedded.h
  HINTS
    "${R_HOME}/include"
    "$ENV{R_HOME}/include"
  PATHS
    "/Library/Frameworks/R.framework/Headers"
    "/Library/Frameworks/R.framework/Resources/include"
    "/usr/share/R/include"
    "/usr/lib/R/include"
    "/usr/local/lib/R/include"
    "C:/Program Files/R/*/include"
  REQUIRED
)

if(NOT R_HOME)
  if(R_INCLUDE_DIR MATCHES "R\\.framework/Headers$")
    set(R_HOME "/Library/Frameworks/R.framework/Resources")
  elseif(R_INCLUDE_DIR MATCHES "([/\\\\])include$")
    get_filename_component(R_HOME "${R_INCLUDE_DIR}" DIRECTORY)
  endif()
endif()

find_library(R_LIBRARY
  NAMES R
  HINTS
    "${R_HOME}/lib"
    "${R_HOME}/lib/x86_64"
    "${R_HOME}/lib/x64"
    "${R_HOME}/bin/x64"
  PATHS
    "/Library/Frameworks"
    "/usr/lib/R/lib"
    "/usr/local/lib/R/lib"
    "C:/Program Files/R/*/bin/x64"
  REQUIRED
)

add_library(EmbedR STATIC
  src/EmbedR.cpp
)
add_library(EmbedR::EmbedR ALIAS EmbedR)

target_include_directories(EmbedR
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${EMBEDR_NLOHMANN_JSON_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${R_INCLUDE_DIR}>
)

if(UNIX AND NOT APPLE)
  target_compile_options(EmbedR PRIVATE -fPIC)
endif()

target_link_libraries(EmbedR
  PUBLIC
    ${R_LIBRARY}
)

target_compile_definitions(EmbedR
  PUBLIC
    R_EMBED_DEFAULT_R_HOME="${R_HOME}"
)

if(WIN32)
  target_compile_definitions(EmbedR PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(EMBEDR_BUILD_TESTS)
  enable_testing()
  add_executable(EmbedR_tests tests/EmbedRTests.cpp)
  target_link_libraries(EmbedR_tests PRIVATE EmbedR::EmbedR)
  add_test(NAME EmbedR_tests COMMAND EmbedR_tests)

  find_path(READLINE_INCLUDE_DIR
    NAMES readline/readline.h
  )
  find_library(READLINE_LIBRARY
    NAMES readline edit
  )

  if(NOT READLINE_INCLUDE_DIR OR NOT READLINE_LIBRARY)
    message(FATAL_ERROR "readline development files are required to build EmbedR_repl.")
  endif()

  find_package(Curses)

  add_executable(rrepl tests/EmbedRRepl.cpp)
  target_include_directories(rrepl PRIVATE ${READLINE_INCLUDE_DIR})
  target_link_libraries(rrepl PRIVATE EmbedR::EmbedR ${READLINE_LIBRARY})
  if(CURSES_FOUND)
    target_link_libraries(rrepl PRIVATE ${CURSES_LIBRARIES})
  endif()
endif()

if(EMBEDR_BUILD_DOCS)
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${PROJECT_SOURCE_DIR}/doc/Doxyfile.in)
    set(DOXYGEN_OUT ${PROJECT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    add_custom_target(docs
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM
    )
  else()
    add_custom_target(docs
      COMMAND ${CMAKE_COMMAND} -E echo "Doxygen not found. Install Doxygen to build docs."
      VERBATIM
    )
  endif()
endif()

install(TARGETS EmbedR
  EXPORT EmbedRTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

install(DIRECTORY "${EMBEDR_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(TARGET rrepl)
  install(TARGETS rrepl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

install(EXPORT EmbedRTargets
  NAMESPACE EmbedR::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EmbedR
)
