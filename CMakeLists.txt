cmake_minimum_required(VERSION 3.25)
project(trainz_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRAINZ_BUILD_TESTS "Build the tests (needs libgtest-dev)" ON)
option(TRAINZ_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TRAINZ_WERROR "Treat compiler warnings as errors" OFF)
option(TRAINZ_WITH_JPEG "Decode .jpg images if libjpeg is installed (libjpeg62-turbo-dev)" ON)
option(TRAINZ_BUILD_VIEWER "Build model-viewer if SDL2 and OpenGL are installed (libsdl2-dev, libgl-dev)" ON)
option(TRAINZ_WITH_MPG123 "Decode MP3 sounds if libmpg123 is installed (libmpg123-dev)" ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "trainz-native targets Linux only")
endif()

set(TRAINZ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                    -Woverloaded-virtual)
if(TRAINZ_WERROR)
  list(APPEND TRAINZ_WARNINGS -Werror)
endif()

if(TRAINZ_SANITIZE)
  add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined,float-cast-overflow)
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
  src/content/EngineSpec.cpp
  src/content/GroundFile.cpp
  src/content/ImageFile.cpp
  src/content/JirfFile.cpp
  src/content/Kuid.cpp
  src/content/MeshFile.cpp
  src/content/ObjectContainer.cpp
  src/content/PlacedObject.cpp
  src/content/Mp3Decoder.cpp
  src/content/ReadOnlyFile.cpp
  src/content/SessionFile.cpp
  src/content/SoundFile.cpp
  src/content/TextureDescriptor.cpp
  src/content/TextureFile.cpp
  src/content/TrackLayout.cpp
  src/input/ActionMap.cpp
  src/input/KeyBindings.cpp
  src/game/Drive.cpp
  src/game/GeneratedWorld.cpp
  src/game/RouteDrive.cpp
  src/sim/TrackNetwork.cpp
  src/sim/TrainPhysics.cpp
  src/view/AssetMeshes.cpp
  src/view/FlyCamera.cpp
  src/view/GroundTextures.cpp
  src/view/MeshPreview.cpp
  src/view/RouteCatalog.cpp
  src/view/RouteLoader.cpp
  src/view/RouteScene.cpp
  src/view/SceneRender.cpp
)
target_include_directories(trainz_content PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(trainz_content PRIVATE ${TRAINZ_WARNINGS})

# MP3 decoding is optional: without libmpg123, decodeMp3 throws a clear error.
if(TRAINZ_WITH_MPG123)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(MPG123 QUIET IMPORTED_TARGET libmpg123)
  endif()
endif()
if(TRAINZ_WITH_MPG123 AND MPG123_FOUND)
  message(STATUS "MP3 decoding: libmpg123 ${MPG123_VERSION}")
  target_compile_definitions(trainz_content PRIVATE TRAINZ_HAVE_MPG123=1)
  target_link_libraries(trainz_content PRIVATE PkgConfig::MPG123)
else()
  message(STATUS "MP3 decoding not built: install libmpg123-dev (and pkg-config)")
endif()

# JPEG decoding (src/content/ImageFile.cpp) is optional: without libjpeg,
# decodeJpeg() throws ContentError and the JPEG tests skip.
if(TRAINZ_WITH_JPEG)
  find_package(JPEG QUIET)
endif()
if(TRAINZ_WITH_JPEG AND JPEG_FOUND)
  target_link_libraries(trainz_content PUBLIC JPEG::JPEG)
  target_compile_definitions(trainz_content PUBLIC TRAINZ_HAVE_JPEG=1)
  message(STATUS "JPEG support: yes (${JPEG_LIBRARIES})")
else()
  message(STATUS "JPEG support: no (install libjpeg62-turbo-dev)")
endif()

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

add_executable(sound-tool tools/sound_tool.cpp)
target_link_libraries(sound-tool PRIVATE trainz_content)
target_compile_options(sound-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(input-tool tools/input_tool.cpp)
target_link_libraries(input-tool PRIVATE trainz_content)
target_compile_options(input-tool PRIVATE ${TRAINZ_WARNINGS})

add_executable(route-tool tools/route_tool.cpp)
target_link_libraries(route-tool PRIVATE trainz_content)
target_compile_options(route-tool PRIVATE ${TRAINZ_WARNINGS})

if(TRAINZ_BUILD_VIEWER)
  find_package(SDL2 QUIET)
  find_package(OpenGL QUIET)
  # SDL and GL headers use C idioms, so these programs get the basic warnings only.
  if(SDL2_FOUND)
    add_executable(sound-player tools/viewer/sound_player.cpp)
    target_link_libraries(sound-player PRIVATE trainz_content SDL2::SDL2)
    target_compile_options(sound-player PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
    # SDL key mapping: a small library so its tests run without a window.
    add_library(trainz_input_sdl src/input/SdlKeys.cpp src/input/SdlGamepad.cpp)
    target_link_libraries(trainz_input_sdl PUBLIC trainz_content SDL2::SDL2)
    target_compile_options(trainz_input_sdl PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
    add_executable(drive-demo tools/game/drive_demo.cpp)
    target_link_libraries(drive-demo PRIVATE trainz_content SDL2::SDL2)
    target_compile_options(drive-demo PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
    add_executable(route-viewer tools/viewer/route_viewer.cpp)
    target_link_libraries(route-viewer PRIVATE trainz_content trainz_input_sdl SDL2::SDL2)
    target_compile_options(route-viewer PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
    add_executable(key-tester tools/viewer/key_tester.cpp)
    target_link_libraries(key-tester PRIVATE trainz_input_sdl)
    target_compile_options(key-tester PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
  else()
    message(STATUS "sound-player, key-tester, route-viewer and model-viewer not built: install libsdl2-dev")
  endif()
  if(SDL2_FOUND AND OpenGL_FOUND)
    add_executable(model-viewer tools/viewer/model_viewer.cpp)
    target_link_libraries(model-viewer PRIVATE trainz_content SDL2::SDL2 OpenGL::GL)
    target_compile_options(model-viewer PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
  elseif(SDL2_FOUND)
    message(STATUS "model-viewer not built: install libgl-dev")
  endif()
endif()

# `cmake --install build --prefix ~/.local` puts the tools in ~/.local/bin and the menu entry
# in ~/.local/share/applications (no other files; the game folder is never touched).
include(GNUInstallDirs)
install(TARGETS ja-tool config-tool asset-tool texture-tool mesh-tool anim-tool sound-tool input-tool route-tool
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
foreach(viewer route-viewer drive-demo model-viewer sound-player key-tester)
  if(TARGET ${viewer})
    install(TARGETS ${viewer} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endforeach()
# Version numbers are the owner's (see the CPack section at the end and docs/PACKAGING.md).
set(TRAINZ_PACKAGE_VERSION "OWNER_SET_VERSION" CACHE STRING
    "Debian package version (owner sets this; never invent one)")
if(TARGET route-viewer)
  install(PROGRAMS tools/launcher/trainz-route-viewer DESTINATION ${CMAKE_INSTALL_BINDIR})
  # The menu entry runs the launcher by its full path (the menu does not search ~/.local/bin
  # or /opt), so it is written at install time, when the real prefix is known: `cmake --install
  # --prefix DIR` and CPack's /opt/trainz-native/<version> both work. Its name carries the
  # package version when one is set, so several versions can sit in one menu.
  if(TRAINZ_PACKAGE_VERSION STREQUAL "OWNER_SET_VERSION")
    set(TRAINZ_DESKTOP_NAME "Trainz Route Viewer (TS2010 port)")
  else()
    set(TRAINZ_DESKTOP_NAME "Trainz Route Viewer (TS2010 port) ${TRAINZ_PACKAGE_VERSION}")
  endif()
  configure_file(tools/launcher/trainz-route-viewer.desktop.in
                 ${CMAKE_CURRENT_BINARY_DIR}/launcher/trainz-route-viewer.desktop.in @ONLY)
  install(CODE "
    file(READ \"${CMAKE_CURRENT_BINARY_DIR}/launcher/trainz-route-viewer.desktop.in\" trainz_desktop)
    string(REPLACE \"%%BINDIR%%\" \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\" trainz_desktop \"\${trainz_desktop}\")
    file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/launcher/out/trainz-route-viewer.desktop\" \"\${trainz_desktop}\")
    file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications\" TYPE FILE
         FILES \"${CMAKE_CURRENT_BINARY_DIR}/launcher/out/trainz-route-viewer.desktop\")
  ")
endif()

if(TRAINZ_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME PackageName.Mapping COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_name_test.cmake")
  add_test(NAME RouteHeights.Cli
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_route_heights_cli.py"
            "$<TARGET_FILE:route-tool>")
  set_tests_properties(RouteHeights.Cli PROPERTIES TIMEOUT 120)
  add_test(NAME RouteTrack.Cli
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_route_track_cli.py"
            "$<TARGET_FILE:route-tool>"
            "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ts2010")
  set_tests_properties(RouteTrack.Cli PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
  if(TARGET drive-demo)
    add_test(NAME DriveDemo.Cli
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_drive_demo_cli.py" "$<TARGET_FILE:drive-demo>")
    set_tests_properties(DriveDemo.Cli PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77
                         ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:abort_on_error=1")
  endif()
  if(TARGET route-viewer)
    add_test(NAME RouteViewer.Cli
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_route_viewer_cli.py"
              "$<TARGET_FILE:route-viewer>"
              "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ts2010")
    # SDL keeps some memory until exit on purpose; leak reports from it are not ours.
    set_tests_properties(RouteViewer.Cli PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77
                         ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:abort_on_error=1")
    add_test(NAME Launcher.Cli
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_launcher_cli.py"
              "${CMAKE_CURRENT_SOURCE_DIR}/tools/launcher/trainz-route-viewer"
              "$<TARGET_FILE:route-viewer>"
              "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ts2010")
    set_tests_properties(Launcher.Cli PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77
                         ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:abort_on_error=1")
  endif()
  add_executable(content_tests
    tests/test_action_map.cpp
    tests/test_animation.cpp
    tests/test_archive.cpp
    tests/test_asset_config.cpp
    tests/test_asset_index.cpp
    tests/test_cfil.cpp
    tests/test_ground.cpp
    tests/test_image.cpp
    tests/test_keybindings.cpp
    tests/test_mesh.cpp
    tests/test_mp3.cpp
    tests/test_corruption.cpp
    tests/test_path_kuid.cpp
    tests/test_real_install.cpp
    tests/test_route_objects.cpp
    tests/test_safe_output.cpp
    tests/test_scene_render.cpp
    tests/test_session.cpp
    tests/test_sim.cpp
    tests/test_game.cpp
    tests/test_sound.cpp
    tests/test_texture.cpp
    tests/test_texture_descriptor.cpp
  )
  target_link_libraries(content_tests PRIVATE trainz_content GTest::gtest_main)
  target_include_directories(content_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  include(GoogleTest)
  # RealInstall.* tests skip themselves unless TRAINZ_DATA_DIR is set.
  gtest_discover_tests(content_tests DISCOVERY_MODE PRE_TEST)
  if(TARGET trainz_input_sdl)
    add_executable(sdl_input_tests tests/test_sdl_keys.cpp tests/test_sdl_gamepad.cpp)
    target_link_libraries(sdl_input_tests PRIVATE trainz_input_sdl GTest::gtest_main)
    target_compile_options(sdl_input_tests PRIVATE -Wall -Wextra $<$<BOOL:${TRAINZ_WERROR}>:-Werror>)
    gtest_discover_tests(sdl_input_tests DISCOVERY_MODE PRE_TEST)
  endif()
endif()

# ---------------------------------------------------------------------------
# CPack — Debian .deb for Hearth OS
# ---------------------------------------------------------------------------
# Version numbers are the owner's. Do not invent, bump or apply one here.
# Set TRAINZ_PACKAGE_VERSION when you configure, then run cpack -G DEB.
# Each version is its own package name and lives under
# /opt/trainz-native/<version>/, so an older install stays startable beside a
# newer one (no shared paths, no Conflicts on the previous package).
# TRAINZ_PACKAGE_VERSION is defined with the install rules above.
# Debian package *names* allow fewer characters than versions: map without collisions
# (cmake/TrainzPackageName.cmake; e.g. 0~ci -> 0+tci, 1-rc1 -> 1+mrc1).
include(cmake/TrainzPackageName.cmake)
if(TRAINZ_PACKAGE_VERSION STREQUAL "OWNER_SET_VERSION")
  set(TRAINZ_PACKAGE_NAME_VERSION "owner+mset+mversion")  # a placeholder; the owner sets the real one
else()
  trainz_package_name_part("${TRAINZ_PACKAGE_VERSION}" TRAINZ_PACKAGE_NAME_VERSION)
endif()
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "trainz-native-${TRAINZ_PACKAGE_NAME_VERSION}")
set(CPACK_PACKAGE_VERSION "${TRAINZ_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VENDOR "Dragongamer9")
set(CPACK_PACKAGE_CONTACT "Dragongamer9")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "TS2010 native tools and route viewer (read-only) for Hearth OS")
set(CPACK_PACKAGE_DESCRIPTION
    "Command-line tools and a route viewer that read a Trainz Simulator 2010 install. Not the playable game.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/trainz-native/${TRAINZ_PACKAGE_VERSION}")
# Install with CMAKE_INSTALL_PREFIX = that prefix and the staging folder as DESTDIR, so the
# menu entry written at install time names /opt/trainz-native/<version>/bin, not the staging path.
set(CPACK_SET_DESTDIR ON)
set(CPACK_INSTALL_PREFIX "${CPACK_PACKAGING_INSTALL_PREFIX}")  # with SET_DESTDIR, CPack installs to this prefix
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dragongamer9")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# Debian 13 renamed libmpg123-0 to libmpg123-0t64 (both names exist on Hearth OS).
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libsdl2-2.0-0, libgl1, libmpg123-0t64 | libmpg123-0, libjpeg62-turbo")
# No Conflicts/Replaces on other trainz-native-* versions: they must coexist.
include(CPack)
