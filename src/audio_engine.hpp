#pragma once

#include "audio_data.hpp"

#include <memory>
#include <string>
#include <vector>

namespace AudioEngine {

enum class Type {
  PULSEAUDIO,
  PIPEWIRE,
  AUTO // Automatically select based on availability
};

// Convert string to engine type
Type stringToType(const std::string& str);

// Convert engine type to string
std::string typeToString(Type type);

// Get available engines
std::vector<Type> getAvailableEngines();

// Abstract base class for audio engines
class AudioEngineBase {
public:
  virtual ~AudioEngineBase() = default;

  // Initialize the audio engine
  virtual bool initialize(const std::string& deviceName = "", AudioData* audioData = nullptr) = 0;

  // Read audio data into buffer
  virtual bool readAudio(float* buffer, size_t frames) = 0;

  // Clean up resources
  virtual void cleanup(AudioData* audioData) = 0;

  // Get engine type
  virtual Type getType() const = 0;

  // Get sample rate
  virtual uint32_t getSampleRate() const = 0;

  // Get channel count
  virtual uint32_t getChannels() const = 0;

  // Check if engine is running
  virtual bool isRunning() const = 0;

  // Get last error message
  virtual std::string getLastError() const = 0;

  // Check if current config matches desired config
  virtual bool needsReconfiguration(const std::string& deviceName = "", uint32_t sampleRate = 44100,
                                    uint32_t bufferSize = 512) const = 0;

  // Get current device name
  virtual std::string getCurrentDevice() const = 0;
};

// Factory function to create audio engines
std::unique_ptr<AudioEngineBase> createEngine(Type type);

// Utility function to create best available engine
std::unique_ptr<AudioEngineBase> createBestAvailableEngine(Type preferred = Type::AUTO);

} // namespace AudioEngine