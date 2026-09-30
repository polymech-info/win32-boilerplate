// Own ribbon strip: single COwnRibbonChromePage + ToolbarBuilder-style toolbar (no tab row).
#include "stdafx.h"

#ifdef FEATURE_USE_OWN_RIBBON

#ifndef FEATURE_CUSTOM_COMMANDS
#define FEATURE_CUSTOM_COMMANDS 0
#endif

#include "constants.hpp"
#include "OwnRibbonTab.h"
#include "Mainfrm.h"
#include "RibbonUI.h"
#include "helpers/theme.hpp"
#ifdef FEATURE_SVG_BUTTONS
#include "svg_paths.generated.h"
#endif

#include <algorithm>
#include <string>
#include <vector>
#ifdef FEATURE_SVG_BUTTONS
#include <filesystem>
#endif

namespace {

using own_ribbon::IconPresentation;
using own_ribbon::LayoutItem;

LayoutItem Sep()
{
    LayoutItem s{};
    s.cmdId = 0;
    return s;
}

#ifdef FEATURE_SVG_BUTTONS
/// Pre-built wide paths to Tabler `icons/filled/*.svg` (see `packages/tabler-icons`).
struct TablerFilledPaths {
    std::wstring home, layout_left, layout_bottom, layout_right, theme, resize, compress, meta, transform, find, duplicates, chat, run, pause,
        resume, cancel, save_session, load_session, reset_layout, app_settings;

    TablerFilledPaths()
    {
        namespace fs = std::filesystem;
        const fs::path b(PM_TABLER_FILLED_DIR_W);
        home          = (b / "home.svg").wstring();
        layout_left   = (b / "layout-sidebar.svg").wstring();
        layout_bottom = (b / "layout-bottombar.svg").wstring();
        layout_right  = (b / "layout-sidebar-right.svg").wstring();
        theme         = (b / "moon.svg").wstring();
        resize        = (b / "photo.svg").wstring();
        compress      = (b / "archive.svg").wstring();
        meta          = (b / "tags.svg").wstring();
        transform     = (b / "sparkles.svg").wstring();
        find          = (b / "search.svg").wstring();
        duplicates    = (b / "stack-2.svg").wstring();
        chat          = (b / "message-circle.svg").wstring();
        run           = (b / "player-play.svg").wstring();
        pause         = (b / "player-pause.svg").wstring();
        resume        = (b / "player-track-next.svg").wstring();
        cancel        = (b / "player-stop.svg").wstring();
        save_session  = (b / "bookmark.svg").wstring();
        load_session  = (b / "folder-open.svg").wstring();
        reset_layout  = (b / "layout.svg").wstring();
        app_settings  = (b / "settings.svg").wstring();
    }
};

static const TablerFilledPaths g_tabler;
#endif

const std::vector<LayoutItem>& LayoutToggleLayout()
{
    static const std::vector<LayoutItem> kItems = [] {
        std::vector<LayoutItem> v;
#ifdef FEATURE_SVG_BUTTONS
        auto push = [&](UINT cmd, const std::wstring& svgPath, COLORREF tint, UINT labelRes, const wchar_t* en) {
            LayoutItem x{};
            x.cmdId           = cmd;
            x.svgFilePathW    = svgPath.c_str();
            x.svgTint         = tint;
            x.icon            = IconPresentation::Large32;
            x.showLabel       = true;
            x.isToggle        = true;
            x.labelStringId   = labelRes;
            x.label           = en;
            x.tooltip         = en;
            v.push_back(x);
        };
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
        push(IDC_CMD_VIEW_HOME, g_tabler.home, RGB(99, 102, 241), 0, L"Home");
#endif
        push(IDC_CMD_VIEW_FILETREE, g_tabler.layout_left, RGB(59, 130, 246), IDC_CMD_VIEW_FILETREE_LabelTitle_RESID,
             L"Explorer");
#ifdef FEATURE_CONSOLE
        if (pmui::web_console::available())
            push(IDC_CMD_VIEW_CONSOLE, g_tabler.layout_bottom, RGB(34, 197, 94), 0, L"Terminal");
#if FEATURE_COMMAND_LOG_VIEW
        else
            push(IDC_CMD_VIEW_LOG, g_tabler.layout_bottom, RGB(34, 197, 94), IDC_CMD_VIEW_LOG_LabelTitle_RESID, L"Log");
#endif
#else
#if FEATURE_COMMAND_LOG_VIEW
        push(IDC_CMD_VIEW_LOG, g_tabler.layout_bottom, RGB(34, 197, 94), IDC_CMD_VIEW_LOG_LabelTitle_RESID, L"Log");
#endif
#endif
        push(IDC_CMD_VIEW_CHAT, g_tabler.layout_right, RGB(6, 182, 212), IDC_CMD_VIEW_CHAT_LabelTitle_RESID, L"Chat");
        push(IDC_CMD_TOGGLE_THEME, g_tabler.theme, CLR_NONE, 0, L"Theme");
#else
        auto push = [&](UINT cmd, UINT res, UINT labelRes, const wchar_t* en) {
            LayoutItem x{};
            x.cmdId         = cmd;
            x.imageResId    = res;
            x.icon          = IconPresentation::Large32;
            x.showLabel     = true;
            x.isToggle      = true;
            x.labelStringId = labelRes;
            x.label         = en;
            x.tooltip       = en;
            v.push_back(x);
        };
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
        push(IDC_CMD_VIEW_HOME, IDC_CMD_VIEW_FILETREE_LargeImages_RESID, 0, L"Home");
#endif
        push(IDC_CMD_VIEW_FILETREE, IDC_CMD_VIEW_FILETREE_LargeImages_RESID, IDC_CMD_VIEW_FILETREE_LabelTitle_RESID,
             L"Explorer");
#ifdef FEATURE_CONSOLE
        if (pmui::web_console::available())
            push(IDC_CMD_VIEW_CONSOLE, IDC_CMD_VIEW_LOG_LargeImages_RESID, 0, L"Terminal");
#if FEATURE_COMMAND_LOG_VIEW
        else
            push(IDC_CMD_VIEW_LOG, IDC_CMD_VIEW_LOG_LargeImages_RESID, IDC_CMD_VIEW_LOG_LabelTitle_RESID, L"Log");
#endif
#else
#if FEATURE_COMMAND_LOG_VIEW
        push(IDC_CMD_VIEW_LOG, IDC_CMD_VIEW_LOG_LargeImages_RESID, IDC_CMD_VIEW_LOG_LabelTitle_RESID, L"Log");
#endif
#endif
//        push(IDC_CMD_VIEW_CHAT, IDC_CMD_VIEW_CHAT_LargeImages_RESID, IDC_CMD_VIEW_CHAT_LabelTitle_RESID, L"Chat");
        push(IDC_CMD_TOGGLE_THEME, IDC_CMD_APP_SETTINGS_LargeImages_RESID, 0, L"Theme");
#endif
        return v;
    }();
    return kItems;
}

const std::vector<LayoutItem>& HomeLayout()
{
    static const std::vector<LayoutItem> kItems = [] {
        std::vector<LayoutItem> v;
#ifdef FEATURE_SVG_BUTTONS
        auto push = [&](UINT cmd, const std::wstring& svgPath, COLORREF tint, UINT labelRes, const wchar_t* en, UINT tipRes = 0) {
            LayoutItem x{};
            x.cmdId           = cmd;
            x.svgFilePathW    = svgPath.c_str();
            x.svgTint         = tint;
            x.icon            = IconPresentation::Large32;
            x.showLabel       = true;
            x.isToggle        = false;
            x.labelStringId   = labelRes;
            x.tooltipStringId = tipRes;
            x.label           = en;
            v.push_back(x);
        };
#if FEATURE_COMMAND_RESIZE
//        push(IDC_CMD_RESIZE, g_tabler.resize, RGB(14, 165, 233), IDC_CMD_RESIZE_LabelTitle_RESID, L"Resize");
#endif
#if FEATURE_COMMAND_COMPRESS
//        push(IDC_CMD_COMPRESS, g_tabler.compress, RGB(168, 85, 247), IDC_CMD_COMPRESS_LabelTitle_RESID, L"Compress");
#endif
#if FEATURE_COMMAND_META
//        push(IDC_CMD_META, g_tabler.meta, RGB(245, 158, 11), IDC_CMD_META_LabelTitle_RESID, L"Meta");
#endif
#if FEATURE_COMMAND_TRANSFORM
//        push(IDC_CMD_TRANSFORM, g_tabler.transform, RGB(236, 72, 153), IDC_CMD_TRANSFORM_LabelTitle_RESID, L"Transform");
#endif
#if FEATURE_COMMAND_FIND
//        push(IDC_CMD_FIND, g_tabler.find, RGB(20, 184, 166), IDC_CMD_FIND_LabelTitle_RESID, L"Find");
#endif
#if FEATURE_COMMAND_DUPLICATES
//        push(IDC_CMD_DUPLICATES, g_tabler.duplicates, RGB(16, 185, 129), IDC_CMD_DUPLICATES_LabelTitle_RESID, L"Duplicates");
#endif
#if FEATURE_COMMAND_LLM
//        push(IDC_CMD_CHAT, g_tabler.chat, RGB(6, 182, 212), IDC_CMD_CHAT_LabelTitle_RESID, L"Chat", IDC_CMD_CHAT_TooltipDescription_RESID);
#endif
        #if FEATURE_COMMAND_UI_COMMAND_CONTROL
        v.push_back(Sep());
        push(IDC_CMD_RUN, g_tabler.run, RGB(34, 197, 94), IDC_CMD_RUN_LabelTitle_RESID, L"Run");
        push(IDC_CMD_PAUSE, g_tabler.pause, RGB(156, 163, 175), IDC_CMD_PAUSE_LabelTitle_RESID, L"Pause", IDC_CMD_PAUSE_TooltipDescription_RESID);
        push(IDC_CMD_RESUME, g_tabler.resume, RGB(74, 222, 128), IDC_CMD_RESUME_LabelTitle_RESID, L"Resume",
             IDC_CMD_RESUME_TooltipDescription_RESID);
        push(IDC_CMD_CANCEL, g_tabler.cancel, RGB(248, 113, 113), IDC_CMD_CANCEL_LabelTitle_RESID, L"Cancel",
             IDC_CMD_CANCEL_TooltipDescription_RESID);
        v.push_back(Sep());
#endif
#if FEATURE_COMMAND_SESSION_PERSISTENCE
        push(IDC_CMD_SAVE_SESSION, g_tabler.save_session, RGB(96, 165, 250), IDC_CMD_SAVE_SESSION_LabelTitle_RESID, L"Save session",
             IDC_CMD_SAVE_SESSION_TooltipDescription_RESID);
        push(IDC_CMD_LOAD_SESSION, g_tabler.load_session, RGB(52, 211, 153), IDC_CMD_LOAD_SESSION_LabelTitle_RESID, L"Load session",
             IDC_CMD_LOAD_SESSION_TooltipDescription_RESID);
#endif
#else
        auto push = [&](UINT cmd, UINT res, UINT labelRes, const wchar_t* en, UINT tipRes = 0) {
            LayoutItem x{};
            x.cmdId           = cmd;
            x.imageResId      = res;
            x.icon            = IconPresentation::Large32;
            x.showLabel       = true;
            x.isToggle        = false;
            x.labelStringId   = labelRes;
            x.tooltipStringId = tipRes;
            x.label           = en;
            v.push_back(x);
        };
#if FEATURE_COMMAND_RESIZE
        push(IDC_CMD_RESIZE, IDC_CMD_RESIZE_LargeImages_RESID, IDC_CMD_RESIZE_LabelTitle_RESID, L"Resize");
#endif
#if FEATURE_COMMAND_COMPRESS
        push(IDC_CMD_COMPRESS, IDC_CMD_COMPRESS_LargeImages_RESID, IDC_CMD_COMPRESS_LabelTitle_RESID, L"Compress");
#endif
#if FEATURE_COMMAND_META
        push(IDC_CMD_META, IDC_CMD_META_LargeImages_RESID, IDC_CMD_META_LabelTitle_RESID, L"Meta");
#endif
#if FEATURE_COMMAND_TRANSFORM
        push(IDC_CMD_TRANSFORM, IDC_CMD_TRANSFORM_LargeImages_RESID, IDC_CMD_TRANSFORM_LabelTitle_RESID, L"Transform");
#endif
#if FEATURE_COMMAND_FIND
        push(IDC_CMD_FIND, IDC_CMD_FIND_LargeImages_RESID, IDC_CMD_FIND_LabelTitle_RESID, L"Find");
#endif
#if FEATURE_COMMAND_DUPLICATES
        push(IDC_CMD_DUPLICATES, IDC_CMD_DUPLICATES_LargeImages_RESID, IDC_CMD_DUPLICATES_LabelTitle_RESID, L"Duplicates");
#endif
#if FEATURE_COMMAND_LLM
        push(IDC_CMD_CHAT, IDC_CMD_CHAT_LargeImages_RESID, IDC_CMD_CHAT_LabelTitle_RESID, L"Chat", IDC_CMD_CHAT_TooltipDescription_RESID);
#endif
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
        //v.push_back(Sep());
        push(IDC_CMD_RUN, IDC_CMD_RUN_LargeImages_RESID, IDC_CMD_RUN_LabelTitle_RESID, L"Run");
        push(IDC_CMD_PAUSE, IDC_CMD_PAUSE_LargeImages_RESID, IDC_CMD_PAUSE_LabelTitle_RESID, L"Pause", IDC_CMD_PAUSE_TooltipDescription_RESID);
        push(IDC_CMD_RESUME, IDC_CMD_RESUME_LargeImages_RESID, IDC_CMD_RESUME_LabelTitle_RESID, L"Resume",
             IDC_CMD_RESUME_TooltipDescription_RESID);
        push(IDC_CMD_CANCEL, IDC_CMD_CANCEL_LargeImages_RESID, IDC_CMD_CANCEL_LabelTitle_RESID, L"Cancel",
             IDC_CMD_CANCEL_TooltipDescription_RESID);
        v.push_back(Sep());
#endif
#if FEATURE_COMMAND_SESSION_PERSISTENCE
        push(IDC_CMD_SAVE_SESSION, IDC_CMD_SAVE_SESSION_LargeImages_RESID, IDC_CMD_SAVE_SESSION_LabelTitle_RESID, L"Save session",
             IDC_CMD_SAVE_SESSION_TooltipDescription_RESID);
        push(IDC_CMD_LOAD_SESSION, IDC_CMD_LOAD_SESSION_LargeImages_RESID, IDC_CMD_LOAD_SESSION_LabelTitle_RESID, L"Load session",
             IDC_CMD_LOAD_SESSION_TooltipDescription_RESID);
#endif
#endif
        return v;
    }();
    return kItems;
}

const std::vector<LayoutItem>& ViewFooterLayout()
{
    static const std::vector<LayoutItem> kItems = [] {
        std::vector<LayoutItem> v;
#ifdef FEATURE_SVG_BUTTONS
        auto push = [&](UINT cmd, const std::wstring& svgPath, COLORREF tint, UINT labelRes, const wchar_t* en, UINT tipRes = 0) {
            LayoutItem x{};
            x.cmdId           = cmd;
            x.svgFilePathW    = svgPath.c_str();
            x.svgTint         = tint;
            x.icon            = IconPresentation::Large32;
            x.showLabel       = true;
            x.isToggle        = false;
            x.labelStringId   = labelRes;
            x.tooltipStringId = tipRes;
            x.label           = en;
            v.push_back(x);
        };
        push(IDC_CMD_RESET_LAYOUT, g_tabler.reset_layout, RGB(148, 163, 184), IDC_CMD_RESET_LAYOUT_LabelTitle_RESID, L"Reset layout");
#ifdef FEATURE_BROWSER
        push(IDC_CMD_FILE_SETTINGS, g_tabler.app_settings, RGB(167, 139, 250), 0, L"Settings");
#endif
#else
        auto push = [&](UINT cmd, UINT res, UINT labelRes, const wchar_t* en, UINT tipRes = 0) {
            LayoutItem x{};
            x.cmdId           = cmd;
            x.imageResId      = res;
            x.icon            = IconPresentation::Large32;
            x.showLabel       = true;
            x.isToggle        = false;
            x.labelStringId   = labelRes;
            x.tooltipStringId = tipRes;
            x.label           = en;
            v.push_back(x);
        };
        push(IDC_CMD_RESET_LAYOUT, IDC_CMD_RESET_LAYOUT_LargeImages_RESID, IDC_CMD_RESET_LAYOUT_LabelTitle_RESID, L"Reset layout");
#ifdef FEATURE_BROWSER
        push(IDC_CMD_FILE_SETTINGS, IDC_CMD_APP_SETTINGS_LargeImages_RESID, 0, L"Settings");
#endif
#endif
        return v;
    }();
    return kItems;
}

std::vector<LayoutItem> CombinedRibbonLayout()
{
    std::vector<LayoutItem> v;
    v.insert(v.end(), LayoutToggleLayout().begin(), LayoutToggleLayout().end());
    v.push_back(Sep());
    v.insert(v.end(), HomeLayout().begin(), HomeLayout().end());
    own_ribbon::AppendCustomRibbonLayout(v);
    v.push_back(Sep());
    v.insert(v.end(), ViewFooterLayout().begin(), ViewFooterLayout().end());
    return v;
}

bool BlocksWhileProcessing(UINT cmdId)
{
    if (own_ribbon::IsCustomRibbonCommandId(cmdId))
        return true;
    switch (cmdId) {
    case IDC_CMD_RESIZE:
    case IDC_CMD_COMPRESS:
    case IDC_CMD_META:
    case IDC_CMD_TRANSFORM:
    case IDC_CMD_FIND:
    case IDC_CMD_DUPLICATES:
    case IDC_CMD_RUN:
    case IDC_CMD_LOAD_SESSION:
        return true;
    default:
        return false;
    }
}

void SyncProcessingBlockedItems(own_ribbon::COwnRibbonToolStrip& strip, const std::vector<LayoutItem>& items, bool processing)
{
    for (const LayoutItem& it : items) {
        if (it.cmdId != 0 && BlocksWhileProcessing(it.cmdId))
            strip.SyncEnabled(it.cmdId, (it.enabled && !processing) ? TRUE : FALSE);
        if (it.dropdownItems)
            SyncProcessingBlockedItems(strip, *it.dropdownItems, processing);
    }
}

} // namespace

void COwnRibbonChromePage::ApplyPageChrome(bool dark, COLORREF stripBg, COLORREF stripFg)
{
    m_pageBg    = stripBg;
    m_ruleColor = pmui::theme_palette().caption_pen;
    m_strip.ApplyChrome(dark, stripBg, stripFg);
}

BOOL COwnRibbonChromePage::OnEraseBkgnd(Win32xx::CDC& dc)
{
    CRect rc = GetClientRect();
    dc.SolidFill(m_pageBg, rc);
    const int ruleH = own_ribbon::RibbonChromeBottomRule(*this);
    if (ruleH > 0 && rc.Height() > ruleH) {
        CRect line(rc.left, rc.bottom - ruleH, rc.right, rc.bottom);
        dc.SolidFill(m_ruleColor, line);
    }
    return TRUE;
}

void COwnRibbonChromePage::PreCreate(CREATESTRUCT& cs)
{
    Win32xx::CWnd::PreCreate(cs);
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
}

LRESULT COwnRibbonChromePage::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SIZE && m_strip.IsWindow()) {
        CRect r   = GetClientRect();
        const int top    = own_ribbon::RibbonChromeTopInset(*this);
        const int bottom = own_ribbon::RibbonChromeBottomInset(*this);
        const int rule   = own_ribbon::RibbonChromeBottomRule(*this);
        const int stripH = (std::max)(r.Height() - top - bottom - rule, 1);
        m_strip.SetWindowPos(nullptr, 0, top, r.Width(), stripH, SWP_NOZORDER);
    }
    return CWnd::WndProcDefault(msg, wp, lp);
}

bool COwnRibbonTab::Create(CMainFrame& parent)
{
    return COwnRibbonChromePage::Create(parent) != nullptr;
}

bool COwnRibbonTab::BuildRibbonLayout()
{
    HINSTANCE inst = Win32xx::GetApp()->GetInstanceHandle();
    m_layoutItems = CombinedRibbonLayout();
    return m_strip.ApplyLayout(inst, *this, m_layoutItems);
}

int COwnRibbonTab::OnCreate(CREATESTRUCT&)
{
    if (!m_strip.Create(*this)) {
        (void)::OutputDebugStringW((L"[" + std::wstring(pm::brand::k_app_id_w) + L"] COwnRibbonTab::OnCreate failed: m_strip.Create\n").c_str());
        return -1;
    }
    return 0;
}

int COwnRibbonTab::PreferredHeight() const
{
    if (!IsWindow())
        return 0;
    COwnRibbonTab* self = const_cast<COwnRibbonTab*>(this);
    const int top    = own_ribbon::RibbonChromeTopInset(*self);
    const int bottom = own_ribbon::RibbonChromeBottomInset(*self);
    const int rule   = own_ribbon::RibbonChromeBottomRule(*self);
    const int measured = self->m_strip.MeasuredStripHeight();
    if (measured > 0)
        return top + measured + bottom + rule;
    return top + own_ribbon::PreferredStripHeight(*this) + bottom + rule;
}

void COwnRibbonTab::ApplyChrome(bool dark, COLORREF pageBg)
{
    if (!IsWindow())
        return;
    const COLORREF fg = pmui::theme_palette().window_fg;
    ApplyPageChrome(dark, pageBg, fg);
    ::InvalidateRect(*this, nullptr, TRUE);
}

void COwnRibbonTab::SyncBatchControls(CMainFrame& frame)
{
    const bool paused   = frame.RibbonBatchPaused();
    const bool proc     = frame.RibbonProcessing();
    const bool hasQueue = frame.RibbonHasQueueItems();

    if (!m_strip.ToolBar().IsWindow())
        return;
    auto en = [&](UINT id, BOOL e) {
        if (m_strip.ToolBar().CommandToIndex(id) >= 0)
            m_strip.SyncEnabled(id, e);
    };
    SyncProcessingBlockedItems(m_strip, m_layoutItems, proc);
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
    en(IDC_CMD_RUN, proc ? FALSE : TRUE);
    en(IDC_CMD_PAUSE, (proc && !paused) ? TRUE : FALSE);
    en(IDC_CMD_RESUME, paused ? TRUE : FALSE);
    en(IDC_CMD_CANCEL, proc ? TRUE : FALSE);
#endif
#if FEATURE_COMMAND_SESSION_PERSISTENCE
    en(IDC_CMD_SAVE_SESSION, (hasQueue && !proc) ? TRUE : FALSE);
    en(IDC_CMD_LOAD_SESSION, proc ? FALSE : TRUE);
#endif
}

#endif // FEATURE_USE_OWN_RIBBON
