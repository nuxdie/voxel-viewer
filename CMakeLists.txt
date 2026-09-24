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
option(VV_BUNDLE_GLFW "Download and statically link GLFW (for portable release builds)" OFF)
option(VV_STATIC_LIBSTDCXX "Statically link libstdc++/libgcc (for portable release builds)" OFF)

if(VV_BUNDLE_GLFW)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
    URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9)
  FetchContent_MakeAvailable(glfw)
else()
  find_package(glfw3 3.3 QUIET)
endif()
if(NOT TARGET glfw)
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
  src/smooth_mesher.cpp
  src/splats.cpp
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
option(VV_GLES "Render with OpenGL ES 3.0 (tests the Android code path on desktop)" OFF)
if(VV_GLES)
  target_compile_definitions(voxel-viewer PRIVATE VV_GLES=1)
endif()
target_compile_options(voxel-viewer PRIVATE -Wall -Wextra)
if(TARGET glfw)
  target_link_libraries(voxel-viewer PRIVATE voxcore glfw OpenGL::GL ${CMAKE_DL_LIBS})
else()
  target_link_libraries(voxel-viewer PRIVATE voxcore PkgConfig::GLFW OpenGL::GL ${CMAKE_DL_LIBS})
endif()

if(VV_STATIC_LIBSTDCXX)
  target_link_options(voxel-viewer PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Headless tool: prints model statistics, useful for testing loaders.
add_executable(voxinfo tools/voxinfo.cpp)
target_link_libraries(voxinfo PRIVATE voxcore)
if(VV_STATIC_LIBSTDCXX)
  target_link_options(voxinfo PRIVATE -static-libstdc++ -static-libgcc)
endif()

install(TARGETS voxel-viewer voxinfo RUNTIME DESTINATION bin)

enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME loaders
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_samples.py
                   $<TARGET_FILE:voxinfo> ${CMAKE_CURRENT_BINARY_DIR}/samples)
endif()

add_executable(smooth_mesher_test tests/smooth_mesher_test.cpp)
target_link_libraries(smooth_mesher_test PRIVATE voxcore)
add_test(NAME smooth_mesher COMMAND smooth_mesher_test)
