{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  ninja,
  pkg-config,
  SDL2,
  libpulseaudio,
  pipewire,
  fftwFloat,
  freetype,
  glew,
  libGL,
  yaml-cpp,
  libebur128,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "pulse-visualizer";
  version = "1.0.1";

  src = fetchFromGitHub {
    owner = "Beacroxx";
    repo = "pulse-visualizer";
    rev = "882d6072f651ae00fcba66c9c2d5df2835b6cf61";
    hash = "sha256-OR7ahdKaj5vPB6ab8TTAX4cb4DApND0XcWUX22sJ5is=";
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ];

  buildInputs = [
    SDL2
    libpulseaudio
    pipewire
    fftwFloat
    freetype
    glew
    libGL
    yaml-cpp
    libebur128
  ];

  strictDeps = true;
  enableParallelBuilding = true;

  postPatch = ''
    substituteInPlace CMakeLists.txt \
      --replace-warn " -march=native" "" \
      --replace-warn " -mtune=native" "" \
      --replace-warn "-Wl,-s" "" \
      --replace-warn " -s" "" \
      --replace-warn 'set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Installation prefix" FORCE)' ""
  '';

  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

  meta = with lib; {
    description = "Real-time audio visualizer using SDL2, OpenGL, PulseAudio/PipeWire";
    homepage = "https://github.com/Beacroxx/pulse-visualizer";
    license = licenses.gpl3;
    maintainers = [ maintainers.miyu ];
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
})
