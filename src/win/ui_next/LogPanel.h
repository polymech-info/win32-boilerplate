#ifndef PM_UI_LOGPANEL_H
#define PM_UI_LOGPANEL_H

#include "stdafx.h"
#include "helpers/dock_helpers.h"
#include "helpers/dock_panel_toolstrip.h"

#include <deque>
#include <set>
#include <string>

/////////////////////////////////////////////////////////
/// Report list + filters: consumes log lines (UI + `pm-image.log` mirror). Does not call
/// `logger::*` / `ui_log_file_*`. Ingestion: `UWM_LOG_MESSAGE` → `LogMessage` → `AppendLine`.
class CLogView : public CListView
{
public:
    CLogView() = default;
    virtual ~CLogView() override = default;

    void AppendLine(const CString& text);
    void Clear();
    void CopyAllToClipboard();
    void RefreshThemeColors();
    /// Filter combos live on `CLogContentHost`; call after both HWNDs exist.
    void SetFilterCombos(HWND levelCombo, HWND sourceCombo);
    void OnFilterCombosChanged();
    /// `WM_NOTIFY` NM_CUSTOMDRAW for this listview (parent routes here).
    bool TryCustomDrawNotify(LPARAM lparam, LRESULT& out);
    /// `WM_NOTIFY` LVN_GETINFOTIPW — fills tooltip with full untruncated detail (parent routes here).
    bool TryInfoTipNotify(LPARAM lparam, LRESULT& out);
    /// Returns raw log text for the given list-row index; empty if out of range.
    std::wstring GetEntryRaw(int listRow) const;

protected:
    virtual void    OnAttach() override;
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CLogView(const CLogView&)            = delete;
    CLogView& operator=(const CLogView&) = delete;

    struct LogEntry {
        std::wstring raw;
        std::wstring time;
        std::wstring level_norm; ///< lowercase spdlog level, or `ui`
        std::wstring level_disp; ///< column text
        std::wstring source;
        std::wstring summary;
        std::wstring detail;
    };

    void     parse_into_entry(const CString& text, LogEntry& e);
    void     setup_columns();
    void     layout_last_column();
    void     refresh_rows_after_mutation();
    void     push_entry(LogEntry&& e);
    void     rebuild_list();
    void     rebuild_sources_from_entries();
    void     rebuild_source_combo_preserving_sel();
    void     note_source(const std::wstring& src);
    bool     entry_passes_filter(const LogEntry& e) const;
    void     trim_if_needed();
    COLORREF level_text_color(const std::wstring& level_norm) const;
    void     copy_selected_rows_to_clipboard();

    std::deque<LogEntry>   m_entries;
    std::set<std::wstring> m_sources;
    HWND                   m_hComboLevel{};
    HWND                   m_hComboSource{};
    static constexpr size_t k_max_entries = 12000;
    static constexpr size_t k_trim_to     = 8000;
};

/////////////////////////////////////////////////////////
// CLogContentHost — tool strip + level/source filter row + log list.
class CLogContentHost : public CWnd
{
public:
    CLogContentHost() = default;
    CLogView& GetLogView() { return m_log; }
    void      ClearLog();
    void      RefreshLogPanelChrome();

protected:
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    virtual int     OnCreate(CREATESTRUCT& cs) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CLogContentHost(const CLogContentHost&)            = delete;
    CLogContentHost& operator=(const CLogContentHost&) = delete;

    void layout_children(int client_w, int client_h);
    int  filter_row_height() const;
    int  list_top() const;
    void update_detail_pane();
    void invalidate_panel_tree(bool erase);

    pmui::CDockPanelToolStrip m_strip;
    HWND                      m_hLblLevel{};
    HWND                      m_hLblSource{};
    HWND                      m_hComboLevel{};
    HWND                      m_hComboSource{};
    CLogView                  m_log;

    HWND m_hDetail{};          ///< Read-only multiline EDIT showing selected entry's full raw text.
    int  m_detailPaneH{0};     ///< Current detail pane height in pixels (DPI-aware).
    bool m_draggingSplitter{false};
    int  m_dragStartY{0};
    int  m_dragStartH{0};

    static constexpr int k_split_h      = 4;   ///< Logical px — DPI-scaled at runtime.
    static constexpr int k_detail_min_h = 36;
    static constexpr int k_detail_max_h = 500;
    static constexpr int k_detail_def_h = 72;
};

/////////////////////////////////////////////////////////
// CLogContainer — dock container hosting CLogContentHost.
class CLogContainer : public CDockContainerBase
{
public:
    CLogContainer();
    virtual ~CLogContainer() override = default;
    CLogView& GetLogView() { return m_content.GetLogView(); }
    void       ClearLog();
    void       RefreshTabTheme() override;

protected:
    void PreCreate(CREATESTRUCT& cs) override;

private:
    CLogContainer(const CLogContainer&)            = delete;
    CLogContainer& operator=(const CLogContainer&) = delete;
    CLogContentHost m_content;
};

/////////////////////////////////////////////////////////
// CDockLog — docker wrapping CLogContainer.
class CDockLog : public CDockPanelBase
{
public:
    CDockLog();
    virtual ~CDockLog() override = default;
    CLogContainer& GetLogContainer() { return m_container; }

private:
    CDockLog(const CDockLog&)            = delete;
    CDockLog& operator=(const CDockLog&) = delete;
    CLogContainer m_container;
};

#endif // PM_UI_LOGPANEL_H
