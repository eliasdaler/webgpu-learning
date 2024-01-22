### SDL
if (NOT BUILD_SHARED_LIBS)
  set(SDL_SHARED_ENABLED_BY_DEFAULT OFF CACHE BOOL "Don't build SDL as shared lib")
endif()
option(SDL_TEST "Build the SDL2_test library" OFF)
option(SDL_AUDIO_ENABLED_BY_DEFAULT "Enable the Audio subsystem" OFF)
add_subdirectory(SDL)

### Dawn
option(DAWN_FETCH_DEPENDENCIES "Use fetch_dawn_dependencies.py as an alternative to using depot_tools" ON)
option(DAWN_BUILD_SAMPLES "Enables building Dawn's samples" OFF)

option(TINT_BUILD_CMD_TOOLS "Build the Tint command line tools" OFF)
option(TINT_BUILD_DOCS "Build documentation" OFF)
option(TINT_BUILD_SPV_READER "Build the SPIR-V input reader" OFF)
option(TINT_BUILD_WGSL_READER "Build the WGSL input reader" ON)
option(TINT_BUILD_GLSL_WRITER "Build the GLSL output writer" OFF)
option(TINT_BUILD_GLSL_VALIDATOR "Build the GLSL output validator" OFF)
option(TINT_BUILD_MSL_WRITER "Build the MSL output writer" OFF)
option(TINT_BUILD_SPV_WRITER "Build the SPIR-V output writer" OFF)
option(TINT_BUILD_WGSL_WRITER "Build the WGSL output writer" ON)
option(TINT_BUILD_TESTS "Build tests" OFF)

add_subdirectory(dawn)

# silence some warnings
if(CMAKE_COMPILER_IS_GNUCXX OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  target_compile_options(dawn_proc PRIVATE -Wno-attributes)
  target_compile_options(dawn_wire PRIVATE -Wno-return-type)
  target_compile_options(dawn_native PRIVATE -Wno-return-type)
endif()

### stb

add_subdirectory(stb)

## tinygltf
option(TINYGLTF_BUILD_LOADER_EXAMPLE "Build loader_example(load glTF and dump infos)" OFF)
option(TINYGLTF_INSTALL "Install tinygltf files during install step." OFF)
option(TINYGLTF_HEADER_ONLY "On: header-only mode. Off: create tinygltf library(No TINYGLTF_IMPLEMENTATION required in your project)" ON)
add_subdirectory(tinygltf)
add_library(tinygltf::tinygltf ALIAS tinygltf)

## glm
add_subdirectory(glm)
