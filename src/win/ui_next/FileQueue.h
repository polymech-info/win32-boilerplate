#ifndef PM_UI_FILEQUEUE_H
#define PM_UI_FILEQUEUE_H

#include "stdafx.h"
#include "helpers/dock_helpers.h"
#include <shellapi.h>
#include <string>

// Column indices for the queue list view.
enum QueueCol : int { COL_NAME = 0, COL_OPERATION, COL_STATUS, COL_PATH, COL_COUNT };

// Payload for UWM_QUEUE_TOOL_CALL. Keep `col_path` empty for non-file rows
// so a queue click does not try to open it as a path in the image preview.
namespace pmui {
struct QueueToolCallRowW {
    std::wstring col_name;
    std::wstring col_operation; // e.g. "image_transform", "image_create", "Share"
    std::wstring col_status;
    std::wstring col_path; // display note or "—"; use empty to skip preview
};
/// Post to the main frame's `CMainFrame::OnQueueToolCall` (non-blocking).
bool PmPostQueueToolCallRow(HWND frame_hwnd, std::wstring col_name, std::wstring col_operation,
                            std::wstring col_status, std::wstring col_path = {});

} // namespace pmui

/////////////////////////////////////////////////////////
// CQueueListView — accepts drag-drop of files/folders,
// displays them in a report-style list, tracks status.
// (Startup "open folder in app" from Explorer seeds the file tree only — no rows added here.)
// LLM image outputs (chat and ribbon flows) are appended via the main frame's
// CMainFrame::OnGeneratedFile (UWM_GENERATED_FILE). LLM tool call/result lines use
// CMainFrame::OnQueueToolCall (UWM_QUEUE_TOOL_CALL) and AddToolCallRow.
class CQueueListView : public CListView
{
public:
    CQueueListView() = default;
    virtual ~CQueueListView() override = default;

    int  AddFile(const CString& path);
    /// One row (e.g. LLM tool call / result). `path` column is a note only;
    /// leave it empty to avoid the preview trying to open it.
    int  AddToolCallRow(const CString& name, const CString& operation, const CString& status, const CString& path);
    /// Status column text (e.g. "Queued", "Uploading 3/10 …"). Long jobs such as
    /// Pixlwiz Share post `UWM_PIXLWIZ_SHARE_PROGRESS` from a worker; the frame calls this on the UI thread.
    void SetItemStatus(int item, LPCWSTR status);
    void ClearAll();
    /** Remove all selected rows (e.g. DEL key). Indices deleted high-to-low. */
    void RemoveSelectedItems();
    int  QueueCount() const;
    CString GetItemPath(int item);

    /// Re-apply current theme palette (background + text) to the listview.
    void RefreshThemeColors();

protected:
    virtual void    OnAttach() override;
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    virtual LRESULT OnNotifyReflect(WPARAM wparam, LPARAM lparam) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CQueueListView(const CQueueListView&) = delete;
    CQueueListView& operator=(const CQueueListView&) = delete;

    LRESULT OnDropFiles(UINT msg, WPARAM wparam, LPARAM lparam);
    void SetupColumns();
    void LayoutColumns();
};

/////////////////////////////////////////////////////////
// CQueueContainer — dock container hosting CQueueListView.
class CQueueContainer : public CDockContainerBase
{
public:
    CQueueContainer();
    virtual ~CQueueContainer() override = default;
    CQueueListView& GetListView() { return m_listView; }

private:
    CQueueContainer(const CQueueContainer&) = delete;
    CQueueContainer& operator=(const CQueueContainer&) = delete;
    CQueueListView m_listView;
};

/////////////////////////////////////////////////////////
// CDockQueue — docker wrapping CQueueContainer.
class CDockQueue : public CDockPanelBase
{
public:
    CDockQueue();
    virtual ~CDockQueue() override = default;
    CQueueContainer& GetQueueContainer() { return m_container; }

private:
    CDockQueue(const CDockQueue&) = delete;
    CDockQueue& operator=(const CDockQueue&) = delete;
    CQueueContainer m_container;
};

#endif // PM_UI_FILEQUEUE_H
