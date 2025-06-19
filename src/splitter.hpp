#pragma once

#include "audio_data.hpp"
#include "visualizers.hpp"

#include <SDL2/SDL.h>

class Splitter {
public:
  Splitter(VisualizerBase* leftVisualizer, VisualizerBase* rightVisualizer, Splitter* nextSplitter,
           bool draggable = true);
  void handleEvent(const SDL_Event& event, AudioData& audioData);
  void update(AudioData& audioData);
  void draw(const AudioData& audioData);
  int getPosition() const;
  void setPosition(int pos);
  void setWidth(int width);

private:
  bool dragging = false;
  bool hovering = false;
  int mouseX = 0;
  int position = 0;
  int width = 0;
  VisualizerBase* leftVisualizer;
  VisualizerBase* rightVisualizer;
  Splitter* nextSplitter;
  const bool draggable;
};