cmake_minimum_required(VERSION 3.25)
project(trainz_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRAINZ_BUILD_TESTS "Build the tests (needs libgtest-dev)" ON)
option(TRAINZ_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TRAINZ_WERROR "Treat compiler warnings as errors" OFF)
option(TRAINZ_BUILD_VIEWER "Build model-viewer if SDL2 and OpenGL are installed (libsdl2-dev, libgl-dev)" ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "trainz-native targets Linux only")
endif()

set(TRAINZ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                    -Woverloaded-virtual)
if(TRAINZ_WERROR)
  list(APPEND TRAINZ_WARNINGS -Werror)
endif()

if(TRAINZ_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(trainz_content
  src/common/DataDirs.cpp
  src/common/PngWriter.cpp
  src/common/SafeOutput.cpp
  src/content/AnimationFile.cpp
  src/content/ArchivePath.cpp
  src/content/AssetConfig.cpp
  src/content/AssetIndex.cpp
  src/content/CfilCodec.cpp
  src/content/JaArchive.cpp
  src/content/GroundFile.cpp
  src/content/JirfFile.cpp
  src/content/Kuid.cpp
  src/content/MeshFile.cpp
  src/content/ReadOnlyFile.cpp
  src/content/TextureFile.cpp
  src/view/MeshPreview.cpp
)
target_include_directories(trainz_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(trainz_content PRIVATE ${TRAINZ_WARNINGS})

add_executable(ja-tool tools/ja_tool.cpp)
target_link_libraries(ja-tool PRIVATE trainz_content)
target_compile_options(ja-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(config-tool tools/config_tool.cpp)
target_link_libraries(config-tool PRIVATE trainz_content)
target_compile_options(config-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(asset-tool tools/asset_tool.cpp)
target_link_libraries(asset-tool PRIVATE trainz_content)
target_compile_options(asset-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(texture-tool tools/texture_tool.cpp)
target_link_libraries(texture-tool PRIVATE trainz_content)
target_compile_options(texture-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(mesh-tool tools/mesh_tool.cpp)
target_link_libraries(mesh-tool PRIVATE trainz_content)
target_compile_options(mesh-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(anim-tool tools/anim_tool.cpp)
target_link_libraries(anim-tool PRIVATE trainz_content)
target_compile_options(anim-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(route-tool tools/route_tool.cpp)
target_link_libraries(route-tool PRIVATE trainz_content)
target_compile_options(route-tool PRIVATE ${TRAINZ_WARNINGS})

if(TRAINZ_BUILD_VIEWER)
  find_package(SDL2 QUIET)
  find_package(OpenGL QUIET)
  if(SDL2_FOUND AND OpenGL_FOUND)
    add_executable(model-viewer tools/viewer/model_viewer.cpp)
    target_link_libraries(model-viewer PRIVATE trainz_content SDL2::SDL2 OpenGL::GL)
    # SDL and GL headers use C idioms, so the viewer gets the basic warnings only.
    target_compile_options(model-viewer PRIVATE -Wall -Wextra)
    if(TRAINZ_WERROR)
      target_compile_options(model-viewer PRIVATE -Werror)
    endif()
  else()
    message(STATUS "model-viewer not built: install libsdl2-dev and libgl-dev")
  endif()
endif()

if(TRAINZ_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(content_tests
    tests/test_animation.cpp
    tests/test_archive.cpp
    tests/test_asset_config.cpp
    tests/test_asset_index.cpp
    tests/test_cfil.cpp
    tests/test_ground.cpp
    tests/test_mesh.cpp
    tests/test_corruption.cpp
    tests/test_path_kuid.cpp
    tests/test_real_install.cpp
    tests/test_safe_output.cpp
    tests/test_texture.cpp
  )
  target_link_libraries(content_tests PRIVATE trainz_content GTest::gtest_main)
  target_include_directories(content_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  include(GoogleTest)
  # RealInstall.* tests skip themselves unless TRAINZ_DATA_DIR is set.
  gtest_discover_tests(content_tests DISCOVERY_MODE PRE_TEST)
endif()
