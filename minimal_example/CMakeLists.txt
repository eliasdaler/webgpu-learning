add_executable(minimal_example
  main.cpp
)

set_target_properties(minimal_example PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)

target_add_extra_warnings(minimal_example)

target_include_directories(minimal_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

if(BUILD_SHARED_LIBS)
  target_link_libraries(minimal_example PRIVATE
    SDL2::SDL2
  )
else()
  target_link_libraries(minimal_example PRIVATE
    SDL2::SDL2-static
  )
endif()

if(WIN32)
  target_link_libraries(minimal_example PRIVATE SDL2::SDL2main)
endif()


target_link_libraries(minimal_example PRIVATE stb::image)

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

target_link_libraries(minimal_example PRIVATE
    ${DAWN_TARGETS}
)
