#include "splitter.hpp"

#include "graphics.hpp"
#include "theme.hpp"

#include <GL/glew.h>
#include <algorithm>
#include <unordered_set>

Splitter::Splitter(VisualizerBase* leftVisualizer, VisualizerBase* rightVisualizer, Splitter* nextSplitter,
                   bool draggable)
    : leftVisualizer(leftVisualizer), rightVisualizer(rightVisualizer), nextSplitter(nextSplitter),
      draggable(draggable) {}

void Splitter::handleEvent(const SDL_Event& event, AudioData& audioData) {
  if (!draggable) {
    return;
  }

  switch (event.type) {
  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      mouseX = event.button.x;
      if (abs(mouseX - position) < 10) {
        dragging = true;
      }
    }
    break;
  case SDL_MOUSEBUTTONUP:
    if (event.button.button == SDL_BUTTON_LEFT) {
      dragging = false;
    }
    break;
  case SDL_MOUSEMOTION:
    mouseX = event.motion.x;
    hovering = abs(mouseX - position) < 10;
    if (dragging) {
      position = mouseX;
      update(audioData);
    }
    break;
  default:
    break;
  }
}

void Splitter::update(AudioData& audioData) {
  // Guard against accidental cycles in the splitter chain.  This is cheap and
  // avoids a potential infinite recursion if a programming error links
  // splitters in a loop.
  static thread_local std::unordered_set<const Splitter*> visitGuard;
  if (!visitGuard.insert(this).second) {
    // Already visiting this splitter in the current call-stack – bail out.
    return;
  }

  constexpr int MIN_LEFT_WIDTH = 80;
  constexpr int MIN_RIGHT_WIDTH = 80;
  int leftEdge = leftVisualizer ? leftVisualizer->getPosition() : 0;
  int rightEdge = nextSplitter ? nextSplitter->getPosition() : audioData.windowWidth;

  // Check if we need to enforce aspect ratio for non-draggable splitters
  if (nextSplitter && !nextSplitter->draggable && rightVisualizer && rightVisualizer->shouldEnforceAspectRatio()) {
    int requiredWidth = rightVisualizer->getRequiredWidth(audioData.windowHeight);
    if (requiredWidth > 0) {
      int desiredPosition = position + requiredWidth;
      nextSplitter->setPosition(desiredPosition);
      nextSplitter->update(audioData);
      // compare set position to current position and move self back by the difference
      // This handles cascading collisions with the 2nd next splitter.
      int difference = desiredPosition - nextSplitter->getPosition();
      position -= difference;
    }
  }

  int minPos = leftEdge + MIN_LEFT_WIDTH;
  int maxPos = rightEdge - MIN_RIGHT_WIDTH;
  if (position < minPos)
    position = minPos;
  if (position > maxPos)
    position = maxPos;

  if (leftVisualizer) {
    leftVisualizer->setPosition(leftEdge);
    leftVisualizer->setWidth(position - leftEdge);
  }
  if (rightVisualizer) {
    rightVisualizer->setPosition(position);
    rightVisualizer->setWidth(rightEdge - position);
  }
  if (nextSplitter) {
    nextSplitter->update(audioData);
  }

  // Pop from guard set on the way out so nested independent calls can run.
  visitGuard.erase(this);
}

void Splitter::draw(const AudioData& audioData) {
  Graphics::setupViewport(0, 0, audioData.windowWidth, audioData.windowHeight, audioData.windowHeight);
  const auto& splitterColor = Theme::ThemeManager::getSplitter();
  float splitterColorArray[4] = {splitterColor.r, splitterColor.g, splitterColor.b, splitterColor.a};
  Graphics::drawAntialiasedLine(static_cast<float>(position), 0, static_cast<float>(position),
                                static_cast<float>(audioData.windowHeight), splitterColorArray, 2.0f);

  if (hovering && draggable) {
    float alpha50Color[4] = {splitterColorArray[0], splitterColorArray[1], splitterColorArray[2], 0.5f};
    float squareWidth = 12.0f;
    float x0 = static_cast<float>(position) - squareWidth / 2.0f;
    float x1 = x0 + squareWidth;
    float y0 = 0.0f;
    float y1 = static_cast<float>(audioData.windowHeight);
    glColor4fv(alpha50Color);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
  }
}

int Splitter::getPosition() const { return position; }

void Splitter::setPosition(int pos) { position = pos; }

void Splitter::setWidth(int w) { width = w; }

void Splitter::setHovering(bool hover) {
  hovering = hover;
  if (!hover) {
    dragging = false;
  }
}