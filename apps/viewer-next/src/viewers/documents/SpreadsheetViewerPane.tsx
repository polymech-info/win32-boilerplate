import React, { useEffect, useState } from "react";
import { Loader2 } from "lucide-react";
import * as XLSX from "xlsx";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedArrayBuffer } from "@/bridge/hostedFileFetch";

const MAX_ROWS = 500;

function cellHasDisplayValue(cell: XLSX.CellObject | undefined): boolean {
  if (!cell) return false;
  if (cell.v === undefined || cell.v === null) return false;
  if (typeof cell.v === "string") return cell.v.trim().length > 0;
  if (typeof cell.v === "number" || typeof cell.v === "boolean") return true;
  if (cell.v instanceof Date) return true;
  return String(cell.v).trim().length > 0;
}

/** Shrink `!ref` so trailing all-blank rows (common Excel sheet padding) are not rendered. */
function trimTrailingBlankRows(ws: XLSX.WorkSheet): void {
  if (!ws["!ref"]) return;
  const range = XLSX.utils.decode_range(ws["!ref"]);
  let last = range.s.r - 1;
  for (let r = range.s.r; r <= range.e.r; r++) {
    let any = false;
    for (let c = range.s.c; c <= range.e.c; c++) {
      if (cellHasDisplayValue(ws[XLSX.utils.encode_cell({ r, c })])) {
        any = true;
        break;
      }
    }
    if (any) last = r;
  }
  if (last < range.s.r) last = range.s.r;
  if (last >= range.e.r) return;
  range.e.r = last;
  ws["!ref"] = XLSX.utils.encode_range(range);
}

export function SpreadsheetViewerPane({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const err = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const url = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  const fileName = typeof f.hostedFileName === "string" ? f.hostedFileName : "Spreadsheet";

  const [html, setHtml] = useState("");
  const [loading, setLoading] = useState(true);
  const [parseErr, setParseErr] = useState<string | null>(null);

  useEffect(() => {
    if (!url) {
      setLoading(false);
      setParseErr(null);
      setHtml("");
      return;
    }
    const ac = new AbortController();
    let active = true;
    (async () => {
      setLoading(true);
      setParseErr(null);
      setHtml("");
      try {
        // Static import: Webpack `import('xlsx')` emitted async chunks (e.g. 959.viewer.bundle.js) that
        // the Win32 embed cannot load (no sibling script URL on the viewer vhost).
        const buf = await fetchHostedArrayBuffer(url, ac.signal);
        const workbook = XLSX.read(buf, { type: "array", cellDates: true });
        const first = workbook.SheetNames[0];
        if (!first) throw new Error("Workbook has no sheets");
        const worksheet = workbook.Sheets[first];
        let truncated = false;
        if (worksheet["!ref"]) {
          const range = XLSX.utils.decode_range(worksheet["!ref"]);
          const rowCount = range.e.r - range.s.r + 1;
          if (rowCount > MAX_ROWS) {
            truncated = true;
            range.e.r = range.s.r + MAX_ROWS - 1;
            worksheet["!ref"] = XLSX.utils.encode_range(range);
          }
        }
        trimTrailingBlankRows(worksheet);
        const tableHtml = XLSX.utils.sheet_to_html(worksheet);
        let out = tableHtml;
        if (truncated) {
          out += `<div class="pt-2 text-center text-sm text-amber-700 dark:text-amber-300/90">Preview limited to the first ${MAX_ROWS} rows. Open the file elsewhere for the full sheet.</div>`;
        }
        if (active) {
          setHtml(out);
          setLoading(false);
        }
      } catch (e: unknown) {
        if (ac.signal.aborted) return;
        const msg = e instanceof Error ? e.message : "Failed to parse spreadsheet";
        if (active) {
          setParseErr(msg);
          setLoading(false);
        }
      }
    })();
    return () => {
      active = false;
      ac.abort();
    };
  }, [url]);

  if (err) {
    const fsz = typeof f.hostedFileSizeBytes === "number" ? f.hostedFileSizeBytes : 0;
    const maxB = typeof f.hostedFileMaxBytes === "number" ? f.hostedFileMaxBytes : 0;
    return (
      <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center text-center text-slate-600 dark:text-slate-400">
        <p className="mb-2 text-sm">{err}</p>
        {fsz > 0 && maxB > 0 ? (
          <p className="text-xs text-slate-500 dark:text-slate-500">
            ({(fsz / (1024 * 1024)).toFixed(2)} MB / max {(maxB / (1024 * 1024)).toFixed(0)} MB)
          </p>
        ) : null}
      </div>
    );
  }

  if (!url) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center text-sm text-slate-500 dark:text-slate-400">
        No spreadsheet loaded.
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden bg-transparent">
      <div
        className="flex shrink-0 items-center justify-between border-b px-2 py-1"
        style={{ borderColor: "var(--border)" }}
      >
        <span className="max-w-[90%] truncate text-sm font-medium text-slate-800 dark:text-slate-100">
          {fileName}
        </span>
      </div>
      <div className="pm-scroll min-h-0 flex-1 overflow-auto">
        {loading ? (
          <div className="flex h-full items-center justify-center gap-2 text-slate-500 dark:text-slate-400">
            <Loader2 className="h-5 w-5 animate-spin" />
            <span className="text-sm">Parsing spreadsheet…</span>
          </div>
        ) : parseErr ? (
          <div className="flex h-full items-center justify-center px-3 text-sm text-red-600 dark:text-red-400">
            {parseErr}
          </div>
        ) : (
          <div className="pm-xlsx overflow-x-auto" dangerouslySetInnerHTML={{ __html: html }} />
        )}
      </div>
    </div>
  );
}
