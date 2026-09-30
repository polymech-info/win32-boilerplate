/**
 * Hosted preview files: Win32 WebView2 uses `vw_hosted_read` (native read) when available;
 * otherwise `fetch` to the markdown-assets vhost (may fail cross-origin from `pm-vw`).
 */

import { postViewerWebHostLog } from "@/bridge/hostBridge";
import { hostedNativeReadAvailable, readHostedBytesViaNativeHost } from "@/bridge/hostedNativeRead";

const RETRY_MS = 120;
const MAX_ATTEMPTS = 2;

function formatFetchErr(e: unknown): string {
  if (e instanceof Error) return e.message;
  return String(e);
}

function retriableFetchFailure(e: unknown): boolean {
  if (e instanceof DOMException && e.name === "AbortError") return false;
  if (e instanceof TypeError) return true;
  if (e instanceof Error) {
    const m = e.message;
    return /Failed to fetch|NetworkError|Load failed|ERR_FILE_NOT_FOUND|networkerror/i.test(m);
  }
  return false;
}

function retriableHttpStatus(status: number): boolean {
  return status === 404 || status === 0 || status >= 500;
}

export async function fetchHostedText(url: string, signal: AbortSignal): Promise<string> {
  if (hostedNativeReadAvailable()) {
    try {
      const buf = await readHostedBytesViaNativeHost(url, signal);
      return new TextDecoder("utf-8", { fatal: false }).decode(buf);
    } catch (e) {
      if (signal.aborted) throw e;
      postViewerWebHostLog(
        "warn",
        `fetchHostedText: native read failed (${e instanceof Error ? e.message : String(e)}), falling back to fetch`,
      );
    }
  }
  let lastErr: unknown;
  for (let attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    if (signal.aborted) throw new DOMException("Aborted", "AbortError");
    try {
      const res = await fetch(url, { signal });
      if (!res.ok) {
        if (attempt + 1 < MAX_ATTEMPTS && retriableHttpStatus(res.status)) {
          postViewerWebHostLog(
            "warn",
            `fetchHostedText: HTTP ${res.status} url=${url} attempt=${attempt + 1}/${MAX_ATTEMPTS} retry in ${RETRY_MS}ms`,
          );
          await new Promise<void>((resolve) => setTimeout(resolve, RETRY_MS));
          if (signal.aborted) throw new DOMException("Aborted", "AbortError");
          continue;
        }
        const err = new Error(`HTTP ${res.status}`);
        postViewerWebHostLog(
          "error",
          `fetchHostedText: ${err.message} url=${url} attempts=${attempt + 1}/${MAX_ATTEMPTS}`,
        );
        throw err;
      }
      return await res.text();
    } catch (e) {
      lastErr = e;
      if (signal.aborted) throw e;
      if (attempt + 1 < MAX_ATTEMPTS && retriableFetchFailure(e)) {
        postViewerWebHostLog(
          "warn",
          `fetchHostedText: ${formatFetchErr(e)} url=${url} attempt=${attempt + 1}/${MAX_ATTEMPTS} retry in ${RETRY_MS}ms`,
        );
        await new Promise<void>((resolve) => setTimeout(resolve, RETRY_MS));
        if (signal.aborted) throw new DOMException("Aborted", "AbortError");
        continue;
      }
      postViewerWebHostLog(
        "error",
        `fetchHostedText: ${formatFetchErr(e)} url=${url} attempts=${attempt + 1}/${MAX_ATTEMPTS}`,
      );
      throw e;
    }
  }
  const out =
    lastErr instanceof Error ? lastErr : new Error("fetchHostedText: exhausted retries");
  postViewerWebHostLog("error", `fetchHostedText: ${formatFetchErr(out)} url=${url} (exhausted)`);
  throw out;
}

export async function fetchHostedArrayBuffer(url: string, signal: AbortSignal): Promise<ArrayBuffer> {
  if (hostedNativeReadAvailable()) {
    try {
      return await readHostedBytesViaNativeHost(url, signal);
    } catch (e) {
      if (signal.aborted) throw e;
      postViewerWebHostLog(
        "warn",
        `fetchHostedArrayBuffer: native read failed (${e instanceof Error ? e.message : String(e)}), falling back to fetch`,
      );
    }
  }
  let lastErr: unknown;
  for (let attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    if (signal.aborted) throw new DOMException("Aborted", "AbortError");
    try {
      const res = await fetch(url, { signal });
      if (!res.ok) {
        if (attempt + 1 < MAX_ATTEMPTS && retriableHttpStatus(res.status)) {
          postViewerWebHostLog(
            "warn",
            `fetchHostedArrayBuffer: HTTP ${res.status} url=${url} attempt=${attempt + 1}/${MAX_ATTEMPTS} retry in ${RETRY_MS}ms`,
          );
          await new Promise<void>((resolve) => setTimeout(resolve, RETRY_MS));
          if (signal.aborted) throw new DOMException("Aborted", "AbortError");
          continue;
        }
        const err = new Error(`HTTP ${res.status}`);
        postViewerWebHostLog(
          "error",
          `fetchHostedArrayBuffer: ${err.message} url=${url} attempts=${attempt + 1}/${MAX_ATTEMPTS}`,
        );
        throw err;
      }
      return await res.arrayBuffer();
    } catch (e) {
      lastErr = e;
      if (signal.aborted) throw e;
      if (attempt + 1 < MAX_ATTEMPTS && retriableFetchFailure(e)) {
        postViewerWebHostLog(
          "warn",
          `fetchHostedArrayBuffer: ${formatFetchErr(e)} url=${url} attempt=${attempt + 1}/${MAX_ATTEMPTS} retry in ${RETRY_MS}ms`,
        );
        await new Promise<void>((resolve) => setTimeout(resolve, RETRY_MS));
        if (signal.aborted) throw new DOMException("Aborted", "AbortError");
        continue;
      }
      postViewerWebHostLog(
        "error",
        `fetchHostedArrayBuffer: ${formatFetchErr(e)} url=${url} attempts=${attempt + 1}/${MAX_ATTEMPTS}`,
      );
      throw e;
    }
  }
  const out =
    lastErr instanceof Error ? lastErr : new Error("fetchHostedArrayBuffer: exhausted retries");
  postViewerWebHostLog("error", `fetchHostedArrayBuffer: ${formatFetchErr(out)} url=${url} (exhausted)`);
  throw out;
}
