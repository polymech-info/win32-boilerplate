import type { CustomCommandItem } from "../customCommands/types";
import type { ProviderRpcRequest, ProviderRpcResult, XbloxRunPayload } from "../web/hostBridge";

export type XbloxRuntimeContext = Record<string, unknown>;

export type XbloxRuntimeEvent = {
  path?: string;
  blockId?: string;
  kind?: string;
  type?: string;
  status: "running" | "ok" | "error" | "warning" | "skipped";
  message?: string;
  exitCode?: number;
  errorCode?: number;
  stdout?: string[];
  stderr?: string[];
  data?: unknown;
};

export type XbloxRuntimeRunResult = {
  ok: boolean;
  exitCode?: number;
  events: XbloxRuntimeEvent[];
};

export type XbloxRuntimeDocument = {
  version: 1;
  context?: XbloxRuntimeContext;
  roots: unknown[];
};

export type XbloxContextStore = {
  snapshot(): XbloxRuntimeContext;
  get(name: string): unknown;
  set(name: string, value: unknown): void;
};

export type XbloxRuntime = {
  context: XbloxContextStore;
  evalExpression(expression: string): number | undefined;
  runCommand(command: CustomCommandItem, path?: string): Promise<XbloxRuntimeEvent>;
  log(level: string | undefined, message: string, path?: string): XbloxRuntimeEvent;
  wait(ms: number): Promise<void>;
  emit(event: XbloxRuntimeEvent): void;
  events(): XbloxRuntimeEvent[];
};

export type XbloxDocumentRuntime = XbloxRuntime & {
  runDocument(document: XbloxRuntimeDocument): Promise<XbloxRuntimeRunResult>;
};

function cloneContext(context?: XbloxRuntimeContext): XbloxRuntimeContext {
  try {
    return JSON.parse(JSON.stringify(context || {})) as XbloxRuntimeContext;
  } catch {
    return { ...(context || {}) };
  }
}

function pathParts(name: string) {
  return name.replace(/^this\./, "").split(".").filter(Boolean);
}

export function createXbloxContextStore(initial?: XbloxRuntimeContext): XbloxContextStore {
  const context = cloneContext(initial);
  return {
    snapshot: () => cloneContext(context),
    get(name) {
      let current: unknown = context;
      for (const part of pathParts(name)) {
        if (!current || typeof current !== "object" || !(part in current)) return undefined;
        current = (current as Record<string, unknown>)[part];
      }
      return current;
    },
    set(name, value) {
      const parts = pathParts(name);
      if (!parts.length) return;
      let current: Record<string, unknown> = context;
      for (let index = 0; index + 1 < parts.length; index += 1) {
        const part = parts[index];
        if (!current[part] || typeof current[part] !== "object") current[part] = {};
        current = current[part] as Record<string, unknown>;
      }
      current[parts[parts.length - 1]] = value;
    },
  };
}

export function evaluateXbloxExpression(expression: string, context: XbloxContextStore): number | undefined {
  const normalized = expression
    .replace(/\bthis\./g, "")
    .replace(/\bhost\.hasQueuedWork\(\)/g, "hasQueuedWork")
    .replace(/\bhasQueuedWork\(\)/g, "hasQueuedWork")
    .replace(/\(selection\?\.files\?\.length \?\? 0\)/g, "selectionCount")
    .replace(/\bselection\?\.files\?\.length \?\? 0\b/g, "selectionCount")
    .replace(/\bselection\.files\.length\b/g, "selectionCount")
    .replace(/\b[A-Za-z_][\w.]*/g, (name) => {
      if (name === "nowMs") return String(performance.now());
      const value = context.get(name);
      return typeof value === "number" || typeof value === "boolean" ? String(Number(value)) : name;
    });
  try {
    if (!/^[\d\s.+\-*/%()<>!=&|]+$/.test(normalized)) return undefined;
    return Function(`"use strict"; return Number(${normalized});`)() as number;
  } catch {
    return undefined;
  }
}

function writeConsoleLog(level: string | undefined, message: string) {
  console.log("writeConsoleLog", level, message);
  const normalized = (level || "trace").toLowerCase();
  if (normalized === "error" || normalized === "critical") console.error(message);
  else if (normalized === "warn" || normalized === "warning") console.warn(message);
  else if (normalized === "info") console.info(message);
  else if (normalized === "debug") console.debug(message);
  else console.trace(message);
}

export function createMemoryXbloxRuntime(options: {
  context?: XbloxRuntimeContext;
  onEvent?: (event: XbloxRuntimeEvent) => void;
  runCommand?: (command: CustomCommandItem, path?: string) => Promise<XbloxRuntimeEvent> | XbloxRuntimeEvent;
} = {}): XbloxRuntime {
  const context = createXbloxContextStore(options.context);
  const eventLog: XbloxRuntimeEvent[] = [];
  const runtime: XbloxRuntime = {
    context,
    evalExpression: (expression) => evaluateXbloxExpression(expression, context),
    async runCommand(command, path) {
      if (options.runCommand) return options.runCommand(command, path);
      return {
        path,
        kind: "command",
        status: "ok",
        message: command.label || command.id || "Mock command executed.",
        data: { id: command.id, label: command.label, command },
      };
    },
    log(level, message, path) {
      const resolvedLevel = level || "trace";
      writeConsoleLog(resolvedLevel, `[xblox] ${message}`);
      const event: XbloxRuntimeEvent = {
        path,
        kind: "log",
        status: "ok",
        message,
        data: { level: resolvedLevel, message },
      };
      runtime.emit(event);
      return event;
    },
    wait(ms) {
      return new Promise((resolve) => window.setTimeout(resolve, Math.max(0, ms)));
    },
    emit(event) {
      eventLog.push(event);
      options.onEvent?.(event);
    },
    events: () => [...eventLog],
  };
  return runtime;
}

export function createHostBridgeXbloxRuntime(callHost: <T = unknown>(request: ProviderRpcRequest) => Promise<ProviderRpcResult<T>>): XbloxDocumentRuntime {
  const runtime = createMemoryXbloxRuntime();
  return {
    ...runtime,
    async runCommand(command, path) {
      const result = await callHost<XbloxRunPayload>({ method: "xbloxCommandRun", command, path });
      const event = result.ok
        ? { path, kind: "command", status: "ok" as const, data: result.data, message: "Command completed." }
        : { path, kind: "command", status: "error" as const, message: result.error || "Command failed." };
      runtime.emit(event);
      return event;
    },
    async runDocument(document) {
      const result = await callHost<XbloxRunPayload>({ method: "xbloxDocumentRun", document });
      if (!result.ok) {
        return { ok: false, exitCode: 1, events: [{ status: "error", message: result.error || "Run failed." }] };
      }
      return {
        ok: result.data?.ok !== false,
        exitCode: result.data?.exitCode,
        events: (result.data?.events || []) as XbloxRuntimeEvent[],
      };
    },
  };
}
