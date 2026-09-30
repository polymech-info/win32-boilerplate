#ifndef PM_UI_STDAFX_H
#define PM_UI_STDAFX_H

#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <cassert>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>

#include <wxx_appcore.h>
#include <wxx_archive.h>
#include <wxx_controls.h>
#include <wxx_criticalsection.h>
#include <wxx_cstring.h>
#include <wxx_ddx.h>
#include <wxx_dialog.h>
#include <wxx_dockframe.h>
#include <wxx_docking.h>
#include <wxx_exception.h>
#include <wxx_file.h>
#include <wxx_filefind.h>
#include <wxx_folderdialog.h>
#include <wxx_frame.h>
#include <wxx_gdi.h>
#include <wxx_hglobal.h>
#include <wxx_imagelist.h>
#include <wxx_listview.h>
#include <wxx_mdi.h>
#include <wxx_menu.h>
#include <wxx_menubar.h>
#include <wxx_metafile.h>
#include <wxx_mutex.h>
#include <wxx_propertysheet.h>
#include <wxx_rebar.h>
#include <wxx_rect.h>
#include <wxx_regkey.h>
#include <wxx_richedit.h>
#include <wxx_scrollview.h>
#include <wxx_setup.h>
#include <wxx_socket.h>
#include <wxx_statusbar.h>
#include <wxx_stdcontrols.h>
#include <wxx_tab.h>
#include <wxx_textconv.h>
#include <wxx_themes.h>
#include <wxx_thread.h>
#include <wxx_time.h>
#include <wxx_toolbar.h>
#include <wxx_treeview.h>
#include <wxx_webbrowser.h>
#include <wxx_wincore.h>

#ifndef WIN32_LEAN_AND_MEAN
  #include <wxx_commondlg.h>
  #include <wxx_folderdialogex.h>
  #include <wxx_preview.h>
  #include <wxx_printdialogs.h>
  #include <wxx_printdialogex.h>
  #include <wxx_taskdialog.h>
  #if defined(_MSC_VER)
    #include <wxx_ribbon.h>
  #endif
#endif

// ── Shared ui_next utilities (available in every translation unit) ────────────
#include "helpers/text_conv.hpp"    // pmui::wide_to_utf8 / utf8_to_wide

#endif // PM_UI_STDAFX_H
