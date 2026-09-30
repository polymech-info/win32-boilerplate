import { useEffect } from "react";
import { createRoot } from "react-dom/client";
import { ShellPanel, SettingsPanel, useShellStore } from "./shell";
import {
  defaultShellType,
  handleConsoleHostMessage,
  installHostMessageHandler,
} from "./shell/consoleRun";
import "./shell/shell-styles.css";

const shellBootStart = performance.now();
console.info("[console-open:web] shell bundle script start");

// Global error handlers for debugging
window.addEventListener('error', (event) => {
  console.error('[Global Error]', event.error);
  console.error('[Global Error] Message:', event.message);
  console.error('[Global Error] Filename:', event.filename);
  console.error('[Global Error] Line:', event.lineno);
});

window.addEventListener('unhandledrejection', (event) => {
  console.error('[Unhandled Promise Rejection]', event.reason);
});

/**
 * PolyMech Shell App
 *
 * A terminal application built with xterm.js that integrates with the C++ backend.
 * Features:
 * - Multiple shell support (PowerShell, CMD, Bash, WSL, Git Bash)
 * - Accordion-style shell list on the right side
 * - Settings panel with gear icon
 * - WebView2 integration for C++ host communication
 */

function App() {
  const { settings, updateSettings } = useShellStore();

  useEffect(() => {
    console.info(
      `[console-open:web] App mounted ${Math.round(performance.now() - shellBootStart)} ms after script start`
    );
  }, []);

  // Apply theme to document
  useEffect(() => {
    const isDark =
      settings.theme === "dark" ||
      (settings.theme === "system" &&
        window.matchMedia("(prefers-color-scheme: dark)").matches);

    document.documentElement.classList.toggle("dark", isDark);
    document.documentElement.setAttribute(
      "data-theme",
      isDark ? "dark" : "light"
    );
  }, [settings.theme]);

  // Handle host messages from C++ backend (queued by bridge until registered)
  useEffect(() => {
    return installHostMessageHandler((data) => {
      handleConsoleHostMessage(data, (theme) => updateSettings({ theme }));
    });
  }, [updateSettings]);

  // Alt-drag support for window movement
  useEffect(() => {
    const isInteractive = (el: EventTarget | null): boolean => {
      let node: HTMLElement | null = el instanceof HTMLElement ? el : null;
      while (node && node !== document.body) {
        const tag = node.tagName.toLowerCase();
        if (
          tag === "input" ||
          tag === "textarea" ||
          tag === "select" ||
          tag === "button" ||
          tag === "a" ||
          node.isContentEditable
        ) {
          return true;
        }
        node = node.parentElement;
      }
      return false;
    };

    const onMouseDown = (e: MouseEvent) => {
      if (!e.altKey || e.button !== 0) return;
      if (isInteractive(e.target)) return;
      e.preventDefault();

      // Signal C++ host to begin window move
      const post = window.chrome?.webview?.postMessage;
      if (typeof post === "function") {
        post(JSON.stringify({ t: "cweb_begin_move", src: "alt_drag" }));
      }
    };

    document.addEventListener("mousedown", onMouseDown, true);
    return () => document.removeEventListener("mousedown", onMouseDown, true);
  }, []);

  // No default shell tab — ribbon/console_run creates tabs on demand.

  return (
    <div className="shell-app">
      <ShellPanel />
      <SettingsPanel />
    </div>
  );
}

// Mount the app
const rootEl = document.getElementById("root");
if (!rootEl) {
  throw new Error("Root element #root not found");
}

createRoot(rootEl).render(
  // NOTE: StrictMode disabled for PTY testing to prevent rapid unmount/remount
  // <StrictMode>
    <App />
  // </StrictMode>
);

console.info(
  `[console-open:web] createRoot/render dispatched ${Math.round(performance.now() - shellBootStart)} ms after script start`
);
