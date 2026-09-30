/// <reference types="vite/client" />

declare global {
  interface Window {
    pmChat?: Record<string, unknown>;
    chrome?: {
      webview?: {
        postMessage?: (message: string) => void;
        addEventListener?: (type: string, listener: EventListener) => void;
        removeEventListener?: (type: string, listener: EventListener) => void;
      };
    };
  }
}

export {};
