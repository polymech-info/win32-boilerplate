import { callProviderRpc, hasWebProviderHost } from "./hostBridge";
import {
  deleteSessionLocal,
  generateSessionTitleFromEntries,
  listSessionsMetaLocal,
  loadSessionFullLocal,
  saveSessionFullLocal,
  type ChatSessionMeta,
  type StoredChatEntry,
  type StoredChatSession,
} from "./chatSessionStorage";

const LOG_SESS = "[pm-shared:chat-sessions]";
const CHANGED = "pm-chat-sessions-changed";

export type ChatSessionSnapshotEntry = {
  id: number;
  role: string;
  text: string;
  ts?: number;
  folderHint?: string;
  contextPaths?: string[];
  resultPaths?: string[];
  llmUsage?: Record<string, unknown>;
};

function logSess(...args: unknown[]): void {
  if (typeof console !== "undefined" && console.log) console.log(LOG_SESS, ...args);
}

export function notifyChatSessionsChanged(): void {
  if (typeof window === "undefined") return;
  window.dispatchEvent(new CustomEvent(CHANGED));
}

export function subscribeChatSessionsChanged(fn: () => void): () => void {
  if (typeof window === "undefined") return () => {};
  window.addEventListener(CHANGED, fn);
  return () => window.removeEventListener(CHANGED, fn);
}

function normalizeMetaList(data: unknown): ChatSessionMeta[] {
  if (!data || typeof data !== "object") return [];
  const sessions = (data as { sessions?: unknown }).sessions;
  if (!Array.isArray(sessions)) return [];
  const out: ChatSessionMeta[] = [];
  for (const row of sessions) {
    if (!row || typeof row !== "object") continue;
    const o = row as Record<string, unknown>;
    if (typeof o.id !== "string" || !o.id) continue;
    out.push({
      id: o.id,
      title: typeof o.title === "string" ? o.title : "Chat",
      createdAt: Number(o.createdAt) || 0,
      updatedAt: Number(o.updatedAt) || 0,
    });
  }
  return out.sort((a, b) => b.updatedAt - a.updatedAt);
}

function parseStoredSession(data: unknown): StoredChatSession | null {
  if (!data || typeof data !== "object") return null;
  const o = data as Record<string, unknown>;
  if (typeof o.id !== "string") return null;
  const entriesRaw = o.entries ?? o.messages;
  const entries: StoredChatEntry[] = [];
  if (Array.isArray(entriesRaw)) {
    for (const x of entriesRaw) {
      if (!x || typeof x !== "object") continue;
      const e = x as Record<string, unknown>;
      const id = Number(e.id);
      const text = typeof e.text === "string" ? e.text : "";
      if (!Number.isFinite(id)) continue;
      const role = typeof e.role === "string" ? e.role : "assistant";
      const row: StoredChatEntry = { id, role, text };
      const ts = Number(e.ts);
      if (Number.isFinite(ts) && ts > 0) row.ts = ts;
      const fh = e.folderHint;
      if (typeof fh === "string" && fh.trim()) row.folderHint = fh.trim();
      const cp = e.contextPaths;
      if (Array.isArray(cp) && cp.every((x): x is string => typeof x === "string")) row.contextPaths = cp;
      const rp = e.resultPaths;
      if (Array.isArray(rp) && rp.every((x): x is string => typeof x === "string")) row.resultPaths = rp;
      const lu = e.llmUsage;
      if (lu && typeof lu === "object" && !Array.isArray(lu)) row.llmUsage = lu as Record<string, unknown>;
      entries.push(row);
    }
  }
  const session: StoredChatSession = {
    id: o.id,
    title: typeof o.title === "string" ? o.title : generateSessionTitleFromEntries(entries),
    createdAt: Number(o.createdAt) || 0,
    updatedAt: Number(o.updatedAt) || 0,
    entries,
  };
  if (typeof o.agentJsonText === "string" && o.agentJsonText) session.agentJsonText = o.agentJsonText;
  return session;
}

export async function listChatSessionsMeta(): Promise<ChatSessionMeta[]> {
  if (!hasWebProviderHost()) return listSessionsMetaLocal();
  const r = await callProviderRpc({ method: "listChatSessions" });
  logSess("listChatSessionsMeta", { ok: r.ok, error: r.error });
  if (!r.ok || r.data == null) return [];
  return normalizeMetaList(r.data);
}

export async function loadChatSessionFull(id: string): Promise<StoredChatSession | null> {
  if (!hasWebProviderHost()) return loadSessionFullLocal(id);
  const r = await callProviderRpc({ method: "loadChatSession", id });
  logSess("loadChatSessionFull", id, { ok: r.ok, error: r.error });
  if (!r.ok || r.data == null) return null;
  const inner = (r.data as { session?: unknown }).session ?? r.data;
  return parseStoredSession(inner);
}

export async function persistChatSessionSnapshot(
  id: string,
  entries: ChatSessionSnapshotEntry[],
  extras?: { agentJsonText?: string | null },
): Promise<void> {
  if (entries.length === 0) return;
  const prev = await loadChatSessionFull(id);
  const now = Date.now();
  const session: StoredChatSession = {
    id,
    title: generateSessionTitleFromEntries(entries),
    createdAt: prev?.createdAt ?? now,
    updatedAt: now,
    entries: entries.map((e) => {
      const row: StoredChatEntry = { id: e.id, role: e.role, text: e.text };
      if (typeof e.ts === "number" && Number.isFinite(e.ts)) row.ts = e.ts;
      if (e.folderHint?.trim()) row.folderHint = e.folderHint.trim();
      if (e.contextPaths?.length) row.contextPaths = [...e.contextPaths];
      if (e.resultPaths?.length) row.resultPaths = [...e.resultPaths];
      if (e.llmUsage && typeof e.llmUsage === "object") row.llmUsage = { ...e.llmUsage };
      return row;
    }),
  };
  const agentJsonText = extras?.agentJsonText ?? prev?.agentJsonText;
  if (agentJsonText) session.agentJsonText = agentJsonText;
  if (!hasWebProviderHost()) {
    saveSessionFullLocal(session);
    notifyChatSessionsChanged();
    return;
  }
  const r = await callProviderRpc({ method: "saveChatSession", session });
  if (r.ok) notifyChatSessionsChanged();
  else if (typeof console !== "undefined" && console.warn) console.warn(LOG_SESS, "saveChatSession failed", r.error);
}

export async function deleteChatSession(id: string): Promise<void> {
  if (!hasWebProviderHost()) {
    deleteSessionLocal(id);
    notifyChatSessionsChanged();
    return;
  }
  const r = await callProviderRpc({ method: "deleteChatSession", id });
  if (r.ok) notifyChatSessionsChanged();
}
