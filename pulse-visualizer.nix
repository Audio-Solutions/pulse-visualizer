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
  version = "1.3.7";

  src = fetchFromGitHub {
    owner = "Audio-Solutions";
    repo = "pulse-visualizer";
    rev = "a9de4d164b9c13e68b6032faff2bc81b4623a695";
    hash = "sha256-qUpBWmZSLeXVOKKH+hntzgEjftH6IntEOjzdqEBFUjc=";
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
