cmake_minimum_required(VERSION 3.22) # CMake version check
project(app)                         # Create project "app"
set(CMAKE_CXX_STANDARD 20)           # Enable C++20 standard


# We add an option to enable different settings when developping the app than
# when distributing it.
option(DEV_MODE "Set up development helper settings" ON)

add_executable(app "main.cpp"
        Application.h
        Application.cpp
        ResourceManager.cpp
        ResourceManager.h)

if(DEV_MODE)
    # In dev mode, we load resources from the source tree, so that when we
    # dynamically edit resources (like shaders), these are correctly
    # versionned.
    target_compile_definitions(app PRIVATE
            RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
else()
    # In release mode, we just load resources relatively to wherever the
    # executable is launched from, so that the binary is portable
    target_compile_definitions(app PRIVATE
            RESOURCE_DIR="./resources"
    )
endif()

set(DAWN_FETCH_DEPENDENCIES ON)
add_subdirectory("dawn" EXCLUDE_FROM_ALL)

target_include_directories(app PRIVATE
        ${CMAKE_SOURCE_DIR}/glm)


if(EMSCRIPTEN)
  set_target_properties(app PROPERTIES SUFFIX ".html")
  target_link_libraries(app PRIVATE emdawnwebgpu_cpp webgpu_glfw)
  target_link_options(app PRIVATE "-sASYNCIFY=1" "-sUSE_GLFW=3")
else()
  target_link_libraries(app PRIVATE webgpu_dawn webgpu_glfw glfw)
endif()
