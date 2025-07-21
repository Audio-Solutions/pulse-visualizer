#include "audio_engine.hpp"

#include "config.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// Conditional includes based on available backends
#if HAVE_PULSEAUDIO
#include <pulse/error.h>
#include <pulse/simple.h>
#endif

#if HAVE_PIPEWIRE
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#endif

namespace AudioEngine {

// Convert string to engine type
Type stringToType(const std::string& str) {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower == "pulseaudio" || lower == "pulse") {
    return Type::PULSEAUDIO;
  } else if (lower == "pipewire" || lower == "pw") {
    return Type::PIPEWIRE;
  } else if (lower == "auto") {
    return Type::AUTO;
  }

  return Type::AUTO; // Default fallback
}

// Convert engine type to string
std::string typeToString(Type type) {
  switch (type) {
  case Type::PULSEAUDIO:
    return "pulseaudio";
  case Type::PIPEWIRE:
    return "pipewire";
  case Type::AUTO:
    return "auto";
  }
  return "unknown";
}

// Get available engines
std::vector<Type> getAvailableEngines() {
  std::vector<Type> engines;

#if HAVE_PULSEAUDIO
  engines.push_back(Type::PULSEAUDIO);
#endif

#if HAVE_PIPEWIRE
  engines.push_back(Type::PIPEWIRE);
#endif

  return engines;
}

#if HAVE_PULSEAUDIO
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>

// PulseAudio implementation
class PulseAudioEngine : public AudioEngineBase {
private:
  pa_simple* pa_stream;
  pa_sample_spec sample_spec;
  std::string last_error;
  bool initialized;
  bool running;
  std::string connected_device_name;
  std::string connected_device_description;

  // Device information storage
  struct DeviceInfo {
    uint32_t index;
    std::string name;
    std::string description;
    bool is_monitor;
  };
  std::vector<DeviceInfo> available_sources;
  std::string default_sink_name;

  AudioData* audioData;

  // Callback for source enumeration
  static void source_info_callback(pa_context* context, const pa_source_info* info, int eol, void* userdata) {
    if (eol || !info || !userdata)
      return;

    auto* engine = static_cast<PulseAudioEngine*>(userdata);
    DeviceInfo device;
    device.index = info->index;
    device.name = info->name ? info->name : "";
    device.description = info->description ? info->description : "";
    device.is_monitor = (info->monitor_of_sink != PA_INVALID_INDEX);
    engine->available_sources.push_back(device);
  }

  // Callback for server info (to get default sink)
  static void server_info_callback(pa_context* context, const pa_server_info* info, void* userdata) {
    if (!info || !userdata)
      return;

    auto* engine = static_cast<PulseAudioEngine*>(userdata);
    if (info->default_sink_name) {
      engine->default_sink_name = info->default_sink_name;
    }
  }

  // Context state callback
  static void context_state_callback(pa_context* context, void* userdata) {
    // This callback is needed for pa_context_connect
  }

  void enumerateDevices() {
    available_sources.clear();
    default_sink_name.clear();

    pa_mainloop* ml = pa_mainloop_new();
    if (!ml)
      return;

    pa_context* context = pa_context_new(pa_mainloop_get_api(ml), "pulse-device-enum");
    if (!context) {
      pa_mainloop_free(ml);
      return;
    }

    pa_context_set_state_callback(context, context_state_callback, this);

    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
      pa_context_unref(context);
      pa_mainloop_free(ml);
      return;
    }

    // Wait for context to be ready
    bool done = false;
    while (!done) {
      if (pa_mainloop_iterate(ml, 1, nullptr) < 0)
        break;

      pa_context_state_t state = pa_context_get_state(context);
      if (state == PA_CONTEXT_READY) {
        // Get server info for default sink
        pa_operation* op_server = pa_context_get_server_info(context, server_info_callback, this);
        if (op_server) {
          while (pa_operation_get_state(op_server) == PA_OPERATION_RUNNING) {
            pa_mainloop_iterate(ml, 1, nullptr);
          }
          pa_operation_unref(op_server);
        }

        // Enumerate sources
        pa_operation* op_sources = pa_context_get_source_info_list(context, source_info_callback, this);
        if (op_sources) {
          while (pa_operation_get_state(op_sources) == PA_OPERATION_RUNNING) {
            pa_mainloop_iterate(ml, 1, nullptr);
          }
          pa_operation_unref(op_sources);
        }
        done = true;
      } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        done = true;
      }
    }

    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(ml);
  }

  std::string findBestDevice(const std::string& target_name) {
    if (target_name.empty()) {
      // Use default sink monitor if no specific device requested
      if (!default_sink_name.empty()) {
        std::string monitor_name = default_sink_name + ".monitor";
        // Check if this monitor source exists
        for (const auto& source : available_sources) {
          if (source.name == monitor_name) {
            connected_device_name = source.name;
            connected_device_description = source.description;
            return monitor_name;
          }
        }
      }
      return "default";
    }

    // First try exact name match
    for (const auto& source : available_sources) {
      if (source.name == target_name) {
        connected_device_name = source.name;
        connected_device_description = source.description;
        return source.name;
      }
    }

    // Try partial match
    for (const auto& source : available_sources) {
      if (source.name.find(target_name) != std::string::npos) {
        connected_device_name = source.name;
        connected_device_description = source.description;
        return source.name;
      }
    }

    // If target name has ".monitor" suffix, try finding the base sink name
    if (target_name.find(".monitor") != std::string::npos) {
      std::string base_name = target_name.substr(0, target_name.find(".monitor"));
      std::string monitor_name = base_name + ".monitor";

      for (const auto& source : available_sources) {
        if (source.name == monitor_name) {
          connected_device_name = source.name;
          connected_device_description = source.description;
          return monitor_name;
        }
      }
    }

    // Not found, warn and fall back to default sink monitor
    if (!default_sink_name.empty()) {
      std::string monitor_name = default_sink_name + ".monitor";
      for (const auto& source : available_sources) {
        if (source.name == monitor_name) {
          connected_device_name = source.name;
          connected_device_description = source.description;
          std::cout << "Warning: PulseAudio device '" << target_name
                    << "' not found, using default sink monitor: " << monitor_name << std::endl;
          return monitor_name;
        }
      }
    }

    std::cout << "Warning: PulseAudio device '" << target_name << "' not found, using system default" << std::endl;
    return "default";
  }

public:
  PulseAudioEngine() : pa_stream(nullptr), initialized(false), running(false), audioData(nullptr) {
    sample_spec.format = PA_SAMPLE_FLOAT32;
    sample_spec.rate = 44100;
    sample_spec.channels = 2;
  }

  ~PulseAudioEngine() { cleanup(audioData); }

  bool initialize(const std::string& deviceName = "", AudioData* audioData = nullptr) override {
    if (initialized) {
      cleanup(audioData);
    }
    this->audioData = audioData;

    // Enumerate available devices
    enumerateDevices();

    // Set sample spec according to config
    sample_spec.rate = Config::values().audio.sample_rate;
    sample_spec.channels = 2;

    // Derive PulseAudio buffer attributes from configurable frame count
    uint32_t buffer_frames = 0;
    try {
      buffer_frames = static_cast<uint32_t>(sample_spec.rate / Config::values().window.fps_limit);
    } catch (...) {
      buffer_frames = 512; // sensible default if the key is missing
    }
    if (buffer_frames == 0) {
      buffer_frames = 512;
    }

    const uint32_t bytes_per_frame = sample_spec.channels * sizeof(float);
    const uint32_t buffer_bytes = buffer_frames * bytes_per_frame;

    // Derive pa_buffer_attr fields from buffer_bytes.
    //  - maxlength = 4 × tlength (upper bound before PulseAudio forces an underrun)
    //  - tlength   = desired latency (buffer_bytes)
    //  - minreq    = when PulseAudio wakes us (half of tlength)
    //  - fragsize  = how much data PulseAudio transfers per read
    //  - prebuf    = leave at default (server decides)
    pa_buffer_attr attr = {
        .maxlength = buffer_bytes * 4,
        .tlength = buffer_bytes,
        .prebuf = static_cast<uint32_t>(-1),
        .minreq = buffer_bytes / 2,
        .fragsize = buffer_bytes,
    };

    int error = 0;

    std::string target_device = deviceName.empty() ? Config::values().audio.device : deviceName;
    std::string source = findBestDevice(target_device);

    pa_stream = pa_simple_new(nullptr,               // server
                              "Audio Visualizer",    // application name
                              PA_STREAM_RECORD,      // direction
                              source.c_str(),        // device
                              "Audio Visualization", // stream description
                              &sample_spec,          // sample spec
                              nullptr,               // channel map
                              &attr,                 // buffer attributes
                              &error                 // error code
    );

    if (!pa_stream) {
      last_error = "Failed to create PulseAudio stream: " + std::string(pa_strerror(error));
      return false;
    }

    // Report which device we're using
    if (!connected_device_name.empty()) {
      std::cout << "Connected to PulseAudio device: " << connected_device_name;
      if (!connected_device_description.empty() && connected_device_description != connected_device_name) {
        std::cout << " (" << connected_device_description << ")";
      }
      std::cout << std::endl;
    }

    initialized = true;
    running = true;
    return true;
  }

  bool readAudio(float* buffer, size_t frames) override {
    if (!initialized || !pa_stream) {
      last_error = "Engine not initialized";
      return false;
    }

    int error;
    size_t bytes = frames * sample_spec.channels * sizeof(float);

    if (pa_simple_read(pa_stream, buffer, bytes, &error) < 0) {
      last_error = "Failed to read from PulseAudio stream: " + std::string(pa_strerror(error));
      running = false;
      return false;
    }

    return true;
  }

  void cleanup(AudioData* audioData) override {
    if (pa_stream) {
      pa_simple_free(pa_stream);
      pa_stream = nullptr;
    }
    initialized = false;
    running = false;
  }

  Type getType() const override { return Type::PULSEAUDIO; }

  uint32_t getSampleRate() const override { return sample_spec.rate; }

  uint32_t getChannels() const override { return sample_spec.channels; }

  bool isRunning() const override { return running; }

  std::string getLastError() const override { return last_error; }

  bool needsReconfiguration(const std::string& deviceName = "", uint32_t sampleRate = 44100,
                            uint32_t bufferSize = 512) const override {
    // Check if engine parameters have changed
    if (sample_spec.rate != sampleRate) {
      return true;
    }

    // Check if device has changed
    std::string targetDevice = deviceName.empty() ? Config::values().audio.device : deviceName;
    if (connected_device_name != targetDevice && !connected_device_name.empty()) {
      // Allow device name changes that only differ by the presence or absence of ".monitor" suffix
      auto strip_monitor = [](const std::string& name) {
        if (name.size() >= 8 && name.compare(name.size() - 8, 8, ".monitor") == 0)
          return name.substr(0, name.size() - 8);
        return name;
      };
      if (strip_monitor(connected_device_name) == strip_monitor(targetDevice)) {
        return false;
      }
      return true;
    }

    // Check if buffer size has changed
    static uint32_t currentBufferSize = 0;
    if (currentBufferSize != bufferSize) {
      currentBufferSize = bufferSize;
      return true;
    }

    return false;
  }

  std::string getCurrentDevice() const override { return connected_device_name; }
};
#endif

#if HAVE_PIPEWIRE
// PipeWire implementation
class PipeWireEngine : public AudioEngineBase {
private:
  struct pw_thread_loop* loop;
  struct pw_context* context;
  struct pw_core* core;
  struct pw_stream* stream;
  struct spa_hook stream_listener;

  std::string last_error;
  bool initialized;
  bool running;

  uint32_t sample_rate;

  AudioData* audioData;

  // Note: pw_thread_loop handles threading internally

  // Registry for device enumeration
  struct pw_registry* registry;
  struct spa_hook registry_listener;

  // Device information storage
  struct DeviceInfo {
    uint32_t id;
    std::string name;
    std::string description;
    std::string media_class;
  };
  std::vector<DeviceInfo> available_devices;
  std::string connected_device_name;
  std::string connected_device_description;

  static void on_process(void* userdata) {
    auto* engine = static_cast<PipeWireEngine*>(userdata);
    engine->processAudio();
  }

  static void registry_event_global(void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version,
                                    const struct spa_dict* props) {
    auto* engine = static_cast<PipeWireEngine*>(data);

    if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
      const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
      const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
      const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);

      if (media_class && (strcmp(media_class, "Audio/Source") == 0 || strcmp(media_class, "Audio/Sink") == 0)) {
        // Store device information
        DeviceInfo device;
        device.id = id;
        device.name = name ? name : "";
        device.description = desc ? desc : "";
        device.media_class = media_class;
        engine->available_devices.push_back(device);
      }
    }
  }

  static void registry_event_global_remove(void* data, uint32_t id) {
    // Device removed
  }

  static void on_stream_state_changed(void* data, enum pw_stream_state old, enum pw_stream_state state,
                                      const char* error) {
    auto* engine = static_cast<PipeWireEngine*>(data);

    switch (state) {
    case PW_STREAM_STATE_ERROR:
      engine->last_error = "PipeWire stream error: " + std::string(error ? error : "unknown");
      std::cerr << "PipeWire stream error: " << (error ? error : "unknown") << std::endl;
      engine->running = false;
      break;
    case PW_STREAM_STATE_STREAMING:
      engine->running = true;
      break;
    case PW_STREAM_STATE_UNCONNECTED:
      engine->running = false;
      break;
    case PW_STREAM_STATE_PAUSED:
      // Activate the stream when it reaches paused state
      pw_stream_set_active(engine->stream, true);
      break;
    default:
      break;
    }
  }

  void processAudio() {
    struct pw_buffer* b;
    struct spa_buffer* buf;

    if ((b = pw_stream_dequeue_buffer(stream)) == nullptr) {
      return;
    }

    buf = b->buffer;

    if (buf->datas[0].data == nullptr || buf->datas[0].chunk->size == 0) {
      pw_stream_queue_buffer(stream, b);
      return;
    }

    float* samples = static_cast<float*>(buf->datas[0].data);
    uint32_t n_samples = buf->datas[0].chunk->size / sizeof(float);

    // Ensure we don't process more samples than expected
    if (n_samples > buf->datas[0].maxsize / sizeof(float)) {
      n_samples = buf->datas[0].maxsize / sizeof(float);
    }

    // Copy to AudioData
    if (audioData) {
      size_t bufferSize = audioData->bufferSize;
      for (uint32_t i = 0; i + 1 < n_samples; i += 2) {
        float sampleLeft = samples[i];
        float sampleRight = samples[i + 1];
        audioData->bufferMid[audioData->writePos] = (sampleLeft + sampleRight) / 2.0f;
        audioData->bufferSide[audioData->writePos] = (sampleLeft - sampleRight) / 2.0f;
        audioData->writePos = (audioData->writePos + 1) % bufferSize;
      }
    }

    pw_stream_queue_buffer(stream, b);
  }

  uint32_t findDeviceByName(const std::string& target_name) {
    if (target_name.empty()) {
      return PW_ID_ANY;
    }

    // First try exact name match
    for (const auto& device : available_devices) {
      if (device.name == target_name) {
        connected_device_name = device.name;
        connected_device_description = device.description;
        return device.id;
      }
    }

    // Try partial match if exact match not found
    for (const auto& device : available_devices) {
      if (device.name.find(target_name) != std::string::npos) {
        connected_device_name = device.name;
        connected_device_description = device.description;
        return device.id;
      }
    }

    // If target name has ".monitor" suffix, try finding the base sink name
    if (target_name.find(".monitor") != std::string::npos) {
      std::string base_name = target_name.substr(0, target_name.find(".monitor"));

      for (const auto& device : available_devices) {
        if (device.name == base_name && device.media_class == "Audio/Sink") {
          connected_device_name = device.name;
          connected_device_description = device.description;
          return device.id;
        }
      }
    }

    return PW_ID_ANY;
  }

public:
  PipeWireEngine()
      : loop(nullptr), context(nullptr), core(nullptr), stream(nullptr), initialized(false), running(false),
        sample_rate(44100), registry(nullptr), audioData(nullptr) {}

  ~PipeWireEngine() { cleanup(audioData); }

  bool initialize(const std::string& deviceName = "", AudioData* audioData = nullptr) override {
    if (initialized) {
      cleanup(audioData);
    }
    this->audioData = audioData;

    pw_init(nullptr, nullptr);

    loop = pw_thread_loop_new("pulse-visualizer", nullptr);
    if (!loop) {
      last_error = "Failed to create PipeWire thread loop";
      return false;
    }

    context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    if (!context) {
      last_error = "Failed to create PipeWire context";
      return false;
    }

    core = pw_context_connect(context, nullptr, 0);
    if (!core) {
      last_error = "Failed to connect to PipeWire";
      return false;
    }

    // Start the thread loop.  We need it running for both device enumeration and stream callbacks.
    if (pw_thread_loop_start(loop) < 0) {
      last_error = "Failed to start PipeWire thread loop";
      return false;
    }

    // Perform device enumeration only if the caller specified an explicit device name.
    if (!deviceName.empty()) {
      available_devices.clear();

      static const struct pw_registry_events registry_events = {
          .version = PW_VERSION_REGISTRY_EVENTS,
          .global = registry_event_global,
          .global_remove = registry_event_global_remove,
      };

      pw_thread_loop_lock(loop);

      registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
      pw_registry_add_listener(registry, &registry_listener, &registry_events, this);

      pw_thread_loop_unlock(loop);

      // Wait (briefly) until we have enumerated at least one device or 300 ms pass.
      constexpr auto ENUM_WAIT_TIMEOUT = std::chrono::milliseconds(300);
      auto start_time = std::chrono::steady_clock::now();
      while (available_devices.empty() && std::chrono::steady_clock::now() - start_time < ENUM_WAIT_TIMEOUT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    sample_rate = Config::values().audio.sample_rate;

    // Create stream
    const struct spa_pod* params[1];
    struct spa_audio_info_raw info = {};
    uint8_t buffer[1024];
    struct spa_pod_builder b;

    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = 2;
    info.rate = sample_rate;

    spa_pod_builder_init(&b, buffer, sizeof(buffer));
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    static const struct pw_stream_events stream_events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = on_stream_state_changed,
        .process = on_process,
    };

    pw_thread_loop_lock(loop);

    stream =
        pw_stream_new_simple(pw_thread_loop_get_loop(loop), "Audio Visualizer",
                             pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                               PW_KEY_MEDIA_ROLE, "Music", PW_KEY_STREAM_CAPTURE_SINK, "true", nullptr),
                             &stream_events, this);

    if (!stream) {
      last_error = "Failed to create PipeWire stream";
      std::cerr << "Failed to create PipeWire stream" << std::endl;
      return false;
    }

    // Try to find device by name from config
    std::string target_device_name = deviceName;
    uint32_t target_id = findDeviceByName(target_device_name);

    if (target_id == PW_ID_ANY && !target_device_name.empty()) {
      std::cout << "Warning: Configured PipeWire device '" << target_device_name << "' not found, using autoconnect"
                << std::endl;
    }

    if (pw_stream_connect(stream, PW_DIRECTION_INPUT, target_id,
                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                       PW_STREAM_FLAG_DONT_RECONNECT),
                          params, 1) < 0) {
      pw_thread_loop_unlock(loop);
      last_error = "Failed to connect PipeWire stream";
      std::cerr << "Failed to connect PipeWire stream" << std::endl;
      return false;
    }

    pw_thread_loop_unlock(loop);

    // Wait until the stream reaches the STREAMING state (or timeout ~500 ms)
    constexpr auto STREAM_WAIT_TIMEOUT = std::chrono::milliseconds(500);
    auto wait_start = std::chrono::steady_clock::now();
    while (!running && std::chrono::steady_clock::now() - wait_start < STREAM_WAIT_TIMEOUT) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Report which device we're using
    if (!connected_device_name.empty()) {
      std::cout << "Connected to PipeWire device: " << connected_device_name;
      if (!connected_device_description.empty() && connected_device_description != connected_device_name) {
        std::cout << " (" << connected_device_description << ")";
      }
      std::cout << std::endl;
    } else if (target_id == PW_ID_ANY) {
      std::cout << "Connected to PipeWire (autoconnect)" << std::endl;
    }

    initialized = true;
    return true;
  }

  bool readAudio(float* buffer, size_t frames) override {
    if (!initialized || !stream) {
      last_error = "Engine not initialized";
      return false;
    }

    static size_t lastWritePos = 0;
    // wait for Frames writes to be available
    while ((audioData->writePos - lastWritePos + audioData->bufferSize) % audioData->bufferSize < frames) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    lastWritePos = audioData->writePos;

    return true;
  }

  void cleanup(AudioData* audioData) override {
    if (loop) {
      pw_thread_loop_lock(loop);

      if (registry) {
        spa_hook_remove(&registry_listener);
        pw_proxy_destroy((struct pw_proxy*)registry);
        registry = nullptr;
      }

      if (stream) {
        pw_stream_destroy(stream);
        stream = nullptr;
      }

      pw_thread_loop_unlock(loop);
      pw_thread_loop_stop(loop);
    }

    if (core) {
      pw_core_disconnect(core);
      core = nullptr;
    }

    if (context) {
      pw_context_destroy(context);
      context = nullptr;
    }

    if (loop) {
      pw_thread_loop_destroy(loop);
      loop = nullptr;
    }

    pw_deinit();

    initialized = false;
    running = false;
  }

  Type getType() const override { return Type::PIPEWIRE; }

  uint32_t getSampleRate() const override { return sample_rate; }

  uint32_t getChannels() const override { return 2; }

  bool isRunning() const override { return running; }

  std::string getLastError() const override { return last_error; }

  bool needsReconfiguration(const std::string& deviceName = "", uint32_t sampleRate = 44100,
                            uint32_t bufferSize = 512) const override {
    // Check if engine parameters have changed
    if (sample_rate != sampleRate) {
      return true;
    }

    // Check if device has changed
    std::string targetDevice = deviceName.empty() ? Config::values().audio.device : deviceName;
    if (connected_device_name != targetDevice && !connected_device_name.empty()) {
      return true;
    }

    // Check if buffer size has changed
    static uint32_t currentBufferSize = 0;
    if (currentBufferSize != bufferSize) {
      currentBufferSize = bufferSize;
      return true;
    }

    return false;
  }

  std::string getCurrentDevice() const override { return connected_device_name; }
};
#endif

// Factory function to create audio engines
std::unique_ptr<AudioEngineBase> createEngine(Type type) {
  switch (type) {
#if HAVE_PULSEAUDIO
  case Type::PULSEAUDIO:
    return std::make_unique<PulseAudioEngine>();
#endif

#if HAVE_PIPEWIRE
  case Type::PIPEWIRE:
    return std::make_unique<PipeWireEngine>();
#endif

  case Type::AUTO:
    return createBestAvailableEngine();

  default:
    return nullptr;
  }
}

// Utility function to create best available engine
std::unique_ptr<AudioEngineBase> createBestAvailableEngine(Type preferred) {
  auto available = getAvailableEngines();

  if (available.empty()) {
    return nullptr;
  }

  // If a specific engine is preferred and available, use it
  if (preferred != Type::AUTO) {
    auto it = std::find(available.begin(), available.end(), preferred);
    if (it != available.end()) {
      return createEngine(preferred);
    } else {
      std::cout << "Warning: Preferred audio engine '" << typeToString(preferred)
                << "' not available, using available engine" << std::endl;
    }
  }

  // Priority order: PipeWire first (more modern), then PulseAudio
#if HAVE_PIPEWIRE
  if (std::find(available.begin(), available.end(), Type::PIPEWIRE) != available.end()) {
    return createEngine(Type::PIPEWIRE);
  }
#endif

#if HAVE_PULSEAUDIO
  if (std::find(available.begin(), available.end(), Type::PULSEAUDIO) != available.end()) {
    return createEngine(Type::PULSEAUDIO);
  }
#endif

  return nullptr;
}

} // namespace AudioEngine