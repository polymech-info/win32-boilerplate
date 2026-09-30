import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import type { BlockNode, BlocksFile } from "@/schema/blocks-file";
import {
  createHostBridgeXbloxRuntime,
  createMemoryXbloxRuntime,
  type XbloxRuntimeContext,
  type XbloxRuntimeEvent,
} from "@pm/shared/xblox/runtime";
import type { CustomCommandItem } from "@pm/shared/customCommands/types";
import {
  attachXbloxHostBridge,
  callXbloxHost,
  flattenCommandDocument,
  hasXbloxHost,
  type XbloxCommandsPayload,
  type XbloxDocumentPayload,
} from "@/xblox/hostBridge";
import {
  blockRunDelay,
  blockShouldFail,
  buildExecutionPlan,
  type ExecutableBlock,
} from "@/xblox/prototype/execution";
import { DebugPanels } from "@/xblox/prototype/DebugPanels";
import { CommandStrip } from "@/xblox/prototype/CommandStrip";
import { RUN_STATE_CLEAR_DELAY_MS, STORAGE_KEY, THEME_STORAGE_KEY } from "@/xblox/prototype/constants";
import { installXbloxMockHostBridge } from "@/xblox/prototype/mockHostBridge";
import { sampleBlocks, sampleCommands } from "@/xblox/prototype/sample-data";
import { nextPrototypeTheme, type PrototypeTheme } from "@/xblox/prototype/theme";
import {
  XbloxBuilder,
  XbloxDndProvider,
  getBlockAtPath,
  type BlockRunState,
  type NativePaletteBlock,
} from "@/xblox/react";
import type { CommandPaletteBlock } from "@/xblox/react/xblox-builder";

const MOCK_HOST_INSTALLED = installXbloxMockHostBridge();
const HOSTED_BY_NATIVE = hasXbloxHost();

function safeLocalStorageGet(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function safeLocalStorageSet(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    /* localStorage can be unavailable in some embedded dev contexts. */
  }
}

function readInitialBlocks(): BlockNode[] {
  if (HOSTED_BY_NATIVE) return [];
  const saved = safeLocalStorageGet(STORAGE_KEY);
  if (!saved) return sampleBlocks;
  try {
    return JSON.parse(saved) as BlockNode[];
  } catch {
    return sampleBlocks;
  }
}

function readInitialTheme(): PrototypeTheme {
  return safeLocalStorageGet(THEME_STORAGE_KEY) === "light" ? "light" : "dark";
}

function commandItemsFromPayload(payload?: XbloxCommandsPayload): CustomCommandItem[] {
  const fromDocument = flattenCommandDocument(payload?.document);
  if (fromDocument.length) return fromDocument;
  if (payload?.customCommands?.length) return payload.customCommands;
  return sampleCommands;
}

function makeBlocksFile(blocks: BlockNode[], context: Record<string, unknown>): BlocksFile {
  return { version: 1, context, roots: blocks };
}

function objectRecord(value: unknown): Record<string, unknown> | null {
  return value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : null;
}

function readBlockStoreName(block: unknown): string {
  const record = objectRecord(block);
  if (!record) return "";
  for (const key of ["storeAs", "storeVariable", "storeResult"]) {
    if (typeof record[key] === "string" && record[key]) return record[key] as string;
  }
  const runFlags = objectRecord(record.runFlags);
  if (runFlags) {
    for (const key of ["storeAs", "storeVariable"]) {
      if (typeof runFlags[key] === "string" && runFlags[key]) return runFlags[key] as string;
    }
  }
  return "";
}

function eventDataRecord(event: XbloxRuntimeEvent): Record<string, unknown> {
  return objectRecord(event.data) ?? {};
}

function setScopeValue(scope: Record<string, unknown>, name: string, value: unknown): Record<string, unknown> {
  const parts = name.split(".").map((part) => part.trim()).filter(Boolean);
  if (!parts.length) return scope;
  const next: Record<string, unknown> = { ...scope };
  let cursor = next;
  for (let i = 0; i < parts.length - 1; i += 1) {
    const part = parts[i];
    const child = objectRecord(cursor[part]);
    const cloned = child ? { ...child } : {};
    cursor[part] = cloned;
    cursor = cloned;
  }
  cursor[parts[parts.length - 1]] = value;
  return next;
}

function mapRuntimePath(path: string | undefined, pathMap: Record<string, string>): string | undefined {
  if (!path) return path;
  if (pathMap[path]) return pathMap[path];
  const key = Object.keys(pathMap)
    .filter((candidate) => path.startsWith(`${candidate}/`))
    .sort((a, b) => b.length - a.length)[0];
  return key ? `${pathMap[key]}${path.slice(key.length)}` : path;
}

function defaultRuntimeContext(context: Record<string, unknown>): XbloxRuntimeContext {
  if (Object.keys(context).length) return context;
  return {
    source: "xblox-demo",
    selection: { files: ["demo/input.png"] },
    selectionCount: 1,
    mode: "capture",
    hasQueuedWork: true,
  };
}

function commandActionLabel(command: CustomCommandItem) {
  if (command.cliCommand) return `cli:${command.cliCommand}`;
  if (command.appCommand) return `app:${command.appCommand}`;
  if (command.ribbonCommand) return `ribbon:${command.ribbonCommand}`;
  if (command.externalCommand) return "external";
  if (command.url) return "url";
  if (command.path) return "path";
  return "metadata";
}

function commandToRunScriptBlock(command: CustomCommandItem): BlockNode {
  return {
    kind: "command",
    id: command.id ?? command.label ?? "custom-command",
    command,
  };
}

function blockContinuesOnError(block: unknown): boolean {
  const record = objectRecord(block);
  const kind = typeof record?.kind === "string" ? record.kind : "";
  const runFlags = objectRecord(record?.runFlags);
  const value = runFlags?.continueOnError ?? record?.continueOnError;
  if (typeof value === "boolean") return value;
  return kind === "command" || kind === "shell" || kind === "Shell";
}

function blocksContainBackground(blocks: readonly unknown[]): boolean {
  for (const block of blocks) {
    const record = objectRecord(block);
    if (!record) continue;
    const runFlags = objectRecord(record.runFlags);
    if (record.background === true || runFlags?.background === true) return true;
    for (const key of ["items", "consequent", "alternate", "children"]) {
      const childBlocks = record[key];
      if (Array.isArray(childBlocks) && blocksContainBackground(childBlocks)) return true;
    }
  }
  return false;
}

function nativeBlocksFromPayload(payload?: XbloxCommandsPayload): NativePaletteBlock[] {
  const raw = payload?.nativeBlocks;
  console.log("[xblox] host nativeBlocks raw", raw);
  if (!Array.isArray(raw)) return [];
  const blocks = raw.flatMap((item) => {
    const record = objectRecord(item);
    const block = objectRecord(record?.block);
    const kind = typeof record?.kind === "string" ? record.kind : typeof block?.kind === "string" ? block.kind : "";
    if (!kind || !block) {
      console.log("[xblox] skipped native block descriptor", item);
    }
    if (!kind || !block) return [];
    return [{
      kind,
      label: typeof record?.label === "string" ? record.label : kind,
      description: typeof record?.description === "string" ? record.description : undefined,
      group: typeof record?.group === "string" ? record.group : "Native",
      params: Array.isArray(record?.params) ? record.params : undefined,
      block: block as BlockNode,
    }];
  });
  console.log("[xblox] native palette parsed", blocks.map((block) => ({ kind: block.kind, group: block.group, label: block.label })));
  return blocks;
}

function useHostPreferredTheme(enabled: boolean, setTheme: (theme: PrototypeTheme) => void) {
  useEffect(() => {
    if (!enabled || typeof window === "undefined" || !window.matchMedia) return;
    const query = window.matchMedia("(prefers-color-scheme: dark)");
    const apply = () => setTheme(query.matches ? "dark" : "light");
    apply();
    query.addEventListener?.("change", apply);
    return () => query.removeEventListener?.("change", apply);
  }, [enabled, setTheme]);
}

function applyHostFontMetrics(fontSizePx?: number, fontExtraPt?: number) {
  const resolvedPx = Number.isFinite(fontSizePx)
    ? fontSizePx
    : Number.isFinite(fontExtraPt)
      ? ((9 + (fontExtraPt ?? 0)) * 96) / 72
      : undefined;
  if (!resolvedPx) return;
  document.documentElement.style.setProperty("--pm-cweb-font-size", `${resolvedPx}px`);
  document.documentElement.style.setProperty("--pm-cweb-font-family", `"Segoe UI", system-ui, sans-serif`);
}

export function PrototypeApp() {
  const [hostedByNative, setHostedByNative] = useState(HOSTED_BY_NATIVE);
  const [blocks, setBlocks] = useState<BlockNode[]>(readInitialBlocks);
  const [context, setContext] = useState<Record<string, unknown>>({});
  const [filePath, setFilePath] = useState("");
  const [commands, setCommands] = useState<CustomCommandItem[]>(HOSTED_BY_NATIVE ? [] : sampleCommands);
  const [nativePalette, setNativePalette] = useState<NativePaletteBlock[]>([]);
  const [theme, setTheme] = useState<PrototypeTheme>(readInitialTheme);
  const [runState, setRunState] = useState<Record<string, BlockRunState>>({});
  const [selectedPath, setSelectedPath] = useState<string | null>(blocks.length ? "0" : null);
  const [eventLog, setEventLog] = useState(
    HOSTED_BY_NATIVE
      ? MOCK_HOST_INSTALLED
        ? "Mock native XBlox host ready."
        : "Waiting for native XBlox host."
      : "Standalone demo ready with sample custom commands.",
  );
  const [hostStatus, setHostStatus] = useState(HOSTED_BY_NATIVE ? "Loading .xblox file..." : "");
  const [dirty, setDirty] = useState(false);
  const [running, setRunning] = useState(false);
  const clearRunStateTimer = useRef<number | null>(null);
  const activeRunPathMap = useRef<Record<string, string>>({});
  const blocksRef = useRef<BlockNode[]>(blocks);
  const stopRequestedRef = useRef(false);
  useHostPreferredTheme(hostedByNative, setTheme);

  useEffect(() => {
    blocksRef.current = blocks;
  }, [blocks]);

  useEffect(() => {
    if (!hostedByNative) safeLocalStorageSet(STORAGE_KEY, JSON.stringify(blocks));
  }, [blocks, hostedByNative]);

  useEffect(() => {
    safeLocalStorageSet(THEME_STORAGE_KEY, theme);
  }, [theme]);

  useEffect(
    () => () => {
      if (clearRunStateTimer.current != null) window.clearTimeout(clearRunStateTimer.current);
    },
    [],
  );

  useEffect(() => {
    if (hostedByNative) return;
    const deadline = Date.now() + 2_000;
    const timer = window.setInterval(() => {
      if (hasXbloxHost()) {
        setHostedByNative(true);
        setCommands([]);
        setNativePalette([]);
        setEventLog("Native XBlox host detected.");
        setHostStatus("Loading .xblox file...");
        window.clearInterval(timer);
      } else if (Date.now() > deadline) {
        window.clearInterval(timer);
      }
    }, 50);
    return () => window.clearInterval(timer);
  }, [hostedByNative]);

  const blocksFile = useMemo(() => makeBlocksFile(blocks, context), [blocks, context]);
  const blocksJson = useMemo(() => JSON.stringify(blocksFile, null, 2), [blocksFile]);
  const hostRuntime = useMemo(() => createHostBridgeXbloxRuntime(callXbloxHost), []);
  const commandPalette = useMemo<CommandPaletteBlock[]>(
    () =>
      commands
        .filter((command) => command.type !== "separator" && command.visible !== false && command.enabled !== false)
        .map((command) => ({
          id: command.id ?? command.label ?? "custom-command",
          label: command.label ?? command.id ?? "Custom command",
          description: commandActionLabel(command),
          block: commandToRunScriptBlock(command),
        })),
    [commands],
  );

  const loadFromHost = useCallback(async (pathHint?: string) => {
    if (!hostedByNative) return;
    setHostStatus(pathHint ? `Loading ${pathHint}` : "Loading .xblox file...");
    const result = await callXbloxHost<XbloxDocumentPayload>({ method: "xbloxDocumentGet" });
    if (!result.ok) {
      setHostStatus(result.error ?? "Failed to load .xblox file.");
      setEventLog(result.error ?? "Failed to load .xblox file.");
      return;
    }

    const payload = result.data;
    console.log("[xblox] xbloxDocumentGet payload", payload);
    const document = payload?.document;
    const nextBlocks = Array.isArray(document?.roots) ? document.roots : [];
    setBlocks(nextBlocks);
    setContext(document?.context && typeof document.context === "object" ? document.context : {});
    setFilePath(payload?.path ?? pathHint ?? "");
    setCommands(commandItemsFromPayload(payload?.commands));
    const hostNativePalette = nativeBlocksFromPayload(payload?.commands);
    setNativePalette(hostNativePalette);
    if (!hostNativePalette.length) {
      console.log("[xblox] no host native palette; using playground/default palette");
    }
    setSelectedPath(nextBlocks.length ? "0" : null);
    setRunState({});
    setDirty(false);
    setHostStatus(payload?.path ? "Loaded from native preview host." : "New unsaved XBlox document.");
    setEventLog(`Loaded ${nextBlocks.length} root block${nextBlocks.length === 1 ? "" : "s"} from host.`);
  }, [hostedByNative]);

  const applyScopeEvent = useCallback((event: XbloxRuntimeEvent) => {
    if (event.status !== "ok") return;
    const data = eventDataRecord(event);
    setContext((current) => {
      let next = current;
      if (event.kind === "setVariable" && typeof data.name === "string") {
        next = setScopeValue(next, data.name, data.value);
      }
      if (event.kind === "getVariable" && typeof data.target === "string" && data.target) {
        next = setScopeValue(next, data.target, data.value);
      }
      const block = event.path ? objectRecord(getBlockAtPath(blocksRef.current, event.path)) : null;
      const storeName = readBlockStoreName(block);
      if (storeName) {
        const value = "result" in data ? data.result : data;
        next = setScopeValue(next, storeName, value);
      }
      return next;
    });
  }, []);

  const applyHostRunEvent = useCallback((event: unknown) => {
    const rawEvent = event as XbloxRuntimeEvent | undefined;
    const xbloxEvent = rawEvent ? { ...rawEvent, path: mapRuntimePath(rawEvent.path, activeRunPathMap.current) } : undefined;
    if (!xbloxEvent?.path) return;
    const status = xbloxEvent.status === "ok"
      ? "ok"
      : xbloxEvent.status === "skipped"
        ? "skipped"
        : xbloxEvent.status === "running"
          ? "running"
          : "error";
    setRunState((current) => {
      const nextState: BlockRunState = status === "ok" || status === "running"
        ? { status }
        : { status, error: xbloxEvent.message || xbloxEvent.kind || "XBlox host event" };
      const next = {
        ...current,
        [xbloxEvent.path as string]: nextState,
      };
      if (status === "running") setRunning(true);
      else if (!Object.values(next).some((state) => state.status === "running")) window.setTimeout(() => setRunning(false), 0);
      return next;
    });
    applyScopeEvent(xbloxEvent);
    setEventLog(JSON.stringify(xbloxEvent, null, 2));
  }, [applyScopeEvent]);

  useEffect(() => {
    if (!hostedByNative) return;
    const detach = attachXbloxHostBridge({
      onHostXblox: ({ path, fontExtraPt, fontSizePx }) => {
        applyHostFontMetrics(fontSizePx, fontExtraPt);
        void loadFromHost(path);
      },
      onHostXbloxRunEvent: ({ event }) => applyHostRunEvent(event),
    });
    return detach;
  }, [applyHostRunEvent, hostedByNative, loadFromHost]);

  useEffect(() => {
    if (!hostedByNative) return;
    void loadFromHost();
  }, [hostedByNative, loadFromHost]);

  const clearRunStateSoon = useCallback(() => {
    if (clearRunStateTimer.current != null) window.clearTimeout(clearRunStateTimer.current);
    clearRunStateTimer.current = window.setTimeout(() => {
      setRunState({});
      clearRunStateTimer.current = null;
    }, RUN_STATE_CLEAR_DELAY_MS);
  }, []);

  const stopRun = useCallback(() => {
    stopRequestedRef.current = true;
    if (hostedByNative) void callXbloxHost({ method: "xbloxDocumentStop" });
    setRunning(false);
    setHostStatus("Run stop requested.");
    setEventLog("Run stop requested.");
  }, [hostedByNative]);

  const updateBlocks = useCallback((next: BlockNode[] | ((current: BlockNode[]) => BlockNode[])) => {
    setBlocks((current) => (typeof next === "function" ? next(current) : next));
    setDirty(hostedByNative);
  }, [hostedByNative]);

  const updateRootScope = useCallback((next: Record<string, unknown>) => {
    setContext(next);
    setDirty(hostedByNative);
  }, [hostedByNative]);

  const saveDocument = useCallback(async () => {
    if (!hostedByNative) {
      safeLocalStorageSet(STORAGE_KEY, JSON.stringify(blocks));
      setEventLog("Standalone blocks saved to localStorage.");
      return;
    }
    const result = await callXbloxHost<{ path?: string }>({
      method: "xbloxDocumentSave",
      path: filePath,
      document: blocksFile,
    });
    if (!result.ok) {
      setHostStatus(result.error ?? "Save failed.");
      setEventLog(result.error ?? "Save failed.");
      return;
    }
    setFilePath(result.data?.path ?? filePath);
    setDirty(false);
    setHostStatus("Saved.");
    setEventLog(`Saved ${blocks.length} root block${blocks.length === 1 ? "" : "s"}.`);
  }, [blocks, blocksFile, filePath, hostedByNative]);

  const runHostDocument = useCallback(async () => {
    activeRunPathMap.current = {};
    stopRequestedRef.current = false;
    const hasBackground = blocksContainBackground(blocks);
    setRunning(true);
    try {
      const result = await hostRuntime.runDocument(blocksFile);
      const events = result.events ?? [];
      if (events.length > 0) {
        const nextRunState: Record<string, BlockRunState> = {};
        for (const event of events) {
          if (!event.path) continue;
          const status = event.status === "ok" ? "ok" : event.status === "skipped" ? "skipped" : "error";
          nextRunState[event.path] = status === "ok" ? { status } : { status, error: event.message || event.kind || "XBlox host event" };
        }
        setRunState(nextRunState);
      }
      setHostStatus(result.ok === false ? `Run failed (${result.exitCode ?? 1}).` : hasBackground ? "Background run started." : "Run completed.");
      setEventLog(JSON.stringify(result, null, 2));
      if (!hasBackground) clearRunStateSoon();
    } finally {
      if (!hasBackground) setRunning(false);
    }
  }, [blocks, blocksFile, clearRunStateSoon, hostRuntime]);

  const runCurrentChain = useCallback(async (chainBlocks: BlockNode[], listPath: string) => {
    const pathMap: Record<string, string> = {};
    chainBlocks.forEach((_, index) => {
      pathMap[String(index)] = listPath ? `${listPath}/${index}` : String(index);
    });
    activeRunPathMap.current = pathMap;
    stopRequestedRef.current = false;
    const hasBackground = blocksContainBackground(chainBlocks);
    setRunning(true);
    if (!hostedByNative) {
      try {
        setRunState((current) => {
          const next = { ...current };
          for (const path of Object.values(pathMap)) next[path] = { status: stopRequestedRef.current ? "skipped" : "ok" };
          return next;
        });
        setEventLog(stopRequestedRef.current ? "Run stopped." : `Completed ${chainBlocks.length} block${chainBlocks.length === 1 ? "" : "s"} in current chain.`);
        clearRunStateSoon();
      } finally {
        setRunning(false);
      }
      return;
    }
    try {
      const chainDocument = makeBlocksFile(chainBlocks, defaultRuntimeContext(context));
      const result = await hostRuntime.runDocument(chainDocument);
      setHostStatus(result.ok === false ? `Chain failed (${result.exitCode ?? 1}).` : hasBackground ? "Background chain started." : "Chain run completed.");
      setEventLog(JSON.stringify(result, null, 2));
      if (!hasBackground) clearRunStateSoon();
    } finally {
      if (!hasBackground) setRunning(false);
    }
  }, [clearRunStateSoon, context, hostRuntime, hostedByNative]);

  const runBlockSequence = useCallback(
    async (_block: ExecutableBlock, path: string) => {
      if (hostedByNative) {
        const selectedDocument = makeBlocksFile([_block as BlockNode], defaultRuntimeContext(context));
        const hasBackground = blocksContainBackground([_block]);
        activeRunPathMap.current = { "0": path };
        stopRequestedRef.current = false;
        setRunning(true);
        setSelectedPath(path);
        setRunState((current) => ({ ...current, [path]: { status: "running" } }));
        try {
          const result = await hostRuntime.runDocument(selectedDocument);
          if (!hasBackground || result.ok === false) {
            setRunState((current) => ({ ...current, [path]: { status: result.ok === false ? "error" : "ok", error: result.ok === false ? "Selected block run failed." : undefined } }));
          }
          setHostStatus(result.ok === false ? `Run failed (${result.exitCode ?? 1}).` : hasBackground ? "Background block started." : "Selected block run completed.");
          setEventLog(JSON.stringify(result, null, 2));
          if (!hasBackground) clearRunStateSoon();
        } finally {
          if (!hasBackground) setRunning(false);
        }
        return;
      }
      stopRequestedRef.current = false;
      setRunning(true);
      if (clearRunStateTimer.current != null) window.clearTimeout(clearRunStateTimer.current);
      clearRunStateTimer.current = null;

      const runtimeContext = defaultRuntimeContext(context);
      const plan = buildExecutionPlan(blocks, runtimeContext);
      const runnable = plan.filter((step) => step.path === path || step.path.startsWith(`${path}/`));
      if (!runnable.length) runnable.push({ path, block: _block });
      const runPaths = new Set(runnable.map((step) => step.path));
      const runtime = createMemoryXbloxRuntime({ context: runtimeContext });

      setRunState((current) => {
        const next = { ...current };
        for (const step of runnable) delete next[step.path];
        return next;
      });

      try {
      for (let index = 0; index < runnable.length; index += 1) {
        if (stopRequestedRef.current) {
          setEventLog("Run stopped.");
          clearRunStateSoon();
          return;
        }
        const step = runnable[index];
        setSelectedPath(step.path);
        setRunState((current) => ({ ...current, [step.path]: { status: "running" } }));
        setEventLog(`Running ${step.block.kind} at ${step.path} (${index + 1}/${runnable.length}).`);
        await runtime.wait(blockRunDelay(step.block));

        if (blockShouldFail(step.block)) {
          if (blockContinuesOnError(step.block)) {
            setRunState((current) => ({
              ...current,
              [step.path]: { status: "error", error: "Demo host block failed; continuing because Continue on Error is enabled." },
            }));
            setEventLog(`Error at ${step.path}; continuing.`);
            continue;
          }
          const skipped = runnable.slice(index + 1);
          setRunState((current) => {
            const next: Record<string, BlockRunState> = {
              ...current,
              [step.path]: { status: "error", error: "Demo host stopped this chain because the block failed." },
            };
            for (const skippedStep of skipped) {
              if (!runPaths.has(skippedStep.path)) continue;
              next[skippedStep.path] = {
                status: "skipped",
                error: `Skipped because ${step.path} failed.`,
              };
            }
            return next;
          });
          setEventLog(`Error at ${step.path}; skipped ${skipped.length} subsequent block${skipped.length === 1 ? "" : "s"}.`);
          clearRunStateSoon();
          return;
        }

        if (step.block.kind === "command") {
          const event = await runtime.runCommand(step.block.command, step.path);
          if (event.status === "error") {
            setRunState((current) => ({ ...current, [step.path]: { status: "error", error: event.message || "Command failed." } }));
            setEventLog(event.message || "Command failed.");
            if (!blockContinuesOnError(step.block)) {
              clearRunStateSoon();
              return;
            }
            continue;
          }
        }

        if (step.block.kind === "log") {
          const evaluated = runtime.evalExpression(step.block.message);
          const message = evaluated == null ? step.block.message : String(evaluated);
          runtime.log(step.block.level, message, step.path);
        }

        setRunState((current) => ({ ...current, [step.path]: { status: "ok" } }));
      }

      setEventLog(`Completed ${runnable.length} block${runnable.length === 1 ? "" : "s"} from ${path}.`);
      clearRunStateSoon();
      } finally {
        setRunning(false);
      }
    },
    [blocks, clearRunStateSoon, context, hostRuntime, hostedByNative],
  );

  return (
    <XbloxDndProvider>
      <main className={`xblox-app xblox-theme-${theme} ${hostedByNative ? "xblox-app--host" : "xblox-app--standalone"}`}>
        {!hostedByNative ? <CommandStrip commands={commands} /> : null}

        <XbloxBuilder
          value={blocks}
          onChange={updateBlocks}
          rootScope={context}
          onRootScopeChange={updateRootScope}
          palette={nativePalette.length ? [] : undefined}
          nativePalette={nativePalette}
          selectedPath={selectedPath ?? undefined}
          onSelectionChange={(path) => setSelectedPath(path)}
          runState={runState}
          onRunBlock={runBlockSequence}
          onRunChain={runCurrentChain}
          onStopRun={stopRun}
          running={running}
          onSave={saveDocument}
          saveDisabled={hostedByNative && !dirty}
          theme={theme}
          onToggleTheme={() => setTheme((current) => nextPrototypeTheme(current))}
          commandPalette={commandPalette}
        />

        {!hostedByNative ? <DebugPanels eventLog={eventLog} blocksJson={blocksJson} /> : null}
      </main>
    </XbloxDndProvider>
  );
}
