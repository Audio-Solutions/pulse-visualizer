# pulse-visualizer - Audio Visualizer

![pulse-visualizer in Phosphor Mode](media/viz.gif)

## What Even Is This?

Ever wanted to *see* your music? Pulse is a real-time audio visualizer inspired by [MiniMeters](https://minimeters.app/). It turns whatever your system is playing into eye-candy (or at least, that's the idea). Built in C++ with SDL3, OpenGL, PulseAudio, and FFTW, Pulse tries to make your music look as cool as it sounds. Unless your system sucks, then, well, good luck.

## Features

- Experimental colorful beam
- Real-time, low-latency audio visualization
- CRT phosphor emulation with GPU compute shaders (glow, persistence, all the retro stuff)
  - Curved screen, vignette, grain, chromatic aberration (toggle as you like)
  - Blur, light spread, and overdrive are all tweakable
- Multiple visualizer styles:
  - Lissajous curve (with fancy spline interpolation and stretch modes)
  - Oscilloscope (gradient modes, pitch following, cycle limiting)
  - FFT/Constant-Q (mid/side, right/left, sharp/flat note key)
  - Spectrogram (log/linear scale, interpolation)
  - Peak and LUFS (shortterm, momentary, integrated)
  - VU meter (digital and analog)
- Configurable order at runtime (lost on exit for now)
- Separate FFT/CQT threads for mid/side channels
- Extensive SIMD (AVX2) acceleration
- Live config and theme hot-reloading (no restart needed, mostly)
- Draggable splitters for custom layout
- Mouse interaction: hover FFT for note readout, drag splitters, etc.
- Cross-platform: PulseAudio and PipeWire
- Hardware-accelerated graphics (OpenGL)
- A pile of ready-made themes (see `themes/`)

## Installation

### Arch Linux (AUR)
Just grab it from the AUR as `pulse-visualizer-git`:
```bash
yay -S pulse-visualizer-git
```
### NixOS
Download the `pulse-visualizer.nix` and add this to your flake:
```nix
nixpkgs.config.packageOverrides = pkgs: {
  pulse-visualizer = pkgs.callPackage /home/user/flake/pulse-visualizer.nix { };
};

environment.systemPackages = [ pkgs.pulse-visualizer ];
```

### Nix
Change directory to the project root and add it to your shell via:
```bash
nix-shell
```
### Other Distros 
You can get the binary from the [Releases page](https://github.com/Beacroxx/pulse-visualizer/releases/latest). There is an install script in the tarball, make sure to run it as root before running the binary:
```bash
tar -xvf pulse-visualizer-<version>.tar.gz
cd pulse-visualizer-<version>
chmod +x install.sh
sudo ./install.sh
```
Alternatively, you can build from source:

#### Build dependencies

- C++17 compiler
- CMake 3.10+
- Ninja-build
- Development headers for: SDL3, PulseAudio or PipeWire (0.3+), FFTW3, FreeType2, GLEW, YAML-CPP, libebur128

Fedora:  
```sudo dnf5 install SDL3-devel fftw3-devel freetype-devel glew-devel yaml-cpp-devel ninja pipewire-devel libebur128-devel```  
Debian:  
```sudo apt install clang cmake libsdl3-dev libfftw3-dev libfreetype-dev libglew-dev libyaml-cpp-dev ninja libebur128-dev```  

```bash
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
sudo ninja install
```

On first run, config and themes get copied to `~/.config/pulse-visualizer/` if they're not already there.

#### Note 
 Do not build with `sudo ninja install`. This will cause ownership and permission issues. Please run the build as a regular user (`ninja`) and only use `sudo ninja install` for the install step.

## Dependencies & Platform Support

- SDL3
- PulseAudio **or** PipeWire (0.3+)
- FFTW3
- FreeType2
- OpenGL
- GLEW
- YAML-CPP
- libebur128

**Works on:**
- Linux (PulseAudio or PipeWire)
- BSD (PulseAudio)

## Configuration

Pulse doesn't have a settings menu (and probably never will), so you'll be editing a YAML config file. On first run, it copies a template to `~/.config/pulse-visualizer/config.yml`.

For comprehensive configuration documentation, see [CONFIGURATION.md](CONFIGURATION.md).

### Quick Configuration

The most important settings to configure:

- `audio`: Set your audio backend and input device
- `font`: Set your font path
- `window`: Set window size, theme, and FPS limit
- `phosphor`, `fft`, `lissajous`, `oscilloscope`: Fine-tune visualizers and effects

For all available options and detailed descriptions, see `/usr/local/share/pulse-visualizer/config.yml.template`.

### Theming

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
pulse-visualizer
```

- Drag splitters to resize/rearrange visualizers
- Lissajous will enforce a square aspect ratio
- Hover FFT for real-time frequency, dB, and note readout
- All config and theme changes are live, no restart needed

## Contributing & Support

Want to help? PRs welcome! Please clang-format your code before submitting (see the clang-format file). Sorry for the chaos.

See [CONTRIBUTORS](CONTRIBUTORS) for a list of contributors to this project.

If you like pulse-visualizer, you can buy me a coffee at [ko-fi.com/beacrox](https://ko-fi.com/beacrox).

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [MiniMeters](https://minimeters.app/) for inspiration
- [Richard Andersson](https://richardandersson.net/?p=350) for the Phosphor effect
- [JetBrains](https://www.jetbrains.com/) and the [Nerd Fonts](https://www.nerdfonts.com/) project for the JetBrains Mono Nerd Font
- SDL3, PulseAudio, PipeWire, FFTW, FreeType, libebur128, YAML-CPP, and everyone else who made this possible
