cmake_minimum_required(VERSION 3.10)
project(Pulse VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimized Release flags for best performance
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -s")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -Wl,-s")
endif()

# Add custom Profile build type for profiling with perf
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS_PROFILE "-O1 -g -fno-omit-frame-pointer -fno-inline-functions-called-once -fno-optimize-sibling-calls")
  set(CMAKE_C_FLAGS_PROFILE "-O1 -g -fno-omit-frame-pointer -fno-inline-functions-called-once -fno-optimize-sibling-calls")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS_PROFILE "-O1 -g -fno-omit-frame-pointer -fno-inline")
  set(CMAKE_C_FLAGS_PROFILE "-O1 -g -fno-omit-frame-pointer -fno-inline")
endif()
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "")

# Set available build types
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel;Profile" CACHE STRING "" FORCE)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find SDL2 package
find_package(SDL2 REQUIRED)

# Find audio backends - check for both PulseAudio and PipeWire
set(AUDIO_BACKENDS_FOUND FALSE)

# Find PulseAudio (optional)
pkg_check_modules(PULSE libpulse-simple)
if(PULSE_FOUND)
  message(STATUS "Found PulseAudio: ${PULSE_VERSION}")
  set(AUDIO_BACKENDS_FOUND TRUE)
  add_compile_definitions(HAVE_PULSEAUDIO=1)
else()
  message(STATUS "PulseAudio not found")
  add_compile_definitions(HAVE_PULSEAUDIO=0)
endif()

# Find PipeWire (optional)
pkg_check_modules(PIPEWIRE libpipewire-0.3)
if(PIPEWIRE_FOUND)
  message(STATUS "Found PipeWire: ${PIPEWIRE_VERSION}")
  set(AUDIO_BACKENDS_FOUND TRUE)
  add_compile_definitions(HAVE_PIPEWIRE=1)
else()
  message(STATUS "PipeWire not found")
  add_compile_definitions(HAVE_PIPEWIRE=0)
endif()

# Ensure at least one audio backend is available
if(NOT AUDIO_BACKENDS_FOUND)
  message(FATAL_ERROR "No audio backends found! Please install either PulseAudio (libpulse-simple-dev) or PipeWire (libpipewire-0.3-dev)")
endif()

# Find FFTW
pkg_check_modules(FFTW REQUIRED IMPORTED_TARGET fftw3f)

# Find FreeType
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

# Find OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# Find GLEW
find_package(GLEW REQUIRED)

# Find YAML-CPP
find_package(yaml-cpp REQUIRED)

# Include directories
include_directories(${SDL2_INCLUDE_DIRS} ${PULSE_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})

# Define data directory for shader loading
add_compile_definitions(PULSE_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/pulse-visualizer")

# Add executable
file(GLOB SRC_FILES src/*.cpp)
add_executable(Pulse ${SRC_FILES})

# Link libraries
target_link_libraries(Pulse PRIVATE
  ${SDL2_LIBRARIES}
  PkgConfig::FFTW
  PkgConfig::FREETYPE
  OpenGL::GL
  ${GLEW_LIBRARIES}
  yaml-cpp::yaml-cpp
)

# Link audio backends conditionally
if(PULSE_FOUND)
  target_link_libraries(Pulse PRIVATE ${PULSE_LIBRARIES})
  target_include_directories(Pulse PRIVATE ${PULSE_INCLUDE_DIRS})
  target_compile_options(Pulse PRIVATE ${PULSE_CFLAGS_OTHER})
endif()

if(PIPEWIRE_FOUND)
  target_link_libraries(Pulse PRIVATE ${PIPEWIRE_LIBRARIES})
  target_include_directories(Pulse PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
  target_compile_options(Pulse PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
  target_compile_options(Pulse PRIVATE -Wno-nan-infinity-disabled)
endif()

# Installation targets
include(GNUInstallDirs)

# Install the executable
install(TARGETS Pulse
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install README and LICENSE
install(FILES README.md LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer
)

# Install shaders directory
install(DIRECTORY shaders/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/shaders
  FILES_MATCHING PATTERN "*.vert" PATTERN "*.frag" PATTERN "*.comp"
)

# Install config template
install(FILES config.yml.template
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer
)

# Install font
install(FILES JetBrainsMonoNerdFont-Medium.ttf
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/fonts
)

# Install themes directory
install(DIRECTORY themes/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/themes
  FILES_MATCHING PATTERN "*.txt"
)

# Create a desktop entry
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/pulse-visualizer.desktop.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.desktop"
  @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.desktop"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Create a man page
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/pulse-visualizer.1.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.1"
  @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.1"
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
  add_compile_definitions(HAVE_AVX2)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
endif()
