#pragma once
#include "common.hpp"

namespace AudioEngine {
enum Type { PULSEAUDIO, PIPEWIRE, AUTO };

Type toType(std::string str);

namespace Pulseaudio {
#if HAVE_PULSEAUDIO
extern pa_simple* paStream;
extern bool running;
extern bool initialized;

struct DeviceInfo {
  uint32_t index;
  std::string name;
  std::string desc;
  bool isMonitor;
};

extern std::vector<DeviceInfo> availableSources;
extern std::string defaultSink;

void sourceInfoCallback(pa_context* ctx, const pa_source_info* info, int eol, void*);
void enumerate();
std::string find(std::string dev);
void cleanup();
bool init();
bool read(float* buffer, const size_t& samples);
bool reconfigure();
#else
bool init();
bool read(float*, size_t);
bool reconfigure(const std::string&, uint32_t, size_t);
#endif
} // namespace Pulseaudio

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

struct DeviceInfo {
  uint32_t id;
  std::string name;
  std::string desc;
  std::string mediaClass;
};

extern std::vector<DeviceInfo> availableDevices;

void registryEventGlobal(void*, uint32_t id, uint32_t perm, const char* type, uint32_t vers,
                         const struct spa_dict* props);
void registryEventGlobalRemove(void*, uint32_t id);
void onStreamStateChanged(void*, enum pw_stream_state old, enum pw_stream_state state, const char* err);
void onProcess(void*);
std::pair<std::string, uint32_t> find(std::string dev);
void cleanup();
bool init();
bool read(float*, const size_t& frames);
bool reconfigure();
#else
bool init();
bool read(float*, size_t);
bool reconfigure(const std::string&, uint32_t, size_t);
#endif
} // namespace PipeWire

std::optional<Type> init();
void cleanup();
bool reconfigure();
bool read(float* buffer, const size_t& samples);

} // namespace AudioEngine