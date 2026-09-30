#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace pmui {

// ── Static provider / model tables ────────────────────────────────────────────

struct AudioProviderEntry {
    const char*    id;
    const wchar_t* label;
};

struct AudioModelEntry {
    const char*    id;     // stored in settings / sent to API
    const wchar_t* label;  // display name shown in combo
};

// ── Controller ────────────────────────────────────────────────────────────────
// Manages provider combos (STT + TTS) and PixlWiz model combos.
// ElevenLabs-specific model / voice combos live in ElevenLabsSelectorController.
//
// populate_model_combo delegates to ElevenLabsSelectorController automatically
// when provider_id == "elevenlabs".
//
// Usage pattern:
//   1. populate_stt_provider_combo / populate_tts_provider_combo on init.
//   2. populate_model_combo on init and on provider CBN_SELCHANGE.
//   3. selected_provider_id / selected_model_id when saving.
//
// No defaults: if cur_* is empty, index 0 is pre-selected (first list item).
// Saving idx 0 is a valid explicit choice.

class AudioSelectorController {
public:
    // ── Provider combos ───────────────────────────────────────────────────────

    static int populate_stt_provider_combo(HWND h_combo, const std::string& cur_provider);
    static int populate_tts_provider_combo(HWND h_combo, const std::string& cur_provider);

    // ── Model combos ──────────────────────────────────────────────────────────
    // Routes to ElevenLabsSelectorController when provider_id == "elevenlabs".

    static void populate_model_combo(HWND h_combo, const std::string& provider_id,
                                     bool is_stt, const std::string& cur_model);

    // ── Readers ───────────────────────────────────────────────────────────────

    // Returns the canonical provider id for the current combo selection.
    // Returns "" (not a hardcoded default) when nothing is selected.
    static std::string selected_provider_id(HWND h_combo, bool is_stt);

    // Returns the canonical model id via CB_GETITEMDATA; "" on no selection.
    static std::string selected_model_id(HWND h_combo);

    // ── Static tables (exposed for reuse) ─────────────────────────────────────

    static const AudioProviderEntry kSttProviders[];
    static const int                kSttProviderCount;

    static const AudioProviderEntry kTtsProviders[];
    static const int                kTtsProviderCount;

    static const AudioModelEntry kPixlWizSttModels[];
    static const int             kPixlWizSttModelCount;

    static const AudioModelEntry kPixlWizTtsModels[];
    static const int             kPixlWizTtsModelCount;
};

} // namespace pmui
