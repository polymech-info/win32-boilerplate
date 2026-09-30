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

export function toggleColorScheme(): void {
  const nextDark = !document.documentElement.classList.contains("dark");
  document.documentElement.classList.toggle("dark", nextDark);
  persist(nextDark ? "dark" : "light");
  window.dispatchEvent(new CustomEvent(CHANGE_EVENT));
}
