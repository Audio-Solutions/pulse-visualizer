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
 * @brief Walks the entire hierarchy tree and applies a callback or visitor to each node.
 * @param node The root node to start traversal from.
 * @param fn The callback or visitor for each node variant.
 */
template <typename T>
  requires visitable<T, Variant> || std::invocable<T, Node&>
void walkTree(Node& node, T&& fn) {
  if (Splitter* s = std::get_if<Splitter>(node.get())) {
    walkTree(s->primary, fn);
    walkTree(s->secondary, fn);
  }

  if constexpr (visitable<T, Variant>)
    std::visit(std::forward<T>(fn), *node);
  else
    fn(node);
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
  auto it = Config::options.visualizers.find(group);
  if (it == Config::options.visualizers.end())
    return;

  auto& node = it->second;

  bool isDragging = false;

  // clang-format off
  Visitor draggingVisitor = {
    [&](std::shared_ptr<VisualizerWindow>& w) -> void {
      if (w->dragging)
        isDragging = true;
    },
    [&](Splitter&) -> void {}
  };
  // clang-format on

  walkTree(node, draggingVisitor);

  // If any visualizer is dragging, show Move cursor and return.
  if (isDragging) {
    SDLWindow::setCursor(SDLWindow::CursorType::Move);
    return;
  }

  bool anyHorizontal = false;
  bool anyVertical = false;

  // clang-format off
  Visitor hoveringVisitor = {
    [&](Splitter& s) -> void {
      if (s.hovering) {
        if (s.orientation == Orientation::Horizontal)
          anyHorizontal = true;
        else
          anyVertical = true;
      }
    },
    [&](std::shared_ptr<VisualizerWindow>&) -> void {}
  };
  // clang-format on

  walkTree(node, hoveringVisitor);

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
  auto isThisWindow = [&event](auto const& p) { return event.window.windowID == std::get<1>(p).winID; };
  if (auto it = std::ranges::find_if(SDLWindow::states, isThisWindow);
      it == SDLWindow::states.end() || it->first != group || !movable)
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
  if (!node) {
    logWarnAt(std::source_location::current(), "Nullptr found while walking tree");
    return;
  }

  bounds.w = std::clamp(bounds.w, MIN_SIDELENGTH / 2, MAX_SIDELENGTH);
  bounds.h = std::clamp(bounds.h, MIN_SIDELENGTH / 2, MAX_SIDELENGTH);

  std::function<std::pair<Size, Constraint>(Node&, Bounds, WindowManager::Orientation)> getConstraints;
  getConstraints = [&](Node& node, Bounds bounds,
                       WindowManager::Orientation orientation) -> std::pair<Size, Constraint> {
    // clang-format off
    Visitor boundVisitor = {
      [&](std::shared_ptr<VisualizerWindow>& w) -> std::pair<Size, Constraint> {
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
      [&](Splitter& s) -> std::pair<Size, Constraint> {
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
      }
    };

    return std::visit(boundVisitor, *node);
  };

  Visitor boundVisitor =  {
    [&](std::shared_ptr<VisualizerWindow>& w) -> void {
      w->bounds = bounds;
      w->resizeTextures();
    },
    [&](Splitter& s) -> void {
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
      } else if ((primaryAxis == ConstraintType::Forced && secondaryAxis == ConstraintType::Minimum) ||
                (primaryAxis == ConstraintType::Minimum && secondaryAxis == ConstraintType::Forced)) {
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
    }
  };

  std::visit(boundVisitor, *node);
  // clang-format on
}

std::tuple<VisualizerWindow*, VisualizerWindow*, HoverRegion> getHoverState(std::string& group) {
  SDLWindow::State& state = SDLWindow::states[group];

  VisualizerWindow* draggingWindow = nullptr;
  VisualizerWindow* hoveringWindow = nullptr;

  // clang-format off
  Visitor modifierVisitor = {
    [&](std::shared_ptr<VisualizerWindow>& w) -> void {
      if (w->dragging)
        draggingWindow = w.get();
      if (w->hovering)
        hoveringWindow = w.get();
    },
    [&](Splitter&) -> void {}
  };
  // clang-format on

  walkTree(Config::options.visualizers[group], modifierVisitor);

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

  // clang-format off
  std::function<void(Node& node)> nodeCallback;
  nodeCallback = [&](Node& node) {
    Visitor nodeVisitor = {
      [&](std::shared_ptr<VisualizerWindow>& w) -> void {
        if (w->id == draggingId)
          aPtr = &node;
        if (w->id == hoveringId)
          bPtr = &node;
      },
      [&](Splitter&) -> void {}
    };

    std::visit(nodeVisitor, *node);
  };
  // clang-format on

  walkTree(root, nodeCallback);

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
  Visitor removalVisitor = {
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
    }
  };
  // clang-format on

  return std::visit(removalVisitor, *n);
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

  // clang-format off
  std::function<void(Node& node)> parentCallback;
  parentCallback = [&](Node& node) {
    Visitor parentVisitor = {
      [&](Splitter& s) {
        Visitor childVisitor = {
          [&](std::shared_ptr<VisualizerWindow> &w) -> void {
            if (w->id == draggingId)
              aParentPtr = &node;
            if (w->id == hoveringId)
              bParentPtr = &node;
          },
          [](Splitter&) -> void {}
        };

        std::visit(childVisitor, *s.primary);
        std::visit(childVisitor, *s.secondary);
      },
      [](std::shared_ptr<VisualizerWindow>&) -> void {}
    };

    std::visit(parentVisitor, *node);
  };
  // clang-format on

  walkTree(it->second, parentCallback);

  if (!aParentPtr || !bParentPtr) {
    logWarnAt(std::source_location::current(), "Could not find parent pointers during insertion");
    return;
  }

  auto dragging = VisualizerRegistry::find(draggingId).lock();
  auto hovering = VisualizerRegistry::find(hoveringId).lock();

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
    initialize();
    Config::save();
    return;
  }

  // Remove dragging node first
  if (!removeVisualizerById(it->second, draggingId))
    logWarnAt(std::source_location::current(), "Could not remove dragged node while inserting");

  Node* hoveringNode = nullptr;

  // clang-format off
  std::function<void(Node& node)> hoveringCallback;
  hoveringCallback = [&](Node& node) {
    Visitor hoveringVisitor = {
      [&](std::shared_ptr<VisualizerWindow>& w) -> void {
        if (w->id == hoveringId)
          hoveringNode = &node;
      },
      [&](Splitter&) -> void {}
    };

    std::visit(hoveringVisitor, *node);
  };
  // clang-format on

  walkTree(it->second, hoveringCallback);

  // Replace hovering with splitter
  if (hoveringNode)
    *hoveringNode = std::make_shared<WindowManager::Variant>(std::move(s));
  else
    logWarnAt(std::source_location::current(), "Could not find hovered node while inserting");

  // Trigger reload
  initialize();
  Config::save();
}

void addVisualizerToGroup(const std::string& group, const std::string& id) {
  auto vis = VisualizerRegistry::find(id).lock();
  if (!vis) {
    logWarnAt(std::source_location::current(), "Could not find visualizer with id '{}'", id);
    return;
  }

  auto [it, inserted] = Config::options.visualizers.try_emplace(group);
  auto& root = it->second;
  if (!root) {
    root = std::make_shared<WindowManager::Variant>(vis);
    logDebug("Created new window '{}' with visualizer '{}'", group, id);
  } else {
    // Create a new splitter that contains the existing root and the new id
    Splitter s;
    s.orientation = WindowManager::Orientation::Horizontal;
    s.ratio = 0.5f;
    s.primary = std::move(root);
    s.secondary = std::make_shared<WindowManager::Variant>(vis);
    root = std::make_shared<WindowManager::Variant>(std::move(s));
    logDebug("Inserted visualizer '{}' into window '{}'", id, group);
  }

  boundsDirty.store(true);
}

void removeVisualizerFromGroup(const std::string& group, const std::string& id) {
  auto it = Config::options.visualizers.find(group);
  if (it == Config::options.visualizers.end()) {
    logWarnAt(std::source_location::current(), "Could not find window with group '{}'", group);
    return;
  }

  if (removeVisualizerById(it->second, id))
    logDebug("Removed visualizer '{}' from window '{}'", id, group);
  if (!it->second) {
    Config::options.visualizers.erase(group);
    logDebug("Deleted empty window '{}'", group);
  }

  initialize();
}

void drawDragRegion(std::string key) {
  auto [draggingWindow, hoveringWindow, region] = getHoverState(key);
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
  if (!boundsDirty.exchange(false, std::memory_order_acq_rel))
    return;

  for (auto& [key, node] : Config::options.visualizers) {
    SDLWindow::State& state = SDLWindow::states[key];

    SDL_GL_MakeCurrent(state.win, state.glContext);
    updateBounds(node, Bounds(0, 0, state.windowSizes.first, state.windowSizes.second));
  }
}

void render() {
  for (auto& [key, node] : Config::options.visualizers) {
    SDLWindow::State& state = SDLWindow::states[key];
    SDL_GL_MakeCurrent(state.win, state.glContext);

    // clang-format off
    Visitor renderVisitor = {
      [&](std::shared_ptr<VisualizerWindow>& w) -> void { setViewport(w->bounds); w->render(); },
      [&](Splitter& s) -> void { setViewport(s.bounds); s.render(); },
    };
    // clang-format on

    walkTree(node, renderVisitor);

    drawDragRegion(key);
  }
}

void cleanup() {
  for (auto& [key, node] : Config::options.visualizers) {
    SDLWindow::State& state = SDLWindow::states[key];
    SDL_GL_MakeCurrent(state.win, state.glContext);

    // clang-format off
    Visitor cleanupVisitor = {
      [&](std::shared_ptr<VisualizerWindow>& w) -> void { w->cleanup(); w.reset(); }, 
      [&](Splitter&) -> void {}
    };
    // clang-format on

    walkTree(node, cleanupVisitor);
  }
}

void handleEvent(const SDL_Event& event) {
  for (auto& [key, node] : Config::options.visualizers) {
    SDLWindow::State& state = SDLWindow::states[key];

    // clang-format off
    Visitor nodeEventVisitor = {
      [&](std::shared_ptr<VisualizerWindow>& w) -> void { w->handleEvent(event); },
      [&](Splitter& s) -> void { s.handleEvent(event); }
    };
    // clang-format on

    walkTree(node, nodeEventVisitor);
  }
}

void VisualizerWindow::resizeTextures() {
  using enum WindowManager::Textures;
  constexpr size_t N = std::tuple_size_v<decltype(phosphor.textures)>;

  bool sizeChanged = (phosphor.textureWidth != bounds.w || phosphor.textureHeight != bounds.h);

  std::function<bool(GLuint texture)> initialized;
  initialized = [](GLuint texture) { return texture == 0; };
  if (!sizeChanged && std::ranges::none_of(phosphor.textures, initialized)) [[likely]]
    return;

  // Lambda to copy image data
  std::function<void(GLuint oldTex, GLuint newTex, size_t texIndex)> transferTexture;
  transferTexture = [this](GLuint oldTex, GLuint newTex, size_t texIndex) {
    using enum WindowManager::Textures;

    int minWidth = std::min(bounds.w, phosphor.textureWidth);
    int minHeight = std::min(bounds.h, phosphor.textureHeight);

    GLenum internalFormat = texIndex == OUTPUT ? GL_RGBA8 : GL_R32UI;
    GLenum format = texIndex == OUTPUT ? GL_RGBA : GL_RED_INTEGER;
    GLenum type = texIndex == OUTPUT ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT;

    // Calculate offsets for centering texture in both directions
    int srcOffsetX = std::max(0, (phosphor.textureWidth - bounds.w) / 2);
    int srcOffsetY = std::max(0, (phosphor.textureHeight - bounds.h) / 2);
    int dstOffsetX = std::max(0, (bounds.w - phosphor.textureWidth) / 2);
    int dstOffsetY = std::max(0, (bounds.h - phosphor.textureHeight) / 2);
    GLenum err = GL_NO_ERROR;

    // Create new texture
    glBindTexture(GL_TEXTURE_2D, newTex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, bounds.w, bounds.h, 0, format, type, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glClearTexImage(newTex, 0, format, type, nullptr);

    if (glIsTexture(oldTex)) [[likely]]
      glCopyImageSubData(oldTex, GL_TEXTURE_2D, 0, srcOffsetX, srcOffsetY, 0, newTex, GL_TEXTURE_2D, 0, dstOffsetX,
                         dstOffsetY, 0, minWidth, minHeight, 1);
  };

  if (phosphor.unused) {
    GLuint newTexture = 0;
    glGenTextures(1, &newTexture);
    transferTexture(phosphor.textures[OUTPUT], newTexture, OUTPUT);
    if (phosphor.textures[OUTPUT])
      glDeleteTextures(1, &phosphor.textures[OUTPUT]);
    phosphor.textures[OUTPUT] = newTexture;
    phosphor.textureHeight = bounds.h;
    phosphor.textureWidth = bounds.w;

    glBindTexture(GL_TEXTURE_2D, 0);
    return;
  }

  std::array<GLuint, N> oldTextures;
  std::ranges::copy(phosphor.textures, oldTextures.begin());

  std::array<GLuint, N> newTextures {};
  glGenTextures(N, newTextures.data());

  GLenum err = glGetError();
  if (err != GL_NO_ERROR) [[unlikely]]
    logWarnAt(std::source_location::current(), "OpenGL error during texture generation: {}", glErrorString(err));

  for (size_t i : std::views::iota((size_t)0, N))
    transferTexture(oldTextures[i], newTextures[i], i);

  glDeleteTextures(N, oldTextures.data());
  std::ranges::copy(newTextures, phosphor.textures.begin());

  phosphor.textureHeight = bounds.h;
  phosphor.textureWidth = bounds.w;

  glBindTexture(GL_TEXTURE_2D, 0);
}

void VisualizerWindow::cleanup() {
  constexpr size_t N = std::tuple_size_v<decltype(phosphor.textures)>;

  SDLWindow::selectWindow(group);
  glDeleteTextures(N, phosphor.textures.data());
  std::ranges::fill(phosphor.textures, 0);
}

void VisualizerWindow::draw() {
  using enum WindowManager::Textures;

  // Validate phosphor output texture
  if (!glIsTexture(phosphor.textures[OUTPUT])) [[unlikely]] {
    throw makeErrorAt(std::source_location::current(), "outputTexture is not a texture");
  }

  // Render phosphor effect using output texture
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, phosphor.textures[OUTPUT]);
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

  // Check for OpenGL errors
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) [[unlikely]]
    logDebug("OpenGL error during draw: {}", glErrorString(err));
}

void VisualizerWindow::handleEvent(const SDL_Event& event) {
  std::function<bool(std::pair<std::string, SDLWindow::State> kv)> isThisWindow;
  isThisWindow = [&event](auto const& kv) { return event.window.windowID == kv.second.winID; };

  if (auto it = std::ranges::find_if(SDLWindow::states, isThisWindow);
      it == SDLWindow::states.end() || it->first != group)
    return;

  std::function<bool(int x, int y, int off)> isHovering;
  isHovering = [&](int x, int y, int off) {
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
    if (dragging) {
      auto [draggingWindow, hoveringWindow, region] = getHoverState(group);
      if (draggingWindow && hoveringWindow && draggingWindow != hoveringWindow) {
        if (region == HoverRegion::Center) {
          swapVisualizer(Config::options.visualizers[group], draggingWindow->id, hoveringWindow->id);
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
  std::function<bool(std::pair<std::string, SDLWindow::State> const& kv)> destroyable;
  destroyable = [](auto const& kv) {
    return kv.second.removable && Config::options.visualizers.find(kv.first) == Config::options.visualizers.end();
  };

  std::vector<std::string> toDestroy;
  std::ranges::copy(SDLWindow::states | std::views::filter(destroyable) | std::views::keys,
                    std::back_inserter(toDestroy));

  std::ranges::for_each(toDestroy, SDLWindow::destroyWindow);

  std::function<bool(std::pair<std::string, Node> const& kv)> isNotHidden;
  isNotHidden = [](auto const& kv) { return kv.first != "hidden"; };

  for (auto& [key, node] : Config::options.visualizers | std::views::filter(isNotHidden)) {
    if (SDLWindow::states.find(key) == SDLWindow::states.end() && key != "main") {
      SDLWindow::createWindow(key, key, Config::options.window.default_width, Config::options.window.default_height);
      SDLWindow::states[key].removable = true;
    }

    // clang-format off
    Visitor initVisitor = {
      [&](std::shared_ptr<VisualizerWindow> &w) -> void { w->group = key; w->configure(); },
      [&](Splitter &s) -> void { s.group = key; }
    };
    // clang-format on

    walkTree(node, initVisitor);
  }

  boundsDirty.store(true);
  updateBounds();
}
} // namespace WindowManager
