import type { AppCommandOption, RibbonCommandOption } from "../customCommands/types";

export type ProviderRpcResult<T = unknown> = {
  ok: boolean;
  data?: T;
  error?: string;
};

export type AppCommandsGetReply = {
  commands: AppCommandOption[];
};

export type RibbonCommandsGetReply = {
  commands: RibbonCommandOption[];
};

export type PathToolCatalogTool = {
  name?: string;
  description?: string;
  input_schema?: unknown;
};

export type PathToolCatalogReply = {
  tools?: PathToolCatalogTool[];
  disabled?: boolean;
  note?: string;
};

export type PixlwizAuthPayload = {
  read_ok?: boolean;
  read_error?: string;
  logged_in?: boolean;
  oauth_file_present?: boolean;
  has_refresh_token?: boolean;
  app_user_id?: string;
  zitadel_sub?: string;
  access_token_expired_est?: boolean;
};

export type PixlwizCreditPayload = {
  ok?: boolean;
  spend?: number;
  max_budget?: number | null;
  budget_duration?: string;
  budget_reset_at?: string;
  error?: string;
};

export type ProviderRpcRequest = {
  method: string;
  [key: string]: unknown;
};

export type HostProviderRpcMessage = {
  kind: "hostProviderRpc";
  id: string;
  ok?: boolean;
  data?: unknown;
  error?: string;
};

export type HostThemeMessage = {
  t: "cweb_theme";
  theme?: unknown;
};

export type HostBusMessage = {
  t: "cweb_bus";
  [key: string]: unknown;
};

export type HostMessage = Record<string, unknown>;

export type PmBusPayload = Record<string, unknown>;

export type PmBusMessage = {
  from?: string;
  to?: string;
  payload: unknown;
  raw: HostBusMessage;
};

export type PmHostContext = {
  selection: string[];
  explorerSelection: string[];
  contextExtra: string[];
  folder: string;
  selectionBytes: number;
  workspace: string;
};

export type PmHostLogLevel = "log" | "info" | "warn" | "error" | "debug";

export type XbloxCommandsPayload<TCommand = unknown, TDocument = unknown> = {
  commandsPath?: string;
  document?: TDocument;
  registeredCommands?: Array<{ id?: string; label?: string; available?: boolean }>;
  customCommands?: TCommand[];
  nativeBlocks?: Array<{
    kind: string;
    label?: string;
    description?: string;
    group?: string;
    block?: unknown;
    params?: unknown;
    flags?: unknown;
    platformMask?: number;
  }>;
};

export type XbloxDocumentPayload<TBlocksFile = unknown, TCommand = unknown, TDocument = unknown> = {
  path?: string;
  document?: TBlocksFile | null;
  commands?: XbloxCommandsPayload<TCommand, TDocument>;
};

export type XbloxRunEvent = {
  path?: string;
  blockId?: string;
  kind?: string;
  type?: string;
  status?: string;
  message?: string;
  exitCode?: number;
  errorCode?: number;
  stdout?: string[];
  stderr?: string[];
  data?: unknown;
};

export type XbloxRunPayload = {
  ok?: boolean;
  exitCode?: number;
  events?: XbloxRunEvent[];
};

export type PmParsedArgs = {
  raw: string[];
  argv: string[];
  executable: string;
  cwd: string;
  args: string[];
  options: Record<string, unknown>;
};

export type PmHostApi = {
  readonly host: {
    readonly available: boolean;
    post: (message: Record<string, unknown>) => void;
    send: (kind: string, payload?: Record<string, unknown>) => void;
    rpc: <T = unknown>(request: ProviderRpcRequest) => Promise<ProviderRpcResult<T>>;
    onMessage: (listener: (message: HostMessage) => void) => () => void;
  };
  readonly bus: {
    send: (to: string | undefined, payload: PmBusPayload) => void;
    broadcast: (payload: PmBusPayload) => void;
    onMessage: (listener: (message: PmBusMessage) => void) => () => void;
  };
  readonly features: Record<string, unknown>;
  readonly systemContext: Record<string, unknown>;
  context: PmHostContext;
  args: (argv?: string[]) => PmParsedArgs;
  log: (level: PmHostLogLevel, ...args: unknown[]) => void;
  setStatus: (status: HostMessage) => void;
  onContextChange: (listener: (context: PmHostContext, status: HostMessage) => void) => () => void;
  addContextPaths: (paths: string[]) => void;
  removeContextPaths: (paths: string[]) => void;
  pickContextPaths: (mode?: "files" | "folder") => void;
  openPathDefault: (path: string) => void;
  openPathInternal: (path: string) => void;
  openFolderInExplorer: (path: string) => void;
  selectPathInExplorer: (path: string) => void;
  chat?: Record<string, unknown>;
  [key: string]: unknown;
};

declare global {
  interface Window {
    APP_FEATURES?: Record<string, boolean>;
    SYSTEM_CONTEXT?: Record<string, unknown>;
    pm?: PmHostApi;
  }
}

type WebviewWindow = Window & {
  chrome?: {
    webview?: {
      postMessage?: (message: string) => void;
      addEventListener?: typeof window.addEventListener;
      removeEventListener?: typeof window.removeEventListener;
    };
  };
  cwWebOnHostMessage?: (message: unknown) => void;
};

const emptyContext = (): PmHostContext => ({
  selection: [],
  explorerSelection: [],
  contextExtra: [],
  folder: "",
  selectionBytes: 0,
  workspace: "",
});

let installedPmApi: PmHostApi | null = null;

type PendingRpc = {
  method: string;
  resolve: (value: ProviderRpcResult) => void;
};

export function hasWebviewHost(): boolean {
  return typeof window !== "undefined" && typeof (window as WebviewWindow).chrome?.webview?.postMessage === "function";
}

export function postToHost(message: Record<string, unknown>): void {
  const webview = typeof window !== "undefined" ? (window as WebviewWindow).chrome?.webview : undefined;
  if (webview?.postMessage) {
    webview.postMessage(JSON.stringify(message));
  }
}

function readWindowRecord(name: "APP_FEATURES" | "SYSTEM_CONTEXT"): Record<string, unknown> {
  if (typeof window === "undefined") return {};
  const raw = window[name];
  return raw && typeof raw === "object" && !Array.isArray(raw) ? raw : {};
}

function stringArray(value: unknown): string[] {
  return Array.isArray(value) ? value.filter((item): item is string => typeof item === "string") : [];
}

function cloneContext(context: PmHostContext): PmHostContext {
  return {
    selection: [...context.selection],
    explorerSelection: [...context.explorerSelection],
    contextExtra: [...context.contextExtra],
    folder: context.folder,
    selectionBytes: context.selectionBytes,
    workspace: context.workspace,
  };
}

function applyStatusToContext(context: PmHostContext, status: HostMessage): PmHostContext {
  const next = cloneContext(context);
  if ("selection" in status) next.selection = stringArray(status.selection);
  if ("explorer_selection" in status) next.explorerSelection = stringArray(status.explorer_selection);
  if ("context_extra" in status) next.contextExtra = stringArray(status.context_extra);
  if (typeof status.folder === "string") next.folder = status.folder;
  const selectionBytes = status.selection_bytes ?? status.selectionBytes;
  if (typeof selectionBytes === "number" && Number.isFinite(selectionBytes)) next.selectionBytes = selectionBytes;
  if (typeof status.workspace === "string") next.workspace = status.workspace;
  return next;
}

function readSystemLaunch(systemContext: Record<string, unknown>): { raw: string[]; argv: string[]; executable: string; cwd: string } {
  const launch = systemContext.launch;
  const launchRecord = launch && typeof launch === "object" && !Array.isArray(launch) ? launch as Record<string, unknown> : {};
  const raw = stringArray(launchRecord.arguments);
  const argv = stringArray(launchRecord.argv);
  return {
    raw,
    argv,
    executable: typeof launchRecord.executable === "string" ? launchRecord.executable : "",
    cwd: typeof launchRecord.cwd === "string" ? launchRecord.cwd : "",
  };
}

function setParsedOption(options: Record<string, unknown>, key: string, value: unknown): void {
  const old = options[key];
  if (old === undefined) {
    options[key] = value;
  } else if (Array.isArray(old)) {
    old.push(value);
  } else {
    options[key] = [old, value];
  }
}

function optionValue(raw: string): string | number {
  const n = Number(raw);
  return raw.trim() !== "" && Number.isFinite(n) ? n : raw;
}

function parsePmCliArgs(raw: string[]): { args: string[]; options: Record<string, unknown> } {
  const args: string[] = [];
  const options: Record<string, unknown> = { "--": [] };

  for (let i = 0; i < raw.length; ++i) {
    const arg = raw[i] ?? "";
    if (arg === "--") {
      options["--"] = raw.slice(i + 1);
      break;
    }
    if (!arg.startsWith("-") || arg === "-") {
      args.push(arg);
      continue;
    }
    if (arg.startsWith("--no-") && arg.length > 5) {
      setParsedOption(options, arg.slice(5), false);
      continue;
    }
    if (arg.startsWith("--")) {
      const eq = arg.indexOf("=", 2);
      const key = eq >= 0 ? arg.slice(2, eq) : arg.slice(2);
      if (!key) continue;
      if (eq >= 0) {
        setParsedOption(options, key, optionValue(arg.slice(eq + 1)));
        continue;
      }
      const next = raw[i + 1];
      if (next != null && !String(next).startsWith("-")) {
        i += 1;
        setParsedOption(options, key, optionValue(String(next)));
      } else {
        setParsedOption(options, key, true);
      }
      continue;
    }

    const short = arg.slice(1);
    const eq = short.indexOf("=");
    if (eq >= 0) {
      const key = short.slice(0, eq);
      if (key) setParsedOption(options, key, optionValue(short.slice(eq + 1)));
      continue;
    }
    if (short.length > 1) {
      for (const key of short) setParsedOption(options, key, true);
      continue;
    }
    const next = raw[i + 1];
    if (next != null && !String(next).startsWith("-")) {
      i += 1;
      setParsedOption(options, short, optionValue(String(next)));
    } else {
      setParsedOption(options, short, true);
    }
  }

  return { args, options };
}

function parsePmArgs(systemContext: Record<string, unknown>, overrideArgv?: string[]): PmParsedArgs {
  const launch = readSystemLaunch(systemContext);
  const raw = overrideArgv ? [...overrideArgv] : launch.raw;
  const parsed = parsePmCliArgs(raw);
  return {
    raw,
    argv: overrideArgv ? [launch.executable || "pm-image", ...raw] : launch.argv,
    executable: launch.executable,
    cwd: launch.cwd,
    args: parsed.args,
    options: parsed.options,
  };
}

export function installPolyMechHostApi(): PmHostApi {
  if (typeof window === "undefined") {
    if (installedPmApi) return installedPmApi;
    const api = {
      host: {
        available: false,
        post: () => {},
        send: () => {},
        rpc: async () => ({ ok: false, error: "no window" }),
        onMessage: () => () => {},
      },
      bus: {
        send: () => {},
        broadcast: () => {},
        onMessage: () => () => {},
      },
      features: {},
      systemContext: {},
      context: emptyContext(),
      args: () => parsePmArgs({}),
      log: () => {},
      setStatus: () => {},
      onContextChange: () => () => {},
      addContextPaths: () => {},
      removeContextPaths: () => {},
      pickContextPaths: () => {},
      openPathDefault: () => {},
      openPathInternal: () => {},
      openFolderInExplorer: () => {},
      selectPathInExplorer: () => {},
    } satisfies PmHostApi;
    installedPmApi = api;
    return api;
  }

  if (installedPmApi) return installedPmApi;
  if (window.pm) {
    installedPmApi = window.pm;
    return installedPmApi;
  }

  let context = emptyContext();
  const messageListeners = new Set<(message: HostMessage) => void>();
  const busListeners = new Set<(message: PmBusMessage) => void>();
  const contextListeners = new Set<(context: PmHostContext, status: HostMessage) => void>();
  const systemContext = readWindowRecord("SYSTEM_CONTEXT");
  let parsedLaunchArgs: PmParsedArgs | null = null;
  let api!: PmHostApi;
  const rpcClient = createProviderRpcClient({
    idPrefix: "pm",
    onLog: (event, detail) => {
      api.log("debug", `[pm.host] ${event}`, detail);
    },
  });

  const notifyContext = (status: HostMessage) => {
    const snapshot = cloneContext(context);
    contextListeners.forEach((listener) => listener(snapshot, status));
  };

  const postKind = (kind: string, payload: Record<string, unknown> = {}) => postToHost({ ...payload, kind });
  const postBus = (to: string | undefined, payload: PmBusPayload) => {
    const target = typeof to === "string" && to.trim() ? to.trim() : "*";
    postToHost({ ...payload, t: "cweb_bus", to: target });
  };

  api = {
    host: {
      get available() {
        return hasWebviewHost();
      },
      post: postToHost,
      send: postKind,
      rpc: (request) => rpcClient.call(request),
      onMessage: (listener) => {
        messageListeners.add(listener);
        return () => messageListeners.delete(listener);
      },
    },
    bus: {
      send: postBus,
      broadcast: (payload) => postBus("*", payload),
      onMessage: (listener) => {
        busListeners.add(listener);
        return () => busListeners.delete(listener);
      },
    },
    features: readWindowRecord("APP_FEATURES"),
    systemContext,
    context,
    args: (argv) => {
      if (argv) return parsePmArgs(systemContext, argv);
      parsedLaunchArgs ??= parsePmArgs(systemContext);
      return parsedLaunchArgs;
    },
    log: (level, ...args) => {
      const consoleMethod =
        level === "debug" ? "log" : level;
      const consoleLike = typeof console !== "undefined" ? console : undefined;
      const fn = consoleLike?.[consoleMethod as "log" | "info" | "warn" | "error"];
      // CWebView's injected console bridge forwards this to the native log.
      if (typeof fn === "function") fn.call(consoleLike, "[pm]", ...args);
    },
    setStatus: (status) => {
      context = applyStatusToContext(context, status);
      api.context = context;
      notifyContext(status);
    },
    onContextChange: (listener) => {
      contextListeners.add(listener);
      return () => contextListeners.delete(listener);
    },
    addContextPaths: (paths) => postKind("addContextPaths", { paths }),
    removeContextPaths: (paths) => postKind("removeContextPaths", { paths }),
    pickContextPaths: (mode = "files") => postKind("pickContextPaths", { mode }),
    openPathDefault: (path) => postKind("openPathDefault", { path }),
    openPathInternal: (path) => postKind("openPathInternal", { path }),
    openFolderInExplorer: (path) => postKind("openFolderInExplorer", { path }),
    selectPathInExplorer: (path) => postKind("selectPathInExplorer", { path }),
  };

  attachHostMessageListener({
    onMessage: (message) => {
      messageListeners.forEach((listener) => listener(message));
    },
    onProviderRpcReply: (message) => {
      if (String(message.id ?? "").startsWith("pm")) rpcClient.handleHostProviderRpc(message);
    },
    onBus: (message) => {
      const busMessage: PmBusMessage = {
        from: typeof message.from === "string" ? message.from : undefined,
        to: typeof message.to === "string" ? message.to : undefined,
        payload: "payload" in message ? message.payload : message,
        raw: message,
      };
      busListeners.forEach((listener) => listener(busMessage));
    },
  });

  window.pm = api;
  installedPmApi = api;
  return api;
}

export function parseHostMessage(raw: unknown): HostMessage | null {
  if (raw == null) return null;
  if (typeof raw === "object" && !Array.isArray(raw)) return raw as HostMessage;
  if (typeof raw !== "string") return null;
  try {
    const parsed = JSON.parse(raw) as unknown;
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? (parsed as HostMessage) : null;
  } catch {
    return null;
  }
}

export type ProviderRpcClientOptions = {
  idPrefix?: string;
  mock?: (request: ProviderRpcRequest) => Promise<ProviderRpcResult>;
  post?: (message: Record<string, unknown>) => void;
  hasHost?: () => boolean;
  onLog?: (event: string, detail?: unknown) => void;
};

export type ProviderRpcClient = {
  call: <T = unknown>(request: ProviderRpcRequest) => Promise<ProviderRpcResult<T>>;
  handleHostProviderRpc: (message: HostMessage) => boolean;
};

export function createProviderRpcClient(options: ProviderRpcClientOptions = {}): ProviderRpcClient {
  let seq = 0;
  const pending = new Map<string, PendingRpc>();
  const idPrefix = options.idPrefix ?? "r";
  const post = options.post ?? postToHost;
  const hasHost = options.hasHost ?? hasWebviewHost;

  function handleHostProviderRpc(message: HostMessage): boolean {
    if (message.kind !== "hostProviderRpc" || message.id == null) return false;
    const id = String(message.id);
    const req = pending.get(id);
    if (!req) {
      options.onLog?.("hostProviderRpc:missing-pending", { id });
      return true;
    }
    pending.delete(id);
    const result: ProviderRpcResult = {
      ok: message.ok === true,
      data: message.data,
      error: typeof message.error === "string" ? message.error : undefined,
    };
    options.onLog?.("providerRpc:reply", { id, method: req.method, ok: result.ok, error: result.error });
    req.resolve(result);
    return true;
  }

  async function call<T = unknown>(request: ProviderRpcRequest): Promise<ProviderRpcResult<T>> {
    const method = String(request.method || "");
    if (!hasHost()) {
      if (options.mock) return (await options.mock(request)) as ProviderRpcResult<T>;
      return { ok: false, error: "no WebView host" };
    }
    const rpcId = `${idPrefix}${++seq}`;
    options.onLog?.("providerRpc:request", { id: rpcId, method });
    return new Promise((resolve) => {
      pending.set(rpcId, { method, resolve: resolve as (value: ProviderRpcResult) => void });
      post({ kind: "providerRpc", rpcId, ...request });
    });
  }

  return { call, handleHostProviderRpc };
}

export type XbloxHostBridgeHandlers = {
  onHostXblox?: (payload: { path?: string; fontExtraPt?: number; fontSizePx?: number }) => void;
  onHostXbloxRunEvent?: (payload: { event?: unknown }) => void;
};

export function hasXbloxHost(): boolean {
  return hasWebviewHost();
}

export function createXbloxHostClient(options: Omit<ProviderRpcClientOptions, "idPrefix"> = {}): ProviderRpcClient {
  return createProviderRpcClient({ idPrefix: "xblox-", ...options });
}

export function callXbloxHost<T = unknown>(request: ProviderRpcRequest): Promise<ProviderRpcResult<T>> {
  const client = createXbloxHostClient();
  const detach = attachHostMessageListener({
    onProviderRpcReply: (message) => {
      if (String(message.id ?? "").startsWith("xblox-")) client.handleHostProviderRpc(message);
    },
  });
  return client.call<T>(request).finally(detach);
}

export function attachXbloxHostBridge(
  handlers: XbloxHostBridgeHandlers,
  client?: ProviderRpcClient,
): () => void {
  return attachHostMessageListener({
    onProviderRpcReply: (message) => {
      if (String(message.id ?? "").startsWith("xblox-")) client?.handleHostProviderRpc(message);
    },
    onMessage: (message) => {
      if (message.kind === "hostXblox") {
        handlers.onHostXblox?.({
          path: typeof message.path === "string" ? message.path : undefined,
          fontExtraPt: typeof message.fontExtraPt === "number" ? message.fontExtraPt : undefined,
          fontSizePx: typeof message.fontSizePx === "number" ? message.fontSizePx : undefined,
        });
      }
      if (message.kind === "hostXbloxRunEvent") {
        handlers.onHostXbloxRunEvent?.({ event: message.event });
      }
    },
  });
}

export type HostMessageHandlers = {
  onMessage?: (message: HostMessage) => void;
  onProviderRpcReply?: (message: HostProviderRpcMessage) => void;
  onTheme?: (theme: unknown, message: HostThemeMessage) => void;
  onBus?: (message: HostBusMessage) => void;
};

export function attachHostMessageListener(handlers: HostMessageHandlers): () => void {
  if (typeof window === "undefined") return () => {};
  const w = window as WebviewWindow;
  const webview = w.chrome?.webview;
  let lastSerialized = "";
  let lastAt = 0;

  const handle = (raw: unknown) => {
    const message = parseHostMessage(raw);
    if (!message) return;
    const serialized = JSON.stringify(message);
    const now = Date.now();
    if (serialized === lastSerialized && now - lastAt < 50) return;
    lastSerialized = serialized;
    lastAt = now;

    handlers.onMessage?.(message);
    if (message.kind === "hostProviderRpc" && message.id != null) {
      handlers.onProviderRpcReply?.(message as HostProviderRpcMessage);
      return;
    }
    if (message.t === "cweb_theme") {
      handlers.onTheme?.((message as HostThemeMessage).theme, message as HostThemeMessage);
      return;
    }
    if (message.t === "cweb_bus") {
      handlers.onBus?.(message as HostBusMessage);
    }
  };

  const listener = (event: MessageEvent) => handle(event?.data);
  webview?.addEventListener?.("message", listener as EventListener);

  const previous = w.cwWebOnHostMessage;
  w.cwWebOnHostMessage = (message: unknown) => {
    previous?.(message);
    handle(message);
  };

  return () => {
    webview?.removeEventListener?.("message", listener as EventListener);
    if (w.cwWebOnHostMessage) {
      w.cwWebOnHostMessage = previous;
    }
  };
}

export async function fetchSettingsAppCommands(
  rpc: (method: string, payload?: Record<string, unknown>) => Promise<unknown>,
): Promise<AppCommandOption[]> {
  try {
    const data = (await rpc("settingsAppCommandsGet")) as AppCommandsGetReply | undefined;
    const commands = Array.isArray(data?.commands) ? data.commands : [];
    if (commands.length) return commands;
  } catch {
    // Fall through to dev defaults when the host RPC is unavailable.
  }
  const { devAppCommandOptions } = await import("../customCommands/helpers");
  return devAppCommandOptions();
}

export async function fetchSettingsRibbonCommands(
  rpc: (method: string, payload?: Record<string, unknown>) => Promise<unknown>,
): Promise<RibbonCommandOption[]> {
  try {
    const data = (await rpc("settingsRibbonCommandsGet")) as RibbonCommandsGetReply | undefined;
    const commands = Array.isArray(data?.commands) ? data.commands : [];
    if (commands.length) return commands;
  } catch {
    // Fall through to dev defaults when the host RPC is unavailable.
  }
  const { devRibbonCommandOptions } = await import("../customCommands/helpers");
  return devRibbonCommandOptions();
}

export async function fetchHostPathToolIds(
  rpc: (method: string, payload?: Record<string, unknown>) => Promise<unknown>,
  fallback: readonly string[] = [],
): Promise<string[]> {
  try {
    const data = (await rpc("hostPathToolsCatalogGet")) as PathToolCatalogReply | undefined;
    const names = Array.isArray(data?.tools)
      ? data.tools
          .map((tool) => tool?.name)
          .filter((name): name is string => typeof name === "string" && name.trim().length > 0)
      : [];
    if (names.length) return names;
  } catch {
    // Fall through to caller-supplied dev/static fallback when the host is unavailable.
  }
  return [...fallback];
}

export function normalizePixlwizAuthPayload(value: Record<string, unknown>): PixlwizAuthPayload {
  return {
    read_ok: value.read_ok === true,
    read_error: typeof value.read_error === "string" ? value.read_error : undefined,
    logged_in: value.logged_in === true,
    oauth_file_present: value.oauth_file_present === true,
    has_refresh_token: value.has_refresh_token === true,
    app_user_id: typeof value.app_user_id === "string" ? value.app_user_id : undefined,
    zitadel_sub: typeof value.zitadel_sub === "string" ? value.zitadel_sub : undefined,
    access_token_expired_est: value.access_token_expired_est === true,
  };
}

export function normalizePixlwizCreditPayload(value: Record<string, unknown>): PixlwizCreditPayload {
  return {
    ok: value.ok === true,
    spend: typeof value.spend === "number" ? value.spend : undefined,
    max_budget: value.max_budget === null || value.max_budget === undefined
      ? null
      : typeof value.max_budget === "number" ? value.max_budget : null,
    budget_duration: typeof value.budget_duration === "string" ? value.budget_duration : undefined,
    budget_reset_at: typeof value.budget_reset_at === "string" ? value.budget_reset_at : undefined,
    error: typeof value.error === "string" ? value.error : undefined,
  };
}
