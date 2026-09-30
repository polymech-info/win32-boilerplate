#include "stdafx.h"
#include "AudioSelectorController.h"
#include "ElevenLabsSelectorController.h"

namespace pmui {

// ── Static provider tables ─────────────────────────────────────────────────────

const AudioProviderEntry AudioSelectorController::kSttProviders[] = {
    { "pixlwiz",    L"PixlWiz (proxy)"     },
    { "elevenlabs", L"ElevenLabs (direct)" },
};
const int AudioSelectorController::kSttProviderCount =
    static_cast<int>(sizeof(kSttProviders) / sizeof(kSttProviders[0]));

const AudioProviderEntry AudioSelectorController::kTtsProviders[] = {
    { "pixlwiz",    L"PixlWiz (proxy)"     },
    { "elevenlabs", L"ElevenLabs (direct)" },
};
const int AudioSelectorController::kTtsProviderCount =
    static_cast<int>(sizeof(kTtsProviders) / sizeof(kTtsProviders[0]));

// ── PixlWiz model tables ────────────────────────────────────────────────────────

const AudioModelEntry AudioSelectorController::kPixlWizSttModels[] = {
    { "pixlwiz-speech-to-text", L"PixlWiz STT  (Whisper-1)" },
};
const int AudioSelectorController::kPixlWizSttModelCount =
    static_cast<int>(sizeof(kPixlWizSttModels) / sizeof(kPixlWizSttModels[0]));

const AudioModelEntry AudioSelectorController::kPixlWizTtsModels[] = {
    { "pixlwiz-speech",       L"PixlWiz TTS  (Multilingual v2)"    },
    { "pixlwiz-speech-turbo", L"PixlWiz TTS Turbo  (v2.5)"         },
};
const int AudioSelectorController::kPixlWizTtsModelCount =
    static_cast<int>(sizeof(kPixlWizTtsModels) / sizeof(kPixlWizTtsModels[0]));

// ── Helpers ────────────────────────────────────────────────────────────────────

namespace {

int fill_provider_combo(HWND h, const AudioProviderEntry* entries, int count,
                        const std::string& cur_id)
{
    if (!h) return 0;
    ::SendMessageW(h, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (int i = 0; i < count; ++i) {
        ::SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entries[i].label));
        if (cur_id == entries[i].id) sel = i;
    }
    ::SendMessageW(h, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    return sel;
}

void fill_model_combo(HWND h, const AudioModelEntry* entries, int count,
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

} // namespace

// ── Public interface ───────────────────────────────────────────────────────────

int AudioSelectorController::populate_stt_provider_combo(
    HWND h_combo, const std::string& cur_provider)
{
    return fill_provider_combo(h_combo, kSttProviders, kSttProviderCount, cur_provider);
}

int AudioSelectorController::populate_tts_provider_combo(
    HWND h_combo, const std::string& cur_provider)
{
    return fill_provider_combo(h_combo, kTtsProviders, kTtsProviderCount, cur_provider);
}

void AudioSelectorController::populate_model_combo(
    HWND h_combo, const std::string& provider_id, bool is_stt, const std::string& cur_model)
{
    if (!h_combo) return;

    if (provider_id == "elevenlabs") {
        if (is_stt)
            ElevenLabsSelectorController::populate_stt_model_combo(h_combo, cur_model);
        else
            ElevenLabsSelectorController::populate_tts_model_combo(h_combo, cur_model);
        return;
    }

    // PixlWiz (proxy) — or any unrecognised provider falls through to PixlWiz tables.
    if (is_stt)
        fill_model_combo(h_combo, kPixlWizSttModels, kPixlWizSttModelCount, cur_model);
    else
        fill_model_combo(h_combo, kPixlWizTtsModels, kPixlWizTtsModelCount, cur_model);
}

std::string AudioSelectorController::selected_provider_id(HWND h_combo, bool is_stt)
{
    if (!h_combo) return {};
    const int idx = static_cast<int>(::SendMessageW(h_combo, CB_GETCURSEL, 0, 0));
    const AudioProviderEntry* table = is_stt ? kSttProviders : kTtsProviders;
    const int count                 = is_stt ? kSttProviderCount : kTtsProviderCount;
    if (idx < 0 || idx >= count)
        return count > 0 ? table[0].id : "";
    return table[idx].id;
}

std::string AudioSelectorController::selected_model_id(HWND h_combo)
{
    if (!h_combo) return {};
    const int idx = static_cast<int>(::SendMessageW(h_combo, CB_GETCURSEL, 0, 0));
    if (idx < 0) return {};
    const LRESULT data = ::SendMessageW(h_combo, CB_GETITEMDATA, static_cast<WPARAM>(idx), 0);
    if (data == CB_ERR || data == 0) return {};
    return reinterpret_cast<const AudioModelEntry*>(data)->id;
}

} // namespace pmui
