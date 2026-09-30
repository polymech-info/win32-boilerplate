import { flattenCommandDocument } from "@pm/shared/customCommands/helpers";
import type { CustomCommandItem } from "@pm/shared/customCommands/types";
import { createMemoryXbloxRuntime } from "@pm/shared/xblox/runtime";
import type { BlocksFile } from "@/schema/blocks-file";
import { blockShouldFail, buildExecutionPlan } from "./execution";
import { sampleBlocks, sampleCommandsDoc } from "./sample-data";

type MockListener = (event: MessageEvent) => void;

const MOCK_STORAGE_KEY = "pm.xblox.mockHost.document.v3";
const MOCK_PATH = "mock://pm-image/xblox-demo.xblox";

function shouldInstallMockHost() {
  if (typeof window === "undefined") return false;
  const hasNativeHost = typeof (window as Window & { chrome?: { webview?: { postMessage?: unknown } } }).chrome?.webview?.postMessage === "function";
  if (hasNativeHost) return false;
  const params = new URLSearchParams(window.location.search);
  return import.meta.env.DEV || params.get("mockHost") === "1";
}

function readMockDocument(): BlocksFile {
  try {
    const raw = window.localStorage.getItem(MOCK_STORAGE_KEY);
    if (raw) {
      const parsed = JSON.parse(raw) as BlocksFile;
      if (parsed && Array.isArray(parsed.roots)) return parsed;
    }
  } catch {
    /* fall through to seeded document */
  }
  return {
    version: 1,
    context: {
      source: "mock-xblox-host",
      selection: { files: ["C:/Users/demo/Pictures/input.png"] },
      selectionCount: 1,
      mode: "capture",
    },
    roots: sampleBlocks,
  };
}

function writeMockDocument(document: BlocksFile) {
  try {
    window.localStorage.setItem(MOCK_STORAGE_KEY, JSON.stringify(document));
  } catch {
    /* localStorage can be unavailable in browser privacy modes. */
  }
}

function commandsPayload() {
  const customCommands = flattenCommandDocument(sampleCommandsDoc);
  return {
    commandsPath: "mock://pm-image/commands.json",
    document: sampleCommandsDoc,
    registeredCommands: [
      { id: "resize", label: "Resize", available: true },
      { id: "transform", label: "Transform", available: true },
      { id: "llm agent", label: "LLM Agent", available: true },
    ],
    customCommands: customCommands.map((command: CustomCommandItem) => ({
      group: "Dev Commands",
      id: command.id,
      label: command.label,
      type: command.type,
      action: command.cliCommand ? "cli" : command.appCommand ? "app" : command.path ? "path" : "metadata",
      ...command,
    })),
  };
}

function runMockDocument(document: BlocksFile) {
  const runtime = createMemoryXbloxRuntime({ context: document.context || {} });
  const plan = buildExecutionPlan(document.roots || [], document.context || {});
  const events = [];
  let failed = false;
  for (const step of plan) {
    if (failed) {
      events.push({ path: step.path, kind: step.block.kind, status: "skipped", message: "Skipped after previous failure." });
      continue;
    }
    if (blockShouldFail(step.block)) {
      failed = true;
      events.push({ path: step.path, kind: step.block.kind, status: "error", message: "Mock host stopped this chain because the block failed.", exitCode: 1 });
    } else {
      if (step.block.kind === "log") {
        
        const evaluated = runtime.evalExpression(step.block.message);
        const message = evaluated == null ? step.block.message : String(evaluated);
        events.push(runtime.log(step.block.level, message, step.path));
      } else {
        events.push({ path: step.path, kind: step.block.kind, status: "ok", message: "Mock host executed block." });
      }
    }
  }
  return { ok: !failed, exitCode: failed ? 1 : 0, events };
}

export function installXbloxMockHostBridge() {
  if (!shouldInstallMockHost()) return false;

  const listeners = new Set<MockListener>();
  let currentDocument = readMockDocument();

  const emit = (message: Record<string, unknown>) => {
    const event = { data: JSON.stringify(message) } as MessageEvent;
    listeners.forEach((listener) => listener(event));
  };

  const reply = (id: unknown, ok: boolean, data?: unknown, error?: string) => {
    window.setTimeout(() => emit({ kind: "hostProviderRpc", id: String(id ?? ""), ok, data, error }), 40);
  };

  const webview = {
    postMessage(raw: string) {
      let message: Record<string, unknown>;
      try {
        message = JSON.parse(raw) as Record<string, unknown>;
      } catch {
        return;
      }
      const rpcId = message.rpcId;
      const method = String(message.method || "");
      if (message.kind !== "providerRpc" || !rpcId) return;

      if (method === "xbloxDocumentGet") {
        reply(rpcId, true, { path: MOCK_PATH, document: currentDocument, commands: commandsPayload() });
      } else if (method === "xbloxDocumentSave") {
        currentDocument = (message.document && typeof message.document === "object" ? message.document : currentDocument) as BlocksFile;
        writeMockDocument(currentDocument);
        reply(rpcId, true, { path: typeof message.path === "string" && message.path ? message.path : MOCK_PATH });
      } else if (method === "xbloxDocumentRun") {
        const document = (message.document && typeof message.document === "object" ? message.document : currentDocument) as BlocksFile;
        reply(rpcId, true, runMockDocument(document));
      } else if (method === "xbloxCommandsGet" || method === "settingsCustomCommandsGet") {
        reply(rpcId, true, commandsPayload());
      } else if (method === "settingsAppCommandsGet") {
        reply(rpcId, true, {
          commands: [
            { id: "chat", label: "Chat", available: true },
            { id: "takescreenshot", label: "Take screenshot", available: true },
            { id: "browse", label: "Browse", available: true },
          ],
        });
      } else if (method === "settingsRibbonCommandsGet") {
        reply(rpcId, true, {
          commands: [
            { id: "filetree", label: "Explorer", available: true },
            { id: "theme", label: "Theme", available: true },
            { id: "resetLayout", label: "Reset layout", available: true },
            { id: "settings", label: "Settings", available: true },
          ],
        });
      } else if (method === "xbloxCommandRun") {
        reply(rpcId, true, { ok: true, exitCode: 0, events: [{ status: "ok", kind: "command", message: "Mock command executed." }] });
      } else {
        reply(rpcId, false, undefined, `Mock xblox host: unknown method ${method}`);
      }
    },
    addEventListener(type: string, listener: EventListener) {
      if (type === "message") listeners.add(listener as MockListener);
    },
    removeEventListener(type: string, listener: EventListener) {
      if (type === "message") listeners.delete(listener as MockListener);
    },
  };

  const w = window as Window & { chrome?: { webview?: typeof webview } };
  w.chrome = { ...(w.chrome || {}), webview };
  window.setTimeout(() => emit({ kind: "hostXblox", path: MOCK_PATH }), 80);
  return true;
}
