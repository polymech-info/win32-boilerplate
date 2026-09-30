#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_llm_agent.hpp"

#include "cli_tty.hpp"
#include "html/html.h"
#include "llm/agent_memory.hpp"
#include "llm/agent_scheduler.hpp"
#include "llm/path_tool_executor.hpp"  // abort_active_speak / abort_active_run
#ifndef FEATURE_HOME_LLM_TOOLS
#define FEATURE_HOME_LLM_TOOLS 1
#endif
#ifndef FEATURE_HOME_LLM_SKILLS
#define FEATURE_HOME_LLM_SKILLS 1
#endif
#if FEATURE_HOME_LLM_SKILLS
#include "llm/agent_skills.hpp"
#endif
#include "core/settings_runtime.hpp"

#include <filesystem>
namespace fs = std::filesystem;

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <conio.h>
#endif

#if defined(FEATURE_STT) && FEATURE_STT
#include "core/audio.hpp"
#include "stt/elevenlabs.hpp"
#include "stt/proxy_stt.hpp"
#include "tts/elevenlabs_tts.hpp"
#include "tts/proxy_tts.hpp"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#endif

namespace {

#if FEATURE_HOME_LLM_SKILLS
media::llm::skills::SkillSnapshot load_skill_snapshot_for_cwd(const std::string& cwd_hint) {
    media::runtime_settings::AgentSkillsSettings cfg;
    std::string err;
    (void)media::runtime_settings::load_agent_skills_settings(cfg, err);
    media::llm::skills::SkillPolicy policy;
    policy.enabled = cfg.enabled;
    policy.roaming_enabled = cfg.roaming_enabled;
    policy.workspace_enabled = cfg.workspace_enabled;
    policy.pinned = cfg.pinned;
    policy.disabled = cfg.disabled;
    return media::llm::skills::discover_skills(policy, cwd_hint);
}
#endif

void log_llm_usage_aggregate_debug(const nlohmann::json& agg) {
    if (!agg.is_object())
        return;
    auto num_ll = [](const nlohmann::json& j, const char* k) -> long long {
        if (!j.contains(k) || !j[k].is_number())
            return 0;
        if (j[k].is_number_unsigned())
            return static_cast<long long>(j[k].get<std::uint64_t>());
        if (j[k].is_number_integer())
            return j[k].get<long long>();
        return static_cast<long long>(j[k].get<double>());
    };
    const long long pr = num_ll(agg, "prompt_tokens");
    const long long co = num_ll(agg, "completion_tokens");
    const long long tt = num_ll(agg, "total_tokens");
    const auto rounds = agg.value("llm_rounds", nlohmann::json::array());
    const bool has_cost = agg.contains("cost") && agg["cost"].is_number();
    const bool has = (rounds.is_array() && !rounds.empty()) || tt > 0 || pr > 0 || co > 0 || has_cost;
    if (!has)
        return;

    std::ostringstream os;
    os << "llm agent: usage — prompt=" << pr << " completion=" << co << " total=" << tt;
    if (has_cost) {
        const double c = agg["cost"].get<double>();
        os << std::fixed << std::setprecision(6) << " cost_usd≈" << c << std::defaultfloat;
    }
    if (rounds.is_array() && !rounds.empty())
        os << " api_rounds=" << rounds.size();
    logger::debug(os.str());
}

void append_session_turn_summary_if_enabled(const PmImageCliState& /*st*/,
                                            const std::string&     session_task_id,
                                            const std::string&     user_prompt,
                                            const media::llm::agent::Result& result)
{
    // Gate purely on whether a session is open — single-turn + --session-id
    // (cross-process A2A pattern) also benefits from the persisted event log.
    if (session_task_id.empty() || !result.ok)
        return;

    nlohmann::json entry = nlohmann::json::object();
    entry["ts"]   = media::llm::agent::now_iso8601();
    entry["user"] = user_prompt;
    if (!result.final_text.empty())
        entry["assistant"] = result.final_text;

    nlohmann::json tools = nlohmann::json::array();
    try {
        if (result.transcript.is_object() && result.transcript.contains("messages")
            && result.transcript["messages"].is_array()) {
            for (const auto& m : result.transcript["messages"]) {
                if (!m.is_object())
                    continue;
                if (m.value("role", std::string()) != "tool")
                    continue;
                const std::string tn = m.value("name", std::string());
                if (!tn.empty())
                    tools.push_back(tn);
            }
        }
    } catch (...) {}
    if (!tools.empty())
        entry["tools"] = std::move(tools);

    media::llm::agent::session_append_turn(session_task_id, entry);
}

// ── Scheduler daemon helper for `llm agent --scheduler` ─────────────────────
//
// SIGINT is intentionally handled via a single global atomic flag (and not the
// per-call `prov` capture) because std::signal cannot accept a closure on
// POSIX/Windows. The flag also doubles as a ctrl-c sentinel for the wait loop.
std::atomic<bool> g_scheduler_stop_requested{false};

void scheduler_sigint_handler(int) {
    g_scheduler_stop_requested.store(true, std::memory_order_release);
    // Best-effort wake the scheduler thread out of any in-progress speak/run.
    media::llm::path::abort_active_speak();
    media::llm::path::abort_active_run();
}

/// Configure and run the global AgentScheduler in-process, blocking the CLI
/// process so scheduled ticks (`schedule_every` / `schedule_in` / `schedule_at`
/// created during the agent's turn(s)) actually fire. Returns when:
///   * SIGINT is received (Ctrl+C)             → exit code 0
///   * timeout_s > 0 and that many seconds pass → exit code 0
///   * exit_when_idle and no enabled tasks remain → exit code 0
int run_scheduler_blocking(const media::llm::agent::ProviderSettings& prov,
                           int  timeout_s,
                           bool exit_when_idle)
{
    auto& sched = media::llm::agent::global_scheduler();

    // Re-snapshot the provider settings on every tick. Capturing prov by
    // value here is intentional: settings.json changes mid-loop are NOT
    // hot-reloaded (a CLI session is short-lived).
    sched.configure([prov] { return prov; },
                    /*on_event=*/nullptr);

    g_scheduler_stop_requested.store(false, std::memory_order_release);

    using SigHandler = void (*)(int);
    SigHandler prev_sigint = std::signal(SIGINT, &scheduler_sigint_handler);

    {
        auto tasks = media::llm::agent::task_store_list();
        size_t enabled = 0;
        for (const auto& t : tasks) if (t.enabled) ++enabled;
        std::cerr << "[scheduler] starting -- enabled tasks=" << enabled
                  << " (total=" << tasks.size() << ")";
        if (timeout_s > 0) std::cerr << " timeout=" << timeout_s << "s";
        if (exit_when_idle) std::cerr << " exit-when-idle=on";
        std::cerr << " router=" << prov.router << " model=" << prov.model
                  << " -- press Ctrl+C to stop\n";
    }

    sched.start();

    const auto deadline = (timeout_s > 0)
        ? std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s)
        : std::chrono::steady_clock::time_point::max();

    while (!g_scheduler_stop_requested.load(std::memory_order_acquire)) {
        if (timeout_s > 0 && std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "[scheduler] timeout reached -- stopping\n";
            break;
        }
        if (exit_when_idle) {
            auto tasks = media::llm::agent::task_store_list();
            bool any_enabled = false;
            for (const auto& t : tasks) {
                if (t.enabled) { any_enabled = true; break; }
            }
            if (!any_enabled) {
                std::cerr << "[scheduler] no enabled tasks remain -- stopping\n";
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    sched.stop();
    std::signal(SIGINT, prev_sigint);
    g_scheduler_stop_requested.store(false, std::memory_order_release);
    return 0;
}

} // namespace

// ── mic conversation loop ─────────────────────────────────────────────────────
#if defined(FEATURE_STT) && FEATURE_STT

static int cmd_llm_agent_mic(const PmImageCliState& st,
                              media::llm::agent::Turn         base_turn,
                              const media::llm::agent::ProviderSettings& prov)
{
    namespace chr = std::chrono;
    using clk     = chr::steady_clock;

    // ── Resolve STT / TTS from chat Audio settings ────────────────────────────
    media::runtime_settings::ChatProviderSettings chat;
    {
        std::string err;
        media::runtime_settings::load_chat_provider(chat, err);
    }

    const std::string stt_provider = chat.stt_provider;
    const std::string stt_model    = chat.stt_model;

    if (stt_provider.empty()) {
        std::cerr << "llm agent mic: no STT provider — configure one in "
                     "App Settings → Chat → Voice & Audio\n";
        return 1;
    }

    // CLI --stt-api-key overrides the key; base_url always comes from settings.
    std::string stt_key      = st.ag_mic_stt_api_key;
    std::string stt_base_url;
    if (stt_key.empty())
        media::runtime_settings::merge_provider_credentials(
            stt_provider, false, stt_key, stt_base_url);
    if (stt_key.empty()) {
        std::cerr << "llm agent mic: no API key for STT provider '" << stt_provider
                  << "' — configure it in App Settings → API Providers\n";
        return 1;
    }

    const std::string tts_provider  = chat.tts_provider;
    const std::string tts_model     = chat.tts_model;
    const std::string tts_voice_id  = chat.tts_voice_id;
    std::string tts_key, tts_base_url;
    if (!tts_provider.empty())
        media::runtime_settings::merge_provider_credentials(
            tts_provider, false, tts_key, tts_base_url);
    const bool use_tts = !st.ag_mic_no_tts && !tts_provider.empty() && !tts_key.empty();

    const bool use_elevenlabs_stt = (stt_provider == "elevenlabs");
    const bool use_whisper_stt    = !use_elevenlabs_stt; // pixlwiz / other batch

    // Whisper is batch (full utterance → REST call after silence), so a short VAD
    // window easily truncates mid-sentence.  Clamp to 2500 ms for batch providers.
    const int vad_silence_ms = use_whisper_stt
        ? std::max(st.ag_mic_silence_ms, 2500)
        : st.ag_mic_silence_ms;

    // ── thread-safe queues ────────────────────────────────────────────────────
    std::mutex              prompt_mtx;
    std::condition_variable prompt_cv;
    std::queue<std::string> prompt_queue;   // committed utterances → LLM

    std::mutex              print_mtx;
    std::queue<std::string> print_queue;    // messages for main thread to display
    std::string             last_partial;

    // Gate: suppress STT while speakers are active to avoid echo.
    std::atomic<bool> tts_playing{false};

    // ── VAD state (audio thread only) ─────────────────────────────────────────
    constexpr double k_vad_rms = 200.0;
    bool vad_has_speech    = false;
    bool vad_committed     = false;
    auto vad_last_speech   = clk::now();

    // ── ElevenLabs STT session (real-time WebSocket) ──────────────────────────
    std::unique_ptr<pm::stt::ElevenLabsSTT> el_stt;
    if (use_elevenlabs_stt) {
        el_stt = std::make_unique<pm::stt::ElevenLabsSTT>();

        el_stt->on_partial = [&](const std::string& t) {
            std::lock_guard<std::mutex> lk(print_mtx);
            last_partial = t;
        };

        std::string last_committed_text;
        el_stt->on_committed = [&, last_committed_text](const std::string& t) mutable {
            std::string segment;
            {
                std::lock_guard<std::mutex> lk(print_mtx);
                if (t.empty()) return;
                if (t == last_committed_text) return;
                if (t.size() > last_committed_text.size() &&
                    t.compare(0, last_committed_text.size(), last_committed_text) == 0) {
                    segment = t.substr(last_committed_text.size());
                    const auto p = segment.find_first_not_of(" \t");
                    if (p != std::string::npos && p > 0) segment = segment.substr(p);
                } else {
                    segment = t;
                }
                last_committed_text = t;
                if (segment.empty()) return;
                last_partial.clear();
                print_queue.push("[you] " + segment);
            }
            {
                std::lock_guard<std::mutex> lk(prompt_mtx);
                prompt_queue.push(segment);
                prompt_cv.notify_one();
            }
        };

        el_stt->on_error = [&](const std::string& e) {
            std::lock_guard<std::mutex> lk(print_mtx);
            print_queue.push("[stt error] " + e);
        };

        std::cout << "llm agent mic: connecting to ElevenLabs STT...\n" << std::flush;
        pm::stt::ElevenLabsSTT::Config scfg;
        scfg.api_key     = stt_key;
        scfg.sample_rate = 16000;

        std::string       connect_err;
        std::atomic<bool> connect_done{false};
        std::thread connect_thread([&]{
            try { el_stt->connect(scfg); }
            catch (const std::exception& ex) { connect_err = ex.what(); }
            connect_done.store(true);
        });
        while (!connect_done.load() && !media::cli::cancel_requested())
            std::this_thread::sleep_for(chr::milliseconds(100));
        if (media::cli::cancel_requested()) {
            el_stt->close();
            connect_thread.join();
            return 130;
        }
        connect_thread.join();
        if (!connect_err.empty()) {
            std::cerr << "llm agent mic: STT connect failed: " << connect_err << "\n";
            return 1;
        }
        std::cout << "llm agent mic: STT ready\n" << std::flush;
    } else {
        std::cout << "llm agent mic: Whisper STT ready (batch — transcribes on silence)\n"
                  << std::flush;
    }

    // ── PCM buffer for Whisper batch path ─────────────────────────────────────
    std::mutex           pcm_buf_mtx;
    std::vector<int16_t> pcm_buf;

    // Shared helper: enqueue a prompt as if it came from STT.
    auto enqueue_prompt_text = [&](const std::string& text) {
        if (text.empty()) return;
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            last_partial.clear();
            print_queue.push("[you] " + text);
        }
        {
            std::lock_guard<std::mutex> lk(prompt_mtx);
            prompt_queue.push(text);
            prompt_cv.notify_one();
        }
    };

    auto transcribe_whisper_snapshot = [&](std::vector<int16_t> snap) {
        if (snap.empty()) return;
        std::thread([snap = std::move(snap),
                     stt_base_url, stt_key,
                     eff_model = stt_model.empty()
                         ? "pixlwiz-speech-to-text" : stt_model,
                     &print_mtx, &print_queue,
                     &enqueue_prompt_text]() mutable {
            const std::string tmp =
                (std::filesystem::temp_directory_path()
                 / "pmi_agent_stt.wav").string();
            try {
                if (!pm::stt::write_pcm_wav(tmp,
                        snap.data(), snap.size(), 16000))
                    throw std::runtime_error("could not write temp WAV");
                const std::string text = pm::stt::proxy_stt_transcribe(
                    tmp, stt_base_url, stt_key, eff_model);
                if (text.empty()) return;
                enqueue_prompt_text(text);
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lk(print_mtx);
                print_queue.push(
                    std::string("[stt error] ") + ex.what());
            }
        }).detach();
    };

    // Enter-to-commit for voice:
    // - ElevenLabs STT: send commit frame (flush current utterance now)
    // - Whisper batch STT: snapshot current PCM and transcribe immediately
    auto force_commit_stt = [&]() {
        if (use_elevenlabs_stt && el_stt) {
            el_stt->commit();
            return;
        }
        if (use_whisper_stt) {
            std::vector<int16_t> snap;
            {
                std::lock_guard<std::mutex> lk(pcm_buf_mtx);
                snap = std::move(pcm_buf);
                pcm_buf.clear();
            }
            transcribe_whisper_snapshot(std::move(snap));
        }
    };

    // Manual Enter handling (line buffered stdin):
    // - empty line + Enter => force commit current spoken buffer
    // - non-empty line + Enter => typed prompt send
    std::thread manual_input_thread([&] {
        while (!media::cli::cancel_requested()) {
            std::string line;
            if (!std::getline(std::cin, line))
                break; // EOF / stdin closed
            const auto b = line.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) {
                force_commit_stt();
                continue;
            }
            const auto e = line.find_last_not_of(" \t\r\n");
            line = line.substr(b, e - b + 1);
            enqueue_prompt_text(line);
        }
    });
    manual_input_thread.detach();

    // ── mic capture + VAD ─────────────────────────────────────────────────────
    auto on_pcm = [&](const int16_t* data, size_t frames) {
        if (tts_playing.load()) {
            vad_has_speech  = false;
            vad_committed   = false;
            vad_last_speech = clk::now();
            if (use_whisper_stt) {
                std::lock_guard<std::mutex> lk(pcm_buf_mtx);
                pcm_buf.clear(); // discard echo
            }
            return;
        }

        if (use_elevenlabs_stt && el_stt) {
            el_stt->send_pcm(data, frames);
        } else if (use_whisper_stt) {
            std::lock_guard<std::mutex> lk(pcm_buf_mtx);
            pcm_buf.insert(pcm_buf.end(), data, data + frames);
        }

        if (vad_silence_ms > 0 && frames > 0) {
            double sum = 0.0;
            for (size_t i = 0; i < frames; ++i)
                sum += static_cast<double>(data[i]) * data[i];
            const double rms = std::sqrt(sum / static_cast<double>(frames));
            const auto   now = clk::now();
            if (rms >= k_vad_rms) {
                vad_last_speech = now;
                if (!vad_has_speech) { vad_has_speech = true; vad_committed = false; }
            } else if (vad_has_speech && !vad_committed) {
                const auto silent = chr::duration_cast<chr::milliseconds>(
                    now - vad_last_speech).count();
                if (silent >= vad_silence_ms) {
                    vad_committed = true; vad_has_speech = false;

                    if (use_elevenlabs_stt && el_stt) {
                        el_stt->commit();
                    } else if (use_whisper_stt) {
                        // Snapshot PCM buffer and transcribe in a background thread.
                        std::vector<int16_t> snap;
                        {
                            std::lock_guard<std::mutex> lk(pcm_buf_mtx);
                            snap = std::move(pcm_buf);
                            pcm_buf.clear();
                        }
                        transcribe_whisper_snapshot(std::move(snap));
                    }
                }
            }
        }
    };

    pm::audio::AudioInput mic;
    try { mic.start(on_pcm, st.ag_mic_input); }
    catch (const std::exception& ex) {
        std::cerr << "llm agent mic: " << ex.what() << "\n";
        if (el_stt) el_stt->close();
        return 1;
    }

    // ── banner ────────────────────────────────────────────────────────────────
    const std::string stt_label = stt_provider + "/"
        + (stt_model.empty() ? (use_elevenlabs_stt ? "Scribe v2 Realtime" : "pixlwiz-speech-to-text") : stt_model);
    std::string vad_label;
    if (vad_silence_ms <= 0) {
        vad_label = "  vad=off";
    } else {
        vad_label = "  vad=" + std::to_string(vad_silence_ms) + " ms";
        if (use_whisper_stt && st.ag_mic_silence_ms < 2500)
            vad_label += " (auto-raised for batch STT)";
    }
    std::cout << "llm agent (mic)\n"
              << "  device  : " << mic.opened_device_name() << "\n"
              << "  stt     : " << stt_label << vad_label << "\n"
              << "  llm     : " << prov.router << "  " << prov.model << "\n";
    std::string session_task_id;
    if (st.ag_multi_turn) {
        session_task_id = media::llm::agent::session_get_or_create(st.ag_session_id);
        std::cout << "  memory  : on (" << session_task_id << ")\n";
    } else {
        std::cout << "  memory  : off\n";
    }
    if (use_tts)
        std::cout << "  tts     : " << tts_provider
                  << (tts_model.empty()    ? "" : "  model=" + tts_model)
                  << (tts_voice_id.empty() ? "" : "  voice=" + tts_voice_id) << "\n";
    std::cout << "  enter   : empty line commits current speech now; typed text + Enter sends text\n"
              << "  stop    : Ctrl+C\n" << std::flush;

    // ── TTS config ────────────────────────────────────────────────────────────
    pm::tts::ElevenLabsTTSConfig el_tts_cfg;
    el_tts_cfg.api_key  = tts_key;
    if (!tts_model.empty())    el_tts_cfg.model_id  = tts_model;
    if (!tts_voice_id.empty()) el_tts_cfg.voice_id  = tts_voice_id;

    pm::audio::AudioOutput speaker;

    // Drain pending display messages onto stdout.
    auto drain_print = [&] {
        std::lock_guard<std::mutex> lk(print_mtx);
        while (!print_queue.empty()) {
            std::cerr << "\r" << std::string(80, ' ') << "\r"; // clear partial line
            std::cout << print_queue.front() << "\n" << std::flush;
            print_queue.pop();
        }
    };

    // ── conversation loop ─────────────────────────────────────────────────────
    while (!media::cli::cancel_requested()) {
        std::string prompt;
        {
            std::unique_lock<std::mutex> lk(prompt_mtx);
            prompt_cv.wait_for(lk, chr::milliseconds(200),
                [&]{ return !prompt_queue.empty() || media::cli::cancel_requested(); });
            if (prompt_queue.empty()) {
                drain_print();
                // Show partial transcript in-place while listening.
                {
                    std::lock_guard<std::mutex> plk(print_mtx);
                    if (!last_partial.empty()) {
                        auto preview = last_partial.substr(0, 70);
                        std::cerr << "\r  [" << preview
                                  << (last_partial.size() > 70 ? "..." : "") << "]   "
                                  << std::flush;
                    }
                }
                continue;
            }
            prompt = std::move(prompt_queue.front());
            prompt_queue.pop();
        }

        drain_print(); // flush "[you] ..." before agent output

        // Run one LLM turn with the spoken prompt.
        auto turn        = base_turn;
        turn.user_prompt = prompt;
        if (st.ag_multi_turn && !session_task_id.empty()) {
            nlohmann::json mem_state, recent_evs;
            media::llm::agent::session_load(session_task_id, mem_state, recent_evs, 20);
            turn.task_id       = session_task_id;
            turn.memory_state  = std::move(mem_state);
            turn.recent_events = std::move(recent_evs);
        }

        auto agent_cb = [&](const media::llm::agent::Event& e) -> bool {
            using K = media::llm::agent::Event::Kind;
            switch (e.kind) {
            case K::LlmRound: {
                // Log compact per-round telemetry so it's visible without --log-level trace.
                std::string line = "[llm] round=" + std::to_string(e.payload.value("round", 0));
                if (e.payload.value("stateful", false))
                    line += " stateful";
                if (e.payload.contains("tokens") && e.payload["tokens"].is_object()) {
                    const auto& t = e.payload["tokens"];
                    if (t.contains("input"))
                        line += " in=" + t["input"].dump();
                    if (t.contains("output"))
                        line += " out=" + t["output"].dump();
                    if (t.contains("reasoning"))
                        line += " reasoning=" + t["reasoning"].dump();
                }
                if (e.payload.contains("model") && e.payload["model"].is_string())
                    line += " model=" + e.payload["model"].get<std::string>();
                logger::info(line);
                break;
            }
            case K::Thinking: {
                logger::info(std::string("[thinking] ") + e.text);
                break;
            }
            case K::ToolCall:
                std::cerr << "\r  [tool] " << e.tool_name << "                    \n";
                break;
            case K::Error:
                std::cerr << "[agent error] " << e.text << "\n";
                break;
            default: break;
            }
            return !media::cli::cancel_requested();
        };

        auto result = media::llm::agent::run_turn(turn, prov, agent_cb);
        append_session_turn_summary_if_enabled(st, session_task_id, turn.user_prompt, result);
        if (result.cancelled) break;

        if (!result.final_text.empty()) {
            std::cout << "[agent] " << result.final_text << "\n" << std::flush;

            if (use_tts && !media::cli::cancel_requested()) {
                try {
                    std::vector<uint8_t> audio;
                    if (tts_provider == "pixlwiz") {
                        const std::string eff_model =
                            tts_model.empty() ? "pixlwiz-speech" : tts_model;
                        audio = pm::tts::proxy_tts_synthesize(
                            result.final_text, tts_base_url, tts_key, eff_model);
                    } else {
                        audio = pm::tts::elevenlabs_tts_synthesize(
                            result.final_text, el_tts_cfg);
                    }
                    if (!audio.empty() && !media::cli::cancel_requested()) {
                        tts_playing.store(true);
                        std::atomic<bool> play_done{false};
                        std::thread pt([&]{
                            speaker.play_sync(audio.data(), audio.size());
                            play_done.store(true);
                        });
                        // Safety timeout: never keep STT gated forever if audio playback stalls.
                        const auto play_deadline = clk::now() + chr::seconds(30);
                        while (!play_done.load() && !media::cli::cancel_requested()
                               && clk::now() < play_deadline)
                            std::this_thread::sleep_for(chr::milliseconds(30));
                        if (!play_done.load()) speaker.stop();
                        pt.join();
                        tts_playing.store(false);
                    }
                } catch (const std::exception& ex) {
                    tts_playing.store(false);
                    std::cerr << "[tts error] " << ex.what() << "\n";
                }
            }
        }
    }

    mic.stop();
    if (el_stt) el_stt->close();
    std::cerr << "\rllm agent mic: stopped                \n";
    return media::cli::cancel_requested() ? 130 : 0;
}

#endif // FEATURE_STT

int pm_image_cmd_llm_agent(CLI::App& app, PmImageCliState& st) {
    (void)app;

#if !FEATURE_HOME_LLM_TOOLS
    st.ag_no_tools = true;
    st.ag_disable_tools.clear();
#endif

    // Recovery for `--disable-tools --flag`: if a flag-shaped token is consumed
    // as the disable-tools "value", treat it as "no tool ids provided".
    // In that case disable all tools (same as --no-tools) and recover common
    // mode flags that users often place after --disable-tools.
    if (!st.ag_disable_tools.empty() && st.ag_disable_tools.rfind("--", 0) == 0) {
        const std::string swallowed_flag = st.ag_disable_tools;
        st.ag_disable_tools.clear();
        st.ag_no_tools = true;
        if (swallowed_flag == "--single-turn")
            st.ag_multi_turn = false;
        else if (swallowed_flag == "--multi-turn")
            st.ag_multi_turn = true;
        if (swallowed_flag == "--single-turn" || swallowed_flag == "--multi-turn")
            st.ag_multi_turn_explicit = true;
    }

    // --cwd is the folder context for the agent (processed globally in dispatch)
    std::string folder_context = st.cwd;

#if defined(FEATURE_STT) && FEATURE_STT
    // Mic mode: --prompt is used as optional system context, not the user prompt.
    if (st.ag_mic) {
        // Resolve provider settings first (same waterfall as normal mode).
        media::llm::agent::ProviderSettings prov;
        {
            std::string cs_err;
            media::runtime_settings::ChatProviderSettings cs;
            media::runtime_settings::load_chat_provider(cs, cs_err);
            prov.router         = st.ag_router.empty()   ? cs.router         : st.ag_router;
            prov.model          = st.ag_model.empty()    ? cs.model          : st.ag_model;
            prov.base_url       = st.ag_base_url;
            prov.api_key        = st.ag_api_key;
            prov.timeout_ms     = st.ag_timeout_ms > 0   ? st.ag_timeout_ms  : cs.timeout_ms;
            prov.max_iterations = st.ag_max_iter   > 0   ? st.ag_max_iter    : cs.max_iterations;
            if (prov.timeout_ms <= 0)        prov.timeout_ms     = 180'000;
            if (!st.ag_no_tools && prov.max_iterations <= 0) prov.max_iterations = 8;
        }
        if (st.ag_no_tools) prov.max_iterations = 0;
        prov.api_mode = (st.ag_api_mode == "responses")
            ? media::llm::agent::LlmApiMode::Responses
            : (st.ag_api_mode == "realtime")
                ? media::llm::agent::LlmApiMode::Realtime
                : media::llm::agent::LlmApiMode::ChatCompletion;
        prov.streaming_mode = (st.ag_streaming == "on")
            ? media::llm::agent::LlmStreamingMode::On
            : (st.ag_streaming == "off")
                ? media::llm::agent::LlmStreamingMode::Off
                : media::llm::agent::LlmStreamingMode::Auto;
        media_cli::fill_chat_llm_credentials_from_app_settings(prov.api_key, prov.base_url, prov.router);

        if (prov.api_key.empty()) {
            std::cerr << "llm agent mic: no LLM API key. Pass --api-key or configure app settings.\n";
            return 1;
        }

        media::llm::agent::Turn base_turn;
        base_turn.system_extra = st.ag_prompt; // --prompt = optional system context
        base_turn.selection    = st.ag_paths;
        base_turn.folder_hint  = folder_context;
        base_turn.godmode      = st.ag_godmode;
        media_cli::parse_agent_disable_tools(st.ag_disable_tools, st.ag_no_tools, base_turn.disabled_path_tools);

        return cmd_llm_agent_mic(st, std::move(base_turn), prov);
    }
#endif // FEATURE_STT

    // Normal (text) mode — prompt can come from --prompt or piped stdin.
    if (st.ag_prompt.empty() && !cli_tty::stdin_is_tty()) {
        std::ostringstream buf;
        buf << std::cin.rdbuf();
        st.ag_prompt = buf.str();
        // Trim trailing whitespace to avoid accidental empty prompt/newline-only pipe.
        while (!st.ag_prompt.empty() && std::isspace(static_cast<unsigned char>(st.ag_prompt.back())))
            st.ag_prompt.pop_back();
    }
    if (st.ag_prompt.empty()) {
        std::cerr << "llm agent: --prompt is required (or pipe prompt via stdin, or use --mic for voice input)\n";
        return 1;
    }

    // Default log path to agent.json in cwd if not specified
    if (st.ag_log_path.empty()) {
        st.ag_log_path = "agent.json";
    }

    // Validate and resolve --include paths and --folder (must exist).
    std::error_code ec;
    for (auto& p : st.ag_paths) {
        fs::path fp(p);
        if (!fp.is_absolute()) {
            fp = fs::absolute(fp, ec);
        }
        if (ec || !fs::exists(fp)) {
            logger::error(std::string("llm agent: --include path does not exist: ") + p);
            return 1;
        }
        p = fp.string();
    }
    // Validate folder_context exists (comes from --cwd which is already applied)
    if (!folder_context.empty()) {
        fs::path fp(folder_context);
        if (!fp.is_absolute()) {
            fp = fs::absolute(fp, ec);
        }
        if (ec || !fs::exists(fp)) {
            logger::error(std::string("llm agent: --cwd folder does not exist: ") + folder_context);
            return 1;
        }
        folder_context = fp.string();
    }

    media::llm::agent::Turn turn;
    turn.user_prompt = st.ag_prompt;
    turn.selection   = st.ag_paths;
    turn.folder_hint = folder_context;
    turn.godmode     = st.ag_godmode;
    media_cli::parse_agent_disable_tools(st.ag_disable_tools, st.ag_no_tools, turn.disabled_path_tools);

    std::string session_task_id;
    // Open a session whenever multi-turn is on, OR the user explicitly named
    // one via --session-id (so single-turn invocations sharing an id read
    // each other's persisted memory).
    if (st.ag_multi_turn || !st.ag_session_id.empty()) {
        session_task_id = media::llm::agent::session_get_or_create(st.ag_session_id);
        nlohmann::json mem_state, recent_evs;
        media::llm::agent::session_load(session_task_id, mem_state, recent_evs, 20);
        turn.task_id       = session_task_id;
        turn.memory_state  = std::move(mem_state);
        turn.recent_events = std::move(recent_evs);
    }

    // Base: load Chat Provider Settings from the app config store (Windows only).
    // CLI args overlay on top of whatever is saved there.
    media::llm::agent::ProviderSettings prov;
    {
        std::string cs_err;
        media::runtime_settings::ChatProviderSettings cs;
        media::runtime_settings::load_chat_provider(cs, cs_err);  // best-effort

        prov.router         = st.ag_router.empty()   ? cs.router         : st.ag_router;
        prov.model          = st.ag_model.empty()    ? cs.model          : st.ag_model;
        prov.base_url       = st.ag_base_url;
        prov.api_key        = st.ag_api_key;  // waterfall fills this below
        prov.timeout_ms     = st.ag_timeout_ms > 0   ? st.ag_timeout_ms     : cs.timeout_ms;
        prov.max_iterations = st.ag_max_iter   > 0   ? st.ag_max_iter       : cs.max_iterations;
        // Final guard: only timeout has a hard fallback (others error if unset).
        if (prov.timeout_ms <= 0)        prov.timeout_ms     = 180'000;
        if (!st.ag_no_tools && prov.max_iterations <= 0) prov.max_iterations = 8;
    }
    if (st.ag_no_tools) {
        // Drives `agent.cpp`: `max_iterations > 0` is required to pass OpenAI tool list.
        prov.max_iterations = 0;
    }

    prov.api_mode = (st.ag_api_mode == "responses")
        ? media::llm::agent::LlmApiMode::Responses
        : (st.ag_api_mode == "realtime")
            ? media::llm::agent::LlmApiMode::Realtime
            : media::llm::agent::LlmApiMode::ChatCompletion;
    prov.streaming_mode = (st.ag_streaming == "on")
        ? media::llm::agent::LlmStreamingMode::On
        : (st.ag_streaming == "off")
            ? media::llm::agent::LlmStreamingMode::Off
            : media::llm::agent::LlmStreamingMode::Auto;

    media_cli::fill_chat_llm_credentials_from_app_settings(prov.api_key, prov.base_url, prov.router);

    auto agent_log_utc_now = []() -> std::string {
        std::time_t t = std::time(nullptr);
#if defined(_WIN32)
        std::tm tm_buf{};
        if (gmtime_s(&tm_buf, &t) != 0) return {};
        const std::tm* g = &tm_buf;
#else
        const std::tm* g = std::gmtime(&t);
#endif
        if (!g) return {};
        char buf[40];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", g);
        return buf;
    };

    auto agent_append_log_event = [](nlohmann::json& events,
                                     const media::llm::agent::Event& e) {
        nlohmann::json j;
        using K = media::llm::agent::Event::Kind;
        switch (e.kind) {
        case K::TurnStarted:
            j["kind"] = "turn_started";
            j["detail"] = e.payload;
            break;
        case K::LlmRound:
            j["kind"] = "llm_round";
            j["detail"] = e.payload;  // {round, stateful, response_id?, tokens, model}
            break;
        case K::ToolCall:
            j["kind"] = "tool_call";
            j["tool"] = e.tool_name;
            j["detail"] = e.payload;
            break;
        case K::ToolResult:
            j["kind"] = "tool_result";
            j["tool"] = e.tool_name;
            j["detail"] = e.payload;  // includes full envelope
            break;
        case K::ToolFileProgress:
            j["kind"] = "tool_file_progress";
            j["tool"] = e.tool_name;
            j["detail"] = e.payload;  // single-file envelope during image_transform
            break;
        case K::Thinking:
            j["kind"] = "thinking";
            j["text"] = e.text;
            break;
        case K::AssistantText:
            j["kind"] = "assistant_text";
            j["text"] = e.text;
            break;
        case K::Error:
            j["kind"] = "error";
            j["text"] = e.text;
            break;
        case K::Done:
            j["kind"] = "done";
            j["text"] = e.text;
            break;
        default:
            j["kind"] = "unknown";
            break;
        }
        events.push_back(std::move(j));
    };

    auto write_agent_log = [&](const nlohmann::json& root) {
        if (st.ag_log_path.empty()) return;
        std::ofstream f(st.ag_log_path, std::ios::out | std::ios::trunc);
        if (!f) {
            std::cerr << "llm agent: could not open --log file: " << st.ag_log_path << "\n";
            return;
        }
        f << root.dump(2) << "\n";
    };

    if (st.ag_dry) {
        const std::string sys = media::llm::agent::build_system_prompt(turn);
        const std::string api_mode_str =
            prov.api_mode == media::llm::agent::LlmApiMode::Responses
                ? "responses (`/v1/responses`)"
                : (prov.api_mode == media::llm::agent::LlmApiMode::Realtime
                    ? "realtime (experimental WebSocket `/v1/realtime`, text-only)"
                    : "completion (`/v1/chat/completions`)");
        const std::string stream_mode_str =
            prov.streaming_mode == media::llm::agent::LlmStreamingMode::On  ? "on" :
            prov.streaming_mode == media::llm::agent::LlmStreamingMode::Off ? "off" : "auto";

        std::ostringstream md;
        md << "# llm agent — dry run\n\n";

        // ── Provider ─────────────────────────────────────────────────────────
        md << "## Provider\n\n";
        md << "| Setting | Value |\n";
        md << "| :------ | :---- |\n";
        md << "| router | `" << (prov.router.empty() ? "(default)" : prov.router) << "` |\n";
        md << "| model | `"  << (prov.model.empty()  ? "(default)" : prov.model)  << "` |\n";
        md << "| base_url | "
           << (prov.base_url.empty() ? "*(router default)*" : "`" + prov.base_url + "`")
           << " |\n";
        md << "| api_key | " << (prov.api_key.empty() ? "**missing**" : "set") << " |\n";
        md << "| api_mode | " << api_mode_str << " |\n";
        md << "| streaming | `" << stream_mode_str << "` |\n";
        md << "| timeout | " << prov.timeout_ms << " ms |\n";
        md << "| max_iterations | " << prov.max_iterations << " |\n";
        md << "\n";

        // ── Context ───────────────────────────────────────────────────────────
        md << "## Context\n\n";
        md << "| Setting | Value |\n";
        md << "| :------ | :---- |\n";
        md << "| folder | `" << (turn.folder_hint.empty() ? "(none)" : turn.folder_hint) << "` |\n";
        md << "| included files | " << turn.selection.size() << " |\n";
        md << "\n";
        if (!turn.selection.empty()) {
            for (const auto& p : turn.selection)
                md << "- `" << p << "`\n";
            md << "\n";
        }

        // ── Tools ─────────────────────────────────────────────────────────────
        if (turn.godmode)
            md << "**\xe2\x9a\xa0\xef\xb8\x8f GODMODE active** \xe2\x80\x94 all filesystem safety guards are bypassed.\n\n";

        md << "## Tools\n\n";
#if !FEATURE_HOME_LLM_TOOLS
        md << "*Built-in path tools and MCP tools are disabled in this build "
              "(`FEATURE_HOME_LLM_TOOLS=OFF`).*\n\n";
#else
        if (st.ag_no_tools) {
            md << "*All path tools disabled via `--no-tools`.*\n\n";
        } else if (!st.ag_disable_tools.empty()) {
            if (!turn.disabled_path_tools.empty()) {
                md << "**Disabled:**\n\n";
                for (const auto& d : turn.disabled_path_tools)
                    md << "- `" << d << "`\n";
                md << "\n";
            } else {
                md << "*`--disable-tools` value `" << st.ag_disable_tools
                   << "` did not match any known tool — full catalog active.*\n\n";
            }
        } else {
            md << "*Full catalog enabled.*\n\n";
        }
#endif

        // ── Skills ────────────────────────────────────────────────────────────
#if FEATURE_HOME_LLM_SKILLS
        const auto snap = load_skill_snapshot_for_cwd(turn.folder_hint);
        md << "## Skills\n\n";
        md << "| Root | Path |\n";
        md << "| :--- | :--- |\n";
        md << "| roaming | `" << snap.roaming_root.string() << "` |\n";
        md << "| workspace | `" << snap.workspace_root.string() << "` |\n\n";
        md << "| Name | Source | Available | Active |\n";
        md << "| :--- | :--- | :---: | :---: |\n";
        if (snap.entries.empty()) {
            md << "| *(none)* |  |  |  |\n\n";
        } else {
            for (const auto& e : snap.entries) {
                md << "| `" << e.name << "` | "
                   << (e.source == media::llm::skills::SkillSource::Roaming ? "roaming" : "workspace")
                   << " | " << (e.available ? "✓" : "✗")
                   << " | " << (e.active ? "✓" : "✗")
                   << " |\n";
            }
            md << "\n";
        }
#else
        md << "## Skills\n\n";
        md << "*Skills are disabled in this build (`FEATURE_HOME_LLM_SKILLS=OFF`).*\n\n";
#endif

        // ── Render metadata (tables / lists) through the markdown renderer ───
        const bool want_render =
            st.ag_markdown == "plain"  ? false :
            st.ag_markdown == "render" ? true  :
            cli_tty::stdout_is_tty();
        const bool use_color =
            st.ag_color == "never"  ? false :
            st.ag_color == "always" ? true  :
            cli_tty::stdout_is_tty() && !cli_tty::env_requests_plain_output();
#if defined(_WIN32)
        if (want_render && use_color)
            cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif
        // ANSI helpers (no-ops when color is off)
        const char* bold  = use_color ? "\033[1m"  : "";
        const char* dim   = use_color ? "\033[2m"  : "";
        const char* reset = use_color ? "\033[0m"  : "";
        const char* rule  = use_color ? "\033[2m"  : "";

        {
            std::string meta = md.str();
            if (want_render) {
                html::MdOptions mdopts{};
                int w = cli_tty::terminal_width_columns();
                if (w <= 0) w = 80;
                meta = html::markdown_to_terminal(meta, mdopts, use_color, w);
            }
            std::cout << meta;
            if (meta.empty() || meta.back() != '\n') std::cout << '\n';
        }

        // ── Prompts printed verbatim — the system prompt is prose, not markdown ─
        auto print_rule = [&]() {
            int w = cli_tty::terminal_width_columns();
            if (w <= 0 || w > 220) w = 72;
            std::cout << rule;
            for (int i = 0; i < w; ++i) std::cout << '-';
            std::cout << reset << '\n';
        };

        print_rule();
        std::cout << bold << "System prompt" << reset << "\n\n";
        std::cout << dim << sys << reset << "\n";

        print_rule();
        std::cout << bold << "User prompt" << reset << "\n\n";
        std::cout << turn.user_prompt << "\n";

        if (!st.ag_log_path.empty()) {
            nlohmann::json paths = nlohmann::json::array();
            for (const auto& p : turn.selection) paths.push_back(p);
            nlohmann::json logj{
                {"schema_version", "1"},
                {"generated_at",   agent_log_utc_now()},
                {"command",        "llm agent"},
                {"dry_run",        true},
                {"cli", {
                    {"prompt", st.ag_prompt},
                    {"paths",  std::move(paths)},
                    {"folder", turn.folder_hint},
                    {"no_tools", st.ag_no_tools},
                    {"multi_turn", st.ag_multi_turn},
                    {"session_id", session_task_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(session_task_id)},
                    {"godmode", st.ag_godmode},
                    {"disable_tools", st.ag_disable_tools.empty() ? nlohmann::json(nullptr) : nlohmann::json(st.ag_disable_tools)},
                    {"json_stdout", st.ag_json},
                }},
                {"provider", {
                    {"router", prov.router},
                    {"model", prov.model},
                    {"base_url", prov.base_url.empty()
                        ? nlohmann::json(nullptr) : nlohmann::json(prov.base_url)},
                    {"timeout_ms", prov.timeout_ms},
                    {"max_iterations", prov.max_iterations},
                    {"api_mode", prov.api_mode == media::llm::agent::LlmApiMode::Responses ? "responses"
                                  : prov.api_mode == media::llm::agent::LlmApiMode::Realtime ? "realtime"
                                  : "completion"},
                    {"streaming", prov.streaming_mode == media::llm::agent::LlmStreamingMode::On ? "on"
                                  : prov.streaming_mode == media::llm::agent::LlmStreamingMode::Off ? "off"
                                  : "auto"},
                    {"api_key_configured", !prov.api_key.empty()},
                }},
                {"turn", {
                    {"user_prompt", turn.user_prompt},
                    {"selection", nlohmann::json(turn.selection)},
                    {"folder_hint", turn.folder_hint},
                    {"disabled_path_tools", nlohmann::json(turn.disabled_path_tools)},
                    {"godmode", turn.godmode},
                }},
                {"system_prompt", sys},
            };
            write_agent_log(logj);
        }
        return 0;
    }

    if (prov.api_key.empty()) {
        if (!st.ag_log_path.empty()) {
            nlohmann::json paths = nlohmann::json::array();
            for (const auto& p : turn.selection) paths.push_back(p);
            write_agent_log(nlohmann::json{
                {"schema_version", "1"},
                {"generated_at", agent_log_utc_now()},
                {"command", "llm agent"},
                {"error", "no_api_key"},
                {"message", "Pass --api-key or configure Chat / provider API keys in app settings."},
                {"cli", {{"prompt", st.ag_prompt},
                         {"paths", paths},
                         {"folder", turn.folder_hint},
                         {"no_tools", st.ag_no_tools},
                         {"godmode", st.ag_godmode},
                         {"disable_tools", st.ag_disable_tools.empty() ? nlohmann::json(nullptr) : nlohmann::json(st.ag_disable_tools)}}},
                {"provider", {{"router", prov.router}, {"model", prov.model}}},
            });
        }
        std::cerr << "llm agent: no API key supplied. Pass --api-key, or set the key in app settings "
                     "(Chat provider and/or API Keys / providers in settings.json).\n";
        return 1;
    }

    const bool interactive_multi_turn =
        st.ag_multi_turn_explicit
        && st.ag_multi_turn
        && !st.ag_json
        && cli_tty::stdout_is_tty();

    std::string current_prompt = turn.user_prompt;
    if (interactive_multi_turn) {
        const std::string mode =
            prov.api_mode == media::llm::agent::LlmApiMode::Realtime ? "realtime"
            : prov.api_mode == media::llm::agent::LlmApiMode::Responses ? "responses"
            : "completion";
        std::cerr << "llm agent: " << mode
                  << " multi-turn interactive mode (Enter prompt, Ctrl+C to exit)\n";
    }

    for (;;) {
        turn.user_prompt = current_prompt;
        if (st.ag_multi_turn && !session_task_id.empty()) {
            nlohmann::json mem_state, recent_evs;
            media::llm::agent::session_load(session_task_id, mem_state, recent_evs, 20);
            turn.task_id       = session_task_id;
            turn.memory_state  = std::move(mem_state);
            turn.recent_events = std::move(recent_evs);
        }

        bool saw_text_delta = false;
        nlohmann::json log_events = nlohmann::json::array();

        // Stream events to stderr in human-readable form unless --json is set.
        media::llm::agent::EventCallback cb_stderr;
        if (!st.ag_json) {
            cb_stderr = [&](const media::llm::agent::Event& e) -> bool {
                using K = media::llm::agent::Event::Kind;
                switch (e.kind) {
                    case K::TurnStarted:
                        logger::debug("-> turn started");
                        break;
                    case K::LlmRound: {
                        std::string line = "[llm] round=" + std::to_string(e.payload.value("round", 0));
                        if (e.payload.value("stateful", false)) line += " stateful";
                        if (e.payload.contains("tokens") && e.payload["tokens"].is_object()) {
                            const auto& t = e.payload["tokens"];
                            if (t.contains("input"))     line += " in=" + t["input"].dump();
                            if (t.contains("output"))    line += " out=" + t["output"].dump();
                            if (t.contains("reasoning")) line += " reasoning=" + t["reasoning"].dump();
                        }
                        logger::info(line);
                        break;
                    }
                    case K::ToolCall:
                        logger::debug(std::string("  tool <- ") + e.tool_name + " " + e.payload.dump());
                        break;
                    case K::ToolFileProgress:
                        logger::debug(std::string("  tool (file) ") + e.tool_name + " "
                            + e.payload.value("envelope", nlohmann::json{}).dump());
                        break;
                    case K::ToolResult:
                        logger::debug(std::string("  tool ok ") + e.tool_name + " summary="
                            + e.payload.value("envelope", nlohmann::json{})
                                   .value("summary", nlohmann::json{})
                                   .dump());
                        break;
                    case K::Thinking: {
                        logger::info(std::string("[thinking] ") + e.text);
                        break;
                    }
                    case K::TextDelta:
                        saw_text_delta = true;
                        std::cout << e.text << std::flush;
                        break;
                    case K::AssistantText:
                        logger::debug(std::string("<- assistant: ") + e.text);
                        break;
                    case K::Error:
                        logger::error(std::string("llm agent: ") + e.text);
                        break;
                    case K::Done:
                        break;
                    default: break;
            }
                return true;
            };
        }

        media::llm::agent::EventCallback cb;
        if (!st.ag_log_path.empty()) {
            cb = [&, cb_stderr](const media::llm::agent::Event& e) -> bool {
                agent_append_log_event(log_events, e);
                if (cb_stderr) return cb_stderr(e);
                return true;
            };
        } else {
            cb = std::move(cb_stderr);
        }

        auto result = media::llm::agent::run_turn(turn, prov, cb);
        append_session_turn_summary_if_enabled(st, session_task_id, turn.user_prompt, result);

        nlohmann::json result_json{
            {"ok",         result.ok},
            {"final_text", result.final_text},
            {"iterations", result.iterations},
            {"transcript", result.transcript},
        };
        if (result.cancelled) result_json["cancelled"] = true;
        if (!result.error.empty()) result_json["error"] = result.error;
        if (result.llm_usage_aggregate.is_object()) {
            const int tt = result.llm_usage_aggregate.value("total_tokens", 0);
            const int pr = result.llm_usage_aggregate.value("prompt_tokens", 0);
            const int co = result.llm_usage_aggregate.value("completion_tokens", 0);
            const auto rounds = result.llm_usage_aggregate.value("llm_rounds", nlohmann::json::array());
            const bool has_cost = result.llm_usage_aggregate.contains("cost")
                                  && result.llm_usage_aggregate["cost"].is_number();
            if ((rounds.is_array() && !rounds.empty()) || tt > 0 || pr > 0 || co > 0 || has_cost)
                result_json["llm_usage"] = result.llm_usage_aggregate;
        }

        if (!st.ag_log_path.empty()) {
            nlohmann::json paths = nlohmann::json::array();
            for (const auto& p : turn.selection) paths.push_back(p);
            nlohmann::json logj{
                {"schema_version", "1"},
                {"generated_at",   agent_log_utc_now()},
                {"command",        "llm agent"},
                {"cli", {
                    {"prompt", turn.user_prompt},
                    {"paths",  std::move(paths)},
                    {"folder", turn.folder_hint},
                    {"no_tools", st.ag_no_tools},
                        {"multi_turn", st.ag_multi_turn},
                        {"session_id", session_task_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(session_task_id)},
                    {"godmode", st.ag_godmode},
                    {"disable_tools", st.ag_disable_tools.empty() ? nlohmann::json(nullptr) : nlohmann::json(st.ag_disable_tools)},
                    {"json_stdout", st.ag_json},
                }},
                {"provider", {
                    {"router", prov.router},
                    {"model", prov.model},
                    {"base_url", prov.base_url.empty()
                        ? nlohmann::json(nullptr) : nlohmann::json(prov.base_url)},
                    {"timeout_ms", prov.timeout_ms},
                    {"max_iterations", prov.max_iterations},
                    {"api_mode", prov.api_mode == media::llm::agent::LlmApiMode::Responses ? "responses"
                                  : prov.api_mode == media::llm::agent::LlmApiMode::Realtime ? "realtime"
                                  : "completion"},
                    {"streaming", prov.streaming_mode == media::llm::agent::LlmStreamingMode::On ? "on"
                                  : prov.streaming_mode == media::llm::agent::LlmStreamingMode::Off ? "off"
                                  : "auto"},
                    {"api_key_configured", true},
                }},
                {"turn", {
                    {"user_prompt", turn.user_prompt},
                    {"selection", nlohmann::json(turn.selection)},
                    {"folder_hint", turn.folder_hint},
                    {"disabled_path_tools", nlohmann::json(turn.disabled_path_tools)},
                    {"godmode", turn.godmode},
                }},
                {"events", std::move(log_events)},
                {"result", result_json},
            };
            write_agent_log(logj);
        }

        if (st.ag_json) {
            std::cout << result_json.dump(2) << "\n";
        } else if (result.ok) {
            if (!saw_text_delta) {
                bool want_render = false;
                if (st.ag_markdown == "plain")
                    want_render = false;
                else if (st.ag_markdown == "render")
                    want_render = true;
                else
                    want_render = cli_tty::stdout_is_tty();

                bool use_color = false;
                if (st.ag_color == "never")
                    use_color = false;
                else if (st.ag_color == "always")
                    use_color = true;
                else
                    use_color = cli_tty::stdout_is_tty() && !cli_tty::env_requests_plain_output();

#if defined(_WIN32)
                if (want_render && use_color)
                    cli_tty::win_enable_virtual_terminal_processing_stdout();
#endif

                std::string out = result.final_text;
                if (want_render) {
                    // Default MdOptions: GFM on (tables=true, etc.). Pipe tables → padded | grid in md_terminal.cpp.
                    html::MdOptions mdopts{};
                    int w = cli_tty::terminal_width_columns();
                    if (w <= 0)
                        w = 80;
                    out = html::markdown_to_terminal(result.final_text, mdopts, use_color, w);
                }
                std::cout << out;
                if (out.empty() || out.back() != '\n')
                    std::cout << '\n';
            } else if (result.final_text.empty() || result.final_text.back() != '\n') {
                std::cout << '\n';
            }
        } else if (result.cancelled) {
            std::cerr << "llm agent: interrupted (Ctrl+C)\n";
        }

        log_llm_usage_aggregate_debug(result.llm_usage_aggregate);

        if (result.cancelled)
            return 130;
        if (!result.ok)
            return 1;
        if (!interactive_multi_turn) {
            if (st.ag_scheduler)
                return run_scheduler_blocking(prov, st.ag_scheduler_timeout_s,
                                              st.ag_scheduler_exit_when_idle);
            return 0;
        }

        std::cout << "> " << std::flush;
        std::string next_prompt;
        if (!std::getline(std::cin, next_prompt)) {
            if (st.ag_scheduler)
                return run_scheduler_blocking(prov, st.ag_scheduler_timeout_s,
                                              st.ag_scheduler_exit_when_idle);
            return 0;
        }
        if (next_prompt.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;
        current_prompt = std::move(next_prompt);
    }
}

void pm_image_register_llm(CLI::App& app, PmImageCliState& s) {
#if FEATURE_HOME_LLM_TOOLS
    const char* llm_desc = "LLM: tools-list / tools-call / info (saved chat + image defaults) / agent (path tools)";
#else
    const char* llm_desc = "LLM: info (saved chat defaults) / agent (text-only; built-in tools disabled)";
#endif
    s.llm_cmd = app.add_subcommand("llm", llm_desc);
    s.llm_cmd->require_subcommand(1);

#if FEATURE_HOME_LLM_TOOLS
    const char* llm_info_desc =
        "Show Chat router/model and image provider/model from app settings, effective defaults "
        "for path tools, and which CLI flags override per command (find/transform/meta/duplicates, …)";
#else
    const char* llm_info_desc =
        "Show Chat router/model and configured LLM providers from app settings.";
#endif
    s.llm_info_cmd = s.llm_cmd->add_subcommand(
        "info",
        llm_info_desc);
    s.llm_info_cmd->add_flag("--json", s.llm_info_json, "Print machine-readable JSON on stdout");
    s.llm_info_cmd
        ->add_option("--markdown", s.llm_info_markdown,
                     "Human stdout: render (default — always pretty-print), auto (only on TTY), or plain (raw UTF-8). "
                     "Ignored with --json.")
        ->check(CLI::IsMember({"auto", "plain", "render"}))
        ->default_val("render");
    s.llm_info_cmd
        ->add_option("--color", s.llm_info_color,
                     "When markdown rendering is used: auto (color on TTY unless NO_COLOR/TERM=dumb), never, or always.")
        ->check(CLI::IsMember({"auto", "never", "always"}))
        ->default_val("auto");
    s.llm_info_cmd->add_flag(
        "--no-mcp-probe", s.llm_info_no_mcp_probe,
        "Do not run live MCP profile probes (stdio/HTTP handshakes). JSON/text output still lists settings; "
        "the `mcp` object notes that the probe was skipped.");
    s.llm_info_cmd->add_flag_function(
        "--mcp-probe",
        [&](std::int64_t) { s.llm_info_no_mcp_probe = false; },
        "Run live MCP profile probes (stdio/HTTP handshakes).");

    s.llm_info_providers_cmd = s.llm_info_cmd->add_subcommand(
        "providers",
        "List all LLM providers configured in app settings (name, api_key_set, base_url, default_model).");
    s.llm_info_providers_cmd->add_flag("--json", s.llm_info_json, "Output JSON array");

    s.llm_info_models_cmd = s.llm_info_cmd->add_subcommand(
        "models",
        "List models for a given provider. "
        "pixlwiz / openrouter / replicate: fetches live catalog (disk-cached; replicate 3 d, others 24 h). "
        "Other names: shows configured default_model from app settings.");
    s.llm_info_models_cmd->add_option(
        "--provider", s.llm_info_models_provider,
        "Provider to query: pixlwiz | openrouter | replicate | <name from `llm info providers`>")->required(true);
    s.llm_info_models_cmd->add_flag("--no-cache", s.llm_info_models_no_cache,
        "Bypass the disk cache and force a live HTTP fetch (pixlwiz / openrouter).");
    s.llm_info_models_cmd->add_flag("--json", s.llm_info_json, "Output JSON");

#if FEATURE_HOME_LLM_TOOLS
    s.llm_info_tools_cmd = s.llm_info_cmd->add_subcommand(
        "tools",
        "List all built-in path-mode agent tools (name + description). "
        "These are the tools available to the chat agent in every session.");
    s.llm_info_tools_cmd->add_flag("--json", s.llm_info_json, "Output JSON array {name, description}");

#endif

#if FEATURE_HOME_LLM_SKILLS
    s.llm_info_skills_cmd = s.llm_info_cmd->add_subcommand(
        "skills",
        "List discovered agent skills from roaming and workspace roots with availability/active state.");
    s.llm_info_skills_cmd->add_flag("--json", s.llm_info_json, "Output JSON");
#endif

#if FEATURE_HOME_LLM_TOOLS
    const std::string llm_tools_list_desc = std::string("Print the JSON-Schema tool catalog (one entry per ") + pm::brand::k_app_id_u8 + " op)";
    s.llm_list_cmd = s.llm_cmd->add_subcommand("tools-list", llm_tools_list_desc.c_str());
    s.llm_list_cmd->add_flag("--path", s.llm_tools_list_path,
        "List path-mode chat agent tools (default catalog for llm agent). Omit for in-buffer REST/MCP tools.");

    s.llm_call_cmd = s.llm_cmd->add_subcommand("tools-call",
        "Invoke a tool by name with a JSON arguments envelope");
    s.llm_call_cmd->add_option("--name", s.llm_call_name,
        "Tool name (image_resize|image_compress|image_transform|image_create|image_meta|image_find|file_read)")->required(true);
    s.llm_call_cmd->add_option("--args", s.llm_call_args_path,
        "Path to JSON arguments file ('-' or '@-' for stdin; omit = empty {}).");
    s.llm_call_cmd->add_option("--image-file", s.llm_call_image_file,
        "Convenience: read this file, base64-encode it, and inject as arguments.image.b64.");
#endif

#if FEATURE_HOME_LLM_TOOLS
    s.llm_agent_cmd = s.llm_cmd->add_subcommand("agent",
        "Run a single chat-agent turn: LLM picks tools (image_resize / compress / "
        "transform / meta / find), runs them on the supplied paths, and writes "
        "outputs to disk. Use --no-tools for a plain one-shot text reply (no path tools). "
        "Use --disable-tools=a,b to omit specific path tools from the catalog. "
        "Provider is router-aware (OpenAI-compatible client).");
#else
    s.llm_agent_cmd = s.llm_cmd->add_subcommand("agent",
        "Run a single text-only chat-agent turn. Built-in path tools and MCP tools "
        "are disabled in this build (FEATURE_HOME_LLM_TOOLS=OFF).");
#endif
    s.llm_agent_cmd->add_option("-p,--prompt", s.ag_prompt,
        "User prompt (required unless piped via stdin or --mic is used, e.g. 'compress these as MozJPEG quality 70')");
    s.llm_agent_cmd->add_option("--include", s.ag_paths,
        "One or more file paths to put in the agent's selection context. "
        "Repeatable. When omitted, --cwd is used as the folder context.")->expected(-1);
    s.llm_agent_cmd->add_option("--router", s.ag_router,
        "LLM router (openrouter|openai|deepseek|gemini|ollama|fireworks|xai|huggingface). "
        "Default: from Chat Provider Settings in app.");
    s.llm_agent_cmd->add_option("--model", s.ag_model,
        "Model id (router-specific, e.g. openai/gpt-4o-mini). "
        "Default: from Chat Provider Settings in app.");
    s.llm_agent_cmd->add_option("--api-key", s.ag_api_key,
        "API key (optional; default from app chat / API Keys in settings.json)");
    s.llm_agent_cmd->add_option("--base-url", s.ag_base_url,
        "Override the router's default base URL (OpenAI-compatible endpoints). "
        "Default: from app API Providers settings.");
    s.llm_agent_cmd->add_option("--timeout-ms", s.ag_timeout_ms,
        "HTTP timeout per LLM round (ms). Default: from Chat Provider Settings (or 60000).");
    s.llm_agent_cmd->add_option("--max-iter", s.ag_max_iter,
        "Maximum tool-call iterations before forcing a final response. "
        "Default: from Chat Provider Settings (or 8).");
#if FEATURE_HOME_LLM_TOOLS
    s.llm_agent_cmd->add_flag("--no-tools", s.ag_no_tools,
        "Do not register path tools (no list_images, image_resize, image_compress, …) — one LLM text turn only");
#endif
    s.llm_agent_cmd->add_flag(
        "--multi-turn", s.ag_multi_turn,
        "Enable session memory across turns (default: on).")
        ->each([&](const std::string&) { s.ag_multi_turn_explicit = true; });
    s.llm_agent_cmd->add_flag_function(
        "--single-turn",
        [&](std::int64_t) { s.ag_multi_turn = false; s.ag_multi_turn_explicit = true; },
        "Disable session memory and run as one-shot only.");
    s.llm_agent_cmd->add_option(
        "--session-id", s.ag_session_id,
        "Optional session id used when --multi-turn is enabled (allows continuity across CLI invocations).");
    s.llm_agent_cmd->add_flag(
        "--scheduler", s.ag_scheduler,
        "After the first turn completes, start the agent scheduler in this "
        "process so schedule_every / schedule_in / schedule_at tasks created "
        "by the agent actually fire. Blocks until Ctrl+C, --scheduler-timeout "
        "expires, or --scheduler-exit-when-idle and no tasks remain.");
    s.llm_agent_cmd->add_option(
        "--scheduler-timeout", s.ag_scheduler_timeout_s,
        "When --scheduler is set, auto-exit after N seconds (0 = run until Ctrl+C / idle).")
        ->default_val(0);
    s.llm_agent_cmd->add_flag(
        "--scheduler-exit-when-idle", s.ag_scheduler_exit_when_idle,
        "When --scheduler is set, exit cleanly once every scheduled task is "
        "disabled (one-shots done, every-tasks hit max_runs / cancelled).");
#if FEATURE_HOME_LLM_TOOLS
    s.llm_agent_cmd->add_option("--disable-tools", s.ag_disable_tools,
        "Comma- or semicolon-separated path tools to omit: list_images, file_glob, file_read, file_search, "
        "image_resize, image_compress, image_transform, image_create, image_meta, image_find, write_file. Ineffective with --no-tools");
    s.llm_agent_cmd->add_flag("--godmode", s.ag_godmode,
        "Bypass ALL filesystem safety guards (sensitive-path deny, extension blocklist, "
        "dotfile block, shell command validation). Dangerous — use only when you know what you are doing.");
#endif
    s.llm_agent_cmd->add_flag("--json", s.ag_json,
        "Emit the full transcript as JSON on stdout (instead of plain-text events)");
    s.llm_agent_cmd->add_flag("--dry-run", s.ag_dry,
        "Resolve provider + tools + selection; print context as Markdown to stdout; no LLM call.");
    s.llm_agent_cmd->add_option("--log", s.ag_log_path,
        "Write a JSON run log (provider, per-event tool calls with full envelopes, transcript) to this file (truncates).")->default_val("agent.json");
    s.llm_agent_cmd
        ->add_option("--type", s.ag_api_mode,
                     "LLM API type: completion (POST /chat/completions) or "
                     "responses (POST /responses — OpenAI Responses API; supported by "
                     "OpenAI, OpenRouter, and LiteLLM proxy), or realtime "
                     "(experimental WebSocket /realtime PoC; text-only; best with router=openai). "
                     "Default: " + std::string(pm::llm::k_default_api_mode) + ".")
        ->check(CLI::IsMember({"completion", "responses", "realtime"}))
        ->default_val(pm::llm::k_default_api_mode);
    s.llm_agent_cmd
        ->add_option("--streaming", s.ag_streaming,
                     "Streaming mode: auto (default), on, off. "
                     "Implemented now for --type responses; completion remains non-streaming.")
        ->check(CLI::IsMember({"auto", "on", "off"}))
        ->default_val("auto");
    s.llm_agent_cmd
        ->add_option("--markdown", s.ag_markdown,
                     "Human stdout: auto (render when stdout is a TTY), plain (raw model UTF-8), or render (always run "
                     "the terminal markdown pass). Ignored with --json.")
        ->check(CLI::IsMember({"auto", "plain", "render"}))
        ->default_val("auto");
    s.llm_agent_cmd
        ->add_option("--color", s.ag_color,
                     "When markdown rendering is used: auto (color on TTY unless NO_COLOR/TERM=dumb), never, or always.")
        ->check(CLI::IsMember({"auto", "never", "always"}))
        ->default_val("auto");
    s.llm_agent_cmd->positionals_at_end(true);

#if defined(FEATURE_STT) && FEATURE_STT
    s.llm_agent_cmd->add_flag(
        "--mic", s.ag_mic,
        "Use the microphone as prompt input (continuous STT → LLM → TTS loop). "
        "Replaces --prompt for user input; --prompt may still be given as context. "
        "Requires --stt-api-key or ELEVENLABS_API_KEY. Press Ctrl+C to stop.");
    s.llm_agent_cmd->add_option(
        "--stt-api-key", s.ag_mic_stt_api_key,
        "ElevenLabs API key for real-time STT (and TTS when --voice-id is set). "
        "Falls back to ELEVENLABS_API_KEY environment variable.");
    s.llm_agent_cmd->add_option(
        "--voice-id", s.ag_mic_voice_id,
        "ElevenLabs voice ID to speak LLM responses aloud (empty = text-only). "
        "Browse voices at elevenlabs.io/app/voice-library.");
    s.llm_agent_cmd->add_flag(
        "--no-tts", s.ag_mic_no_tts,
        "Mic mode: disable TTS playback entirely (keep listening continuously after each response).");
    s.llm_agent_cmd->add_option(
        "--tts-model-id", s.ag_mic_model_id,
        "ElevenLabs TTS model used with --voice-id (default: eleven_v3).")
        ->default_val("eleven_v3");
    s.llm_agent_cmd->add_option(
        "--input", s.ag_mic_input,
        "Microphone device name (case-insensitive substring; use `audio info` to list). "
        "Omit to use the system default input device.");
    s.llm_agent_cmd->add_option(
        "--silence-ms", s.ag_mic_silence_ms,
        "Silence duration in ms after which speech is auto-committed to the LLM "
        "(0 = disabled; default 1500).")
        ->default_val(1500);
#endif
}
