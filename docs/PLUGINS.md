# Pulse Visualizer Plugin Guide

This guide explains how to write and use plugins for Pulse Visualizer.  
Plugins are shared libraries that can draw into Pulse’s window, react to SDL events, and read configuration and theme data.

Current limitations:

- Plugins cannot create their own visualizers yet  
- Plugins do not have access to the full internal rendering pipeline  
- No Windows support yet  

For the full API surface and data structures, see [`plugin_api.hpp`](../src/include/plugin_api.hpp) in `src/include`.

## Plugin Lifecycle

Each plugin is a shared library that must export a fixed set of C‑linkage entry points:

```cpp
extern "C" int pvPluginInit(const PvAPI* api);
extern "C" void pvPluginStart();
extern "C" void pvPluginStop();
extern "C" void draw();
extern "C" void handleEvent(SDL_Event& event);
```

Pulse Visualizer will:

- Load `.so` files from the plugin directory  
- Call `pvPluginInit` once after loading  
- Call `pvPluginStart` when the plugin is activated  
- Call `draw` once per frame  
- Forward SDL events into `handleEvent`  
- Call `pvPluginStop` before unloading or on shutdown  

If `pvPluginInit` returns a non‑zero value, the plugin is treated as failed and skipped during startup.

## Required Entry Points

A typical plugin looks like this:

```cpp
#include <pulse-visualizer/plugin_api.hpp>  // adjust include path as needed

static const PvAPI* api = nullptr;

extern "C" int pvPluginInit(const PvAPI* a) {
  if (!a || a->apiVersion < 1)
    return -1;

  // pre-startup initialization

  api = a;
  return 0;
}

extern "C" void pvPluginStart() {
  if (!api)
    return;

  // one-time startup code
}

extern "C" void pvPluginStop() {
  if (!api)
    return;

  // cleanup code
}

extern "C" void draw() {
  if (!api)
    return;

  // per-frame drawing using api helpers
}

extern "C" void handleEvent(SDL_Event& event) {
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

## What Plugins Can Access

The `PvAPI` struct exposes the plugin ABI and is the only supported way to talk to Pulse from a plugin.

You get:

- Window helpers (creation/deletion, size, DPI, basic window info)  
- Immediate‑style drawing helpers (lines, rectangles, arcs, text)  
- Text layout helpers (wrapping, truncation, fitting into a box)  
- Color helpers (RGBA/HSVA conversion, mixing, applying alpha)  
- Pointers to the current config and theme  
- A debug flag for optional stdout/stderr logging  

Important notes:

- Treat the drawing functions as fire‑and‑forget helpers; you do not manage OpenGL state.  
- Do not assume anything about the internal renderer beyond what `plugin_api.hpp` documents.  
- Config and theme pointers are owned by Pulse Visualizer; never free them, and avoid caching deep copies that might drift out of sync.  
- Only use fields and functions declared in `plugin_api.hpp`; internal headers and structs are not stable and may change between releases.

## Building a Plugin

Plugins are regular shared libraries built against the public headers.

Minimal manual build on Linux/BSD:

```bash
clang++ -std=c++23 -fPIC -shared \
  -I../include \
  -o helloworld.so \
  helloworld.cpp
```

- `-std=c++23` matches the main project  
- `-fPIC` is required for shared libraries on most ELF systems  
- `-shared` produces a `.so` instead of an executable  
- `-I../include` must point to the directory containing `plugin_api.hpp`  

Example CMake setup:

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyPulsePlugin)

set(CMAKE_CXX_STANDARD 23)

add_library(myplugin SHARED myplugin.cpp)

target_include_directories(myplugin PRIVATE
  ../include  # adjust to your layout
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

Basic workflow:

- Build `myplugin.so`  
- Copy it into `~/.config/pulse-visualizer/plugins/`  
- Start Pulse Visualizer and check its logs for plugin load status  

If `pvPluginInit` returns non‑zero, the plugin is logged as failed and ignored for the session.

## Minimal “Hello World” Plugin

This is a very small plugin that just prints to stdout on start/stop:

```cpp
#include <pulse-visualizer/plugin_api.hpp>
#include <iostream>

static const PvAPI* api = nullptr;

extern "C" int pvPluginInit(const PvAPI* a) {
  if (!a || a->apiVersion < 1)
    return -1;

  api = a;
  return 0;
}

extern "C" void pvPluginStart() {
  if (!api)
    return;

  std::cout << "hello world from plugin\n";
}

extern "C" void pvPluginStop() {
  if (!api)
    return;

  std::cout << "bye from plugin\n";
}

extern "C" void draw() {
  if (!api)
    return;

  // nothing to draw yet
}

extern "C" void handleEvent(SDL_Event& event) {
  if (!api)
    return;

  (void)event;
}
```

Build it:

```bash
clang++ -std=c++23 -fPIC -shared \
  -I../include \
  -o helloworld.so \
  helloworld.cpp
```

Copy `helloworld.so` into the plugin directory and start Pulse Visualizer to see the messages in your terminal.

## Stability and Compatibility

- The plugin ABI is versioned via `PvAPI::apiVersion`  
- The API may evolve over time; always check `apiVersion` in `pvPluginInit` and abort if required features are missing  
- Only rely on what `plugin_api.hpp` explicitly exposes; internal structures, headers, or symbols are not guaranteed to be stable between releases
