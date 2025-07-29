#include "include/audio_engine.hpp"

#include "include/config.hpp"
#include "include/dsp.hpp"

namespace AudioEngine {

Type toType(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);

  if (str == "pulseaudio" || str == "pulse" || str == "pa")
    return Type::PULSEAUDIO;
  else if (str == "pipewire" || str == "pw")
    return Type::PIPEWIRE;
  return Type::AUTO;
}

namespace Pulseaudio {
#if HAVE_PULSEAUDIO

// PulseAudio stream and state
pa_simple* paStream;
bool running = false;
bool initialized = false;

// Available audio sources and default sink
std::vector<DeviceInfo> availableSources;
std::string defaultSink;

void sourceInfoCallback(pa_context* ctx, const pa_source_info* info, int eol, void*) {
  if (eol || !info)
    return;

  // Extract device information from PulseAudio
  DeviceInfo dev;
  dev.index = info->index;
  dev.name = info->name;
  dev.desc = info->description;
  dev.isMonitor = info->monitor_of_sink != PA_INVALID_INDEX;
  availableSources.push_back(dev);
}

void enumerate() {
  availableSources.clear();
  defaultSink.clear();

  // Create PulseAudio mainloop and context
  pa_mainloop* ml = pa_mainloop_new();
  if (!ml)
    return;

  pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "pulse-device-enum");
  if (!ctx) {
    pa_mainloop_free(ml);
    return;
  }

  pa_context_set_state_callback(ctx, [](pa_context*, void*) {}, nullptr);

  if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return;
  }

  // Wait for context to be ready and enumerate devices
  bool done = false;
  do {
    if (pa_mainloop_iterate(ml, 1, nullptr) < 0)
      ;
    break;

    pa_context_state_t state = pa_context_get_state(ctx);
    if (state == PA_CONTEXT_READY) {
      // Get server info to find default sink
      pa_operation* opServer = pa_context_get_server_info(
          ctx,
          [](pa_context* ctx, const pa_server_info* info, void*) {
            if (!info)
              return;

            if (info->default_sink_name)
              defaultSink = info->default_sink_name;
          },
          nullptr);

      if (opServer) {
        while (pa_operation_get_state(opServer) == PA_OPERATION_RUNNING)
          pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(opServer);
      }

      // Get source list
      pa_operation* opSources = pa_context_get_source_info_list(ctx, sourceInfoCallback, nullptr);
      if (opSources) {
        while (pa_operation_get_state(opSources) == PA_OPERATION_RUNNING)
          pa_mainloop_iterate(ml, 1, nullptr);
        pa_operation_unref(opSources);
      }
      done = true;
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
      done = true;
    }
  } while (!done);

  pa_context_disconnect(ctx);
  pa_context_unref(ctx);
  pa_mainloop_free(ml);
}

std::string find(std::string dev) {
  if (dev.empty()) {
    if (defaultSink.empty())
      return "default";

    dev = defaultSink + ".monitor";
  }

  // Search for matching device
  for (const auto& source : availableSources) {
    if (source.name.find(dev) != std::string::npos ||
        source.name.find(dev.substr(0, dev.find(".monitor"))) != std::string::npos) {
      return source.name;
    }
  }

  std::cout << "Warning: PulseAudio device '" << dev << "' not found. using system default" << std::endl;
  return "default";
}

void cleanup() {
  if (paStream) {
    pa_simple_free(paStream);
    paStream = nullptr;
  }
  initialized = false;
  running = false;
}

bool init() {
  if (initialized)
    cleanup();

  enumerate();

  // Configure audio format
  pa_sample_spec sampleSpec;
  sampleSpec.rate = Config::options.audio.sample_rate;
  sampleSpec.channels = 2;

  // Calculate buffer size based on FPS
  uint32_t samples = 0;
  try {
    samples = static_cast<uint32_t>(sampleSpec.rate / Config::options.window.fps_limit);
  } catch (...) {
    samples = 512;
  }

  const uint32_t bytesPerSample = sampleSpec.channels * sizeof(float);
  const uint32_t bufferSize = samples * bytesPerSample;

  // Configure buffer attributes
  pa_buffer_attr attr = {.maxlength = bufferSize * 4,
                         .tlength = bufferSize,
                         .prebuf = static_cast<uint32_t>(-1),
                         .minreq = bufferSize / 2,
                         .fragsize = bufferSize};

  std::string dev = find(Config::options.audio.device);

  // Create PulseAudio stream
  int err = 0;
  paStream = pa_simple_new(nullptr, "Pulse Visualizer", PA_STREAM_RECORD, dev.c_str(), "Pulse Audio Visualizer",
                           &sampleSpec, nullptr, &attr, &err);

  if (!paStream) {
    std::cerr << "Failed to create PulseAudio stream: " << pa_strerror(err) << std::endl;
    return false;
  }

  initialized = true;
  running = true;

  if (dev.empty())
    return true;

  std::cout << "Connected to PulseAudio device '" << dev << "'" << std::endl;
  return true;
}

bool read(float* buffer, const size_t& samples) {
  if (!initialized || !paStream)
    return true;

  // Read audio data from PulseAudio
  int err;
  if (pa_simple_read(paStream, buffer, samples * 2 * sizeof(float), &err) < 0) {
    std::cout << "Failed to read from pulseAudio stream: " << pa_strerror(err) << std::endl;
    running = false;
    return false;
  }

  return true;
}

bool reconfigure() {
  static std::string lastDevice = "";
  static uint32_t lastSampleRate = 0;
  static size_t lastFPS = 0;

  // Check if reconfiguration is needed
  if (lastDevice == Config::options.audio.device && lastFPS == Config::options.window.fps_limit &&
      lastSampleRate == Config::options.audio.sample_rate) [[likely]]
    return false;

  lastSampleRate = Config::options.audio.sample_rate;
  lastFPS = Config::options.window.fps_limit;
  lastDevice = Config::options.audio.device;

  init();

  return true;
}
#else
bool init() { return false; }
bool read(float*, size_t) { return false; }
bool reconfigure(const std::string&, uint32_t, size_t) { return false; }
#endif
} // namespace Pulseaudio

namespace PipeWire {
#if HAVE_PIPEWIRE

// PipeWire handles and state
struct pw_thread_loop* loop;
struct pw_context* ctx;
struct pw_core* core;
struct pw_stream* stream;
struct spa_hook streamListener;
struct pw_registry* registry;
struct spa_hook registryListener;

bool initialized = false;
bool running = false;

// Thread synchronization
std::atomic<uint32_t> writtenSamples;
std::mutex mutex;
std::condition_variable cv;

std::vector<DeviceInfo> availableDevices;

void registryEventGlobal(void*, uint32_t id, uint32_t perm, const char* type, uint32_t vers,
                         const struct spa_dict* props) {
  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char* desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);

    // Add audio sources and sinks to device list
    if (mediaClass && (strcmp(mediaClass, "Audio/Source") == 0 || strcmp(mediaClass, "Audio/Sink") == 0)) {
      DeviceInfo dev;
      dev.id = id;
      dev.name = name ? name : "";
      dev.desc = desc ? desc : "";
      dev.mediaClass = mediaClass;
      availableDevices.push_back(dev);
    }
  }
}

void registryEventGlobalRemove(void*, uint32_t id) {}

void onStreamStateChanged(void*, enum pw_stream_state old, enum pw_stream_state state, const char* err) {
  switch (state) {
  case PW_STREAM_STATE_ERROR:
    std::cerr << "PipeWire stream error: " << (err ? err : "unknown") << std::endl;
  case PW_STREAM_STATE_UNCONNECTED:
    running = false;
    break;
  case PW_STREAM_STATE_STREAMING:
    running = true;
    break;
  case PW_STREAM_STATE_PAUSED:
    pw_stream_set_active(stream, true);
  default:
    break;
  }
}

#include <immintrin.h>

void onProcess(void*) {
  struct pw_buffer* b;
  struct spa_buffer* buf;

  if ((b = pw_stream_dequeue_buffer(stream)) == nullptr)
    return;

  buf = b->buffer;

  if (buf->datas[0].data == nullptr || buf->datas[0].chunk->size == 0) {
    pw_stream_queue_buffer(stream, b);
    return;
  }

  // Process audio data
  float* samples = static_cast<float*>(buf->datas[0].data);
  size_t n_samples = buf->datas[0].chunk->size / sizeof(float);

  if (n_samples > buf->datas[0].maxsize / sizeof(float))
    n_samples = buf->datas[0].maxsize / sizeof(float);

  float gain = powf(10.0f, Config::options.audio.gain_db / 20.0f);

  // Process samples with SIMD optimization if available
#ifdef HAVE_AVX2
  size_t simd_samples = n_samples & ~7;
  __m256i left_idx = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
  __m256i right_idx = _mm256_setr_epi32(1, 3, 5, 7, 0, 0, 0, 0);

  for (size_t i = 0; i < simd_samples; i += 8) {
    __m256 data = _mm256_loadu_ps(&samples[i]);

    __m256 left = _mm256_permutevar8x32_ps(data, left_idx);
    __m256 right = _mm256_permutevar8x32_ps(data, right_idx);

    __m256 vgain = _mm256_set1_ps(gain);
    left = _mm256_mul_ps(left, vgain);
    right = _mm256_mul_ps(right, vgain);

    __m256 mid = _mm256_mul_ps(_mm256_add_ps(left, right), _mm256_set1_ps(0.5f));
    __m256 side = _mm256_mul_ps(_mm256_sub_ps(left, right), _mm256_set1_ps(0.5f));

    _mm_storeu_ps(&DSP::bufferMid[DSP::writePos], _mm256_castps256_ps128(mid));
    _mm_storeu_ps(&DSP::bufferSide[DSP::writePos], _mm256_castps256_ps128(side));
    DSP::writePos = (DSP::writePos + 4) % DSP::bufferSize;
  }

  for (size_t i = simd_samples; i + 1 < n_samples; i += 2) {
#else
  for (size_t i = 0; i + 1 < n_samples; i += 2) {
#endif
    float left = samples[i] * gain;
    float right = samples[i + 1] * gain;
    DSP::bufferMid[DSP::writePos] = (left + right) / 2.0f;
    DSP::bufferSide[DSP::writePos] = (left - right) / 2.0f;
    DSP::writePos = (DSP::writePos + 1) % DSP::bufferSize;
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    writtenSamples += n_samples;
  }
  cv.notify_one();

  pw_stream_queue_buffer(stream, b);
}

std::pair<std::string, uint32_t> find(std::string dev) {
  if (dev.empty()) {
    return std::make_pair("default", PW_ID_ANY);
  }

  // Search for matching device
  for (const auto& device : availableDevices) {
    if (device.name.find(dev) != std::string::npos ||
        device.name.find(dev.substr(0, dev.find(".monitor"))) != std::string::npos) {
      return std::make_pair(device.name, device.id);
    }
  }

  std::cout << "Warning: PipeWire device '" << dev << "' not found. using system default" << std::endl;
  return std::make_pair("default", PW_ID_ANY);
}

void cleanup() {
  if (loop) {
    pw_thread_loop_lock(loop);

    if (registry) {
      spa_hook_remove(&registryListener);
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

  if (ctx) {
    pw_context_destroy(ctx);
    ctx = nullptr;
  }

  if (loop) {
    pw_thread_loop_destroy(loop);
    loop = nullptr;
  }

  pw_deinit();

  initialized = false;
  running = false;
}

bool init() {
  if (initialized)
    cleanup();

  pw_init(nullptr, nullptr);

  // Create PipeWire thread loop
  loop = pw_thread_loop_new("pulse-visualizer", nullptr);
  if (!loop) {
    std::cerr << "Failed to create PipeWire thread loop" << std::endl;
    return false;
  }

  // Create PipeWire context
  ctx = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
  if (!ctx) {
    std::cerr << "Failed to create PipeWire context" << std::endl;
    return false;
  }

  // Connect to PipeWire core
  core = pw_context_connect(ctx, nullptr, 0);
  if (!core) {
    std::cerr << "Failed to connect to PipeWire" << std::endl;
    return false;
  }

  if (pw_thread_loop_start(loop) < 0) {
    std::cerr << "Failed to start PipeWire thread loop" << std::endl;
    return false;
  }

  // Setup registry to enumerate devices
  availableDevices.clear();
  static const struct pw_registry_events registryEvents = {
      .version = PW_VERSION_REGISTRY_EVENTS, .global = registryEventGlobal, .global_remove = registryEventGlobalRemove};

  pw_thread_loop_lock(loop);
  registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(registry, &registryListener, &registryEvents, nullptr);
  pw_thread_loop_unlock(loop);

  // Wait for device enumeration
  constexpr auto TIMEOUT = std::chrono::milliseconds(300);
  auto start = std::chrono::steady_clock::now();
  while (availableDevices.empty() && std::chrono::steady_clock::now() - start < TIMEOUT)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Configure audio format
  const struct spa_pod* params[1];
  struct spa_audio_info_raw info = {};
  uint8_t buffer[1024];
  struct spa_pod_builder b;

  info.format = SPA_AUDIO_FORMAT_F32;
  info.channels = 2;
  info.rate = Config::options.audio.sample_rate;

  spa_pod_builder_init(&b, buffer, sizeof(buffer));
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  // Setup stream events
  static const struct pw_stream_events streamEvents = {
      .version = PW_VERSION_STREAM_EVENTS, .state_changed = onStreamStateChanged, .process = onProcess};

  pw_thread_loop_lock(loop);

  // Calculate buffer size
  size_t targetSamples = info.rate / Config::options.window.fps_limit;
  size_t samples = 1;
  while (samples < targetSamples)
    samples *= 2;

  samples = std::max(samples, static_cast<size_t>(128));

  std::string nodeLatency = std::to_string(samples) + "/" + std::to_string(info.rate);

  // Create PipeWire stream
  stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), "Pulse Visualizer",
                                pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                  PW_KEY_MEDIA_ROLE, "Music", PW_KEY_STREAM_CAPTURE_SINK, "true",
                                                  PW_KEY_NODE_LATENCY, nodeLatency.c_str(), nullptr),
                                &streamEvents, nullptr);

  if (!stream) {
    std::cerr << "Failed to create PipeWire stream" << std::endl;
    return false;
  }

  // Connect stream to device
  auto [dev, devId] = find(Config::options.audio.device);
  if (pw_stream_connect(stream, PW_DIRECTION_INPUT, devId,
                        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                     PW_STREAM_FLAG_DONT_RECONNECT),
                        params, 1) < 0) {
    pw_thread_loop_unlock(loop);
    std::cerr << "Failed to connect PipeWire stream" << std::endl;
    return false;
  }

  pw_thread_loop_unlock(loop);

  // Wait for stream to be ready
  start = std::chrono::steady_clock::now();
  while (!running && std::chrono::steady_clock::now() - start < TIMEOUT)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::cout << "Connected to PipeWire device: '" << dev << "'" << std::endl;

  initialized = true;
  return true;
}

bool read(float*, const size_t& frames) {
  if (!initialized || !stream)
    return true;

  // Wait for enough samples to be available
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return writtenSamples > frames; });
  writtenSamples = 0;

  return true;
}

bool reconfigure() {
  static std::string lastDevice = "";
  static uint32_t lastSampleRate = 0;
  static size_t lastFPS = 0;

  // Check if reconfiguration is needed
  if (lastDevice == Config::options.audio.device && lastFPS == Config::options.window.fps_limit &&
      lastSampleRate == Config::options.audio.sample_rate) [[likely]]
    return false;

  lastSampleRate = Config::options.audio.sample_rate;
  lastFPS = Config::options.window.fps_limit;
  lastDevice = Config::options.audio.device;

  init();

  return true;
}
#else
bool init() { return false; }
bool read(float*, size_t) { return false; }
bool reconfigure(const std::string&, uint32_t, size_t) { return false; }
#endif
} // namespace PipeWire

std::optional<Type> init() {
  Type engine = toType(Config::options.audio.engine);

  // Check for available backends
#if !HAVE_PULSEAUDIO
  if (engine == Type::PULSEAUDIO) {
    std::cerr << "Pulse not compiled with Pulseaudio support. Using auto" << std::endl;
    engine = Type::AUTO;
  }
#endif

#if !HAVE_PIPEWIRE
  if (engine == Type::PIPEWIRE) {
    std::cerr << "Pulse not compiled with PipeWire support. Using auto" << std::endl;
    engine = Type::AUTO;
  }
#endif

  // Initialize selected backend
  switch (engine) {
  case Type::AUTO:
#if HAVE_PIPEWIRE
    if (PipeWire::init()) {
#if HAVE_PULSEAUDIO
      Pulseaudio::cleanup();
#endif
      return Type::PIPEWIRE;
    }
#endif
#if HAVE_PULSEAUDIO
    if (Pulseaudio::init()) {
#if HAVE_PIPEWIRE
      PipeWire::cleanup();
#endif
      return Type::PULSEAUDIO;
    }
#endif
    break;
  case Type::PULSEAUDIO:
    if (Pulseaudio::init()) {
#if HAVE_PIPEWIRE
      PipeWire::cleanup();
#endif
      return Type::PULSEAUDIO;
    } else
      return std::nullopt;
    break;
  case Type::PIPEWIRE:
    if (PipeWire::init()) {
#if HAVE_PULSEAUDIO
      Pulseaudio::cleanup();
#endif
      return Type::PIPEWIRE;
    } else
      return std::nullopt;
    break;
  }

  return std::nullopt;
}

void cleanup() {
#if HAVE_PULSEAUDIO
  AudioEngine::Pulseaudio::cleanup();
#endif
#if HAVE_PIPEWIRE
  AudioEngine::PipeWire::cleanup();
#endif
}

bool reconfigure() {
#if HAVE_PULSEAUDIO
  if (Pulseaudio::running) {
    return AudioEngine::Pulseaudio::reconfigure();
  }
#endif
#if HAVE_PIPEWIRE
  if (PipeWire::running) {
    return AudioEngine::PipeWire::reconfigure();
  }
#endif
  return false;
}

bool read(float* buffer, const size_t& samples) {
#if HAVE_PULSEAUDIO
  if (Pulseaudio::running) {
    return AudioEngine::Pulseaudio::read(buffer, samples);
  }
#endif
#if HAVE_PIPEWIRE
  if (PipeWire::running) {
    return AudioEngine::PipeWire::read(buffer, samples);
  }
#endif
  return false;
}

} // namespace AudioEngine