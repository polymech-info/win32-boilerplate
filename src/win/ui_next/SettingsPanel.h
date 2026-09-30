#ifndef PM_UI_SETTINGSPANEL_H
#define PM_UI_SETTINGSPANEL_H

#include "stdafx.h"
#include "features.h"
#include "helpers/dock_helpers.h"
#include "settings_view_models.hpp"
#include "ReplicateSelectorController.h"
#include "core/resize.hpp"
#include "core/duplicates.hpp"
#include "settings_controls.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef FEATURE_PNG_COMPRESSOR
#include "core/png_compress.hpp"
#endif

/// Posted to `CSettingsView` as `UWM_SETTINGS_REPLICATE_INIT_DONE` with `lparam` pointing here; handler deletes.
struct SettingsRepinitPayload {
    pmui::ReplicateSelectorState tf;
    pmui::ReplicateSelectorState meta;
    pmui::ReplicateSelectorState find;
    bool ok_tf = false;
    bool ok_meta = false;
    bool ok_find = false;
};

/////////////////////////////////////////////////////////
// CSettingsView — Photoshop-style resize/compress settings panel.
// Modes:  MODE_RESIZE, MODE_TRANSFORM, MODE_COMPRESS
class CSettingsView : public CWnd
{
public:
    CSettingsView() = default;
    virtual ~CSettingsView() override = default;

    enum Mode { MODE_RESIZE, MODE_TRANSFORM, MODE_COMPRESS, MODE_META, MODE_FIND, MODE_DUPLICATES };
    void SetMode(Mode mode);
    Mode GetMode() const { return m_mode; }

    /// After `pmui::ui_font()` changes: remeasure static/check/edit heights in the scroll host
    /// (labels were sized for the default template; extra points need more vertical room).
    void RelayoutForUiFont();

    // Resize
    void ReadOptions(media::ResizeOptions& opt, std::string& out_dir) const;
    void WriteOptions(const media::ResizeOptions& opt, const std::string& out_dir);

    // Compress
    void ReadCompressSettings(CompressSettings& out) const;

    // Meta
    void ReadMetaSettings(MetaSettings& out) const;

    // Find
    void ReadFindSettings(FindSettings& out) const;

    // Duplicates (lib `find_duplicates`)
    void ReadDuplicatesSettings(media::DuplicatesOptions& out) const;
    CommandProviderOverridesKeysPresent LoadCommandProviderOverrides();
    void SaveCommandProviderOverrides() const;

    // Transform (AI image editing — replaces the old AI ribbon tab)
    void ReadTransformSettings(TransformSettings& out) const;
    void SetTransformPrompt(const std::string& utf8_text);   // used by preset loader
    std::string GetTransformPrompt() const;

protected:
    virtual int     OnCreate(CREATESTRUCT& cs) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;
    void            PreCreate(CREATESTRUCT& cs) override;

private:
    CSettingsView(const CSettingsView&) = delete;
    CSettingsView& operator=(const CSettingsView&) = delete;

    void CreateControls();
    void UpdateSettingsLayoutGeometry();
    HWND MakeLabel(LPCWSTR t, int y, int w = pmui::settings_controls::Layout::label_w);
    void OnCmpBrowse();
    void AddThemedSep(std::vector<HWND>& group, HWND parent, HINSTANCE inst, int x0, int sepW, int y);
    void AddSectionCard(std::vector<HWND>& group, HWND parent, HINSTANCE inst, int x, int y, int w, int h);
    void InstallSettingsPanelTooltips();
    /// Replicate “Collection” row: only shown in the matching mode (deferred provider init used to reshow on top of Resize).
    void SyncReplicateRowVisibilityForMode();
    /// Horiz. width of `m_hScrollContent` (narrows when a vert. scrollbar is visible) — use for `cw`/`sepW`.
    int  SettingsContentClientWidth() const;
    void CreateResizeControls(HWND parent, HINSTANCE inst, int x0, int cx, int rh, int dy, int sepW, int cw, int trackW);
    void CreateCompressControls(HWND parent, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw, int cmpSliderW);
    void CreateDuplicatesControls(HWND parent, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw);
    void CreateTransformControls(HWND parent, HINSTANCE inst, int x0, int lw, int cx, int rh, int dy, int sepW, int cw);
    void CreateMetaControls(HWND parent, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw);
    void CreateFindControls(HWND parent, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw);

    // Resize
    void OnBrowseOutput();
    void OnResPresetChanged();
    void OnRatioChanged();
    void OnWidthChanged();
    void OnHeightChanged();
    void OnQualitySlider();
    void ApplyFont(HFONT hf);
    void UpdateSettingsPanelScroll();
    void OnSettingsVScroll(int code, int posHiword);
    static int MaxChildBottom(HWND contentHost);
    static LRESULT CALLBACK ContentHostSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    // ── Resize controls ───────────────────────────────────────────────────
    HWND m_hResPreset{};
    HWND m_hRatio{};
    HWND m_hMaxW{};
    HWND m_hMaxH{};
    HWND m_hFit{};
    HWND m_hPreset{};
    HWND m_hOutDir{};
    HWND m_hBtnBrowse{};
    HWND m_hFormat{};           // output format combo (auto/jpg/png/webp/tiff/avif)
    HWND m_hSliderQuality{};
    HWND m_hLblQuality{};
    HWND m_hKernel{};
    HWND m_hAutorot{};
    HWND m_hEnlarge{};
    HWND m_hStrip{};

    // ── Transform controls (AI image editing) ────────────────────────────
    HWND m_hTfModel{};
    HWND m_hTfProvider{};
    HWND m_hTfCollection{};
    HWND m_hTfCollectionLbl{};
    HWND m_hTfRefresh{};
    HWND m_hTfAspect{};
    HWND m_hTfSize{};
    HWND m_hTfPrompt{};
    HWND m_hTfPresets{};
    HWND m_hTfRefList{};        // listbox of selected reference images
    HWND m_hTfRefAdd{};         // [+ Add reference…] button
    HWND m_hTfRefClear{};       // [Clear] button
    /// Pre-resize in memory (shared preset widths with Meta/Find; Transform adds RAW-only).
    HWND m_hTfPreresizeFirst{};
    HWND m_hTfPreresizeW{};
    HWND m_hTfPreresizeRawOnly{};
    void OnTfRefAdd();
    void OnTfRefClear();
    void OnTfProviderChanged();
    void OnTfCollectionChanged();
    void OnTfRefreshReplicateModels();
    void ApplyDeferredCommandProviderOverrides();
    void startReplicateNetworkBatchAsync();
    void handleReplicateNetworkBatchDone(SettingsRepinitPayload* p);
    void reapplyCommandOverrideModelSelections();

    // ── Compress controls (always visible in compress mode) ───────────────
    HWND m_hCmpDest{};
    HWND m_hCmpDir{};
    HWND m_hCmpBrowse{};
    HWND m_hCmpFormat{};        // compressor combo: PNG / MozJPEG
    HWND m_hCmpStrip{};         // [x] strip metadata

    // ── PNG section (visible when PNG selected) ───────────────────────────
    HWND m_hCmpLevel{};
    HWND m_hCmpLevelLbl{};
    HWND m_hCmpQuantize{};
    HWND m_hCmpColors{};
    HWND m_hCmpQualSlider{};
    HWND m_hCmpQualLbl{};
    HWND m_hCmpZopfli{};

    // ── MozJPEG section (visible when MozJPEG selected) ──────────────────
    HWND m_hCmpJpegSlider{};
    HWND m_hCmpJpegLbl{};
    HWND m_hCmpProgressive{};
    HWND m_hCmpTrellis{};

    void OnCmpFormatChanged();   // show/hide PNG vs MozJPEG sub-sections
    void OnMetaBrowse();
    void OnMetaProviderChanged();
    void OnMetaCollectionChanged();
    void OnMetaRefreshReplicateModels();
    void OnMetaPresetChanged();
    void OnFindProviderChanged();
    void OnFindCollectionChanged();
    void OnFindRefreshReplicateModels();

    // ── Meta controls ────────────────────────────────────────────────────
    HWND m_hMetaOutDir{};
    HWND m_hMetaBrowse{};
    HWND m_hMetaOutMd{};
    HWND m_hMetaOutJson{};
    HWND m_hMetaUpdateExif{};
    HWND m_hMetaResizeFirst{};
    HWND m_hMetaResizeW{};         // combo (256/512/768/1024)
    HWND m_hMetaProvider{};
    HWND m_hMetaModel{};
    HWND m_hMetaCollection{};
    HWND m_hMetaCollectionLbl{};
    HWND m_hMetaRefresh{};
    HWND m_hMetaPreset{};          // preset combo
    HWND m_hMetaPrompt{};          // multi-line edit
    pmui::ReplicateSelectorState m_metaReplicateState;
    pmui::ReplicateSelectorState m_tfReplicateState;
    pmui::ReplicateSelectorState m_findReplicateState;
    pmui::ReplicateSelectorController m_repCtl;

    // ── Find controls ────────────────────────────────────────────────────
    HWND m_hFindPrompt{};
    HWND m_hFindLlm{};
    HWND m_hFindBypassCache{};
    HWND m_hFindNoGenerate{};
    HWND m_hFindMatchFolders{};
    HWND m_hFindRecursive{};
    HWND m_hFindUseMd{};
    HWND m_hFindUseJson{};
    HWND m_hFindUseExif{};
    HWND m_hFindMax{};
    HWND m_hFindProvider{};
    HWND m_hFindCollection{};
    HWND m_hFindCollectionLbl{};
    HWND m_hFindRefresh{};
    HWND m_hFindModel{};
    HWND m_hFindResizeW{};
    HWND m_hFindResizeFirst{};
    HWND m_hFindRefList{};
    HWND m_hFindRefAdd{};
    HWND m_hFindRefClear{};

    // Duplicates
    HWND m_hDupHowSec{};     ///< "How to compare" section label
    HWND m_hDupVisSec{};     ///< Visual match section label
    HWND m_hDupTextSec{};    ///< Text/EXIF section label
    HWND m_hDupPairSec{};   ///< "AI: JSON pair comparison" section label
    HWND m_hDupGenSec{};    ///< "Missing .json: Meta cataloguer" section label
    HWND m_hDupMetaGenHint{}; ///< Explains Meta tab uses image provider, not pair-compare
    HWND m_hDupMode{};
    HWND m_hDupMinGroup{};
    HWND m_hDupRecursive{};
    HWND m_hDupMaxHam{};
    HWND m_hDupFpSameSize{};
    HWND m_hDupUseMd{};
    HWND m_hDupUseJson{};
    HWND m_hDupUseExif{};
    HWND m_hDupMetaPrompt{};
    HWND m_hDupMetaLlm{};
    HWND m_hDupLlmSource{};
    HWND m_hDupLlmInfo{};
    HWND m_hDupMinSim{};
    HWND m_hDupImplicitMeta{}; ///< Generate missing .json via Meta (when LLM compare on)
    HWND m_hTooltipDup{};  ///< per-control tips for duplicates
    void OnFindRefAdd();
    void OnFindRefClear();
    void RefreshDupControlStates();
    void RefreshDupLlmInfoText();
    void InstallDuplicateTooltips();

    // ── State ─────────────────────────────────────────────────────────────
    /// All per-mode child controls are parented here; the outer view scrolls it.
    HWND m_hScrollContent{};
    int  m_scrollPos{0};
    int  m_contentHeight{0};
    /// Last `SettingsContentClientWidth()` used for field layout (`-1` = not yet applied).
    int  m_settingsLayoutContentW{-1};

    Mode m_mode = MODE_RESIZE;
    int  m_ratioW = 0, m_ratioH = 0;

    /** `appearance.display_language` at panel creation — drives resize-tab UI strings. */
    std::string m_display_language{"en"};
    bool m_updatingDims = false;
    /// Parallel to "LLM provider" combo: [0] = use Chat settings, then provider keys
    std::vector<std::string> m_dupLlmProviderKeys;

    std::vector<HWND> m_resizeControls;
    std::vector<HWND> m_transformControls;
    std::vector<HWND> m_compressControls;    // always shown in compress mode
    std::vector<HWND> m_cmpPngControls;      // shown only when PNG selected
    std::vector<HWND> m_cmpMozjpegControls;  // shown only when MozJPEG selected
    std::vector<HWND> m_metaControls;
    std::vector<HWND> m_findControls;
    std::vector<HWND> m_dupControls;

    HWND m_hTooltipSettings{}; ///< shared tooltips_class32 for Settings panel (Transform / Find / Meta buttons)

    /// While true, Replicate branches of `OnTfProviderChanged` / `OnMetaProviderChanged` / `OnFindProviderChanged`
    /// only update row visibility (network runs in `startReplicateNetworkBatchAsync`).
    bool m_deferReplicateNetworkInit = false;
    std::shared_ptr<std::atomic<bool>> m_replicateAsyncCancel;
};

/////////////////////////////////////////////////////////
class CSettingsContainer : public CDockContainerBase
{
public:
    CSettingsContainer();
    virtual ~CSettingsContainer() override = default;
    CSettingsView& GetSettingsView() { return m_view; }
private:
    CSettingsContainer(const CSettingsContainer&) = delete;
    CSettingsContainer& operator=(const CSettingsContainer&) = delete;
    CSettingsView m_view;
};

/////////////////////////////////////////////////////////
class CDockSettings : public CDockPanelBase
{
public:
    CDockSettings();
    virtual ~CDockSettings() override = default;
    CSettingsContainer& GetSettingsContainer() { return m_container; }
private:
    CDockSettings(const CDockSettings&) = delete;
    CDockSettings& operator=(const CDockSettings&) = delete;
    CSettingsContainer m_container;
};

#endif // PM_UI_SETTINGSPANEL_H
