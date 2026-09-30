# In-app Explorer selection vs batch commands

This doc ties **IExplorerBrowser** list selection to **Resize / Compress / Meta / Transform** (and the same pattern elsewhere). Code paths: `FileTreePanel.cpp` (`CheckSelection`), `Mainfrm.cpp` (`OnExplorerSelection`), `Mainfrm_commands.cpp`, `Mainfrm_queue.cpp` (`AddFilesToQueue`, `TryEnqueueCurrentExplorerFolderIfEmpty`).

## Intended product behavior

| Situation | Expected command inputs |
|-----------|-------------------------|
| **Nothing selected** in the Explorer list while browsing a real filesystem folder | Treat as **“all enqueueable images under the current folder”** (same as enqueueing that folder). |
| **One or more items selected** (files and/or folders) | Run on **only** those items: files directly; each selected folder expanded to its images (consistent with `AddFilesToQueue` / `paths_to_enqueue` semantics). |
| **Mixed**: some rows selected | Union of expanded paths from the selection only — not the whole browsed directory. |

## Current behavior (as implemented)

### Empty selection → current folder (mostly matches intent)

When `m_explorerSelectionPaths` is empty, commands fall back to the **file queue**. If the queue is empty, **`TryEnqueueCurrentExplorerFolderIfEmpty()`** runs: it takes **`GetCurrentFolder()`** (the folder the pane is **navigated to**), not a highlighted row, and calls **`AddFilesToQueue({ folder }, false)`**.

So **browsing `D:\work\in` with no selection** and an empty queue should enqueue from **`D:\work\in`** before Resize/Meta/etc. That is the effective **default** for “nothing selected,” as long as the queue was empty.

Caveats to keep in mind:

- If the queue is **not** empty, commands process **the whole queue** without trimming to the Explorer folder. Users can clear the queue or rely on selection once selection is fixed.
- Virtual / non-filesystem locations may yield an empty `GetCurrentFolder()`; behavior degrades gracefully (empty queue message).

### Folder-only or folder-in-selection → broken today

**`CheckSelection()`** only forwards paths whose extension passes **`is_image_ext` or `is_text_ext`**. A selected **directory** has no such extension, so it **never** enters the posted selection vector.

**`OnExplorerSelection`** then copies **only image paths** into **`m_explorerSelectionPaths`**, so folders are excluded again.

Result: selecting **only** a subfolder (e.g. `sub`) while browsing the parent leaves **`m_explorerSelectionPaths` empty**. Commands then use the **full queue** or **`GetCurrentFolder()`** (the parent), so work runs on **siblings** and misses the intended “selected folder only” semantics. This matches the failure mode: parent files + `sub` files if both were ever enqueued, or parent folder scope instead of `sub`.

## Implementation (done in tree)

- **`FileTreePanel.cpp` — `CheckSelection`:** includes filesystem directories in the posted path list (still filters non-dir files to image/text).
- **`Mainfrm.cpp` — `OnExplorerSelection`:** copies directories and image files into **`m_explorerSelectionPaths`**; preview uses the first image, else first markdown/text; folder-only clears image preview.
- **`UpdateExplorerSelectionStatusPart`:** sums sizes for regular files only; label uses **item(s)** so folders are accurate.

## Residual QA

- Browse `in`, select only `sub`, Resize → only files under `in\sub`.
- Browse `in`, select nothing, empty queue → all files under `in` (unchanged).
- Multi-select `a.jpg` + `sub` → union from **`AddFilesToQueue`** only.

## See also

- [explorer.md](explorer.md) — shell context menu / `IExecute` (separate from in-app Explorer pane).
- `FileTreePanel.h` — selection polling notes (`TIMER_SELECTION`, `IFolderView2::GetSelection`).
