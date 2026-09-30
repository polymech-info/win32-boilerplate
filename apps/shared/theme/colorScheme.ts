/** localStorage + <html class="dark">; Tailwind `dark:` uses @custom-variant in styles.css */
export const COLOR_SCHEME_STORAGE_KEY = "pm-chat-next.theme";

const CHANGE_EVENT = "pm-chat-color-scheme";

export function getColorSchemeDark(): boolean {
  if (typeof document === "undefined") return false;
  return document.documentElement.classList.contains("dark");
}

export function subscribeColorScheme(onStoreChange: () => void): () => void {
  if (typeof window === "undefined") return () => {};
  const fn = () => onStoreChange();
  window.addEventListener(CHANGE_EVENT, fn);
  return () => window.removeEventListener(CHANGE_EVENT, fn);
}

function persist(mode: "light" | "dark") {
  try {
    localStorage.setItem(COLOR_SCHEME_STORAGE_KEY, mode);
  } catch {
    /* ignore */
  }
}

function applyColorSchemeDark(dark: boolean, persistChoice: boolean): void {
  document.documentElement.classList.toggle("dark", dark);
  document.documentElement.setAttribute("data-theme", dark ? "dark" : "light");
  if (persistChoice) persist(dark ? "dark" : "light");
  window.dispatchEvent(new CustomEvent(CHANGE_EVENT));
}

export function applyHostColorScheme(theme: unknown): void {
  if (theme !== "dark" && theme !== "light") return;
  applyColorSchemeDark(theme === "dark", false);
}

function parseHostMessage(raw: unknown): Record<string, unknown> | null {
  if (raw == null) return null;
  if (typeof raw === "string") {
    try {
      return JSON.parse(raw) as Record<string, unknown>;
    } catch {
      return null;
    }
  }
  return typeof raw === "object" ? (raw as Record<string, unknown>) : null;
}

export function installHostColorSchemeBridge(): () => void {
  if (typeof window === "undefined") return () => {};

  const applyMessage = (raw: unknown) => {
    const msg = parseHostMessage(raw);
    if (msg?.t === "cweb_theme") applyHostColorScheme(msg.theme);
  };

  const w = window as Window & {
    chrome?: { webview?: { addEventListener?: typeof window.addEventListener; removeEventListener?: typeof window.removeEventListener } };
    cwWebOnHostMessage?: (msg: unknown) => void;
  };

  const listener = (ev: MessageEvent) => applyMessage(ev.data);
  w.chrome?.webview?.addEventListener?.("message", listener as EventListener);

  const previous = w.cwWebOnHostMessage;
  w.cwWebOnHostMessage = (msg: unknown) => {
    applyMessage(msg);
    previous?.(msg);
  };

  return () => {
    w.chrome?.webview?.removeEventListener?.("message", listener as EventListener);
    if (w.cwWebOnHostMessage !== previous)
      w.cwWebOnHostMessage = previous;
  };
}

export function toggleColorScheme(): void {
  const nextDark = !document.documentElement.classList.contains("dark");
  applyColorSchemeDark(nextDark, true);
}
