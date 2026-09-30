import { DEMO_SESSION_ID } from "@/chat/demoSession";

const KEY = "pm-chat-next.recentSessions";
const MAX_STORED = 12;
const MAX_SHOWN = 6;

/** Remember a session after the user opens it. If moveToTop is false, only ensures it's in the list without reordering. */
export function recordSessionVisited(sessionId: string, moveToTop: boolean = true): void {
  const id = String(sessionId ?? "").trim();
  if (!id) return;
  try {
    const raw = localStorage.getItem(KEY);
    const parsed: unknown = raw ? JSON.parse(raw) : [];
    const prev = Array.isArray(parsed)
      ? parsed.filter((x): x is string => typeof x === "string")
      : [];
    const exists = prev.includes(id);
    // If already in list and not moving to top, keep current order
    if (exists && !moveToTop) return;
    // Otherwise, move to top (or add if not exists)
    const filtered = prev.filter((x) => x !== id);
    const next = [id, ...filtered].slice(0, MAX_STORED);
    localStorage.setItem(KEY, JSON.stringify(next));
  } catch {
    /* private mode / quota */
  }
}

function getStoredIds(): string[] {
  try {
    const raw = localStorage.getItem(KEY);
    const p = raw ? JSON.parse(raw) : [];
    return Array.isArray(p) ? p.filter((x): x is string => typeof x === "string").slice(0, MAX_STORED) : [];
  } catch {
    return [];
  }
}

function labelForSessionId(id: string): string {
  if (id === DEMO_SESSION_ID) return "Demo";
  if (id.length <= 14) return id;
  return `${id.slice(0, 8)}…`;
}

export type SessionSidebarOptions = {
  /** When true (e.g. app home `/`), omit Demo — that surface is its own new chat, not the demo session. */
  omitDemo?: boolean;
};

/** Demo first (unless omitted), then recent ids (deduped), capped for a compact list. */
export function getSessionSidebarRows(options?: SessionSidebarOptions): { id: string; label: string }[] {
  const stored = getStoredIds();
  const seen = new Set<string>();
  const out: { id: string; label: string }[] = [];
  const push = (id: string) => {
    if (seen.has(id)) return;
    seen.add(id);
    out.push({ id, label: labelForSessionId(id) });
  };

  if (!options?.omitDemo) {
    push(DEMO_SESSION_ID);
  }

  for (const id of stored) {
    if (id === DEMO_SESSION_ID) continue;
    push(id);
    if (out.length >= MAX_SHOWN) break;
  }

  return out;
}
