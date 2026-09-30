import { useShellStore, type ShellType } from "./shellStore";

export type ConsoleRunPayload = {
  text: string;
  newShell?: boolean;
  shellId?: string;
  closeOnExit?: boolean;
};

type HostMessage = Record<string, unknown>;

const readyShells = new Set<string>();
const startingShells = new Set<string>();
const closeOnExitShells = new Set<string>();
const pendingByShell = new Map<string, string[]>();

const READY_TIMEOUT_MS = 10_000;
const DEFAULT_COLS = 80;
const DEFAULT_ROWS = 24;

declare global {
  interface Window {
    cwWebSetHostMessageHandler?: (handler: HostMessageHandler | null) => void;
    cwWebOnHostMessage?: (data: HostMessage) => void;
  }
}

type HostMessageHandler = (data: HostMessage) => void;

export function defaultShellType(): ShellType {
  return navigator.platform.toLowerCase().includes("win") ? "powershell" : "bash";
}

function nativePost(message: Record<string, unknown>): void {
  window.chrome?.webview?.postMessage?.(JSON.stringify(message));
}

function postShellStart(shellId: string, shellType: ShellType, oneShot = false): void {
  nativePost({
    t: "shell_start",
    shellId,
    shellType,
    cols: DEFAULT_COLS,
    rows: DEFAULT_ROWS,
    oneShot,
  });
}

function postShellData(shellId: string, shellType: ShellType, text: string): void {
  const data = text.replace(/\r\n?/g, "\n").replace(/\n/g, "\r");
  nativePost({
    t: "shell_data",
    shellId,
    shellType,
    data: data.endsWith("\r") ? data : `${data}\r`,
  });
}

function flushPending(shellId: string, shellType: ShellType): void {
  const queue = pendingByShell.get(shellId);
  if (!queue?.length) return;
  pendingByShell.delete(shellId);
  for (const text of queue) postShellData(shellId, shellType, text);
}

export function markShellReady(shellId: string): void {
  if (!shellId) return;
  readyShells.add(shellId);
  startingShells.delete(shellId);
  const shell = useShellStore.getState().shells.find((s) => s.id === shellId);
  if (shell) flushPending(shellId, shell.type);
}

export function markShellClosed(shellId: string): void {
  if (!shellId) return;
  readyShells.delete(shellId);
  startingShells.delete(shellId);
  closeOnExitShells.delete(shellId);
  pendingByShell.delete(shellId);
}

function removeShellTab(shellId: string): void {
  const st = useShellStore.getState();
  if (st.shells.some((s) => s.id === shellId)) st.removeShell(shellId);
}

/** Append shell exit so the PTY closes after a one-shot host command (matches external closeOnExit). */
function wrapForCloseOnExit(text: string, shellType: ShellType): string {
  const trimmed = text.trimEnd();
  if (/\bexit\b\s*$/i.test(trimmed)) return trimmed;
  if (shellType === "cmd") return `${trimmed} & exit`;
  return `${trimmed}; exit`;
}

function disposeShellTab(shellId: string): void {
  markShellClosed(shellId);
  removeShellTab(shellId);
}

/** Drop orphaned one-shot tabs whose PTY already ended but UI tab was not removed. */
function purgeDeadCloseOnExitTabs(): void {
  for (const shellId of [...closeOnExitShells]) {
    if (readyShells.has(shellId) || startingShells.has(shellId)) continue;
    disposeShellTab(shellId);
  }
}

function resolveShell(newShell: boolean, explicitShellId?: string, closeOnExit?: boolean): {
  shellId: string;
  shellType: ShellType;
} {
  const st = useShellStore.getState();
  const fallbackType = defaultShellType();

  if (!newShell) {
    const preferredId = explicitShellId ?? st.activeShellId ?? undefined;
    if (preferredId) {
      const shell = st.shells.find((s) => s.id === preferredId);
      if (shell) {
        st.setActiveShell(preferredId);
        return { shellId: preferredId, shellType: shell.type };
      }
    }
    if (st.shells.length > 0) {
      const shell = st.shells.find((s) => s.id === st.activeShellId) ?? st.shells[0];
      st.setActiveShell(shell.id);
      return { shellId: shell.id, shellType: shell.type };
    }
    const createdId = st.addShell(fallbackType);
    return { shellId: createdId, shellType: fallbackType };
  }

  // One-shot closeOnExit commands always get a fresh tab (avoid reusing a stuck recording shell).
  if (!closeOnExit) {
    const idleShells = st.shells.filter((s) => !readyShells.has(s.id) && !startingShells.has(s.id));
    if (idleShells.length === 1) {
      const shell = idleShells[0];
      st.setActiveShell(shell.id);
      return { shellId: shell.id, shellType: shell.type };
    }
  }

  const shellId = st.addShell(fallbackType);
  return { shellId, shellType: fallbackType };
}

function scheduleReadyFallback(shellId: string): void {
  window.setTimeout(() => {
    if (readyShells.has(shellId)) return;
    console.warn(`[console-run] shell_ready timeout for ${shellId}; sending queued input anyway`);
    markShellReady(shellId);
  }, READY_TIMEOUT_MS);
}

/** Host-initiated direct command execution (ribbon, chat bus, C++ RunInWebConsole). */
export function runConsoleCommand(payload: ConsoleRunPayload): void {
  let text = payload.text.trim();
  if (!text) return;

  if (payload.closeOnExit) purgeDeadCloseOnExitTabs();

  const { shellId, shellType } = resolveShell(
    payload.newShell !== false,
    payload.shellId,
    payload.closeOnExit === true
  );

  if (payload.closeOnExit) {
    closeOnExitShells.add(shellId);
    text = wrapForCloseOnExit(text, shellType);
  }

  if (readyShells.has(shellId)) {
    postShellData(shellId, shellType, text);
    return;
  }

  const queue = pendingByShell.get(shellId) ?? [];
  queue.push(text);
  pendingByShell.set(shellId, queue);

  if (startingShells.has(shellId)) return;

  startingShells.add(shellId);
  postShellStart(shellId, shellType, payload.closeOnExit === true);
  scheduleReadyFallback(shellId);
}

export function installHostMessageHandler(handler: HostMessageHandler): () => void {
  if (typeof window.cwWebSetHostMessageHandler === "function") {
    window.cwWebSetHostMessageHandler(handler);
    return () => window.cwWebSetHostMessageHandler?.(null);
  }

  window.cwWebOnHostMessage = handler;
  return () => {
    window.cwWebOnHostMessage = undefined;
  };
}

export function handleConsoleHostMessage(data: HostMessage, onTheme?: (theme: "dark" | "light") => void): boolean {
  if (data.t === "cweb_theme") {
    if (data.theme === "dark" || data.theme === "light") onTheme?.(data.theme);
    return true;
  }

  if (data.t === "cweb_bus") {
    const payload =
      data.payload && typeof data.payload === "object" ? (data.payload as HostMessage) : null;
    if (!payload) return false;

    if (payload.t === "console_run" && typeof payload.text === "string") {
      runConsoleCommand({
        text: payload.text,
        newShell: payload.newShell !== false,
        shellId: typeof payload.shellId === "string" ? payload.shellId : undefined,
        closeOnExit: payload.closeOnExit === true,
      });
      return true;
    }

    if (payload.t === "console_insert" && typeof payload.text === "string") {
      const st = useShellStore.getState();
      let shellId = typeof payload.shellId === "string" ? payload.shellId : st.activeShellId;
      if (!shellId) shellId = st.addShell(defaultShellType());
      else st.setActiveShell(shellId);
      window.setTimeout(() => {
        window.dispatchEvent(
          new CustomEvent("shell-draft", {
            detail: { shellId, text: payload.text },
          })
        );
      }, 0);
      return true;
    }
  }

  if (data.t === "shell_ready" && typeof data.shellId === "string") {
    markShellReady(data.shellId);
    return true;
  }

  if (data.t === "shell_output" && typeof data.shellId === "string") {
    window.dispatchEvent(
      new CustomEvent("shell-output", {
        detail: { shellId: data.shellId, data: data.data },
      })
    );
    return true;
  }

  if (data.t === "shell_exit" && typeof data.shellId === "string") {
    const autoClose = closeOnExitShells.has(data.shellId);
    if (autoClose) {
      disposeShellTab(data.shellId);
    } else {
      markShellClosed(data.shellId);
    }
    window.dispatchEvent(
      new CustomEvent("shell-exit", {
        detail: { shellId: data.shellId, code: data.code, autoClose },
      })
    );
    return true;
  }

  return false;
}
