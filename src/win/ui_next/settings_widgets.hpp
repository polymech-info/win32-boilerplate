#pragma once
// Thin Win32 “widget” holders: own an HWND, create via struct params, forward notifications via WM_COMMAND/WM_NOTIFY
// to the parent (no duplicate message maps here). See docs/win32xx-next.md (Policy).

#include "settings_controls.hpp"

namespace pmui::settings_widgets {

/// Preset `CBS_DROPDOWNLIST` combo; parent receives `CBN_*` as usual.
class PresetCombo {
public:
    PresetCombo() = default;
    explicit PresetCombo(HWND h) : m_h(h) {}

    HWND create(const pmui::settings_controls::PresetComboParams& p)
    {
        m_h = pmui::settings_controls::create_preset_combo(p);
        return m_h;
    }
    [[nodiscard]] HWND get() const { return m_h; }
    [[nodiscard]] explicit operator HWND() const { return m_h; }

private:
    HWND m_h{};
};

class LineEdit {
public:
    LineEdit() = default;
    explicit LineEdit(HWND h) : m_h(h) {}
    HWND create(const pmui::settings_controls::LineEditParams& p)
    {
        m_h = pmui::settings_controls::create_single_line_edit(p);
        return m_h;
    }
    [[nodiscard]] HWND get() const { return m_h; }
    [[nodiscard]] explicit operator HWND() const { return m_h; }

private:
    HWND m_h{};
};

class Autocheck {
public:
    Autocheck() = default;
    explicit Autocheck(HWND h) : m_h(h) {}
    HWND create(const pmui::settings_controls::AutocheckParams& p)
    {
        m_h = pmui::settings_controls::create_autocheck(p);
        return m_h;
    }
    [[nodiscard]] HWND get() const { return m_h; }
    [[nodiscard]] explicit operator HWND() const { return m_h; }

private:
    HWND m_h{};
};

} // namespace pmui::settings_widgets
