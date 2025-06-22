# Pulse - Audio Visualizer

Ever wanted to *see* your music? Pulse is a real-time audio visualizer inspired by [MiniMeters](https://minimeters.app/) that transforms your system's audio into eye-catching (maybe?) visual effects. Built in C++ and powered by SDL2, OpenGL, PulseAudio, and FFTW, Pulse delivers smooth, responsive visuals that move in sync with your music (unless your system sucks :D).

## Current Status

⚠️ **Early Development Notice**: Pulse is still in active development and comes with a few rough edges:

- **Phosphor effect may need configuration for light themes.** The effect relies on additive blending, which is most visible on dark backgrounds. For light themes, you may need to set `waveform_invert_noise_brightness: 1` in your config file to invert the lissajous phosphor blending for better visibility.
- No settings menu or source selection yet
- Configuration is handled through a config file (see [Building](#building) for more details)
- Performance may vary depending on your system's framerate
- Bugs and quirks are to be expected

## Features

- Real-time audio visualization
- Live configuration updates
- Multiple visualization styles:
	- Lissajous curve with toggleable phosphor-like effect and Catmull-Rom spline interpolation
	- Oscilloscope with configurable gradient modes, Catmull-Rom spline interpolation and toggleable phosphor-like effect
	- FFT with mid/side and right/left modes and sharp/flat note key mode
	- Spectrogram
- Popout windows for visualizers (fixed aspect ratio visualizers are a little janky when resizing)
- Configurable visualizer order
- Low-latency audio processing (PulseAudio **and** PipeWire backends)
- Hardware-accelerated graphics
- Cross-platform support
- Includes a collection of ready-made themes (see the `themes/` folder).

## Dependencies

- C++17 compatible compiler
- CMake 3.10 or newer
- SDL2
- PulseAudio **or** PipeWire (0.3+)
- FFTW3
- FreeType2
- OpenGL
- Nlohmann JSON

## Platform Support

Pulse is confirmed to work on:
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

### Themeing 
To choose a theme, edit the `"default_theme"` field in your config file (`~/.config/pulse-visualizer/config.json`):

```json
"default_theme": "mocha.txt"
```

Replace `"mocha.txt"` with any theme file from the `themes/` directory. All files in that directory are installed to `~/.config/pulse-visualizer/themes/` when you run `sudo ninja install`.

## Usage

After installation, you can run the visualizer from anywhere:

```bash
Pulse
```
## Contributing

Contributions are welcome! If you want to help improve Pulse, feel free to jump in. I use [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to format the code, so please use it to format your code before submitting a pull request.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Support

If you find Pulse useful and want to support its development, you can buy me a coffee at [ko-fi.com/beacrox](https://ko-fi.com/beacrox).

## Acknowledgments

- [MiniMeters](https://minimeters.app/) for inspiration in audio visualization design
- [Richard Andersson](https://richardandersson.net/?p=350) for the Phosphor effect
- SDL2 for cross-platform graphics and input
- PulseAudio for audio capture
- FFTW for fast Fourier transforms
- FreeType for text rendering
