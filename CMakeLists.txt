cmake_minimum_required(VERSION 3.10)
project(Pulse)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if config.hpp exists, if not provide helpful error message
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/config.hpp")
  message(FATAL_ERROR "
  ================================================
  CONFIGURATION ERROR
  ================================================
  
  The file 'src/config.hpp' does not exist.
  
  Before building, you need to:
  1. Copy src/config.hpp.template to src/config.hpp
  2. Edit src/config.hpp to configure your settings
  
  ================================================
  ")
endif()

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find SDL2 package
find_package(SDL2 REQUIRED)

# Find PulseAudio
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple)

# Find FFTW
pkg_check_modules(FFTW REQUIRED IMPORTED_TARGET fftw3f)

# Find FreeType
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Include directories
include_directories(${SDL2_INCLUDE_DIRS} ${PULSE_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})

# Add executable
file(GLOB SRC_FILES src/*.cpp)
add_executable(Pulse ${SRC_FILES})

# Link libraries
target_link_libraries(Pulse PRIVATE
  ${SDL2_LIBRARIES}
  PkgConfig::PULSE
  PkgConfig::FFTW
  PkgConfig::FREETYPE
  OpenGL::GL
)
