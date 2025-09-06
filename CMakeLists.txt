cmake_minimum_required(VERSION 3.10)
project(Pulse VERSION 1.3.3)

option(CI_SPECIFIC "Enable CI specific configuration" OFF)
option(SKIP_CPACK "Skip packaging configuration" ON)

# Get git version info
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimized Release flags for best performance
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -s")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /fp:fast -flto -Xclang -fcxx-exceptions /DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "/O2 /fp:fast -flto -Xclang -fcxx-exceptions /DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto /DEBUG:NONE")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -mtune=native -ffast-math -flto -DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -Wl,-s")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /fp:fast /GL /DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "/O2 /fp:fast /GL /DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /DEBUG:NONE")
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

if(WIN32)
  include("PkgConfigWindows.cmake")
endif()

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find SDL3 package
find_package(SDL3 REQUIRED)

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

# "Find" WASAPI
if(WIN32)
  set(AUDIO_BACKENDS_FOUND TRUE)
  add_compile_definitions(HAVE_WASAPI=1)
else()
  add_compile_definitions(HAVE_WASAPI=0)
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

# Find libebur128
pkg_check_modules(EBUR128 REQUIRED IMPORTED_TARGET libebur128)

# Include directories
include_directories(${SDL3_INCLUDE_DIRS} ${PULSE_INCLUDE_DIRS} ${FFTW_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})

if(WIN32)
  # Set installation prefix to C:\Program Files\pulse-visualizer
  set(CMAKE_INSTALL_PREFIX "C:/Program Files" CACHE PATH "Installation prefix" FORCE)

  add_compile_definitions(PULSE_DATA_DIR="${CMAKE_INSTALL_PREFIX}/pulse-visualizer")
else()
  # Set installation prefix to /usr
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Installation prefix" FORCE)

  # Define data directory for shader loading
  add_compile_definitions(PULSE_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/pulse-visualizer")
endif()

set(PROJECT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(PROJECT_VERSION_FULL "${PROJECT_VERSION_STRING}.${GIT_COMMIT_HASH}")
add_compile_definitions(
    VERSION_STRING="${PROJECT_VERSION_STRING}"
    VERSION_COMMIT="${GIT_COMMIT_HASH}"
    VERSION_FULL="${PROJECT_VERSION_FULL}"
)

message(STATUS "pulse-visualizer version ${PROJECT_VERSION_STRING} commit ${GIT_COMMIT_HASH}")

# Add executable
file(GLOB SRC_FILES src/*.cpp)

if(WIN32)
  # Generate version.rc from template
  configure_file(${CMAKE_SOURCE_DIR}/version.rc.in ${CMAKE_BINARY_DIR}/version.rc @ONLY)

  set(RESOURCE_FILE "resources.rc" "${CMAKE_BINARY_DIR}/version.rc")
else()
  set(RESOURCE_FILE "")
endif()

# Fix Ninja RPATH error on ELF-based or XCOFF platforms
if(UNIX AND NOT APPLE)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

add_executable(pulse-visualizer ${SRC_FILES} ${RESOURCE_FILE})

# Link libraries
target_link_libraries(pulse-visualizer PRIVATE
  ${SDL3_LIBRARIES}
  PkgConfig::FFTW
  PkgConfig::FREETYPE
  OpenGL::GL
  ${GLEW_LIBRARIES}
  yaml-cpp::yaml-cpp
  PkgConfig::EBUR128
)

if(WIN32)
  target_link_libraries(pulse-visualizer PRIVATE ole32 uuid avrt)
endif()

# Link audio backends conditionally
if(PULSE_FOUND)
  target_link_libraries(pulse-visualizer PRIVATE ${PULSE_LIBRARIES})
  target_include_directories(pulse-visualizer PRIVATE ${PULSE_INCLUDE_DIRS})
  target_compile_options(pulse-visualizer PRIVATE ${PULSE_CFLAGS_OTHER})
endif()

if(PIPEWIRE_FOUND)
  target_link_libraries(pulse-visualizer PRIVATE ${PIPEWIRE_LIBRARIES})
  target_include_directories(pulse-visualizer PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
  target_compile_options(pulse-visualizer PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
  target_compile_options(pulse-visualizer PRIVATE -Wno-nan-infinity-disabled)
endif()

#include cpack if skip_cpack is not set
if(WIN32 AND NOT SKIP_CPACK)
  message(STATUS "Packaging is NOT skipped, configuring for installer creation")
  include("PackageWindows.cmake")
elseif(UNIX AND NOT SKIP_CPACK)
  message(STATUS "Packaging is NOT skipped, configuring for installer creation")
  include("PackageLinux.cmake")
else()
  message(STATUS "Packaging is skipped, configuring for local installation")

  # Installation targets
  include(GNUInstallDirs)

  if(WIN32)
    set(CMAKE_INSTALL_BINDIR "${CMAKE_INSTALL_PREFIX}/pulse-visualizer")
    set(CMAKE_INSTALL_DATADIR "${CMAKE_INSTALL_PREFIX}")
  endif()

  # Install the executable
  install(TARGETS pulse-visualizer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  # Install the DLL's on Windows
  if(WIN32)
    file(GLOB DLLS "${CMAKE_BINARY_DIR}/*.dll")
    install(FILES ${DLLS}
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()

  # Install README and LICENSE
  install(FILES README.md CONFIGURATION.md LICENSE CONTRIBUTORS.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer
  )

  # Install shaders directory
  install(DIRECTORY shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/shaders
    FILES_MATCHING PATTERN "*.vert" PATTERN "*.frag" PATTERN "*.comp"
  )

  # Install config template
  if(WIN32)
    install(FILES config.yml.template.windows
      DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer
      RENAME config.yml.template
    )
  else()
    install(FILES config.yml.template
      DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer
    )
  endif()

  # Install font
  if(WIN32)
    install(FILES JetBrainsMonoNerdFont-Medium.ttf
      DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer
    )
  else()
    install(FILES JetBrainsMonoNerdFont-Medium.ttf
      DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/fonts
    )
  endif()

  # Install themes directory
  install(DIRECTORY themes/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/themes
    FILES_MATCHING PATTERN "*.txt"
  )

  if(NOT WIN32)

    # Install icon
    install(FILES media/icon/main.png
      DESTINATION ${CMAKE_INSTALL_DATADIR}/pulse-visualizer/icons
      RENAME "pulse-visualizer.png"
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
  endif()
endif()

include(CheckCXXCompilerFlag)

# Check for GCC/Clang-style AVX2 support
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_MAVX2)

# Check for MSVC-style AVX2 support
check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_ARCH_AVX2)

if(COMPILER_SUPPORTS_MAVX2)
  add_compile_definitions(HAVE_AVX2)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2 -mfma")
elseif(COMPILER_SUPPORTS_ARCH_AVX2)
  add_compile_definitions(HAVE_AVX2)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2")
endif()

unset(HAS_MM256_LOG10_PS CACHE)
unset(HAS_MM256_POW_PS CACHE)

include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
  #include <immintrin.h>
  int main() {
    __m256 x = _mm256_set1_ps(1.0f);
    __m256 y = _mm256_log10_ps(x);
    (void)y;
    return 0;
  }
" HAS_MM256_LOG10_PS)

check_cxx_source_compiles("
  #include <immintrin.h>
  int main() {
    __m256 x = _mm256_set1_ps(1.0f);
    __m256 y = _mm256_pow_ps(x, x);
    (void)y;
    return 0;
  }
" HAS_MM256_POW_PS)

if(HAS_MM256_LOG10_PS)
  add_compile_definitions(HAVE_MM256_LOG10_PS)
endif()

if(HAS_MM256_POW_PS)
  add_compile_definitions(HAVE_MM256_POW_PS)
endif()
