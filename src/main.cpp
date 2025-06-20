#include "audio_data.hpp"
#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "splitter.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

// Structure to hold the current visualizer configuration
struct VisualizerConfig {
  int spectrogramOrder = -1; // -1 = disabled, >= 0 = position order
  int lissajousOrder = -1;
  int oscilloscopeOrder = -1;
  int fftOrder = -1;

  bool operator!=(const VisualizerConfig& other) const {
    return spectrogramOrder != other.spectrogramOrder || lissajousOrder != other.lissajousOrder ||
           oscilloscopeOrder != other.oscilloscopeOrder || fftOrder != other.fftOrder;
  }
};

// Function to read current visualizer config
VisualizerConfig readVisualizerConfig() {
  VisualizerConfig config;
  config.spectrogramOrder = Config::getInt("visualizers.spectrogram_order");
  config.lissajousOrder = Config::getInt("visualizers.lissajous_order");
  config.oscilloscopeOrder = Config::getInt("visualizers.oscilloscope_order");
  config.fftOrder = Config::getInt("visualizers.fft_order");
  return config;
}

// Function to rebuild visualizers and splitters
void rebuildVisualizerLayout(const VisualizerConfig& config, AudioData& audioData,
                             std::vector<std::unique_ptr<VisualizerBase>>& visualizers,
                             std::vector<VisualizerBase*>& visualizerPtrs,
                             std::vector<std::unique_ptr<Splitter>>& splitters, Splitter*& rootSplitter,
                             SpectrogramVisualizer*& spectrogramVis, LissajousVisualizer*& lissajousVis,
                             OscilloscopeVisualizer*& oscilloscopeVis, FFTVisualizer*& fftVis) {

  // Clear existing objects
  visualizers.clear();
  visualizerPtrs.clear();
  splitters.clear();
  rootSplitter = nullptr;
  spectrogramVis = nullptr;
  lissajousVis = nullptr;
  oscilloscopeVis = nullptr;
  fftVis = nullptr;

  // Create a vector of enabled visualizers with their orders
  struct VisualizerOrderPair {
    int order;
    std::unique_ptr<VisualizerBase> visualizer;
    VisualizerBase** rawPtr;
  };

  std::vector<VisualizerOrderPair> orderedVis;

  // Add enabled visualizers to the ordering vector
  if (config.spectrogramOrder >= 0) {
    auto vis = std::make_unique<SpectrogramVisualizer>();
    spectrogramVis = vis.get();
    orderedVis.push_back(
        {config.spectrogramOrder, std::move(vis), reinterpret_cast<VisualizerBase**>(&spectrogramVis)});
  }

  if (config.lissajousOrder >= 0) {
    auto vis = std::make_unique<LissajousVisualizer>();
    lissajousVis = vis.get();
    lissajousVis->setAspectRatioConstraint(1.0f, true);
    orderedVis.push_back({config.lissajousOrder, std::move(vis), reinterpret_cast<VisualizerBase**>(&lissajousVis)});
  }

  if (config.oscilloscopeOrder >= 0) {
    auto vis = std::make_unique<OscilloscopeVisualizer>();
    oscilloscopeVis = vis.get();
    orderedVis.push_back(
        {config.oscilloscopeOrder, std::move(vis), reinterpret_cast<VisualizerBase**>(&oscilloscopeVis)});
  }

  if (config.fftOrder >= 0) {
    auto vis = std::make_unique<FFTVisualizer>();
    fftVis = vis.get();
    orderedVis.push_back({config.fftOrder, std::move(vis), reinterpret_cast<VisualizerBase**>(&fftVis)});
  }

  // Sort visualizers by their order
  std::sort(orderedVis.begin(), orderedVis.end(),
            [](const VisualizerOrderPair& a, const VisualizerOrderPair& b) { return a.order < b.order; });

  // Move sorted visualizers to the main containers
  for (auto& pair : orderedVis) {
    visualizerPtrs.push_back(pair.visualizer.get());
    visualizers.push_back(std::move(pair.visualizer));
  }

  // Create splitters dynamically based on ordered visualizers
  if (visualizerPtrs.size() >= 2) {
    // Create splitters from right to left
    for (size_t i = visualizerPtrs.size() - 1; i > 0; --i) {
      VisualizerBase* leftVis = visualizerPtrs[i - 1];
      VisualizerBase* rightVis = visualizerPtrs[i];

      // Determine if this splitter should be draggable
      // The lissajous visualizer should have a non-draggable splitter to its right
      bool isDraggable = true;
      if (leftVis == lissajousVis) {
        isDraggable = false; // Make splitter to the right of lissajous non-draggable
      }

      Splitter* nextSplitter = (i == visualizerPtrs.size() - 1) ? nullptr : splitters.back().get();
      auto splitter = std::make_unique<Splitter>(leftVis, rightVis, nextSplitter, isDraggable);

      if (i == 1) {
        rootSplitter = splitter.get();
      }

      splitters.push_back(std::move(splitter));
    }

    // Reverse splitters vector since we created them backwards
    std::reverse(splitters.begin(), splitters.end());
    if (!splitters.empty()) {
      rootSplitter = splitters[0].get();
    }
  }

  // Set initial splitter positions
  constexpr int MIN_VIS_WIDTH = 80;
  int availableWidth = audioData.windowWidth;

  if (rootSplitter && !visualizerPtrs.empty()) {
    // Calculate proportional widths
    int widthPerVisualizer = std::max(MIN_VIS_WIDTH, availableWidth / static_cast<int>(visualizerPtrs.size()));

    // Set splitter positions
    for (size_t i = 0; i < splitters.size(); ++i) {
      int position = (i + 1) * widthPerVisualizer;

      // Special handling for lissajous aspect ratio
      if (i < visualizerPtrs.size() - 1 && visualizerPtrs[i] == lissajousVis) {
        int requiredWidth = lissajousVis->getRequiredWidth(audioData.windowHeight);
        if (requiredWidth > 0) {
          int lissajousStart = (i == 0) ? 0 : splitters[i - 1]->getPosition();
          position = lissajousStart + requiredWidth;
        }
      }

      // Special handling when lissajous is the first (leftmost) visualizer
      if (i == 0 && visualizerPtrs[0] == lissajousVis) {
        int requiredWidth = lissajousVis->getRequiredWidth(audioData.windowHeight);
        if (requiredWidth > 0) {
          int maxWidth = audioData.windowWidth - (visualizerPtrs.size() - 1) * MIN_VIS_WIDTH;
          position = std::min(requiredWidth, maxWidth);
        }
      }

      // Ensure position doesn't exceed window bounds
      int maxPos = audioData.windowWidth - (splitters.size() - i) * MIN_VIS_WIDTH;
      position = std::min(position, maxPos);

      splitters[i]->setPosition(position);
    }

    // Initial layout update
    rootSplitter->update(audioData);
  }
}

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return 1;
  }

  // Load config
  Config::load();

  bool drawFPS = Config::getBool("debug.log_fps");

  // Initialize the theme system
  Theme::ThemeManager::initialize();

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window* win = SDL_CreateWindow("Audio Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     Config::getInt("window.default_width"), Config::getInt("window.default_height"),
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!win) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
    std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  Graphics::initialize();

  AudioData audioData;

  // Pre-allocate all buffers
  audioData.bufferMid.resize(audioData.bufferSize, 0.0f);
  audioData.bufferSide.resize(audioData.bufferSize, 0.0f);
  audioData.bandpassedMid.resize(audioData.bufferSize, 0.0f);

  // Pre-allocate all FFT-related vectors
  const int FFT_SIZE = 4096;
  audioData.fftMagnitudesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.prevFftMagnitudesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.smoothedMagnitudesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.interpolatedValuesMid.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.fftMagnitudesSide.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.prevFftMagnitudesSide.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.smoothedMagnitudesSide.resize(FFT_SIZE / 2 + 1, 0.0f);
  audioData.interpolatedValuesSide.resize(FFT_SIZE / 2 + 1, 0.0f);

  // Initialize visualizer configuration and layout
  VisualizerConfig currentConfig = readVisualizerConfig();

  // Create containers for visualizers and splitters
  std::vector<std::unique_ptr<VisualizerBase>> visualizers;
  std::vector<VisualizerBase*> visualizerPtrs;
  std::vector<std::unique_ptr<Splitter>> splitters;
  Splitter* rootSplitter = nullptr;

  // Keep raw pointers for specific visualizer access
  SpectrogramVisualizer* spectrogramVis = nullptr;
  LissajousVisualizer* lissajousVis = nullptr;
  OscilloscopeVisualizer* oscilloscopeVis = nullptr;
  FFTVisualizer* fftVis = nullptr;

  // Build initial layout
  rebuildVisualizerLayout(currentConfig, audioData, visualizers, visualizerPtrs, splitters, rootSplitter,
                          spectrogramVis, lissajousVis, oscilloscopeVis, fftVis);

  std::thread audioThreadHandle(AudioProcessing::audioThread, &audioData);

  bool running = true;
  bool windowHasFocus = true;

  Uint32 frameCount = 0;
  Uint32 lastFpsUpdate = SDL_GetTicks();
  Uint32 lastThemeCheck = SDL_GetTicks();

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      // Handle splitter events
      for (auto& splitter : splitters) {
        splitter->handleEvent(event, audioData);
      }

      switch (event.type) {
      case SDL_QUIT:
        running = false;
        break;

      case SDL_MOUSEMOTION: {
        // Update FFT visualizer mouse position if enabled
        if (fftVis) {
          int mouseX = event.motion.x;
          int mouseY = event.motion.y;

          // Check if mouse is over FFT visualizer area
          int fftPosition = fftVis->getPosition();
          int fftWidth = fftVis->getWidth();
          bool isOverFFT = (mouseX >= fftPosition && mouseX < fftPosition + fftWidth && mouseY >= 0 &&
                            mouseY < audioData.windowHeight);

          if (isOverFFT) {
            // Convert to FFT visualizer local coordinates
            float localX = static_cast<float>(mouseX - fftPosition);
            float localY = static_cast<float>(audioData.windowHeight - mouseY); // Flip Y coordinate
            fftVis->updateMousePosition(localX, localY);
            fftVis->setHovering(true);
          } else {
            fftVis->setHovering(false);
          }
        }
      } break;

      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          audioData.windowWidth = event.window.data1;
          audioData.windowHeight = event.window.data2;
          glViewport(0, 0, audioData.windowWidth, audioData.windowHeight);

          // Handle aspect ratio for leftmost lissajous visualizer
          if (lissajousVis && !visualizerPtrs.empty() && visualizerPtrs[0] == lissajousVis) {
            int requiredWidth = lissajousVis->getRequiredWidth(audioData.windowHeight);
            if (requiredWidth > 0 && !splitters.empty()) {
              // Adjust the first splitter position to maintain lissajous aspect ratio
              constexpr int MIN_VIS_WIDTH = 80;
              int maxWidth = audioData.windowWidth - (visualizerPtrs.size() - 1) * MIN_VIS_WIDTH;
              int newPosition = std::min(requiredWidth, maxWidth);
              splitters[0]->setPosition(newPosition);
            }
          }

          // Update layout
          if (rootSplitter) {
            rootSplitter->update(audioData);
          }
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          windowHasFocus = false;
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
          windowHasFocus = true;
        }
        break;
      }
    }

    // Reset hover states when window loses focus
    if (!windowHasFocus) {
      if (fftVis) {
        fftVis->setHovering(false);
      }
      for (auto& splitter : splitters) {
        splitter->setHovering(false);
      }
    }

    // Update audio data hover state
    audioData.fftHovering = fftVis ? fftVis->isHovering() : false;

    // Check for theme and config changes every second
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastThemeCheck >= 1000) {
      // Check for config changes first
      bool configChanged = Config::reloadIfChanged();

      // Check if visualizer configuration has changed
      if (configChanged) {
        VisualizerConfig newConfig = readVisualizerConfig();
        if (newConfig != currentConfig) {
          std::cerr << "Visualizer configuration changed, rebuilding layout..." << std::endl;
          currentConfig = newConfig;
          rebuildVisualizerLayout(currentConfig, audioData, visualizers, visualizerPtrs, splitters, rootSplitter,
                                  spectrogramVis, lissajousVis, oscilloscopeVis, fftVis);
        }
      }

      // Then check for direct theme file changes
      Theme::ThemeManager::reloadIfChanged();
      lastThemeCheck = currentTime;
    }

    // Always update layout before drawing
    if (rootSplitter) {
      rootSplitter->update(audioData);
    }

    const auto& background = Theme::ThemeManager::getBackground();
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT);

    {
      std::lock_guard<std::mutex> lock(audioData.mutex);

      // Draw enabled visualizers
      if (spectrogramVis) {
        spectrogramVis->draw(audioData, 0);
      }
      if (lissajousVis) {
        lissajousVis->draw(audioData, 0);
      }
      if (oscilloscopeVis && oscilloscopeVis->getWidth() > 0) {
        oscilloscopeVis->draw(audioData, 0);
      }
      if (fftVis && fftVis->getWidth() > 0) {
        fftVis->draw(audioData, 0);
      }
    }

    // Draw splitters
    for (auto& splitter : splitters) {
      splitter->draw(audioData);
    }

    SDL_GL_SwapWindow(win);

    frameCount++;
    currentTime = SDL_GetTicks();
    if (currentTime - lastFpsUpdate >= 1000) {
      if (drawFPS) {
        std::cerr << "FPS: " << frameCount << std::endl;
      }
      frameCount = 0;
      lastFpsUpdate = currentTime;
    }
  }

  audioData.running = false;
  audioThreadHandle.join();

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();

  return 0;
}