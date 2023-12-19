cmake_minimum_required(VERSION 3.18)

project(
    LearnWebGPU
    VERSION 0.1.0
    LANGUAGES CXX C
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ColorDiagnostics)
include(TargetAddExtraWarnings)

if (WIN32)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Link to libs dynamically")
else()
  set(BUILD_SHARED_LIBS ON CACHE BOOL "Link to libs dynamically")
endif()


# Check that git submodules were cloned
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
  message("${CMAKE_CURRENT_SOURCE_DIR}/SDL/CMakeLists.txt")
  message(FATAL_ERROR "Please initialize submodules using:\n  git submodule update --init --recursive")
endif()

add_subdirectory(third_party)
add_subdirectory(src)
