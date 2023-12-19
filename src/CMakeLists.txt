add_executable(game
  util/OSUtil.cpp
  util/WebGPUUtil.cpp
  util/SDLWebGPU.cpp

  Game.cpp
  main.cpp
)

target_include_directories(game PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

set_target_properties(game PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)

target_add_extra_warnings(game)

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
  # dawncpp
  # dawncpp_headers
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
