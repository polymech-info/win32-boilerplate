import { useMemo, useState } from "react";
import { X } from "lucide-react";

import { usePmChatStore } from "./pmChatStore";
import { CATEGORY_STYLE, getPathMimeCategory, pathFileName } from "./selectionPathIcons";

/** A single file chip — icon + truncated name, clickable to preview in explorer. */
function FileChip({ path, onClick }: { path: string; onClick: () => void }) {
  const name = pathFileName(path);
  const cat = getPathMimeCategory(path);
  const { icon: TypeIcon, color } = CATEGORY_STYLE[cat];
  return (
    <button
      type="button"
      title={path}
      onClick={onClick}
      className="flex shrink-0 items-center gap-1 rounded-md border border-slate-200/80 bg-white px-1.5 py-0.5 text-[11px] leading-tight text-slate-700 shadow-sm transition-colors hover:border-violet-400/60 hover:bg-violet-50/80 hover:text-violet-800 dark:border-slate-600/70 dark:bg-slate-800/80 dark:text-slate-300 dark:hover:border-violet-500/50 dark:hover:bg-violet-900/30 dark:hover:text-violet-200"
    >
      <TypeIcon size={12} strokeWidth={1.75} className="shrink-0" style={{ color }} aria-hidden />
      <span className="max-w-[120px] truncate font-medium">{name}</span>
    </button>
  );
}

/**
 * Compact strip that shows files written/created by agent tools during this session.
 * Appears between the filmstrip and the composer dock.
 * Click a chip → select in explorer + open preview.
 * × → dismiss (entries stay in transcript, strip just hides until next tool write).
 */
export function ToolOutputStrip() {
  const entries = usePmChatStore((s) => s.entries);
  const openPathInternal = usePmChatStore((s) => s.openPathInternal);

  // ID watermark: entries at or below this id are hidden after "clear".
  const [clearedBelowId, setClearedBelowId] = useState(0);

  // Collect unique file paths from tool-output entries (role:'file') above the watermark.
  const filePaths = useMemo(() => {
    const seen = new Set<string>();
    const result: string[] = [];
    for (const e of entries) {
      if (e.id <= clearedBelowId) continue;
      if (e.role !== "file" && e.role !== "image") continue;
      const p = (e.text ?? "").trim();
      if (!p || seen.has(p)) continue;
      seen.add(p);
      result.push(p);
    }
    return result;
  }, [entries, clearedBelowId]);

  if (filePaths.length === 0) return null;

  const maxId = entries.reduce((m, e) => (e.id > m ? e.id : m), 0);

  return (
    <div className="flex shrink-0 items-center gap-1.5 border-t border-slate-200/70 bg-slate-50/60 px-2 py-1 dark:border-slate-700/60 dark:bg-surface-dark/60">
      {/* Label */}
      <span className="shrink-0 text-[10px] font-semibold uppercase tracking-wide text-slate-400 dark:text-slate-500">
        Changed
      </span>

      {/* Scrollable chips */}
      <div className="pm-scroll flex min-w-0 flex-1 gap-1 overflow-x-auto">
        {filePaths.map((p) => (
          <FileChip key={p} path={p} onClick={() => openPathInternal(p)} />
        ))}
      </div>

      {/* Clear button */}
      <button
        type="button"
        title="Dismiss changed-files strip"
        aria-label="Dismiss changed-files strip"
        onClick={() => setClearedBelowId(maxId)}
        className="ml-0.5 shrink-0 rounded p-0.5 text-slate-400 transition-colors hover:bg-slate-200/70 hover:text-slate-600 dark:text-slate-500 dark:hover:bg-slate-700/60 dark:hover:text-slate-300"
      >
        <X size={13} strokeWidth={2} aria-hidden />
      </button>
    </div>
  );
}
