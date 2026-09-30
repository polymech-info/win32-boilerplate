import { useCallback, useEffect, useRef, useState } from "react";
import type { PointerEvent as ReactPointerEvent, WheelEvent as ReactWheelEvent } from "react";
import { Loader2, RotateCcw, ZoomIn, ZoomOut } from "lucide-react";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedArrayBuffer } from "@/bridge/hostedFileFetch";

function isNativeFilePath(url: string): boolean {
  return url.startsWith("file://");
}

function imageMimeFromName(name: string): string {
  const ext = name.split(".").pop()?.toLowerCase() ?? "";
  if (ext === "svg") return "image/svg+xml";
  if (ext === "jpg" || ext === "jpeg") return "image/jpeg";
  if (ext === "png") return "image/png";
  if (ext === "gif") return "image/gif";
  if (ext === "webp") return "image/webp";
  if (ext === "bmp") return "image/bmp";
  if (ext === "avif") return "image/avif";
  if (ext === "ico") return "image/x-icon";
  return "application/octet-stream";
}

export function ImageViewerPane({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const err = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const url = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  const fileName = typeof f.hostedFileName === "string" ? f.hostedFileName : "Image";

  const [zoom, setZoom] = useState(1);
  const [src, setSrc] = useState("");
  const [blobLoading, setBlobLoading] = useState(false);
  const [blobErr, setBlobErr] = useState("");
  const [directFailed, setDirectFailed] = useState(false);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const dragRef = useRef<{ pointerId: number; x: number; y: number; panX: number; panY: number } | null>(null);

  const resetView = useCallback(() => {
    setZoom(1);
    setPan({ x: 0, y: 0 });
  }, []);

  const zoomBy = useCallback((factor: number) => {
    setZoom((z) => Math.min(8, Math.max(0.1, z * factor)));
  }, []);

  useEffect(() => {
    resetView();
    setSrc("");
    setBlobErr("");
    setDirectFailed(false);
    if (!url) return;
    if (!isNativeFilePath(url)) {
      setSrc(url);
      return;
    }

    const ac = new AbortController();
    setBlobLoading(true);
    fetchHostedArrayBuffer(url, ac.signal)
      .then((buf) => {
        if (ac.signal.aborted) return;
        setSrc(URL.createObjectURL(new Blob([buf], { type: imageMimeFromName(fileName) })));
        setBlobLoading(false);
      })
      .catch((e) => {
        if (ac.signal.aborted) return;
        setBlobErr(e instanceof Error ? e.message : "Failed to load image");
        setBlobLoading(false);
      });

    return () => ac.abort();
  }, [url, fileName, resetView]);

  useEffect(() => {
    return () => {
      if (src.startsWith("blob:")) URL.revokeObjectURL(src);
    };
  }, [src]);

  useEffect(() => {
    if (!directFailed || !url || isNativeFilePath(url) || src.startsWith("blob:")) return;
    const ac = new AbortController();
    setBlobLoading(true);
    fetchHostedArrayBuffer(url, ac.signal)
      .then((buf) => {
        if (ac.signal.aborted) return;
        setSrc(URL.createObjectURL(new Blob([buf], { type: imageMimeFromName(fileName) })));
        setBlobErr("");
        setBlobLoading(false);
      })
      .catch((e) => {
        if (ac.signal.aborted) return;
        setBlobErr(e instanceof Error ? e.message : "Failed to load image");
        setBlobLoading(false);
      });
    return () => ac.abort();
  }, [directFailed, fileName, src, url]);

  const displayErr = err || blobErr;
  if (displayErr) {
    return (
      <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center p-6 text-center text-slate-600 dark:text-slate-400">
        <p className="mb-2 text-sm">{displayErr}</p>
      </div>
    );
  }

  if (!url) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center p-6 text-sm text-slate-500 dark:text-slate-400">
        No image loaded.
      </div>
    );
  }

  const onPointerDown = (event: ReactPointerEvent<HTMLDivElement>) => {
    if (!src || event.button !== 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    dragRef.current = {
      pointerId: event.pointerId,
      x: event.clientX,
      y: event.clientY,
      panX: pan.x,
      panY: pan.y,
    };
  };

  const onPointerMove = (event: ReactPointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) return;
    setPan({
      x: drag.panX + event.clientX - drag.x,
      y: drag.panY + event.clientY - drag.y,
    });
  };

  const onPointerEnd = (event: ReactPointerEvent<HTMLDivElement>) => {
    if (dragRef.current?.pointerId !== event.pointerId) return;
    dragRef.current = null;
    try {
      event.currentTarget.releasePointerCapture(event.pointerId);
    } catch {
      /* pointer capture may already be released */
    }
  };

  const onWheel = (event: ReactWheelEvent<HTMLDivElement>) => {
    if (!src) return;
    event.preventDefault();
    zoomBy(event.deltaY < 0 ? 1.12 : 1 / 1.12);
  };

  return (
    <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden bg-transparent">
      <div className="flex shrink-0 items-center justify-between gap-2 border-b px-2 py-1" style={{ borderColor: "var(--border)" }}>
        <span className="min-w-0 truncate text-sm font-medium">{fileName}</span>
        <div className="flex shrink-0 items-center gap-1 text-xs">
          <button type="button" className="rounded border px-2 py-0.5" style={{ borderColor: "var(--border)" }} onClick={() => zoomBy(1 / 1.25)}>
            <ZoomOut className="h-3.5 w-3.5" />
          </button>
          <button type="button" className="min-w-[4.5ch] rounded border px-2 py-0.5" style={{ borderColor: "var(--border)" }} onClick={resetView}>
            {Math.round(zoom * 100)}%
          </button>
          <button type="button" className="rounded border px-2 py-0.5" style={{ borderColor: "var(--border)" }} onClick={() => zoomBy(1.25)}>
            <ZoomIn className="h-3.5 w-3.5" />
          </button>
          <button type="button" className="rounded border px-2 py-0.5" style={{ borderColor: "var(--border)" }} onClick={resetView} title="Reset zoom and pan">
            <RotateCcw className="h-3.5 w-3.5" />
          </button>
        </div>
      </div>
      <div
        className="min-h-0 flex-1 overflow-hidden bg-zinc-100 dark:bg-black"
        onDoubleClick={resetView}
        onPointerCancel={onPointerEnd}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerEnd}
        onWheel={onWheel}
        style={{ cursor: src ? (dragRef.current ? "grabbing" : "grab") : "default", touchAction: "none" }}
      >
        <div className="flex h-full w-full items-center justify-center p-4">
          {blobLoading || !src ? (
            <div className="flex items-center gap-2 text-slate-500 dark:text-slate-400">
              <Loader2 className="h-5 w-5 animate-spin" />
              <span className="text-sm">Loading image...</span>
            </div>
          ) : (
            <img
              key={src}
              src={src}
              alt={fileName}
              className="select-none"
              draggable={false}
              onError={() => setDirectFailed(true)}
              style={{
                maxWidth: "100%",
                maxHeight: "100%",
                transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`,
                transformOrigin: "center center",
                height: "auto",
              }}
            />
          )}
        </div>
      </div>
    </div>
  );
}
