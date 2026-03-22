{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  ninja,
  pkg-config,
  sdl3,
  sdl3-image,
  curl,
  libpulseaudio,
  pipewire,
  fftwFloat,
  freetype,
  libGL,
  yaml-cpp,
  libebur128,
  clang,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "pulse-visualizer";
  version = "1.3.9";

  src = fetchFromGitHub {
    owner = "Audio-Solutions";
    repo = "pulse-visualizer";
    rev = "b873f0e2d47f836d0f9f89d2104be14d301db7c6";
    hash = "sha256-IzJXFbsbpRszJEpU98exK4EKGU8kHH51BZzokJwzPzU=";
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    clang
  ];

  buildInputs = [
    sdl3
    sdl3-image
    libpulseaudio
    pipewire
    fftwFloat
    freetype
    curl
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
