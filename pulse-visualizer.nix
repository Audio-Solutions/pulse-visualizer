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
  version = "unstable-2025-08-08";

  src = fetchFromGitHub {
    owner = "Beacroxx";
    repo = "pulse-visualizer";
    rev = "feeaa8dff66b5d199e25a8ddde58a145ff5f97e5";
    hash = "sha256-rUlTDUDyo7D2qT4mTPbjF98d5CnOBghmn9jYXsPvLzk=";
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
      --replace-warn " -s" ""
  '';

  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];

  postInstall = ''
    ln -s "$out/bin/Pulse" "$out/bin/pulse-visualizer"
  '';

  meta = with lib; {
    description = "Real-time audio visualizer using SDL2, OpenGL, PulseAudio/PipeWire";
    homepage = "https://github.com/Beacroxx/pulse-visualizer";
    license = licenses.gpl3;
    maintainers = [ maintainers.miyu ];
    platforms = [ "x86_64-linux" "aarch64-linux" ];
    mainProgram = "pulse-visualizer";
  };
})
