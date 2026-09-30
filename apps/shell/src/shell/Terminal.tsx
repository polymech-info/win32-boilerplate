import { useEffect, useRef, useState } from "react";
import { Terminal as XTerm, type ILink } from "@xterm/xterm";
import { FitAddon } from "@xterm/addon-fit";
import "@xterm/xterm/css/xterm.css";
import type { ShellType, ShellSettings } from "./shellStore";
import { usePtyWebSocket } from "./usePtyWebSocket";

interface TerminalProps {
  shellId: string;
  shellType: ShellType;
  settings: ShellSettings;
  isActive: boolean;
  onData?: (data: string) => void;
  onResize?: (cols: number, rows: number) => void;
  onImagePath?: (shellId: string, path: string) => void;
}

// Enable PTY mode when VITE_PTY_WS_URL is set or in dev mode
const PTY_ENABLED =
  import.meta.env.VITE_PTY_WS_URL || import.meta.env.DEV;

type NativeHostWindow = Window & {
  chrome?: {
    webview?: {
      postMessage?: (message: string) => void;
    };
  };
};

function nativePost(message: Record<string, unknown>): void {
  const post = (window as NativeHostWindow).chrome?.webview?.postMessage;
  if (typeof post === "function") {
    post(JSON.stringify(message));
  }
}

function openTerminalFileLink(path: string, line?: number, column?: number): void {
  nativePost({ t: "shell_open_link", path, line, column });
}

function openTerminalUrl(url: string, openInApp: boolean): void {
  if (hasNativePtyHost()) {
    nativePost({ t: "shell_open_url", url, inApp: openInApp });
    return;
  }
  window.open(url, "_blank", "noopener,noreferrer");
}

const IMAGE_PATH_RE = /[A-Za-z]:[^\r\n<>|"]+?\.(?:png|jpe?g|gif|webp|bmp|tiff?|avif)/gi;

function stripAnsi(text: string): string {
  return text.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "");
}

function collectImagePaths(text: string): string[] {
  const clean = stripAnsi(text);
  const out: string[] = [];
  for (const match of clean.matchAll(IMAGE_PATH_RE)) {
    const path = match[0].trim().replace(/[),.;:]+$/g, "");
    if (path) out.push(path);
  }
  return [...new Set(out)];
}

function offsetToBufferPosition(
  offset: number,
  segments: Array<{ y: number; start: number; text: string }>
) {
  let segment = segments[segments.length - 1];
  for (const candidate of segments) {
    if (offset <= candidate.start + candidate.text.length) {
      segment = candidate;
      break;
    }
  }
  return {
    x: Math.max(1, offset - segment.start + 1),
    y: segment.y,
  };
}

function terminalLineLinks(
  term: XTerm,
  bufferLineNumber: number,
  openLinksInApp: boolean
): ILink[] | undefined {
  const buffer = term.buffer.active;
  let groupStart = bufferLineNumber;
  while (groupStart > 1 && buffer.getLine(groupStart - 1)?.isWrapped) {
    groupStart--;
  }

  let groupEnd = bufferLineNumber;
  while (buffer.getLine(groupEnd)?.isWrapped) {
    groupEnd++;
  }

  const segments: Array<{ y: number; start: number; text: string }> = [];
  let text = "";
  for (let y = groupStart; y <= groupEnd; y++) {
    const line = buffer.getLine(y - 1);
    if (!line) continue;
    const part = line.translateToString(y === groupEnd);
    segments.push({ y, start: text.length, text: part });
    text += part;
  }
  if (!text) return undefined;

  const links: ILink[] = [];
  const linkRegex = new RegExp(
    [
      String.raw`(?<url>https?:\/\/[^\s"'<>]+)`,
      String.raw`(?<path>(?:[A-Za-z]:[\\/]|\.{1,2}[\\/]|[\\/])(?:[^\s"'<>|()[\]{}:]+[\\/])*[^\s"'<>|()[\]{}:]+)(?::(?<line>\d+)(?::(?<col>\d+))?)?`,
    ].join("|"),
    "g"
  );
  let match: RegExpExecArray | null;
  while ((match = linkRegex.exec(text)) !== null) {
    const endOffset = match.index + match[0].length;
    const intersectsCurrentLine = segments.some(
      (segment) =>
        segment.y === bufferLineNumber &&
        match!.index < segment.start + segment.text.length &&
        endOffset > segment.start
    );
    if (!intersectsCurrentLine) continue;

    const text = match[0];
    const path = match.groups?.path;
    const url = match.groups?.url;
    const row = match.groups?.line ? Number(match.groups.line) : undefined;
    const col = match.groups?.col ? Number(match.groups.col) : undefined;
    links.push({
      text,
      range: {
        start: offsetToBufferPosition(match.index, segments),
        end: offsetToBufferPosition(endOffset, segments),
      },
      decorations: { pointerCursor: true, underline: false },
      activate: () => {
        if (url) {
          openTerminalUrl(url, openLinksInApp);
        } else if (path) {
          openTerminalFileLink(path, row, col);
        }
      },
    });
  }
  return links.length > 0 ? links : undefined;
}

function hasNativePtyHost(): boolean {
  return (
    typeof window !== "undefined" &&
    typeof (window as NativeHostWindow).chrome?.webview?.postMessage === "function"
  );
}

export function Terminal({
  shellId,
  shellType,
  settings,
  isActive,
  onData,
  onResize,
  onImagePath,
}: TerminalProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const terminalRef = useRef<XTerm | null>(null);
  const fitAddonRef = useRef<FitAddon | null>(null);
  const resizeObserverRef = useRef<ResizeObserver | null>(null);
  const isInitializedRef = useRef(false);
  const lastResizeRef = useRef<{ cols: number; rows: number } | null>(null);
  const nativeStartPostedRef = useRef<string | null>(null);
  const onDataRef = useRef(onData);
  const onResizeRef = useRef(onResize);
  const onImagePathRef = useRef(onImagePath);
  const openLinksInAppRef = useRef(settings.openLinksInApp);
  const [showDemoMessage, setShowDemoMessage] = useState(!PTY_ENABLED);
  const [terminalReady, setTerminalReady] = useState(false);
  const [nativePty, setNativePty] = useState(() => hasNativePtyHost());
  const webSocketPty = Boolean(PTY_ENABLED && !nativePty);

  useEffect(() => {
    onDataRef.current = onData;
  }, [onData]);

  useEffect(() => {
    onResizeRef.current = onResize;
  }, [onResize]);

  useEffect(() => {
    onImagePathRef.current = onImagePath;
  }, [onImagePath]);

  useEffect(() => {
    openLinksInAppRef.current = settings.openLinksInApp;
  }, [settings.openLinksInApp]);

  useEffect(() => {
    if (nativePty) return;
    const timer = window.setInterval(() => {
      if (hasNativePtyHost()) {
        setNativePty(true);
        window.clearInterval(timer);
      }
    }, 50);
    return () => window.clearInterval(timer);
  }, [nativePty]);

  const notifyResize = (cols: number, rows: number) => {
    const next = { cols: Math.floor(cols), rows: Math.floor(rows) };
    const last = lastResizeRef.current;
    if (last && last.cols === next.cols && last.rows === next.rows) return;
    lastResizeRef.current = next;
    onResizeRef.current?.(next.cols, next.rows);
  };

  // Initialize terminal
  useEffect(() => {
    if (!containerRef.current || isInitializedRef.current) return;

    const terminalInitStart = performance.now();
    console.info(`[console-open:web] terminal init begin shell=${shellId} active=${isActive}`);

    const term = new XTerm({
      fontSize: settings.fontSize,
      fontFamily: settings.fontFamily,
      cursorStyle: settings.cursorStyle,
      cursorBlink: settings.cursorBlink,
      scrollback: settings.scrollback,
      wordSeparator: settings.wordSeparator,
      theme: {
        background: settings.theme === "light" ? "#ffffff" : "#1e1e1e",
        foreground: settings.theme === "light" ? "#333333" : "#d4d4d4",
        cursor: settings.theme === "light" ? "#333333" : "#d4d4d4",
        selectionBackground: settings.theme === "light" ? "#add6ff" : "#264f78",
        black: "#000000",
        red: "#cd3131",
        green: "#0dbc79",
        yellow: "#e5e510",
        blue: "#2472c8",
        magenta: "#bc3fbc",
        cyan: "#11a8cd",
        white: "#e5e5e5",
        brightBlack: "#666666",
        brightRed: "#f14c4c",
        brightGreen: "#23d18b",
        brightYellow: "#f5f543",
        brightBlue: "#3b8eea",
        brightMagenta: "#d670d6",
        brightCyan: "#29b8db",
        brightWhite: "#e5e5e5",
      },
    });

    const fitAddon = new FitAddon();
    term.loadAddon(fitAddon);
    term.registerLinkProvider({
      provideLinks: (bufferLineNumber, callback) =>
        callback(terminalLineLinks(term, bufferLineNumber, openLinksInAppRef.current)),
    });
    term.attachCustomKeyEventHandler((event) => {
      if (event.type !== "keydown") return true;
      const copyKey = event.ctrlKey && event.shiftKey && event.code === "KeyC";
      const pasteKey = event.ctrlKey && event.shiftKey && event.code === "KeyV";
      const requestPaste = () => {
        term.focus();
        if (hasNativePtyHost()) {
          nativePost({ t: "shell_paste", shellId, shellType });
          return;
        }
        void navigator.clipboard?.readText().then((text) => {
          if (text) term.paste(text);
        });
      };
      if (copyKey) {
        const selection = term.getSelection();
        if (selection) {
          void navigator.clipboard?.writeText(selection);
        }
        return false;
      }
      if (pasteKey) {
        requestPaste();
        return false;
      }
      return true;
    });

    term.open(containerRef.current);
    fitAddon.fit();

    terminalRef.current = term;
    fitAddonRef.current = fitAddon;
    isInitializedRef.current = true;
    setTerminalReady(true);
    console.info(
      `[console-open:web] terminal init ready shell=${shellId} ${Math.round(performance.now() - terminalInitStart)} ms`
    );

    // Handle resize
    resizeObserverRef.current = new ResizeObserver(() => {
      if (fitAddonRef.current && terminalRef.current) {
        fitAddonRef.current.fit();
        const dimensions = fitAddonRef.current.proposeDimensions();
        if (dimensions) {
          notifyResize(dimensions.cols, dimensions.rows);
        }
      }
    });

    if (containerRef.current.parentElement) {
      resizeObserverRef.current.observe(containerRef.current.parentElement);
    }

    const onContextMenu = (event: MouseEvent) => {
      event.preventDefault();
      event.stopPropagation();
      term.focus();
      if (hasNativePtyHost()) {
        nativePost({ t: "shell_paste", shellId, shellType });
        return;
      }
      void navigator.clipboard?.readText().then((text) => {
        if (text) term.paste(text);
      });
    };
    const onAuxClick = (event: MouseEvent) => {
      if (event.button !== 1) return;
      event.preventDefault();
      event.stopPropagation();
      term.focus();
      if (hasNativePtyHost()) {
        nativePost({ t: "shell_paste", shellId, shellType });
        return;
      }
      void navigator.clipboard?.readText().then((text) => {
        if (text) term.paste(text);
      });
    };
    const preventNativeContextMenu = (event: MouseEvent) => {
      if (event.button === 2) {
        event.preventDefault();
        event.stopPropagation();
      }
    };
    containerRef.current.addEventListener("mousedown", preventNativeContextMenu, true);
    containerRef.current.addEventListener("contextmenu", onContextMenu, true);
    containerRef.current.addEventListener("auxclick", onAuxClick, true);

    return () => {
      resizeObserverRef.current?.disconnect();
      containerRef.current?.removeEventListener("mousedown", preventNativeContextMenu, true);
      containerRef.current?.removeEventListener("contextmenu", onContextMenu, true);
      containerRef.current?.removeEventListener("auxclick", onAuxClick, true);
      term.dispose();
      terminalRef.current = null;
      fitAddonRef.current = null;
      isInitializedRef.current = false;
    };
  }, []);

  // PTY WebSocket connection (when enabled) - only connect after terminal is ready
  const {
    isConnected,
    isReady,
    reconnectAttempt,
    sendInput,
    resize: resizePty,
    reconnect,
  } = usePtyWebSocket({
    shellId,
    shellType,
    terminal: terminalRef.current,
    enabled: webSocketPty && terminalReady,
    onConnect: () => {
      console.log("[Terminal] PTY connected");
      setShowDemoMessage(false);
    },
    onDisconnect: () => {
      console.log("[Terminal] PTY disconnected");
    },
    onError: (error) => {
      console.error("[Terminal] PTY error:", error);
      setShowDemoMessage(true);
    },
    onExit: (code) => {
      terminalRef.current?.writeln(`\r\n\x1b[31mProcess exited with code ${code}\x1b[0m`);
    },
    onOutput: (data) => {
      for (const path of collectImagePaths(data)) onImagePathRef.current?.(shellId, path);
    },
  });

  // Show reconnection status in terminal
  useEffect(() => {
    if (reconnectAttempt > 0 && terminalRef.current) {
      terminalRef.current.writeln(`\r\n\x1b[33m[Reconnecting... attempt ${reconnectAttempt}]\x1b[0m`);
    }
  }, [reconnectAttempt]);

  // Native WebView2 PTY output from the C++ host.
  useEffect(() => {
    if (!nativePty || !terminalRef.current) return;

    const onOutput = (event: Event) => {
      const detail = (event as CustomEvent).detail as
        | { shellId?: string; data?: string }
        | undefined;
      if (detail?.shellId === shellId && typeof detail.data === "string") {
        terminalRef.current?.write(detail.data);
        for (const path of collectImagePaths(detail.data)) onImagePathRef.current?.(shellId, path);
      }
    };
    const onExit = (event: Event) => {
      const detail = (event as CustomEvent).detail as
        | { shellId?: string; code?: number; autoClose?: boolean }
        | undefined;
      if (detail?.shellId !== shellId) return;
      nativeStartPostedRef.current = null;
      if (detail.autoClose) return;
      terminalRef.current?.writeln(
        `\r\n\x1b[31mProcess exited with code ${detail.code ?? 0}\x1b[0m`
      );
    };
    window.addEventListener("shell-output", onOutput);
    window.addEventListener("shell-exit", onExit);
    return () => {
      window.removeEventListener("shell-output", onOutput);
      window.removeEventListener("shell-exit", onExit);
    };
  }, [nativePty, shellId]);

  // Handle terminal input
  useEffect(() => {
    if (!terminalRef.current) return;

    const disposable = terminalRef.current.onData((data) => {
      if (nativePty) {
        onDataRef.current?.(data);
      } else if (webSocketPty && isReady) {
        // Send to PTY server
        sendInput(data);
      } else {
        // Demo mode: echo back
        onDataRef.current?.(data);

        if (data === "\r") {
          terminalRef.current?.writeln("");
          terminalRef.current?.write("$ ");
        } else if (data === "\x7f") {
          // Backspace
          terminalRef.current?.write("\b \b");
        } else {
          terminalRef.current?.write(data);
        }
      }
    });

    return () => disposable.dispose();
  }, [nativePty, webSocketPty, isReady, sendInput]);

  // Handle resize → PTY
  useEffect(() => {
    if (!terminalRef.current) return;

    const disposable = terminalRef.current.onResize(({ cols, rows }) => {
      if (webSocketPty && isReady) {
        resizePty(cols, rows);
      }
      notifyResize(cols, rows);
    });

    return () => disposable.dispose();
  }, [webSocketPty, isReady, resizePty]);

  // Initial prompt / connection message
  useEffect(() => {
    if (!terminalRef.current || !isActive || !terminalReady) return;

    if (nativePty) {
      if (nativeStartPostedRef.current !== shellId) {
        nativeStartPostedRef.current = shellId;
        console.info(`[console-open:web] native shell_start post shell=${shellId}`);
        terminalRef.current.writeln(`\x1b[1;32mPolyMech Shell\x1b[0m - ${shellType}`);
        terminalRef.current.writeln("\x1b[90mConnecting to native PTY host...\x1b[0m");
        nativePost({
          t: "shell_start",
          shellId,
          shellType,
          cols: terminalRef.current.cols || 80,
          rows: terminalRef.current.rows || 24,
        });
      }
    } else if (webSocketPty) {
      if (isConnected) {
        terminalRef.current.writeln(`\x1b[1;32mPolyMech Shell\x1b[0m - ${shellType}`);
        terminalRef.current.writeln("\x1b[90mConnecting to PTY server...\x1b[0m");
      } else {
        terminalRef.current.writeln(`\x1b[1;32mPolyMech Shell\x1b[0m - ${shellType}`);
        terminalRef.current.writeln("\x1b[31mPTY server not available\x1b[0m");
        terminalRef.current.writeln("\x1b[90mRun: node server/pty-server.js\x1b[0m");
      }
    } else {
      // Demo mode prompt
      terminalRef.current.writeln(`\x1b[1;32mPolyMech Shell\x1b[0m - ${shellType}`);
      terminalRef.current.writeln("\x1b[90mDemo mode (PTY disabled)\x1b[0m");
      terminalRef.current.writeln("");
      terminalRef.current.write("$ ");
    }
  }, [shellId, shellType, isActive, isConnected, nativePty, webSocketPty, terminalReady]);

  // Update settings when they change
  useEffect(() => {
    if (!terminalRef.current) return;

    terminalRef.current.options.fontSize = settings.fontSize;
    terminalRef.current.options.fontFamily = settings.fontFamily;
    terminalRef.current.options.cursorStyle = settings.cursorStyle;
    terminalRef.current.options.cursorBlink = settings.cursorBlink;
    terminalRef.current.options.scrollback = settings.scrollback;
    terminalRef.current.options.wordSeparator = settings.wordSeparator;
    terminalRef.current.options.theme = {
      background: settings.theme === "light" ? "#ffffff" : "#1e1e1e",
      foreground: settings.theme === "light" ? "#333333" : "#d4d4d4",
      cursor: settings.theme === "light" ? "#333333" : "#d4d4d4",
      selectionBackground: settings.theme === "light" ? "#add6ff" : "#264f78",
      black: "#000000",
      red: "#cd3131",
      green: "#0dbc79",
      yellow: "#e5e510",
      blue: "#2472c8",
      magenta: "#bc3fbc",
      cyan: "#11a8cd",
      white: "#e5e5e5",
      brightBlack: "#666666",
      brightRed: "#f14c4c",
      brightGreen: "#23d18b",
      brightYellow: "#f5f543",
      brightBlue: "#3b8eea",
      brightMagenta: "#d670d6",
      brightCyan: "#29b8db",
      brightWhite: "#e5e5e5",
    };

    // Refit when font changes
    setTimeout(() => {
      fitAddonRef.current?.fit();
    }, 0);
  }, [settings]);

  // Focus terminal when it becomes active
  useEffect(() => {
    if (isActive && terminalRef.current) {
      setTimeout(() => {
        fitAddonRef.current?.fit();
        const dimensions = fitAddonRef.current?.proposeDimensions();
        if (dimensions) notifyResize(dimensions.cols, dimensions.rows);
        terminalRef.current?.focus();
      }, 0);
    }
  }, [isActive]);

  return (
    <div
      ref={containerRef}
      className="terminal-container"
      style={{
        width: "100%",
        height: "100%",
        padding: 0,
        display: isActive ? "block" : "none",
      }}
    />
  );
}
