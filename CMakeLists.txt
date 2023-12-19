cmake_minimum_required(VERSION 3.18)

project(
    LearnWebGPU
    VERSION 0.1.0
    LANGUAGES CXX C
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ColorDiagnostics)
include(TargetAddExtraWarnings)

add_subdirectory(third_party)
add_subdirectory(src)
