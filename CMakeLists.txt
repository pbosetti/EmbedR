cmake_minimum_required(VERSION 3.20)
project(EmbedR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EMBEDR_BUILD_TESTS "Build EmbedR tests" ${PROJECT_IS_TOP_LEVEL})
option(EMBEDR_BUILD_DOCS "Build EmbedR docs target" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

set(R_HOME "" CACHE PATH "Path to R home (for example /Library/Frameworks/R.framework/Resources)")
if(NOT R_HOME AND DEFINED ENV{R_HOME} AND NOT "$ENV{R_HOME}" STREQUAL "")
  set(R_HOME "$ENV{R_HOME}")
endif()

find_path(R_INCLUDE_DIR
  NAMES Rembedded.h
  HINTS
    "${R_HOME}/include"
    "$ENV{R_HOME}/include"
  PATHS
    "/Library/Frameworks/R.framework/Headers"
    "/Library/Frameworks/R.framework/Resources/include"
    "/usr/lib/R/include"
    "/usr/local/lib/R/include"
  REQUIRED
)

if(NOT R_HOME)
  if(R_INCLUDE_DIR MATCHES "R\\.framework/Headers$")
    set(R_HOME "/Library/Frameworks/R.framework/Resources")
  elseif(R_INCLUDE_DIR MATCHES "/include$")
    get_filename_component(R_HOME "${R_INCLUDE_DIR}" DIRECTORY)
  endif()
endif()

find_library(R_LIBRARY
  NAMES R
  HINTS
    "${R_HOME}/lib"
    "${R_HOME}/lib/x86_64"
    "${R_HOME}/bin/x64"
  PATHS
    "/Library/Frameworks"
  REQUIRED
)

add_library(EmbedR STATIC
  src/EmbedR.cpp
)
add_library(EmbedR::EmbedR ALIAS EmbedR)

target_include_directories(EmbedR
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${R_INCLUDE_DIR}>
)

target_link_libraries(EmbedR
  PUBLIC
    nlohmann_json::nlohmann_json
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
