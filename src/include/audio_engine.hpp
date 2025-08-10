/*
 * Pulse Audio Visualizer
 * Copyright (C) 2025 Beacroxx
 * Copyright (C) 2025 Contributors (see CONTRIBUTORS)
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
#include "common.hpp"

namespace AudioEngine {

/**
 * @brief Audio backend types
 */
enum Type { PULSEAUDIO, PIPEWIRE, AUTO };

/**
 * @brief Convert string to audio backend type
 * @param str String representation of backend type
 * @return Audio backend type
 */
Type toType(std::string str);

/**
 * @brief PulseAudio backend implementation
 */
namespace Pulseaudio {
#if HAVE_PULSEAUDIO
extern pa_simple* paStream;
extern bool running;
extern bool initialized;

/**
 * @brief Audio device information
 */
struct DeviceInfo {
  uint32_t index;
  std::string name;
  std::string desc;
  bool isMonitor;
};

extern std::vector<DeviceInfo> availableSources;
extern std::string defaultSink;

/**
 * @brief Callback for source information
 * @param ctx PulseAudio context
 * @param info Source information
 * @param eol End of list flag
 * @param userdata User data pointer
 */
void sourceInfoCallback(pa_context* ctx, const pa_source_info* info, int eol, void*);

/**
 * @brief Enumerate available audio sources
 */
void enumerate();

/**
 * @brief Find audio device by name
 * @param dev Device name
 * @return Device name if found
 */
std::string find(std::string dev);

/**
 * @brief Cleanup PulseAudio resources
 */
void cleanup();

/**
 * @brief Initialize PulseAudio backend
 * @return true if successful
 */
bool init();

/**
 * @brief Read audio samples from PulseAudio
 * @param buffer Output buffer
 * @param samples Number of samples to read
 * @return true if successful
 */
bool read(float* buffer, const size_t& samples);

/**
 * @brief Reconfigure PulseAudio stream
 * @return true if successful
 */
bool reconfigure();

#else
bool init();
bool read(float*, size_t);
bool reconfigure(const std::string&, uint32_t, size_t);
#endif

} // namespace Pulseaudio

/**
 * @brief PipeWire backend implementation
 */
namespace PipeWire {
#if HAVE_PIPEWIRE
extern struct pw_thread_loop* loop;
extern struct pw_context* ctx;
extern struct pw_core* core;
extern struct pw_stream* stream;
extern struct spa_hook streamListener;
extern struct pw_registry* registry;
extern struct spa_hook registryListener;

extern bool initialized;
extern bool running;

extern std::atomic<uint32_t> writtenSamples;
extern std::mutex mutex;
extern std::condition_variable cv;

/**
 * @brief Audio device information
 */
struct DeviceInfo {
  uint32_t id;
  std::string name;
  std::string desc;
  std::string mediaClass;
};

extern std::vector<DeviceInfo> availableDevices;

/**
 * @brief Registry global event callback
 * @param data User data
 * @param id Object ID
 * @param perm Permissions
 * @param type Object type
 * @param vers Version
 * @param props Properties
 */
void registryEventGlobal(void*, uint32_t id, uint32_t perm, const char* type, uint32_t vers,
                         const struct spa_dict* props);

/**
 * @brief Registry global remove event callback
 * @param data User data
 * @param id Object ID
 */
void registryEventGlobalRemove(void*, uint32_t id);

/**
 * @brief Stream state change callback
 * @param data User data
 * @param old Previous state
 * @param state New state
 * @param err Error message
 */
void onStreamStateChanged(void*, enum pw_stream_state old, enum pw_stream_state state, const char* err);

/**
 * @brief Stream process callback
 * @param data User data
 */
void onProcess(void*);

/**
 * @brief Find audio device by name
 * @param dev Device name
 * @return Pair of (device name, device ID)
 */
std::pair<std::string, uint32_t> find(std::string dev);

/**
 * @brief Cleanup PipeWire resources
 */
void cleanup();

/**
 * @brief Initialize PipeWire backend
 * @return true if successful
 */
bool init();

/**
 * @brief Read audio samples from PipeWire
 * @param buffer Output buffer
 * @param frames Number of frames to read
 * @return true if successful
 */
bool read(float*, const size_t& frames);

/**
 * @brief Reconfigure PipeWire stream
 * @return true if successful
 */
bool reconfigure();

#else
bool init();
bool read(float*, size_t);
bool reconfigure(const std::string&, uint32_t, size_t);
#endif

} // namespace PipeWire

/**
 * @brief Initialize audio engine with best available backend
 * @return Audio backend type if successful
 */
std::optional<Type> init();

/**
 * @brief Cleanup audio engine resources
 */
void cleanup();

/**
 * @brief Reconfigure audio engine
 * @return true if successful
 */
bool reconfigure();

/**
 * @brief Read audio samples from current backend
 * @param buffer Output buffer
 * @param samples Number of samples to read
 * @return true if successful
 */
bool read(float* buffer, const size_t& samples);

} // namespace AudioEngine