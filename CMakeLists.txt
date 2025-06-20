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

# Custom install target for user configuration
install(CODE "
  # Get the original user's home directory (works with sudo)
  if(DEFINED ENV{SUDO_USER})
    set(CURRENT_USER \$ENV{SUDO_USER})
  else()
    execute_process(COMMAND whoami OUTPUT_VARIABLE CURRENT_USER OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
  
  execute_process(COMMAND getent passwd \${CURRENT_USER} OUTPUT_VARIABLE PASSWD_LINE OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REPLACE \":\" \";\" PASSWD_FIELDS \${PASSWD_LINE})
  list(GET PASSWD_FIELDS 5 USER_HOME)
  
  # Create user config directories
  execute_process(COMMAND \${CMAKE_COMMAND} -E make_directory \${USER_HOME}/.config/pulse-visualizer)
  execute_process(COMMAND \${CMAKE_COMMAND} -E make_directory \${USER_HOME}/.config/pulse-visualizer/themes)
  
  # Copy config template to user config
  execute_process(COMMAND \${CMAKE_COMMAND} -E copy_if_different
    \"${CMAKE_CURRENT_SOURCE_DIR}/config.json.template\"
    \"\${USER_HOME}/.config/pulse-visualizer/config.json\"
  )
  
  # Copy themes to user config (using shell command to avoid CMake GLOB issues)
  execute_process(COMMAND bash -c \"cp '${CMAKE_CURRENT_SOURCE_DIR}/themes/'*.txt '\${USER_HOME}/.config/pulse-visualizer/themes/'\" RESULT_VARIABLE COPY_RESULT)
  
  # Change ownership back to the original user
  execute_process(COMMAND chown -R \${CURRENT_USER}:\${CURRENT_USER} \${USER_HOME}/.config/pulse-visualizer)
  
  if(COPY_RESULT EQUAL 0)
    message(STATUS \"Installed user configuration to \${USER_HOME}/.config/pulse-visualizer/\")
  else()
    message(WARNING \"Failed to copy themes to \${USER_HOME}/.config/pulse-visualizer/themes/\")
  endif()
")