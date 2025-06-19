# Pulse - Audio Visualizer

Ever wanted to *see* your music? Pulse is a real-time audio visualizer inspired by [MiniMeters](https://minimeters.app/) that transforms your system's audio into eye-catching (maybe?) visual effects. Built in C++ and powered by SDL2, OpenGL, PulseAudio, and FFTW, Pulse delivers smooth, responsive visuals that move in sync with your music (unless your system sucks :D).

## Current Status

⚠️ **Early Development Notice**: Pulse is still in active development and comes with a few rough edges:

- **Phosphor effect is incompatible with light themes or themes that have a white or near-white background.** The effect relies on additive blending, which is only visible on dark backgrounds.
- No settings menu or source selection yet
- Configuration is handled through a config file (see [Building](#building) for more details)
- Performance may vary depending on your system's framerate
- Bugs and quirks are to be expected


## Features

- Real-time audio visualization
- Multiple visualization styles:
	- Lissajous curve with toggleable phosphor-like effect and Catmull-Rom spline interpolation
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

## Platform Support

Pulse is confirmed to work on:
- Linux (with PulseAudio)
- BSD (with PulseAudio)


## Building

First, copy the configuration template to create your config file:

```bash
cp src/config.json.template ~/.config/pulse-visualizer/config.json
```

Additionally, if you want to keep your config in the project root for testing, you can symlink it:

```bash
ln -sf ~/.config/pulse-visualizer/config.json ./config.json
```

Then edit `~/.config/pulse-visualizer/config.json` (or `./config.json` if you symlinked) to set your PulseAudio source and font path.

> **Note:** Font configuration and PulseAudio source are still hardcoded by path/name. You must set the correct values in your config file:
>
> - For the font, set the `"default_font"` field to the full path of your font file.
> - For the PulseAudio source, set the `"default_source"` field to the correct source name (get it via `pactl list sources | grep Name`).


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
