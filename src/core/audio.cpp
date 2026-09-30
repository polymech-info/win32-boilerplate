#if defined(FEATURE_STT) && FEATURE_STT

// Pull in the full miniaudio implementation once here.
// All other TUs that include audio.hpp only see the thin class declaration.
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include "audio.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace pm::audio {

// ── helpers ──────────────────────────────────────────────────────────────────

static bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(),   needle.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    return it != haystack.end();
}

// ── enumerate_capture_devices ─────────────────────────────────────────────────

std::vector<DeviceInfo> enumerate_capture_devices() {
    ma_context ctx{};
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS)
        return {};

    ma_device_info* pCapture  = nullptr;
    ma_uint32       nCapture  = 0;
    ma_device_info* pPlayback = nullptr;
    ma_uint32       nPlayback = 0;

    std::vector<DeviceInfo> result;
    if (ma_context_get_devices(&ctx, &pPlayback, &nPlayback, &pCapture, &nCapture) == MA_SUCCESS) {
        result.reserve(nCapture);
        for (ma_uint32 i = 0; i < nCapture; ++i) {
            DeviceInfo d;
            d.name        = pCapture[i].name;
            d.is_default  = (pCapture[i].isDefault != 0);
            // native format fields only populated after a full device init;
            // report the first native format hint if available, else zeros.
            if (pCapture[i].nativeDataFormatCount > 0) {
                d.channels    = pCapture[i].nativeDataFormats[0].channels;
                d.sample_rate = pCapture[i].nativeDataFormats[0].sampleRate;
            }
            result.push_back(std::move(d));
        }
    }

    ma_context_uninit(&ctx);
    return result;
}

// ── AudioInput ────────────────────────────────────────────────────────────────

struct AudioInput::Impl {
    ma_device   device{};
    std::atomic<bool> running{false};
    std::function<void(const int16_t*, size_t)> on_pcm;
    std::string opened_name;

    static void data_callback(ma_device* pDevice,
                               void*        /*pOutput*/,
                               const void*  pInput,
                               ma_uint32    frameCount)
    {
        auto* self = static_cast<Impl*>(pDevice->pUserData);
        if (self && self->on_pcm && pInput && frameCount > 0) {
            self->on_pcm(static_cast<const int16_t*>(pInput),
                         static_cast<size_t>(frameCount));
        }
    }
};

AudioInput::AudioInput()  : m_(std::make_unique<Impl>()) {}
AudioInput::~AudioInput() { stop(); }

void AudioInput::start(std::function<void(const int16_t*, size_t)> on_pcm,
                       const std::string& device_name)
{
    if (m_->running.load())
        stop();

    m_->on_pcm = std::move(on_pcm);

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format   = ma_format_s16;
    cfg.capture.channels = 1;
    cfg.sampleRate       = 16000;
    cfg.dataCallback     = Impl::data_callback;
    cfg.pUserData        = m_.get();

    // If a device name was requested, enumerate and find matching id.
    ma_device_id device_id{};
    bool         use_specific = false;
    if (!device_name.empty()) {
        ma_context ctx{};
        if (ma_context_init(nullptr, 0, nullptr, &ctx) == MA_SUCCESS) {
            ma_device_info* pCapture  = nullptr;
            ma_uint32       nCapture  = 0;
            ma_device_info* pPlayback = nullptr;
            ma_uint32       nPlayback = 0;
            if (ma_context_get_devices(&ctx, &pPlayback, &nPlayback,
                                        &pCapture, &nCapture) == MA_SUCCESS) {
                for (ma_uint32 i = 0; i < nCapture; ++i) {
                    if (icontains(std::string(pCapture[i].name), device_name)) {
                        device_id    = pCapture[i].id;
                        use_specific = true;
                        break;
                    }
                }
            }
            ma_context_uninit(&ctx);
        }
        if (!use_specific)
            throw std::runtime_error(
                "audio: no capture device matching \"" + device_name + "\" (see `audio info`)");
    }

    if (use_specific)
        cfg.capture.pDeviceID = &device_id;

    if (ma_device_init(nullptr, &cfg, &m_->device) != MA_SUCCESS)
        throw std::runtime_error("audio: ma_device_init failed");

    if (ma_device_start(&m_->device) != MA_SUCCESS) {
        ma_device_uninit(&m_->device);
        throw std::runtime_error("audio: ma_device_start failed");
    }

    // Store the actual device name reported by miniaudio after init.
    m_->opened_name = m_->device.capture.name;
    m_->running.store(true);
}

void AudioInput::stop() {
    if (!m_->running.load())
        return;
    ma_device_stop(&m_->device);
    ma_device_uninit(&m_->device);
    m_->running.store(false);
}

bool AudioInput::is_running() const {
    return m_->running.load();
}

const std::string& AudioInput::opened_device_name() const {
    return m_->opened_name;
}

// ── AudioOutput ───────────────────────────────────────────────────────────────

struct AudioOutput::Impl {
    std::atomic<bool> stop_flag{false};
    std::atomic<bool> eos{false};   // end-of-stream signalled from callback

    ma_decoder decoder{};
    ma_device   device{};
    bool        decoder_open = false;
    bool        device_open  = false;

    static void data_callback(ma_device* pDevice,
                               void*        pOutput,
                               const void*  /*pInput*/,
                               ma_uint32    frameCount)
    {
        auto* self = static_cast<Impl*>(pDevice->pUserData);
        if (!self || self->stop_flag.load()) {
            std::memset(pOutput, 0,
                frameCount * ma_get_bytes_per_frame(
                    pDevice->playback.format, pDevice->playback.channels));
            return;
        }
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&self->decoder, pOutput, frameCount, &read);
        if (read < frameCount) {
            // Silence the tail and flag end-of-stream.
            const size_t frame_bytes = ma_get_bytes_per_frame(
                pDevice->playback.format, pDevice->playback.channels);
            std::memset(static_cast<char*>(pOutput) + read * frame_bytes, 0,
                        (frameCount - read) * frame_bytes);
            self->eos.store(true);
        }
    }
};

AudioOutput::AudioOutput()  : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { stop(); }

void AudioOutput::play_sync(const uint8_t* data, size_t bytes) {
    impl_->stop_flag.store(false);
    impl_->eos.store(false);

    // Decoder: auto-detect format (MP3, WAV, FLAC) from memory buffer.
    if (ma_decoder_init_memory(data, bytes, nullptr, &impl_->decoder) != MA_SUCCESS)
        throw std::runtime_error("AudioOutput: ma_decoder_init_memory failed");
    impl_->decoder_open = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = impl_->decoder.outputFormat;
    cfg.playback.channels = impl_->decoder.outputChannels;
    cfg.sampleRate        = impl_->decoder.outputSampleRate;
    cfg.dataCallback      = Impl::data_callback;
    cfg.pUserData         = impl_.get();

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->decoder_open = false;
        throw std::runtime_error("AudioOutput: ma_device_init failed");
    }
    impl_->device_open = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        ma_decoder_uninit(&impl_->decoder);
        impl_->device_open = impl_->decoder_open = false;
        throw std::runtime_error("AudioOutput: ma_device_start failed");
    }

    // Block until EOS or stop() is called.
    while (!impl_->eos.load() && !impl_->stop_flag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Brief drain so the last frames actually leave the DAC buffer.
    if (!impl_->stop_flag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(120));

    ma_device_stop(&impl_->device);
    ma_device_uninit(&impl_->device);
    ma_decoder_uninit(&impl_->decoder);
    impl_->device_open = impl_->decoder_open = false;
}

void AudioOutput::stop() {
    impl_->stop_flag.store(true);
}

bool AudioOutput::is_playing() const {
    return impl_->device_open && !impl_->eos.load();
}

// ── Async file playback ───────────────────────────────────────────────────────

namespace {

// Shared AudioOutput for async file playback.
// Using a single instance ensures:
//   1. Multiple play_sound_file_async() calls stop previous playback
//   2. No resource accumulation from repeated device init/teardown
std::unique_ptr<AudioOutput> g_async_player;
std::mutex                     g_async_player_mtx;
std::atomic<bool>              g_async_playback_active{false};

} // namespace

void play_sound_file_async(const std::string& file_path) {
    // Stop any existing playback first.
    stop_sound_file();

    // Resolve relative paths against current working directory.
    namespace fs = std::filesystem;
    fs::path path = fs::path(file_path);
    if (path.is_relative()) {
        path = fs::absolute(path);
    }

    // Read the entire file into memory.
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        throw std::runtime_error("play_sound_file: cannot open: " + path.string());
    }

    const auto size = ifs.tellg();
    if (size <= 0) {
        throw std::runtime_error("play_sound_file: empty file: " + path.string());
    }

    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("play_sound_file: read error: " + path.string());
    }
    ifs.close();

    // Start playback on a detached thread.
    // The thread owns the buffer and cleans up after playback.
    std::thread playback_thread([buf = std::move(buffer)]() mutable {
        std::lock_guard<std::mutex> lk(g_async_player_mtx);

        g_async_player = std::make_unique<AudioOutput>();
        g_async_playback_active.store(true);

        try {
            g_async_player->play_sync(buf.data(), buf.size());
        } catch (...) {
            // Swallow errors in async path; caller isn't waiting.
        }

        g_async_playback_active.store(false);
        g_async_player.reset();
    });

    playback_thread.detach();
}

void stop_sound_file() {
    std::lock_guard<std::mutex> lk(g_async_player_mtx);
    if (g_async_player) {
        g_async_player->stop();
    }
    // Wait briefly for the audio thread to acknowledge stop.
    // This prevents starting new playback while the old device is still tearing down.
    int spins = 0;
    while (g_async_playback_active.load() && spins < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ++spins;
    }
}

} // namespace pm::audio

#endif // FEATURE_STT
