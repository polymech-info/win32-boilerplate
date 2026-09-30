import { router } from "@/router";
import { usePmChatStore } from "@/pm-chat/pmChatStore";

function parseBusPayload(detail: unknown): Record<string, unknown> | null {
  if (!detail || typeof detail !== "object") return null;
  const payload = (detail as { payload?: unknown }).payload;
  return payload && typeof payload === "object" ? (payload as Record<string, unknown>) : null;
}

export function attachChatHostNavigation(): void {
  if (typeof window === "undefined") return;
  window.addEventListener("cweb_bus", (ev: Event) => {
    const payload = parseBusPayload((ev as CustomEvent).detail);
    if (!payload) return;
    if (payload.t === "chat_navigate_session") {
      const sessionId = typeof payload.sessionId === "string" ? payload.sessionId.trim() : "";
      if (!sessionId) return;
      void router.navigate({ to: "/chat/$sessionId", params: { sessionId } });
      return;
    }
    if (payload.t === "chat_insert_draft") {
      const text = typeof payload.text === "string" ? payload.text : "";
      if (!text.trim()) return;
      usePmChatStore.getState().setInputDraft(text);
      window.setTimeout(() => document.getElementById("pm-input")?.focus(), 0);
    }
  });
}
