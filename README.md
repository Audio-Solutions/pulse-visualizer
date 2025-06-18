# Pulse - Audio Visualizer

Ever wanted to *see* your music? Pulse is a real-time audio visualizer that transforms your system's audio into eye-catching (maybe?) visual effects. Built in C++ and powered by SDL2, OpenGL, PulseAudio, and FFTW, Pulse delivers smooth, responsive visuals that move in sync with your music (unless your system sucks :D).

## Current Status

⚠️ **Early Development Notice**: Pulse is still in active development and comes with a few rough edges:

- Font configuration is hardcoded (update [config.hpp line 62](src/config.hpp#L62))
- PulseAudio source is hardcoded (get the name via `pactl list sources | grep Name` and update [config.hpp line 58](src/config.hpp#L58))
- No settings menu or source selection yet
- Configuration is minimal and handled through a header file
- Performance may vary depending on your system's framerate
- Bugs and quirks are to be expected


## Features

- Real-time audio visualization
- Multiple visualization styles:
	- Lissajous curve with toggleable phosphor-like effect
	- Oscilloscope
	- FFT
- Low-latency audio processing
- Hardware-accelerated graphics
- Cross-platform support


## Dependencies

- C++17 compatible compiler
- CMake 3.10 or newer
- SDL2
- PulseAudio
- FFTW3
- FreeType2
- OpenGL


## Building

First, copy the configuration template to create your config file:

```bash
cp src/config.hpp.template src/config.hpp
```

Then edit `src/config.hpp` to set your PulseAudio source and font path.

Now you can build the project:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```


## Usage

After building, just run the executable:

```bash
./Pulse
```


## Contributing

Contributions are welcome! If you want to help improve Pulse, feel free to jump in.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Support

If you find Pulse useful and want to support its development, you can buy me a coffee at [ko-fi.com/beacrox](https://ko-fi.com/beacrox).

## Acknowledgments

- [MiniMeters](https://minimeters.app/) for inspiration in audio visualization design
- SDL2 for cross-platform graphics and input
- PulseAudio for audio capture
- FFTW for fast Fourier transforms
- FreeType for text rendering
