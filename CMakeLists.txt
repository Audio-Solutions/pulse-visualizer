cmake_minimum_required(VERSION 3.10)
project(Pulse)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
add_executable(Pulse
  src/main.cpp
  src/graphics.cpp
  src/audio_processing.cpp
  src/visualizers.cpp
)

# Link libraries
target_link_libraries(Pulse PRIVATE
  ${SDL2_LIBRARIES}
  PkgConfig::PULSE
  PkgConfig::FFTW
  PkgConfig::FREETYPE
  OpenGL::GL
)
