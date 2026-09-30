import { useEffect, useRef, useState } from "react";
import { Moon, Sun, Loader2 } from "lucide-react";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedArrayBuffer } from "@/bridge/hostedFileFetch";

/** Max video size loaded via vw_hosted_read → blob URL (macOS native, no virtual host). */
const VIDEO_BLOB_MAX_BYTES = 100 * 1024 * 1024; // 100 MB

/**
 * On macOS WKWebView the host passes `file://` URLs which the browser cannot load
 * directly (outside allowingReadAccessTo). Detect and use the vw_hosted_read bridge.
 * Win32 passes `https://pm-md-assets/…` which plays natively.
 */
function isNativeFilePath(url: string): boolean {
  return url.startsWith("file://");
}

/**
 * Centre preview for hosted MP4/WebM/etc. on the markdown-assets vhost.
 *
 * We use native `<video controls>` instead of `@vidstack/react` + `MediaCommunitySkin`:
 * in WebView2 the community skin's mobile/buffering layout often mis-sizes (buffering SVG
 * fills half the pane, controls hidden behind it). Native controls are reliable and
 * keep the embed bundle smaller.
 */
export function VideoViewerPane({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const err = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const url = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  const fileName = typeof f.hostedFileName === "string" ? f.hostedFileName : "Video";
  const fileSizeBytes = typeof f.hostedFileSizeBytes === "number" ? f.hostedFileSizeBytes : 0;
  const videoRef = useRef<HTMLVideoElement | null>(null);

  const hostTheme = f.theme;
  const [isDarkTheme, setIsDarkTheme] = useState(hostTheme !== "light");

  // Resolved src passed to <video>: either the original URL (Win32 vhost / HTTP)
  // or a blob: URL created from vw_hosted_read bytes (macOS file:// path).
  const [resolvedSrc, setResolvedSrc] = useState<string>("");
  const [blobLoading, setBlobLoading] = useState(false);
  const [blobErr, setBlobErr] = useState<string>("");

  // Sync with global app theme changes.
  useEffect(() => {
    if (hostTheme === "dark" || hostTheme === "light") {
      setIsDarkTheme(hostTheme === "dark");
    }
  }, [hostTheme]);

  // Resolve the video source — use vw_hosted_read bridge for macOS file:// URLs.
  useEffect(() => {
    setResolvedSrc("");
    setBlobErr("");
    setBlobLoading(false);
    if (!url) return;

    if (!isNativeFilePath(url)) {
      // Win32 / HTTP: browser can load directly.
      setResolvedSrc(url);
      return;
    }

    // macOS: file:// — WKWebView blocks direct access outside allowingReadAccessTo.
    if (fileSizeBytes > VIDEO_BLOB_MAX_BYTES) {
      setBlobErr(
        `Video too large for in-app preview (${(fileSizeBytes / (1024 * 1024)).toFixed(0)} MB — limit ${VIDEO_BLOB_MAX_BYTES / (1024 * 1024)} MB). Open with QuickTime Player.`,
      );
      return;
    }

    const ac = new AbortController();
    setBlobLoading(true);

    fetchHostedArrayBuffer(url, ac.signal)
      .then((buf) => {
        if (ac.signal.aborted) return;
        const ext = url.split(".").pop()?.toLowerCase() ?? "";
        const mime =
          ext === "mp4" || ext === "m4v" ? "video/mp4" :
          ext === "mov" ? "video/quicktime" :
          ext === "webm" ? "video/webm" :
          ext === "mkv" ? "video/x-matroska" :
          ext === "ogg" ? "video/ogg" :
          ext === "avi" ? "video/x-msvideo" :
          "video/mp4";
        const blob = new Blob([buf], { type: mime });
        setResolvedSrc(URL.createObjectURL(blob));
        setBlobLoading(false);
      })
      .catch((e) => {
        if (ac.signal.aborted) return;
        setBlobErr(e instanceof Error ? e.message : "Failed to load video");
        setBlobLoading(false);
      });

    return () => ac.abort();
  }, [url, fileSizeBytes]);

  // Revoke blob: URL and pause element when url changes.
  useEffect(() => {
    return () => {
      if (resolvedSrc.startsWith("blob:")) {
        URL.revokeObjectURL(resolvedSrc);
      }
    };
  }, [resolvedSrc]);

  useEffect(() => {
    // Capture the element at effect-setup time. With key={resolvedSrc} React destroys the
    // old <video> and mounts the new one before passive effects run, so reading
    // videoRef.current in the cleanup would target the NEW element and strip its src.
    const el = videoRef.current;
    return () => {
      if (!el) return;
      try {
        el.pause();
        el.removeAttribute("src");
        el.load();
      } catch {
        /* ignore */
      }
    };
  }, [resolvedSrc]);

  useEffect(() => {
    if (!resolvedSrc || blobLoading || err || blobErr) return;
    const el = videoRef.current;
    if (!el) return;
    const play = () => {
      const pending = el.play();
      if (pending) pending.catch(() => {});
    };
    if (el.readyState >= HTMLMediaElement.HAVE_METADATA) play();
    else el.addEventListener("loadedmetadata", play, { once: true });
    return () => el.removeEventListener("loadedmetadata", play);
  }, [blobErr, blobLoading, err, resolvedSrc]);

  const displayErr = err || blobErr;
  if (displayErr) {
    const fsz = fileSizeBytes;
    const maxB = typeof f.hostedFileMaxBytes === "number" ? f.hostedFileMaxBytes : 0;
    return (
      <div className={`flex h-full min-h-0 flex-1 flex-col items-center justify-center p-6 text-center ${isDarkTheme ? "text-slate-400" : "text-slate-600"}`}>
        <p className="mb-2 text-sm">{displayErr}</p>
        {fsz > 0 && maxB > 0 ? (
          <p className="text-xs text-slate-500">
            ({(fsz / (1024 * 1024)).toFixed(2)} MB / max {(maxB / (1024 * 1024)).toFixed(0)} MB)
          </p>
        ) : null}
      </div>
    );
  }

  if (!url) {
    return (
      <div className={`flex h-full min-h-0 flex-1 items-center justify-center p-6 text-sm ${isDarkTheme ? "text-slate-400" : "text-slate-500"}`}>
        No video loaded.
      </div>
    );
  }

  if (blobLoading) {
    return (
      <div className={`flex h-full min-h-0 flex-1 flex-col items-center justify-center gap-2 ${isDarkTheme ? "text-slate-400" : "text-slate-500"}`}>
        <Loader2 className="h-5 w-5 animate-spin" />
        <span className="text-sm">Loading video…</span>
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden bg-transparent">
      <div
        className="flex shrink-0 items-center justify-between gap-2 border-b py-1 px-2"
        style={{ borderColor: "var(--border)" }}
      >
        <span className={`min-w-0 truncate text-sm font-medium ${isDarkTheme ? "text-zinc-100" : "text-slate-800"}`}>
          {fileName}
        </span>
        <button
          type="button"
          className={`shrink-0 rounded border px-2 py-1 ${
            isDarkTheme
              ? "text-slate-300 hover:bg-slate-800"
              : "text-slate-600 hover:bg-slate-100"
          }`}
          style={{ borderColor: "var(--border)" }}
          title={isDarkTheme ? "Switch to light theme" : "Switch to dark theme"}
          onClick={() => setIsDarkTheme((v) => !v)}
        >
          {isDarkTheme ? <Sun className="h-3.5 w-3.5" /> : <Moon className="h-3.5 w-3.5" />}
        </button>
      </div>
      <div className={`flex min-h-0 flex-1 flex-col ${isDarkTheme ? "bg-black" : "bg-zinc-200"}`}>
        <video
          ref={videoRef}
          key={resolvedSrc}
          className="min-h-0 w-full flex-1 bg-black object-contain"
          src={resolvedSrc}
          autoPlay
          controls
          playsInline
          preload="metadata"
        />
      </div>
    </div>
  );
}
