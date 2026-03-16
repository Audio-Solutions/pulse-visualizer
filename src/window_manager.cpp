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
#include "include/visualizer_registry.hpp"

namespace WindowManager {

float dt;

std::atomic<bool> boundsDirty = true;

void setViewport(Bounds bounds) {
  // Set OpenGL viewport and projection matrix for rendering
  glViewport(bounds.x, bounds.y, bounds.w, bounds.h);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, bounds.w, 0, bounds.h, -10.0, 10.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

/**
 * @brief Walks the entire hierarchy tree and applies a callback to each node.
 * @param node The root node to start traversal from.
 * @param callback The function to call for each node pointer.
 */
template <typename Fn> void walkTree(Node& nodePtr, Fn&& callback) {
  if (!nodePtr)
    return;

  // clang-format off
  std::visit(Visitor {
    [&](Splitter& s) {
      walkTree(s.primary, callback);
      walkTree(s.secondary, callback);
    },
    [&](auto&) {}},
    *nodePtr);
  // clang-format on

  callback(nodePtr);
}

void Splitter::render() {
  if (orientation == Orientation::Horizontal) {
    int relX = static_cast<int>((bounds.w - SPLITTER_WIDTH) * ratio + SPLITTER_WIDTH / 2);
    Graphics::drawLine(relX, 0, relX, bounds.h, Theme::colors.bgAccent, 2.0f);
    if (hovering)
      Graphics::drawFilledRect(relX - 5, 0, 10, bounds.h, Theme::alpha(Theme::colors.accent, 0.3f));
  } else {
    int relY = static_cast<int>((bounds.h - SPLITTER_WIDTH) * ratio + SPLITTER_WIDTH / 2);
    Graphics::drawLine(0, relY, bounds.w, relY, Theme::colors.bgAccent, 2.0f);
    if (hovering)
      Graphics::drawFilledRect(0, relY - 5, bounds.w, 10, Theme::alpha(Theme::colors.accent, 0.3f));
  }
}

/**
 * @brief Update cursor for a window group based on the state of its tree.
 * @param group The group to update
 */
void updateCursorForGroup(const std::string& group) {
  if (group.empty())
    return;
  auto it = SDLWindow::states.find(group);
  if (it == SDLWindow::states.end())
    return;

  auto& state = it->second;

  bool isDragging = false;

  walkTree(state.root, [&](Node& node) {
    // clang-format off
    std::visit(Visitor {
      [&](std::shared_ptr<VisualizerWindow>& w) {
        if (w->dragging)
          isDragging = true;
      },
      [&](auto&) {}},
      *node);
    // clang-format on
  });

  // If any visualizer is dragging, show Move cursor and return.
  if (isDragging) {
    SDLWindow::setCursor(SDLWindow::CursorType::Move);
    return;
  }

  bool anyHorizontal = false;
  bool anyVertical = false;

  walkTree(state.root, [&](Node& node) {
    // clang-format off
    std::visit(Visitor {
      [&](Splitter& s) {
        if (s.hovering) {
          if (s.orientation == Orientation::Horizontal)
            anyHorizontal = true;
          else
            anyVertical = true;
        }
      },
      [&](auto&) {}},
      *node);
    // clang-format on
  });

  SDLWindow::CursorType ct = SDLWindow::CursorType::Default;
  if (anyHorizontal && anyVertical)
    ct = SDLWindow::CursorType::Move;
  else if (anyHorizontal)
    ct = SDLWindow::CursorType::Horizontal;
  else if (anyVertical)
    ct = SDLWindow::CursorType::Vertical;

  SDLWindow::setCursor(ct);
}

void Splitter::handleEvent(const SDL_Event& event) {
  std::string group = "";
  for (auto& [_group, state] : SDLWindow::states) {
    if (event.window.windowID == state.winID) {
      group = _group;
      break;
    }
  }

  if (group.empty() || group != this->group || !movable)
    return;

  float mouseX, mouseY;
  auto isNearLine = [&](int x, int y) {
    int dist;
    if (orientation == WindowManager::Orientation::Horizontal) {
      if (y < bounds.y - 6 || y > bounds.y + bounds.h + 6)
        return false;
      dist = std::abs(x - (bounds.x + ratio * bounds.w));
    } else {
      if (x < bounds.x - 6 || x > bounds.x + bounds.w + 6)
        return false;
      dist = std::abs(y - (bounds.y + ratio * bounds.h));
    }
    return dist < 6;
  };

  const float MIN_RATIO =
      MIN_SIDELENGTH / (orientation == WindowManager::Orientation::Horizontal ? bounds.w : bounds.h);

  bool hoverChanged;
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
    mouseX = event.motion.x;
    mouseY = SDLWindow::states[group].windowSizes.second - event.motion.y;
    hovering = isNearLine(mouseX, mouseY);

    hoverChanged = (hovering != prevHovering);
    prevHovering = hovering;
    if (hoverChanged && !group.empty())
      updateCursorForGroup(group);

    if (!dragging)
      break;

    if (orientation == WindowManager::Orientation::Horizontal)
      ratio = (mouseX - bounds.x) / bounds.w;
    else
      ratio = (mouseY - bounds.y) / bounds.h;

    ratio = std::max(MIN_RATIO, std::min(1.0f - MIN_RATIO, ratio));

    boundsDirty.store(true);
    break;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    mouseX = event.button.x;
    mouseY = SDLWindow::states[group].windowSizes.second - event.button.y;
    if (event.button.button == SDL_BUTTON_LEFT)
      if (isNearLine(mouseX, mouseY)) {
        dragging = true;
      }
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button == SDL_BUTTON_LEFT) {
      if (dragging) {
        dragging = false;
      } else {
        dragging = false;
      }
    }
    break;

  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    hovering = false;
    if (dragging) {
      dragging = false;
      SDLWindow::setCursor(SDLWindow::CursorType::Default);
    } else if (!group.empty()) {
      updateCursorForGroup(group);
    }
    break;

  default:
    break;
  }
}

void updateBounds(Node& node, Bounds bounds) {
  if (!node)
    return;

  bounds.w = std::clamp(bounds.w, MIN_SIDELENGTH / 2, MAX_SIDELENGTH);
  bounds.h = std::clamp(bounds.h, MIN_SIDELENGTH / 2, MAX_SIDELENGTH);

  std::function<std::pair<Size, Constraint>(Node&, Bounds, WindowManager::Orientation)> getConstraints;
  getConstraints = [&](Node& node, Bounds bounds,
                       WindowManager::Orientation orientation) -> std::pair<Size, Constraint> {
    // clang-format off
    return std::visit(Visitor {
        [&](std::shared_ptr<VisualizerWindow>& w) {
          Size constraintSize = 
              Size{w->minSize.w > 0 ? w->minSize.w : MIN_SIDELENGTH, w->minSize.h > 0 ? w->minSize.h : MIN_SIDELENGTH};
          Constraint constraint {};
          if (w->forceWidth > FLT_EPSILON) {
            constraintSize.w = w->forceWidth;
            constraint.w = ConstraintType::Forced;
          } else if (w->aspectRatio > FLT_EPSILON) {
            if (orientation == WindowManager::Orientation::Horizontal) {
              constraintSize.w = bounds.h * w->aspectRatio;
              constraint.w = ConstraintType::Forced;
            } else {
              constraintSize.h = bounds.w / w->aspectRatio;
              constraint.h = ConstraintType::Forced;
            }
          }

          return std::make_pair(constraintSize, constraint);
        },
        [&](Splitter& s) {
          int totalSize =
              (s.orientation == WindowManager::Orientation::Horizontal ? bounds.w : bounds.h) - SPLITTER_WIDTH;
          int primarySize = static_cast<int>(totalSize * s.ratio);
          int secondarySize = totalSize - primarySize;

          Bounds primaryBounds = s.orientation == WindowManager::Orientation::Horizontal
                                    ? Bounds(bounds.x, bounds.y, primarySize, bounds.h)
                                    : Bounds(bounds.x, bounds.y, bounds.w, primarySize);
          Bounds secondaryBounds =
              s.orientation == WindowManager::Orientation::Horizontal
                  ? Bounds(bounds.x + primarySize + SPLITTER_WIDTH, bounds.y, secondarySize, bounds.h)
                  : Bounds(bounds.x, bounds.y + primarySize + SPLITTER_WIDTH, bounds.w, secondarySize);

          std::tie(s.primaryConstraintSize, s.primaryConstraint) =
              getConstraints(s.primary, primaryBounds, s.orientation);
          std::tie(s.secondaryConstraintSize, s.secondaryConstraint) =
              getConstraints(s.secondary, secondaryBounds, s.orientation);

          Size constraintSize;
          Constraint constraint;
          if (s.orientation == WindowManager::Orientation::Horizontal) {
            int totalW = s.primaryConstraintSize.w + s.secondaryConstraintSize.w;
            int maxH = std::max(s.primaryConstraintSize.h, s.secondaryConstraintSize.h);
            constraintSize = Size(totalW, maxH);
            ConstraintType wType = (s.primaryConstraint.w == ConstraintType::Forced &&
                                    s.secondaryConstraint.w == ConstraintType::Forced)
                                      ? ConstraintType::Forced
                                      : ConstraintType::Minimum;
            ConstraintType hType = (s.primaryConstraint.h == ConstraintType::Forced ||
                                    s.secondaryConstraint.h == ConstraintType::Forced)
                                      ? ConstraintType::Forced
                                      : ConstraintType::Minimum;
            constraint = Constraint(wType, hType);
          } else {
            int totalH = s.primaryConstraintSize.h + s.secondaryConstraintSize.h;
            int maxW = std::max(s.primaryConstraintSize.w, s.secondaryConstraintSize.w);
            constraintSize = Size(maxW, totalH);
            ConstraintType hType = (s.primaryConstraint.h == ConstraintType::Forced &&
                                    s.secondaryConstraint.h == ConstraintType::Forced)
                                      ? ConstraintType::Forced
                                      : ConstraintType::Minimum;
            ConstraintType wType = (s.primaryConstraint.w == ConstraintType::Forced ||
                                    s.secondaryConstraint.w == ConstraintType::Forced)
                                      ? ConstraintType::Forced
                                      : ConstraintType::Minimum;
            constraint = Constraint(wType, hType);
          }

          return std::make_pair(constraintSize, constraint);
        },
        [&](auto&) {return std::make_pair(Size{MIN_SIDELENGTH, MIN_SIDELENGTH}, Constraint{});}},
        *node);
  };

  std::visit(Visitor {
    [&](std::shared_ptr<VisualizerWindow>& w) {
      w->bounds = bounds;
      w->resizeTextures();
    },
    [&](Splitter& s) {
      int& boundAxis = s.orientation == WindowManager::Orientation::Horizontal ? bounds.w : bounds.h;
      int totalSize = boundAxis - SPLITTER_WIDTH;
      int primarySize = static_cast<int>(totalSize * s.ratio);
      int secondarySize = totalSize - primarySize;

      Bounds primaryBounds = s.orientation == WindowManager::Orientation::Horizontal
                                ? Bounds(bounds.x, bounds.y, primarySize, bounds.h)
                                : Bounds(bounds.x, bounds.y, bounds.w, primarySize);
      Bounds secondaryBounds =
          s.orientation == WindowManager::Orientation::Horizontal
              ? Bounds(bounds.x + primarySize + SPLITTER_WIDTH, bounds.y, secondarySize, bounds.h)
              : Bounds(bounds.x, bounds.y + primarySize + SPLITTER_WIDTH, bounds.w, secondarySize);

      auto [primaryConstraintSize, primaryConstraint] =
          getConstraints(s.primary, primaryBounds, s.orientation);
      auto [secondaryConstraintSize, secondaryConstraint] =
          getConstraints(s.secondary, secondaryBounds, s.orientation);

      ConstraintType primaryAxis =
          s.orientation == WindowManager::Orientation::Horizontal ? primaryConstraint.w : primaryConstraint.h;
      ConstraintType secondaryAxis =
          s.orientation == WindowManager::Orientation::Horizontal ? secondaryConstraint.w : secondaryConstraint.h;
      int primaryAxisSize = s.orientation == WindowManager::Orientation::Horizontal ? primaryConstraintSize.w
                                                                            : primaryConstraintSize.h;
      int secondaryAxisSize = s.orientation == WindowManager::Orientation::Horizontal ? secondaryConstraintSize.w
                                                                              : secondaryConstraintSize.h;

      if (primaryAxis == ConstraintType::Minimum && secondaryAxis == ConstraintType::Minimum) {
        boundAxis = std::max(boundAxis, primaryAxisSize + secondaryAxisSize + SPLITTER_WIDTH);
        float minRatio = static_cast<float>(primaryAxisSize) / (boundAxis - SPLITTER_WIDTH);
        float maxRatio = 1.0f - static_cast<float>(secondaryAxisSize) / (boundAxis - SPLITTER_WIDTH);
        if (minRatio > maxRatio)
          s.ratio = minRatio;
        else
          s.ratio = std::clamp(s.ratio, minRatio, maxRatio);
        s.movable = true;
      } else if (primaryAxis == ConstraintType::Forced && secondaryAxis == ConstraintType::Minimum ||
                primaryAxis == ConstraintType::Minimum && secondaryAxis == ConstraintType::Forced) {
        boundAxis = std::max(boundAxis, primaryAxisSize + secondaryAxisSize + SPLITTER_WIDTH);
        s.ratio = primaryAxis == ConstraintType::Forced
                        ? static_cast<float>(primaryAxisSize) / (boundAxis - SPLITTER_WIDTH)
                        : 1.0f - static_cast<float>(secondaryAxisSize) / (boundAxis - SPLITTER_WIDTH);
        s.movable = false;
      } else {
        boundAxis = primaryAxisSize + secondaryAxisSize + SPLITTER_WIDTH;
        s.ratio = static_cast<float>(primaryAxisSize) / (boundAxis - SPLITTER_WIDTH);
        s.movable = false;
      }

      totalSize = boundAxis - SPLITTER_WIDTH;
      primarySize = static_cast<int>(totalSize * s.ratio);
      secondarySize = totalSize - primarySize;

      primaryBounds = s.orientation == WindowManager::Orientation::Horizontal
                          ? Bounds(bounds.x, bounds.y, primarySize, bounds.h)
                          : Bounds(bounds.x, bounds.y, bounds.w, primarySize);
      secondaryBounds =
          s.orientation == WindowManager::Orientation::Horizontal
              ? Bounds(bounds.x + primarySize + SPLITTER_WIDTH, bounds.y, secondarySize, bounds.h)
              : Bounds(bounds.x, bounds.y + primarySize + SPLITTER_WIDTH, bounds.w, secondarySize);

      updateBounds(s.primary, primaryBounds);
      updateBounds(s.secondary, secondaryBounds);

      s.bounds = bounds;
    },
    [&](auto&) {}},
      *node);
  // clang-format on
}

std::tuple<VisualizerWindow*, VisualizerWindow*, HoverRegion> getHoverState(SDLWindow::State& state) {
  VisualizerWindow* draggingWindow = nullptr;
  VisualizerWindow* hoveringWindow = nullptr;

  if (!state.root)
    return std::tuple {nullptr, nullptr, HoverRegion::None};

  walkTree(state.root, [&](Node& node) {
    // clang-format off
    std::visit(Visitor {
      [&](std::shared_ptr<VisualizerWindow>& w) {
        if (w->dragging)
          draggingWindow = w.get();
        if (w->hovering)
          hoveringWindow = w.get();
      },
      [&](auto&) {}},
      *node);
    // clang-format on
  });

  if (draggingWindow && hoveringWindow && draggingWindow != hoveringWindow) {
    auto hoveringBounds = hoveringWindow->bounds;
    int halfW = hoveringBounds.w / 2;
    int halfH = hoveringBounds.h / 2;
    int centerX = (hoveringBounds.w - halfW) / 2;
    int centerY = (hoveringBounds.h - halfH) / 2;

    int mx = state.mousePos.first - hoveringBounds.x;
    int my = state.mousePos.second - hoveringBounds.y;

    if (mx >= centerX && mx < centerX + halfW && my >= centerY && my < centerY + halfH) {
      return {draggingWindow, hoveringWindow, HoverRegion::Center};
    }

    // Normalize mouse to [-1,1] relative to bounds center
    float normX = (mx / (hoveringBounds.w / 2.0f)) - 1.0f;
    float normY = (my / (hoveringBounds.h / 2.0f)) - 1.0f;

    // Edge center normalized positions
    float topDist = std::abs(normY + 1.0f);
    float bottomDist = std::abs(normY - 1.0f);
    float leftDist = std::abs(normX + 1.0f);
    float rightDist = std::abs(normX - 1.0f);

    float minDist = std::min({topDist, bottomDist, leftDist, rightDist});
    if (minDist == topDist && my < centerY) {
      return {draggingWindow, hoveringWindow, HoverRegion::Top};
    }
    if (minDist == bottomDist && my >= centerY + halfH) {
      return {draggingWindow, hoveringWindow, HoverRegion::Bottom};
    }
    if (minDist == leftDist && mx < centerX) {
      return {draggingWindow, hoveringWindow, HoverRegion::Left};
    }
    if (minDist == rightDist && mx >= centerX + halfW) {
      return {draggingWindow, hoveringWindow, HoverRegion::Right};
    }
  }

  return {nullptr, nullptr, HoverRegion::None};
}

void swapVisualizer(Node& root, std::string_view draggingId, std::string_view hoveringId) {
  if (!root)
    return;

  Node* aPtr = nullptr;
  Node* bPtr = nullptr;

  walkTree(root, [&](Node& node) {
    // clang-format off
    std::visit(Visitor {
      [&](std::shared_ptr<VisualizerWindow>& w) {
        if (w->id == draggingId)
          aPtr = &node;
        if (w->id == hoveringId)
          bPtr = &node;
      },
      [&](auto&) {}},
      *node);
    // clang-format on
  });

  if (!aPtr || !bPtr || aPtr == bPtr)
    return;

  std::swap(*aPtr, *bPtr);
  Config::save();
}

/**
 * @brief Helper to remove a visualizer by id. Promotes sibling when a child is removed.
 */
bool removeVisualizerById(Node& n, const std::string& id) {

  // clang-format off
  return std::visit(Visitor {
    [&](std::shared_ptr<VisualizerWindow>& w) -> bool {
      if (w->id == id) {
        n.reset();
        return true;
      }
      return false;
    },
    [&](Splitter& s) -> bool {
      if (removeVisualizerById(s.primary, id)) {
        if (!s.primary)
          n = std::move(s.secondary);
        return true;
      }
      if (removeVisualizerById(s.secondary, id)) {
        if (!s.secondary)
          n = std::move(s.primary);
        return true;
      }
      return false;
    }}, 
    *n);
  // clang-format on
}

/**
 * @brief Insert a Splitter at the hovered location and move 'dragging' into it.
 */
void insertVisualizer(std::string group, std::string draggingId, std::string hoveringId, HoverRegion region) {
  auto it = Config::options.visualizers.find(group);
  if (it == Config::options.visualizers.end())
    return;

  Node* aParentPtr = nullptr;
  Node* bParentPtr = nullptr;

  walkTree(it->second, [&](Node& node) {
    // clang-format off
    std::visit(Visitor {
      [&](Splitter& s) {
        auto checkNode = [&](Node& opt) {
          if (auto wPtr = std::get_if<std::shared_ptr<VisualizerWindow>>(opt.get())) {
            auto w = *wPtr;
            if (w->id == draggingId)
              aParentPtr = &node;
            if (w->id == hoveringId)
              bParentPtr = &node;
          }
        };
        checkNode(s.primary);
        checkNode(s.secondary);
      },
      [&](auto&) {}},
      *node);
    // clang-format on
  });

  if (!aParentPtr || !bParentPtr)
    return;

  auto dragging = VisualizerRegistry::find(draggingId);
  auto hovering = VisualizerRegistry::find(hoveringId);

  Splitter s;
  s.orientation = (region == HoverRegion::Top || region == HoverRegion::Bottom)
                      ? WindowManager::Orientation::Vertical
                      : WindowManager::Orientation::Horizontal;

  switch (region) {
  case HoverRegion::Top:
  case HoverRegion::Left:
    s.primary = std::make_shared<WindowManager::Variant>(dragging);
    s.secondary = std::make_shared<WindowManager::Variant>(hovering);
    break;
  case HoverRegion::Bottom:
  case HoverRegion::Right:
    s.primary = std::make_shared<WindowManager::Variant>(hovering);
    s.secondary = std::make_shared<WindowManager::Variant>(dragging);
  default:
    break;
  };
  s.ratio = 0.5f;

  if (aParentPtr == bParentPtr) {
    // Parents are equal, update in-place
    *aParentPtr = std::make_shared<WindowManager::Variant>(std::move(s));
    boundsDirty.store(true);
    updateBounds();
    Config::save();
    return;
  }

  // Remove dragging node first
  removeVisualizerById(it->second, draggingId);

  Node* hoveringNode = nullptr;
  walkTree(it->second, [&](Node& node) {
    // clang-format off
    std::visit(Visitor {
      [&](std::shared_ptr<VisualizerWindow>& w) {
        if (w->id == hoveringId)
          hoveringNode = &node;
      },
      [&](auto&) {}},
      *node);
    // clang-format on
  });

  // Replace hovering with splitter
  if (hoveringNode)
    *hoveringNode = std::make_shared<WindowManager::Variant>(std::move(s));

  // Trigger reload
  boundsDirty.store(true);
  updateBounds();
  Config::save();
}

void addVisualizerToGroup(const std::string& group, const std::string& id) {
  auto vis = VisualizerRegistry::find(id);
  if (!vis)
    return;

  auto [it, inserted] = Config::options.visualizers.try_emplace(group);
  auto& root = it->second;
  if (!root) {
    root = std::make_shared<WindowManager::Variant>(vis);
  } else {
    // Create a new splitter that contains the existing root and the new id
    Splitter s;
    s.orientation = WindowManager::Orientation::Horizontal;
    s.ratio = 0.5f;
    s.primary = std::move(root);
    s.secondary = std::make_shared<WindowManager::Variant>(vis);
    root = std::make_shared<WindowManager::Variant>(std::move(s));
  }

  boundsDirty.store(true);
}

void removeVisualizerFromGroup(const std::string& group, const std::string& id) {
  auto it = Config::options.visualizers.find(group);
  if (it == Config::options.visualizers.end())
    return;
  removeVisualizerById(it->second, id);
  if (!it->second)
    Config::options.visualizers.erase(group);

  initialize();
}

void drawHoverHandles(std::string key, SDLWindow::State& state) {
  auto [draggingWindow, hoveringWindow, region] = getHoverState(state);
  if (region == HoverRegion::None || !hoveringWindow)
    return;

  auto hoveringBounds = hoveringWindow->bounds;
  setViewport(hoveringBounds);

  int halfW = hoveringBounds.w / 2;
  int halfH = hoveringBounds.h / 2;
  int centerX = (hoveringBounds.w - halfW) / 2;
  int centerY = (hoveringBounds.h - halfH) / 2;

  switch (region) {
  case HoverRegion::Center:
    Graphics::drawFilledRect(centerX, centerY, halfW, halfH, Theme::alpha(Theme::colors.color, 0.5f));
    break;
  case HoverRegion::Top:
    Graphics::drawFilledRect(0, 0, hoveringBounds.w, centerY, Theme::alpha(Theme::colors.color, 0.5f));
    break;
  case HoverRegion::Bottom:
    Graphics::drawFilledRect(0, centerY + halfH, hoveringBounds.w, hoveringBounds.h - (centerY + halfH),
                             Theme::alpha(Theme::colors.color, 0.5f));
    break;
  case HoverRegion::Left:
    Graphics::drawFilledRect(0, 0, centerX, hoveringBounds.h, Theme::alpha(Theme::colors.color, 0.5f));
    break;
  case HoverRegion::Right:
    Graphics::drawFilledRect(centerX + halfW, 0, hoveringBounds.w - (centerX + halfW), hoveringBounds.h,
                             Theme::alpha(Theme::colors.color, 0.5f));
    break;
  default:
    break;
  }
}

void updateBounds() {
  if (boundsDirty.load() == false)
    return;
  boundsDirty.store(false);

  for (auto& [key, state] : SDLWindow::states) {
    if (!state.root)
      continue;

    SDL_GL_MakeCurrent(state.win, state.glContext);
    updateBounds(state.root, Bounds(0, 0, state.windowSizes.first, state.windowSizes.second));
  }
}

void render() {
  for (auto& [key, state] : SDLWindow::states) {
    SDL_GL_MakeCurrent(state.win, state.glContext);

    if (!state.root)
      continue;

    walkTree(state.root, [&](Node& ptr) {
      // clang-format off
      std::visit(Visitor {
        [&](std::shared_ptr<VisualizerWindow>& w) {
          setViewport(w->bounds);
          w->render();
        },
        [&](Splitter& s) {
          setViewport(s.bounds);
          s.render();
        },
        [&](auto&) {}},
        *ptr);
      // clang-format on
    });

    drawHoverHandles(key, state);
  }
}

void cleanup() {
  for (auto& [key, state] : SDLWindow::states) {
    SDL_GL_MakeCurrent(state.win, state.glContext);

    if (!state.root)
      continue;

    walkTree(state.root, [&](Node& ptr) {
      // clang-format off
      std::visit(Visitor {
        [&](std::shared_ptr<VisualizerWindow>& w) { w->cleanup(); }, 
        [&](auto&) {}}
        , *ptr);
      // clang-format on
    });
  }
}

void handleEvent(const SDL_Event& event) {
  for (auto& [key, state] : SDLWindow::states) {

    if (!state.root)
      continue;

    walkTree(state.root, [&](Node& ptr) {
      // clang-format off
      std::visit(Visitor {
        [&](std::shared_ptr<VisualizerWindow>& w) { w->handleEvent(event); },
        [&](Splitter& s) { s.handleEvent(event); },
        [&](auto&) {}},
        *ptr);
      // clang-format on
    });
  }
}

void VisualizerWindow::transferTexture(GLuint oldTex, GLuint newTex, GLenum format, GLenum type) {
  // Calculate minimum dimensions for texture transfer
  int minWidth = std::min(bounds.w, phosphor.textureWidth);
  int minHeight = std::min(bounds.h, phosphor.textureHeight);
  std::vector<uint8_t> newData(bounds.w * bounds.h * 4, 0);

  if (glIsTexture(oldTex)) [[likely]] {
    // Read pixel data from old texture
    std::vector<uint8_t> oldData(phosphor.textureWidth * phosphor.textureHeight * 4);
    glBindTexture(GL_TEXTURE_2D, oldTex);
    glGetTexImage(GL_TEXTURE_2D, 0, format, type, oldData.data());

    // Calculate offsets for centering texture in both directions
    int srcOffsetX = std::max(0, (phosphor.textureWidth - bounds.w) / 2);
    int srcOffsetY = std::max(0, (phosphor.textureHeight - bounds.h) / 2);
    int dstOffsetX = std::max(0, (bounds.w - phosphor.textureWidth) / 2);
    int dstOffsetY = std::max(0, (bounds.h - phosphor.textureHeight) / 2);

    for (int y = 0; y < minHeight; ++y) {
      int oldRowBase = (y + srcOffsetY) * phosphor.textureWidth * 4 + srcOffsetX * 4;
      int newRowBase = (y + dstOffsetY) * bounds.w * 4 + dstOffsetX * 4;
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
  glTexImage2D(GL_TEXTURE_2D, 0, format == GL_RED_INTEGER ? GL_R32UI : GL_RGBA8, bounds.w, bounds.h, 0, format, type,
               newData.data());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void VisualizerWindow::resizeTextures() {
  bool sizeChanged = (phosphor.textureWidth != bounds.w || phosphor.textureHeight != bounds.h);

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

  if (!sizeChanged && !textureUninitialized) [[likely]]
    return;

  glFinish();
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  if (phosphor.unused) {
    GLuint newTexture = 0;
    glGenTextures(1, &newTexture);
    constexpr size_t LAST = size_t(sizeof(textures) / sizeof(textures[0])) - 1;
    transferTexture(*textures[LAST], newTexture, GL_RGBA, GL_UNSIGNED_BYTE);
    if (textures[LAST])
      glDeleteTextures(1, textures[LAST]);
    *textures[LAST] = newTexture;
  } else {
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
  }

  phosphor.textureHeight = bounds.h;
  phosphor.textureWidth = bounds.w;

  glBindTexture(GL_TEXTURE_2D, 0);
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

void VisualizerWindow::draw() {
  // Validate phosphor output texture
  if (!glIsTexture(phosphor.outputTexture)) [[unlikely]] {
    throw std::runtime_error("WindowManager::VisualizerWindow::draw(): outputTexture is not a Texture");
  }

  if (Config::options.phosphor.enabled) {
    // Render phosphor effect using output texture
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, phosphor.outputTexture);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    float w = static_cast<float>(bounds.w);
    float h = static_cast<float>(bounds.h);
    float y = 0.0f;

    float vertices[] = {0.0f, y, 0.f, 0.f, w, y, 1.f, 0.f, w, y + h, 1.f, 1.f, 0.0f, y + h, 0.f, 1.f};

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
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_TEXTURE_2D);
  }

  // Check for OpenGL errors
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) [[unlikely]]
    LOG_DEBUG(std::string("WindowManager::VisualizerWindow::draw(): OpenGL error during draw: ") + std::to_string(err));
}

void VisualizerWindow::handleEvent(const SDL_Event& event) {
  std::string group = "";
  for (auto& [_group, state] : SDLWindow::states) {
    if (event.window.windowID == state.winID) {
      group = _group;
      break;
    }
  }

  if (group.empty() || group != this->group)
    return;

  auto isHovering = [&](int x, int y, int off) -> bool {
    return x >= bounds.x + off && x < bounds.x + bounds.w - off && y >= bounds.y + off && y < bounds.y + bounds.h - off;
  };

  float mouseX, mouseY;
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
    mouseX = event.motion.x;
    mouseY = SDLWindow::states[group].windowSizes.second - event.motion.y;
    hovering = isHovering(mouseX, mouseY, 0);

    if (!dragging)
      break;

    break;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    mouseX = event.button.x;
    mouseY = SDLWindow::states[group].windowSizes.second - event.button.y;
    dragging = isHovering(mouseX, mouseY, 10);
    if (dragging)
      updateCursorForGroup(group);
    break;

  case SDL_EVENT_MOUSE_BUTTON_UP: {
    // If we were dragging, handle drop actions before clearing dragging state
    if (dragging) {
      auto& state = SDLWindow::states[group];
      auto [draggingWindow, hoveringWindow, region] = getHoverState(state);
      if (draggingWindow && hoveringWindow && draggingWindow != hoveringWindow) {
        // Operate on the config tree instead of the live SDL tree
        if (region == HoverRegion::Center) {
          swapVisualizer(state.root, draggingWindow->id, hoveringWindow->id);
        } else if (region != HoverRegion::None)
          insertVisualizer(draggingWindow->group, draggingWindow->id, hoveringWindow->id, region);
      }
    }

    dragging = false;
    updateCursorForGroup(group);
    break;
  }

  default:
    break;
  }
}

void initialize() {
  std::vector<std::string> toDestroy;
  toDestroy.reserve(SDLWindow::states.size());
  for (auto const& [key, state] : SDLWindow::states) {
    if (state.removable & Config::options.visualizers.find(key) == Config::options.visualizers.end())
      toDestroy.push_back(key);
  }
  for (auto const& key : toDestroy)
    SDLWindow::destroyWindow(key);

  for (auto& [key, node] : Config::options.visualizers) {
    if (key == "hidden")
      continue;

    auto it = SDLWindow::states.find(key);
    if (it == SDLWindow::states.end() && key != "main") {
      SDLWindow::createWindow(key, key, Config::options.window.default_width, Config::options.window.default_height);
      it = SDLWindow::states.find(key);
    }

    if (it == SDLWindow::states.end()) {
      LOG_DEBUG("Failed to find window state for group: " << key);
      continue;
    }

    if (key != "main")
      it->second.removable = true;

    walkTree(node, [&](Node& n) {
      // clang-format off
      std::visit(Visitor {
        [&](std::shared_ptr<VisualizerWindow> &w) {
          w->group = key;
          w->configure();
        },
        [&](Splitter &s) {
          s.group = key;
        }
      }, *n);
      // clang-format on
    });

    it->second.root = node;
  }

  boundsDirty.store(true);
  updateBounds();
}
} // namespace WindowManager
