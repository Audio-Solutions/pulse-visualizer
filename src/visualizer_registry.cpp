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

#include "include/visualizer_registry.hpp"

#include "include/config.hpp"
namespace VisualizerRegistry {

std::vector<std::shared_ptr<WindowManager::VisualizerWindow>> visualizers;

void ensureBuiltinsRegistered() {
  static std::once_flag once;
  std::call_once(once, [] {
    visualizers.push_back(SpectrumAnalyzer::createVisualizer());
    visualizers.push_back(Lissajous::createVisualizer());
    visualizers.push_back(Oscilloscope::createVisualizer());
    visualizers.push_back(Spectrogram::createVisualizer());
    visualizers.push_back(Waveform::createVisualizer());
    visualizers.push_back(LUFS::createVisualizer());
    visualizers.push_back(VU::createVisualizer());
  });
}

std::shared_ptr<WindowManager::VisualizerWindow> find(const std::string& id) {
  ensureBuiltinsRegistered();
  auto it = std::find_if(visualizers.begin(), visualizers.end(),
                         [&](const auto& visualizer) { return visualizer->id == id; });
  return it == visualizers.end() ? nullptr : *it;
}

bool registerVisualizer(std::shared_ptr<WindowManager::VisualizerWindow> visualizer) {
  if (!visualizer || visualizer->id.empty())
    return false;
  visualizers.push_back(visualizer);
  return true;
}

std::span<const std::shared_ptr<WindowManager::VisualizerWindow>> list() {
  ensureBuiltinsRegistered();
  return visualizers;
}

} // namespace VisualizerRegistry
