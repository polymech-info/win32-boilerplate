import { ENGINE } from "@/bridge/engine";

/**
 * Win32 host calls `window.pmViewer.setStatus({ selection, folder, viewerKind, … })`.
 * Add fields only in sync with `viewer_web_contract.hpp` + `CViewerWebPanel::FlushContextToWeb`.
 */
export type ViewerWebStatus = {
  selection: string[];
  folder: string;
  viewerKind?: string;
  /** Host-driven flags — keep in sync with `viewer_web_contract.hpp` + `FlushContextToWeb`. */
  features?: {
    /** Raw UTF-8 markdown when the host renders client-side (WebView2 + react-markdown). */
    markdownText?: string;
    /** Base URL for resolving relative links/images (e.g. markdown assets vhost + `/`). */
    markdownBaseUrl?: string;
    /** Legacy: full HTTPS URL of server-generated markdown HTML (iframe). */
    markdownEmbedUrl?: string;
    /** HTTPS URL of the model file (markdown assets vhost → file parent folder). */
    threeModelUrl?: string;
    threeFileName?: string;
    threeFileSizeBytes?: number;
    threeMaxBytes?: number;
    /** Host-side validation (e.g. file too large for in-web preview). */
    threeError?: string;
    /** Raw OpenSCAD source text for parameter parsing in the OpenSCAD viewer. */
    openscadSourceText?: string;
    /** PDF / spreadsheet / video / image / plain-text: HTTPS URL on markdown-assets vhost (file parent folder). */
    hostedFileUrl?: string;
    /** Plain-text only: UTF-8 body from host; when set, skip `fetch(hostedFileUrl)` (same-origin issues). */
    hostedFileText?: string;
    hostedFileName?: string;
    hostedFileSizeBytes?: number;
    hostedFileMaxBytes?: number;
    hostedFileError?: string;
    /** Host theme: 'dark' | 'light' — drives default viewer theme before user override. */
    theme?: "dark" | "light";
  } & Record<string, unknown>;
};

export type ViewerHostApi = {
  setStatus: (s: ViewerWebStatus) => void;
  setLocale?: (tag: string) => void;
  setFontExtraPt?: (n: number) => void;
};

declare global {
  interface Window {
    pmViewer?: ViewerHostApi;
  }
}

export function installPmViewerHost(api: ViewerHostApi): void {
  const wrappedApi: ViewerHostApi = {
    ...api,
    setStatus: (s: ViewerWebStatus) => {
      postViewerWebHostLog(
        "warn",
        `[bridge] setStatus kind=${s.viewerKind ?? "(none)"} folder="${s.folder ?? ""}" sel=${s.selection?.length ?? 0} ` +
          `hostedUrl=${s.features?.hostedFileUrl ?? "(none)"} hostedErr=${s.features?.hostedFileError ?? "(none)"}`,
      );
      api.setStatus(s);
    },
  };
  window.pmViewer = wrappedApi;
  (window as unknown as { __PM_MARKDOWN_ENGINE?: string }).__PM_MARKDOWN_ENGINE = ENGINE;
  postViewerWebHostLog("warn", `[bridge] installPmViewerHost — engine=${ENGINE}`);
}

type VwConsoleLevel = "warn" | "error";

/**
 * Same JSON shape as the injected console bridge in `ViewerWebPanel.cpp` (`t: "vw_console"`).
 * Native host forwards to the C++ `vw_console` bridge → default logger (`vweb_log_warn` / `vweb_log_error`).
 */
export function postViewerWebHostLog(level: VwConsoleLevel, msg: string): void {
  const w = window as Window & { chrome?: { webview?: { postMessage?: (s: string) => void } } };
  const post = w.chrome?.webview?.postMessage;
  if (!post) return;
  const clipped = msg.length > 1800 ? msg.slice(0, 1800) : msg;
  try {
    post(JSON.stringify({ t: "vw_console", level, msg: clipped }));
  } catch {
    /* host or serialization unavailable */
  }
}

type WebviewApi = {
  postMessage?: (s: string) => void;
  addEventListener?: (type: "message", listener: (ev: MessageEvent) => void) => void;
  removeEventListener?: (type: "message", listener: (ev: MessageEvent) => void) => void;
};

function getWebview(): WebviewApi | undefined {
  return (window as Window & { chrome?: { webview?: WebviewApi } }).chrome?.webview;
}

/** Post any JSON message to the Win32 host (`PostWebMessageAsString`). No-op outside WebView2. */
export function postToHost(msg: Record<string, unknown>): void {
  try {
    getWebview()?.postMessage?.(JSON.stringify(msg));
  } catch {
    /* unavailable in browser dev mode */
  }
}

/** Subscribe to messages posted from the Win32 host. Returns a cleanup function. */
export function onHostMessage(listener: (data: Record<string, unknown>) => void): () => void {
  const webview = getWebview();
  if (!webview?.addEventListener) return () => {};
  const handler = (ev: MessageEvent) => {
    try {
      const data = typeof ev.data === "string" ? JSON.parse(ev.data) : ev.data;
      if (data && typeof data === "object") listener(data as Record<string, unknown>);
    } catch {}
  };
  webview.addEventListener("message", handler);
  return () => webview.removeEventListener?.("message", handler);
}
