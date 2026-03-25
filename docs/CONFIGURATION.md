# Pulse Audio Visualizer Configuration Guide

This guide provides comprehensive documentation for configuring Pulse Audio Visualizer.
The configuration file is located at `~/.config/pulse-visualizer/config.yml` for
Linux and `C:\Users\<username>\.config\pulse-visualizer\config.yml` for Windows
and uses YAML format.

## Quick Start

1. Start Pulse at least once to generate the config file
2. Edit the configuration file to match your system

Most config options update live, so you can change them without restarting Pulse,
but some may require a restart.

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

**Note:** Some boolean options that previously existed are now implied by their
respective int/float counterparts being nonzero. For example,
`phosphor.screen.curvature_enabled: true` is implied when
`phosphor.screen.curvature > FLT_EPSILON` (FLT_EPSILON is 1e-6).

## Configuration Sections

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
  device: default
  
  # Audio gain adjustment in dB (positive=louder, negative=quieter)
  # Adjust if audio is too quiet, like when Spotify or YouTube normalizes the volume
  gain_db: 0.0
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

### FFT (Fast Fourier Transform) Settings

Controls frequency analysis and spectrum visualization.

```yaml
fft:
  # FFT size (must be power of 2, higher=better frequency resolution)
  # Common values: 2048, 4096, 8192, 16384
  size: 4096
  
  # Enable frequency markers on the display
  markers: true
  
  # Slope correction for frequency response (dB per octave, visual only)
  slope: 3.0
  
  # Note key mode: "sharp" or "flat" (affects frequency labels)
  key: sharp
  
  # Stereo mode: "midside" (mid/side channels) or "leftright" (left/right channels)
  mode: midside
  
  # Beam multiplier for phosphor effect
  beam_multiplier: 1.0
  
  # Rotation of the FFT display
  # 0 = 0 degrees, 1 = 90 degrees, 2 = 180 degrees, 3 = 270 degrees
  rotation: 0
  
  # Flip the FFT display along the frequency axis
  flip_x: false
  # Enable hover cursor for FFT display
  cursor: false
  # Show readout header with peak/frequency/note info for strongest partial
  readout_header: false

  # Frequency scale for the FFT display: "log", "linear", or "mel"
  frequency_scale: mel

  # Frequency and amplitude limits
  limits:
    # dB level at the top of the display before slope correction
    max_db: 0.0

    # dB level at the bottom of the display before slope correction
    min_db: -60.0

    # Maximum frequency to display in Hz
    max_freq: 22000.0

    # Minimum frequency to display in Hz
    min_freq: 10.0

  # Smoothing settings for FFT values
  smoothing:
    # Enable velocity smoothing for FFT values
    enabled: true

    # Fall speed of FFT bars (higher=faster fall, more responsive)
    fall_speed: 50.0

    # Fall speed when window is hovered over (higher=faster fall)
    hover_fall_speed: 10.0

    # Rise speed of FFT bars (higher=faster rise, more responsive)
    rise_speed: 500.0

  # Constant-Q Transform settings (better frequency resolution in the low end)
  cqt:
    # Enable Constant-Q Transform
    enabled: true

    # Number of frequency bins per octave for CQT (higher=better resolution)
    # Significant CPU usage increase with higher values
    bins_per_octave: 60

  # 3D sphere visualization settings
  sphere:
    # Enable 3D sphere visualization
    enabled: false

    # Maximum frequency to display in sphere (lower frequencies look better)
    max_freq: 5000.0

    # Base radius of the sphere
    base_radius: 0.1
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
  # Beam multiplier for phosphor effect
  beam_multiplier: 1.0
  # Spline settings for curve interpolation
  spline:
    # Enable Catmull-Rom spline interpolation for smoother curves (tension controls strength)
    tension: 1.0
    # Number of interpolated points between each pair of samples
    segments: 10

  # Options: "normal", "circle", "pulsar", "rotate", "black_hole"
  # rotate is a 45 degree rotation of the curve
  # circle is the rotated curve stretched to a circle
  # pulsar is all of the above, makes the outside be silent
  # and the inside be loud which looks really cool
  # black_hole is a rotated circle with a black hole-like effect
  # normal is the default mode
  mode: normal
  
  # Rotation of the lissajous display
  # 0 = 0 degrees, 1 = 90 degrees, 2 = 180 degrees, 3 = 270 degrees
  rotation: 0
```

### Oscilloscope Settings

Controls the oscilloscope waveform visualization.

```yaml
oscilloscope:
  # Time window for oscilloscope in ms
  window: 50.0
  
  # Beam multiplier for phosphor effect
  beam_multiplier: 1.0
  
  # Rotation of the oscilloscope display
  # 0 = 0 degrees, 1 = 90 degrees, 2 = 180 degrees, 3 = 270 degrees
  rotation: 0
  
  # Flip the oscilloscope display along the time axis
  flip_x: false
  
  # Pitch following and alignment settings
  pitch:
    # Follow pitch for phase alignment (true=align to detected pitch)
    follow: true
    
    # Alignment type: "peak" or "zero_crossing"
    type: zero_crossing
    
    # Alignment position: "left", "center", or "right"
    alignment: center
    
    # Number of cycles to track
    cycles: 0
    
    # Minimum time window to display in oscilloscope in ms
    min_cycle_time: 16.0
  
  # Spline settings for curve interpolation
  spline:
    # Spline tension for curve smoothness (0.0-2.0)
    tension: 1.0
    # Spline density for curve detail
    segments: 10
  
  # Lowpass filter settings for oscilloscope
  lowpass:
    # Enable lowpass filter for oscilloscope
    enabled: false
    
    # Cutoff frequency in Hz
    cutoff: 200.0
    
    # Order of the lowpass filter (higher = steeper filter, more CPU usage, must be multiple of 2)
    order: 4
  
  # Bandpass filter settings for pitch detection
  bandpass:
    # Bandwidth in Hz
    bandwidth: 10.0
    
    # Sidelobe attenuation in dB
    # Higher values = more attenuation, more CPU usage
    sidelobe: 60.0
```

### Phosphor Effect Settings

Controls the CRT phosphor simulation effect (realistic glow and persistence).

#### WARNING: These settings are computationally expensive and affect performance

```yaml
phosphor:
  # Enable or disable phosphor effects globally
  enabled: true
  
  # Electron beam settings
  beam:
    # Energy of the electron beam (affects brightness of phosphor effect)
    energy: 90.0
    
    # Enable rainbow color effect for the beam
    rainbow: true
  
  # Blur effect settings
  blur:
    # Spread of the blur effect (higher=more spread, more GPU intensive)
    spread: 128
    
    # Intensity of blur for nearby pixels (0.0-1.0, higher=more blur)
    near_intensity: 0.6
    
    # Intensity of blur for distant pixels (0.0-1.0, higher=more blur)
    far_intensity: 0.8
  
  # Screen effect settings
  screen:
    # Decay parameters for phosphor persistence
    decay:
      # When the rate of decay starts slowing down
      threshold: 0.999999046
      # How quickly the tail of the decay fades
      tail: 0.099999994
      # How quickly the head of the decay fades
      depth: 0.899999976

    # Curvature intensity for curved screen effect (0.0-1.0)
    curvature: 0.1
    
    # Gap scalar for curved screen effect (0.0-1.0)
    gap: 0.03
    
    # Vignette strength (0.0-1.0)
    vignette: 0.3
    
    # Chromatic aberration strength (0.0-1.0)
    chromatic_aberration: 0.008
    
    # Grain strength (spatial noise, 0.0-1.0)
    grain: 0.1

    # AntiAliasing size (2-16)
    aa_size: 4

  # Reflections settings
  reflections:
    # Strength of reflections (0.0-1.0)
    strength: 0.5

```

### Spectrogram Settings

Controls the spectrogram visualization (time-frequency heat map).

```yaml
spectrogram:
  # Frequency scale: "log", "linear", or "mel"
  frequency_scale: mel

  # Sharpen broad spectral peaks by iteratively reassigning energy to nearby dominant bins
  iterative_reassignment: true
  
  # Time window for spectrogram in seconds
  window: 4.0
  
  # Frequency and amplitude limits
  limits:
    # Maximum dB level
    max_db: -10.0
    
    # Minimum dB level
    min_db: -60.0

    # Minimum frequency
    min_freq: 10

    # Maximum frequency
    max_freq: 4000

```

### LUFS Settings

Controls the LUFS (Loudness Units) visualization.

```yaml
lufs:
  # Mode: "shortterm", "momentary", "integrated"
  # momentary is over a 400ms window
  # shortterm is over a 3s window
  # integrated is over the entire recording
  mode: momentary

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
  window: 100.0
  
  # Style: "analog" or "digital"
  style: digital
  
  # Calibration dB level
  # a calibration of 3dB means a 0dB pure sine wave is at 0dB in the meter
  calibration_db: 3.0
  
  # Scale: "log" or "linear"
  scale: log
  
  # Needle width for analog VU meter
  needle_width: 2.0
  
  # Momentum settings for analog VU meter
  momentum:
    # Enable momentum for analog VU meter
    enabled: true
    
    # Spring constant of needle
    spring_constant: 500.0
    
    # Damping ratio of needle
    damping_ratio: 10.0
```

### Visualizer Layout Settings

Controls arrangement of visualizers using a nested hierarchy tree.

The layout is a YAML tree where each node is either:

- a split branch with `type: hsplit` or `type: vsplit`, a `ratio` (0.0–1.0), and `children` (sequence).
  Split branch nodes must have exactly two children (primary and secondary).
- a leaf with `id: <visualizer_id>`, referencing a single visualizer

This enables flexible nested splits and resizing. Rearrangement is swaywm-like: drag a visualizer and
drop it to replace another (center) or create a split (top/bottom/left/right); the tree will be
adjusted accordingly. Only one instance of each visualizer ID may exist.

Example (tree form):

```yaml
visualizers:
  main:
    type: vsplit
    ratio: 0.5
    children:
      - type: hsplit
        ratio: 0.333
        children:
          - type: hsplit
            ratio: 0.445682466
            children:
              - type: hsplit
                ratio: 0.375
                children:
                  - id: vu
                  - id: lufs
              - id: lissajous
          - type: hsplit
            ratio: 0.5
            children:
              - id: oscilloscope
              - id: spectrum_analyzer
      - type: hsplit
        ratio: 0.5
        children:
          - id: waveform
          - id: spectrogram
```

Available visualizers: spectrum_analyzer, lissajous, oscilloscope, spectrogram, waveform, lufs, vu

### Waveform Settings

Scrolling time-domain envelope display. The renderer groups samples by pixel-width buckets and draws a min→max line
for each emitted history column.

```yaml
waveform:
  # History length in seconds across the full waveform width
  window: 2.0

  # Frequency split between low and mid bands for color analysis
  low_mid_split_hz: 250.0

  # Frequency split between mid and high bands for color analysis
  mid_high_split_hz: 4000.0

  # Display-only tilt for color analysis (dB/oct)
  # Positive values emphasize highs, negative values emphasize lows
  slope: 3.0

  # Channel mode: "mono" or "stereo"
  # In stereo mode waveform follows fft.mode:
  # - midside => Mid/Side
  # - leftright => Left/Right
  mode: stereo
  # Show a center midline when the signal is effectively silent.
  # true = draw a single center pixel when min and max are nearly equal.
  # false = never draw the silent midpoint.
  midline: true
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
  
  # Keep the window always on top of other windows
  always_on_top: false

  # Try to use Wayland instead of X11
  wayland: false
```

### Plugin Settings

Plugin-managed options are stored under the `plugins` top-level key.

```yaml
plugins:
  my_plugin:
    optionA: 5
    subcategory:
      optionB: selectableA
```

- `my_plugin` is defined by the plugin via `pvPluginGetInfo`.
- Nested keys are created from plugin-registered option paths.
- Options are read during config load/reload and written by normal config saves.

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
- Disable CQT: `fft.cqt.enabled: false`
- Lower FPS limit: `window.fps_limit: 60`

**Visual Artifacts:**

- Adjust phosphor settings
- Check your GPU drivers and OpenGL support
