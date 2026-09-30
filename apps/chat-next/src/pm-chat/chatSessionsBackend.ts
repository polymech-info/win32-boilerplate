/**
 * Chat session list / load / save / delete.
 * - No WebView2: `chatSessionStorage` (localStorage).
 * - WebView2: `providerRpc` — `listChatSessions`, `loadChatSession`, `saveChatSession`, `deleteChatSession`
 *   (implemented in ChatWebPanel.cpp / PolymechWebChatView.swift; stubs return empty / ok until real storage exists).
 */

import { callProviderRpc, hasWebProviderHost } from "./hostBridge";

const LOG_SESS = "[pm-chat:sessions]";

function logSess(...args: unknown[]): void {
  if (typeof console !== "undefined" && console.log) console.log(LOG_SESS, ...args);
}
import {
  deleteSessionLocal,
  generateSessionTitleFromEntries,
  listSessionsMetaLocal,
  loadSessionFullLocal,
  type ChatSessionMeta,
  type StoredChatEntry,
  type StoredChatSession,
  saveSessionFullLocal,
} from "./chatSessionStorage";
import type { ChatEntry } from "./pmChatStore";

const CHANGED = "pm-chat-sessions-changed";

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
      const role = e.role;
      if (!Number.isFinite(id)) continue;
      const validRoles = ["user", "assistant", "tool", "system", "error", "image", "file"] as const;
      const r = typeof role === "string" && (validRoles as readonly string[]).includes(role) ? role : "assistant";
      const tsRaw = e.ts;
      const ts =
        typeof tsRaw === "number" && Number.isFinite(tsRaw)
          ? tsRaw
          : typeof tsRaw === "string" && /^\d+$/.test(tsRaw)
            ? Number(tsRaw)
            : undefined;
      const row: StoredChatEntry = { id, role: r, text, ...(ts != null && Number.isFinite(ts) ? { ts } : {}) };
      const lu = (e as { llmUsage?: unknown }).llmUsage;
      if (lu && typeof lu === "object" && !Array.isArray(lu)) row.llmUsage = lu as Record<string, unknown>;
      const fh = (e as { folderHint?: unknown }).folderHint;
      if (typeof fh === "string" && fh.trim()) row.folderHint = fh.trim();
      const cp = e.contextPaths;
      if (Array.isArray(cp) && cp.length && cp.every((x): x is string => typeof x === "string")) {
        const paths = cp.map((x) => x.trim()).filter(Boolean);
        if (paths.length) row.contextPaths = paths;
      }
      const rp = e.resultPaths;
      if (Array.isArray(rp) && rp.length && rp.every((x): x is string => typeof x === "string")) {
        const paths = rp.map((x) => x.trim()).filter(Boolean);
        if (paths.length) row.resultPaths = paths;
      }
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
  logSess("parseStoredSession", { id: session.id, agentJsonPresent: !!session.agentJsonText, agentJsonLen: session.agentJsonText?.length ?? 0, rawField: typeof o.agentJsonText });
  return session;
}

export async function listChatSessionsMeta(): Promise<ChatSessionMeta[]> {
  if (!hasWebProviderHost()) {
    const rows = listSessionsMetaLocal();
    logSess("listChatSessionsMeta (localStorage)", rows.length);
    return rows;
  }
  logSess("listChatSessionsMeta → providerRpc");
  const r = await callProviderRpc({ method: "listChatSessions" });
  logSess("listChatSessionsMeta ←", { ok: r.ok, error: r.error, raw: r.data });
  if (!r.ok || r.data == null) return [];
  return normalizeMetaList(r.data);
}

export async function loadChatSessionFull(id: string): Promise<StoredChatSession | null> {
  if (!hasWebProviderHost()) {
    const s = loadSessionFullLocal(id);
    logSess("loadChatSessionFull (localStorage)", id, !!s);
    return s;
  }
  logSess("loadChatSessionFull → providerRpc", id);
  const r = await callProviderRpc({ method: "loadChatSession", id });
  logSess("loadChatSessionFull ←", id, { ok: r.ok, error: r.error, hasData: r.data != null });
  if (!r.ok || r.data == null) return null;
  const inner = (r.data as { session?: unknown }).session ?? r.data;
  const parsed = parseStoredSession(inner);
  if (!parsed?.entries?.length && inner != null) {
    logSess("loadChatSessionFull parse miss", id, { innerType: typeof inner, keys: inner && typeof inner === "object" ? Object.keys(inner as object) : [] });
  }
  return parsed;
}

export async function persistChatSessionSnapshot(id: string, entries: ChatEntry[], extras?: { agentJsonText?: string | null }): Promise<void> {
  if (entries.length === 0) {
    logSess("persistChatSessionSnapshot skip (empty entries)", id);
    return;
  }
  logSess("persistChatSessionSnapshot start", id, "entries", entries.length, "webview", hasWebProviderHost());
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
      if (e.llmUsage && typeof e.llmUsage === "object") row.llmUsage = { ...e.llmUsage } as Record<string, unknown>;
      return row;
    }),
  };
  const agentJsonText = extras?.agentJsonText ?? prev?.agentJsonText;
  logSess("persistChatSessionSnapshot agentJson", { fromExtras: !!extras?.agentJsonText, fromPrev: !!prev?.agentJsonText, willSave: !!agentJsonText, len: agentJsonText?.length ?? 0 });
  if (agentJsonText) session.agentJsonText = agentJsonText;
  if (!hasWebProviderHost()) {
    saveSessionFullLocal(session);
    logSess("persistChatSessionSnapshot saved (localStorage)", id);
    notifyChatSessionsChanged();
    return;
  }
  const r = await callProviderRpc({ method: "saveChatSession", session });
  logSess("persistChatSessionSnapshot ← saveChatSession", id, { ok: r.ok, error: r.error });
  if (r.ok) notifyChatSessionsChanged();
  else if (typeof console !== "undefined" && console.warn) {
    console.warn(LOG_SESS, "saveChatSession failed", r.error);
  }
}

export async function deleteChatSession(id: string): Promise<void> {
  if (!hasWebProviderHost()) {
    deleteSessionLocal(id);
    logSess("deleteChatSession (localStorage)", id);
    notifyChatSessionsChanged();
    return;
  }
  logSess("deleteChatSession → providerRpc", id);
  const r = await callProviderRpc({ method: "deleteChatSession", id });
  logSess("deleteChatSession ←", id, { ok: r.ok, error: r.error });
  if (r.ok) notifyChatSessionsChanged();
}
