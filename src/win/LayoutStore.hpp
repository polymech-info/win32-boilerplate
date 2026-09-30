#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace media::layout {

/**
 * One column in the Explorer Details view: identity + display width.
 * The GUID is stored as a formatted string so the struct is JSON-friendly
 * without pulling <objbase.h> into every consumer.
 * Format: "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" (as produced by StringFromGUID2).
 */
struct FiletreeColumnSpec {
    std::string fmtid_str;   ///< Formatted GUID string
    DWORD       pid   = 0;   ///< Property ID
    UINT        width = 120; ///< Column width in pixels
};

/**
 * Main window placement, panel visibility, and lightweight UI state.
 *
 * Dock topology is separate (`win32_dock`); this struct is the single in-memory
 * representation for `workbench.<slot>.window`, standalone layout import/export,
 * and session replay snapshots.
 */
struct WindowLayout {
    bool  has_placement = false;
    int   show_cmd      = 1;          // SW_* constant
    POINT min_pos       = {-1, -1};
    POINT max_pos       = {-1, -1};
    RECT  normal_rect   = {100, 100, 1300, 900};

    /** When true, the document carried explicit panel visibility. */
    bool has_panel_visibility = false;
    /** User-intent visibility for ribbon View toggles; applied after dock restore. */
    bool pv_queue = true;
    bool pv_log = true;
    bool pv_settings = true;
    bool pv_fileinfo = true;
    bool pv_filetree = true;
    bool pv_nodes = false;
    bool pv_findresults = false;
    bool pv_dupresults  = false;
    bool pv_chat        = false;
    bool pv_console     = false;

    /** Centre preview mode: false = plain CFileViewer, true = tab host around it. */
    bool center_view_tabbed = false;

    /** Last folder navigated to in the Explorer panel (UTF-8 path, empty = Desktop). */
    std::string filetree_folder;

    /** File -> Recent files/folders (UTF-8 paths, newest first; max 15). */
    std::vector<std::string> recent_files;
    std::vector<std::string> recent_folders;

    /**
     * When true, `IExplorerBrowser` is created with `EBO_SHOWFRAMES` (address bar,
     * command rows, navigation tree).
     */
    bool filetree_show_shell_frames = false;

    /**
     * Glob mask applied to the Explorer Details view (semicolon-separated,
     * e.g. "*.jpg;*.png"). Folders are always visible so navigation is
     * unaffected. Default "*.*" means no filtering.
     */
    std::string filetree_filter_mask = "*.*";

    /**
     * Ordered list of visible columns in the Details view with their widths.
     * Empty = use Shell defaults (do not touch IColumnManager).
     * Populated by SnapshotColumnSettings and applied by ApplyColumnSettings
     * after each navigation.
     */
    std::vector<FiletreeColumnSpec> filetree_columns;

    /**
     * IFolderView2 view mode (FOLDERVIEWMODE integer):
     *   1=Large icons  2=Small icons  3=List  4=Details  5=Thumbnails
     *   6=Tiles  7=Thumbstrip  8=Content   -1/0 = use Shell default.
     * Default 4 (FVM_DETAILS) matches the hard-coded FOLDERSETTINGS in InitBrowser.
     */
    int filetree_view_mode = 4;   // FVM_DETAILS

    /**
     * Icon / thumbnail pixel size for icon-based views (16, 32, 48, 96, 256…).
     * 0 = let the Shell choose the default for the selected view mode.
     */
    int filetree_icon_size = 0;

    /**
     * When true (default), the main frame defers `LoadDockContainers` until after
     * first show via a posted message.
     */
    bool defer_dock_containers = true;
};

struct WorkbenchWindowDefaults {
    int width  = 0; ///< 0 = unset
    int height = 0;
    bool has_size() const noexcept { return width > 0 && height > 0; }
};

class LayoutStore final {
public:
    LayoutStore() = delete;

    /** Last loaded/persisted preference for deferred dock container restore. */
    static bool DeferDockContainerLoad();

    /** Parse a raw `window` JSON object into @p out, including validation/sanitizing. */
    static bool ParseWindowLayout(const nlohmann::json& window_json, WindowLayout& out, std::string& err);

    /** Serialize @p layout as the canonical raw `window` JSON object. */
    static nlohmann::json WindowLayoutToJson(const WindowLayout& layout);

    /** Serialize the trimmed portable `window` object used by File -> Save/Load Layout. */
    static nlohmann::json WindowLayoutToExportJson(const WindowLayout& layout);

    /** Load `workbench.<slot>.window` from settings.json, with defaults for missing data. */
    static bool LoadWindowLayout(WindowLayout& out, std::string& err, const char* workbench_slot = "main");

    /** Save `workbench.<slot>.window` to settings.json, preserving unrelated settings. */
    static bool SaveWindowLayout(const WindowLayout& layout, std::string& err, const char* workbench_slot = "main");

    /** Update only the Explorer shell-frame flag inside the selected workbench window layout. */
    static bool SetFiletreeShowShellFrames(bool show, std::string& err, const char* workbench_slot = "main");

    /** Update only the Explorer glob-filter mask inside the selected workbench window layout. */
    static bool SetFiletreeFilterMask(const std::string& mask, std::string& err, const char* workbench_slot = "main");

    /** Load `workbench.<slot>.window_defaults` from settings.json. */
    static bool LoadWorkbenchWindowDefaults(WorkbenchWindowDefaults& out, std::string& err,
                                            const char* workbench_slot = "main");
};

} // namespace media::layout
