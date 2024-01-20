add_executable(game
  Graphics/Mesh.cpp

  util/GltfLoader.cpp
  util/ImageLoader.cpp
  util/OSUtil.cpp
  util/SDLWebGPU.cpp
  util/WebGPUUtil.cpp

  Game.cpp
  main.cpp
)

set_target_properties(game PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)

target_add_extra_warnings(game)

target_include_directories(game PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

add_custom_target(copy_assets
  COMMENT "Copying game assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
)

add_dependencies(game copy_assets)

if(BUILD_SHARED_LIBS)
  target_link_libraries(game PRIVATE
    SDL2::SDL2
  )
else()
  target_link_libraries(game PRIVATE
    SDL2::SDL2-static
  )
endif()

if(WIN32)
  target_link_libraries(game PRIVATE SDL2::SDL2main)
endif()


target_link_libraries(game PRIVATE stb::image)

## link with Dawn
set(DAWN_TARGETS
  # core_tables
	dawn_common
	dawn_headers
	dawn_native
	dawn_platform
	dawn_proc
	dawn_utils
	dawn_wire
  dawncpp
  dawncpp_headers
  # emscripten_bits_gen
  # enum_string_mapping
  # extinst_tables
	webgpu_dawn
  # webgpu_headers_gen
	libtint
  # tint_diagnostic_utils
  # tint_utils_io
  # tint_val
  # tint-format
  # tint-lint
)

target_link_libraries(game PRIVATE
    ${DAWN_TARGETS}
)

target_link_libraries(game PRIVATE
  glm::glm
  tinygltf::tinygltf
)

target_compile_definitions(game
  PUBLIC
    GLM_FORCE_CTOR_INIT
    GLM_FORCE_XYZW_ONLY
    GLM_FORCE_EXPLICIT_CTOR
)
