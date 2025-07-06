#include "audio_data.hpp"
#include "audio_processing.hpp"
#include "config.hpp"
#include "graphics.hpp"
#include "splitter.hpp"
#include "theme.hpp"
#include "visualizers.hpp"

#include <GL/glew.h>
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
  // Use centralized config access
  return {.spectrogramOrder = Config::values().visualizers.spectrogram_order,
          .lissajousOrder = Config::values().visualizers.lissajous_order,
          .oscilloscopeOrder = Config::values().visualizers.oscilloscope_order,
          .fftOrder = Config::values().visualizers.fft_order};
}

// Function to enforce window aspect ratio based on visualizer configuration
void enforceWindowAspectRatio(SDL_Window* window, const std::vector<VisualizerBase*>& visualizerPtrs,
                              bool& enforcingAspectRatio, AudioData& audioData,
                              std::vector<std::unique_ptr<Splitter>>& splitters, Splitter* rootSplitter) {
  // Only enforce aspect ratio for single visualizers with aspect ratio constraints
  if (visualizerPtrs.size() == 1) {
    VisualizerBase* singleVis = visualizerPtrs[0];
    if (singleVis && singleVis->shouldEnforceAspectRatio()) {
      int currentWidth, currentHeight;
      SDL_GetWindowSize(window, &currentWidth, &currentHeight);

      int requiredWidth = singleVis->getRequiredWidth(currentHeight);
      if (requiredWidth > 0 && requiredWidth != currentWidth) {
        if (Config::values().debug.log_fps) {
          std::cerr << "Enforcing aspect ratio: " << currentWidth << "x" << currentHeight << " -> " << requiredWidth
                    << "x" << currentHeight << std::endl;
        }
        // Set flag to prevent infinite loop
        enforcingAspectRatio = true;
        // Resize window to maintain aspect ratio
        SDL_SetWindowSize(window, requiredWidth, currentHeight);

        // Update OpenGL state and visualizer dimensions immediately
        audioData.windowWidth = requiredWidth;
        audioData.windowHeight = currentHeight;
        glViewport(0, 0, requiredWidth, currentHeight);

        // Update single visualizer dimensions
        singleVis->setPosition(0);
        singleVis->setWidth(requiredWidth);

        // Force phosphor contexts to update their textures on next draw
        // by temporarily setting their dimensions to 0
        if (auto* lissajousVis = dynamic_cast<LissajousVisualizer*>(singleVis)) {
          lissajousVis->invalidatePhosphorContext();
        } else if (auto* oscilloscopeVis = dynamic_cast<OscilloscopeVisualizer*>(singleVis)) {
          oscilloscopeVis->invalidatePhosphorContext();
        } else if (auto* fftVis = dynamic_cast<FFTVisualizer*>(singleVis)) {
          fftVis->invalidatePhosphorContext();
        }

        // Reset flag immediately since we've handled the resize manually
        enforcingAspectRatio = false;
      }
    }
  } else if (visualizerPtrs.size() > 1 && rootSplitter) {
    // For multiple visualizers, update splitter layout if needed
    int currentWidth, currentHeight;
    SDL_GetWindowSize(window, &currentWidth, &currentHeight);

    if (currentWidth != audioData.windowWidth || currentHeight != audioData.windowHeight) {
      // Update audioData dimensions
      audioData.windowWidth = currentWidth;
      audioData.windowHeight = currentHeight;
      glViewport(0, 0, currentWidth, currentHeight);

      // Update splitter layout
      rootSplitter->update(audioData);

      // Force phosphor contexts to update their textures on next draw
      for (auto* vis : visualizerPtrs) {
        if (auto* lissajousVis = dynamic_cast<LissajousVisualizer*>(vis)) {
          lissajousVis->invalidatePhosphorContext();
        } else if (auto* oscilloscopeVis = dynamic_cast<OscilloscopeVisualizer*>(vis)) {
          oscilloscopeVis->invalidatePhosphorContext();
        } else if (auto* fftVis = dynamic_cast<FFTVisualizer*>(vis)) {
          fftVis->invalidatePhosphorContext();
        }
      }
    }
  }
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

  // Create splitters dynamically based on visualizers
  if (visualizerPtrs.size() >= 2) {
    // Create splitters from right to left
    for (size_t i = visualizerPtrs.size() - 1; i > 0; --i) {
      VisualizerBase* leftVis = visualizerPtrs[i - 1];
      VisualizerBase* rightVis = visualizerPtrs[i];

      // Determine if this splitter should be draggable
      // Visualizers with aspect ratio constraints should have non-draggable splitters to their right
      bool isDraggable = true;
      if (leftVis->shouldEnforceAspectRatio()) {
        isDraggable = false; // Make splitter to the right of aspect-ratio-constrained visualizer non-draggable
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

      // Special handling for aspect ratio constraints
      if (i < visualizerPtrs.size() - 1 && visualizerPtrs[i]->shouldEnforceAspectRatio()) {
        int requiredWidth = visualizerPtrs[i]->getRequiredWidth(audioData.windowHeight);
        if (requiredWidth > 0) {
          int visualizerStart = (i == 0) ? 0 : splitters[i - 1]->getPosition();
          position = visualizerStart + requiredWidth;
        }
      }

      // Special handling when aspect-ratio-constrained visualizer is the first (leftmost) visualizer
      if (i == 0 && !visualizerPtrs.empty() && visualizerPtrs[0]->shouldEnforceAspectRatio()) {
        int requiredWidth = visualizerPtrs[0]->getRequiredWidth(audioData.windowHeight);
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
  } else if (!visualizerPtrs.empty() && splitters.empty()) {
    // Handle single visualizer case - no splitters needed
    VisualizerBase* singleVis = visualizerPtrs[0];
    singleVis->setPosition(0);

    // Special handling for aspect ratio when it's the only visualizer
    if (singleVis->shouldEnforceAspectRatio()) {
      int requiredWidth = singleVis->getRequiredWidth(audioData.windowHeight);
      if (requiredWidth > 0) {
        // Use the required width, but don't exceed window width
        singleVis->setWidth(std::min(requiredWidth, audioData.windowWidth));
      } else {
        singleVis->setWidth(audioData.windowWidth);
      }
    } else {
      singleVis->setWidth(audioData.windowWidth);
    }
  }
}

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return 1;
  }

  // Load config
  Config::load();

  // Initialize the theme system
  Theme::ThemeManager::initialize();

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_Window* win = SDL_CreateWindow("Audio Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     Config::values().window.default_width, Config::values().window.default_height,
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

  // Pre-allocate all FFT-related vectors using config FFT size
  const int FFT_SIZE = Config::values().audio.fft_size;
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
  bool enforcingAspectRatio = false; // Flag to prevent infinite resize loops

  // Audio synchronization
  uint64_t lastAudioFrame = 0;
  uint64_t currentAudioFrame = 0;

  Uint32 frameCount = 0;
  Uint32 lastFpsUpdate = SDL_GetTicks();
  Uint32 lastThemeCheck = SDL_GetTicks();

  // Enforce window aspect ratio if needed (after all initialization)
  enforceWindowAspectRatio(win, visualizerPtrs, enforcingAspectRatio, audioData, splitters, rootSplitter);

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        running = false;
        break;

      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          // Skip processing if we're already enforcing aspect ratio to prevent infinite loops
          if (enforcingAspectRatio) {
            break;
          }

          audioData.windowWidth = event.window.data1;
          audioData.windowHeight = event.window.data2;
          glViewport(0, 0, audioData.windowWidth, audioData.windowHeight);

          // Enforce window aspect ratio if needed (this may trigger another resize event)
          enforceWindowAspectRatio(win, visualizerPtrs, enforcingAspectRatio, audioData, splitters, rootSplitter);

          // Handle aspect ratio for leftmost aspect-ratio-constrained visualizer
          if (!visualizerPtrs.empty() && visualizerPtrs[0]->shouldEnforceAspectRatio()) {
            int requiredWidth = visualizerPtrs[0]->getRequiredWidth(audioData.windowHeight);
            if (requiredWidth > 0 && !splitters.empty()) {
              // Adjust the first splitter position to maintain aspect ratio
              constexpr int MIN_VIS_WIDTH = 80;
              int maxWidth = audioData.windowWidth - (visualizerPtrs.size() - 1) * MIN_VIS_WIDTH;
              int newPosition = std::min(requiredWidth, maxWidth);
              splitters[0]->setPosition(newPosition);
            }
          }

          // Update layout
          if (rootSplitter) {
            rootSplitter->update(audioData);
          } else if (!visualizerPtrs.empty() && splitters.empty()) {
            // Handle single visualizer case - no splitters needed
            VisualizerBase* singleVis = visualizerPtrs[0];
            singleVis->setPosition(0);

            // Special handling for aspect ratio when it's the only visualizer
            if (singleVis->shouldEnforceAspectRatio()) {
              int requiredWidth = singleVis->getRequiredWidth(audioData.windowHeight);
              if (requiredWidth > 0) {
                // Use the required width, but don't exceed window width
                singleVis->setWidth(std::min(requiredWidth, audioData.windowWidth));
              } else {
                singleVis->setWidth(audioData.windowWidth);
              }
            } else {
              singleVis->setWidth(audioData.windowWidth);
            }
          }
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          windowHasFocus = false;
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
          windowHasFocus = true;
        }
        break;

      case SDL_MOUSEMOTION:
        // Handle FFT visualizer mouse tracking
        if (fftVis) {
          int mouseX = event.motion.x;
          int mouseY = event.motion.y;

          // Check if mouse is over the FFT visualizer area specifically
          bool isOverFFT = (mouseX >= fftVis->getPosition() && mouseX < fftVis->getPosition() + fftVis->getWidth() &&
                            mouseY >= 0 && mouseY < audioData.windowHeight);

          if (isOverFFT) {
            float localX = static_cast<float>(mouseX);
            float localY = static_cast<float>(audioData.windowHeight - mouseY); // Flip Y coordinate
            fftVis->updateMousePosition(localX, localY);
            fftVis->setHovering(true);
          } else {
            fftVis->setHovering(false);
          }
        }
        break;
      }

      // Handle splitter events
      for (auto& splitter : splitters) {
        splitter->handleEvent(event, audioData);
      }
    }

    // Wait for new audio data to be available
    currentAudioFrame = audioData.getCurrentFrame();
    if (currentAudioFrame <= lastAudioFrame) {
      // No new audio data, wait for it
      if (!audioData.waitForNewData(lastAudioFrame, std::chrono::milliseconds(16))) { // ~60 FPS timeout
        // Timeout occurred, continue to next frame for UI responsiveness
        continue;
      }
      currentAudioFrame = audioData.getCurrentFrame();
    }
    lastAudioFrame = currentAudioFrame;

    // Update delta time for frame-rate independent animations
    audioData.updateDeltaTime();

    // Reset hover states when window loses focus
    if (!windowHasFocus) {
      for (auto& splitter : splitters) {
        splitter->setHovering(false);
      }
      // Also reset FFT visualizer hover state when window loses focus
      if (fftVis) {
        fftVis->setHovering(false);
      }
    }

    // Update audio data hover state
    audioData.fftHovering = false;
    if (fftVis) {
      audioData.fftHovering = fftVis->isHovering();
    }

    // Check for theme and config changes every second
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - lastThemeCheck >= 1000) {
      // Check for config changes first
      bool configChanged = Config::reloadIfChanged();

      // Check if visualizer configuration has changed
      if (configChanged) {
        VisualizerConfig newConfig = readVisualizerConfig();
        if (newConfig != currentConfig) {
          currentConfig = newConfig;
          rebuildVisualizerLayout(currentConfig, audioData, visualizers, visualizerPtrs, splitters, rootSplitter,
                                  spectrogramVis, lissajousVis, oscilloscopeVis, fftVis);

          // Enforce window aspect ratio after layout rebuild
          enforceWindowAspectRatio(win, visualizerPtrs, enforcingAspectRatio, audioData, splitters, rootSplitter);
        }
      }

      // Then check for direct theme file changes
      Theme::ThemeManager::reloadIfChanged();
      lastThemeCheck = currentTime;
    }

    // Always update layout before drawing
    if (rootSplitter) {
      rootSplitter->update(audioData);
    } else if (!visualizerPtrs.empty() && splitters.empty()) {
      // Handle single visualizer case - no splitters needed
      VisualizerBase* singleVis = visualizerPtrs[0];
      singleVis->setPosition(0);

      // Special handling for aspect ratio when it's the only visualizer
      if (singleVis->shouldEnforceAspectRatio()) {
        int requiredWidth = singleVis->getRequiredWidth(audioData.windowHeight);
        if (requiredWidth > 0) {
          // Use the required width, but don't exceed window width
          singleVis->setWidth(std::min(requiredWidth, audioData.windowWidth));
        } else {
          singleVis->setWidth(audioData.windowWidth);
        }
      } else {
        singleVis->setWidth(audioData.windowWidth);
      }
    }

    // Clear the screen
    const auto& colors = Theme::ThemeManager::colors();
    glClearColor(colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    {
      std::lock_guard<std::mutex> lock(audioData.mutex);

      // Draw all visualizers
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
      if (Config::values().debug.log_fps) {
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