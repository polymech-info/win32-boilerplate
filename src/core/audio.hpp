#pragma once

#if defined(FEATURE_STT) && FEATURE_STT

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pm::audio {

// Information about a system capture device as reported by miniaudio.
struct DeviceInfo {
    std::string name;
    bool        is_default  = false;
    uint32_t    channels    = 0;
    uint32_t    sample_rate = 0;
};

// Enumerate all capture devices available on this system via miniaudio.
// Returns an empty vector on failure (miniaudio context init error).
std::vector<DeviceInfo> enumerate_capture_devices();

// Streaming microphone capture at 16 kHz / mono / s16le.
// The callback fires on the miniaudio audio thread with interleaved s16 samples.
// Thread-safe start/stop; only one concurrent capture per instance.
class AudioInput {
public:
    AudioInput();
    ~AudioInput();

    AudioInput(const AudioInput&)            = delete;
    AudioInput& operator=(const AudioInput&) = delete;

    // Start capture.
    //   on_pcm      — called from the audio thread; pcm[0..frame_count) are mono s16le samples.
    //   device_name — case-insensitive substring match against DeviceInfo::name;
    //                 empty string (default) → system default capture device.
    // Throws std::runtime_error on miniaudio init failure.
    void start(std::function<void(const int16_t* pcm, size_t frame_count)> on_pcm,
               const std::string& device_name = "");

    // Stop capture and block until the audio thread is idle.
    void stop();

    bool is_running() const;

    // Name of the device that was opened by start() (empty before start).
    const std::string& opened_device_name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

// ── AudioOutput ───────────────────────────────────────────────────────────────
// Decode and play audio from memory through the default output device.
// miniaudio built-in decoders handle MP3, WAV, FLAC from memory.
// play_sync() blocks until the clip finishes or stop() is called.

// Play an audio file asynchronously (non-blocking).
// The file is decoded and played on a background thread.
// Returns immediately; playback continues until the file ends or stop() is called.
// Supports MP3, WAV, FLAC via miniaudio.
// Throws std::runtime_error if the file cannot be opened or decoded.
void play_sound_file_async(const std::string& file_path);

// Stop any active async playback started by play_sound_file_async().
void stop_sound_file();

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&)            = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Decode audio bytes and play them synchronously.
    // Throws std::runtime_error on init failure.
    void play_sync(const uint8_t* data, size_t bytes);

    // Signal stop from any thread (unblocks an in-progress play_sync).
    void stop();

    bool is_playing() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pm::audio

#endif // FEATURE_STT
