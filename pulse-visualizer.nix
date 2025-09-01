{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  ninja,
  pkg-config,
  sdl3,
  libpulseaudio,
  pipewire,
  fftwFloat,
  freetype,
  glew,
  libGL,
  yaml-cpp,
  libebur128,
  clang,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "pulse-visualizer";
  version = "1.3.1";

  src = fetchFromGitHub {
    owner = "Audio-Solutions";
    repo = "pulse-visualizer";
    rev = "90dce5cc3c01c7a19d48fcb23cdc383cfb6008e8";
    hash = "sha256-Ln3rI2U3dm7oxXeIqPyRqFXcZSKjCK11kkSQvBSmrnU=";
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    clang
  ];

  buildInputs = [
    sdl3
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
      --replace-fail " -march=native" "" \
      --replace-fail " -mtune=native" "" \
      --replace-fail "-Wl,-s" "" \
      --replace-fail " -s" "" \
      --replace-fail 'set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Installation prefix" FORCE)' ""
  '';

  cmakeFlags = [
    "-G Ninja"
    "-DCMAKE_CXX_COMPILER=clang++"
    "-DCMAKE_C_COMPILER=clang"
    "-DCMAKE_BUILD_TYPE=Release"
  ];

  meta = {
    description = "Real-time audio visualizer inspired by MiniMeters";
    homepage = "https://github.com/Audio-Solutions/pulse-visualizer";
    license = lib.licenses.gpl3;
    maintainers = with lib.maintainers; [ miyu ];
    platforms = lib.platforms.x86_64;
    badPlatforms = lib.platforms.darwin;
  };
})
