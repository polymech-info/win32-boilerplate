/**
 * Win32 WebView2: read bytes for `features.hostedFileUrl` / `threeModelUrl` via `PostWebMessageAsString`
 * (`vw_hosted_read` → chunked `vw_hosted_read_*`). Avoids cross-origin `fetch(pm-md)` from `pm-vw`.
 */

const WEBVIEW = typeof window !== "undefined" ? (window as unknown as WebviewWindow).chrome?.webview : undefined;

type WebviewWindow = Window & {
  chrome?: {
    webview?: {
      postMessage?: (s: string) => void;
      addEventListener?: (type: "message", listener: (ev: MessageEvent) => void) => void;
      removeEventListener?: (type: "message", listener: (ev: MessageEvent) => void) => void;
    };
  };
};

function decodeB64Chunk(b64: string): Uint8Array {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i) & 0xff;
  return out;
}

function parseWebMessageData(data: unknown): Record<string, unknown> | null {
  if (typeof data === "string") {
    try {
      return JSON.parse(data) as Record<string, unknown>;
    } catch {
      return null;
    }
  }
  if (data && typeof data === "object") return data as Record<string, unknown>;
  return null;
}

export function hostedNativeReadAvailable(): boolean {
  return Boolean(WEBVIEW?.postMessage && WEBVIEW.addEventListener && WEBVIEW.removeEventListener);
}

/** Reads the current preview file bytes through the native host (no HTTP to `pm-md`). */
export async function readHostedBytesViaNativeHost(url: string, signal: AbortSignal): Promise<ArrayBuffer> {
  if (!hostedNativeReadAvailable() || !WEBVIEW?.postMessage || !WEBVIEW.addEventListener || !WEBVIEW.removeEventListener) {
    throw new Error("readHostedBytesViaNativeHost: WebView2 bridge unavailable");
  }
  const id = `hr_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 10)}`;
  const post = WEBVIEW.postMessage;

  return await new Promise<ArrayBuffer>((resolve, reject) => {
    let total = 0;
    let out: Uint8Array | null = null;
    let buf: ArrayBuffer | null = null;
    let timer: ReturnType<typeof setTimeout> | null = null;

    // Initial handshake timeout: host must reply within 5 s.
    // Sliding: reset to 10 s on each chunk so large files don't time out mid-stream.
    const INIT_TIMEOUT_MS = 5_000;
    const CHUNK_TIMEOUT_MS = 10_000;

    const fail = (msg: string) => {
      cleanup();
      reject(new Error(msg));
    };

    const cleanup = () => {
      WEBVIEW.removeEventListener?.("message", onMsg);
      signal.removeEventListener("abort", onAbort);
      if (timer !== null) {
        window.clearTimeout(timer);
        timer = null;
      }
    };

    const resetTimer = (ms: number) => {
      if (timer !== null) window.clearTimeout(timer);
      timer = window.setTimeout(() => {
        if (signal.aborted) return;
        fail("hosted_read_timeout");
      }, ms);
    };

    const onAbort = () => {
      cleanup();
      reject(new DOMException("Aborted", "AbortError"));
    };

    const onMsg = (event: MessageEvent) => {
      if (signal.aborted) return;
      const j = parseWebMessageData(event.data);
      if (!j || typeof j.t !== "string" || j.id !== id) return;
      const t = j.t as string;
      if (t === "vw_hosted_read_err") {
        fail(typeof j.err === "string" ? j.err : "hosted_read_error");
        return;
      }
      if (t === "vw_hosted_read_meta") {
        const n = typeof j.size === "number" && Number.isFinite(j.size) ? Math.floor(j.size) : NaN;
        if (!Number.isFinite(n) || n < 0) {
          fail("invalid_size");
          return;
        }
        total = n;
        buf = new ArrayBuffer(total);
        out = new Uint8Array(buf);
        // Handshake received — switch to per-chunk sliding timeout.
        resetTimer(CHUNK_TIMEOUT_MS);
        return;
      }
      if (t === "vw_hosted_read_chunk") {
        if (!out || buf === null) {
          fail("chunk_before_meta");
          return;
        }
        const off = typeof j.off === "number" && Number.isFinite(j.off) ? Math.floor(j.off) : NaN;
        const b64 = typeof j.b64 === "string" ? j.b64 : "";
        if (!Number.isFinite(off) || off < 0 || off > total) {
          fail("bad_chunk_off");
          return;
        }
        const chunk = decodeB64Chunk(b64);
        if (off + chunk.byteLength > total) {
          fail("chunk_overflow");
          return;
        }
        out.set(chunk, off);
        // Slide the deadline forward — host is still actively sending.
        resetTimer(CHUNK_TIMEOUT_MS);
        return;
      }
      if (t === "vw_hosted_read_done") {
        if (!buf || total === 0) {
          fail("done_before_data");
          return;
        }
        cleanup();
        resolve(buf);
      }
    };

    signal.addEventListener("abort", onAbort);
    WEBVIEW.addEventListener("message", onMsg);

    resetTimer(INIT_TIMEOUT_MS);

    try {
      post(JSON.stringify({ t: "vw_hosted_read", id, url }));
    } catch (e) {
      cleanup();
      reject(e instanceof Error ? e : new Error(String(e)));
    }
  });
}
