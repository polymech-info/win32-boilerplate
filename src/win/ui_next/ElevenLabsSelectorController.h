#pragma once

#include <Windows.h>
#include <string>

#include "AudioSelectorController.h"       // reuse AudioModelEntry
#include "widgets/SearchableCombo.hpp"

namespace pmui {

// ── ElevenLabs audio selector ─────────────────────────────────────────────────
//
// STT  — model only (Scribe real-time family, WebSocket).
//        No voice: ElevenLabs STT is recognition, not synthesis.
//
// TTS  — model (eleven_v3 / eleven_multilingual_v2 / …)
//        + voice (SearchableCombo: known list of UUIDs with free-text fallback).
//
// No defaults: if cur_* is empty the first list item (idx 0) is auto-selected.
// Saving idx 0 is a valid explicit choice.
//
// Usage — STT:
//   ElevenLabsSelectorController::populate_stt_model_combo(hModel, cs.stt_model);
//   …
//   cs.stt_model = ElevenLabsSelectorController::selected_stt_model(hModel);
//
// Usage — TTS:
//   ElevenLabsSelectorController::populate_tts_model_combo(hModel, cs.tts_model);
//   ElevenLabsSelectorController::populate_tts_voice_combo(scVoice, cs.tts_voice_id);
//   …
//   cs.tts_model    = ElevenLabsSelectorController::selected_tts_model(hModel);
//   cs.tts_voice_id = ElevenLabsSelectorController::selected_tts_voice(scVoice);

class ElevenLabsSelectorController {
public:
    // ── STT model table ───────────────────────────────────────────────────────
    // Only real-time-capable scribe models; ElevenLabsSTT connects via
    // wss://.../v1/speech-to-text/realtime.
    static const AudioModelEntry kSttModels[];
    static const int             kSttModelCount;

    // ── TTS model table ───────────────────────────────────────────────────────
    static const AudioModelEntry kTtsModels[];
    static const int             kTtsModelCount;

    // ── TTS known voices ──────────────────────────────────────────────────────
    // Seed the voice SearchableCombo.  Users may type any UUID freely.
    static const AudioModelEntry kTtsVoices[];
    static const int             kTtsVoiceCount;

    // ── STT ───────────────────────────────────────────────────────────────────

    // Fills the CBS_DROPDOWNLIST STT model combo.  Returns selected index.
    static int         populate_stt_model_combo(HWND h_combo, const std::string& cur_model);
    // Reads the model id via CB_GETITEMDATA; returns "" on no selection.
    static std::string selected_stt_model(HWND h_combo);

    // ── TTS ───────────────────────────────────────────────────────────────────

    // Fills the CBS_DROPDOWNLIST TTS model combo.  Returns selected index.
    static int         populate_tts_model_combo(HWND h_combo, const std::string& cur_model);
    // Reads the model id via CB_GETITEMDATA; returns "" on no selection.
    static std::string selected_tts_model(HWND h_combo);

    // Fills the TTS voice SearchableCombo with known voice labels / UUIDs,
    // then sets the edit field to cur_voice_id (list item or free text).
    static void        populate_tts_voice_combo(pmui::widgets::SearchableCombo& combo,
                                                const std::string& cur_voice_id);
    // Returns the current edit-field value (UUID or free text) as UTF-8.
    static std::string selected_tts_voice(const pmui::widgets::SearchableCombo& combo);
};

} // namespace pmui
