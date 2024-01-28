add_executable(game
  Math/Transform.cpp

  Graphics/Camera.cpp
  Graphics/Mesh.cpp

  util/GltfLoader.cpp
  util/ImageLoader.cpp
  util/InputUtil.cpp
  util/OSUtil.cpp
  util/SDLWebGPU.cpp
  util/WebGPUUtil.cpp

  FreeCameraController.cpp
  MaterialCache.cpp
  MeshCache.cpp

  Game.cpp
  main.cpp
)

set_target_properties(game PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)

target_add_extra_warnings(game)

target_include_directories(game PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

add_custom_command(TARGET game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "Copying game assets to $<TARGET_FILE_DIR:game>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:game>/assets"
)

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
  imgui::imgui
)

target_compile_definitions(game
  PUBLIC
    GLM_FORCE_CTOR_INIT
    GLM_FORCE_XYZW_ONLY
    GLM_FORCE_EXPLICIT_CTOR
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(game PUBLIC Tracy::TracyClient)
target_compile_definitions(game PRIVATE $<$<CONFIG:Debug,RelWithDebInfo>:TRACY_ENABLE>)

