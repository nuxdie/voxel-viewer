cmake_minimum_required(VERSION 3.18)
project(voxelviewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The viewer engine is shared with the desktop build.
set(VV_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)

add_library(voxelviewer SHARED
  jni_bridge.cpp
  ${VV_SRC}/nbt.cpp
  ${VV_SRC}/vox_loader.cpp
  ${VV_SRC}/schematic_loader.cpp
  ${VV_SRC}/block_colors.cpp
  ${VV_SRC}/loader.cpp
  ${VV_SRC}/mesher.cpp
  ${VV_SRC}/smooth_mesher.cpp
  ${VV_SRC}/splats.cpp
  ${VV_SRC}/light_field.cpp
  ${VV_SRC}/renderer.cpp
  ${VV_SRC}/camera.cpp
)
target_include_directories(voxelviewer PRIVATE ${VV_SRC})
target_compile_options(voxelviewer PRIVATE -Wall -Wextra -O2)
target_link_libraries(voxelviewer PRIVATE GLESv3 log z)
