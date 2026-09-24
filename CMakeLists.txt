cmake_minimum_required(VERSION 3.16)
project(voxel_viewer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
endif()

add_library(voxcore STATIC
  src/nbt.cpp
  src/vox_loader.cpp
  src/schematic_loader.cpp
  src/block_colors.cpp
  src/loader.cpp
  src/mesher.cpp
  src/image_writer.cpp
)
target_include_directories(voxcore PUBLIC src)
target_link_libraries(voxcore PUBLIC ZLIB::ZLIB Threads::Threads)
target_compile_options(voxcore PRIVATE -Wall -Wextra)

add_executable(voxel-viewer
  src/main.cpp
  src/gl_loader.cpp
  src/renderer.cpp
  src/camera.cpp
)
target_compile_options(voxel-viewer PRIVATE -Wall -Wextra)
if(TARGET glfw)
  target_link_libraries(voxel-viewer PRIVATE voxcore glfw OpenGL::GL ${CMAKE_DL_LIBS})
else()
  target_link_libraries(voxel-viewer PRIVATE voxcore PkgConfig::GLFW OpenGL::GL ${CMAKE_DL_LIBS})
endif()

# Headless tool: prints model statistics, useful for testing loaders.
add_executable(voxinfo tools/voxinfo.cpp)
target_link_libraries(voxinfo PRIVATE voxcore)

install(TARGETS voxel-viewer voxinfo RUNTIME DESTINATION bin)

enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME loaders
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_samples.py
                   $<TARGET_FILE:voxinfo> ${CMAKE_CURRENT_BINARY_DIR}/samples)
endif()
