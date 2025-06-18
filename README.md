# Pulse - Audio Visualizer

Ever wanted to *see* your music? Pulse is a real-time audio visualizer inspired by [MiniMeters](https://minimeters.app/) that transforms your system's audio into eye-catching (maybe?) visual effects. Built in C++ and powered by SDL2, OpenGL, PulseAudio, and FFTW, Pulse delivers smooth, responsive visuals that move in sync with your music (unless your system sucks :D).

## Current Status

⚠️ **Early Development Notice**: Pulse is still in active development and comes with a few rough edges:

- Font configuration is hardcoded (update [config.hpp line 70](src/config.hpp.template#L70))
- PulseAudio source is hardcoded (get the name via `pactl list sources | grep Name` and update [config.hpp line 65](src/config.hpp.template#L65))
- No settings menu or source selection yet
- Configuration is minimal and handled through a header file
- Performance may vary depending on your system's framerate
- Bugs and quirks are to be expected


## Features

- Real-time audio visualization
- Multiple visualization styles:
	- Lissajous curve with toggleable phosphor-like effect
	- Oscilloscope with configurable gradient modes
	- FFT with mid/side and right/left modes and sharp/flat note key mode
- Low-latency audio processing
- Hardware-accelerated graphics
- Cross-platform support
- Included themes:
	- Mocha (Catppuccin)
	- Macchiato (Catppuccin)
	- Frappe (Catppuccin)
	- Latte (Catppuccin)
	- Windows Light
	- Windows Dark
	- Breeze Light
	- Breeze Dark
	- AMOLED
	- CMYK Printer
	- Monochrome Printer
	- Carbonfox (AMOLED Dark)
	- Nightfox

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


### Themeing 
To choose a theme, edit the following line in `theme.cpp`

```cpp
ThemeDefinition ThemeManager::currentTheme = Themes::MOCHA;
```
and replace the `MOCHA` to reflect the value of the theme you wish to apply. Valid values are:
- `MOCHA` (Catppuccin Mocha)
- `MACCHIATO` (Catppuccin Macchiato)
- `FRAPPE` (Catppuccin Frappe)
- `LATTE` (Catppuccin Latte)
- `WINDOWS_LIGHT` (Windows Light)
- `WINDOWS_DARK` (Windows Dark)
- `BREEZE_LIGHT` (Breeze Light)
- `BREEZE_DARK` (Breeze Dark)
- `AMOLED` (AMOLED)
- `CMYK_PRINTER` (CMYK Printer)
- `MONOCHROME_PRINTER` (Monochrome Printer)
- `CARBONFOX` (Carbonfox AMOLED Dark)
- `NIGHTFOX` (Nightfox)

\
\
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

Contributions are welcome! If you want to help improve Pulse, feel free to jump in. I use [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to format the code, so please use it to format your code before submitting a pull request.

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
