import React, { useEffect, useRef, useState } from "react";
import { Loader2 } from "lucide-react";
import { pdfjs, Document, Page } from "react-pdf";
import "react-pdf/dist/Page/AnnotationLayer.css";
import "react-pdf/dist/Page/TextLayer.css";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedArrayBuffer } from "@/bridge/hostedFileFetch";

/** Single-file Webpack embed cannot reliably bundle the worker path; use unpkg matching pdfjs.version. */
function ensurePdfWorker(): void {
  if (pdfjs.GlobalWorkerOptions.workerSrc) return;
  pdfjs.GlobalWorkerOptions.workerSrc = `https://unpkg.com/pdfjs-dist@${pdfjs.version}/build/pdf.worker.min.mjs`;
}

export function PdfViewerPane({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const err = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const url = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  const fileName = typeof f.hostedFileName === "string" ? f.hostedFileName : "PDF";

  const [numPages, setNumPages] = useState<number>();
  const [scale, setScale] = useState(1.0);
  const [containerWidth, setContainerWidth] = useState<number>();
  const [pdfData, setPdfData] = useState<ArrayBuffer | null>(null);
  const [pdfLoadErr, setPdfLoadErr] = useState<string | null>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    ensurePdfWorker();
  }, []);

  useEffect(() => {
    setNumPages(undefined);
    setPdfData(null);
    setPdfLoadErr(null);
  }, [url]);

  useEffect(() => {
    if (!url) return;
    const ac = new AbortController();
    (async () => {
      try {
        const buf = await fetchHostedArrayBuffer(url, ac.signal);
        setPdfData(buf);
        setPdfLoadErr(null);
      } catch (e) {
        if (ac.signal.aborted) return;
        setPdfData(null);
        setPdfLoadErr(e instanceof Error ? e.message : "Failed to load PDF bytes");
      }
    })();
    return () => ac.abort();
  }, [url]);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      for (const entry of entries) setContainerWidth(entry.contentRect.width);
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

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
        No PDF loaded.
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden bg-transparent">
      <div className="flex shrink-0 items-center justify-between px-2 py-1">
        <span className="max-w-[70%] truncate text-sm font-medium">{fileName}</span>
        <div className="flex items-center gap-3 text-xs opacity-80">
          <span>{numPages ? `${numPages} page${numPages !== 1 ? "s" : ""}` : "—"}</span>
          <div className="flex items-center gap-1">
            <button
              type="button"
              className="rounded border px-2 py-0.5"
              style={{ borderColor: "var(--border)" }}
              onClick={() => setScale((s) => Math.max(0.5, s - 0.2))}
            >
              −
            </button>
            <span className="min-w-[3ch] text-center">{Math.round(scale * 100)}%</span>
            <button
              type="button"
              className="rounded border px-2 py-0.5"
              style={{ borderColor: "var(--border)" }}
              onClick={() => setScale((s) => Math.min(3, s + 0.2))}
            >
              +
            </button>
          </div>
        </div>
      </div>
      <div
        ref={containerRef}
        className="pm-scroll min-h-0 flex-1 overflow-auto"
      >
        <div className="flex flex-col items-center gap-2">
          {pdfLoadErr ? (
            <div className="text-sm text-red-500">{pdfLoadErr}</div>
          ) : !pdfData ? (
            <div className="flex items-center gap-2 text-slate-500">
              <Loader2 className="h-5 w-5 animate-spin" />
              <span className="text-sm">Loading PDF…</span>
            </div>
          ) : (
            <Document
              key={url}
              file={pdfData}
              onLoadSuccess={({ numPages: n }) => setNumPages(n)}
              loading={
                <div className="flex items-center gap-2 text-slate-500">
                  <Loader2 className="h-5 w-5 animate-spin" />
                  <span className="text-sm">Rendering PDF…</span>
                </div>
              }
              error={<div className="text-sm text-red-500">Failed to load PDF.</div>}
            >
              {numPages
                ? Array.from({ length: numPages }, (_, i) => (
                    <Page
                      key={i + 1}
                      pageNumber={i + 1}
                      scale={scale}
                      width={containerWidth}
                      renderTextLayer
                      renderAnnotationLayer
                      className="shadow-lg"
                    />
                  ))
                : null}
            </Document>
          )}
        </div>
      </div>
    </div>
  );
}
