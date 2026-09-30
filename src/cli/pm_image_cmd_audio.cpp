#if defined(FEATURE_STT) && FEATURE_STT

#include "pm_image_cmd_audio.hpp"
#include "pm_image_cli_state.hpp"
#include "core/audio.hpp"
#include "core/cli_cancel.hpp"
#include "stt/elevenlabs.hpp"
#include "stt/proxy_stt.hpp"
#include "tts/elevenlabs_tts.hpp"
#include "tts/proxy_tts.hpp"

#include "core/settings_runtime.hpp"

#include "core/audio_record_session.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cmath>      // std::sqrt
#include <cstdlib>    // getenv

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// ── streaming WAV writer ──────────────────────────────────────────────────────

namespace {

constexpr uint32_t k_sample_rate = 16000;
constexpr uint16_t k_channels    = 1;
constexpr uint16_t k_bits        = 16;

#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]         = {'R','I','F','F'};
    uint32_t chunk_size      = 0;   // fixed up on close
    char     wave[4]         = {'W','A','V','E'};
    char     fmt_id[4]       = {'f','m','t',' '};
    uint32_t fmt_size        = 16;
    uint16_t audio_format    = 1;   // PCM
    uint16_t channels        = k_channels;
    uint32_t sample_rate     = k_sample_rate;
    uint32_t byte_rate       = k_sample_rate * k_channels * (k_bits / 8);
    uint16_t block_align     = k_channels * (k_bits / 8);
    uint16_t bits_per_sample = k_bits;
    char     data_id[4]      = {'d','a','t','a'};
    uint32_t data_size       = 0;   // fixed up on close
};
#pragma pack(pop)
static_assert(sizeof(WavHeader) == 44, "WavHeader must be exactly 44 bytes");

// Streams s16le PCM directly to a WAV file; fixup on close().
class WavWriter {
public:
    bool open(const std::filesystem::path& dst) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (dst.has_parent_path())
            fs::create_directories(dst.parent_path(), ec);
        ofs_.open(dst, std::ios::binary | std::ios::trunc);
        if (!ofs_) return false;
        WavHeader hdr{};
        ofs_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        data_bytes_ = 0;
        return ofs_.good();
    }

    // Called from audio thread — serialised by caller's mutex.
    void write(const int16_t* pcm, size_t frames) {
        const auto bytes = static_cast<uint32_t>(frames * sizeof(int16_t));
        ofs_.write(reinterpret_cast<const char*>(pcm), bytes);
        data_bytes_ += bytes;
    }

    // Seek back and fix RIFF + data chunk sizes; flush.
    bool close() {
        if (!ofs_.is_open()) return true;
        ofs_.seekp(0, std::ios::beg);
        WavHeader hdr{};
        hdr.data_size  = data_bytes_;
        hdr.chunk_size = data_bytes_ + 36;
        ofs_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        ofs_.flush();
        return ofs_.good();
    }

    uint32_t data_bytes() const { return data_bytes_; }

private:
    std::ofstream ofs_;
    uint32_t      data_bytes_ = 0;
};

// Ctrl+C is handled globally by media::cli::cancel_requested()
// (installed once at process start via install_cli_interrupt_handlers).
static bool stop_requested() { return media::cli::cancel_requested(); }

// ── audio provider resolution ─────────────────────────────────────────────────

struct ResolvedAudioProvider {
    std::string provider;   // "pixlwiz" | "elevenlabs" | …
    std::string api_key;
    std::string base_url;   // non-empty for pixlwiz
};

// CLI flag > chat settings > hardcoded fallback.
// For STT the WebSocket implementation always calls ElevenLabs directly, so
// the effective provider only determines which ProviderMap entry to pull the
// API key from; "elevenlabs" is tried as a secondary key source.
// Priority: CLI --provider/--api-key > chat audio settings. No env-var fallbacks, no
// hardcoded provider guesses — if the setting is empty or the key is missing the caller
// reports an error pointing the user to App Settings → API Providers.
static ResolvedAudioProvider resolve_audio_provider(
    const std::string&                          cli_provider,
    const std::string&                          cli_api_key,
    const std::string&                          settings_provider,
    const media::runtime_settings::ProviderMap& pmap)
{
    ResolvedAudioProvider r;
    r.provider = !cli_provider.empty() ? cli_provider : settings_provider;

    r.api_key = cli_api_key;
    if (r.api_key.empty() && !r.provider.empty()) {
        if (auto it = pmap.find(r.provider); it != pmap.end()) {
            r.api_key  = it->second.api_key;
            r.base_url = it->second.base_url;
        }
    }
    return r;
}

// ── OpenAI-compatible /audio/speech (PixlWiz proxy TTS) ──────────────────────

static size_t curl_write_bytes_vec(void* ptr, size_t sz, size_t nmemb, void* ud) {
    const size_t n = sz * nmemb;
    auto* v = static_cast<std::vector<uint8_t>*>(ud);
    const auto* p = static_cast<const uint8_t*>(ptr);
    v->insert(v->end(), p, p + n);
    return n;
}

// proxy_stt_transcribe is now in pm::stt:: (audio/stt/proxy_stt.hpp).

// proxy_tts_synthesize is now in pm::tts:: (audio/tts/proxy_tts.hpp).

// ── progress ticker ───────────────────────────────────────────────────────────

static void print_progress(double elapsed_s, uint32_t data_bytes, bool newline = false) {
    const double kb = data_bytes / 1024.0;
    // Format: "  3.2 s   51200 samples   100.0 KB"
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "  %5.1f s   %7u samples   %7.1f KB",
                  elapsed_s,
                  static_cast<unsigned>(data_bytes / sizeof(int16_t)),
                  kb);
    std::cerr << "\r" << buf;
    if (newline) std::cerr << "\n";
    else         std::cerr << std::flush;
}

} // namespace

// ── audio info ────────────────────────────────────────────────────────────────

static int cmd_audio_info(const PmImageCliState& st) {
    auto devices = pm::audio::enumerate_capture_devices();
    if (devices.empty()) {
        std::cout << "audio info: no capture devices found\n";
        return 0;
    }

    if (st.audio_info_json) {
        nlohmann::json arr = nlohmann::json::array();
        for (size_t i = 0; i < devices.size(); ++i) {
            nlohmann::json d;
            d["index"]       = i;
            d["name"]        = devices[i].name;
            d["is_default"]  = devices[i].is_default;
            d["channels"]    = devices[i].channels;
            d["sample_rate"] = devices[i].sample_rate;
            arr.push_back(std::move(d));
        }
        std::cout << arr.dump(2) << "\n";
        return 0;
    }

    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        std::cout << "Device " << i
                  << (d.is_default ? " (default)" : "         ")
                  << ": " << d.name;
        if (d.channels || d.sample_rate)
            std::cout << "  channels=" << d.channels
                      << "  sample_rate=" << d.sample_rate;
        std::cout << "\n";
    }
    return 0;
}

// ── audio record ──────────────────────────────────────────────────────────────

static int cmd_audio_record(const PmImageCliState& st) {
    namespace fs  = std::filesystem;
    namespace chr = std::chrono;

    if (st.audio_record_dst.empty() && st.audio_record_text_out.empty()) {
        std::cerr << "audio record: specify --dst and/or --text-out\n";
        return 1;
    }

    const bool use_stt = st.audio_stt || !st.audio_record_text_out.empty();
    if (!use_stt) {
        if (st.audio_record_dst.empty()) {
            std::cerr << "audio record: --text-out or --dst is required\n";
            return 1;
        }
        if (!st.audio_stt_provider.empty()) {
            std::cerr << "audio record: --provider requires --stt or --text-out\n";
            return 1;
        }
        if (!st.audio_stt_api_key.empty()) {
            std::cerr << "audio record: --api-key requires --stt or --text-out\n";
            return 1;
        }
        if (!st.audio_record_tts_voice_id.empty()) {
            std::cerr << "audio record: --voice-id requires --stt or --text-out\n";
            return 1;
        }
    }

    const bool keep_wav = !st.audio_record_dst.empty();
    fs::path dst;
    if (keep_wav) {
        dst = fs::path(st.audio_record_dst);
        if (dst.is_relative())
            dst = fs::absolute(dst);
    } else {
        std::error_code ec;
        dst = fs::temp_directory_path(ec);
        if (ec)
            dst = fs::absolute(".");
        const auto tag = chr::steady_clock::now().time_since_epoch().count();
        dst /= "pm-audio-record-" + std::to_string(tag) + ".wav";
    }

    const bool timed     = st.audio_record_duration_ms > 0;
    const int  dur_ms    = st.audio_record_duration_ms;
    const auto& dev_name = st.audio_record_input;

    // Load chat settings for provider/model defaults.
    media::runtime_settings::ChatProviderSettings cs;
    { std::string cs_err; media::runtime_settings::load_chat_provider(cs, cs_err); }

    // Resolve STT provider + API key (CLI > chat settings; only when --stt).
    media::runtime_settings::ProviderMap pmap;
    { std::string perr; media::runtime_settings::load_providers(pmap, perr); }

    const ResolvedAudioProvider stt_prov = resolve_audio_provider(
        st.audio_stt_provider, st.audio_stt_api_key, cs.stt_provider, pmap);

    const std::string& stt_api_key = stt_prov.api_key;
    if (use_stt && (stt_prov.provider.empty() || stt_api_key.empty())) {
        std::cerr << "audio record: no voice input provider configured.\n"
                  << "  Set it in: App Settings → Chat Provider → Voice & Audio → Voice input provider\n"
                  << "  or pass --provider and --api-key on the command line.\n";
        return 1;
    }

    // Two STT modes:
    //  elevenlabs → ElevenLabs Scribe v2 Realtime WebSocket (live partials during recording)
    //  pixlwiz    → OpenAI Whisper via LiteLLM /audio/transcriptions (batch, post-recording)
    const bool use_elevenlabs_stt = use_stt && stt_prov.provider == "elevenlabs";
    const bool use_whisper_stt    = use_stt && stt_prov.provider == "pixlwiz";
    // Also resolve chat.stt_model for whisper (default: "pixlwiz-speech-to-text").
    const std::string stt_model = cs.stt_model.empty() ? "pixlwiz-speech-to-text" : cs.stt_model;

    std::string eff_stt_provider;
    std::string eff_stt_model;
    if (use_elevenlabs_stt) {
        eff_stt_provider = "elevenlabs";
        if (!cs.stt_model.empty() && cs.stt_model != "pixlwiz-speech-to-text")
            eff_stt_model = cs.stt_model;
        else
            eff_stt_model = "scribe_v2_realtime";
    } else if (use_whisper_stt) {
        eff_stt_provider = "pixlwiz";
        eff_stt_model = stt_model;
    }

    char format_buf[64];
    std::snprintf(format_buf, sizeof(format_buf), "s16le mono %u Hz", k_sample_rate);
    const std::string audio_format = format_buf;

    pm::audio::record_session::Owner session;
    {
        pm::audio::record_session::StartInfo sinfo;
        sinfo.use_stt        = use_stt;
        sinfo.dst            = keep_wav ? dst.string() : std::string{};
        sinfo.text_out       = st.audio_record_text_out;
        sinfo.format         = audio_format;
        sinfo.stt_provider   = eff_stt_provider;
        sinfo.stt_model      = eff_stt_model;
        std::string sess_err;
        if (!session.acquire(sinfo, sess_err)) {
            std::cerr << "audio record: " << sess_err << "\n";
            return 1;
        }
    }

    // Open WAV file immediately so it's visible on disk from the start.
    WavWriter wav;
    if (!wav.open(dst)) {
        std::cerr << "audio record: cannot create file: " << dst.string() << "\n";
        return 1;
    }

    // ── optional STT session ─────────────────────────────────────────────────
    std::unique_ptr<pm::stt::ElevenLabsSTT> stt;

    // Thread-safe queues drained by the main tick() — avoids races with \r.
    std::mutex               stt_print_mtx;
    std::string              last_partial;
    std::queue<std::string>  committed_lines; // "[committed] ..." — for display
    std::atomic<bool>        got_committed{false};
    std::string              full_transcript;  // accumulated for --text-out
    // ElevenLabs committed_transcript is cumulative (full session text).
    // Track what we've already synthesised so we only send the delta to TTS.
    std::string              last_committed_text;

    // ── optional TTS worker (STT committed → synthesise → play) ─────────────
    const bool use_tts = use_stt && !st.audio_record_tts_voice_id.empty();
    std::mutex               tts_queue_mtx;
    std::condition_variable  tts_cv;
    std::queue<std::string>  tts_pending;   // text waiting to be synthesised
    std::atomic<bool>        tts_stop{false};
    std::thread              tts_thread;
    // Gate: while TTS is playing we suppress send_pcm to the STT engine so the
    // microphone cannot pick up speaker output and feed it back as new speech.
    std::atomic<bool>        tts_playing{false};

    if (use_tts) {
        tts_thread = std::thread([&] {
            pm::tts::ElevenLabsTTSConfig tcfg;
            tcfg.api_key       = stt_api_key; // same key for both STT and TTS
            tcfg.voice_id      = st.audio_record_tts_voice_id;
            tcfg.model_id      = st.audio_record_tts_model_id;
            tcfg.output_format = "mp3_44100_128";

            // Reuse a single AudioOutput instance for all TTS segments.
            // Creating a new device per segment causes cumulative latency drift.
            pm::audio::AudioOutput speaker;

            while (true) {
                std::unique_lock<std::mutex> lk(tts_queue_mtx);
                tts_cv.wait_for(lk, chr::milliseconds(100),
                    [&]{ return !tts_pending.empty() || tts_stop.load(); });

                while (!tts_pending.empty() && !stop_requested()) {
                    auto text = std::move(tts_pending.front());
                    tts_pending.pop();
                    lk.unlock();
                    try {
                        auto audio = pm::tts::elevenlabs_tts_synthesize(text, tcfg);
                        if (!audio.empty() && !stop_requested()) {
                            // Mute STT input while the speakers are active so the
                            // mic cannot capture and re-transcribe our own output.
                            tts_playing.store(true);
                            // play_sync blocks until playback completes; no extra thread needed.
                            speaker.play_sync(audio.data(), audio.size());
                            // Unmute after playback finishes.
                            tts_playing.store(false);
                        }
                    } catch (const std::exception& ex) {
                        tts_playing.store(false);
                        std::lock_guard<std::mutex> plk(stt_print_mtx);
                        committed_lines.push("[tts error] " + std::string(ex.what()));
                    }
                    lk.lock();
                }

                if (tts_stop.load() && tts_pending.empty())
                    break;
                if (stop_requested())
                    break;
            }
        });
    }

    if (use_elevenlabs_stt) {
        stt = std::make_unique<pm::stt::ElevenLabsSTT>();

        stt->on_partial = [&](const std::string& t) {
            std::lock_guard<std::mutex> lk(stt_print_mtx);
            last_partial = t;
        };
        stt->on_committed = [&](const std::string& t) {
            // ElevenLabs committed_transcript is CUMULATIVE — every event
            // contains the full session transcript up to that point.
            // Rules:
            //  • empty t          → silent commit (no new audio); skip entirely.
            //  • t starts with last → normal extension; delta = suffix after last.
            //  • otherwise        → server corrected/reset; use full t as segment
            //                       and reset the baseline to t.
            // NEVER let an empty / shorter response shrink the baseline, or the
            // next real commit will replay text we already synthesised.
            std::string segment;
            {
                std::lock_guard<std::mutex> lk(stt_print_mtx);
                if (t.empty()) {
                    // Silent commit — server acknowledged but no new transcript.
                    // Mark got_committed so the drain loop doesn't hang.
                    got_committed.store(true);
                    return;
                }

                if (t == last_committed_text) {
                    // Exact duplicate — nothing new.
                    got_committed.store(true);
                    return;
                }

                if (t.size() > last_committed_text.size() &&
                    t.compare(0, last_committed_text.size(), last_committed_text) == 0) {
                    // Clean forward extension: extract the new tail.
                    segment = t.substr(last_committed_text.size());
                } else {
                    // Server corrected or reset the transcript; treat the whole
                    // thing as new (avoids repeating but also avoids losing words).
                    segment = t;
                }

                // Trim leading whitespace at the protocol segment boundary.
                const auto pos = segment.find_first_not_of(" \t");
                if (pos != std::string::npos && pos > 0) segment = segment.substr(pos);

                last_committed_text = t; // advance baseline only on non-empty t
                last_partial.clear();
                got_committed.store(true);

                if (!segment.empty()) {
                    committed_lines.push("[committed] " + segment);
                    if (!full_transcript.empty()) full_transcript += ' ';
                    full_transcript += segment;
                }
            }
            if (use_tts && !segment.empty()) {
                std::lock_guard<std::mutex> lk(tts_queue_mtx);
                tts_pending.push(segment);
                tts_cv.notify_one();
            }
        };
        stt->on_error = [&](const std::string& e) {
            std::lock_guard<std::mutex> lk(stt_print_mtx);
            committed_lines.push("[stt error] " + e);
        };

        pm::stt::ElevenLabsSTT::Config scfg;
        scfg.api_key     = stt_api_key;
        scfg.sample_rate = k_sample_rate;
        // Use the model selected in App Settings → Voice & Audio; fall back to the
        // built-in default (scribe_v2_realtime) when the field is empty or still
        // holds the PixlWiz sentinel (settings upgraded from an older install).
        if (!cs.stt_model.empty() && cs.stt_model != "pixlwiz-speech-to-text")
            scfg.model_id = cs.stt_model;

        // Run connect() in a background thread so Ctrl+C can abort it.
        std::cout << "audio record: connecting to ElevenLabs STT...\n" << std::flush;
        std::string connect_err;
        std::atomic<bool> connect_done{false};
        std::thread connect_thread([&] {
            try { stt->connect(scfg); }
            catch (const std::exception& ex) { connect_err = ex.what(); }
            connect_done.store(true);
        });

        while (!connect_done.load() && !stop_requested())
            std::this_thread::sleep_for(chr::milliseconds(100));

        if (stop_requested()) {
            stt->close(); // wakes the session_cv inside connect()
            connect_thread.join();
            wav.close();
            std::cerr << "\naudio record: aborted before STT session opened\n";
            return 130;
        }
        connect_thread.join();

        if (!connect_err.empty()) {
            std::cerr << "audio record: STT connect failed: " << connect_err << "\n";
            return 1;
        }
        std::cout << "audio record: STT session ready\n" << std::flush;
    }

    // ── audio capture + VAD ───────────────────────────────────────────────────
    std::mutex wav_mtx;

    // VAD state: detect silence → auto-commit the current STT utterance.
    // All fields accessed only from the miniaudio capture thread.
    const int  vad_silence_ms  = use_elevenlabs_stt ? st.audio_record_silence_ms : 0;
    // RMS threshold (0-32767 scale). ~200 ≈ 0.6% of full-scale; quiet room noise
    // sits well below this while soft speech easily clears it.
    constexpr double k_vad_rms_threshold = 200.0;
    bool     vad_has_speech      = false; // true once speech was detected
    bool     vad_committed       = false; // true after auto-commit; cleared by new speech
    using    clk = chr::steady_clock;
    auto     vad_last_speech_tp  = clk::now(); // time of last above-threshold frame

    auto on_pcm = [&](const int16_t* data, size_t frames) {
        { std::lock_guard<std::mutex> lk(wav_mtx); wav.write(data, frames); }
        if (!stt) return;

        // While TTS is playing through speakers, suppress the mic feed to STT
        // so the microphone cannot pick up and re-transcribe our own output.
        // Also reset VAD so stale "speech" timestamps don't trigger an immediate
        // auto-commit the moment we unmute.
        if (tts_playing.load()) {
            vad_has_speech = false;
            vad_committed  = false;
            vad_last_speech_tp = clk::now();
            return;
        }

        stt->send_pcm(data, frames);

        // VAD: compute RMS of this chunk and track the last-speech timestamp.
        if (vad_silence_ms > 0 && frames > 0) {
            double sum = 0.0;
            for (size_t i = 0; i < frames; ++i)
                sum += static_cast<double>(data[i]) * data[i];
            const double rms = std::sqrt(sum / static_cast<double>(frames));
            const auto   now = clk::now();

            if (rms >= k_vad_rms_threshold) {
                // Active speech — reset silence window.
                vad_last_speech_tp = now;
                if (!vad_has_speech) {
                    vad_has_speech = true;
                    vad_committed  = false; // new utterance starts
                }
            } else if (vad_has_speech && !vad_committed) {
                // Silence following speech — check duration.
                const auto silent_ms = chr::duration_cast<chr::milliseconds>(
                    now - vad_last_speech_tp).count();
                if (silent_ms >= vad_silence_ms) {
                    // Auto-commit: tell ElevenLabs to finalise the utterance.
                    vad_committed  = true;
                    vad_has_speech = false;
                    stt->commit(); // thread-safe: just enqueues a WS message
                }
            }
        }
    };

    pm::audio::AudioInput mic;
    try {
        mic.start(on_pcm, dev_name);
    } catch (const std::exception& ex) {
        std::cerr << "audio record: " << ex.what() << "\n";
        if (stt) stt->close();
        return 1;
    }
    session.set_device(mic.opened_device_name());

    // Banner.
    std::cout << "audio record\n"
              << "  device  : " << mic.opened_device_name() << "\n"
              << "  format  : " << audio_format << "\n";
    if (keep_wav)
        std::cout << "  output  : " << dst.string() << "\n";
    if (!st.audio_record_text_out.empty()) {
        fs::path txt_path = fs::path(st.audio_record_text_out);
        if (txt_path.is_relative()) txt_path = fs::absolute(txt_path);
        std::cout << "  text-out: " << txt_path.string() << "\n";
    }
    if (use_elevenlabs_stt) {
        std::cout << "  stt     : elevenlabs (Scribe v2 Realtime — live)\n";
        if (vad_silence_ms > 0)
            std::cout << "  vad     : auto-commit after " << vad_silence_ms << " ms silence\n";
    } else if (use_whisper_stt) {
        std::cout << "  stt     : pixlwiz / " << stt_model << " (Whisper — transcribed after recording)\n";
    }
    if (use_tts)
        std::cout << "  tts     : " << stt_prov.provider
                  << "  voice=" << st.audio_record_tts_voice_id
                  << "  model=" << st.audio_record_tts_model_id << "\n";
    if (timed)
        std::cout << "  duration: " << dur_ms << " ms\n";
    else
        std::cout << "  stop    : Ctrl+C\n";
    std::cout << std::flush;

    const auto t0 = chr::steady_clock::now();

    auto poll_session_stop = [&] {
        if (session.stop_requested())
            media::cli::test_request_cancel();
    };

    // Progress ticker — drains committed/error lines first, then redraws progress.
    auto tick = [&] {
        poll_session_stop();

        // Drain any lines queued by background STT callbacks.
        {
            std::lock_guard<std::mutex> lk(stt_print_mtx);
            while (!committed_lines.empty()) {
                std::cerr << "\r" << std::string(79, ' ') << "\r"; // clear line
                std::cout << committed_lines.front() << "\n" << std::flush;
                committed_lines.pop();
            }
        }

        const double elapsed = chr::duration<double>(
            chr::steady_clock::now() - t0).count();
        uint32_t bytes;
        { std::lock_guard<std::mutex> lk(wav_mtx); bytes = wav.data_bytes(); }
        session.set_samples(bytes / static_cast<uint32_t>(sizeof(int16_t)));

        if (use_stt) {
            std::string stt_live;
            {
                std::lock_guard<std::mutex> lk(stt_print_mtx);
                stt_live = full_transcript;
                if (!last_partial.empty()) {
                    if (!stt_live.empty() && stt_live.back() != ' ')
                        stt_live += ' ';
                    stt_live += last_partial;
                }
            }
            session.set_stt_buffer(stt_live);
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "  %5.1f s   %7u samples   %7.1f KB",
                      elapsed,
                      static_cast<unsigned>(bytes / sizeof(int16_t)),
                      bytes / 1024.0);
        std::cerr << "\r" << buf;

        if (use_elevenlabs_stt) {
            std::lock_guard<std::mutex> lk(stt_print_mtx);
            if (!last_partial.empty()) {
                auto p = last_partial.substr(0, 55);
                std::cerr << "  [" << p;
                if (last_partial.size() > 55) std::cerr << "...";
                std::cerr << "]";
            }
        }
        std::cerr << std::flush;
    };

    if (timed) {
        const auto deadline = t0 + chr::milliseconds(dur_ms);
        while (chr::steady_clock::now() < deadline && !stop_requested()) {
            poll_session_stop();
            std::this_thread::sleep_for(chr::milliseconds(200));
            tick();
        }
    } else {
        while (!stop_requested()) {
            poll_session_stop();
            std::this_thread::sleep_for(chr::milliseconds(200));
            tick();
        }
    }

    session.set_state(pm::audio::record_session::State::Stopping);

    mic.stop();

    const double total_s = chr::duration<double>(
        chr::steady_clock::now() - t0).count();
    uint32_t final_bytes;
    { std::lock_guard<std::mutex> lk(wav_mtx); final_bytes = wav.data_bytes(); }

    // Final progress line.
    print_progress(total_s, final_bytes, /*newline=*/true);

    // ── STT commit + drain ───────────────────────────────────────────────────
    if (stt) {
        std::cout << "audio record: committing STT...\n" << std::flush;
        stt->commit();
        // Wait for committed transcript — do NOT short-circuit on stop_requested()
        // because the first Ctrl+C only stops recording; we still need the final
        // transcript to arrive before TTS can synthesise it.
        const auto wait_end = chr::steady_clock::now() + chr::seconds(5);
        while (!got_committed.load() && chr::steady_clock::now() < wait_end) {
            std::this_thread::sleep_for(chr::milliseconds(100));
            tick();
        }
        tick(); // final drain
        stt->close();
    }

    // Signal TTS worker that no more text is coming, then wait for it to
    // finish synthesising and playing whatever is still in the queue.
    if (tts_thread.joinable()) {
        tts_stop.store(true);
        tts_cv.notify_all();
        if (!tts_pending.empty()) // a quick peek (approximate — worker drains it)
            std::cout << "audio record: finishing TTS playback...\n" << std::flush;
        tts_thread.join();
    }

    if (!wav.close()) {
        std::cerr << "audio record: failed to finalise WAV header\n";
        return 1;
    }

    // ── Whisper batch transcription (pixlwiz) ─────────────────────────────────
    if (use_whisper_stt) {
        std::cout << "audio record: transcribing with Whisper..." << std::flush;
        try {
            full_transcript = pm::stt::proxy_stt_transcribe(
                dst.string(), stt_prov.base_url, stt_api_key, stt_model);
            std::cout << "\n[transcript] " << full_transcript << "\n" << std::flush;
        } catch (const std::exception& ex) {
            std::cerr << "\naudio record: Whisper transcription failed: " << ex.what() << "\n";
        }
    }

    // Write transcript file if --text-out was requested.
    if (!st.audio_record_text_out.empty()) {
        fs::path txt_path = fs::path(st.audio_record_text_out);
        if (txt_path.is_relative()) txt_path = fs::absolute(txt_path);
        std::ofstream ofs(txt_path, std::ios::out | std::ios::trunc);
        if (!ofs) {
            std::cerr << "audio record: cannot create transcript file: " << txt_path.string() << "\n";
        } else {
            ofs << full_transcript << "\n";
            ofs.close();
            std::cout << "audio record: transcript → " << txt_path.string()
                      << "  (" << full_transcript.size() << " chars)\n";
        }
    }

    std::error_code sz_ec;
    const auto file_size = fs::file_size(dst, sz_ec);
    std::cout << "audio record: done\n"
              << "  samples : " << (final_bytes / sizeof(int16_t)) << "\n"
              << "  duration: " << total_s << " s\n";
    if (keep_wav) {
        std::cout << "  file    : " << dst.string();
        if (!sz_ec)
            std::cout << "  (" << file_size << " bytes)";
        std::cout << "\n";
    } else {
        std::error_code rm_ec;
        fs::remove(dst, rm_ec);
    }
    return 0;
}

static uint64_t now_unix_ms_for_status()
{
    namespace chr = std::chrono;
    return static_cast<uint64_t>(chr::duration_cast<chr::milliseconds>(
        chr::system_clock::now().time_since_epoch()).count());
}

static int cmd_audio_record_stop() {
    std::string err;
    if (!pm::audio::record_session::request_stop(err)) {
        std::cerr << "audio record stop: " << err << "\n";
        return 1;
    }
    std::cout << "audio record stop: signal sent\n";
    return 0;
}

static int cmd_audio_record_status(const PmImageCliState& st) {
    namespace chr = std::chrono;

    pm::audio::record_session::Status info;
    std::string err;
    if (!pm::audio::record_session::read_status(info, err)) {
        std::cerr << "audio record status: " << err << "\n";
        return 1;
    }

    if (!info.active) {
        if (st.audio_record_status_json) {
            nlohmann::json j;
            j["active"] = false;
            std::cout << j.dump(2) << "\n";
        } else {
            std::cout << "audio record status: not recording\n";
        }
        return 0;
    }

    const double elapsed_s = info.started_unix_ms > 0
        ? chr::duration<double>(chr::milliseconds(
              static_cast<int64_t>(now_unix_ms_for_status() - info.started_unix_ms))).count()
        : 0.0;

    if (st.audio_record_status_json) {
        nlohmann::json j;
        j["active"]    = true;
        j["pid"]       = info.pid;
        j["state"]     = static_cast<uint32_t>(info.state);
        j["use_stt"]   = info.use_stt;
        j["samples"]   = info.samples;
        j["elapsed_s"] = elapsed_s;
        if (!info.device.empty())       j["device"] = info.device;
        if (!info.format.empty())       j["format"] = info.format;
        if (!info.stt_provider.empty()) j["stt_provider"] = info.stt_provider;
        if (!info.stt_model.empty())    j["stt_model"] = info.stt_model;
        if (!info.dst.empty())      j["dst"] = info.dst;
        if (!info.text_out.empty()) j["text_out"] = info.text_out;
        if (info.use_stt && !info.stt_buffer.empty()) j["stt_buffer"] = info.stt_buffer;
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    std::cout << "audio record status: recording\n"
              << "  pid     : " << info.pid << "\n"
              << "  elapsed : " << elapsed_s << " s\n"
              << "  samples : " << info.samples << "\n";
    if (!info.device.empty())
        std::cout << "  device  : " << info.device << "\n";
    if (!info.format.empty())
        std::cout << "  format  : " << info.format << "\n";
    if (!info.dst.empty())
        std::cout << "  dst     : " << info.dst << "\n";
    if (!info.text_out.empty())
        std::cout << "  text-out: " << info.text_out << "\n";
    if (info.use_stt) {
        std::cout << "  stt     : ";
        if (!info.stt_provider.empty()) {
            std::cout << info.stt_provider;
            if (!info.stt_model.empty())
                std::cout << " / " << info.stt_model;
            std::cout << "\n";
        }
        std::cout << "  stt buf : ";
        if (info.stt_buffer.empty())
            std::cout << "(waiting)\n";
        else
            std::cout << info.stt_buffer << "\n";
    }
    return 0;
}

// ── audio tts ─────────────────────────────────────────────────────────────────

static int cmd_audio_tts(const PmImageCliState& st) {
    namespace fs  = std::filesystem;

    // Load chat settings for provider/model/voice defaults.
    media::runtime_settings::ChatProviderSettings cs;
    { std::string cs_err; media::runtime_settings::load_chat_provider(cs, cs_err); }
    media::runtime_settings::ProviderMap pmap;
    { std::string perr; media::runtime_settings::load_providers(pmap, perr); }

    // Effective TTS provider: CLI --provider > chat.tts_provider > "elevenlabs".
    const ResolvedAudioProvider tts_prov = resolve_audio_provider(
        st.audio_tts_provider, st.audio_tts_api_key, cs.tts_provider, pmap);

    const std::string& api_key = tts_prov.api_key;
    if (tts_prov.provider.empty() || api_key.empty()) {
        std::cerr << "audio tts: no voice output provider configured.\n"
                  << "  Set it in: App Settings → Chat Provider → Voice & Audio → Voice output provider\n"
                  << "  or pass --provider and --api-key on the command line.\n";
        return 1;
    }

    // Sentinels: these are the hardcoded defaults in pm_image_cli_state.hpp.
    // When a user hasn't explicitly passed them we fall back to chat settings.
    constexpr const char* kDefaultVoiceId  = "tLK6fPv15M0oKv4V3ACR";
    constexpr const char* kDefaultModelId  = "eleven_v3";
    const bool voice_is_default = (st.audio_tts_voice_id == kDefaultVoiceId);
    const bool model_is_default = (st.audio_tts_model_id == kDefaultModelId);

    // Effective voice / model depend on provider.
    // PixlWiz: model = tts_model alias (e.g. "pixlwiz-speech"); voice = optional UUID override.
    // ElevenLabs: voice = tts_model (stored as voice UUID in settings); model = eleven_* variant.
    std::string eff_voice_id = st.audio_tts_voice_id;
    std::string eff_model_id = st.audio_tts_model_id;

    if (tts_prov.provider == "pixlwiz") {
        // Model: settings alias > hard proxy default > CLI default (eleven_v3 doesn't apply here).
        if (model_is_default)
            eff_model_id = !cs.tts_model.empty() ? cs.tts_model : "pixlwiz-speech";
        // Voice: not stored in settings for pixlwiz (proxy sets its default Sarah).
        // Only pass it if the user explicitly specified something other than the EL sentinel.
        if (voice_is_default)
            eff_voice_id.clear();
    } else {
        // ElevenLabs (direct): tts_voice_id is the voice UUID; tts_model is the synthesis model.
        if (voice_is_default && !cs.tts_voice_id.empty())
            eff_voice_id = cs.tts_voice_id;
        if (model_is_default && !cs.tts_model.empty())
            eff_model_id = cs.tts_model;
    }

    const bool has_dst  = !st.audio_tts_dst.empty();
    const bool do_play  = !st.audio_tts_no_play;

    if (!has_dst && !do_play) {
        std::cerr << "audio tts: nothing to do (--no-play and no --dst)\n";
        return 1;
    }

    // Resolve destination + format.
    fs::path    dst;
    std::string fmt = st.audio_tts_format;

    if (has_dst) {
        dst = fs::path(st.audio_tts_dst);
        if (dst.is_relative()) dst = fs::absolute(dst);
        if (fmt.empty()) {
            std::string ext = dst.extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            fmt = pm::tts::tts_format_for_ext(ext);
        }
    }
    // For play-only (no --dst), always fetch MP3 — miniaudio decodes it natively.
    if (fmt.empty()) fmt = "mp3_44100_128";

    std::cout << "audio tts\n"
              << "  provider: " << tts_prov.provider << "\n"
              << "  voice   : " << (eff_voice_id.empty() ? "(proxy default)" : eff_voice_id) << "\n"
              << "  model   : " << eff_model_id << "\n"
              << "  format  : " << fmt << "\n";
    if (has_dst)
        std::cout << "  output  : " << dst.string() << "\n";
    if (do_play)
        std::cout << "  playback: speakers\n";
    std::cout << "  text    : " << st.audio_tts_text.substr(0, 80)
              << (st.audio_tts_text.size() > 80 ? "..." : "") << "\n"
              << std::flush;

    // Synthesise — route to proxy (PixlWiz) or ElevenLabs direct.
    std::vector<uint8_t> audio;
    try {
        if (tts_prov.provider == "pixlwiz") {
            // OpenAI-compatible /audio/speech via LiteLLM proxy.
            // fmt for the proxy uses OpenAI names: "mp3", "opus", "aac", "flac".
            std::string proxy_fmt = fmt;
            // Strip ElevenLabs-style suffixes ("mp3_44100_128" → "mp3").
            const auto us = proxy_fmt.find('_');
            if (us != std::string::npos) proxy_fmt = proxy_fmt.substr(0, us);
            audio = pm::tts::proxy_tts_synthesize(
                st.audio_tts_text, tts_prov.base_url, api_key,
                eff_model_id, eff_voice_id, proxy_fmt);
        } else {
            pm::tts::ElevenLabsTTSConfig cfg;
            cfg.api_key       = api_key;
            cfg.voice_id      = eff_voice_id;
            cfg.model_id      = eff_model_id;
            cfg.output_format = fmt;
            audio = pm::tts::elevenlabs_tts_synthesize(st.audio_tts_text, cfg);
        }
    } catch (const std::exception& ex) {
        std::cerr << "audio tts: " << ex.what() << "\n";
        return 1;
    }
    if (audio.empty()) {
        std::cerr << "audio tts: server returned empty response\n";
        return 1;
    }

    // Save to file if requested.
    if (has_dst) {
        std::ofstream ofs(dst, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            std::cerr << "audio tts: cannot create: " << dst.string() << "\n";
            return 1;
        }

        if (pm::tts::tts_format_is_pcm(fmt)) {
            // Prepend WAV header for raw PCM output.
            const int      sr         = pm::tts::tts_pcm_sample_rate(fmt);
            const uint16_t channels   = 1;
            const uint16_t bps        = 16;
            const uint32_t data_bytes = static_cast<uint32_t>(audio.size());
            auto w16 = [&](uint16_t v){ ofs.write(reinterpret_cast<const char*>(&v), 2); };
            auto w32 = [&](uint32_t v){ ofs.write(reinterpret_cast<const char*>(&v), 4); };
            ofs.write("RIFF", 4); w32(36 + data_bytes);
            ofs.write("WAVE", 4);
            ofs.write("fmt ", 4); w32(16); w16(1); w16(channels);
            w32(static_cast<uint32_t>(sr));
            w32(static_cast<uint32_t>(sr * channels * bps / 8));
            w16(static_cast<uint16_t>(channels * bps / 8)); w16(bps);
            ofs.write("data", 4); w32(data_bytes);
        }

        ofs.write(reinterpret_cast<const char*>(audio.data()),
                  static_cast<std::streamsize>(audio.size()));
        ofs.close();
        if (!ofs) {
            std::cerr << "audio tts: write error: " << dst.string() << "\n";
            return 1;
        }

        std::error_code ec;
        const auto sz = fs::file_size(dst, ec);
        std::cout << "audio tts: saved  " << dst.string();
        if (!ec) std::cout << "  (" << sz << " bytes)";
        std::cout << "\n" << std::flush;
    }

    // Play through speakers (aborts immediately if Ctrl+C is pressed).
    if (do_play && !stop_requested()) {
        namespace chr = std::chrono;
        std::cout << "audio tts: playing..." << std::flush;
        try {
            pm::audio::AudioOutput out;
            // play_sync blocks until playback completes; handle Ctrl+C via stop()
            std::atomic<bool> play_done{false};
            std::thread playback_thread([&]{
                out.play_sync(audio.data(), audio.size());
                play_done.store(true);
            });
            // Poll for Ctrl+C and forward cancellation to the audio device.
            while (!play_done.load() && !stop_requested())
                std::this_thread::sleep_for(chr::milliseconds(30));
            if (stop_requested())
                out.stop(); // signals miniaudio to end playback
            playback_thread.join();
        } catch (const std::exception& ex) {
            std::cerr << "\naudio tts: playback error: " << ex.what() << "\n";
            return 1;
        }
        std::cout << (stop_requested() ? " aborted\n" : " done\n");
    }

    return 0;
}

// ── audio play ────────────────────────────────────────────────────────────────

static int cmd_audio_play(const PmImageCliState& st) {
    namespace fs  = std::filesystem;
    namespace chr = std::chrono;

    if (st.audio_play_path.empty()) {
        std::cerr << "audio play: no file specified\n";
        return 1;
    }

    // Resolve path (relative → absolute from cwd).
    fs::path path = fs::path(st.audio_play_path);
    if (path.is_relative()) {
        path = fs::absolute(path);
    }

    // Verify file exists and is readable.
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
        std::cerr << "audio play: file not found: " << path.string() << "\n";
        return 1;
    }

    // Read the file into memory.
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cerr << "audio play: cannot open: " << path.string() << "\n";
        return 1;
    }
    const auto size = ifs.tellg();
    if (size <= 0) {
        std::cerr << "audio play: empty file: " << path.string() << "\n";
        return 1;
    }
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "audio play: read error: " << path.string() << "\n";
        return 1;
    }
    ifs.close();

    std::cout << "audio play: " << path.filename().string()
              << " (" << buffer.size() << " bytes)";
    if (st.audio_play_wait) {
        std::cout << "...\n" << std::flush;
    } else {
        std::cout << " (async)\n" << std::flush;
    }

    try {
        if (st.audio_play_wait) {
            // Blocking mode: play synchronously with Ctrl+C handling.
            pm::audio::AudioOutput out;
            std::atomic<bool> play_done{false};
            std::thread pt([&]{
                out.play_sync(buffer.data(), buffer.size());
                play_done.store(true);
            });
            // Poll for Ctrl+C and forward cancellation.
            while (!play_done.load() && !stop_requested())
                std::this_thread::sleep_for(chr::milliseconds(30));
            if (stop_requested())
                out.stop();
            pt.join();
            if (stop_requested()) {
                std::cout << "audio play: aborted\n";
                return 130; // SIGINT exit code
            }
            std::cout << "audio play: done\n";
        } else {
            // Fire-and-forget mode: use the async helper.
            pm::audio::play_sound_file_async(path.string());
        }
    } catch (const std::exception& ex) {
        std::cerr << "audio play: error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}

// ── dispatch ──────────────────────────────────────────────────────────────────

int pm_image_cmd_audio(CLI::App& /*app*/, PmImageCliState& st) {
    if (st.audio_info_cmd && st.audio_info_cmd->parsed())
        return cmd_audio_info(st);
    if (st.audio_record_stop_cmd && st.audio_record_stop_cmd->parsed())
        return cmd_audio_record_stop();
    if (st.audio_record_status_cmd && st.audio_record_status_cmd->parsed())
        return cmd_audio_record_status(st);
    if (st.audio_record_cmd && st.audio_record_cmd->parsed())
        return cmd_audio_record(st);
    if (st.audio_play_cmd && st.audio_play_cmd->parsed())
        return cmd_audio_play(st);
    if (st.audio_tts_cmd && st.audio_tts_cmd->parsed())
        return cmd_audio_tts(st);
    return 0;
}

void pm_image_register_audio(CLI::App& app, PmImageCliState& s) {
    s.audio_cmd = app.add_subcommand(
        "audio",
        "Audio utilities: list devices, record to WAV, play audio files, and TTS synthesis. "
        "Uses miniaudio (MP3, WAV, FLAC) and WASAPI/CoreAudio/ALSA. "
        "Requires FEATURE_STT build.");
    s.audio_cmd->require_subcommand(1);

    s.audio_info_cmd = s.audio_cmd->add_subcommand(
        "info",
        "List capture devices available on this system (name, channels, sample rate, default flag).");
    s.audio_info_cmd->add_flag(
        "--json", s.audio_info_json,
        "Machine-readable JSON array on stdout.");

    s.audio_record_cmd = s.audio_cmd->add_subcommand(
        "record",
        "Record from a capture device (PCM s16le mono 16 kHz). "
        "Stops after --duration-ms, Ctrl+C, console close, or `audio record stop`. "
        "Plain recording: --dst. Transcript only: --text-out (enables STT). Both is also fine.");
    s.audio_record_cmd->require_subcommand(0, 1);
    s.audio_record_cmd
        ->add_option(
            "--dst", s.audio_record_dst,
            "Destination WAV file. Optional when --text-out is set (scratch WAV is used internally). "
            "Relative paths are resolved from the current working directory.");
    s.audio_record_cmd
        ->add_option(
            "--input", s.audio_record_input,
            "Capture device name (case-insensitive substring; use `audio info` to list names). "
            "Omit to use the system default input device.");
    s.audio_record_cmd
        ->add_option(
            "--duration-ms", s.audio_record_duration_ms,
            "Stop recording after this many milliseconds (0 = run until Ctrl+C).")
        ->default_val(0)
        ->check(CLI::NonNegativeNumber);
    s.audio_record_cmd
        ->add_flag(
            "--stt", s.audio_stt,
            "Enable speech-to-text alongside recording (live ElevenLabs or batch Whisper). "
            "Implied by --text-out. Without either flag, --dst records WAV only.");
    s.audio_record_cmd
        ->add_option(
            "--provider", s.audio_stt_provider,
            "STT provider when --stt or --text-out is used. "
            "Currently supported: elevenlabs (Scribe v2 Realtime), pixlwiz (Whisper batch). "
            "Defaults to chat.stt_provider from app settings.")
        ->check(CLI::IsMember({"elevenlabs", "pixlwiz"}, CLI::ignore_case));
    s.audio_record_cmd
        ->add_option(
            "--api-key", s.audio_stt_api_key,
            "API key for the selected STT provider (--provider elevenlabs → xi-api-key). "
            "Also used as the TTS key when --voice-id is set. "
            "Falls back to the provider entry in App Settings when omitted.");
    s.audio_record_cmd
        ->add_option(
            "--voice-id", s.audio_record_tts_voice_id,
            "ElevenLabs voice ID for real-time TTS playback. "
            "When set, each committed transcript is synthesised and played through the speakers. "
            "Requires --stt/--text-out and --provider elevenlabs. Browse voices at elevenlabs.io/app/voice-library.");
    s.audio_record_cmd
        ->add_option(
            "--model-id", s.audio_record_tts_model_id,
            "ElevenLabs TTS model used with --voice-id (default: eleven_v3).")
        ->default_val("eleven_v3");
    s.audio_record_cmd
        ->add_option(
            "--silence-ms", s.audio_record_silence_ms,
            "Auto-commit STT utterance after this many milliseconds of silence "
            "(0 = disabled; requires --stt or --text-out; default: 1500).")
        ->default_val(1500);
    s.audio_record_cmd
        ->add_option(
            "--text-out", s.audio_record_text_out,
            "Write the full STT transcript to this file (UTF-8 text). "
            "Enables STT automatically. --dst is optional. "
            "Relative paths are resolved from the current working directory.");

    s.audio_record_stop_cmd = s.audio_record_cmd->add_subcommand(
        "stop",
        "Signal a running `audio record` session in another terminal to stop cooperatively.");
    s.audio_record_status_cmd = s.audio_record_cmd->add_subcommand(
        "status",
        "Show the active `audio record` session (device, format, provider/model, elapsed time, dst, text-out, live STT buffer).");
    s.audio_record_status_cmd->add_flag(
        "--json", s.audio_record_status_json,
        "Machine-readable JSON on stdout.");

    s.audio_play_cmd = s.audio_cmd->add_subcommand(
        "play",
        "Play an audio file through the default output device. "
        "Supports MP3, WAV, FLAC via miniaudio. "
        "Playback is asynchronous by default; use --wait to block until finished.");
    s.audio_play_cmd
        ->add_option(
            "path", s.audio_play_path,
            "Path to the audio file. Relative paths are resolved from the current working directory.")
        ->required(true);
    s.audio_play_cmd
        ->add_flag(
            "--wait", s.audio_play_wait,
            "Block until playback finishes (normally async). Ctrl+C aborts playback.");

    s.audio_tts_cmd = s.audio_cmd->add_subcommand(
        "tts",
        "Synthesise speech from text using a TTS provider and write audio to a file.");
    s.audio_tts_cmd
        ->add_option(
            "--text", s.audio_tts_text,
            "Text to synthesise. Use quotes for multi-word input.")
        ->required(true);
    s.audio_tts_cmd
        ->add_option(
            "--dst", s.audio_tts_dst,
            "Destination file (.mp3 / .wav / .opus). "
            "Extension determines the default output format when --format is omitted. "
            "Omit to play through speakers without saving.");
    s.audio_tts_cmd
        ->add_flag(
            "--no-play", s.audio_tts_no_play,
            "Do not play audio through speakers; only save to --dst.");
    s.audio_tts_cmd
        ->add_option(
            "--provider", s.audio_tts_provider,
            "TTS provider: elevenlabs (direct API) or pixlwiz (proxy /audio/speech). "
            "Defaults to chat.tts_provider from app settings, then elevenlabs.")
        ->check(CLI::IsMember({"elevenlabs", "pixlwiz"}, CLI::ignore_case));
    s.audio_tts_cmd
        ->add_option(
            "--api-key", s.audio_tts_api_key,
            "API key for the TTS provider. Falls back to ELEVENLABS_API_KEY env var.");
    s.audio_tts_cmd
        ->add_option(
            "--voice-id", s.audio_tts_voice_id,
            "Voice ID. For elevenlabs: ElevenLabs voice UUID. "
            "For pixlwiz: ElevenLabs voice UUID override (empty = proxy default). "
            "Defaults to chat.tts_model from app settings when omitted.")
        ->default_val("tLK6fPv15M0oKv4V3ACR");
    s.audio_tts_cmd
        ->add_option(
            "--model-id", s.audio_tts_model_id,
            "Model ID. For elevenlabs: eleven_v3, eleven_turbo_v2, etc. "
            "For pixlwiz: proxy alias (pixlwiz-speech, pixlwiz-speech-turbo). "
            "Defaults to chat.tts_model from app settings when omitted.")
        ->default_val("eleven_v3");
    s.audio_tts_cmd
        ->add_option(
            "--format", s.audio_tts_format,
            "Output format override. "
            "ElevenLabs: mp3_44100_128, pcm_44100, opus_48000_32. "
            "PixlWiz proxy: mp3, opus, aac, flac. "
            "Defaults to mp3_44100_128 for .mp3, pcm_44100 for .wav.");
}

#endif // FEATURE_STT
