# Pulse - Audio Visualizer

![Pulse Audio Visualizer in Phosphor Mode](media/viz.gif)

## What Even Is This?

Ever wanted to *see* your music? Pulse is a real-time audio visualizer inspired by [MiniMeters](https://minimeters.app/). It turns whatever your system is playing into eye-candy (or at least, that's the idea). Built in C++ with SDL2, OpenGL, PulseAudio, and FFTW, Pulse tries to make your music look as cool as it sounds. Unless your system sucks, then, well, good luck.

## Features

- Real-time, low-latency audio visualization
- CRT phosphor emulation with GPU compute shaders (glow, persistence, all the retro stuff)
  - Curved screen, vignette, grain, chromatic aberration (toggle as you like)
  - Blur, light spread, and overdrive are all tweakable
- Multiple visualizer styles:
  - Lissajous curve (with fancy spline interpolation)
  - Oscilloscope (gradient modes, pitch following, cycle limiting)
  - FFT/Constant-Q (mid/side, right/left, sharp/flat note key)
  - Spectrogram (log/linear scale, interpolation)
- Separate FFT/CQT threads for mid/side channels
- SIMD (AVX2) acceleration for FFT/CQT and smoothing/interpolation in audio processing
- Live config and theme hot-reloading (no restart needed, mostly)
- Draggable splitters for custom layout
- Mouse interaction: hover FFT for note readout, drag splitters, etc.
- Cross-platform: PulseAudio **and** PipeWire
- Hardware-accelerated graphics (OpenGL)
- A pile of ready-made themes (see `themes/`)

## Installation

### Arch Linux (AUR)

Just grab it from the AUR as `pulse-visualizer-git`:

```bash
yay -S pulse-visualizer-git
```

### Other Distros (Manual Build)

Clone, build, install:

```bash
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
sudo ninja install
```

This will:
- Put the executable in `/usr/local/bin/Pulse`
- Install system files to `/usr/local/share/pulse-visualizer/`
- Set up desktop integration and man page

On first run, config and themes get copied to `~/.config/pulse-visualizer/` if they're not already there.

## Dependencies & Platform Support

**You need:**
- C++17 compiler
- CMake 3.10+
- SDL2
- PulseAudio **or** PipeWire (0.3+)
- FFTW3
- FreeType2
- OpenGL
- GLEW
- YAML-CPP
- Ninja-build

Fedora:  
```sudo dnf5 install SDL2-devel fftw3-devel freetype-devel glew-devel yaml-cpp-devel ninja pipewire-devel```  
Debian:  
```sudo apt install clang cmake libsdl2-dev libfftw3-dev libfreetype-dev libglew-dev libyaml-cpp-dev ninja```

**Works on:**
- Linux (PulseAudio or PipeWire)
- BSD (PulseAudio)

## Configuration

Pulse doesn't have a settings menu (and probably never will), so you'll be editing a YAML config file. On first run, it copies a template to `~/.config/pulse-visualizer/config.yml`.

The configuration is now organized into logical sections. Here are the most important ones:

- `audio`: Set your audio backend and input device here. For example:
  ```yaml
  audio:
    engine: pipewire   # or pulseaudio, or auto (default)
    device: your_audio_device_name
  ```
  - `engine` chooses the backend (`pipewire`, `pulseaudio`, or `auto`).
  - `device` is the name of your input device (see `pactl list sources short` or `pw-cli ls Node`).
- `font`: Set your font path:
  ```yaml
  font: /full/path/to/your/font.ttf
  ```
- `window`: Set window size, theme, and FPS limit:
  ```yaml
  window:
    default_width: 1080
    default_height: 200
    theme: mocha.txt
    fps_limit: 240
  ```
- `phosphor`, `fft`, `lissajous`, `oscilloscope`, `bandpass_filter`, `spectrogram`, `visualizers`: Fine-tune every aspect of the visualizers and effects. Each section is documented in the config template.

For all available options and detailed descriptions, see `/usr/local/share/pulse-visualizer/config.yml.template`.

### Themeing

To pick a theme, edit the `theme` field in your config:

```yaml
window:
  theme: green-crt.txt
```

Swap `green-crt.txt` for any file in `themes/`. Theme files get copied to `~/.config/pulse-visualizer/themes/` on first run.

Theme files support a bunch of color and property keys (see `_TEMPLATE.txt` for all options). All the main colors are required. You can fine-tune visualizer-specific stuff too. Theme and config changes reload live.

## Usage

Just run:

```bash
Pulse
```

- Drag splitters to resize/rearrange visualizers
- Lissajous will enforce a square aspect ratio
- Hover FFT for real-time frequency, dB, and note readout
- All config and theme changes are live, no restart needed

## Contributing & Support

Want to help? PRs welcome! Please clang-format your code before submitting (see the clang-format file). Sorry for the chaos.

If you like Pulse, you can buy me a coffee at [ko-fi.com/beacrox](https://ko-fi.com/beacrox).

## License

MIT. See [LICENSE](LICENSE).

## Acknowledgments

- [MiniMeters](https://minimeters.app/) for inspiration
- [Richard Andersson](https://richardandersson.net/?p=350) for the Phosphor effect
- SDL2, PulseAudio, PipeWire, FFTW, FreeType, and everyone else who made this possible
