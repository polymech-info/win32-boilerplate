import {
  attachHostMessageListener,
  createProviderRpcClient,
  hasWebviewHost,
  installPolyMechHostApi,
  postToHost,
  type ProviderRpcRequest,
  type ProviderRpcResult,
} from "../web/hostBridge";

export type { ProviderRpcResult } from "../web/hostBridge";
export const pmHost = installPolyMechHostApi();

const LOG_RPC = "[pm-shared:chat-rpc]";

function logRpc(...args: unknown[]): void {
  if (typeof console !== "undefined" && console.log) console.log(LOG_RPC, ...args);
}

const providerRpcClient = createProviderRpcClient({
  idPrefix: "r",
  post: postHost,
  hasHost: hasWebProviderHost,
  onLog: (event, detail) => logRpc(event, detail),
});

let providerRpcReplyDetach: (() => void) | null = null;

export function hasWebProviderHost(): boolean {
  return hasWebviewHost();
}

export function postHost(msg: Record<string, unknown>): void {
  const kind = String(msg?.kind ?? "");
  if (kind === "providerRpc") {
    const rpcId = (msg as { rpcId?: unknown }).rpcId != null ? String((msg as { rpcId?: unknown }).rpcId) : "";
    const payloadId = msg?.id != null ? String(msg.id) : "";
    const method = String((msg as { method?: string }).method ?? "");
    logRpc("postHost ->", { kind, rpcId, id: payloadId, method, hasSession: !!(msg as { session?: unknown }).session });
  } else if (kind !== "chatWebState") {
    logRpc("postHost ->", { kind });
  }
  if (hasWebviewHost()) postToHost(msg);
  else logRpc("postHost (no webview, console only)", msg);
}

function ensureProviderRpcReplyBridge(): void {
  if (providerRpcReplyDetach) return;
  providerRpcReplyDetach = attachHostMessageListener({
    onProviderRpcReply: (message) => {
      providerRpcClient.handleHostProviderRpc(message);
    },
  });
}

export function callProviderRpc<T = unknown>(req: ProviderRpcRequest): Promise<ProviderRpcResult<T>> {
  ensureProviderRpcReplyBridge();
  return providerRpcClient.call<T>(req);
}
