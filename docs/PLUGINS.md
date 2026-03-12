# Pulse Visualizer Plugin Guide

This guide explains how to write and use plugins for Pulse Visualizer.  
Plugins are shared libraries that can render into Pulse’s window, react to SDL events, and read configuration and theme data.

Current limitations:

- Plugins do not have access to the full internal rendering pipeline  

For the full API surface and data structures, see [`plugin_api.hpp`](../src/include/plugin_api.hpp) in `src/include`.

## Plugin Lifecycle

Each plugin is a shared library that must export a fixed set of C‑linkage entry points:

```cpp
PV_API int pvPluginInit(const PvAPI* api);
PV_API void pvPluginGetInfo(const char** key, const char** displayName);
PV_API void pvPluginOnConfigReload();
PV_API void pvPluginStart();
PV_API void pvPluginStop();
PV_API void draw();
PV_API void handleEvent(SDL_Event& event);
```

Pulse Visualizer will:

- Load `.so`/`.dll` files from the plugin directory  
- Call `pvPluginGetInfo` to retrieve plugin key/display metadata  
- Call `pvPluginInit` once after loading  
- Call `pvPluginOnConfigReload` after host config reloads  
- Call `pvPluginStart` when the plugin is activated  
- Call `draw` once per frame  
- Forward SDL events into `handleEvent`  
- Call `pvPluginStop` before unloading or on shutdown  

If any required symbol is missing, or if `pvPluginInit` returns a non‑zero value, the plugin is treated as failed and skipped during startup.

## Required Entry Points

A typical plugin looks like this:

```cpp
#include <pulse-visualizer/plugin_api.hpp>  // adjust include path as needed

static const PvAPI* api = nullptr;

PV_API void pvPluginGetInfo(const char** key, const char** displayName) {
  if (key)
    *key = "my_plugin";
  if (displayName)
    *displayName = "My Plugin";
}

PV_API int pvPluginInit(const PvAPI* a) {
  if (!a || a->apiVersion < PLUGIN_API_VERSION)
    return -1;

  // pre-startup initialization

  api = a;
  return 0;
}

PV_API void pvPluginStart() {
  if (!api)
    return;

  // one-time startup code
}

PV_API void pvPluginStop() {
  if (!api)
    return;

  // cleanup code
}

PV_API void pvPluginOnConfigReload() {
  if (!api)
    return;

  // refresh cached config/state
}

PV_API void draw() {
  if (!api)
    return;

  // per-frame drawing using api helpers
}

PV_API void handleEvent(SDL_Event& event) {
  if (!api)
    return;

  // optional: react to events
  (void)event;
}
```

Always:

- Check `api` for null in every entry point  
- Check `api->apiVersion` in `pvPluginInit`  
- Return non‑zero from `pvPluginInit` if the current API version is unsupported  
- Return a stable plugin key from `pvPluginGetInfo` (used as `plugins.<key>` in YAML)
- Export all required entry points

## Plugin Config Passthrough API

Plugins can register scalar config options under their own YAML namespace:

```yaml
plugins:
  my_plugin:
    optionA: 5
    subcategory:
      optionB: selectableA
```

Use the typed helper methods on `PvAPI`:

```cpp
struct CachedConfig {
  int optionA = 5;
  std::string optionB = "selectableA";
};

static CachedConfig cachedConfig;

static void refreshCachedConfig() {
  if (!api)
    return;

  if (auto value = api->getConfigOption<int>("optionA"); value.has_value())
    cachedConfig.optionA = *value;

  if (auto value = api->getConfigOption<std::string>("subcategory.optionB"); value.has_value())
    cachedConfig.optionB = *value;
}

PV_API int pvPluginInit(const PvAPI* a) {
  if (!a || a->apiVersion < PLUGIN_API_VERSION)
    return -1;

  api = a;

  Config::PluginConfigSpec gainDesc;
  gainDesc.label = "Gain";
  gainDesc.min = 0;
  gainDesc.max = 10;
  api->registerConfigOption<int>("optionA", 5, gainDesc);

  Config::PluginConfigSpec modeDesc;
  modeDesc.label = "Mode";
  modeDesc.useTick = false;
  modeDesc.choices = {"selectableA", "selectableB"};
  api->registerConfigOption<std::string>("subcategory.optionB", "selectableA", modeDesc);

  // Prime local cache once during startup.
  refreshCachedConfig();

  return 0;
}

PV_API void pvPluginOnConfigReload() {
  // Refresh cached values only when the host reports config changes.
  refreshCachedConfig();
}

PV_API void draw() {
  if (!api)
    return;

  // Use cached values in the hot path.
  if (cachedConfig.optionB == "selectableA")
    api->setConfigOption<int>("optionA", cachedConfig.optionA);
}
```

### Performance Recommendation

Cache plugin options in local variables/structs and refresh them in `pvPluginOnConfigReload`.

Why this matters:

- Per-frame option reads will perform repeated `std::unordered_map` lookups.
- Doing that work in `draw()` can become unnecessarily expensive.
- Refreshing cached values only on `pvPluginOnConfigReload` keeps the render path fast and predictable.

Behavior guarantees:

- Duplicate register for the same plugin/path fails and logs an error.
- `getConfigOption<T>` returns `std::nullopt` for unknown or mismatched type.
- `setConfigOption<T>` returns `false` when not registered or invalid.
- Registered plugin options are shown in the Config window `Plugins` page.

Callback when host config reloads:

```cpp
PV_API void pvPluginOnConfigReload() {
  // strongly recommended: refresh your cached config here
}
```

## Registering a Visualizer Window

Plugins can register their own visualizer by deriving from `WindowManager::VisualizerWindow`
and passing an instance to `api->registerVisualizer(...)`.

```cpp
static const PvAPI* api = nullptr;

class HelloVisualizer : public WindowManager::VisualizerWindow {
public:
  HelloVisualizer() { id = "hello"; }

  void render() override {
    if (!api || !api->drawText)
      return;

    api->drawText("hello world", 10.0f, 10.0f, 16.0f, api->theme->text);
  }
};

PV_API int pvPluginInit(const PvAPI* a) {
  if (!a || a->apiVersion < PLUGIN_API_VERSION)
    return -1;

  api = a;

  // ...

  api->registerVisualizer(std::make_shared<HelloVisualizer>());
}
```

In `render`, the SDL window state is no longer passed directly to visualizers.  
The renderer now calls `api->setViewport()` automatically for each visualizer, so all coordinates passed to the API are relative to that visualizer’s local origin.  

To access a visualizer’s absolute position and size within the main SDL window, use `this->bounds`:  
`this->bounds.x` and `this->bounds.y` give the bottom-left coordinates, and  
`this->bounds.w` and `this->bounds.h` give the width and height.  

## What Plugins Can Access

The `PvAPI` struct exposes the plugin ABI and is the only supported way to talk to Pulse from a plugin.

You get:

- Plugin metadata/context (`apiVersion`, `pluginKey`, `pluginDisplayName`, `pluginContext`)  
- Host helpers (`saveConfig`, `expandUserPath`)  
- Window helpers (creation/deletion, selection, size, cursor, viewport)  
- Immediate‑style drawing helpers (lines, rectangles, arcs, text)  
- Text layout helpers (wrapping, truncation, fitting into a box)  
- Color helpers (RGBA/HSVA conversion, mixing, applying alpha)  
- Plugin config passthrough (`registerConfigOptionValue`, `getConfigOptionValue`, `setConfigOptionValue`) and typed wrappers (`registerConfigOption<T>`, `getConfigOption<T>`, `setConfigOption<T>`)  
- Pointers to the current config and theme  
- Pointer to current SDL window states (`api->states`)  
- A debug flag for optional stdout/stderr logging  
- Visualizer registration via `api->registerVisualizer(...)`  

### Reading DSP Buffers (Read-Only)

`PvAPI` exposes read-only pointers to the live DSP buffers and the current write position:

- `api->bufferMid`, `api->bufferSide`
- `api->fftMidRaw`, `api->fftMid`, `api->fftMidPhase`, `api->fftSideRaw`, `api->fftSide`, `api->fftSidePhase`
- `api->writePos`

Use them as read-only views. Do not mutate these vectors or any pointed data.

```cpp
PV_API void draw() {
  if (!api || !api->bufferMid || !api->writePos)
    return;

  const auto& mid = *api->bufferMid;
  if (mid.empty())
    return;

  // Snapshot write position first, then clamp to current vector size.
  const size_t wp = *api->writePos;
  const size_t idx = wp % mid.size();

  // Read latest sample from circular buffer.
  const float latest = mid[idx];
  (void)latest;

  // FFT buffers are plain vectors too:
  if (api->fftMid && !api->fftMid->empty()) {
    const float firstBin = (*api->fftMid)[0];
    (void)firstBin;
  }
}
```

For circular-buffer access, always mod by the current vector size (`index % buffer.size()`).

Important notes:

- Treat the drawing functions as fire‑and‑forget helpers; you do not manage OpenGL state.  
- Do not assume anything about the internal renderer beyond what `plugin_api.hpp` documents.  
- Config and theme pointers are owned by Pulse Visualizer; never free them, and avoid caching deep copies that might drift out of sync.  
- Only use fields and functions declared in `plugin_api.hpp`; internal headers and structs are not stable and may change between releases.

## Building a Plugin

Plugins are regular shared libraries built against the public headers.

Minimal manual build on Linux/BSD:

```bash
clang++ -std=c++20 -lSDL3 -fPIC -shared \
  ../external/glad/src/gl.c \
  -I../include \
  -I../external/glad/include \
  -o helloworld.so \
  helloworld.cpp
```

- `-std=c++20` matches the main project  
- `-lSDL3` is required for SDL3
- `../external/glad/src/gl.c` and `-I../external/glad/include` provide the GLAD loader
- `-fPIC` is required for shared libraries on most ELF systems  
- `-shared` produces a `.so` instead of an executable  
- `-I../include` must point to the directory containing `plugin_api.hpp`  

Example CMake setup:

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyPulsePlugin)

set(CMAKE_CXX_STANDARD 20)

add_library(myplugin SHARED myplugin.cpp)

add_library(glad OBJECT
  ../external/glad/src/gl.c
)

target_include_directories(glad PUBLIC
  ../external/glad/include
)

target_include_directories(myplugin PRIVATE
  ../include
  ../external/glad/include
)

find_package(SDL3 REQUIRED)

target_link_libraries(myplugin PRIVATE
  SDL3::SDL3
  glad
)

set_target_properties(myplugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "myplugin"
)
```

Then build:

```bash
cmake -S . -B build
cmake --build build
```

Use the same compiler and general build settings as Pulse itself for best ABI compatibility.

## Installing and Loading Plugins

Pulse Visualizer scans for plugins at startup in:

- `~/.config/pulse-visualizer/plugins/` on Linux/BSD (and other Unix‑like systems)  
- `C:\Users\<username>\.config\pulse-visualizer\plugins` on Windows

Basic workflow:

- Build `myplugin.so`  
- Copy it into the plugins folder
- Start Pulse Visualizer and check its logs for plugin load status  

If a required symbol is missing, or if `pvPluginInit` returns non‑zero, the plugin is logged as failed and ignored for the session.

## Minimal “Hello World” Plugin

This is a very small plugin that just prints to stdout on start/stop:

```cpp
#include <pulse-visualizer/plugin_api.hpp>
#include <iostream>

static const PvAPI* api = nullptr;

PV_API void pvPluginGetInfo(const char** key, const char** displayName) {
  if (key)
    *key = "helloworld";
  if (displayName)
    *displayName = "Hello World";
}

PV_API int pvPluginInit(const PvAPI* a) {
  if (!a || a->apiVersion < PLUGIN_API_VERSION)
    return -1;

  api = a;
  return 0;
}

PV_API void pvPluginStart() {
  if (!api)
    return;

  std::cout << "hello world from plugin\n";
}

PV_API void pvPluginStop() {
  if (!api)
    return;

  std::cout << "bye from plugin\n";
}

PV_API void pvPluginOnConfigReload() {
  if (!api)
    return;
}

PV_API void draw() {
  if (!api)
    return;

  // nothing to draw yet
}

PV_API void handleEvent(SDL_Event& event) {
  if (!api)
    return;

  (void)event;
}
```

Build it:

```bash
clang++ -std=c++20 -lSDL3 -fPIC -shared \
  ../external/glad/src/gl.c \
  -I../include \
  -I../external/glad/include \
  -o helloworld.so \
  helloworld.cpp
```

Copy `helloworld.so` into the plugin directory and start Pulse Visualizer to see the messages in your terminal.

## Stability and Compatibility

- The plugin ABI is versioned via `PvAPI::apiVersion`  
- The API may evolve over time; always check `apiVersion` in `pvPluginInit` and abort if required features are missing  
- Only rely on what `plugin_api.hpp` explicitly exposes; internal structures, headers, or symbols are not guaranteed to be stable between releases
