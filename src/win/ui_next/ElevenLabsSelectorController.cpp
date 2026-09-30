#include "stdafx.h"
#include "ElevenLabsSelectorController.h"

#include <vector>

namespace pmui {

// ── STT model table ────────────────────────────────────────────────────────────
// Real-time models only — ElevenLabsSTT connects via the realtime WebSocket
// endpoint (wss://.../v1/speech-to-text/realtime?model_id=…).

const AudioModelEntry ElevenLabsSelectorController::kSttModels[] = {
    { "scribe_v2_realtime", L"Scribe v2  (real-time streaming)" },
};
const int ElevenLabsSelectorController::kSttModelCount =
    static_cast<int>(sizeof(kSttModels) / sizeof(kSttModels[0]));

// ── TTS model table ────────────────────────────────────────────────────────────

const AudioModelEntry ElevenLabsSelectorController::kTtsModels[] = {
    { "eleven_v3",              L"ElevenLabs v3"               },
    { "eleven_multilingual_v2", L"Multilingual v2"              },
    { "eleven_turbo_v2_5",      L"Turbo v2.5  (low latency)"   },
    { "eleven_flash_v2_5",      L"Flash v2.5  (ultra-fast)"    },
};
const int ElevenLabsSelectorController::kTtsModelCount =
    static_cast<int>(sizeof(kTtsModels) / sizeof(kTtsModels[0]));

// ── TTS known voices ───────────────────────────────────────────────────────────
// Voice IDs from the ElevenLabs account (matches dev_config.yaml).
// The SearchableCombo allows typing any UUID beyond this list.

const AudioModelEntry ElevenLabsSelectorController::kTtsVoices[] = {
    { "tLK6fPv15M0oKv4V3ACR", L"Sarah    — Mature, Reassuring, Confident"    },
    { "Xb7hH8MSUJpSbSDYk0k2", L"Alice    — Clear, Engaging Educator"          },
    { "XrExE9yKIg1WjnnlVkGX", L"Matilda  — Knowledgeable, Professional"       },
    { "onwK4e9ZLuTAKqWW03F9", L"Daniel   — Steady Broadcaster"                },
    { "nPczCjzI2devNBz1zQrb", L"Brian    — Deep, Resonant and Comforting"     },
    { "pNInz6obpgDQGcFmaJgB", L"Adam     — Dominant, Firm"                    },
    { "JBFqnCBsd6RMkjVDRZzb", L"George   — Warm, Captivating Storyteller"     },
    { "cgSgspJ2msm6clMCkdW9", L"Jessica  — Playful, Bright, Warm"             },
    { "SAz9YHcvj6GT2YYXdXww", L"River    — Relaxed, Neutral, Informative"     },
    { "TX3LPaxmHKxFdv7VOQHJ", L"Liam     — Energetic, Social Media Creator"   },
};
const int ElevenLabsSelectorController::kTtsVoiceCount =
    static_cast<int>(sizeof(kTtsVoices) / sizeof(kTtsVoices[0]));

// ── Helpers ────────────────────────────────────────────────────────────────────

namespace {

// Voice / model IDs are all printable ASCII — simple cast is safe.
static std::wstring ascii_to_wide(const char* s)
{
    if (!s) return {};
    std::wstring w;
    while (*s) w += static_cast<wchar_t>(static_cast<unsigned char>(*s++));
    return w;
}

static std::string wide_to_ascii(const std::wstring& w)
{
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s += static_cast<char>(c & 0x7F);
    return s;
}

// Fill h with AudioModelEntry items; stores entry pointer as CB_SETITEMDATA.
// Selects the entry whose .id matches cur_id; falls back to index 0.
static void fill_model_combo(HWND h, const AudioModelEntry* entries, int count,
                              const std::string& cur_id)
{
    if (!h) return;
    ::SendMessageW(h, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (int i = 0; i < count; ++i) {
        const int idx = static_cast<int>(
            ::SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entries[i].label)));
        if (idx >= 0)
            ::SendMessageW(h, CB_SETITEMDATA, static_cast<WPARAM>(idx),
                           reinterpret_cast<LPARAM>(&entries[i]));
        if (cur_id == entries[i].id) sel = i;
    }
    ::SendMessageW(h, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

static std::string read_model_combo(HWND h)
{
    if (!h) return {};
    const int idx = static_cast<int>(::SendMessageW(h, CB_GETCURSEL, 0, 0));
    if (idx < 0) return {};
    const LRESULT data = ::SendMessageW(h, CB_GETITEMDATA, static_cast<WPARAM>(idx), 0);
    if (data == CB_ERR || data == 0) return {};
    return reinterpret_cast<const AudioModelEntry*>(data)->id;
}

} // namespace

// ── STT ───────────────────────────────────────────────────────────────────────

int ElevenLabsSelectorController::populate_stt_model_combo(
    HWND h_combo, const std::string& cur_model)
{
    fill_model_combo(h_combo, kSttModels, kSttModelCount, cur_model);
    return static_cast<int>(::SendMessageW(h_combo, CB_GETCURSEL, 0, 0));
}

std::string ElevenLabsSelectorController::selected_stt_model(HWND h_combo)
{
    return read_model_combo(h_combo);
}

// ── TTS ───────────────────────────────────────────────────────────────────────

int ElevenLabsSelectorController::populate_tts_model_combo(
    HWND h_combo, const std::string& cur_model)
{
    fill_model_combo(h_combo, kTtsModels, kTtsModelCount, cur_model);
    return static_cast<int>(::SendMessageW(h_combo, CB_GETCURSEL, 0, 0));
}

std::string ElevenLabsSelectorController::selected_tts_model(HWND h_combo)
{
    return read_model_combo(h_combo);
}

void ElevenLabsSelectorController::populate_tts_voice_combo(
    pmui::widgets::SearchableCombo& combo, const std::string& cur_voice_id)
{
    std::vector<std::wstring> labels, values;
    labels.reserve(static_cast<size_t>(kTtsVoiceCount));
    values.reserve(static_cast<size_t>(kTtsVoiceCount));
    for (int i = 0; i < kTtsVoiceCount; ++i) {
        labels.emplace_back(kTtsVoices[i].label);
        values.emplace_back(ascii_to_wide(kTtsVoices[i].id));
    }
    combo.set_items(std::move(labels), std::move(values));
    combo.set_value(ascii_to_wide(cur_voice_id.c_str()));
}

std::string ElevenLabsSelectorController::selected_tts_voice(
    const pmui::widgets::SearchableCombo& combo)
{
    return wide_to_ascii(combo.get_value());
}

} // namespace pmui
