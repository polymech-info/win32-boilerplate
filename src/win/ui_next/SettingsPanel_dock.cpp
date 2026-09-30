#include "stdafx.h"
#include "SettingsPanel.h"
#include "win/settings_store.hpp"
#include "helpers/settings_panel_i18n.hpp"

// ── CDockSettings / CSettingsContainer ───────────────────────────────────────

CSettingsContainer::CSettingsContainer()
{
    std::string             err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const pmui::settings_panel_i18n::DockStrings& dk =
        pmui::settings_panel_i18n::dock_strings_for(app.display_language);
    SetTabText(dk.tab_generic);
    SetDockCaption(dk.caption_resize);
    SetView(m_view);
}

CDockSettings::CDockSettings()
{
    SetView(m_container);
}