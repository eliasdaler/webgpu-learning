add_executable(minimal_example
  main.cpp
)

set_target_properties(minimal_example PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)

target_add_extra_warnings(minimal_example)

target_include_directories(minimal_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(minimal_example PRIVATE glfw)

## link with Dawn
set(DAWN_TARGETS
	dawn_common
	dawn_headers
	dawn_native
	dawn_platform
	dawn_proc
  dawncpp
  dawncpp_headers
	webgpu_dawn
  dawn_glfw
	libtint
)

target_link_libraries(minimal_example PRIVATE
    ${DAWN_TARGETS}
)
