/**
 * Dev / browser persistence for chat sessions (ported from ref/chatSessions.ts).
 * Keys are namespaced for chat-next; host WebView uses providerRpc instead (see chatSessionsBackend.ts).
 */

const SESSIONS_INDEX_KEY = "pm-chat-next.chat-sessions-index";
const SESSION_PREFIX = "pm-chat-next.chat-session-";
const MAX_SESSIONS = 50;

const ROLES = ["user", "assistant", "tool", "system", "error", "image", "file"] as const;

export type ChatSessionMeta = {
  id: string;
  title: string;
  createdAt: number;
  updatedAt: number;
};

/** Serialized row (aligned with `ChatEntry` in pmChatStore). */
export type StoredChatEntry = {
  id: number;
  role: string;
  text: string;
  ts?: number;
  folderHint?: string;
  contextPaths?: string[];
  resultPaths?: string[];
  llmUsage?: Record<string, unknown>;
};

export type StoredChatSession = ChatSessionMeta & { entries: StoredChatEntry[]; agentJsonText?: string };

function parseRole(x: unknown): string {
  return typeof x === "string" && (ROLES as readonly string[]).includes(x as (typeof ROLES)[number]) ? x : "assistant";
}

function parseEntry(x: unknown): StoredChatEntry | null {
  if (!x || typeof x !== "object") return null;
  const o = x as Record<string, unknown>;
  const id = Number(o.id);
  const text = typeof o.text === "string" ? o.text : "";
  if (!Number.isFinite(id)) return null;
  const tsRaw = o.ts;
  const ts =
    typeof tsRaw === "number" && Number.isFinite(tsRaw)
      ? tsRaw
      : typeof tsRaw === "string" && /^\d+$/.test(tsRaw)
        ? Number(tsRaw)
        : undefined;
  const base: StoredChatEntry = { id, role: parseRole(o.role), text };
  if (ts != null && Number.isFinite(ts)) base.ts = ts;
  const cp = o.contextPaths;
  if (Array.isArray(cp) && cp.length && cp.every((x): x is string => typeof x === "string")) {
    const paths = cp.map((x) => x.trim()).filter(Boolean);
    if (paths.length) base.contextPaths = paths;
  }
  const rp = o.resultPaths;
  if (Array.isArray(rp) && rp.length && rp.every((x): x is string => typeof x === "string")) {
    const paths = rp.map((x) => x.trim()).filter(Boolean);
    if (paths.length) base.resultPaths = paths;
  }
  const fh = o.folderHint;
  if (typeof fh === "string" && fh.trim()) base.folderHint = fh.trim();
  const lu = o.llmUsage;
  if (lu && typeof lu === "object" && !Array.isArray(lu)) base.llmUsage = lu as Record<string, unknown>;
  return base;
}

export function generateSessionTitleFromEntries(entries: { role: string; text: string }[]): string {
  const firstUser = entries.find((m) => m.role === "user");
  if (!firstUser?.text) return "New Chat";
  const t = firstUser.text.trim();
  if (t.length <= 60) return t || "New Chat";
  return `${t.slice(0, 60)}…`;
}

/** Session metadata only, newest first */
export function listSessionsMetaLocal(): ChatSessionMeta[] {
  try {
    const raw = localStorage.getItem(SESSIONS_INDEX_KEY);
    if (!raw) return [];
    const index = JSON.parse(raw) as unknown;
    if (!Array.isArray(index)) return [];
    const out: ChatSessionMeta[] = [];
    for (const row of index) {
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
  } catch {
    return [];
  }
}

export function loadSessionFullLocal(id: string): StoredChatSession | null {
  try {
    const raw = localStorage.getItem(SESSION_PREFIX + id);
    if (!raw) return null;
    const o = JSON.parse(raw) as Record<string, unknown>;
    if (typeof o.id !== "string") return null;
    const entriesRaw = o.entries ?? o.messages;
    const entries: StoredChatEntry[] = [];
    if (Array.isArray(entriesRaw)) {
      for (const x of entriesRaw) {
        const e = parseEntry(x);
        if (e) entries.push(e);
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
    console.log("[pm-chat:agentJson] loadSessionFullLocal", { id, rawFieldType: typeof o.agentJsonText, agentJsonPresent: !!session.agentJsonText, agentJsonLen: session.agentJsonText?.length ?? 0 });
    return session;
  } catch {
    return null;
  }
}

export function saveSessionFullLocal(session: StoredChatSession): void {
  try {
    localStorage.setItem(SESSION_PREFIX + session.id, JSON.stringify(session));
    const index = listSessionsMetaLocal();
    const meta: ChatSessionMeta = {
      id: session.id,
      title: session.title,
      createdAt: session.createdAt,
      updatedAt: session.updatedAt,
    };
    const existing = index.findIndex((s) => s.id === session.id);
    const next = [...index];
    if (existing >= 0) next[existing] = meta;
    else next.unshift(meta);
    next.sort((a, b) => b.updatedAt - a.updatedAt);
    while (next.length > MAX_SESSIONS) {
      const removed = next.pop();
      if (removed) localStorage.removeItem(SESSION_PREFIX + removed.id);
    }
    localStorage.setItem(SESSIONS_INDEX_KEY, JSON.stringify(next));
  } catch {
    /* quota / private mode */
  }
}

export function deleteSessionLocal(id: string): void {
  try {
    localStorage.removeItem(SESSION_PREFIX + id);
    const index = listSessionsMetaLocal().filter((s) => s.id !== id);
    localStorage.setItem(SESSIONS_INDEX_KEY, JSON.stringify(index));
  } catch {
    /* */
  }
}
