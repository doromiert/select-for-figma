cmake_minimum_required(VERSION 3.21)
project(FigmaGtk VERSION 1.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)
include(CTest)
file(GLOB APP_BUILD_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp data/*.in data/*.js data/*.css data/*.xml data/*.svg data/icons/*.svg)
list(APPEND APP_BUILD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_SOURCE_DIR}/flake.nix" "${CMAKE_CURRENT_SOURCE_DIR}/flake.lock" "${CMAKE_CURRENT_SOURCE_DIR}/package.nix")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${APP_BUILD_SOURCES})
set(APP_SOURCE_HASHES "")
foreach(source IN LISTS APP_BUILD_SOURCES)
  file(SHA256 "${source}" source_hash)
  string(APPEND APP_SOURCE_HASHES "${source_hash}")
endforeach()
string(SHA256 APP_BUILD_ID "${APP_SOURCE_HASHES}")
configure_file(data/build-info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/build-info.hpp" @ONLY)


find_package(PkgConfig REQUIRED)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/icons.c"
  COMMAND "${GLIB_COMPILE_RESOURCES}" "${CMAKE_CURRENT_SOURCE_DIR}/data/icons.gresource.xml"
    "--sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data" --generate-source "--target=${CMAKE_CURRENT_BINARY_DIR}/icons.c"
  DEPENDS data/clipboard.js data/icons.gresource.xml data/icons/external-link-symbolic.svg data/zane.js data/zane-native.js data/adwaita.css VERBATIM)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4>=4.16 libadwaita-1>=1.8)
pkg_check_modules(SUPPORT REQUIRED IMPORTED_TARGET json-glib-1.0 epoxy fontconfig freetype2 libudev libsodium libsecret-1 libsoup-3.0)

if(DEFINED ENV{CEF_ROOT})
  set(CEF_DEFAULT_ROOT "$ENV{CEF_ROOT}")
else()
  set(CEF_DEFAULT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
endif()
set(CEF_ROOT "${CEF_DEFAULT_ROOT}" CACHE PATH "Extracted CEF binary distribution")
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(FATAL_ERROR "CEF SDK missing. Run python3 tools/fetch-cef.py, or set -DCEF_ROOT=/path/to/cef")
endif()
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
set(USE_SANDBOX OFF CACHE BOOL "CEF static sandbox library (not used on Linux)")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

add_executable(figma-gtk
  "${CMAKE_CURRENT_BINARY_DIR}/icons.c"
  src/main.cpp src/application.cpp src/workspace-tabs.cpp src/browser.cpp src/zane.cpp src/zane-models.cpp src/cursor.cpp src/clipboard.cpp src/keepassxc.cpp src/policy.cpp src/state.cpp src/fonts.cpp
)
target_include_directories(figma-gtk SYSTEM PRIVATE "${CEF_ROOT}")
target_include_directories(figma-gtk PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(figma-gtk PRIVATE
  libcef_dll_wrapper "${CEF_ROOT}/Release/libcef.so" PkgConfig::GTK PkgConfig::SUPPORT
)
target_compile_definitions(figma-gtk PRIVATE
  CEF_RESOURCE_DIR="${CEF_ROOT}/Resources"
  CEF_BINARY_DIR="${CEF_ROOT}/Release"
  APP_VERSION="${PROJECT_VERSION}"
)
target_compile_options(figma-gtk PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(figma-gtk PROPERTIES OUTPUT_NAME select-for-figma BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}" INSTALL_RPATH "$ORIGIN")
file(GLOB CEF_RUNTIME_FILES "${CEF_ROOT}/Release/*" "${CEF_ROOT}/Resources/*")
foreach(runtime_file IN LISTS CEF_RUNTIME_FILES)
  get_filename_component(runtime_name "${runtime_file}" NAME)
  file(CREATE_LINK "${runtime_file}" "${CMAKE_CURRENT_BINARY_DIR}/${runtime_name}" SYMBOLIC)
endforeach()

install(TARGETS figma-gtk RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/figma-gtk)
file(RELATIVE_PATH APP_LIBDIR_FROM_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
configure_file(data/figma-gtk.in "${CMAKE_CURRENT_BINARY_DIR}/figma-gtk-launcher" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/figma-gtk-launcher" DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME select-for-figma)
install(DIRECTORY "${CEF_ROOT}/Resources/" DESTINATION ${CMAKE_INSTALL_LIBDIR}/figma-gtk)
install(DIRECTORY "${CEF_ROOT}/Release/" DESTINATION ${CMAKE_INSTALL_LIBDIR}/figma-gtk
  FILES_MATCHING PATTERN "*.so*" PATTERN "*.bin" PATTERN "*.json" PATTERN "chrome-sandbox")
install(FILES data/io.github.doromiert.FigmaGtk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.doromiert.FigmaGtk.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/io.github.doromiert.FigmaGtk.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/figma-gtk)
install(FILES "${CEF_ROOT}/LICENSE.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/figma-gtk RENAME CEF_LICENSE.txt)
install(FILES "${CEF_ROOT}/CREDITS.html" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/figma-gtk)

if(BUILD_TESTING)
  find_program(NODE_EXECUTABLE node REQUIRED)
  add_test(NAME zane-cursor-chat COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/zane-chat-test.mjs")
  add_test(NAME zane-editor-actions COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/zane-native-test.mjs")
  add_executable(notification-fixture tests/notification-fixture.cpp)
  target_link_libraries(notification-fixture PRIVATE PkgConfig::GTK)
  add_executable(instance-fixture tests/instance-fixture.cpp)
  target_include_directories(instance-fixture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(instance-fixture PRIVATE PkgConfig::GTK)
  add_executable(policy-test tests/policy-test.cpp src/policy.cpp)
  target_include_directories(policy-test PRIVATE src)
  target_link_libraries(policy-test PRIVATE PkgConfig::GTK)
  add_test(NAME navigation-policy COMMAND policy-test)
  add_executable(state-test tests/state-test.cpp src/state.cpp src/policy.cpp)
  target_include_directories(state-test PRIVATE src)
  target_link_libraries(state-test PRIVATE PkgConfig::SUPPORT PkgConfig::GTK)
  add_test(NAME persistent-state COMMAND state-test)
  add_executable(keepassxc-test tests/keepassxc-test.cpp src/keepassxc.cpp src/policy.cpp)
  target_include_directories(keepassxc-test PRIVATE src)
  target_link_libraries(keepassxc-test PRIVATE PkgConfig::SUPPORT PkgConfig::GTK)
  add_test(NAME keepassxc-protocol COMMAND keepassxc-test)
endif()
