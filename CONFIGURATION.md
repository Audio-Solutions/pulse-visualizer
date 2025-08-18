# Pulse Audio Visualizer Configuration Guide

This guide provides comprehensive documentation for configuring Pulse Audio Visualizer. The configuration file is located at `~/.config/pulse-visualizer/config.yml` for linux and `C:\Users\<username>\.config\pulse-visualizer\config.yml` for windows and uses YAML format.

## Quick Start

1. Start Pulse at least once to generate the config file
2. Edit the configuration file to match your system

Most config options update live, so you can change them without restarting Pulse, but some may require a restart.

## Boolean Values

Boolean configuration options accept multiple formats:

**True values:**
- `true`
- `yes`
- `on`
- `1`

**False values:**
- `false`
- `no`
- `off`
- `0`

## Configuration Sections

### Version

Controls the version of the configuration file.

```yaml
# This is the version of the configuration file. It should match the version of the application.
# If the version is not compatible, the application will show a warning and exit.
version: v1.2.2
```

### Audio Processing Settings

Controls how audio is captured, processed, and displayed.

```yaml
audio:
  # Audio sample rate in Hz (must match your system's audio output)
  # Common values: 44100, 48000, 96000
  sample_rate: 48000
  
  # Threshold below which audio is considered silent (in dB)
  # Lower values = visualizer will draw even at lower volumes
  silence_threshold: -90.0
  
  # Audio backend: "pulseaudio", "pipewire", "wasapi", or "auto" (auto-detects best available, pipewire first, then pulseaudio, then wasapi)
  # "auto" is recommended for most systems, but you can force a specific backend
  engine: pipewire
  
  # Audio device name (see system audio devices)
  # Find your device with: pactl list sources short (PulseAudio) or pw-cli ls Node (PipeWire)
  device: alsa_output.pci-0000_0e_00.4.iec958-stereo
  
  # Audio gain adjustment in dB (positive=louder, negative=quieter)
  # Adjust if audio is too quiet, like when Spotify or YouTube normalizes the volume
  gain_db: -0.0
```

**Audio Device Examples:**
- PulseAudio: `alsa_output.pci-0000_0e_00.4.iec958-stereo`
- PipeWire: `alsa_output.pci-0000_0e_00.4.iec958-stereo`
- USB Audio: `alsa_output.usb-Focusrite_Scarlett_2i2_USB-00.analog-stereo`
- Bluetooth: `bluez_sink.00_11_22_33_44_55.a2dp_sink`
- Windows: `Speakers (Realtek High Definition Audio)`

### Debug Settings

Controls debugging and logging features.

```yaml
debug:
  # Enable FPS logging to console (true/false)
  # Useful for performance monitoring
  log_fps: false
  
  # Show bandpassed signal on oscilloscope
  # Shows the filtered signal used for pitch detection
  show_bandpassed: false
```

### Phosphor Effect Settings

Controls the CRT phosphor simulation effect (realistic glow and persistence).

**⚠️ WARNING: These settings are computationally expensive and affect performance**

```yaml
phosphor:
  # Enable or disable phosphor effects globally
  enabled: true
  
  # Intensity of blur for nearby pixels (0.0-1.0, higher=more blur)
  near_blur_intensity: 0.3
  
  # Intensity of blur for distant pixels (0.0-1.0, higher=more blur)
  far_blur_intensity: 0.6
  
  # Energy of the electron beam (affects brightness of phosphor effect)
  beam_energy: 150.0
  
  # Slow decay rate of phosphor persistence (higher=longer persistence)
  decay_slow: 7.0
  
  # Fast decay rate of phosphor persistence (higher=shorter persistence)
  decay_fast: 50.0
  
  # Size of the electron beam (affects line thickness)
  beam_size: 1.0
  
  # Spread of the blur effect (higher=more spread, more GPU intensive)
  line_blur_spread: 64
  
  # Width of the phosphor lines
  line_width: 0.5
  
  # Age threshold for phosphor decay (higher=longer persistence)
  age_threshold: 10
  
  # Range factor for blur calculations (higher=more blur, more GPU intensive)
  range_factor: 2.0
  
  # Enable phosphor grain effect (adds realistic CRT noise)
  enable_grain: true
  
  # Grain strength (Spatial noise)
  grain_strength: 0.1
  
  # Tension of Catmull-Rom splines (0.0-1.0, affects curve smoothness)
  tension: 0.5
  
  # Enable curved phosphor screen effect (simulates curved CRT monitor)
  enable_curved_screen: true
  
  # Curvature intensity for curved screen effect (0.0-1.0)
  screen_curvature: 0.1
  
  # Gap factor between screen edges and border (higher=less gap)
  screen_gap: 1.09
  
  # Vignette strength (0.0-1.0)
  vignette_strength: 0.3
  
  # Chromatic aberration strength (0.0-1.0)
  chromatic_aberration_strength: 0.005

  # Enable color beam effect (true/false)
  # This will make the beam color rotate around the hue depending on the direction its going.
  # This is extremely gpu intensive, so it is disabled by default.
  colorbeam: false
```

### FFT (Fast Fourier Transform) Settings

Controls frequency analysis and spectrum visualization.

```yaml
fft:
  # FFT size (must be power of 2, higher=better frequency resolution)
  # Common values: 2048, 4096, 8192, 16384
  size: 4096
  
  # Enable velocity smoothing for FFT values
  enable_smoothing: true
  
  # Fall speed of FFT bars (higher=faster fall, more responsive)
  fall_speed: 100.0
  
  # Fall speed when window is hovered over (higher=faster fall)
  hover_fall_speed: 10.0
  
  # Rise speed of FFT bars (higher=faster rise, more responsive)
  rise_speed: 500.0
  
  # dB level at the top of the display before slope correction
  max_db: 10.0
  
  # dB level at the bottom of the display before slope correction
  min_db: -60.0
  
  # Maximum frequency to display in Hz
  max_freq: 22000.0
  
  # Minimum frequency to display in Hz
  min_freq: 10.0
  
  # Enable frequency markers on the display
  frequency_markers: true
  
  # Slope correction for frequency response (dB per octave, visual only)
  slope_correction_db: 4.5
  
  # Note key mode: "sharp" or "flat" (affects frequency labels)
  note_key_mode: sharp
  
  # Stereo mode: "midside" (mid/side channels) or "leftright" (left/right channels)
  stereo_mode: midside
  
  # Enable Constant-Q Transform (better frequency resolution in the low end)
  enable_cqt: true
  
  # Number of frequency bins per octave for CQT (higher=better resolution)
  # Significant CPU usage increase with higher values
  cqt_bins_per_octave: 60
  
  # Beam multiplier for phosphor effect
  beam_multiplier: 1.0

  # Rotation of the FFT display
  # 0 = 0 degrees
  # 1 = 90 degrees
  # 2 = 180 degrees
  # 3 = 270 degrees
  rotation: 0

  # Flip the FFT display along the frequency axis
  flip_x: false
```

### Font Settings

Controls text rendering for frequency labels and UI elements.

```yaml
# Path to TrueType font file for text rendering
# Common font paths:
font: ~/.local/share/fonts/JetBrainsMono/JetBrainsMonoNerdFont-Medium.ttf
# font: /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
# font: /usr/share/fonts/TTF/CascadiaCode-Regular.ttf
```

### Lissajous Curve Settings

Controls the Lissajous curve visualization (X-Y plot of left vs right channel).

```yaml
lissajous:
  # Enable Catmull-Rom spline interpolation for smoother curves
  enable_splines: true
  
  # Beam multiplier for phosphor effect
  beam_multiplier: 1.0
  
  # Readback multiplier of the data
  # Defines how much of the previous data is redrawn
  # Higher value means more data is redrawn, which stabilizes the lissajous.
  # Caveat is a minor increase in CPU usage.
  readback_multiplier: 3.0
  
  # Options: "circle", "pulsar", "rotate", "black_hole", "none"
  # rotate is a 45 degree rotation of the curve
  # circle is the rotated curve stretched to a circle
  # pulsar is all of the above, makes the outside be silent
  # and the inside be loud which looks really cool
  # black_hole is a rotated circle with a black hole-like effect
  # none is the default mode
  mode: none

  # Rotation of the lissajous display
  # 0 = 0 degrees
  # 1 = 90 degrees
  # 2 = 180 degrees
  # 3 = 270 degrees
  rotation: 0
```

### Oscilloscope Settings

Controls the oscilloscope waveform visualization.

```yaml
oscilloscope:
  # Time window for oscilloscope in ms
  time_window: 50.0
  
  # Follow pitch for phase alignment (true=align to detected pitch)
  follow_pitch: true
  
  # Alignment position: "left", "center", or "right"
  alignment: center
  
  # Alignment type: "peak" or "zero_crossing"
  alignment_type: zero_crossing
  
  # Only track n cycles of the waveform
  limit_cycles: false
  
  # Number of cycles to track when limit_cycles is true
  cycles: 2
  
  # Minimum time window to display in oscilloscope in ms
  min_cycle_time: 16.0
  
  # Beam multiplier for phosphor effect
  beam_multiplier: 1.0
  
  # Enable lowpass filter for oscilloscope
  enable_lowpass: false

  # Rotation of the oscilloscope display
  # 0 = 0 degrees
  # 1 = 90 degrees
  # 2 = 180 degrees
  # 3 = 270 degrees
  rotation: 0

  # Flip the oscilloscope display along the time axis
  flip_x: false
```

### Bandpass Filter Settings

Controls the bandpass filter applied to the audio signal used for pitch following.

```yaml
bandpass_filter:
  # Bandwidth in Hz or percent of center frequency
  bandwidth: 100.0
  
  # Bandwidth type: "hz" for absolute width, "percent" for relative to center frequency
  bandwidth_type: percent
```

### Spectrogram Settings

Controls the spectrogram visualization (time-frequency heat map).

```yaml
spectrogram:
  # Frequency scale: "log" or "linear"
  frequency_scale: log
  
  # Enable interpolation for smoother spectrogram display
  interpolation: true
  
  # Maximum dB level for spectrogram display
  max_db: -10.0
  
  # Maximum frequency to display in Hz
  max_freq: 22000.0
  
  # Minimum dB level for spectrogram display
  min_db: -60.0
  
  # Minimum frequency to display in Hz
  min_freq: 20.0
  
  # Time window for spectrogram in seconds
  time_window: 4.0
```

### LUFS Settings

Controls the LUFS (Loudness Units) visualization.

```yaml
lufs:
  # Mode: "shortterm", "momentary", "integrated"
  # momentary is over a 400ms window
  # shortterm is over a 3s window
  # integrated is over the entire recording
  mode: shortterm

  # Scale: "log" or "linear"
  scale: log

  # Label: "on", "off", "compact"
  # on = full label following the LUFS bar on the right
  # off = no label
  # compact = compact label in the top area
  label: compact
```

### VU Meter Settings

Controls the VU meter visualization.

```yaml

vu:
  # Time window for VU meter in ms
  time_window: 100.0
  # Style: "analog" or "digital"
  style: digital
  # Calibration dB level
  # a calibration of 3dB means a 0dB pure sine wave is at 0dB in the meter
  calibration_db: 3.0
  # Scale: "log" or "linear"
  scale: log
  # Enable momentum for analog VU meter
  enable_momentum: true
  # Spring constant of needle
  spring_constant: 500.0
  # Damping ratio of needle
  damping_ratio: 10.0
  # Needle width
  needle_width: 2.0
```

### Lowpass Filter Settings

Controls the lowpass filter applied to the audio signal used for oscilloscope.

```yaml
lowpass:
  # Cutoff frequency in Hz
  cutoff: 200.0
  # Order of the lowpass filter (higher = steeper filter, more CPU usage)
  order: 4
```

### Visualizer Layout Settings

Controls the order and visibility of visualizers in window groups
Each group (like "main") creates a window with visualizers arranged left-to-right
Visualizers can be moved between windows by clicking arrow buttons
Windows can be split by right-clicking to pop out visualizers into new windows
Available visualizers: spectrum_analyzer, lissajous, oscilloscope, spectrogram, lufs, vu
Note: you cannot have the same visualizer multiple times either in the same window or in different windows.
Only one instance of a visualizer can exist at a time.

```yaml
visualizers:
  main:
    - lufs
    - vu
    - lissajous
    - oscilloscope
    - spectrum_analyzer
  additional_window:
    - spectrogram
```

### Window Settings

Controls the main application window.

```yaml
window:
  # Default window width in pixels
  default_width: 1080
  
  # Default window height in pixels
  default_height: 200
  
  # FPS limit for the main window
  # This will also throttle the audio thread to save CPU
  # Note: this does not define the exact FPS limit,
  # as pipewire/pulseaudio only accept base-2 read sizes.
  fps_limit: 240
  
  # theme file (from themes/ directory)
  theme: mocha.txt

  # Enable window decorations in your desktop environment
  decorations: true
```

## Troubleshooting

### Common Issues

**No Audio Input:**
- Check your audio device name with `pactl list sources short` (PulseAudio) or `pw-cli ls Node` (PipeWire)
- On Windows, use the Human Readable device name or a substring of it.
- Ensure the device is not muted
- Try different audio engines (`pulseaudio`, `pipewire`, `wasapi`, or `auto`)

**High CPU Usage/Poor Performance:**
- Disable phosphor effects: `phosphor.enabled: false`
- Reduce FFT size: `fft.size: 2048`
- Disable CQT: `fft.enable_cqt: false`
- Lower FPS limit: `window.fps_limit: 60`

**Visual Artifacts:**
- Adjust phosphor settings
- Check your GPU drivers and OpenGL support

### Finding Your Audio Device

**PulseAudio:**
```bash
pactl list sources short
```

**PipeWire:**
```bash
pw-cli ls Node
```

