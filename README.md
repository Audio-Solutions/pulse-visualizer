# Pulse - Audio Visualizer

Ever wanted to *see* your music? Pulse is a real-time audio visualizer inspired by [MiniMeters](https://minimeters.app/) that transforms your system's audio into eye-catching (maybe?) visual effects. Built in C++ and powered by SDL2, OpenGL, PulseAudio, and FFTW, Pulse delivers smooth, responsive visuals that move in sync with your music (unless your system sucks :D).

## Current Status

⚠️ **Early Development Notice**: Pulse is still in active development and comes with a few rough edges:

- No settings menu yet.
- Configuration is handled through a config file (see [Building](#building) for more details).
- Performance may vary depending on your system's framerate.
- Bugs and quirks are to be expected.

## Features

- Real-time audio visualization with low-latency processing
- Enhanced analog CRT phosphor emulation with GPU compute shaders featuring realistic glow and persistence effects
  - **Incompatible with light themes.** (duh)
  - Needs nonzero rgb values for proper overdrive effect.
  - Configurable blur intensity controls with `near_blur_intensity` and `far_blur_intensity` parameters
  - Light spread factor is configurable using `phosphor_range_factor`. **RESOURCE INTENSIVE: $$\mathcal{O}\left(6 \cdot \left(\text{rangefactor} \cdot \text{lineblurspread} \times 2 + 1\right)\right)$$**
  - Blur can be disabled by setting `phosphor_range_factor` to zero
- Multiple visualization styles:
	- Lissajous curve with Catmull-Rom spline interpolation
	- Oscilloscope with configurable gradient modes
	- FFT with mid/side and right/left modes and sharp/flat note key mode, now with Constant-Q transform support
	- Spectrogram
- Live configuration updates
- Cross-platform support (PulseAudio **and** PipeWire backends)
- Hardware-accelerated graphics with OpenGL
- Collection of ready-made themes, including MiniMeters's native themes (see the `themes/` folder)

## Dependencies & Platform Support

**Dependencies:**
- C++17 compatible compiler
- CMake 3.10 or newer
- SDL2
- PulseAudio **or** PipeWire (0.3+)
- FFTW3
- FreeType2
- OpenGL
- GLEW
- YAML-CPP
- Ninja-build

On Fedora, this would be:
```sudo dnf5 install SDL2-devel fftw3-devel freetype-devel glew-devel yaml-cpp-devel ninja pipewire-devel```  
On Debian, this would be:
```sudo apt install clang cmake libsdl2-dev libfftw3-dev libfreetype-dev libglew-dev libyaml-cpp-dev ninja```  
On Archlinux, this would be:
```sudo pacman -S clang cmake sdl2 fftw freetype2 glew yaml-cpp ninja libpipewire```

**Supported Platforms:**
- Linux (PulseAudio **or** PipeWire)
- BSD (PulseAudio)

## Building

Build and install the project:

```bash
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
sudo ninja install
```

The installation will automatically:
- Install the executable to `/usr/local/bin/Pulse`
- Set up user configuration in `~/.config/pulse-visualizer/`
- Install themes to `~/.config/pulse-visualizer/themes/`
- Create desktop integration and man page

> **Note:**
> 1. Select the audio backend in your config: `"audio.engine": "pulseaudio" | "pipewire" | "auto"` (default `auto`).
> 2. Set the input device for the chosen backend:
>    - PulseAudio: `"pulseaudio.default_source"`
>    - PipeWire  : `"pipewire.default_source"`
> 3. Set `"default_font"` to the full path of your font file.

**Themeing:** To choose a theme, edit the `default_theme` field in your config file (`~/.config/pulse-visualizer/config.yml`):

```yaml
window:
  default_theme: green-crt.txt
```

Replace `"green-crt.txt"` with any theme file from the `themes/` directory. All files in that directory are installed to `~/.config/pulse-visualizer/themes/` when you run `sudo ninja install`.

## Usage

After installation, you can run the visualizer from anywhere:

```bash
Pulse
```
## Contributing & Support

Contributions are welcome! If you want to help improve Pulse, feel free to jump in. I use [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to format the code, so please use it to format your code before submitting a pull request. Apologies in advance for my chaotic coding style.

If you find Pulse useful and want to support its development, you can buy me a coffee at [ko-fi.com/beacrox](https://ko-fi.com/beacrox).

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [MiniMeters](https://minimeters.app/) for inspiration in audio visualization design
- [Richard Andersson](https://richardandersson.net/?p=350) for the Phosphor effect
- SDL2 for cross-platform graphics and input
- PulseAudio and PipeWire for audio capture
- FFTW for fast Fourier transforms
- FreeType for text rendering
