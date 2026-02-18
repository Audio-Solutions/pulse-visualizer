/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS.md)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "include/window_manager.hpp"

#include "include/config.hpp"
#include "include/graphics.hpp"
#include "include/sdl_window.hpp"
#include "include/theme.hpp"
#include "include/visualizers.hpp"

namespace WindowManager {

// Delta time for frame timing
float dt;

// Global window and splitter containers
std::map<std::string, std::vector<VisualizerWindow>> windows;
std::map<std::string, std::vector<Splitter>> splitters;
std::vector<std::string> markedForDeletion;

void setViewport(int x, int width, int height) {
  // Set OpenGL viewport and projection matrix for rendering
  glViewport(x, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, width, 0, height, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void handleEvent(const SDL_Event& event) {
  std::string group = "";
  for (auto& [_group, state] : SDLWindow::states) {
    if (event.window.windowID == state.winID) {
      group = _group;
      break;
    }
  }

  // Lambda to delete this window and relocate visualizers to main
  auto deleteThyself = [&]() {
    for (auto& viz : Config::options.visualizers[group])
      Config::options.visualizers["main"].push_back(viz);
    Config::options.visualizers.erase(group);
    reorder();
  };

  switch (event.type) {
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    deleteThyself();
    break;

  case SDL_EVENT_KEY_DOWN:
    switch (event.key.key) {
    case SDLK_Q:
    case SDLK_ESCAPE:
      deleteThyself();
      break;
    }

  default:
    break;
  }
}

void Splitter::handleEvent(const SDL_Event& event) {
  if (!draggable)
    return;
  int mouseX;
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
    mouseX = event.motion.x;
    // Check if mouse is hovering over splitter
    if (!dragging)
      hovering = abs(mouseX - x) < 5;
    // Handle dragging movement
    if (dragging) {
      dx = mouseX - x;
      x = mouseX;
    }
    break;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      mouseX = event.button.x;
      // Start dragging if mouse is over splitter
      if (abs(mouseX - x) < 5) {
        dragging = true;
      }
    }
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button == SDL_BUTTON_LEFT) {
      mouseX = event.button.x;
      dragging = false;
      hovering = abs(mouseX - x) < 5;
    }
    break;

  default:
    break;
  }
}

void VisualizerWindow::handleEvent(const SDL_Event& event) {
  bool isThisGroup = SDLWindow::states[group].winID == event.window.windowID;
  size_t index;
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
    if (!isThisGroup)
      break;
    // Check if mouse is hovering over this window
    hovering = (event.motion.x >= x && event.motion.x < x + width);
    break;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (!isThisGroup)
      break;
    if (event.button.button == SDL_BUTTON_LEFT) {
      index = this - windows[group].data();

      if (event.button.x >= x && event.button.x < x + width) {
        if (buttonPressed(-1, event.button.x, event.button.y))
          swapVisualizer<Direction::Left>(index, group);
        else if (buttonPressed(1, event.button.x, event.button.y))
          swapVisualizer<Direction::Right>(index, group);
        else if (buttonPressed(2, event.button.x, event.button.y)) {
          popWindow(index, group, true);
        } else if (buttonPressed(-2, event.button.x, event.button.y)) {
          popWindow(index, group, false);
        }
      }
      break;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      hovering = false;
      for (auto& [k, vec] : windows)
        for (auto& w : vec)
          w.hovering = false;
      for (auto& [k, vec] : splitters)
        for (auto& s : vec)
          s.hovering = false;
      break;

    default:
      break;
    }
  }
}

void Splitter::draw() {
  if (SDLWindow::states[group].windowSizes.second == 0) [[unlikely]]
    return;

  // Select the window for rendering
  SDLWindow::selectWindow(group);

  // Set viewport for splitter rendering
  setViewport(x - 5, 10, SDLWindow::states[group].windowSizes.second);

  // Draw splitter line
  Graphics::drawLine(5, 0, 5, SDLWindow::states[group].windowSizes.second, Theme::colors.bgAccent, 2.0f);

  // Draw hover highlight if mouse is over splitter
  if (hovering) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4fv(Theme::alpha(Theme::colors.accent, 0.3f));

    float vertices[] = {0.0f,  0.0f,
                        0.f,   0.f, // Bottom left
                        10.0f, 0.0f,
                        1.f,   0.f, // Bottom right
                        10.0f, (float)SDLWindow::states[group].windowSizes.second,
                        1.f,   1.f, // Top right
                        0.0f,  (float)SDLWindow::states[group].windowSizes.second,
                        0.f,   1.f}; // Top left

    glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, nullptr);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glDrawArrays(GL_QUADS, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
  }
}

void VisualizerWindow::transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type) {
  // Calculate minimum dimensions for texture transfer
  int minWidth = std::min(width, phosphor.textureWidth);
  int minHeight = std::min(SDLWindow::states[group].windowSizes.second, phosphor.textureHeight);
  std::vector<uint8_t> newData(width * SDLWindow::states[group].windowSizes.second * 4, 0);

  if (glIsTexture(oldTex)) [[likely]] {
    // Read pixel data from old texture
    std::vector<uint8_t> oldData(phosphor.textureWidth * phosphor.textureHeight * 4);
    glBindTexture(GL_TEXTURE_2D, oldTex);
    glGetTexImage(GL_TEXTURE_2D, 0, format, type, oldData.data());

    // Calculate offsets for centering texture in both directions
    int srcOffsetX = std::max(0, (phosphor.textureWidth - width) / 2);
    int srcOffsetY = std::max(0, (phosphor.textureHeight - SDLWindow::states[group].windowSizes.second) / 2);
    int dstOffsetX = std::max(0, (width - phosphor.textureWidth) / 2);
    int dstOffsetY = std::max(0, (SDLWindow::states[group].windowSizes.second - phosphor.textureHeight) / 2);

    for (int y = 0; y < minHeight; ++y) {
      int oldRowBase = (y + srcOffsetY) * phosphor.textureWidth * 4 + srcOffsetX * 4;
      int newRowBase = (y + dstOffsetY) * width * 4 + dstOffsetX * 4;
      int x = 0;

#ifdef HAVE_AVX2
      // SIMD-optimized texture transfer
      for (; x + 32 <= minWidth * 4; x += 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldData[oldRowBase + x]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&newData[newRowBase + x]), v);
      }
#endif
      // Standard processing for remaining pixels
      for (; x < minWidth * 4; x += 4) {
        int oldIdx = oldRowBase + x;
        int newIdx = newRowBase + x;
        if (newIdx + 3 < newData.size() && oldIdx + 3 < oldData.size()) {
          newData[newIdx] = oldData[oldIdx];
          newData[newIdx + 1] = oldData[oldIdx + 1];
          newData[newIdx + 2] = oldData[oldIdx + 2];
          newData[newIdx + 3] = oldData[oldIdx + 3];
        }
      }
    }
  }
  // Create new texture with transferred data
  glBindTexture(GL_TEXTURE_2D, newTex);
  glTexImage2D(GL_TEXTURE_2D, 0, format == GL_RED_INTEGER ? GL_R32UI : GL_RGBA8, width,
               SDLWindow::states[group].windowSizes.second, 0, format, type, newData.data());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void VisualizerWindow::resizeTextures() {
  bool sizeChanged =
      (phosphor.textureWidth != width || phosphor.textureHeight != SDLWindow::states[group].windowSizes.second);

  GLuint* textures[] = {&phosphor.energyTextureR, &phosphor.energyTextureG, &phosphor.energyTextureB,
                        &phosphor.tempTextureR,   &phosphor.tempTextureG,   &phosphor.tempTextureB,
                        &phosphor.tempTexture2R,  &phosphor.tempTexture2G,  &phosphor.tempTexture2B,
                        &phosphor.tempTexture3R,  &phosphor.tempTexture3G,  &phosphor.tempTexture3B,
                        &phosphor.outputTexture};

  bool textureUninitialized = [textures]() -> bool {
    for (auto texture : textures) {
      if (*texture == 0)
        return true;
    }
    return false;
  }();

  if (!sizeChanged && !textureUninitialized) [[likely]] {
    return;
  }

  glFinish();
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  SDLWindow::selectWindow(group);

  std::vector<GLuint> oldTextures;
  oldTextures.reserve(size_t(sizeof(textures) / sizeof(textures[0])));

  for (int i = 0; i < size_t(sizeof(textures) / sizeof(textures[0])); ++i) {
    oldTextures.push_back(*textures[i]);
    GLuint newTexture = 0;
    glGenTextures(1, &newTexture);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) [[unlikely]]
      throw std::runtime_error(
          "WindowManager::VisualizerWindow::resizeTextures(): OpenGL error during texture generation: " +
          std::to_string(err));
    if (i == size_t(sizeof(textures) / sizeof(textures[0])) - 1)
      transferTexture(oldTextures[i], newTexture, GL_RGBA, GL_UNSIGNED_BYTE);
    else
      transferTexture(oldTextures[i], newTexture, GL_RED_INTEGER, GL_UNSIGNED_INT);
    if (oldTextures[i]) {
      glDeleteTextures(1, &oldTextures[i]);
    }
    *textures[i] = newTexture;
  }

  phosphor.textureHeight = SDLWindow::states[group].windowSizes.second;
  phosphor.textureWidth = width;

  glBindTexture(GL_TEXTURE_2D, 0);
}

void VisualizerWindow::draw() {
  // Validate phosphor output texture
  if (!glIsTexture(phosphor.outputTexture)) [[unlikely]] {
    throw std::runtime_error("WindowManager::VisualizerWindow::draw(): outputTexture is not a Texture");
  }

  // Select the window for rendering
  SDLWindow::selectWindow(group);

  // Set viewport for this window
  setViewport(x, width, SDLWindow::states[group].windowSizes.second);

  if (Config::options.phosphor.enabled) {
    // Render phosphor effect using output texture
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, phosphor.outputTexture);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    float w = static_cast<float>(width);
    float h = static_cast<float>(SDLWindow::states[group].windowSizes.second);
    float y = 0.0f;

    float vertices[] = {0.0f, y,     0.f, 0.f,  // Bottom left
                        w,    y,     1.f, 0.f,  // Bottom right
                        w,    y + h, 1.f, 1.f,  // Top right
                        0.0f, y + h, 0.f, 1.f}; // Top left

    // Draw textured quad
    glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(float) * 4, nullptr);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glDrawArrays(GL_QUADS, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_TEXTURE_2D);
  }

  // Check for OpenGL errors
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) [[unlikely]]
    LOG_DEBUG(std::string("WindowManager::VisualizerWindow::draw(): OpenGL error during draw: ") + std::to_string(err));
}

void VisualizerWindow::drawArrow(int dir) {
  if (SDLWindow::states[group].windowSizes.second == 0) [[unlikely]]
    return;

  // Don't draw dock arrow in main window
  if (group == "main" && dir == -2)
    return;

  auto [arrowX, arrowY] = getArrowPos(dir);
  if (arrowX == -1 && arrowY == -1)
    return;

  float* bgcolor = Theme::colors.bgAccent;
  if (buttonHovering(dir, SDLWindow::states[group].mousePos.first,
                     SDLWindow::states[group].windowSizes.second - SDLWindow::states[group].mousePos.second))
    bgcolor = Theme::colors.accent;

  // Draw background
  Graphics::drawFilledRect(arrowX, arrowY, buttonSize, buttonSize, bgcolor);

  // Draw arrow
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_POLYGON_SMOOTH);
  glColor4fv(Theme::colors.text);
  const float v = static_cast<float>(std::abs(dir) == 2);
  const float h = 1.0f - v;
  const float alpha = 0.2f * static_cast<float>(dir);

  // Branchless coordinate calc because yes
  float x1 = arrowX + buttonSize * (v * 0.3f + h * (0.5f - alpha));
  float y1 = arrowY + buttonSize * (v * (0.5f - alpha / 2.0f) + h * 0.3f);
  float x2 = arrowX + buttonSize * (v * 0.5f + h * (0.5f + alpha));
  float y2 = arrowY + buttonSize * (v * (0.5f + alpha / 2.0f) + h * 0.5f);
  float x3 = arrowX + buttonSize * (v * 0.7f + h * (0.5f - alpha));
  float y3 = arrowY + buttonSize * (v * (0.5f - alpha / 2.0f) + h * 0.7f);
  float vertices[] = {x1, y1, x2, y2, x3, y3};
  glBindBuffer(GL_ARRAY_BUFFER, SDLWindow::vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, reinterpret_cast<void*>(0));
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisableClientState(GL_VERTEX_ARRAY);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glDisable(GL_POLYGON_SMOOTH);
  glDisable(GL_BLEND);
}

bool VisualizerWindow::buttonPressed(int dir, int mouseX, int mouseY) {
  auto [arrowX, arrowY] = getArrowPos(dir);
  if (arrowX == -1 && arrowY == -1)
    return false;

  return (mouseX >= x + arrowX && mouseX < x + arrowX + buttonSize &&
          SDLWindow::states[group].windowSizes.second - mouseY >= arrowY &&
          SDLWindow::states[group].windowSizes.second - mouseY < arrowY + buttonSize);
}

bool VisualizerWindow::buttonHovering(int dir, int mouseX, int mouseY) { return buttonPressed(dir, mouseX, mouseY); }

void drawSplitters() {
  // Draw all window splitters
  for (auto& [key, vec] : splitters) {
    for (auto& splitter : vec) {
      SDLWindow::selectWindow(splitter.group);
      splitter.draw();
    }
  }
}

void renderAll() {
  // Render all visualizer windows
  for (auto& [key, vec] : windows) {
    for (auto& window : vec) {
      SDLWindow::selectWindow(window.group);
      if (window.render)
        window.render();
    }
  }

  // Draw arrows for each window
  for (auto& [key, vec] : windows) {
    for (auto& window : vec) {
      SDLWindow::selectWindow(window.group);
      if (SDLWindow::states[window.group].focused && window.hovering) {
        window.drawArrow(-1);
        window.drawArrow(1);
        window.drawArrow(2);
        window.drawArrow(-2);
      }
    }
  }
}

void resizeTextures() {
  // Resize phosphor textures for all windows
  for (auto& [key, vec] : windows) {
    for (auto& window : vec) {
      SDLWindow::selectWindow(window.group);
      window.resizeTextures();
    }
  }
}

void resizeWindows() {
  constexpr int MIN_WINDOW_SIZE = 1;
  for (auto& [key, vec] : windows) {
    if (splitters[key].size() == 0) {
      // Single window case
      vec[0].x = 0;
      vec[0].width = std::max(SDLWindow::states[vec[0].group].windowSizes.first, MIN_WINDOW_SIZE);
    } else {
      // Multiple windows with splitters
      vec[0].x = 0;
      vec[0].width = std::max(splitters[key][0].x - 1, MIN_WINDOW_SIZE);
      for (int i = 1; i < static_cast<int>(vec.size()) - 1; i++) {
        vec[i].x = splitters[key][i - 1].x + 1;
        vec[i].width = std::max(splitters[key][i].x - splitters[key][i - 1].x - 2, MIN_WINDOW_SIZE);
      }

      vec.back().x = splitters[key].back().x + 1;
      vec.back().width =
          std::max(SDLWindow::states[vec.back().group].windowSizes.first - vec.back().x - 1, MIN_WINDOW_SIZE);
    }
  }
}

int moveSplitter(std::string key, int index, int targetX) {
  // Get references to adjacent windows and splitters
  VisualizerWindow* left = &windows[key][index];
  VisualizerWindow* right = &windows[key][index + 1];
  Splitter* splitter = &splitters[key][index];
  Splitter* splitterLeft = (index != 0 ? &splitters[key][index - 1] : nullptr);
  Splitter* splitterRight =
      (index != static_cast<int>(splitters[key].size()) - 1 ? &splitters[key][index + 1] : nullptr);

  std::string& group = left->group;

  static int iter = 0;

  // Set target position if provided
  if (targetX != -1)
    splitter->x = targetX;
  else
    iter = 0;

  // Prevent infinite loops
  if (++iter > 20) {
    return -1;
  }

  // Lambda function to handle splitter movement constraints
  auto move = [&](int direction) -> bool {
    Splitter* neighborSplitter = direction == 1 ? splitterRight : splitterLeft;
    VisualizerWindow* neighborWindow = direction == 1 ? right : left;
    int boundary = direction == 1 ? SDLWindow::states[group].windowSizes.first : 0;
    if (!neighborWindow) {
      // Handle minimum width constraints for null window case
      if (neighborSplitter) {
        if (direction * (neighborSplitter->x - splitter->x) < MIN_WIDTH) {
          moveSplitter(key, index + direction, splitter->x + direction * MIN_WIDTH);
          return false;
        }
      } else if (direction * (boundary - splitter->x) < MIN_WIDTH) {
        splitter->x = boundary - direction * MIN_WIDTH;
        return false;
      }
      return true;
    }

    int forceWidth = neighborWindow->forceWidth;
    if (!forceWidth && neighborWindow->aspectRatio != 0.0f) {
      forceWidth = std::min(
          static_cast<int>(neighborWindow->aspectRatio * SDLWindow::states[group].windowSizes.second),
          static_cast<int>(SDLWindow::states[group].windowSizes.first - (windows[key].size() - 1) * MIN_WIDTH));
    }

    if (forceWidth > 0) {
      if (neighborSplitter) {
        if (direction * (neighborSplitter->x - splitter->x) != forceWidth) {
          moveSplitter(key, index + direction, splitter->x + direction * forceWidth);
          return false;
        }
      } else if (direction * (boundary - splitter->x) != forceWidth) {
        splitter->x = boundary - direction * forceWidth;
        return false;
      }
    } else if (neighborSplitter) {
      if (direction * (neighborSplitter->x - splitter->x) < MIN_WIDTH) {
        moveSplitter(key, index + direction, splitter->x + direction * MIN_WIDTH);
        return false;
      }
    } else if (direction * (boundary - splitter->x) < MIN_WIDTH) {
      splitter->x = boundary - direction * MIN_WIDTH;
      return false;
    }

    return true;
  };

  // Iteratively solve splitter constraints
  int i = 0;
  while (!(move(1) && move(-1)) && i < 20)
    i++;
  return i;
}

void updateSplitters() {
  // Find currently dragging splitter
  for (auto& [key, vec] : splitters) {
    int draggingIndex = -1;
    for (int i = 0; i < static_cast<int>(vec.size()); ++i) {
      if (vec[i].dragging) {
        draggingIndex = i;
        break;
      }
    }
    if (draggingIndex == -1) {
      for (int i = 0; i < static_cast<int>(vec.size()); i++) {
        moveSplitter(key, i);
      }
    } else {
      moveSplitter(key, draggingIndex);
    }
  }

  // Special handling for centered oscilloscope
  if (Config::options.oscilloscope.centered) {
    for (auto& [key, windowVec] : windows) {
      for (size_t i = 0; i < windowVec.size(); ++i) {
        if (windowVec[i].render == Oscilloscope::render) {
          int mainWindowWidth = SDLWindow::states[key].windowSizes.first;
          // Mirror right splitter if left splitter is in left half of screen
          if (i > 0 && i < splitters[key].size() && splitters[key][i - 1].x < mainWindowWidth / 2 - MIN_WIDTH / 2) {
            splitters[key][i].x = mainWindowWidth - splitters[key][i - 1].x;
          }
          break;
        }
      }
    }
  }
}

void VisualizerWindow::cleanup() {
  SDLWindow::selectWindow(group);
  // Cleanup phosphor effect textures
  GLuint* textures[] = {&phosphor.energyTextureR, &phosphor.energyTextureG, &phosphor.energyTextureB,
                        &phosphor.tempTextureR,   &phosphor.tempTextureG,   &phosphor.tempTextureB,
                        &phosphor.tempTexture2R,  &phosphor.tempTexture2G,  &phosphor.tempTexture2B,
                        &phosphor.tempTexture3R,  &phosphor.tempTexture3G,  &phosphor.tempTexture3B,
                        &phosphor.outputTexture};

  for (auto texture : textures) {
    if (*texture) {
      glDeleteTextures(1, texture);
      *texture = 0;
    }
  }
}

void reorder() {
  // Define visualizer render functions and their window pointers
  std::vector<std::pair<void (*)(), WindowManager::VisualizerWindow*&>> visualizers = {
      {SpectrumAnalyzer::render, SpectrumAnalyzer::window},
      {Lissajous::render,        Lissajous::window       },
      {Oscilloscope::render,     Oscilloscope::window    },
      {Spectrogram::render,      Spectrogram::window     },
      {LUFS::render,             LUFS::window            },
      {VU::render,               VU::window              }
  };

  // Map visualizer names to their indices
  std::map<std::string, size_t> visualizerMap = {
      {"spectrum_analyzer", 0},
      {"lissajous",         1},
      {"oscilloscope",      2},
      {"spectrogram",       3},
      {"lufs",              4},
      {"vu",                5}
  };

  // loop over the keys in the map
  for (auto& [key, value] : Config::options.visualizers) {

    if (value.empty() && key == "main") {
      LOG_ERROR("Warning: Main window has no visualizers.");
      exit(1);
    }

    // Skip "hidden" window
    if (key == "hidden")
      continue;

    // Cleanup existing windows in this group before clearing
    if (windows.find(key) != windows.end()) {
      for (auto& window : windows[key]) {
        window.cleanup();
      }
    }

    // Create window if not main and doesn't already exist
    if (key != "main" && windows.find(key) == windows.end()) {
      SDLWindow::createWindow(key, key, Config::options.window.default_width, Config::options.window.default_height);
    }

    // Clear existing windows and splitters for this group
    windows[key].clear();
    splitters[key].clear();
    windows[key].reserve(value.size());

    // Create windows in the order specified in configuration
    for (size_t i = 0; i < value.size(); ++i) {
      const std::string& visName = value[i];
      auto it = visualizerMap.find(visName);
      if (it == visualizerMap.end()) {
        LOG_ERROR(std::string("Warning: Unknown visualizer '") + visName + "' in configuration");
        continue;
      }

      size_t idx = it->second;
      VisualizerWindow vw {};

      vw.group = key;

      // Set aspect ratio for Lissajous (square) visualizer
      vw.aspectRatio = (visName == "lissajous") ? 1.0f : 0.0f;
      vw.forceWidth = 0;
      if (visName == "lufs") {
        if (Config::options.lufs.label == "on")
          vw.forceWidth = 150;
        else if (Config::options.lufs.label == "compact")
          vw.forceWidth = 100;
        else
          vw.forceWidth = 70;
      }
      if (visName == "vu") {
        if (Config::options.vu.style == "digital")
          vw.forceWidth = 60;
        else
          vw.aspectRatio = 2.0f;
      }

      if (visName == "spectrum_analyzer" && Config::options.fft.sphere.enabled) {
        vw.aspectRatio = 1.0f;
      }

      vw.render = visualizers[idx].first;
      windows[key].push_back(vw);
      visualizers[idx].second = &windows[key].back();

      // Create splitter between windows
      if (i + 1 < value.size()) {
        Splitter s;
        // Disable dragging for first splitter if Lissajous or LUFS is first
        if (i == 0 && (vw.aspectRatio != 0.0f || vw.forceWidth != 0))
          s.draggable = false;
        s.group = key;
        splitters[key].push_back(s);
      }
    }

    // Unset forceWidth or aspectRatio for last window if all windows have enforcers
    if (!windows[key].empty() && std::all_of(windows[key].begin(), windows[key].end(), [](const VisualizerWindow& vw) {
          return vw.forceWidth != 0 || vw.aspectRatio != 0.0f;
        })) {
      windows[key].back().forceWidth = 0;
      windows[key].back().aspectRatio = 0.0f;
    }

    // Disable dragging for last splitter if Lissajous or LUFS is last
    if (!windows[key].empty() && !splitters[key].empty() &&
        (windows[key].back().aspectRatio != 0.0f || windows[key].back().forceWidth != 0))
      splitters[key].back().draggable = false;

    // Propagate fixed splitters from outside inwards
    if (splitters[key].size() > 1) {
      // Propagate from left to right
      for (size_t i = 1; i < splitters[key].size(); ++i) {
        if (!splitters[key][i - 1].draggable &&
            (windows[key][i].aspectRatio != 0.0f || windows[key][i].forceWidth != 0)) {
          splitters[key][i].draggable = false;
        }
      }

      // Propagate from right to left
      for (int i = static_cast<int>(splitters[key].size()) - 2; i >= 0; --i) {
        if (!splitters[key][i + 1].draggable &&
            (windows[key][i + 1].aspectRatio != 0.0f || windows[key][i + 1].forceWidth != 0)) {
          splitters[key][i].draggable = false;
        }
      }
    }

    // Spread splitters evenly across the window width
    size_t n = windows[key].size();
    if (n > 1) {
      for (size_t i = 0; i < splitters[key].size(); ++i) {
        splitters[key][i].x = static_cast<int>((i + 1) * SDLWindow::states[key].windowSizes.first / n);
      }
    }
  }

  // Mark keys not in config for deletion
  for (auto& [key, value] : windows) {
    if (Config::options.visualizers.find(key) == Config::options.visualizers.end()) {
      markedForDeletion.push_back(key);
    }
  }
}

template <Direction direction> void swapVisualizer(size_t index, std::string key) {
  if (index < 0 || index >= windows[key].size() || index + direction < 0 || index + direction >= windows[key].size())
    return;

  std::swap(Config::options.visualizers[key][index], Config::options.visualizers[key][index + direction]);
  Config::save();
}

void popWindow(size_t index, std::string key, bool popout) {
  if (index < 0 || index >= windows[key].size() || (!popout && key == "main"))
    return;

  std::string newKey = popout ? key + "_" + std::to_string(rand() % 1000000) : "main";
  std::string viz = Config::options.visualizers[key][index];
  Config::options.visualizers[key].erase(Config::options.visualizers[key].begin() + index);
  if (Config::options.visualizers[key].empty()) {
    Config::options.visualizers.erase(key);
  }
  Config::options.visualizers[newKey].push_back(viz);

  reorder();
}

void deleteMarkedWindows() {
  // Delete stale keys
  for (auto& key : markedForDeletion) {
    for (auto& window : windows[key]) {
      window.cleanup();
    }
    SDLWindow::destroyWindow(windows[key][0].group);
    windows[key].clear();
    splitters[key].clear();
    windows.erase(key);
    splitters.erase(key);
  }
  markedForDeletion.clear();
}

std::pair<int, int> VisualizerWindow::getArrowPos(int dir) {
  // hide arrows if first or last window in windows array
  size_t winIdx = this - windows[group].data();
  bool isLast = winIdx == windows[group].size() - 1;
  bool isFirst = winIdx == 0;
  if ((isLast && dir == 1) || (isFirst && dir == -1))
    return {-1, -1};

  SDLWindow::selectWindow(group);
  setViewport(x, width, SDLWindow::states[group].windowSizes.second);

  bool wideEnough = width > buttonPadding * 2 + buttonSize * 4;

  int arrowX = (dir == -1 || (dir == -2 && !wideEnough)) ? buttonPadding
               : (dir == 2 && wideEnough)                ? width - buttonPadding - buttonSize * 2
               : (dir == -2)                             ? buttonPadding + buttonSize
                                                         : width - buttonPadding - buttonSize;
  int arrowY = !wideEnough && abs(dir) == 2 ? SDLWindow::states[group].windowSizes.second - buttonPadding - buttonSize
                                            : buttonPadding;

  if (isLast && dir == 2 && wideEnough)
    arrowX += buttonSize;

  if (isFirst && dir == -2 && wideEnough)
    arrowX -= buttonSize;

  return {arrowX, arrowY};
}

} // namespace WindowManager
