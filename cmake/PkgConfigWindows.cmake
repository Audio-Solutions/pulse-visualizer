cmake_minimum_required(VERSION 3.10)

include(FetchContent)

# Step 1: Download the ZIP
FetchContent_Declare(
  pkg_config
  URL https://sourceforge.net/projects/pkgconfiglite/files/0.28-1/pkg-config-lite-0.28-1_bin-win32.zip/download
)
FetchContent_GetProperties(pkg_config)
if(NOT pkg_config_POPULATED)
  FetchContent_Populate(pkg_config)
endif()

# Step 2: Locate pkg-config.exe
set(PKG_CONFIG_EXE "${pkg_config_SOURCE_DIR}/bin/pkg-config.exe")

if(EXISTS ${PKG_CONFIG_EXE})
  message(STATUS "Found pkg-config.exe at ${PKG_CONFIG_EXE}")
else()
  message(FATAL_ERROR "pkg-config.exe not found after extraction")
endif()

set(PKG_CONFIG_EXECUTABLE "${PKG_CONFIG_EXE}")
