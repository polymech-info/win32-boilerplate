import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { Plus } from "lucide-react";

import { collectPathsFromDataTransfer, isFileLikeExternalDrag } from "./fileDrop";
import { hasWebProviderHost, postHost } from "./hostBridge";
import { t } from "./i18n.js";
import { rewriteLocalPath } from "./markdownSetup";
import { usePmChatStore } from "./pmChatStore";
import { CATEGORY_STYLE, getPathMimeCategory, pathFileName } from "./selectionPathIcons";

const FILM_STRIP_THUMB_LS = "pmImageChat.filmStripThumbPx";
/** Matches previous `h-24` / `w-24` (96px) strip. */
const DEFAULT_THUMB_PX = 96;
const MIN_THUMB_PX = 56;
const MAX_THUMB_PX = 200;

function readStoredThumbPx(): number {
  try {
    const raw = localStorage.getItem(FILM_STRIP_THUMB_LS);
    if (raw == null) return DEFAULT_THUMB_PX;
    const n = Number.parseInt(raw, 10);
    if (!Number.isFinite(n)) return DEFAULT_THUMB_PX;
    return Math.min(MAX_THUMB_PX, Math.max(MIN_THUMB_PX, n));
  } catch {
    return DEFAULT_THUMB_PX;
  }
}

/** Vite `public/` assets and http(s) URLs must not go through `rewriteLocalPath` (avoids broken `file://`). */
function thumbSrc(path: string): string {
  if (/^(https?:|data:)/i.test(path)) return path;
  if (path.startsWith("/")) return path;
  return rewriteLocalPath(path);
}

function isLikelyImagePath(path: string): boolean {
  return /\.(jpe?g|png|gif|webp|avif|svg|bmp|heic|heif)(\?.*)?$/i.test(pathFileName(path));
}

function SelectionThumb({
  path,
  thumbPx,
  variant,
  activateTitle,
  onActivate,
  onRemove,
}: {
  path: string;
  thumbPx: number;
  variant: "explorer" | "added";
  activateTitle: string;
  onActivate: (e: React.MouseEvent | React.KeyboardEvent) => void;
  onRemove: (p: string) => void;
}) {
  const [failed, setFailed] = useState(false);
  const name = pathFileName(path);
  const showImg = isLikelyImagePath(path) && !failed;
  const url = thumbSrc(path);
  const btn = Math.max(24, Math.min(36, Math.round(thumbPx * 0.3)));
  const icon = Math.max(14, Math.min(18, Math.round(thumbPx * 0.18)));
  const stripCat = getPathMimeCategory(path);
  const { icon: TypeIcon, color: typeColor } = CATEGORY_STYLE[stripCat];
  const typeIconPx = Math.max(22, Math.min(44, Math.round(thumbPx * 0.38)));

  const ring =
    variant === "added"
      ? "ring-2 ring-inset ring-violet-500/40 dark:ring-violet-400/35"
      : "";

  return (
    <div
      className={`group relative flex-shrink-0 overflow-hidden rounded-lg border border-slate-200/90 bg-slate-100 dark:border-slate-600 dark:bg-slate-800 ${ring}`}
      style={{ width: thumbPx, height: thumbPx, minWidth: thumbPx, minHeight: thumbPx }}
      title={variant === "added" ? `${t("selectionStripAddedTitle")}\n${path}`.trim() : undefined}
    >
      <div
        role="button"
        tabIndex={0}
        className="absolute inset-0 flex cursor-pointer flex-col outline-none select-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-violet-400/70"
        title={activateTitle}
        onClick={(e) => {
          e.preventDefault();
          onActivate(e);
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            onActivate(e);
          }
        }}
      >
        {showImg ? (
          <img
            src={url}
            alt={name}
            className="h-full w-full object-cover"
            draggable={false}
            onError={() => setFailed(true)}
          />
        ) : (
          <div className="flex h-full w-full flex-col items-center justify-center gap-0.5 bg-slate-200/80 px-0.5 pt-0.5 dark:bg-slate-700/80">
            <TypeIcon size={typeIconPx} strokeWidth={1.75} className="shrink-0" style={{ color: typeColor }} aria-hidden />
            <span
              className={`max-w-full truncate px-1 text-center font-medium leading-tight text-slate-600 dark:text-slate-300 ${thumbPx < 72 ? "text-[9px]" : "text-[10px] sm:text-xs"}`}
            >
              {name.length > 18 ? `${name.slice(0, 16)}…` : name}
            </span>
          </div>
        )}
      </div>
      <button
        type="button"
        className="absolute top-1 right-1 z-10 flex items-center justify-center rounded-full bg-black/55 text-white opacity-0 shadow-sm transition-opacity hover:bg-red-600/90 group-hover:opacity-100 focus-visible:opacity-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-violet-400"
        style={{ width: btn, height: btn }}
        title={t("selectionStripRemoveTip")}
        aria-label={t("selectionStripRemoveTip")}
        onClick={(e) => {
          e.stopPropagation();
          onRemove(path);
        }}
      >
        <svg className="shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden style={{ width: icon, height: icon }}>
          <path d="M18 6L6 18M6 6l12 12" />
        </svg>
      </button>
    </div>
  );
}

/** Horizontal thumbnails for current chat context paths (above Quick tools). */
export function SelectionFilmStrip({ forceShow }: { forceShow?: boolean } = {}) {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const selection = usePmChatStore((s) => s.selection);
  const explorerSelection = usePmChatStore((s) => s.explorerSelection);
  const contextExtraPaths = usePmChatStore((s) => s.contextExtraPaths);
  const removeSelectionPath = usePmChatStore((s) => s.removeSelectionPath);
  const openGeneratedPath = usePmChatStore((s) => s.openGeneratedPath);
  const openSelectionPathFullscreen = usePmChatStore((s) => s.openSelectionPathFullscreen);
  const scrollRef = useRef<HTMLDivElement>(null);
  const [thumbPx, setThumbPx] = useState(readStoredThumbPx);
  const [stripDragOver, setStripDragOver] = useState(false);
  const showStrip = forceShow || hasWebProviderHost() || selection.length > 0;

  const baseActivateTitle = useMemo(
    () =>
      `${t("openGenImageDefaultApp")}\n${t("openGenImageInternalHint")}\n${t("openGenImageShiftHint")}`.trim(),
    [i18nRev],
  );

  const onThumbActivate = useCallback(
    (pathRaw: string, e: React.MouseEvent | React.KeyboardEvent) => {
      if ("shiftKey" in e && e.shiftKey) {
        e.preventDefault();
        openSelectionPathFullscreen(pathRaw, true);
        return;
      }
      if ("ctrlKey" in e && (e.ctrlKey || ("metaKey" in e && e.metaKey))) {
        e.preventDefault();
        openSelectionPathFullscreen(pathRaw, false);
        return;
      }
      openGeneratedPath(pathRaw);
    },
    [openGeneratedPath, openSelectionPathFullscreen],
  );

  const onRemove = useCallback(
    (p: string) => {
      removeSelectionPath(p);
    },
    [removeSelectionPath],
  );

  const pickContextFiles = useCallback(() => {
    postHost({ kind: "pickContextPaths", mode: "files" });
  }, []);
  const pickContextFolder = useCallback(() => {
    postHost({ kind: "pickContextPaths", mode: "folder" });
  }, []);

  useEffect(() => {
    const el = scrollRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      if (e.deltaY === 0) return;
      e.preventDefault();
      el.scrollLeft += e.deltaY;
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, []);

  const onResizePointerDown = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const startY = e.clientY;
    const startPx = thumbPx;
    let lastPx = startPx;
    const move = (ev: PointerEvent) => {
      /* Splitter above strip: drag up → larger thumbs (natural “pull strip upward”). */
      lastPx = Math.min(MAX_THUMB_PX, Math.max(MIN_THUMB_PX, startPx + (startY - ev.clientY)));
      setThumbPx(lastPx);
    };
    const end = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
      try {
        localStorage.setItem(FILM_STRIP_THUMB_LS, String(lastPx));
      } catch {
        /* private mode / quota */
      }
      document.body.style.removeProperty("cursor");
      document.body.style.removeProperty("user-select");
    };
    document.body.style.cursor = "ns-resize";
    document.body.style.userSelect = "none";
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
  }, [thumbPx]);

  useEffect(() => {
    const onDragEnd = () => setStripDragOver(false);
    window.addEventListener("dragend", onDragEnd);
    return () => window.removeEventListener("dragend", onDragEnd);
  }, []);

  /** Publish strip bounds in viewport (CSS px) so the Win32 `CF_HDROP` drop target can hit-test + drive highlight. */
  useLayoutEffect(() => {
    if (!hasWebProviderHost() || !showStrip) {
      postHost({ kind: "filmStripClientRect", rect: null });
      return undefined;
    }
    const sync = () => {
      const el = scrollRef.current;
      if (!el) {
        postHost({ kind: "filmStripClientRect", rect: null });
        return;
      }
      const r = el.getBoundingClientRect();
      postHost({
        kind: "filmStripClientRect",
        rect: { left: r.left, top: r.top, right: r.right, bottom: r.bottom },
      });
    };
    sync();
    const el = scrollRef.current;
    const ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(sync) : null;
    if (el && ro) ro.observe(el);
    window.addEventListener("resize", sync);
    window.addEventListener("scroll", sync, true);
    return () => {
      ro?.disconnect();
      window.removeEventListener("resize", sync);
      window.removeEventListener("scroll", sync, true);
      postHost({ kind: "filmStripClientRect", rect: null });
    };
  }, [showStrip, thumbPx, selection.length, explorerSelection.length, contextExtraPaths.length]);

  useEffect(() => {
    if (!hasWebProviderHost()) return undefined;
    const w = window as Window & {
      chrome?: { webview?: { addEventListener?: typeof window.addEventListener; removeEventListener?: typeof window.removeEventListener } };
    };
    const webview = w.chrome?.webview;
    if (!webview?.addEventListener) return undefined;
    const onMsg = (ev: MessageEvent) => {
      let raw: unknown = ev?.data;
      if (raw == null) return;
      if (typeof raw !== "string") {
        try {
          raw = JSON.stringify(raw);
        } catch {
          return;
        }
      }
      try {
        const o = JSON.parse(String(raw)) as Record<string, unknown>;
        if (o?.kind === "hostFilmStripDragOver") setStripDragOver(!!o.over);
      } catch {
        /* ignore */
      }
    };
    webview.addEventListener("message", onMsg as EventListener);
    return () => webview.removeEventListener?.("message", onMsg as EventListener);
  }, []);

  /**
   * WebView2 + React: `drop` on a nested div often never fires unless `dragover` calls
   * `preventDefault` from a **window capture** listener (same pattern as `installContextFileDrop`).
   * We scope handling to the film-strip bounds so drops elsewhere keep default behavior.
   */
  useLayoutEffect(() => {
    if (!hasWebProviderHost()) return undefined;
    let lastInside = false;
    const rectHit = (e: DragEvent) => {
      const el = scrollRef.current;
      if (!el) return false;
      const r = el.getBoundingClientRect();
      return (
        e.clientX >= r.left &&
        e.clientX <= r.right &&
        e.clientY >= r.top &&
        e.clientY <= r.bottom
      );
    };
    const onWinDragOver = (e: DragEvent) => {
      const inside = rectHit(e);
      if (inside !== lastInside) {
        lastInside = inside;
        setStripDragOver(inside);
      }
      if (!inside) return;
      if (!isFileLikeExternalDrag(e)) return;
      e.preventDefault();
      if (e.dataTransfer) e.dataTransfer.dropEffect = "copy";
    };
    const onWinDrop = (e: DragEvent) => {
      lastInside = false;
      setStripDragOver(false);
      if (!rectHit(e)) return;
      const paths = collectPathsFromDataTransfer(e.dataTransfer);
      if (paths.length === 0 && !isFileLikeExternalDrag(e)) return;
      e.preventDefault();
      e.stopPropagation();
      if (paths.length) postHost({ kind: "addContextPaths", paths });
    };
    window.addEventListener("dragover", onWinDragOver, true);
    window.addEventListener("drop", onWinDrop, true);
    return () => {
      window.removeEventListener("dragover", onWinDragOver, true);
      window.removeEventListener("drop", onWinDrop, true);
    };
  }, []);

  if (!showStrip) return null;

  return (
    <div className="flex flex-col">
      {/* Splitter above the strip (between transcript and thumbs): drag up to enlarge. */}
      <div
        role="separator"
        aria-orientation="horizontal"
        aria-valuemin={MIN_THUMB_PX}
        aria-valuemax={MAX_THUMB_PX}
        aria-valuenow={thumbPx}
        aria-label={t("selectionStripResizeAria")}
        title={t("selectionStripResizeTip")}
        className="group relative mx-1 mt-0.5 mb-1 flex h-2 cursor-ns-resize touch-none items-center justify-center rounded-sm sm:mx-2"
        onPointerDown={onResizePointerDown}
      >
        <span className="h-0.5 w-12 rounded-full bg-slate-300/90 transition-[width,background-color] group-hover:w-16 group-hover:bg-violet-400/80 dark:bg-slate-600/90 dark:group-hover:bg-violet-500/70" />
      </div>
      <div className="px-2 pb-2 pt-0.5 sm:px-3 sm:pb-2.5 sm:pt-1">
        <div
          ref={scrollRef}
          className={`pm-scroll flex max-w-full gap-2 overflow-x-auto px-1 py-1 sm:gap-2.5 ${
            stripDragOver
              ? "rounded-md bg-violet-50/80 ring-1 ring-inset ring-violet-400/45 dark:bg-violet-950/25 dark:ring-violet-500/40"
              : ""
          }`}
          role="list"
          aria-label={t("selectionStripAria")}
        >
          {explorerSelection.map((p) => (
            <div key={`e:${p}`} className="flex-shrink-0" role="listitem">
              <SelectionThumb
                path={p}
                thumbPx={thumbPx}
                variant="explorer"
                activateTitle={p ? `${baseActivateTitle}\n${p}`.trim() : baseActivateTitle}
                onActivate={(e) => onThumbActivate(p, e)}
                onRemove={onRemove}
              />
            </div>
          ))}
          {hasWebProviderHost() || forceShow ? (
            <div
              className="flex-shrink-0"
              role="listitem"
              aria-label={t("selectionStripDropAria")}
              style={{ width: thumbPx, height: thumbPx, minWidth: thumbPx, minHeight: thumbPx }}
            >
              <div
                role="button"
                tabIndex={0}
                className={`box-border flex h-full w-full min-h-0 cursor-pointer items-center justify-center rounded-lg border border-dashed transition-colors ${
                  stripDragOver
                    ? "border-violet-500 bg-violet-100/90 text-violet-800 shadow-[0_1px_3px_rgba(91,33,182,0.12)] dark:border-violet-400 dark:bg-violet-900/50 dark:text-violet-100 dark:shadow-[0_1px_4px_rgba(0,0,0,0.32)]"
                    : "border-slate-300/90 bg-slate-50/80 text-slate-500 shadow-[0_1px_2px_rgba(15,23,42,0.05)] dark:border-slate-600 dark:bg-slate-800/60 dark:text-slate-400 dark:shadow-[0_1px_4px_rgba(0,0,0,0.18)]"
                }`}
                onClick={(e) => {
                  e.preventDefault();
                  pickContextFiles();
                }}
                onKeyDown={(e) => {
                  if (e.key === "Enter" || e.key === " ") {
                    e.preventDefault();
                    pickContextFiles();
                  }
                }}
              >
                <Plus
                  className="shrink-0 opacity-60"
                  strokeWidth={2}
                  aria-hidden
                  size={Math.max(20, Math.min(32, Math.round(thumbPx * 0.3)))}
                />
              </div>
            </div>
          ) : null}
          {contextExtraPaths.map((p) => (
            <div key={`x:${p}`} className="flex-shrink-0" role="listitem">
              <SelectionThumb
                path={p}
                thumbPx={thumbPx}
                variant="added"
                activateTitle={p ? `${baseActivateTitle}\n${p}`.trim() : baseActivateTitle}
                onActivate={(e) => onThumbActivate(p, e)}
                onRemove={onRemove}
              />
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
