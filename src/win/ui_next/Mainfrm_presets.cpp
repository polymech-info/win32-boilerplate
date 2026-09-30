// AI prompt preset management for CMainFrame.
// Implements: load/save/add/remove presets + the ribbon Presets menu.
#include "stdafx.h"
#include "constants.hpp"
#include "Mainfrm.h"
#include "helpers/text_conv.hpp"
#include "helpers/ui_font.hpp"
#include "win/settings_store.hpp"

using json = nlohmann::json;
using pmui::utf8_to_wide;
using pmui::wide_to_utf8;

// ── Load / Save ───────────────────────────────────────────────────────────────

void CMainFrame::LoadPresets()
{
    const media::settings::SettingsLoadLabelScope _ls("LoadPresets");
    m_presets.clear();
    try {
        std::string raw, err;
        if (!media::settings::load_settings_utf8(raw, err) || raw.empty()) return;
        json j = json::parse(raw, nullptr, false);
        if (j.is_discarded()) return;

        // ── New unified storage: chat_web.quick_actions (shared with React UI) ─────────────────
        if (j.contains("chat_web") && j["chat_web"].is_object()) {
            const auto& cw = j["chat_web"];
            // Load quick_actions (user-defined prompts with icon/name)
            if (cw.contains("quick_actions") && cw["quick_actions"].is_array()) {
                for (auto& qa : cw["quick_actions"]) {
                    if (!qa.is_object()) continue;
                    PromptPreset pp;
                    pp.name   = qa.value("name", "");
                    pp.prompt = qa.value("prompt", "");
                    // Icon is ignored for Win32 UI (not displayed)
                    if (!pp.prompt.empty()) {
                        // Deduplicate: skip if same prompt already loaded
                        bool exists = false;
                        for (const auto& existing : m_presets) {
                            if (existing.prompt == pp.prompt) { exists = true; break; }
                        }
                        if (!exists) m_presets.push_back(std::move(pp));
                    }
                }
            }
            // Load design_presets (design/look presets)
            if (cw.contains("design_presets") && cw["design_presets"].is_array()) {
                for (auto& dp : cw["design_presets"]) {
                    if (!dp.is_object()) continue;
                    PromptPreset pp;
                    pp.name   = dp.value("name", "");
                    pp.prompt = dp.value("prompt", "");
                    // Prepend group to name if available for organization
                    std::string group = dp.value("group", "");
                    if (!group.empty() && !pp.name.empty())
                        pp.name = group + " / " + pp.name;
                    if (!pp.prompt.empty()) {
                        bool exists = false;
                        for (const auto& existing : m_presets) {
                            if (existing.prompt == pp.prompt) { exists = true; break; }
                        }
                        if (!exists) m_presets.push_back(std::move(pp));
                    }
                }
            }
        }
    } catch (...) {}
}

void CMainFrame::SavePresets()
{
    const media::settings::SettingsLoadLabelScope _ls("SavePresets");
    json j = json::object();
    try {
        std::string existing, err;
        if (media::settings::load_settings_utf8(existing, err) && !existing.empty()) {
            j = json::parse(existing, nullptr, false);
            if (j.is_discarded()) j = json::object();
        }
    } catch (...) {}

    // ── Unified storage: chat_web.quick_actions (shared with React UI) ──────────
    if (!j.contains("chat_web") || !j["chat_web"].is_object())
        j["chat_web"] = json::object();

    json qa_arr = json::array();
    for (auto& p : m_presets) {
        // Skip entries that came from design_presets (have " / " in name from group)
        if (p.name.find(" / ") != std::string::npos) continue;
        json qa = {
            {"id", "qa-" + std::to_string(std::hash<std::string>{}(p.name + p.prompt))},
            {"name", p.name.empty() ? p.prompt.substr(0, 40) : p.name},
            {"prompt", p.prompt},
            {"icon", "✨"}
        };
        qa_arr.push_back(std::move(qa));
    }
    // Merge with existing quick_actions from React UI (don't overwrite)
    if (j["chat_web"].contains("quick_actions") && j["chat_web"]["quick_actions"].is_array()) {
        for (auto& existing : j["chat_web"]["quick_actions"]) {
            if (!existing.is_object()) continue;
            std::string existing_prompt = existing.value("prompt", "");
            bool already_have = false;
            for (auto& qa : qa_arr) {
                if (qa.value("prompt", "") == existing_prompt) {
                    already_have = true;
                    break;
                }
            }
            if (!already_have) qa_arr.push_back(existing);
        }
    }
    j["chat_web"]["quick_actions"] = qa_arr;

    try {
        std::string err;
        media::settings::save_settings_utf8(j.dump(2), err);
    } catch (...) {}
}

void CMainFrame::AddPreset(const std::string& name, const std::string& prompt)
{
    m_presets.push_back({ name, prompt });
    SavePresets();
}

void CMainFrame::RemovePreset(int index)
{
    if (index >= 0 && index < (int)m_presets.size()) {
        m_presets.erase(m_presets.begin() + index);
        SavePresets();
    }
}

// ── Presets popup menu ────────────────────────────────────────────────────────

void CMainFrame::OnPresets()
{
    ShowPresetsMenu();
}

void CMainFrame::ShowPresetsMenu()
{
    constexpr UINT ID_PRESET_BASE = 50000;
    constexpr UINT ID_REMOVE_BASE = 51000;
    constexpr UINT ID_SAVE_PRESET = 52000;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;

    if (m_presets.empty()) {
        ::AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"(no presets)");
    } else {
        for (int i = 0; i < (int)m_presets.size(); ++i) {
            std::wstring label = utf8_to_wide(m_presets[i].name);
            if (label.empty())
                label = utf8_to_wide(m_presets[i].prompt.substr(0, 40));
            ::AppendMenuW(hMenu, MF_STRING, ID_PRESET_BASE + i, label.c_str());
        }
    }

    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, MF_STRING, ID_SAVE_PRESET, L"Save current prompt\u2026");

    if (!m_presets.empty()) {
        HMENU hRemove = ::CreatePopupMenu();
        for (int i = 0; i < (int)m_presets.size(); ++i) {
            std::wstring label = utf8_to_wide(m_presets[i].name);
            if (label.empty())
                label = utf8_to_wide(m_presets[i].prompt.substr(0, 40));
            ::AppendMenuW(hRemove, MF_STRING, ID_REMOVE_BASE + i, label.c_str());
        }
        ::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hRemove, L"Remove preset");
    }

    POINT pt;
    ::GetCursorPos(&pt);
    UINT choice = (UINT)::TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, GetHwnd(), nullptr);
    ::DestroyMenu(hMenu);
    if (choice == 0) return;

    // ── Save current prompt as a named preset ─────────────────────────────────
    if (choice == ID_SAVE_PRESET) {
        // Read the current prompt straight from the Settings panel.
        std::string current_prompt;
        if (m_pDockSettings) {
            current_prompt = m_pDockSettings->GetSettingsContainer()
                                            .GetSettingsView()
                                            .GetTransformPrompt();
        }
        if (current_prompt.empty()) {
            ::MessageBox(GetHwnd(),
                L"No prompt to save.\nType a prompt in the Settings panel first.",
                pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }

        // Minimal inline dialog for a preset name.
        wchar_t nameBuf[256]{};
        alignas(DWORD) BYTE dlgBuf[1024]{};
        DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuf);
        dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
        dlg->cdit = 3; dlg->cx = 220; dlg->cy = 60;

        WORD* p = reinterpret_cast<WORD*>(dlg + 1);
        *p++ = 0; *p++ = 0;
        const wchar_t dlgTitle[] = L"Save Preset";
        memcpy(p, dlgTitle, sizeof(dlgTitle)); p += sizeof(dlgTitle) / sizeof(WORD);
        if (reinterpret_cast<uintptr_t>(p) % 4) p++;

        auto addCtrl = [&](DWORD style, short x, short y, short cx, short cy,
                            WORD id, const wchar_t* cls, const wchar_t* text) {
            if (reinterpret_cast<uintptr_t>(p) % 4) p++;
            DLGITEMTEMPLATE* item = reinterpret_cast<DLGITEMTEMPLATE*>(p);
            item->style = style | WS_CHILD | WS_VISIBLE;
            item->x = x; item->y = y; item->cx = cx; item->cy = cy; item->id = id;
            p = reinterpret_cast<WORD*>(item + 1);
            size_t clen = wcslen(cls)  + 1; memcpy(p, cls,  clen * 2); p += clen;
            size_t tlen = wcslen(text) + 1; memcpy(p, text, tlen * 2); p += tlen;
            *p++ = 0;
        };
        addCtrl(SS_LEFT, 6, 6, 208, 10, 0xFFFF, L"Static", L"Preset name:");
        addCtrl(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 6, 18, 208, 14, 1001, L"Edit", L"");
        addCtrl(BS_DEFPUSHBUTTON | WS_TABSTOP, 160, 38, 50, 16, IDOK, L"Button", L"Save");

        struct NameCtx { wchar_t* buf; int maxLen; };
        NameCtx ctx = { nameBuf, 255 };

        INT_PTR result = ::DialogBoxIndirectParam(
            ::GetModuleHandle(nullptr), dlg, GetHwnd(),
            [](HWND h, UINT msg, WPARAM wp, LPARAM lp) -> INT_PTR {
                if (msg == WM_INITDIALOG) {
                    ::SetWindowLongPtr(h, GWLP_USERDATA, lp);
                    HFONT hf = pmui::ui_font();
                    ::EnumChildWindows(h, [](HWND c, LPARAM f) -> BOOL {
                        ::SendMessage(c, WM_SETFONT, (WPARAM)f, TRUE); return TRUE;
                    }, (LPARAM)hf);
                    ::SetFocus(::GetDlgItem(h, 1001));
                    return FALSE;
                }
                if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
                    auto* c = reinterpret_cast<NameCtx*>(::GetWindowLongPtr(h, GWLP_USERDATA));
                    ::GetDlgItemTextW(h, 1001, c->buf, c->maxLen);
                    ::EndDialog(h, IDOK);
                    return TRUE;
                }
                if (msg == WM_CLOSE) { ::EndDialog(h, IDCANCEL); return TRUE; }
                return FALSE;
            }, reinterpret_cast<LPARAM>(&ctx));

        if (result != IDOK || nameBuf[0] == L'\0') return;
        AddPreset(wide_to_utf8(nameBuf), current_prompt);
        LogMessage(CString(L"Preset saved: ") + nameBuf);
        return;
    }

    // ── Remove a preset ───────────────────────────────────────────────────────
    if (choice >= ID_REMOVE_BASE && choice < ID_REMOVE_BASE + (UINT)m_presets.size()) {
        int idx = (int)(choice - ID_REMOVE_BASE);
        std::wstring name = utf8_to_wide(m_presets[idx].name);
        RemovePreset(idx);
        LogMessage(CString(L"Preset removed: ") + name.c_str());
        return;
    }

    // ── Load a preset into the Settings panel prompt field ──────────────────
    if (choice >= ID_PRESET_BASE && choice < ID_PRESET_BASE + (UINT)m_presets.size()) {
        int idx = (int)(choice - ID_PRESET_BASE);
        if (m_pDockSettings) {
            m_pDockSettings->GetSettingsContainer()
                           .GetSettingsView()
                           .SetTransformPrompt(m_presets[idx].prompt);
        }
        LogMessage(CString(L"Preset loaded: ") + utf8_to_wide(m_presets[idx].name).c_str());
    }
}
