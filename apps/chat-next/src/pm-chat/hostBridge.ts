/** WebView2 postMessage + provider RPC (ported from ref/main.js). */

import {
  deleteSessionLocal,
  listSessionsMetaLocal,
  loadSessionFullLocal,
  saveSessionFullLocal,
  type StoredChatSession,
} from "./chatSessionStorage";
import {
  attachHostMessageListener,
  createProviderRpcClient,
  hasWebviewHost,
  installPolyMechHostApi,
  normalizePixlwizCreditPayload,
  postToHost,
  type PixlwizCreditPayload,
  type ProviderRpcRequest,
  type ProviderRpcResult,
} from "@pm/shared/web/hostBridge";
import { applyHostColorScheme } from "@pm/shared/theme/colorScheme";

const LOG_RPC = "[pm-chat:rpc]";
export const pmHost = installPolyMechHostApi();

function logRpc(...args: unknown[]): void {
  if (typeof console !== "undefined" && console.trace) console.trace(LOG_RPC, ...args);
}

export type { ProviderRpcResult } from "@pm/shared/web/hostBridge";

export function hasWebProviderHost(): boolean {
  return hasWebviewHost();
}

export function postHost(msg: Record<string, unknown>): void {
  const kind = String(msg?.kind ?? "");
  if (kind === "providerRpc") {
    const rpcId = (msg as { rpcId?: unknown }).rpcId != null ? String((msg as { rpcId?: unknown }).rpcId) : "";
    const payloadId = msg?.id != null ? String(msg.id) : "";
    const method = String((msg as { method?: string }).method ?? "");
    logRpc("postHost →", { kind, rpcId, id: payloadId, method, hasSession: !!(msg as { session?: unknown }).session });
  } else if (kind !== "chatWebState") {
    logRpc("postHost →", { kind });
  }
  if (hasWebviewHost()) {
    postToHost(msg);
  } else {
    logRpc("postHost (no webview, console only)", msg);
  }
}

// ── `kind: "ready"` → Win32 `splash_on_chat_composer_ready` (10s watchdog) ──
// WebView2 can expose `chrome.webview` slightly after our first React effect; a
// one-shot `postHost({ kind: "ready" })` may hit the console fallback and never
// reach the host, which then hits the composer load timeout and exits.

let composerReadyPosted = false;
let composerReadyInterval: ReturnType<typeof setInterval> | null = null;

function tryPostComposerReady(): boolean {
  const w = window as Window & { chrome?: { webview?: { postMessage?: (s: string) => void } } };
  if (!w.chrome?.webview?.postMessage) return false;
  w.chrome.webview.postMessage(JSON.stringify({ kind: "ready" }));
  composerReadyPosted = true;
  if (composerReadyInterval !== null) {
    clearInterval(composerReadyInterval);
    composerReadyInterval = null;
  }
  return true;
}

/** Notify native host that the chat shell is live (`ChatWebPanel` `kind == "ready"`). Retries until postMessage works or timeout. */
export function postComposerReadyToHost(): void {
  if (composerReadyPosted) return;
  if (tryPostComposerReady()) return;

  if (composerReadyInterval !== null) return;
  const deadline = Date.now() + 12_000;
  composerReadyInterval = setInterval(() => {
    if (tryPostComposerReady() || Date.now() > deadline) {
      if (composerReadyInterval !== null) {
        clearInterval(composerReadyInterval);
        composerReadyInterval = null;
      }
      if (!composerReadyPosted) {
        // eslint-disable-next-line no-console
        console.warn("[pm-chat → host] kind=ready not delivered: chrome.webview.postMessage never became available");
      }
    }
  }, 50);
}

/** Stop retrying on unmount (e.g. React StrictMode). Does not clear a successful post — avoids duplicate `ready`. */
export function cancelComposerReadyRetry(): void {
  if (composerReadyInterval !== null) {
    clearInterval(composerReadyInterval);
    composerReadyInterval = null;
  }
}

export function devProviderRpcMock(req: Record<string, unknown>): Promise<ProviderRpcResult> {
  const m = String(req?.method ?? "");
  if (m === "listChatSessions" || m === "loadChatSession" || m === "saveChatSession" || m === "deleteChatSession") {
    const extra =
      m === "saveChatSession" && req.session && typeof req.session === "object"
        ? { entryCount: Array.isArray((req.session as { entries?: unknown }).entries) ? (req.session as { entries: unknown[] }).entries.length : 0 }
        : req;
    logRpc("devProviderRpcMock", m, extra);
  }
  if (m === "getChatFields") {
    return Promise.resolve({
      ok: true,
      data: {
        router: "openrouter",
        model: "openai/gpt-4o-mini",
        image_provider: "google",
        image_model: "gemini-3-pro-image-preview",
      },
    });
  }
  if (m === "imageProviders") {
    return Promise.resolve({
      ok: true,
      data: [
        { id: "google", label: "Google / Gemini" },
        { id: "openai", label: "OpenAI" },
        { id: "replicate", label: "Replicate" },
      ],
    });
  }
  if (m === "imageModels" && req.provider) {
    const p = String(req.provider);
    const rows =
      {
        google: [{ id: "gemini-3-pro-image-preview", label: "gemini-3-pro-image-preview" }],
        openai: [{ id: "gpt-image-1", label: "gpt-image-1" }],
        replicate: [{ id: "black-forest-labs/flux-schnell", label: "black-forest-labs/flux-schnell" }],
      }[p] ?? [];
    return Promise.resolve({ ok: true, data: rows });
  }
  if (m === "replicate" && req.op === "collections") {
    return Promise.resolve({
      ok: true,
      data: { collections: [{ slug: "official", name: "official", description: "" }] },
    });
  }
  if (m === "replicate" && req.op === "models") {
    return Promise.resolve({
      ok: true,
      data: {
        models: [
          {
            slug: "black-forest-labs/flux-schnell",
            description: "",
            url: "",
            visibility: "",
            is_official: true,
          },
        ],
      },
    });
  }
  if (m === "replicate" && req.op === "resolve_collection") {
    return Promise.resolve({ ok: true, data: { collection: "official" } });
  }
  if (m === "llmModels") {
    const router = String(req?.router ?? "");
    const rows =
      router === "openrouter"
        ? [
            { id: "openai/gpt-4o-mini",             label: "GPT-4o Mini (OpenAI)" },
            { id: "openai/gpt-4o",                  label: "GPT-4o (OpenAI)" },
            { id: "google/gemini-2.0-flash-001",    label: "Gemini 2.0 Flash (Google)" },
            { id: "anthropic/claude-3.5-sonnet",    label: "Claude 3.5 Sonnet (Anthropic)" },
            { id: "meta-llama/llama-3.1-8b-instruct", label: "Llama 3.1 8B (Meta)" },
          ]
        : router === "openai"
          ? [
              { id: "gpt-4o-mini",   label: "gpt-4o-mini" },
              { id: "gpt-4o",        label: "gpt-4o" },
              { id: "o1-mini",       label: "o1-mini" },
              { id: "o3-mini",       label: "o3-mini" },
            ]
          : [];
    return Promise.resolve({ ok: true, data: rows });
  }
  if (m === "saveChatFields") return Promise.resolve({ ok: true });
  if (m === "listChatSessions") {
    return Promise.resolve({ ok: true, data: { sessions: listSessionsMetaLocal() } });
  }
  if (m === "loadChatSession") {
    const id = String(req?.id ?? "");
    if (!id) return Promise.resolve({ ok: false, error: "missing id" });
    const session = loadSessionFullLocal(id);
    return Promise.resolve({
      ok: true,
      data: session ? { session } : null,
    });
  }
  if (m === "saveChatSession") {
    const raw = req?.session;
    if (!raw || typeof raw !== "object") return Promise.resolve({ ok: false, error: "missing session" });
    try {
      saveSessionFullLocal(raw as StoredChatSession);
      return Promise.resolve({ ok: true });
    } catch {
      return Promise.resolve({ ok: false, error: "saveSessionFullLocal failed" });
    }
  }
  if (m === "deleteChatSession") {
    const id = String(req?.id ?? "");
    if (!id) return Promise.resolve({ ok: false, error: "missing id" });
    deleteSessionLocal(id);
    return Promise.resolve({ ok: true });
  }
  if (m === "saveChatPastedImage") {
    return Promise.resolve({ ok: false, error: "dev: saveChatPastedImage needs WebView2 host" });
  }
  return Promise.resolve({ ok: false, error: "dev: unknown" });
}

const providerRpcClient = createProviderRpcClient({
  idPrefix: "r",
  mock: (req) => devProviderRpcMock(req),
  post: postHost,
  hasHost: hasWebProviderHost,
  onLog: (event, detail) => logRpc(event, detail),
});

export function callProviderRpc<T = unknown>(req: ProviderRpcRequest): Promise<ProviderRpcResult<T>> {
  return providerRpcClient.call<T>(req);
}

/** Attach WebView2 message listener for provider RPC replies + hostChatWeb. Returns cleanup. */
export function attachWebviewMessageBridge(handlers: {
  onHostChatWeb: (doc: unknown) => void;
  onHostPixlwizAuth?: (payload: Record<string, unknown>) => void;
  onHostPixlwizCredit?: (payload: PixlwizCreditPayload) => void;
}): () => void {
  return attachHostMessageListener({
    onProviderRpcReply: (o) => {
      providerRpcClient.handleHostProviderRpc(o);
    },
    onTheme: (theme) => {
      applyHostColorScheme(theme);
    },
    onBus: (o) => {
      window.dispatchEvent(new CustomEvent("cweb_bus", { detail: o }));
    },
    onMessage: (o) => {
      if (o?.kind === "hostProviderRpc" && o.id) {
        return;
      }
      if (o?.kind === "hostPixlwizAuth") {
        handlers.onHostPixlwizAuth?.(o);
        return;
      }
      if (o?.kind === "hostPixlwizCredit") {
        if (o.ok === false) {
          console.warn("[pm-chat] pixlwiz credit fetch error:", o.error ?? "unknown");
          return;
        }
        handlers.onHostPixlwizCredit?.(normalizePixlwizCreditPayload(o));
        return;
      }
      if (o?.kind === "hostChatWeb" && o.doc) {
        handlers.onHostChatWeb(o.doc);
      }
    },
  });
}
