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

#pragma once

#include "types.hpp"

#include <memory>
#include <span>

namespace VisualizerRegistry {

extern std::vector<std::shared_ptr<WindowManager::VisualizerWindow>> visualizers;

void ensureBuiltinsRegistered();
std::weak_ptr<WindowManager::VisualizerWindow> find(const std::string& id);
bool registerVisualizer(std::shared_ptr<WindowManager::VisualizerWindow> visualizer);
void cleanup();
} // namespace VisualizerRegistry

// Builtin Visualizers
namespace Lissajous {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
namespace Oscilloscope {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
namespace SpectrumAnalyzer {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
namespace Spectrogram {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
namespace Waveform {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
namespace LUFS {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
namespace VU {
std::shared_ptr<WindowManager::VisualizerWindow> createVisualizer();
}
