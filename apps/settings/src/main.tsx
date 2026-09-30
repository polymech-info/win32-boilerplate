import { StrictMode, useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";

import { ModelStringTypeahead } from "@pm/shared/components/ModelStringTypeahead";
import defaultCustomCommandsDoc from "@pm/shared/customCommands/commands.json";
import { devAppCommandOptions, devRibbonCommandOptions } from "@pm/shared/customCommands/helpers";
import { PIXLWIZ_IMAGE_MODELS, PIXLWIZ_IMAGE_RECOG_MODELS, PIXLWIZ_TEXT_MODELS, PIXLWIZ_VIDEO_MODELS } from "@pm/shared/providers/catalog";
import {
  fetchHostPathToolIds,
  fetchSettingsAppCommands,
  fetchSettingsRibbonCommands,
  normalizePixlwizAuthPayload,
  type PixlwizAuthPayload,
} from "@pm/shared/web/hostBridge";
import "@/styles.css";
import {
  CustomCommandsPanel,
  type AppCommandOption,
  type CliCommandOption,
  type RibbonCommandOption,
  type CliHelpSchema,
  type CommandVariableOption,
  type CustomCommandOption,
  type CustomCommandProviderDefaults,
  type CustomCommandProviderScope,
  type CustomCommandsDocument,
  type TablerIconOption,
} from "@/settings/customCommands/CustomCommandsPanel";
import { McpSettingsPanel } from "@/settings/mcp/McpSettingsPanel";
import { SkillsSettingsPanel } from "@/settings/skills/SkillsSettingsPanel";
import { normalizeSkillName, type SkillsSettings } from "@/settings/skills/types";
import { ToolsSettingsPanel } from "@/settings/tools/ToolsSettingsPanel";
import { BackupSettingsPanel } from "@/settings/backup/BackupSettingsPanel";

type ProviderRow = {
  id: string;
  displayName: string;
  apiKey: string;
  baseUrl: string;
  models: string[];
};

type HostRpcReply = {
  kind?: string;
  id?: string | number;
  ok?: boolean;
  error?: string;
  data?: unknown;
};

type ProvidersGetReply = {
  providers: ProviderRow[];
  settingsPath?: string;
  theme?: "light" | "dark";
  displayLanguage?: string;
};

type ChatSettings = {
  router: string;
  model: string;
  max_iterations: number;
  image_provider: string;
  image_model: string;
  image_recognition_provider: string;
  image_recognition_model: string;
  video_provider: string;
  video_model: string;
  stt_provider: string;
  stt_model: string;
  tts_provider: string;
  tts_model: string;
  tts_voice_id: string;
};

type ChatGetReply = {
  chat: ChatSettings;
  displayLanguage?: string;
};
type ChatPresetsGetReply = {
  presets: ChatPreset[];
};
type CustomCommandsGetReply = {
  feature_disabled?: boolean;
  commandsPath: string;
  document: CustomCommandsDocument;
  variables?: CommandVariableOption[];
};
type TablerIconsGetReply = {
  icons: TablerIconOption[];
};
type CliCommandsGetReply = {
  commands: CliCommandOption[];
};
type AppCommandsGetReply = {
  commands: AppCommandOption[];
};
type RibbonCommandsGetReply = {
  commands: RibbonCommandOption[];
};
type CliCommandHelpGetReply = {
  schema: CliHelpSchema;
};
type CommandVariablesGetReply = {
  variables: CommandVariableOption[];
};
type ProviderModelsGetReply = {
  providerId: string;
  models: Option[];
};
type RouterModelsGetReply = {
  routerId: string;
  models: Option[];
};
type GeneralSettings = {
  display_language: string;
  theme: 0 | 1 | 2;
  font_size_extra_pt: number;
  filetree_show_shell_frames: boolean;
  register_in_explorer: boolean;
};
type GeneralGetReply = {
  general: GeneralSettings;
};
type GeneralActionReply = {
  cancelled?: boolean;
};
type NativePathPickReply = {
  cancelled?: boolean;
  path?: string;
};
type McpServerConfig = {
  enabled?: boolean;
  type?: string | null;
  command?: string;
  args?: string[];
  url?: string;
  headers?: Record<string, string>;
  env?: Record<string, string>;
  tool_timeout?: number;
  enabled_tools?: string[];
};
type McpGetReply = {
  mcpJsonPath: string;
  mcpServers: Record<string, McpServerConfig>;
};
type McpPingReply = {
  probe: unknown;
};
type McpServerForm = {
  id: string;
  enabled: boolean;
  name: string;
  type: string;
  command: string;
  args: string;
  url: string;
  headers: string;
  env: string;
  tool_timeout: string;
  enabled_tools: string;
};
type ReplicateCollection = {
  slug: string;
  name: string;
  description?: string;
};
type ReplicateCollectionsReply = {
  collections: ReplicateCollection[];
};
type ReplicateResolveCollectionReply = {
  collection: string | null;
};
type PixlwizGetReply = PixlwizAuthPayload & {
  budget_ok?: boolean;
  spend?: number;
  max_budget?: number | null;
  budget_duration?: string;
  budget_reset_at?: string;
  budget_error?: string;
};
type ProviderOAuthStatusReply = {
  providerId: string;
  hasToken: boolean;
  loginInProgress: boolean;
  info?: string;
  lastError?: string;
};
type ToolsSettings = {
  disabled_path_tools: string[];
  mcp_tools_enabled: boolean;
  disabled_mcp_servers: string[];
};

type AppFeatures = Record<string, boolean>;

function readAppFeatures(): AppFeatures {
  const raw = typeof window !== "undefined" ? window.APP_FEATURES : undefined;
  return raw && typeof raw === "object" ? raw : {};
}

function featureEnabled(features: AppFeatures, key: string): boolean {
  const value = features[key];
  return typeof value === "boolean" ? value : true;
}

const FALLBACK_AGENT_TOOL_IDS = [
  "list_images",
  "file_glob",
  "file_read",
  "file_search",
  "image_resize",
  "image_transform",
  "image_create",
  "create_video",
  "image_understand",
  "image_from_camera",
  "write_file",
  "speak",
  "schedule_at",
  "schedule_in",
  "schedule_every",
  "schedule_cancel",
  "schedule_list",
  "memory_read",
  "memory_write",
  "memory_append_event",
  "run",
];

const DEFAULT_GENERAL_SETTINGS: GeneralSettings = {
  display_language: "en",
  theme: 0,
  font_size_extra_pt: 2,
  filetree_show_shell_frames: false,
  register_in_explorer: false,
};

function normalizeGeneralSettings(g?: Partial<GeneralSettings>): GeneralSettings {
  return { ...DEFAULT_GENERAL_SETTINGS, ...(g || {}) };
}

type Option = { id: string; label: string };
type TypeaheadOption = string | Option;
type RecentByKey = Record<string, string[]>;
type ChatPreset = {
  id: string;
  name: string;
  chat: ChatSettings;
  createdAt: number;
  updatedAt: number;
};

const RECENT_LS_KEY = "pm.settings.recentModels.v1";
const RECENT_LIMIT = 5;
const CHAT_PRESETS_LS_KEY = "pm.settings.chatPresets.v1";

const ROUTER_OPTIONS: Option[] = [
  { id: "openrouter", label: "OpenRouter" },
  { id: "openai", label: "OpenAI" },
  { id: "gemini", label: "Google Gemini" },
  { id: "ollama", label: "Ollama (local)" },
  { id: "pixlwiz", label: "PixlWiz" },
  { id: "custom", label: "Custom (OpenAI-compatible)" },
];

const STT_PROVIDER_OPTIONS: Option[] = [
  { id: "pixlwiz", label: "PixlWiz (proxy)" },
  { id: "elevenlabs", label: "ElevenLabs (direct)" },
];

const TTS_PROVIDER_OPTIONS: Option[] = [
  { id: "pixlwiz", label: "PixlWiz (proxy)" },
  { id: "elevenlabs", label: "ElevenLabs (direct)" },
];

const STT_MODEL_OPTIONS: Record<string, Option[]> = {
  pixlwiz: [{ id: "pixlwiz-speech-to-text", label: "PixlWiz STT (Whisper-1)" }],
  elevenlabs: [{ id: "scribe_v2_realtime", label: "Scribe v2 (real-time streaming)" }],
};

const TTS_MODEL_OPTIONS: Record<string, Option[]> = {
  pixlwiz: [
    { id: "pixlwiz-speech", label: "PixlWiz TTS (Multilingual v2)" },
    { id: "pixlwiz-speech-turbo", label: "PixlWiz TTS Turbo (v2.5)" },
  ],
  elevenlabs: [
    { id: "eleven_v3", label: "ElevenLabs v3" },
    { id: "eleven_multilingual_v2", label: "Multilingual v2" },
    { id: "eleven_turbo_v2_5", label: "Turbo v2.5 (low latency)" },
    { id: "eleven_flash_v2_5", label: "Flash v2.5 (ultra-fast)" },
  ],
};

const ELEVENLABS_VOICE_OPTIONS: Option[] = [
  { id: "tLK6fPv15M0oKv4V3ACR", label: "Sarah" },
  { id: "Xb7hH8MSUJpSbSDYk0k2", label: "Alice" },
  { id: "XrExE9yKIg1WjnnlVkGX", label: "Matilda" },
  { id: "onwK4e9ZLuTAKqWW03F9", label: "Daniel" },
  { id: "nPczCjzI2devNBz1zQrb", label: "Brian" },
  { id: "pNInz6obpgDQGcFmaJgB", label: "Adam" },
  { id: "JBFqnCBsd6RMkjVDRZzb", label: "George" },
  { id: "cgSgspJ2msm6clMCkdW9", label: "Jessica" },
  { id: "SAz9YHcvj6GT2YYXdXww", label: "River" },
  { id: "TX3LPaxmHKxFdv7VOQHJ", label: "Liam" },
];

function normalizeRecentValue(v: string): string {
  return v.trim();
}

function readRecentByKey(): RecentByKey {
  try {
    const raw = window.localStorage.getItem(RECENT_LS_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as unknown;
    if (!parsed || typeof parsed !== "object") return {};
    const out: RecentByKey = {};
    for (const [k, arr] of Object.entries(parsed as Record<string, unknown>)) {
      if (!Array.isArray(arr)) continue;
      out[k] = arr.filter((x): x is string => typeof x === "string").map((x) => x.trim()).filter(Boolean).slice(0, RECENT_LIMIT);
    }
    return out;
  } catch {
    return {};
  }
}

function writeRecentByKey(store: RecentByKey) {
  try {
    window.localStorage.setItem(RECENT_LS_KEY, JSON.stringify(store));
  } catch {
    // Best-effort persistence only.
  }
}

function makeRecentKey(scope: string, provider: string): string {
  return `${scope}:${provider || "default"}`;
}

function pushRecentValue(map: RecentByKey, key: string, value: string) {
  const v = normalizeRecentValue(value);
  if (!v) return;
  const next = [v, ...(map[key] || []).filter((x) => x !== v)].slice(0, RECENT_LIMIT);
  map[key] = next;
}

function mergeRecentOptions(base: TypeaheadOption[], recents: string[]): TypeaheadOption[] {
  const byId = new Map<string, Option>();
  for (const o of base) {
    if (typeof o === "string") byId.set(o, { id: o, label: o });
    else byId.set(o.id, o);
  }
  const seen = new Set<string>();
  const out: TypeaheadOption[] = [];
  for (const r of recents) {
    const id = normalizeRecentValue(r);
    if (!id || seen.has(id)) continue;
    seen.add(id);
    out.push(byId.get(id) || id);
  }
  for (const o of base) {
    const id = typeof o === "string" ? o : o.id;
    if (seen.has(id)) continue;
    seen.add(id);
    out.push(o);
  }
  return out;
}

function readChatPresets(): ChatPreset[] {
  try {
    const raw = window.localStorage.getItem(CHAT_PRESETS_LS_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return [];
    return parsed.filter((x): x is ChatPreset => !!x && typeof x === "object" && typeof (x as ChatPreset).id === "string");
  } catch {
    return [];
  }
}

function writeChatPresets(presets: ChatPreset[]) {
  try {
    window.localStorage.setItem(CHAT_PRESETS_LS_KEY, JSON.stringify(presets));
  } catch {
    // Best-effort persistence only.
  }
}

let rpcSeq = 0;
const rpcPending = new Map<string, (msg: HostRpcReply) => void>();
const CUSTOM_COMMANDS_DEV_LS_KEY = "pm.settings.dev.commandsJson";

function nextRpcId(): string {
  rpcSeq += 1;
  return `settings-rpc-${rpcSeq}`;
}

function hasHostBridge() {
  return typeof window.chrome?.webview?.postMessage === "function";
}

function hostPost(msg: Record<string, unknown>) {
  const post = window.chrome?.webview?.postMessage;
  if (typeof post !== "function") {
    return;
  }
  post(JSON.stringify(msg));
}

type SettingsLogLevel = "log" | "info" | "warn" | "error";

function sanitizeRpcPayload(payload: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(payload)) {
    if (/api[_-]?key/i.test(key)) {
      out[key] = typeof value === "string" && value ? "[redacted]" : value;
      continue;
    }
    out[key] = value;
  }
  return out;
}

/** Logs to browser console; deferred so host console bridge cannot block RPC postMessage. */
function settingsLog(level: SettingsLogLevel, event: string, detail?: Record<string, unknown>) {
  const suffix = detail ? ` ${JSON.stringify(detail)}` : "";
  const line = `[settings-app] ${event}${suffix}`;
  queueMicrotask(() => {
    if (level === "error") console.error(line);
    else if (level === "warn") console.warn(line);
    else if (level === "info") console.info(line);
    else console.log(line);
  });
}

const RPC_TIMEOUT_DEFAULT_MS = 30000;
const RPC_TIMEOUT_CATALOG_MS = 180000;

function rpcTimeoutMs(method: string, payload: Record<string, unknown>): number {
  if (
    method === "settingsRouterModelsGet"
    || method === "settingsProviderModelsGet"
    || method === "settingsReplicateCollectionsGet"
  ) {
    return RPC_TIMEOUT_CATALOG_MS;
  }
  if (method === "settingsCliCommandHelpGet") {
    return 60000;
  }
  if (payload.forceRefresh === true) {
    return RPC_TIMEOUT_CATALOG_MS;
  }
  return RPC_TIMEOUT_DEFAULT_MS;
}

function cloneJson<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

function defaultCustomCommands(): CustomCommandsDocument {
  return cloneJson(defaultCustomCommandsDoc as CustomCommandsDocument);
}

function readDevCustomCommands(): CustomCommandsDocument {
  try {
    const raw = window.localStorage.getItem(CUSTOM_COMMANDS_DEV_LS_KEY);
    if (!raw) return defaultCustomCommands();
    const parsed = JSON.parse(raw) as unknown;
    if (!parsed || typeof parsed !== "object") return defaultCustomCommands();
    return parsed as CustomCommandsDocument;
  } catch {
    return defaultCustomCommands();
  }
}

function writeDevCustomCommands(doc: CustomCommandsDocument) {
  try {
    window.localStorage.setItem(CUSTOM_COMMANDS_DEV_LS_KEY, JSON.stringify(doc, null, 2));
  } catch {
    // Dev-only persistence; keep the editor usable if storage is unavailable.
  }
}

function devCliCommands(): CliCommandOption[] {
  return [
    { id: "resize", label: "Resize", available: true },
    { id: "transform", label: "Transform", available: true },
    { id: "create", label: "Create", available: true },
    { id: "provider", label: "Provider models", available: true },
    { id: "llm", label: "LLM", available: true },
    { id: "settings", label: "Settings", available: true },
    { id: "commands", label: "List registered commands", available: true },
  ];
}

function devCliHelpSchema(command: string): CliHelpSchema {
  const parts = command.split(/\s+/).filter(Boolean);
  const leaf = parts[parts.length - 1] || command;
  const common = {
    name: leaf,
    path: command,
    description: command,
    allowExtras: false,
    subcommands: [],
  };
  const option = (name: string, description: string, typeName = "TEXT", defaultValue = "") => ({
    name,
    names: [`--${name}`],
    positional: false,
    required: false,
    group: "Options",
    description,
    typeName,
    default: defaultValue,
    expectedMin: 1,
    expectedMax: 1,
    allowExtraArgs: false,
  });
  if (command === "transform") {
    return {
      ...common,
      description: "Transform images",
      options: [
        option("src", "Input image path"),
        option("prompt", "Transformation prompt"),
        option("output", "Output image path"),
        option("provider", "Provider override"),
        option("model", "Model override"),
      ],
    };
  }
  if (command === "llm") {
    return {
      ...common,
      description: "LLM",
      options: [],
      subcommands: [
        { name: "agent", description: "Run a chat-agent turn" },
        { name: "info", description: "Show LLM settings info" },
      ],
    };
  }
  if (command === "llm agent") {
    return {
      ...common,
      description: "Run a chat-agent turn",
      options: [
        option("prompt", "User prompt"),
        option("include", "File or folder paths to add to agent selection"),
        option("router", "LLM router"),
        option("model", "Model id"),
        option("max-iter", "Maximum tool-call iterations", "INT"),
      ],
    };
  }
  if (command === "resize") {
    return {
      ...common,
      description: "Resize images",
      options: [
        option("src", "Input image path"),
        option("width", "Target width", "INT"),
        option("height", "Target height", "INT"),
        option("output", "Output image path"),
      ],
    };
  }
  return { ...common, options: [] };
}

const cliSchemaTreeInflight = new Map<string, Promise<Array<readonly [string, CliHelpSchema]>>>();

async function loadCliSchemaTree(commandId: string): Promise<Array<readonly [string, CliHelpSchema]>> {
  const cached = cliSchemaTreeInflight.get(commandId);
  if (cached) {
    settingsLog("info", "cliSchema:dedup", { commandId });
    return cached;
  }
  settingsLog("info", "cliSchema:start", { commandId });
  const task = (async () => {
    const out: Array<readonly [string, CliHelpSchema]> = [];
    const seen = new Set<string>();
    async function visit(commandPath: string) {
      if (seen.has(commandPath)) return;
      seen.add(commandPath);
      const reply = await rpc<CliCommandHelpGetReply>("settingsCliCommandHelpGet", { command: commandPath });
      out.push([commandPath, reply.schema] as const);
      for (const child of reply.schema.subcommands || []) {
        if (child.name) await visit(`${commandPath} ${child.name}`);
      }
    }
    await visit(commandId);
    settingsLog("info", "cliSchema:done", { commandId, paths: out.length });
    return out;
  })();
  cliSchemaTreeInflight.set(commandId, task);
  try {
    return await task;
  } finally {
    cliSchemaTreeInflight.delete(commandId);
  }
}

function providerModelsFetchKey(providerId: string, force: boolean, collectionSlug?: string) {
  if (providerId === "replicate")
    return `replicate:${collectionSlug || "official"}:${force ? "1" : "0"}`;
  return `${providerId}:${force ? "1" : "0"}`;
}

type ModelRefreshScope = "router" | "image" | "vision" | "video";

function providerIdForModelScope(scope: Exclude<ModelRefreshScope, "router">, chat: ChatSettings): string {
  if (scope === "image") return chat.image_provider;
  if (scope === "vision") return chat.image_recognition_provider;
  return chat.video_provider;
}

function defaultChatSettings(): ChatSettings {
  return {
    router: "openrouter",
    model: "",
    max_iterations: 8,
    image_provider: "google",
    image_model: "",
    image_recognition_provider: "google",
    image_recognition_model: "",
    video_provider: "google",
    video_model: "",
    stt_provider: "",
    stt_model: "",
    tts_provider: "",
    tts_model: "",
    tts_voice_id: "",
  };
}

function devCommandVariables(): CommandVariableOption[] {
  return [
    { name: "CURRENT_FILE", group: "Current file", description: "Current file absolute path from Explorer selection or the open preview." },
    { name: "CURRENT_FILE_NAME", group: "Current file", description: "Current file name including extension." },
    { name: "CURRENT_PATH", group: "Current file", description: "Current Explorer folder, or current file parent folder when a file is selected." },
    { name: "CURRENT_SELECTION", group: "Current file", description: "Whitespace-separated current Explorer selection list; entries are files or folders." },
    { name: "CWD", group: "Process", description: "Current command working directory." },
    { name: "PATH_SEP", group: "Process", description: "Native path separator for this platform." },
    { name: "PATH_LIST_SEP", group: "Process", description: "Native delimiter for lists of paths." },
    { name: "SRC_FILE", group: "Source", description: "Selected source file path." },
    { name: "SRC_DIR", group: "Source", description: "Selected source parent directory." },
    { name: "SRC_NAME", group: "Source", description: "Selected source filename without extension." },
    { name: "SRC_EXT", group: "Source", description: "Selected source extension." },
    { name: "SRC_FILE_EXT", group: "Source", description: "Selected source extension including the leading dot." },
    { name: "YYYY", group: "Date / time", description: "Current four-digit year." },
    { name: "MM", group: "Date / time", description: "Current month." },
    { name: "DD", group: "Date / time", description: "Current day of month." },
    { name: "HH", group: "Date / time", description: "Current hour." },
    { name: "SS", group: "Date / time", description: "Current seconds." },
    { name: "KNOWNFOLDER:Home", group: "Known folders - Portable", description: "User home/profile folder." },
    { name: "KNOWNFOLDER:Config", group: "Known folders - Portable", description: "User configuration folder." },
    { name: "KNOWNFOLDER:Data", group: "Known folders - Portable", description: "User data folder." },
    { name: "KNOWNFOLDER:Cache", group: "Known folders - Portable", description: "User cache folder." },
    { name: "KNOWNFOLDER:Temp", group: "Known folders - Portable", description: "Temporary files folder." },
    { name: "KNOWNFOLDER:Desktop", group: "Known folders - User", description: "Desktop folder." },
    { name: "KNOWNFOLDER:Documents", group: "Known folders - User", description: "Documents folder." },
    { name: "KNOWNFOLDER:Downloads", group: "Known folders - User", description: "Downloads folder." },
    { name: "ENV:PATH", group: "Environment", description: "Process PATH environment variable. Replace PATH with any variable name." },
  ];
}

function devRpc<T>(method: string, payload: Record<string, unknown>): Promise<T> | null {
  switch (method) {
    case "settingsCustomCommandsGet":
      return Promise.resolve({
        commandsPath: "localStorage:pm.settings.dev.commandsJson",
        document: readDevCustomCommands(),
        variables: devCommandVariables(),
      } as T);
    case "settingsCustomCommandsSave": {
      const document = payload.document && typeof payload.document === "object"
        ? (payload.document as CustomCommandsDocument)
        : defaultCustomCommands();
      writeDevCustomCommands(document);
      return Promise.resolve({ commandsPath: "localStorage:pm.settings.dev.commandsJson" } as T);
    }
    case "settingsCliCommandsGet":
      return Promise.resolve({ commands: devCliCommands() } as T);
    case "settingsAppCommandsGet":
      return Promise.resolve({ commands: devAppCommandOptions() } as T);
    case "settingsRibbonCommandsGet":
      return Promise.resolve({ commands: devRibbonCommandOptions() } as T);
    case "settingsCliCommandHelpGet":
      return Promise.resolve({ schema: devCliHelpSchema(String(payload.command || "")) } as T);
    case "settingsCommandVariablesGet":
      return Promise.resolve({ variables: devCommandVariables() } as T);
    case "settingsTablerIconsGet":
      return Promise.resolve({ icons: [] } as T);
    case "settingsChatPresetsGet":
      return Promise.resolve({ presets: readChatPresets() } as T);
    case "settingsProvidersGet":
      return Promise.resolve({ providers: [], settingsPath: "localStorage", theme: "light" } as T);
    case "settingsChatGet":
      return Promise.resolve({ chat: defaultChatSettings(), displayLanguage: "en" } as T);
    case "settingsGeneralGet":
      return Promise.resolve({ general: normalizeGeneralSettings({ filetree_show_shell_frames: true }) } as T);
    case "settingsNativePathPick": {
      const pickKind = payload.pickKind === "folder" ? "folder" : "file";
      const path = window.prompt(`Pick ${pickKind} path`);
      return Promise.resolve({ cancelled: !path, path: path || "" } as T);
    }
    default:
      return null;
  }
}

async function copyTextToClipboard(text: string) {
  if (!text) return;
  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(text);
    return;
  }
  const ta = document.createElement("textarea");
  ta.value = text;
  ta.style.position = "fixed";
  ta.style.opacity = "0";
  document.body.appendChild(ta);
  ta.focus();
  ta.select();
  document.execCommand("copy");
  document.body.removeChild(ta);
}

function IconCopy() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
      <rect x="5" y="2" width="9" height="11" rx="1.5" fill="none" stroke="currentColor" strokeWidth="1.2" />
      <rect x="2" y="5" width="9" height="9" rx="1.5" fill="none" stroke="currentColor" strokeWidth="1.2" />
    </svg>
  );
}

function IconEye() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
      <path d="M1.5 8c1.5-2.6 3.8-4 6.5-4s5 1.4 6.5 4c-1.5 2.6-3.8 4-6.5 4s-5-1.4-6.5-4Z" fill="none" stroke="currentColor" strokeWidth="1.2" />
      <circle cx="8" cy="8" r="2.1" fill="none" stroke="currentColor" strokeWidth="1.2" />
    </svg>
  );
}

function IconEyeOff() {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
      <path d="M1.5 8c1.5-2.6 3.8-4 6.5-4s5 1.4 6.5 4c-1.5 2.6-3.8 4-6.5 4s-5-1.4-6.5-4Z" fill="none" stroke="currentColor" strokeWidth="1.2" />
      <path d="M2.5 2.5 13.5 13.5" fill="none" stroke="currentColor" strokeWidth="1.2" />
    </svg>
  );
}

function IconLogin() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true" width="14" height="14">
      <path fill="currentColor" d="M10 3h8a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-8v-2h8V5h-8V3zm-1.7 4.3L9.7 9 7 11.7h9v2.6H7L9.7 17l-1.4 1.4L3.2 13.3a2 2 0 0 1 0-2.8l5.1-5.2 1.4 1.4z"/>
    </svg>
  );
}

function makeMcpServerForm(partial?: Partial<McpServerForm>): McpServerForm {
  return {
    id: partial?.id || `mcp-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
    enabled: partial?.enabled ?? true,
    name: partial?.name || "",
    type: partial?.type || "",
    command: partial?.command || "",
    args: partial?.args || "",
    url: partial?.url || "",
    headers: partial?.headers || "",
    env: partial?.env || "",
    tool_timeout: partial?.tool_timeout || "30",
    enabled_tools: partial?.enabled_tools || "*",
  };
}

function mcpServerConfigToForm(name: string, cfg: McpServerConfig): McpServerForm {
  return makeMcpServerForm({
    enabled: cfg.enabled !== false,
    name,
    type: cfg.type || "",
    command: cfg.command || "",
    args: (cfg.args || []).join(", "),
    url: cfg.url || "",
    headers: cfg.headers && Object.keys(cfg.headers).length ? JSON.stringify(cfg.headers) : "",
    env: cfg.env && Object.keys(cfg.env).length ? JSON.stringify(cfg.env) : "",
    tool_timeout: String(cfg.tool_timeout ?? 30),
    enabled_tools: (cfg.enabled_tools && cfg.enabled_tools.length ? cfg.enabled_tools : ["*"]).join(", "),
  });
}

function parseJsonMap(raw: string): Record<string, string> {
  const t = raw.trim();
  if (!t) return {};
  try {
    const parsed = JSON.parse(t) as unknown;
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) return {};
    const out: Record<string, string> = {};
    for (const [k, v] of Object.entries(parsed as Record<string, unknown>)) {
      if (typeof v === "string") out[k] = v;
    }
    return out;
  } catch {
    return {};
  }
}

function mcpServerFormToConfig(f: McpServerForm): McpServerConfig {
  return {
    enabled: f.enabled,
    type: f.type.trim() || null,
    command: f.command.trim(),
    args: f.args
      .split(",")
      .map((x) => x.trim())
      .filter(Boolean),
    url: f.url.trim(),
    headers: parseJsonMap(f.headers),
    env: parseJsonMap(f.env),
    tool_timeout: Math.max(1, Number.parseInt(f.tool_timeout || "30", 10) || 30),
    enabled_tools: f.enabled_tools
      .split(",")
      .map((x) => x.trim())
      .filter(Boolean),
  };
}

function postBeginMove() {
  const post = window.chrome?.webview?.postMessage;
  if (typeof post !== "function") {
    return;
  }
  post(JSON.stringify({ t: "cweb_begin_move", src: "alt_drag" }));
}

function postWindowCommand(t: "cweb_min" | "cweb_max" | "cweb_close") {
  const post = window.chrome?.webview?.postMessage;
  if (typeof post !== "function") {
    return;
  }
  post(JSON.stringify({ t }));
}

function SelectInput({
  value,
  onChange,
  options,
  disabled,
}: {
  value: string;
  onChange: (v: string) => void;
  options: TypeaheadOption[];
  disabled?: boolean;
}) {
  const hasSelected = options.some((o) => (typeof o === "string" ? o : o.id) === value);
  return (
    <select className="pm-select" value={value} disabled={disabled} onChange={(e) => onChange(e.target.value)}>
      {!hasSelected && value ? <option value={value}>{value}</option> : null}
      {!value ? <option value="">-</option> : null}
      {options.map((o) => {
        const id = typeof o === "string" ? o : o.id;
        const label = typeof o === "string" ? o : o.label;
        return (
          <option key={`${id}:${label}`} value={id}>
            {label}
          </option>
        );
      })}
    </select>
  );
}

const TypeaheadInput = ModelStringTypeahead;


const I18N: Record<
  string,
  {
    tabProviders: string;
    tabChat: string;
    ok: string;
    cancel: string;
    titleProviders: string;
    titleChat: string;
    lblRouter: string;
    lblModel: string;
    lblMaxIter: string;
    lblImageProvider: string;
    lblImageModel: string;
    lblRecognitionProvider: string;
    lblRecognitionModel: string;
    lblVideoProvider: string;
    lblVideoModel: string;
    lblSttProvider: string;
    lblSttModel: string;
    lblTtsProvider: string;
    lblTtsModel: string;
    lblTtsVoice: string;
    secText: string;
    secImage: string;
    secImageGeneration: string;
    secImageVision: string;
    secVideo: string;
    secAudio: string;
    tabGeneral: string;
    tabAdvanced: string;
    tabMcp: string;
    tabPixlwiz: string;
    tabTools: string;
    titleGeneral: string;
    titleAdvanced: string;
    secSettingsFile: string;
    descSettingsFile: string;
    secClipboard: string;
    descClipboard: string;
    secProfileArchive: string;
    descProfileArchive: string;
    btnExportArchive: string;
    btnImportArchive: string;
    msgImportArchiveConfirm: string;
    lblDisplayLanguage: string;
    lblTheme: string;
    lblFontSize: string;
    lblExplorerExtended: string;
    lblEncryptExport: string;
    btnExportSettings: string;
    btnImportSettings: string;
    btnCopyToClipboard: string;
    btnPasteFromClipboard: string;
    msgImportConfirm: string;
    errGeneralAction: string;
    optThemeSystem: string;
    optThemeLight: string;
    optThemeDark: string;
    optLangEn: string;
    optLangEs: string;
    optLangDe: string;
    optLangIt: string;
    optLangFr: string;
    optFont0: string;
    optFont1: string;
    optFont2: string;
    optFont3: string;
    optFont4: string;
    presetSelect: string;
    presetName: string;
    presetLoad: string;
    presetSaveAs: string;
    presetUpdate: string;
    presetRename: string;
    presetDelete: string;
    errPresetNameRequired: string;
    errPresetNameExists: string;
    errPresetSelectUpdate: string;
    errPresetSelectRename: string;
    errPresetSelectDelete: string;
    grpVoiceInput: string;
    grpVoiceOutput: string;
    errGeneralSave: string;
    titleMcp: string;
    mcpPath: string;
    mcpAddServer: string;
    mcpRemoveServer: string;
    mcpServerName: string;
    mcpType: string;
    mcpTypeAuto: string;
    mcpTypeStdio: string;
    mcpTypeSse: string;
    mcpTypeStreamableHttp: string;
    mcpCommand: string;
    mcpArgs: string;
    mcpUrl: string;
    mcpHeaders: string;
    mcpEnv: string;
    mcpToolTimeout: string;
    mcpEnabledTools: string;
    mcpPing: string;
    mcpPingResult: string;
    mcpEnabled: string;
    errMcpSave: string;
    titlePixlwiz: string;
    pixlwizAuthStatus: string;
    pixlwizBudget: string;
    pixlwizLogin: string;
    pixlwizLogout: string;
    pixlwizRefresh: string;
    pixlwizStatusFailed: string;
    pixlwizSignedOut: string;
    pixlwizSignedIn: string;
    titleTools: string;
    toolsPathEnabled: string;
    toolsMcpMaster: string;
    toolsDisabledMcpServers: string;
  }
> = {
  en: {
    tabProviders: "Providers",
    tabChat: "Chat",
    ok: "OK",
    cancel: "Cancel",
    titleProviders: "Provider Settings",
    titleChat: "Chat Provider Settings",
    lblRouter: "Router",
    lblModel: "Model",
    lblMaxIter: "Max iterations",
    lblImageProvider: "Image provider",
    lblImageModel: "Image model",
    lblRecognitionProvider: "Recognition provider",
    lblRecognitionModel: "Recognition model",
    lblVideoProvider: "Video provider",
    lblVideoModel: "Video model",
    lblSttProvider: "Voice Input Provider",
    lblSttModel: "Voice Input Model",
    lblTtsProvider: "Voice Output Provider",
    lblTtsModel: "Voice Output Model",
    lblTtsVoice: "Voice Output Voice",
    secText: "Text",
    secImage: "Image",
    secImageGeneration: "Generation",
    secImageVision: "Vision",
    secVideo: "Video",
    secAudio: "Audio",
    tabGeneral: "General",
    tabAdvanced: "Advanced",
    tabMcp: "MCP",
    tabPixlwiz: "Pixlwiz",
    tabTools: "Tools",
    titleGeneral: "General Settings",
    titleAdvanced: "Advanced",
    secSettingsFile: "Settings file",
    descSettingsFile: "Export or import a single settings document (UTF-8 JSON or encrypted PME1 on Windows).",
    secClipboard: "Clipboard",
    descClipboard: "Copy or paste the full settings JSON for quick sharing.",
    secProfileArchive: "Profile archive",
    descProfileArchive: "Export or import a ZIP of the full app profile. WebView cache folders and the encryption key file are excluded.",
    btnExportArchive: "Export profile archive",
    btnImportArchive: "Import profile archive",
    msgImportArchiveConfirm: "Import profile archive and overwrite the current profile?",
    lblDisplayLanguage: "Display Language",
    lblTheme: "Theme",
    lblFontSize: "Font Size",
    lblExplorerExtended: "Explorer Panel: Extended",
    lblEncryptExport: "Encrypt export",
    btnExportSettings: "Export settings",
    btnImportSettings: "Import settings",
    btnCopyToClipboard: "Copy to clipboard",
    btnPasteFromClipboard: "Paste from clipboard",
    msgImportConfirm: "Import settings from file and overwrite current values?",
    errGeneralAction: "Settings action failed.",
    optThemeSystem: "System",
    optThemeLight: "Light",
    optThemeDark: "Dark",
    optLangEn: "English",
    optLangEs: "Spanish",
    optLangDe: "German",
    optLangIt: "Italian",
    optLangFr: "French",
    optFont0: "Small",
    optFont1: "Compact",
    optFont2: "Standard (System size)",
    optFont3: "Large",
    optFont4: "Extra Large",
    presetSelect: "Select preset...",
    presetName: "Preset name",
    presetLoad: "Load",
    presetSaveAs: "Save As",
    presetUpdate: "Update",
    presetRename: "Rename",
    presetDelete: "Delete",
    errPresetNameRequired: "Preset name is required.",
    errPresetNameExists: "Preset name already exists.",
    errPresetSelectUpdate: "Select a preset to update.",
    errPresetSelectRename: "Select a preset to rename.",
    errPresetSelectDelete: "Select a preset to delete.",
    grpVoiceInput: "Voice Input",
    grpVoiceOutput: "Voice Output",
    errGeneralSave: "Failed to save general settings.",
    titleMcp: "MCP Servers",
    mcpPath: "Profile file",
    mcpAddServer: "Add server",
    mcpRemoveServer: "Remove",
    mcpServerName: "Server Name",
    mcpType: "Type",
    mcpTypeAuto: "Auto-detect",
    mcpTypeStdio: "stdio",
    mcpTypeSse: "sse",
    mcpTypeStreamableHttp: "streamableHttp",
    mcpCommand: "Command",
    mcpArgs: "Args",
    mcpUrl: "URL",
    mcpHeaders: "Headers (JSON)",
    mcpEnv: "Env Vars (JSON)",
    mcpToolTimeout: "Tool Timeout (s)",
    mcpEnabledTools: "Enabled Tools",
    mcpPing: "Ping",
    mcpPingResult: "Ping result",
    mcpEnabled: "Enabled",
    errMcpSave: "Failed to save MCP settings.",
    titlePixlwiz: "Pixlwiz",
    pixlwizAuthStatus: "Auth status",
    pixlwizBudget: "Spending",
    pixlwizLogin: "Login",
    pixlwizLogout: "Logout",
    pixlwizRefresh: "Refresh",
    pixlwizStatusFailed: "Status unavailable",
    pixlwizSignedOut: "Signed out",
    pixlwizSignedIn: "Signed in",
    titleTools: "Tools",
    toolsPathEnabled: "Enabled Path Tools",
    toolsMcpMaster: "Enable MCP tools globally",
    toolsDisabledMcpServers: "Disabled MCP servers (comma-separated)",
  },
  es: {
    tabProviders: "Proveedores",
    tabChat: "Chat",
    ok: "Aceptar",
    cancel: "Cancelar",
    titleProviders: "Ajustes de proveedores",
    titleChat: "Ajustes de proveedor de chat",
    lblRouter: "Enrutador",
    lblModel: "Modelo",
    lblMaxIter: "Máx. iter.",
    lblImageProvider: "Proveedor de imagen",
    lblImageModel: "Modelo de imagen",
    lblRecognitionProvider: "Proveedor reconocimiento",
    lblRecognitionModel: "Modelo reconocimiento",
    lblVideoProvider: "Proveedor de video",
    lblVideoModel: "Modelo de video",
    lblSttProvider: "Proveedor entrada voz",
    lblSttModel: "Modelo entrada voz",
    lblTtsProvider: "Proveedor salida voz",
    lblTtsModel: "Modelo salida voz",
    lblTtsVoice: "Voz salida",
    secText: "Texto",
    secImage: "Imagen",
    secImageGeneration: "Generación",
    secImageVision: "Visión",
    secVideo: "Video",
    secAudio: "Audio",
    tabGeneral: "General",
    tabAdvanced: "Avanzado",
    tabMcp: "MCP",
    tabPixlwiz: "Pixlwiz",
    tabTools: "Tools",
    titleGeneral: "Ajustes generales",
    titleAdvanced: "Avanzado",
    secSettingsFile: "Archivo de ajustes",
    descSettingsFile: "Exportar o importar un documento de ajustes (JSON UTF-8 o PME1 cifrado en Windows).",
    secClipboard: "Portapapeles",
    descClipboard: "Copiar o pegar el JSON completo de ajustes para compartir rápidamente.",
    secProfileArchive: "Archivo de perfil",
    descProfileArchive: "Exportar o importar un ZIP del perfil completo. Se excluyen carpetas de caché WebView y el archivo de clave de cifrado.",
    btnExportArchive: "Exportar archivo de perfil",
    btnImportArchive: "Importar archivo de perfil",
    msgImportArchiveConfirm: "¿Importar archivo de perfil y sobrescribir el perfil actual?",
    lblDisplayLanguage: "Idioma",
    lblTheme: "Tema",
    lblFontSize: "Tamaño de fuente",
    lblExplorerExtended: "Panel Explorer: Extendido",
    lblEncryptExport: "Cifrar exportación",
    btnExportSettings: "Exportar ajustes",
    btnImportSettings: "Importar ajustes",
    btnCopyToClipboard: "Copiar al portapapeles",
    btnPasteFromClipboard: "Pegar desde portapapeles",
    msgImportConfirm: "¿Importar ajustes desde archivo y sobrescribir los valores actuales?",
    errGeneralAction: "Error al ejecutar la acción de ajustes.",
    optThemeSystem: "Sistema",
    optThemeLight: "Claro",
    optThemeDark: "Oscuro",
    optLangEn: "Inglés",
    optLangEs: "Español",
    optLangDe: "Alemán",
    optLangIt: "Italiano",
    optLangFr: "Francés",
    optFont0: "Pequeño",
    optFont1: "Compacto",
    optFont2: "Estándar (Tamaño del sistema)",
    optFont3: "Grande",
    optFont4: "Muy grande",
    presetSelect: "Seleccionar preset...",
    presetName: "Nombre del preset",
    presetLoad: "Cargar",
    presetSaveAs: "Guardar como",
    presetUpdate: "Actualizar",
    presetRename: "Renombrar",
    presetDelete: "Eliminar",
    errPresetNameRequired: "Se requiere nombre del preset.",
    errPresetNameExists: "El nombre del preset ya existe.",
    errPresetSelectUpdate: "Seleccione un preset para actualizar.",
    errPresetSelectRename: "Seleccione un preset para renombrar.",
    errPresetSelectDelete: "Seleccione un preset para eliminar.",
    grpVoiceInput: "Entrada de Voz",
    grpVoiceOutput: "Salida de Voz",
    errGeneralSave: "No se pudieron guardar los ajustes generales.",
    titleMcp: "Servidores MCP",
    mcpPath: "Archivo de perfil",
    mcpAddServer: "Agregar servidor",
    mcpRemoveServer: "Eliminar",
    mcpServerName: "Nombre del servidor",
    mcpType: "Tipo",
    mcpTypeAuto: "Auto-detectar",
    mcpTypeStdio: "stdio",
    mcpTypeSse: "sse",
    mcpTypeStreamableHttp: "streamableHttp",
    mcpCommand: "Comando",
    mcpArgs: "Args",
    mcpUrl: "URL",
    mcpHeaders: "Headers (JSON)",
    mcpEnv: "Variables de entorno (JSON)",
    mcpToolTimeout: "Timeout herramienta (s)",
    mcpEnabledTools: "Herramientas habilitadas",
    mcpPing: "Ping",
    mcpPingResult: "Resultado ping",
    mcpEnabled: "Habilitado",
    errMcpSave: "No se pudieron guardar los ajustes MCP.",
    titlePixlwiz: "Pixlwiz",
    pixlwizAuthStatus: "Estado auth",
    pixlwizBudget: "Gasto",
    pixlwizLogin: "Login",
    pixlwizLogout: "Logout",
    pixlwizRefresh: "Actualizar",
    pixlwizStatusFailed: "Estado no disponible",
    pixlwizSignedOut: "Sesión cerrada",
    pixlwizSignedIn: "Sesión iniciada",
    titleTools: "Tools",
    toolsPathEnabled: "Herramientas de ruta habilitadas",
    toolsMcpMaster: "Habilitar herramientas MCP globalmente",
    toolsDisabledMcpServers: "Servidores MCP deshabilitados (coma)",
  },
  de: {
    tabProviders: "Anbieter",
    tabChat: "Chat",
    ok: "OK",
    cancel: "Abbrechen",
    titleProviders: "Anbieter-Einstellungen",
    titleChat: "Chat-Anbieter-Einstellungen",
    lblRouter: "Router",
    lblModel: "Modell",
    lblMaxIter: "Max. Iter.",
    lblImageProvider: "Bildanbieter",
    lblImageModel: "Bildmodell",
    lblRecognitionProvider: "Erkennungsanbieter",
    lblRecognitionModel: "Erkennungsmodell",
    lblVideoProvider: "Videoanbieter",
    lblVideoModel: "Videomodell",
    lblSttProvider: "Spracheingabe-Anbieter",
    lblSttModel: "Spracheingabe-Modell",
    lblTtsProvider: "Sprachausgabe-Anbieter",
    lblTtsModel: "Sprachausgabe-Modell",
    lblTtsVoice: "Sprachausgabe-Stimme",
    secText: "Text",
    secImage: "Bild",
    secImageGeneration: "Generierung",
    secImageVision: "Erkennung",
    secVideo: "Video",
    secAudio: "Audio",
    tabGeneral: "Allgemein",
    tabAdvanced: "Erweitert",
    tabMcp: "MCP",
    tabPixlwiz: "Pixlwiz",
    tabTools: "Tools",
    titleGeneral: "Allgemeine Einstellungen",
    titleAdvanced: "Erweitert",
    secSettingsFile: "Einstellungsdatei",
    descSettingsFile: "Einzelnes Einstellungsdokument exportieren oder importieren (UTF-8 JSON oder verschlüsseltes PME1 unter Windows).",
    secClipboard: "Zwischenablage",
    descClipboard: "Vollständiges Einstellungs-JSON zum schnellen Teilen kopieren oder einfügen.",
    secProfileArchive: "Profilarchiv",
    descProfileArchive: "ZIP des vollständigen App-Profils exportieren oder importieren. WebView-Cache-Ordner und Schlüsseldatei werden ausgeschlossen.",
    btnExportArchive: "Profilarchiv exportieren",
    btnImportArchive: "Profilarchiv importieren",
    msgImportArchiveConfirm: "Profilarchiv importieren und das aktuelle Profil überschreiben?",
    lblDisplayLanguage: "Sprache",
    lblTheme: "Thema",
    lblFontSize: "Schriftgröße",
    lblExplorerExtended: "Explorer-Panel: Erweitert",
    lblEncryptExport: "Export verschlüsseln",
    btnExportSettings: "Einstellungen exportieren",
    btnImportSettings: "Einstellungen importieren",
    btnCopyToClipboard: "In Zwischenablage kopieren",
    btnPasteFromClipboard: "Aus Zwischenablage einfügen",
    msgImportConfirm: "Einstellungen aus Datei importieren und aktuelle Werte überschreiben?",
    errGeneralAction: "Aktion für Einstellungen fehlgeschlagen.",
    optThemeSystem: "System",
    optThemeLight: "Hell",
    optThemeDark: "Dunkel",
    optLangEn: "Englisch",
    optLangEs: "Spanisch",
    optLangDe: "Deutsch",
    optLangIt: "Italienisch",
    optLangFr: "Französisch",
    optFont0: "Klein",
    optFont1: "Kompakt",
    optFont2: "Standard (Systemgröße)",
    optFont3: "Groß",
    optFont4: "Sehr groß",
    presetSelect: "Preset auswählen...",
    presetName: "Preset-Name",
    presetLoad: "Laden",
    presetSaveAs: "Speichern als",
    presetUpdate: "Aktualisieren",
    presetRename: "Umbenennen",
    presetDelete: "Löschen",
    errPresetNameRequired: "Preset-Name ist erforderlich.",
    errPresetNameExists: "Preset-Name existiert bereits.",
    errPresetSelectUpdate: "Preset zum Aktualisieren auswählen.",
    errPresetSelectRename: "Preset zum Umbenennen auswählen.",
    errPresetSelectDelete: "Preset zum Löschen auswählen.",
    grpVoiceInput: "Spracheingabe",
    grpVoiceOutput: "Sprachausgabe",
    errGeneralSave: "Allgemeine Einstellungen konnten nicht gespeichert werden.",
    titleMcp: "MCP-Server",
    mcpPath: "Profil-Datei",
    mcpAddServer: "Server hinzufügen",
    mcpRemoveServer: "Entfernen",
    mcpServerName: "Servername",
    mcpType: "Typ",
    mcpTypeAuto: "Automatisch",
    mcpTypeStdio: "stdio",
    mcpTypeSse: "sse",
    mcpTypeStreamableHttp: "streamableHttp",
    mcpCommand: "Befehl",
    mcpArgs: "Args",
    mcpUrl: "URL",
    mcpHeaders: "Header (JSON)",
    mcpEnv: "Env-Variablen (JSON)",
    mcpToolTimeout: "Tool-Timeout (s)",
    mcpEnabledTools: "Aktivierte Tools",
    mcpPing: "Ping",
    mcpPingResult: "Ping-Ergebnis",
    mcpEnabled: "Aktiviert",
    errMcpSave: "MCP-Einstellungen konnten nicht gespeichert werden.",
    titlePixlwiz: "Pixlwiz",
    pixlwizAuthStatus: "Auth-Status",
    pixlwizBudget: "Verbrauch",
    pixlwizLogin: "Login",
    pixlwizLogout: "Logout",
    pixlwizRefresh: "Aktualisieren",
    pixlwizStatusFailed: "Status nicht verfügbar",
    pixlwizSignedOut: "Abgemeldet",
    pixlwizSignedIn: "Angemeldet",
    titleTools: "Tools",
    toolsPathEnabled: "Aktivierte Pfad-Tools",
    toolsMcpMaster: "MCP-Tools global aktivieren",
    toolsDisabledMcpServers: "Deaktivierte MCP-Server (Komma-getrennt)",
  },
  it: {
    tabProviders: "Provider",
    tabChat: "Chat",
    ok: "OK",
    cancel: "Annulla",
    titleProviders: "Impostazioni provider",
    titleChat: "Impostazioni provider chat",
    lblRouter: "Router",
    lblModel: "Modello",
    lblMaxIter: "Max iter",
    lblImageProvider: "Provider immagini",
    lblImageModel: "Modello immagini",
    lblRecognitionProvider: "Provider riconoscimento",
    lblRecognitionModel: "Modello riconoscimento",
    lblVideoProvider: "Provider video",
    lblVideoModel: "Modello video",
    lblSttProvider: "Provider input vocale",
    lblSttModel: "Modello input vocale",
    lblTtsProvider: "Provider output vocale",
    lblTtsModel: "Modello output vocale",
    lblTtsVoice: "Voce output",
    secText: "Testo",
    secImage: "Immagine",
    secImageGeneration: "Generazione",
    secImageVision: "Visione",
    secVideo: "Video",
    secAudio: "Audio",
    tabGeneral: "Generale",
    tabAdvanced: "Avanzate",
    tabMcp: "MCP",
    tabPixlwiz: "Pixlwiz",
    tabTools: "Tools",
    titleGeneral: "Impostazioni generali",
    titleAdvanced: "Avanzate",
    secSettingsFile: "File impostazioni",
    descSettingsFile: "Esporta o importa un singolo documento di impostazioni (JSON UTF-8 o PME1 crittografato su Windows).",
    secClipboard: "Appunti",
    descClipboard: "Copia o incolla il JSON completo delle impostazioni per condividerlo rapidamente.",
    secProfileArchive: "Archivio profilo",
    descProfileArchive: "Esporta o importa uno ZIP del profilo completo. Sono escluse le cartelle cache WebView e il file chiave di crittografia.",
    btnExportArchive: "Esporta archivio profilo",
    btnImportArchive: "Importa archivio profilo",
    msgImportArchiveConfirm: "Importare l'archivio profilo e sovrascrivere il profilo corrente?",
    lblDisplayLanguage: "Lingua",
    lblTheme: "Tema",
    lblFontSize: "Dimensione carattere",
    lblExplorerExtended: "Pannello Explorer: Esteso",
    lblEncryptExport: "Crittografa esportazione",
    btnExportSettings: "Esporta impostazioni",
    btnImportSettings: "Importa impostazioni",
    btnCopyToClipboard: "Copia negli appunti",
    btnPasteFromClipboard: "Incolla dagli appunti",
    msgImportConfirm: "Importare impostazioni da file e sovrascrivere i valori correnti?",
    errGeneralAction: "Azione impostazioni non riuscita.",
    optThemeSystem: "Sistema",
    optThemeLight: "Chiaro",
    optThemeDark: "Scuro",
    optLangEn: "Inglese",
    optLangEs: "Spagnolo",
    optLangDe: "Tedesco",
    optLangIt: "Italiano",
    optLangFr: "Francese",
    optFont0: "Piccolo",
    optFont1: "Compatto",
    optFont2: "Standard (Dimensione sistema)",
    optFont3: "Grande",
    optFont4: "Molto grande",
    presetSelect: "Seleziona preset...",
    presetName: "Nome preset",
    presetLoad: "Carica",
    presetSaveAs: "Salva come",
    presetUpdate: "Aggiorna",
    presetRename: "Rinomina",
    presetDelete: "Elimina",
    errPresetNameRequired: "Il nome del preset è obbligatorio.",
    errPresetNameExists: "Il nome del preset esiste già.",
    errPresetSelectUpdate: "Seleziona un preset da aggiornare.",
    errPresetSelectRename: "Seleziona un preset da rinominare.",
    errPresetSelectDelete: "Seleziona un preset da eliminare.",
    grpVoiceInput: "Input Vocale",
    grpVoiceOutput: "Output Vocale",
    errGeneralSave: "Salvataggio impostazioni generali non riuscito.",
    titleMcp: "Server MCP",
    mcpPath: "File profilo",
    mcpAddServer: "Aggiungi server",
    mcpRemoveServer: "Rimuovi",
    mcpServerName: "Nome server",
    mcpType: "Tipo",
    mcpTypeAuto: "Rileva automaticamente",
    mcpTypeStdio: "stdio",
    mcpTypeSse: "sse",
    mcpTypeStreamableHttp: "streamableHttp",
    mcpCommand: "Comando",
    mcpArgs: "Argomenti",
    mcpUrl: "URL",
    mcpHeaders: "Headers (JSON)",
    mcpEnv: "Variabili env (JSON)",
    mcpToolTimeout: "Timeout tool (s)",
    mcpEnabledTools: "Tool abilitati",
    mcpPing: "Ping",
    mcpPingResult: "Risultato ping",
    mcpEnabled: "Abilitato",
    errMcpSave: "Salvataggio impostazioni MCP non riuscito.",
    titlePixlwiz: "Pixlwiz",
    pixlwizAuthStatus: "Stato auth",
    pixlwizBudget: "Spesa",
    pixlwizLogin: "Login",
    pixlwizLogout: "Logout",
    pixlwizRefresh: "Aggiorna",
    pixlwizStatusFailed: "Stato non disponibile",
    pixlwizSignedOut: "Disconnesso",
    pixlwizSignedIn: "Connesso",
    titleTools: "Tools",
    toolsPathEnabled: "Strumenti path abilitati",
    toolsMcpMaster: "Abilita strumenti MCP globalmente",
    toolsDisabledMcpServers: "Server MCP disabilitati (virgola)",
  },
  fr: {
    tabProviders: "Fournisseurs",
    tabChat: "Chat",
    ok: "OK",
    cancel: "Annuler",
    titleProviders: "Paramètres fournisseurs",
    titleChat: "Paramètres fournisseur chat",
    lblRouter: "Routeur",
    lblModel: "Modèle",
    lblMaxIter: "Itér. max",
    lblImageProvider: "Fournisseur image",
    lblImageModel: "Modèle image",
    lblRecognitionProvider: "Fournisseur reconnaissance",
    lblRecognitionModel: "Modèle reconnaissance",
    lblVideoProvider: "Fournisseur vidéo",
    lblVideoModel: "Modèle vidéo",
    lblSttProvider: "Fournisseur entrée vocale",
    lblSttModel: "Modèle entrée vocale",
    lblTtsProvider: "Fournisseur sortie vocale",
    lblTtsModel: "Modèle sortie vocale",
    lblTtsVoice: "Voix sortie",
    secText: "Texte",
    secImage: "Image",
    secImageGeneration: "Génération",
    secImageVision: "Vision",
    secVideo: "Vidéo",
    secAudio: "Audio",
    tabGeneral: "Général",
    tabAdvanced: "Avancé",
    tabMcp: "MCP",
    tabPixlwiz: "Pixlwiz",
    tabTools: "Tools",
    titleGeneral: "Paramètres généraux",
    titleAdvanced: "Avancé",
    secSettingsFile: "Fichier de paramètres",
    descSettingsFile: "Exporter ou importer un document de paramètres (JSON UTF-8 ou PME1 chiffré sous Windows).",
    secClipboard: "Presse-papiers",
    descClipboard: "Copier ou coller le JSON complet des paramètres pour un partage rapide.",
    secProfileArchive: "Archive de profil",
    descProfileArchive: "Exporter ou importer une archive ZIP du profil complet. Les dossiers cache WebView et le fichier de clé de chiffrement sont exclus.",
    btnExportArchive: "Exporter l'archive de profil",
    btnImportArchive: "Importer l'archive de profil",
    msgImportArchiveConfirm: "Importer l'archive de profil et écraser le profil actuel ?",
    lblDisplayLanguage: "Langue",
    lblTheme: "Thème",
    lblFontSize: "Taille de police",
    lblExplorerExtended: "Panneau Explorer : Étendu",
    lblEncryptExport: "Chiffrer l'export",
    btnExportSettings: "Exporter les paramètres",
    btnImportSettings: "Importer les paramètres",
    btnCopyToClipboard: "Copier dans le presse-papiers",
    btnPasteFromClipboard: "Coller depuis le presse-papiers",
    msgImportConfirm: "Importer les paramètres depuis un fichier et écraser les valeurs actuelles ?",
    errGeneralAction: "Échec de l'action de paramètres.",
    optThemeSystem: "Système",
    optThemeLight: "Clair",
    optThemeDark: "Sombre",
    optLangEn: "Anglais",
    optLangEs: "Espagnol",
    optLangDe: "Allemand",
    optLangIt: "Italien",
    optLangFr: "Français",
    optFont0: "Petit",
    optFont1: "Compact",
    optFont2: "Standard (Taille système)",
    optFont3: "Grand",
    optFont4: "Très grand",
    presetSelect: "Sélectionner un preset...",
    presetName: "Nom du preset",
    presetLoad: "Charger",
    presetSaveAs: "Enregistrer sous",
    presetUpdate: "Mettre à jour",
    presetRename: "Renommer",
    presetDelete: "Supprimer",
    errPresetNameRequired: "Le nom du preset est requis.",
    errPresetNameExists: "Le nom du preset existe déjà.",
    errPresetSelectUpdate: "Sélectionnez un preset à mettre à jour.",
    errPresetSelectRename: "Sélectionnez un preset à renommer.",
    errPresetSelectDelete: "Sélectionnez un preset à supprimer.",
    grpVoiceInput: "Entrée Vocale",
    grpVoiceOutput: "Sortie Vocale",
    errGeneralSave: "Échec de l'enregistrement des paramètres généraux.",
    titleMcp: "Serveurs MCP",
    mcpPath: "Fichier profil",
    mcpAddServer: "Ajouter un serveur",
    mcpRemoveServer: "Supprimer",
    mcpServerName: "Nom du serveur",
    mcpType: "Type",
    mcpTypeAuto: "Détection auto",
    mcpTypeStdio: "stdio",
    mcpTypeSse: "sse",
    mcpTypeStreamableHttp: "streamableHttp",
    mcpCommand: "Commande",
    mcpArgs: "Args",
    mcpUrl: "URL",
    mcpHeaders: "Headers (JSON)",
    mcpEnv: "Variables env (JSON)",
    mcpToolTimeout: "Timeout outil (s)",
    mcpEnabledTools: "Outils activés",
    mcpPing: "Ping",
    mcpPingResult: "Résultat ping",
    mcpEnabled: "Activé",
    errMcpSave: "Échec de l'enregistrement des paramètres MCP.",
    titlePixlwiz: "Pixlwiz",
    pixlwizAuthStatus: "Statut auth",
    pixlwizBudget: "Dépense",
    pixlwizLogin: "Login",
    pixlwizLogout: "Logout",
    pixlwizRefresh: "Rafraîchir",
    pixlwizStatusFailed: "Statut indisponible",
    pixlwizSignedOut: "Déconnecté",
    pixlwizSignedIn: "Connecté",
    titleTools: "Tools",
    toolsPathEnabled: "Outils path activés",
    toolsMcpMaster: "Activer les outils MCP globalement",
    toolsDisabledMcpServers: "Serveurs MCP désactivés (virgules)",
  },
};

function rpc<T>(method: string, payload: Record<string, unknown> = {}): Promise<T> {
  if (!hasHostBridge()) {
    settingsLog("info", "rpc:dev", { method, payload: sanitizeRpcPayload(payload) });
    const local = devRpc<T>(method, payload);
    if (local) return local;
  }
  const id = nextRpcId();
  const started = Date.now();
  const timeoutMs = rpcTimeoutMs(method, payload);
  settingsLog("info", "rpc:start", { rpcId: id, method, timeoutMs, payload: sanitizeRpcPayload(payload) });
  return new Promise<T>((resolve, reject) => {
    const timer = window.setTimeout(() => {
      rpcPending.delete(id);
      settingsLog("error", "rpc:timeout", { rpcId: id, method, ms: Date.now() - started, timeoutMs });
      reject(new Error(`RPC timeout: ${method}`));
    }, timeoutMs);
    rpcPending.set(id, (msg) => {
      window.clearTimeout(timer);
      const ms = Date.now() - started;
      if (!msg.ok) {
        settingsLog("error", "rpc:error", { rpcId: id, method, ms, error: msg.error || `RPC error: ${method}` });
        reject(new Error(msg.error || `RPC error: ${method}`));
        return;
      }
      const data = msg.data as Record<string, unknown> | unknown[] | undefined;
      const summary: Record<string, unknown> = { rpcId: id, method, ms };
      if (Array.isArray(data)) summary.count = data.length;
      else if (data && typeof data === "object") {
        if (Array.isArray((data as { models?: unknown[] }).models))
          summary.models = (data as { models: unknown[] }).models.length;
        if (Array.isArray((data as { collections?: unknown[] }).collections))
          summary.collections = (data as { collections: unknown[] }).collections.length;
        if (Array.isArray((data as { providers?: unknown[] }).providers))
          summary.providers = (data as { providers: unknown[] }).providers.length;
      }
      settingsLog("info", "rpc:done", summary);
      resolve(msg.data as T);
    });
    hostPost({ kind: "providerRpc", rpcId: id, method, ...payload });
  });
}

function applyTheme(theme: "light" | "dark") {
  document.documentElement.classList.toggle("dark", theme === "dark");
}

function applyThemeFromGeneral(theme: 0 | 1 | 2) {
  if (theme === 1) applyTheme("light");
  else if (theme === 2) applyTheme("dark");
}

type HostMessageBridgeHandlers = {
  onPixlwizAuth?: (payload: PixlwizAuthPayload) => void;
};

function useHostMessageBridge(handlers: HostMessageBridgeHandlers = {}) {
  const handlersRef = useRef(handlers);
  useEffect(() => {
    handlersRef.current = handlers;
  }, [handlers]);

  useEffect(() => {
    const prev = (window as unknown as { cwWebOnHostMessage?: (msg: unknown) => void }).cwWebOnHostMessage;
    (window as unknown as { cwWebOnHostMessage?: (msg: unknown) => void }).cwWebOnHostMessage = (
      msg: unknown,
    ) => {
      const data = (msg && typeof msg === "object" ? msg : {}) as Record<string, unknown>;
      if (data.t === "cweb_theme") {
        const t = data.theme === "dark" ? "dark" : "light";
        applyTheme(t);
        return;
      }
      if (data.kind === "hostPixlwizAuth") {
        handlersRef.current.onPixlwizAuth?.(normalizePixlwizAuthPayload(data));
      }
      const hostReply = data as HostRpcReply;
      if (hostReply.kind === "hostProviderRpc" && hostReply.id != null) {
        const key = String(hostReply.id);
        const cb = rpcPending.get(key);
        if (cb) {
          rpcPending.delete(key);
          cb(hostReply);
        } else {
          settingsLog("warn", "rpc:orphan_reply", {
            rpcId: key,
            ok: hostReply.ok,
            error: hostReply.error,
          });
        }
      }
      if (typeof prev === "function") {
        prev(msg);
      }
    };
    return () => {
      (window as unknown as { cwWebOnHostMessage?: (msg: unknown) => void }).cwWebOnHostMessage = prev;
    };
  }, []);
}

function App() {
  const [tab, setTab] = useState<"general" | "advanced" | "providers" | "chat" | "commands" | "mcp" | "pixlwiz" | "tools" | "skills">("general");
  const appFeatures = useMemo(() => readAppFeatures(), []);
  const homeLlmToolsAvailable = featureEnabled(appFeatures, "FEATURE_HOME_LLM_TOOLS");
  const homeLlmSkillsAvailable = featureEnabled(appFeatures, "FEATURE_HOME_LLM_SKILLS");
  const mcpClientAvailable = homeLlmToolsAvailable && featureEnabled(appFeatures, "FEATURE_MCP_CLIENT");
  const customCommandsAvailable = featureEnabled(appFeatures, "FEATURE_CUSTOM_COMMANDS");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [rows, setRows] = useState<ProviderRow[]>([]);
  const [showKey, setShowKey] = useState<Record<string, boolean>>({});
  const [settingsPath, setSettingsPath] = useState("");
  const [chat, setChat] = useState<ChatSettings>({
    router: "openrouter",
    model: "",
    max_iterations: 8,
    image_provider: "google",
    image_model: "",
    image_recognition_provider: "google",
    image_recognition_model: "",
    video_provider: "google",
    video_model: "",
    stt_provider: "",
    stt_model: "",
    tts_provider: "",
    tts_model: "",
    tts_voice_id: "",
  });
  const [lang, setLang] = useState("en");
  const [general, setGeneral] = useState<GeneralSettings>(DEFAULT_GENERAL_SETTINGS);
  const [refreshingModelScopes, setRefreshingModelScopes] = useState<Record<ModelRefreshScope, boolean>>({
    router: false,
    image: false,
    vision: false,
    video: false,
  });
  const [encryptExport, setEncryptExport] = useState(true);
  const [recentByKey, setRecentByKey] = useState<RecentByKey>(() => readRecentByKey());
  const [runtimeProviderModels, setRuntimeProviderModels] = useState<Record<string, Option[]>>({});
  const [runtimeRouterModels, setRuntimeRouterModels] = useState<Record<string, Option[]>>({});
  const [replicateCollections, setReplicateCollections] = useState<ReplicateCollection[]>([]);
  const [replicateCollectionByScope, setReplicateCollectionByScope] = useState<Record<"image" | "vision" | "video", string>>({
    image: "official",
    vision: "official",
    video: "official",
  });
  const [chatPresets, setChatPresets] = useState<ChatPreset[]>(() => readChatPresets());
  const [selectedPresetId, setSelectedPresetId] = useState("");
  const [presetName, setPresetName] = useState("");
  const [customCommandsPath, setCustomCommandsPath] = useState("");
  const [customCommandsDoc, setCustomCommandsDoc] = useState<CustomCommandsDocument>({ version: 1, ribbon: { groups: [] } });
  const [tablerIcons, setTablerIcons] = useState<TablerIconOption[]>([]);
  const [cliCommands, setCliCommands] = useState<CliCommandOption[]>([]);
  const [appCommands, setAppCommands] = useState<AppCommandOption[]>([]);
  const [ribbonCommands, setRibbonCommands] = useState<RibbonCommandOption[]>([]);
  const [cliCommandSchemas, setCliCommandSchemas] = useState<Record<string, CliHelpSchema>>({});
  const [commandVariableOptions, setCommandVariableOptions] = useState<CommandVariableOption[]>([]);
  const [mcpJsonPath, setMcpJsonPath] = useState("");
  const [mcpServers, setMcpServers] = useState<McpServerForm[]>([]);
  const [mcpPingResult, setMcpPingResult] = useState("");
  const [expandedMcpServerIds, setExpandedMcpServerIds] = useState<Record<string, boolean>>({});
  const [pixlwiz, setPixlwiz] = useState<PixlwizGetReply | null>(null);
  const [providerOauth, setProviderOauth] = useState<Record<string, ProviderOAuthStatusReply>>({});
  const [tools, setTools] = useState<ToolsSettings>({
    disabled_path_tools: [],
    mcp_tools_enabled: true,
    disabled_mcp_servers: [],
  });
  const [allAgentToolIds, setAllAgentToolIds] = useState<string[]>(() => [...FALLBACK_AGENT_TOOL_IDS]);
  const [skillsSettings, setSkillsSettings] = useState<SkillsSettings>({
    enabled: true,
    roaming_enabled: true,
    workspace_enabled: true,
    pinned: [],
    disabled: [],
    roots: { roaming: "", workspace: "" },
    skills: [],
  });
  const [skillsSearch, setSkillsSearch] = useState("");
  const providerModelsInflight = useRef(new Map<string, Promise<void>>());
  const routerModelsInflight = useRef(new Map<string, Promise<void>>());
  const refreshScopeInflight = useRef(new Map<ModelRefreshScope, Promise<void>>());
  const tabLoadState = useRef({
    providers: false,
    chat: false,
    oauth: false,
    mcp: false,
    tools: false,
    skills: false,
    commands: false,
    pixlwiz: false,
  });
  const [chatDataReady, setChatDataReady] = useState(false);

  useHostMessageBridge();

  useEffect(() => {
    settingsLog("info", "app:ready", {
      hostBridge: hasHostBridge(),
      customCommands: customCommandsAvailable,
    });
  }, [customCommandsAvailable]);

  useEffect(() => {
    if (tab === "mcp" && !mcpClientAvailable) setTab("general");
    else if (tab === "tools" && !homeLlmToolsAvailable) setTab("general");
    else if (tab === "skills" && !homeLlmSkillsAvailable) setTab("general");
    else if (tab === "commands" && !customCommandsAvailable) setTab("general");
  }, [tab, mcpClientAvailable, homeLlmToolsAvailable, homeLlmSkillsAvailable, customCommandsAvailable]);

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
      postBeginMove();
    };

    document.addEventListener("mousedown", onMouseDown, true);
    return () => document.removeEventListener("mousedown", onMouseDown, true);
  }, []);

  useEffect(() => {
    let mounted = true;
    setBusy(true);
    rpc<GeneralGetReply>("settingsGeneralGet")
      .then((g) => {
        if (!mounted) return;
        const generalSettings = normalizeGeneralSettings(g.general);
        setGeneral(generalSettings);
        const l = (generalSettings.display_language || "en").toLowerCase();
        setLang(l);
        applyThemeFromGeneral(generalSettings.theme);
      })
      .catch((e) => {
        if (!mounted) return;
        setError(e instanceof Error ? e.message : String(e));
      })
      .finally(() => {
        if (mounted) setBusy(false);
      });
    return () => {
      mounted = false;
    };
  }, []);

  async function saveTools() {
    if (!homeLlmToolsAvailable) return;
    await rpc<unknown>("settingsToolsSave", tools);
  }

  async function saveSkills() {
    if (!homeLlmSkillsAvailable) return;
    await rpc<unknown>("settingsSkillsSave", {
      enabled: skillsSettings.enabled,
      roaming_enabled: skillsSettings.roaming_enabled,
      workspace_enabled: skillsSettings.workspace_enabled,
      pinned: skillsSettings.pinned,
      disabled: skillsSettings.disabled,
    });
  }

  async function saveSkillsWithState(next: SkillsSettings) {
    if (!homeLlmSkillsAvailable) return;
    await rpc<unknown>("settingsSkillsSave", {
      enabled: next.enabled,
      roaming_enabled: next.roaming_enabled,
      workspace_enabled: next.workspace_enabled,
      pinned: next.pinned,
      disabled: next.disabled,
    });
  }

  function updateSkillsAndPersist(updater: (s: SkillsSettings) => SkillsSettings) {
    setSkillsSettings((prev) => {
      const next = updater(prev);
      void saveSkillsWithState(next)
        .then(() => refreshSkills())
        .catch(() => {});
      return next;
    });
  }

  async function refreshSkills() {
    const s = await rpc<SkillsSettings>("settingsSkillsGet");
    setSkillsSettings({
      enabled: s.enabled !== false,
      roaming_enabled: s.roaming_enabled !== false,
      workspace_enabled: s.workspace_enabled !== false,
      pinned: Array.isArray(s.pinned) ? s.pinned : [],
      disabled: Array.isArray(s.disabled) ? s.disabled : [],
      roots: s.roots || { roaming: "", workspace: "" },
      skills: Array.isArray(s.skills) ? s.skills : [],
    });
  }

  function toggleSkillPinned(name: string, checked: boolean) {
    const key = normalizeSkillName(name);
    updateSkillsAndPersist((s) => {
      const set = new Set((s.pinned || []).map(normalizeSkillName));
      if (checked) set.add(key);
      else set.delete(key);
      return { ...s, pinned: Array.from(set) };
    });
  }

  function toggleSkillDisabled(name: string, checked: boolean) {
    const key = normalizeSkillName(name);
    updateSkillsAndPersist((s) => {
      const set = new Set((s.disabled || []).map(normalizeSkillName));
      if (checked) set.add(key);
      else set.delete(key);
      return { ...s, disabled: Array.from(set) };
    });
  }

  function onSkillsPinnedInputChange(value: string) {
    setSkillsSettings((s) => ({
      ...s,
      pinned: value
        .split(",")
        .map((x) => x.trim())
        .filter(Boolean),
    }));
  }

  function onSkillsDisabledInputChange(value: string) {
    setSkillsSettings((s) => ({
      ...s,
      disabled: value
        .split(",")
        .map((x) => x.trim())
        .filter(Boolean),
    }));
  }

  async function refreshPixlwizStatus() {
    setBusy(true);
    setError("");
    try {
      const data = await rpc<PixlwizGetReply>("settingsPixlwizGet");
      setPixlwiz(data);
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.pixlwizStatusFailed);
    } finally {
      setBusy(false);
    }
  }

  async function pixlwizLogin() {
    setBusy(true);
    setError("");
    try {
      await rpc<{ started: boolean }>("settingsPixlwizLogin");
      await refreshPixlwizStatus();
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.pixlwizStatusFailed);
      setBusy(false);
    }
  }

  async function pixlwizLogout() {
    setBusy(true);
    setError("");
    try {
      await rpc<unknown>("settingsPixlwizLogout");
      await refreshPixlwizStatus();
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.pixlwizStatusFailed);
      setBusy(false);
    }
  }

  const tr = I18N[lang] || I18N.en;
  const providerOptions = useMemo(() => rows.map((r) => ({ id: r.id, name: r.displayName })), [rows]);
  const modelsByProvider = useMemo(
    () =>
      rows.reduce<Record<string, string[]>>((acc, r) => {
        acc[r.id] = r.models || [];
        return acc;
      }, {}),
    [rows],
  );
  const routerProviderId = useMemo(() => {
    if (chat.router === "custom") return "";
    return chat.router;
  }, [chat.router]);
  const chatModelOptions = useMemo(() => {
    const dyn = runtimeRouterModels[chat.router] || [];
    if (dyn.length) return dyn;
    return (modelsByProvider[routerProviderId] || []).map((m) => ({ id: m, label: m }));
  }, [runtimeRouterModels, chat.router, modelsByProvider, routerProviderId]);
  const imageModelOptions = useMemo(() => {
    const dyn = runtimeProviderModels[chat.image_provider] || [];
    if (dyn.length) return dyn;
    return (modelsByProvider[chat.image_provider] || []).map((m) => ({ id: m, label: m }));
  }, [runtimeProviderModels, chat.image_provider, modelsByProvider]);
  const visionModelOptions = useMemo(
    () => {
      const dyn = runtimeProviderModels[chat.image_recognition_provider] || [];
      if (dyn.length) return dyn;
      return (modelsByProvider[chat.image_recognition_provider] || []).map((m) => ({ id: m, label: m }));
    },
    [runtimeProviderModels, modelsByProvider, chat.image_recognition_provider],
  );
  const videoModelOptions = useMemo(() => {
    const dyn = runtimeProviderModels[chat.video_provider] || [];
    if (dyn.length) return dyn;
    return (modelsByProvider[chat.video_provider] || []).map((m) => ({ id: m, label: m }));
  }, [runtimeProviderModels, modelsByProvider, chat.video_provider]);
  const sttModelOptions = useMemo(() => STT_MODEL_OPTIONS[chat.stt_provider] || STT_MODEL_OPTIONS.pixlwiz, [chat.stt_provider]);
  const ttsModelOptions = useMemo(() => TTS_MODEL_OPTIONS[chat.tts_provider] || TTS_MODEL_OPTIONS.pixlwiz, [chat.tts_provider]);
  const customCommandProviderOptions = useMemo<CustomCommandOption[]>(
    () => rows.map((r) => ({ id: r.id, label: r.displayName || r.id })),
    [rows],
  );
  const customCommandRouterOptions = useMemo<CustomCommandOption[]>(
    () => ROUTER_OPTIONS.map((r) => ({ id: r.id, label: r.label || r.id })),
    [],
  );
  const customCommandModelOptionsByProvider = useMemo<Record<string, CustomCommandOption[]>>(() => {
    const out: Record<string, CustomCommandOption[]> = {};
    for (const row of rows) {
      const dyn = runtimeProviderModels[row.id] || [];
      out[row.id] = dyn.length
        ? dyn.map((model) => ({ id: model.id, label: model.label || model.id }))
        : (modelsByProvider[row.id] || []).map((model) => ({ id: model, label: model }));
    }
    return out;
  }, [rows, runtimeProviderModels, modelsByProvider]);
  const customCommandScopedModelOptionsByProvider = useMemo<Partial<Record<CustomCommandProviderScope, Record<string, CustomCommandOption[]>>>>(() => {
    const withPixlwizModels = (models: string[]) => ({
      ...customCommandModelOptionsByProvider,
      pixlwiz: models.map((model) => ({ id: model, label: model })),
    });
    return {
      image: withPixlwizModels(PIXLWIZ_IMAGE_MODELS),
      vision: withPixlwizModels(PIXLWIZ_IMAGE_RECOG_MODELS),
      video: withPixlwizModels(PIXLWIZ_VIDEO_MODELS),
    };
  }, [customCommandModelOptionsByProvider]);
  const customCommandRouterModelOptionsByProvider = useMemo<Record<string, CustomCommandOption[]>>(() => {
    const out: Record<string, CustomCommandOption[]> = {};
    for (const router of ROUTER_OPTIONS) {
      const dyn = runtimeRouterModels[router.id] || [];
      out[router.id] = dyn.length
        ? dyn.map((model) => ({ id: model.id, label: model.label || model.id }))
        : (modelsByProvider[router.id] || []).map((model) => ({ id: model, label: model }));
    }
    out.pixlwiz = PIXLWIZ_TEXT_MODELS.map((model) => ({ id: model, label: model }));
    return out;
  }, [runtimeRouterModels, modelsByProvider]);
  const customCommandProviderDefaults = useMemo<CustomCommandProviderDefaults>(() => {
    const providerLabel = (providerId: string) => rows.find((row) => row.id === providerId)?.displayName || providerId;
    const routerLabel = (routerId: string) => ROUTER_OPTIONS.find((row) => row.id === routerId)?.label || routerId;
    return {
      image: {
        providerId: chat.image_provider,
        modelId: chat.image_model,
        providerLabel: providerLabel(chat.image_provider),
        modelLabel: chat.image_model,
      },
      vision: {
        providerId: chat.image_recognition_provider,
        modelId: chat.image_recognition_model,
        providerLabel: providerLabel(chat.image_recognition_provider),
        modelLabel: chat.image_recognition_model,
      },
      video: {
        providerId: chat.video_provider,
        modelId: chat.video_model,
        providerLabel: providerLabel(chat.video_provider),
        modelLabel: chat.video_model,
      },
      llm: {
        providerId: chat.router,
        modelId: chat.model,
        providerLabel: routerLabel(chat.router),
        modelLabel: chat.model,
      },
    };
  }, [rows, chat.image_provider, chat.image_model, chat.image_recognition_provider, chat.image_recognition_model, chat.video_provider, chat.video_model, chat.router, chat.model]);
  const recentKeyChatModel = useMemo(() => makeRecentKey("chatModel", chat.router || "custom"), [chat.router]);
  const recentKeyImageModel = useMemo(() => makeRecentKey("imageModel", chat.image_provider), [chat.image_provider]);
  const recentKeyVisionModel = useMemo(
    () => makeRecentKey("visionModel", chat.image_recognition_provider),
    [chat.image_recognition_provider],
  );
  const recentKeyVideoModel = useMemo(() => makeRecentKey("videoModel", chat.video_provider), [chat.video_provider]);
  const recentKeySttModel = useMemo(() => makeRecentKey("sttModel", chat.stt_provider), [chat.stt_provider]);
  const recentKeyTtsModel = useMemo(() => makeRecentKey("ttsModel", chat.tts_provider), [chat.tts_provider]);
  const recentKeyTtsVoice = useMemo(() => makeRecentKey("ttsVoice", chat.tts_provider), [chat.tts_provider]);
  const chatModelTypeahead = useMemo(
    () => mergeRecentOptions(chatModelOptions, recentByKey[recentKeyChatModel] || []),
    [chatModelOptions, recentByKey, recentKeyChatModel],
  );
  const imageModelTypeahead = useMemo(
    () => mergeRecentOptions(imageModelOptions, recentByKey[recentKeyImageModel] || []),
    [imageModelOptions, recentByKey, recentKeyImageModel],
  );
  const visionModelTypeahead = useMemo(
    () => mergeRecentOptions(visionModelOptions, recentByKey[recentKeyVisionModel] || []),
    [visionModelOptions, recentByKey, recentKeyVisionModel],
  );
  const videoModelTypeahead = useMemo(
    () => mergeRecentOptions(videoModelOptions, recentByKey[recentKeyVideoModel] || []),
    [videoModelOptions, recentByKey, recentKeyVideoModel],
  );
  const sttModelTypeahead = useMemo(
    () => mergeRecentOptions(sttModelOptions, recentByKey[recentKeySttModel] || []),
    [sttModelOptions, recentByKey, recentKeySttModel],
  );
  const ttsModelTypeahead = useMemo(
    () => mergeRecentOptions(ttsModelOptions, recentByKey[recentKeyTtsModel] || []),
    [ttsModelOptions, recentByKey, recentKeyTtsModel],
  );
  const ttsVoiceTypeahead = useMemo(
    () => mergeRecentOptions(ELEVENLABS_VOICE_OPTIONS, recentByKey[recentKeyTtsVoice] || []),
    [recentByKey, recentKeyTtsVoice],
  );

  function onProviderField(id: string, field: "apiKey" | "baseUrl", value: string) {
    setRows((prev) => prev.map((r) => (r.id === id ? { ...r, [field]: value } : r)));
  }

  function onChatField<K extends keyof ChatSettings>(key: K, value: ChatSettings[K]) {
    setChat((prev) => ({ ...prev, [key]: value }));
  }

  async function fetchProviderModels(providerId: string, force = false, collectionSlugOverride?: string) {
    if (!providerId) return;
    const scope = providerId === chat.image_provider
      ? "image"
      : providerId === chat.image_recognition_provider
        ? "vision"
        : providerId === chat.video_provider
          ? "video"
          : undefined;
    const collectionSlug = collectionSlugOverride
      ?? (providerId === "replicate" && scope ? replicateCollectionByScope[scope] : undefined);
    const inflightKey = providerModelsFetchKey(providerId, force, collectionSlug);
    const existing = providerModelsInflight.current.get(inflightKey);
    if (existing) {
      settingsLog("info", "fetchProviderModels:dedup", { inflightKey, providerId, force, collectionSlug });
      return existing;
    }
    settingsLog("info", "fetchProviderModels:start", { inflightKey, providerId, force, collectionSlug });
    const task = (async () => {
      try {
        const data = await rpc<ProviderModelsGetReply>("settingsProviderModelsGet", {
          providerId,
          forceRefresh: force,
          ...(collectionSlug ? { collectionSlug } : {}),
        });
        setRuntimeProviderModels((prev) => ({ ...prev, [providerId]: data.models || [] }));
        settingsLog("info", "fetchProviderModels:done", {
          inflightKey,
          providerId,
          models: (data.models || []).length,
        });
      } catch (e) {
        settingsLog("error", "fetchProviderModels:error", {
          inflightKey,
          providerId,
          error: e instanceof Error ? e.message : String(e),
        });
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        providerModelsInflight.current.delete(inflightKey);
      }
    })();
    providerModelsInflight.current.set(inflightKey, task);
    return task;
  }

  async function fetchRouterModels(routerId: string, force = false) {
    if (!routerId) return;
    const inflightKey = `${routerId}:${force ? "1" : "0"}`;
    const existing = routerModelsInflight.current.get(inflightKey);
    if (existing) {
      settingsLog("info", "fetchRouterModels:dedup", { inflightKey, routerId, force });
      return existing;
    }
    settingsLog("info", "fetchRouterModels:start", { inflightKey, routerId, force });
    const task = (async () => {
      try {
        const data = await rpc<RouterModelsGetReply>("settingsRouterModelsGet", { routerId, forceRefresh: force });
        setRuntimeRouterModels((prev) => ({ ...prev, [routerId]: data.models || [] }));
        settingsLog("info", "fetchRouterModels:done", {
          inflightKey,
          routerId,
          models: (data.models || []).length,
        });
      } catch (e) {
        settingsLog("error", "fetchRouterModels:error", {
          inflightKey,
          routerId,
          error: e instanceof Error ? e.message : String(e),
        });
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        routerModelsInflight.current.delete(inflightKey);
      }
    })();
    routerModelsInflight.current.set(inflightKey, task);
    return task;
  }

  async function refreshModelScope(scope: ModelRefreshScope) {
    const existing = refreshScopeInflight.current.get(scope);
    if (existing) {
      settingsLog("info", "refreshModelScope:dedup", { scope });
      return existing;
    }
    const refreshStarted = Date.now();
    settingsLog("info", "refreshModelScope:start", { scope });
    const task = (async () => {
      setRefreshingModelScopes((prev) => ({ ...prev, [scope]: true }));
      setError("");
      try {
        if (scope === "router") {
          if (chat.router) await fetchRouterModels(chat.router, true);
        } else {
          const providerId = providerIdForModelScope(scope, chat);
          if (!providerId) return;
          const collectionSlug = providerId === "replicate" ? replicateCollectionByScope[scope] : undefined;
          await fetchProviderModels(providerId, true, collectionSlug);
        }
        settingsLog("info", "refreshModelScope:done", { scope, ms: Date.now() - refreshStarted });
      } catch (e) {
        settingsLog("error", "refreshModelScope:error", {
          scope,
          ms: Date.now() - refreshStarted,
          error: e instanceof Error ? e.message : String(e),
        });
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        setRefreshingModelScopes((prev) => ({ ...prev, [scope]: false }));
      }
    })();
    refreshScopeInflight.current.set(scope, task);
    try {
      await task;
    } finally {
      refreshScopeInflight.current.delete(scope);
    }
  }

  function ModelRefreshButton({ scope }: { scope: ModelRefreshScope }) {
    const busy = refreshingModelScopes[scope];
    return (
      <button
        className={`btn pm-refresh-btn${busy ? " is-spinning" : ""}`}
        type="button"
        onClick={() => void refreshModelScope(scope)}
        disabled={busy}
        aria-busy={busy}
        title={busy ? "Refreshing models…" : "Refresh models"}
      >
        ↻
      </button>
    );
  }

  async function refreshProviderOAuth(providerId: string) {
    try {
      const data = await rpc<ProviderOAuthStatusReply>("settingsProviderOAuthGet", { providerId });
      setProviderOauth((prev) => ({ ...prev, [providerId]: data }));
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  async function loginProviderOAuth(providerId: string) {
    try {
      await rpc<{ message?: string }>("settingsProviderOAuthLogin", { providerId });
      await refreshProviderOAuth(providerId);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  async function logoutProviderOAuth(providerId: string) {
    try {
      await rpc<unknown>("settingsProviderOAuthLogout", { providerId });
      await refreshProviderOAuth(providerId);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  async function fetchReplicateCollections(force = false) {
    settingsLog("info", "fetchReplicateCollections:start", { force });
    try {
      const data = await rpc<ReplicateCollectionsReply>("settingsReplicateCollectionsGet", { forceRefresh: force });
      const cols = (data.collections || []).filter((c) => !!c.slug);
      setReplicateCollections(cols);
      settingsLog("info", "fetchReplicateCollections:done", { force, collections: cols.length });
    } catch (e) {
      settingsLog("error", "fetchReplicateCollections:error", {
        force,
        error: e instanceof Error ? e.message : String(e),
      });
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  async function resolveReplicateCollectionFromModel(scope: "image" | "vision" | "video", modelSlug: string) {
    if (!modelSlug) return;
    try {
      const data = await rpc<ReplicateResolveCollectionReply>("settingsReplicateResolveCollection", { modelSlug });
      if (!data.collection) return;
      setReplicateCollectionByScope((prev) => ({ ...prev, [scope]: data.collection || prev[scope] }));
    } catch {
      // best effort
    }
  }

  function onReplicateCollectionChange(scope: "image" | "vision" | "video", slug: string) {
    setReplicateCollectionByScope((prev) => ({ ...prev, [scope]: slug || "official" }));
    const providerId = scope === "image" ? chat.image_provider : scope === "vision" ? chat.image_recognition_provider : chat.video_provider;
    if (providerId === "replicate") {
      void fetchProviderModels(providerId, true);
    }
  }

  function resetTabLoadState() {
    tabLoadState.current = {
      providers: false,
      chat: false,
      oauth: false,
      mcp: false,
      tools: false,
      skills: false,
      commands: false,
      pixlwiz: false,
    };
    setChatDataReady(false);
  }

  async function ensureProvidersLoaded() {
    if (tabLoadState.current.providers) return;
    const p = await rpc<ProvidersGetReply>("settingsProvidersGet");
    setRows(p.providers || []);
    setSettingsPath(p.settingsPath || "");
    tabLoadState.current.providers = true;
  }

  async function ensureChatLoaded(): Promise<ChatSettings | undefined> {
    if (tabLoadState.current.chat) return chat;
    const [c, cp] = await Promise.all([
      rpc<ChatGetReply>("settingsChatGet"),
      rpc<ChatPresetsGetReply>("settingsChatPresetsGet").catch(() => ({ presets: readChatPresets() })),
    ]);
    setChat(c.chat);
    setChatPresets(cp.presets || []);
    const l = (c.displayLanguage || "").toLowerCase();
    if (l) setLang(l);
    tabLoadState.current.chat = true;
    setChatDataReady(true);
    return c.chat;
  }

  async function ensureOauthLoaded() {
    if (tabLoadState.current.oauth) return;
    tabLoadState.current.oauth = true;
    await Promise.all([
      refreshProviderOAuth("openrouter"),
      refreshProviderOAuth("openai"),
    ]);
  }

  async function ensureMcpLoaded() {
    if (!mcpClientAvailable || tabLoadState.current.mcp) return;
    tabLoadState.current.mcp = true;
    try {
      const m = await rpc<McpGetReply>("settingsMcpGet");
      setMcpJsonPath(m.mcpJsonPath || "");
      const entries = Object.entries(m.mcpServers || {});
      const forms = entries.map(([name, cfg]) => mcpServerConfigToForm(name, cfg));
      setMcpServers(forms.length ? forms : [makeMcpServerForm()]);
    } catch {
      setMcpServers((prev) => (prev.length ? prev : [makeMcpServerForm()]));
    }
  }

  async function ensureToolsLoaded() {
    if (!homeLlmToolsAvailable || tabLoadState.current.tools) return;
    tabLoadState.current.tools = true;
    const [t, toolIds] = await Promise.all([
      rpc<ToolsSettings>("settingsToolsGet"),
      fetchHostPathToolIds(rpc, FALLBACK_AGENT_TOOL_IDS),
    ]);
    setAllAgentToolIds(toolIds);
    setTools({
      disabled_path_tools: Array.isArray(t.disabled_path_tools) ? t.disabled_path_tools : [],
      mcp_tools_enabled: t.mcp_tools_enabled !== false,
      disabled_mcp_servers: Array.isArray(t.disabled_mcp_servers) ? t.disabled_mcp_servers : [],
    });
  }

  async function ensureSkillsLoaded() {
    if (!homeLlmSkillsAvailable || tabLoadState.current.skills) return;
    tabLoadState.current.skills = true;
    const s = await rpc<SkillsSettings>("settingsSkillsGet");
    setSkillsSettings({
      enabled: s.enabled !== false,
      roaming_enabled: s.roaming_enabled !== false,
      workspace_enabled: s.workspace_enabled !== false,
      pinned: Array.isArray(s.pinned) ? s.pinned : [],
      disabled: Array.isArray(s.disabled) ? s.disabled : [],
      roots: s.roots || { roaming: "", workspace: "" },
      skills: Array.isArray(s.skills) ? s.skills : [],
    });
  }

  async function ensurePixlwizLoaded() {
    if (tabLoadState.current.pixlwiz) return;
    tabLoadState.current.pixlwiz = true;
    await refreshPixlwizStatus();
  }

  async function ensureCommandsLoaded() {
    if (!customCommandsAvailable || tabLoadState.current.commands) return;
    settingsLog("info", "commandsTab:load:start");
    const [data, icons, variables] = await Promise.all([
      rpc<CustomCommandsGetReply>("settingsCustomCommandsGet"),
      rpc<TablerIconsGetReply>("settingsTablerIconsGet").catch(() => ({ icons: [] as TablerIconOption[] })),
      rpc<CommandVariablesGetReply>("settingsCommandVariablesGet").catch(() => ({ variables: [] as CommandVariableOption[] })),
    ]);
    setCustomCommandsPath(data.commandsPath || "");
    setCustomCommandsDoc(data.document || { version: 1, ribbon: { groups: [] } });
    if (Array.isArray(data.variables) && data.variables.length) setCommandVariableOptions(data.variables);
    else if (Array.isArray(variables.variables) && variables.variables.length) setCommandVariableOptions(variables.variables);
    setTablerIcons(Array.isArray(icons.icons) ? icons.icons : []);
    const cliData = await rpc<CliCommandsGetReply>("settingsCliCommandsGet");
    const commands = Array.isArray(cliData.commands) ? cliData.commands : [];
    setCliCommands(commands);
    const appData = await fetchSettingsAppCommands((method) => rpc<AppCommandsGetReply>(method));
    setAppCommands(appData);
    const ribbonData = await fetchSettingsRibbonCommands((method) => rpc<RibbonCommandsGetReply>(method));
    setRibbonCommands(ribbonData);
    const entries = await Promise.all(
      commands
        .filter((command) => command.available !== false)
        .map(async (command) => {
          try {
            return await loadCliSchemaTree(command.id);
          } catch {
            return [];
          }
        }),
    );
    setCliCommandSchemas(Object.fromEntries(entries.flat()));
    tabLoadState.current.commands = true;
    settingsLog("info", "commandsTab:load:done", { commands: commands.length });
  }

  async function loadTabData(activeTab: typeof tab, opts?: { force?: boolean }) {
    if (opts?.force) resetTabLoadState();
    settingsLog("info", "tab:load", { tab: activeTab, force: !!opts?.force });
    try {
      switch (activeTab) {
        case "providers":
          await ensureProvidersLoaded();
          await ensureOauthLoaded();
          break;
        case "chat":
          await ensureProvidersLoaded();
          await ensureChatLoaded();
          break;
        case "commands":
          await ensureProvidersLoaded();
          await ensureChatLoaded();
          await ensureCommandsLoaded();
          break;
        case "mcp":
          await ensureMcpLoaded();
          break;
        case "tools":
          await ensureToolsLoaded();
          break;
        case "skills":
          await ensureSkillsLoaded();
          break;
        case "pixlwiz":
          await ensurePixlwizLoaded();
          break;
        default:
          break;
      }
    } catch (e) {
      settingsLog("error", "tab:load:error", {
        tab: activeTab,
        error: e instanceof Error ? e.message : String(e),
      });
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  useEffect(() => {
    void loadTabData(tab);
  }, [tab, mcpClientAvailable, homeLlmToolsAvailable, homeLlmSkillsAvailable, customCommandsAvailable]);

  useEffect(() => {
    if (!chatDataReady || (tab !== "chat" && tab !== "commands")) return;
    void fetchRouterModels(chat.router, false);
  }, [chatDataReady, tab, chat.router]);

  useEffect(() => {
    if (!chatDataReady || (tab !== "chat" && tab !== "commands")) return;
    void fetchProviderModels(chat.image_provider, false);
  }, [chatDataReady, tab, chat.image_provider]);

  useEffect(() => {
    if (!chatDataReady || (tab !== "chat" && tab !== "commands")) return;
    void fetchProviderModels(chat.image_recognition_provider, false);
  }, [chatDataReady, tab, chat.image_recognition_provider]);

  useEffect(() => {
    if (!chatDataReady || (tab !== "chat" && tab !== "commands")) return;
    void fetchProviderModels(chat.video_provider, false);
  }, [chatDataReady, tab, chat.video_provider]);

  useEffect(() => {
    if (!chatDataReady || (tab !== "chat" && tab !== "commands")) return;
    if (chat.image_provider === "replicate" || chat.image_recognition_provider === "replicate" || chat.video_provider === "replicate") {
      void fetchReplicateCollections(false);
    }
  }, [chatDataReady, tab, chat.image_provider, chat.image_recognition_provider, chat.video_provider]);

  async function saveProvidersAndClose() {
    setBusy(true);
    setError("");
    try {
      await rpc("settingsProvidersSave", {
        providers: rows.map((r) => ({ id: r.id, apiKey: r.apiKey, baseUrl: r.baseUrl })),
      });
      hostPost({ t: "cweb_close" });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function saveGeneralAndClose() {
    setBusy(true);
    setError("");
    try {
      await rpc("settingsGeneralSave", general as unknown as Record<string, unknown>);
      hostPost({ t: "cweb_close" });
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralSave);
    } finally {
      setBusy(false);
    }
  }

  async function reloadGeneral() {
    const g = await rpc<GeneralGetReply>("settingsGeneralGet");
    setGeneral(normalizeGeneralSettings(g.general));
  }

  async function reloadProfileFromDisk() {
    resetTabLoadState();
    await reloadGeneral();
    await loadTabData(tab, { force: true });
  }

  async function onExportSettings() {
    setBusy(true);
    setError("");
    try {
      await rpc<GeneralActionReply>("settingsGeneralExport", { encrypted: encryptExport });
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralAction);
    } finally {
      setBusy(false);
    }
  }

  async function onImportSettings() {
    if (!window.confirm(tr.msgImportConfirm)) return;
    setBusy(true);
    setError("");
    try {
      const res = await rpc<GeneralActionReply>("settingsGeneralImport");
      if (!res.cancelled) {
        await reloadProfileFromDisk();
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralAction);
    } finally {
      setBusy(false);
    }
  }

  async function onCopySettingsToClipboard() {
    setBusy(true);
    setError("");
    try {
      await rpc("settingsGeneralClipboardCopy");
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralAction);
    } finally {
      setBusy(false);
    }
  }

  async function onPasteSettingsFromClipboard() {
    if (!window.confirm(tr.msgImportConfirm)) return;
    setBusy(true);
    setError("");
    try {
      await rpc("settingsGeneralClipboardPaste");
      await reloadProfileFromDisk();
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralAction);
    } finally {
      setBusy(false);
    }
  }

  async function onExportArchive() {
    setBusy(true);
    setError("");
    try {
      await rpc<GeneralActionReply>("settingsGeneralExportArchive");
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralAction);
    } finally {
      setBusy(false);
    }
  }

  async function onImportArchive() {
    if (!window.confirm(tr.msgImportArchiveConfirm)) return;
    setBusy(true);
    setError("");
    try {
      const res = await rpc<GeneralActionReply>("settingsGeneralImportArchive");
      if (!res.cancelled) {
        await reloadProfileFromDisk();
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errGeneralAction);
    } finally {
      setBusy(false);
    }
  }

  async function saveChatAndClose() {
    setBusy(true);
    setError("");
    try {
      setRecentByKey((prev) => {
        const next: RecentByKey = { ...prev };
        pushRecentValue(next, makeRecentKey("chatModel", chat.router || "custom"), chat.model);
        pushRecentValue(next, makeRecentKey("imageModel", chat.image_provider), chat.image_model);
        pushRecentValue(next, makeRecentKey("visionModel", chat.image_recognition_provider), chat.image_recognition_model);
        pushRecentValue(next, makeRecentKey("videoModel", chat.video_provider), chat.video_model);
        pushRecentValue(next, makeRecentKey("sttModel", chat.stt_provider), chat.stt_model);
        pushRecentValue(next, makeRecentKey("ttsModel", chat.tts_provider), chat.tts_model);
        pushRecentValue(next, makeRecentKey("ttsVoice", chat.tts_provider), chat.tts_voice_id);
        writeRecentByKey(next);
        return next;
      });
      await rpc("settingsChatSave", chat as unknown as Record<string, unknown>);
      hostPost({ t: "cweb_close" });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function saveMcpAndClose() {
    if (!mcpClientAvailable) {
      hostPost({ t: "cweb_close" });
      return;
    }
    setBusy(true);
    setError("");
    try {
      const mcpServersOut: Record<string, McpServerConfig> = {};
      for (const s of mcpServers) {
        const key = s.name.trim();
        if (!key) continue;
        mcpServersOut[key] = mcpServerFormToConfig(s);
      }
      await rpc("settingsMcpSave", { mcpServers: mcpServersOut });
      hostPost({ t: "cweb_close" });
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errMcpSave);
    } finally {
      setBusy(false);
    }
  }

  async function reloadCustomCommands() {
    if (!customCommandsAvailable) return;
    setBusy(true);
    setError("");
    try {
      const data = await rpc<CustomCommandsGetReply>("settingsCustomCommandsGet");
      setCustomCommandsPath(data.commandsPath || "");
      setCustomCommandsDoc(data.document || { version: 1, ribbon: { groups: [] } });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  async function pickNativePath(kind: "file" | "folder") {
    const picked = await rpc<NativePathPickReply>("settingsNativePathPick", { pickKind: kind });
    if (picked.cancelled || !picked.path) return undefined;
    return picked.path;
  }

  async function saveCustomCommandsAndClose() {
    if (!customCommandsAvailable) {
      hostPost({ t: "cweb_close" });
      return;
    }
    setBusy(true);
    setError("");
    try {
      const data = await rpc<{ commandsPath?: string }>("settingsCustomCommandsSave", { document: customCommandsDoc });
      if (data.commandsPath) setCustomCommandsPath(data.commandsPath);
      hostPost({ t: "cweb_close" });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  function onMcpServerField(id: string, key: keyof McpServerForm, value: string) {
    setMcpServers((prev) => prev.map((s) => (s.id === id ? { ...s, [key]: value } : s)));
  }

  function onMcpServerEnabled(id: string, enabled: boolean) {
    setMcpServers((prev) => prev.map((s) => (s.id === id ? { ...s, enabled } : s)));
  }

  function addMcpServer() {
    setMcpServers((prev) => [...prev, makeMcpServerForm()]);
  }

  function removeMcpServer(id: string) {
    setMcpServers((prev) => {
      const next = prev.filter((s) => s.id !== id);
      return next.length ? next : [makeMcpServerForm()];
    });
  }

  function isMcpExpanded(id: string): boolean {
    return !!expandedMcpServerIds[id];
  }

  function toggleMcpExpanded(id: string) {
    setExpandedMcpServerIds((prev) => ({ ...prev, [id]: !prev[id] }));
  }

  async function pingMcp() {
    if (!mcpClientAvailable) return;
    setBusy(true);
    setError("");
    try {
      const out = await rpc<McpPingReply>("settingsMcpPing");
      setMcpPingResult(JSON.stringify(out.probe, null, 2));
    } catch (e) {
      setError(e instanceof Error ? e.message : tr.errMcpSave);
    } finally {
      setBusy(false);
    }
  }

  async function persistChatPresets(next: ChatPreset[]) {
    setChatPresets(next);
    writeChatPresets(next); // fallback cache for offline/older host behavior
    try {
      await rpc("settingsChatPresetsSave", { presets: next });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  async function saveAsPreset() {
    const name = presetName.trim();
    if (!name) {
      setError(tr.errPresetNameRequired);
      return;
    }
    const exists = chatPresets.some((p) => p.name.toLowerCase() === name.toLowerCase());
    if (exists) {
      setError(tr.errPresetNameExists);
      return;
    }
    const now = Date.now();
    const preset: ChatPreset = {
      id: `chat-preset-${now}`,
      name,
      chat: { ...chat },
      createdAt: now,
      updatedAt: now,
    };
    const next = [...chatPresets, preset];
    await persistChatPresets(next);
    setSelectedPresetId(preset.id);
    setError("");
  }

  function loadPreset() {
    const p = chatPresets.find((x) => x.id === selectedPresetId);
    if (!p) return;
    setChat({ ...p.chat });
    setPresetName(p.name);
    setError("");
  }

  async function updatePreset() {
    if (!selectedPresetId) {
      setError(tr.errPresetSelectUpdate);
      return;
    }
    const next = chatPresets.map((p) => (p.id === selectedPresetId ? { ...p, chat: { ...chat }, updatedAt: Date.now() } : p));
    await persistChatPresets(next);
    setError("");
  }

  async function renamePreset() {
    const name = presetName.trim();
    if (!selectedPresetId) {
      setError(tr.errPresetSelectRename);
      return;
    }
    if (!name) {
      setError(tr.errPresetNameRequired);
      return;
    }
    const next = chatPresets.map((p) => (p.id === selectedPresetId ? { ...p, name, updatedAt: Date.now() } : p));
    await persistChatPresets(next);
    setError("");
  }

  async function deletePreset() {
    if (!selectedPresetId) {
      setError(tr.errPresetSelectDelete);
      return;
    }
    const next = chatPresets.filter((p) => p.id !== selectedPresetId);
    await persistChatPresets(next);
    setSelectedPresetId("");
    setPresetName("");
    setError("");
  }

  async function onOk() {
    if (tab === "general") {
      await saveGeneralAndClose();
      return;
    }
    if (tab === "advanced") {
      hostPost({ t: "cweb_close" });
      return;
    }
    if (tab === "tools") {
      await saveTools();
      hostPost({ t: "cweb_close" });
      return;
    }
    if (tab === "skills") {
      await saveSkills();
      hostPost({ t: "cweb_close" });
      return;
    }
    if (tab === "providers") {
      await saveProvidersAndClose();
      return;
    }
    if (tab === "mcp") {
      await saveMcpAndClose();
      return;
    }
    if (tab === "commands") {
      await saveCustomCommandsAndClose();
      return;
    }
    if (tab === "pixlwiz") {
      hostPost({ t: "cweb_close" });
      return;
    }
    await saveChatAndClose();
  }

  return (
    <div className="pm-window">
      <div className="pm-drag-strip">
        <button
          className="pm-drag-title"
          type="button"
          onMouseDown={(e) => {
            if (e.button !== 0) return;
            e.preventDefault();
            postBeginMove();
          }}
          onDoubleClick={() => postWindowCommand("cweb_max")}
          aria-label="Move Settings window"
        >
          Settings
        </button>
        <div className="pm-window-controls" aria-label="Window controls">
          <button type="button" title="Minimize" aria-label="Minimize" onClick={() => postWindowCommand("cweb_min")}>
            &minus;
          </button>
          <button type="button" title="Maximize or restore" aria-label="Maximize or restore" onClick={() => postWindowCommand("cweb_max")}>
            □
          </button>
          <button className="close" type="button" title="Close" aria-label="Close" onClick={() => postWindowCommand("cweb_close")}>
            ×
          </button>
        </div>
      </div>
      <div className="pm-settings-shell">
        <aside className="pm-sidebar">
          <button type="button" className={`pm-side-item ${tab === "general" ? "active" : ""}`} onClick={() => setTab("general")}>
            {tr.tabGeneral}
          </button>
          {mcpClientAvailable ? (
            <button type="button" className={`pm-side-item ${tab === "mcp" ? "active" : ""}`} onClick={() => setTab("mcp")}>
              {tr.tabMcp}
            </button>
          ) : null}
          {homeLlmToolsAvailable ? (
            <button type="button" className={`pm-side-item ${tab === "tools" ? "active" : ""}`} onClick={() => setTab("tools")}>
              {tr.tabTools}
            </button>
          ) : null}
          {homeLlmSkillsAvailable ? (
            <button type="button" className={`pm-side-item ${tab === "skills" ? "active" : ""}`} onClick={() => setTab("skills")}>
              Skills
            </button>
          ) : null}
          {customCommandsAvailable ? (
            <button type="button" className={`pm-side-item ${tab === "commands" ? "active" : ""}`} onClick={() => setTab("commands")}>
              Commands
            </button>
          ) : null}
          <button type="button" className={`pm-side-item ${tab === "pixlwiz" ? "active" : ""}`} onClick={() => setTab("pixlwiz")}>
            {tr.tabPixlwiz}
          </button>
          <button type="button" className={`pm-side-item ${tab === "providers" ? "active" : ""}`} onClick={() => setTab("providers")}>
            {tr.tabProviders}
          </button>
          <button type="button" className={`pm-side-item ${tab === "chat" ? "active" : ""}`} onClick={() => setTab("chat")}>
            {tr.tabChat}
          </button>
          <button type="button" className={`pm-side-item ${tab === "advanced" ? "active" : ""}`} onClick={() => setTab("advanced")}>
            {tr.tabAdvanced}
          </button>
        </aside>
        <main className="pm-main">
          {!!error && <p className="pm-error">{error}</p>}
          {tab === "general" ? (
            <div className="pm-settings-wrap">
              <section className="pm-provider-card">
                <div className="pm-row">
                  <label>{tr.lblDisplayLanguage}</label>
                  <select
                    className="pm-select"
                    value={general.display_language}
                    onChange={(e) => setGeneral((g) => ({ ...g, display_language: e.target.value }))}
                  >
                    <option value="en">{tr.optLangEn}</option>
                    <option value="es">{tr.optLangEs}</option>
                    <option value="de">{tr.optLangDe}</option>
                    <option value="it">{tr.optLangIt}</option>
                    <option value="fr">{tr.optLangFr}</option>
                  </select>
                </div>
                <div className="pm-row">
                  <label>{tr.lblTheme}</label>
                  <select
                    className="pm-select"
                    value={general.theme}
                    onChange={(e) => setGeneral((g) => ({ ...g, theme: Number(e.target.value) as 0 | 1 | 2 }))}
                  >
                    <option value={0}>{tr.optThemeSystem}</option>
                    <option value={1}>{tr.optThemeLight}</option>
                    <option value={2}>{tr.optThemeDark}</option>
                  </select>
                </div>
                <div className="pm-row">
                  <label>{tr.lblFontSize}</label>
                  <select
                    className="pm-select"
                    value={general.font_size_extra_pt}
                    onChange={(e) => setGeneral((g) => ({ ...g, font_size_extra_pt: Number(e.target.value) }))}
                  >
                    <option value={0}>{tr.optFont0}</option>
                    <option value={1}>{tr.optFont1}</option>
                    <option value={2}>{tr.optFont2}</option>
                    <option value={3}>{tr.optFont3}</option>
                    <option value={4}>{tr.optFont4}</option>
                  </select>
                </div>
              </section>
            </div>
          ) : tab === "advanced" ? (
            <BackupSettingsPanel
              busy={busy}
              encryptExport={encryptExport}
              labels={{
                title: tr.titleAdvanced,
                secSettingsFile: tr.secSettingsFile,
                descSettingsFile: tr.descSettingsFile,
                lblEncryptExport: tr.lblEncryptExport,
                btnExportSettings: tr.btnExportSettings,
                btnImportSettings: tr.btnImportSettings,
                secClipboard: tr.secClipboard,
                descClipboard: tr.descClipboard,
                btnCopyToClipboard: tr.btnCopyToClipboard,
                btnPasteFromClipboard: tr.btnPasteFromClipboard,
                secProfileArchive: tr.secProfileArchive,
                descProfileArchive: tr.descProfileArchive,
                btnExportArchive: tr.btnExportArchive,
                btnImportArchive: tr.btnImportArchive,
              }}
              onEncryptExportChange={setEncryptExport}
              onExportSettings={() => void onExportSettings()}
              onImportSettings={() => void onImportSettings()}
              onCopyToClipboard={() => void onCopySettingsToClipboard()}
              onPasteFromClipboard={() => void onPasteSettingsFromClipboard()}
              onExportArchive={() => void onExportArchive()}
              onImportArchive={() => void onImportArchive()}
            />
          ) : tab === "mcp" && mcpClientAvailable ? (
            <McpSettingsPanel
              busy={busy}
              mcpJsonPath={mcpJsonPath}
              mcpServers={mcpServers}
              mcpPingResult={mcpPingResult}
              labels={{
                addServer: tr.mcpAddServer,
                ping: tr.mcpPing,
                removeServer: tr.mcpRemoveServer,
                path: tr.mcpPath,
                pingResult: tr.mcpPingResult,
                enabled: tr.mcpEnabled,
                serverName: tr.mcpServerName,
                type: tr.mcpType,
                typeAuto: tr.mcpTypeAuto,
                typeStdio: tr.mcpTypeStdio,
                typeSse: tr.mcpTypeSse,
                typeStreamableHttp: tr.mcpTypeStreamableHttp,
                command: tr.mcpCommand,
                args: tr.mcpArgs,
                url: tr.mcpUrl,
                headers: tr.mcpHeaders,
                env: tr.mcpEnv,
                toolTimeout: tr.mcpToolTimeout,
                enabledTools: tr.mcpEnabledTools,
              }}
              isExpanded={isMcpExpanded}
              onToggleExpanded={toggleMcpExpanded}
              onAddServer={addMcpServer}
              onRemoveServer={removeMcpServer}
              onPing={() => void pingMcp()}
              onServerEnabled={onMcpServerEnabled}
              onServerField={onMcpServerField}
            />
          ) : tab === "tools" && homeLlmToolsAvailable ? (
            <ToolsSettingsPanel
              tools={tools}
              allToolIds={allAgentToolIds}
              mcpAvailable={mcpClientAvailable}
              onToggleMcpToolsEnabled={(value) => setTools((s) => ({ ...s, mcp_tools_enabled: value }))}
              onDisabledMcpServersChange={(value) =>
                setTools((s) => ({
                  ...s,
                  disabled_mcp_servers: value
                    .split(",")
                    .map((x) => x.trim())
                    .filter(Boolean),
                }))
              }
              onTogglePathTool={(toolId, enabled) =>
                setTools((s) => {
                  const set = new Set(s.disabled_path_tools);
                  if (enabled) set.delete(toolId);
                  else set.add(toolId);
                  return { ...s, disabled_path_tools: Array.from(set) };
                })
              }
            />
          ) : tab === "skills" && homeLlmSkillsAvailable ? (
            <SkillsSettingsPanel
              busy={busy}
              settings={skillsSettings}
              search={skillsSearch}
              onSearchChange={setSkillsSearch}
              onToggleEnabled={(value) => updateSkillsAndPersist((s) => ({ ...s, enabled: value }))}
              onToggleRoamingEnabled={(value) => updateSkillsAndPersist((s) => ({ ...s, roaming_enabled: value }))}
              onToggleWorkspaceEnabled={(value) => updateSkillsAndPersist((s) => ({ ...s, workspace_enabled: value }))}
              onPinnedInputChange={onSkillsPinnedInputChange}
              onDisabledInputChange={onSkillsDisabledInputChange}
              onSave={() => void saveSkills()}
              onRefresh={() => void refreshSkills()}
              onToggleSkillPinned={toggleSkillPinned}
              onToggleSkillDisabled={toggleSkillDisabled}
            />
          ) : tab === "commands" && customCommandsAvailable ? (
            <CustomCommandsPanel
              busy={busy}
              commandsPath={customCommandsPath}
              document={customCommandsDoc}
              tablerIcons={tablerIcons}
              cliCommands={cliCommands}
              appCommands={appCommands}
              ribbonCommands={ribbonCommands}
              cliCommandSchemas={cliCommandSchemas}
              providerOptions={customCommandProviderOptions}
              modelOptionsByProvider={customCommandModelOptionsByProvider}
              scopedModelOptionsByProvider={customCommandScopedModelOptionsByProvider}
              routerOptions={customCommandRouterOptions}
              routerModelOptionsByProvider={customCommandRouterModelOptionsByProvider}
              providerDefaults={customCommandProviderDefaults}
              variableOptions={commandVariableOptions}
              onPickPath={pickNativePath}
              onChange={setCustomCommandsDoc}
              onReload={() => void reloadCustomCommands()}
            />
          ) : tab === "pixlwiz" ? (
            <div className="pm-settings-wrap">
              <section className="pm-provider-card">
                <div className="pm-row">
                  <label>{tr.pixlwizAuthStatus}</label>
                  <input
                    readOnly
                    value={
                      pixlwiz?.read_ok === false
                        ? `${tr.pixlwizStatusFailed}${pixlwiz.read_error ? `: ${pixlwiz.read_error}` : ""}`
                        : pixlwiz?.logged_in
                          ? tr.pixlwizSignedIn
                          : tr.pixlwizSignedOut
                    }
                  />
                </div>
                <div className="pm-row">
                  <label>{tr.pixlwizBudget}</label>
                  <input
                    readOnly
                    value={
                      pixlwiz?.budget_ok
                        ? `${Number(pixlwiz.spend ?? 0).toFixed(2)} / ${pixlwiz.max_budget == null ? "∞" : Number(pixlwiz.max_budget).toFixed(2)}`
                        : pixlwiz?.budget_error || "-"
                    }
                  />
                </div>
                <div className="pm-presets-actions">
                  <button className="btn" type="button" onClick={() => void pixlwizLogin()} disabled={busy}>
                    {tr.pixlwizLogin}
                  </button>
                  <button className="btn" type="button" onClick={() => void pixlwizLogout()} disabled={busy}>
                    {tr.pixlwizLogout}
                  </button>
                  <button className="btn" type="button" onClick={() => void refreshPixlwizStatus()} disabled={busy}>
                    {tr.pixlwizRefresh}
                  </button>
                </div>
              </section>
            </div>
          ) : tab === "providers" ? (
            <div className="pm-settings-wrap">
              <div className="pm-provider-list">
                {rows.map((r) => (
                  <section key={r.id} className="pm-provider-card">
                    <h2>{r.displayName}</h2>
                    <div className="pm-row">
                      <label>API key</label>
                      <div className="pm-input-row">
                        <input type={showKey[r.id] ? "text" : "password"} value={r.apiKey} onChange={(e) => onProviderField(r.id, "apiKey", e.target.value)} />
                        {(r.id === "openrouter" || r.id === "openai") ? (
                          <>
                            <button
                              className="btn pm-refresh-btn"
                              type="button"
                              title={`OAuth login (${r.id})`}
                              aria-label={`OAuth login (${r.id})`}
                              onClick={() => void loginProviderOAuth(r.id)}
                            >
                              <IconLogin />
                            </button>
                            <button
                              className="btn pm-refresh-btn"
                              type="button"
                              title={`OAuth logout (${r.id})`}
                              aria-label={`OAuth logout (${r.id})`}
                              onClick={() => void logoutProviderOAuth(r.id)}
                            >
                              ×
                            </button>
                          </>
                        ) : null}
                        <button
                          className="btn pm-refresh-btn"
                          type="button"
                          title="Copy API key"
                          aria-label="Copy API key"
                          onClick={() => void copyTextToClipboard(r.apiKey)}
                        >
                          <IconCopy />
                        </button>
                        <button
                          className="btn pm-refresh-btn"
                          type="button"
                          title={showKey[r.id] ? "Hide API key" : "Show API key"}
                          aria-label={showKey[r.id] ? "Hide API key" : "Show API key"}
                          onClick={() => setShowKey((s) => ({ ...s, [r.id]: !s[r.id] }))}
                        >
                          {showKey[r.id] ? <IconEyeOff /> : <IconEye />}
                        </button>
                      </div>
                    </div>
                    {(r.id === "openrouter" || r.id === "openai") ? (
                      <div className="pm-row">
                        <label>OAuth</label>
                        <input
                          readOnly
                          value={
                            providerOauth[r.id]?.loginInProgress
                              ? "logging in..."
                              : providerOauth[r.id]?.hasToken
                                ? "token available"
                                : providerOauth[r.id]?.lastError || providerOauth[r.id]?.info || "not logged in"
                          }
                        />
                      </div>
                    ) : null}
                    <div className="pm-row">
                      <label>Base URL</label>
                      <input value={r.baseUrl} onChange={(e) => onProviderField(r.id, "baseUrl", e.target.value)} />
                    </div>
                  </section>
                ))}
              </div>
            </div>
          ) : (
            <div className="pm-settings-wrap">
              <section className="pm-presets-bar">
                <div className="pm-presets-row">
                  <select
                    className="pm-select"
                    value={selectedPresetId}
                    onChange={(e) => {
                      const id = e.target.value;
                      setSelectedPresetId(id);
                      const p = chatPresets.find((x) => x.id === id);
                      if (p) setPresetName(p.name);
                    }}
                  >
                    <option value="">{tr.presetSelect}</option>
                    {chatPresets.map((p) => (
                      <option key={p.id} value={p.id}>
                        {p.name}
                      </option>
                    ))}
                  </select>
                  <input
                    className="pm-preset-name"
                    type="text"
                    placeholder={tr.presetName}
                    value={presetName}
                    onChange={(e) => setPresetName(e.target.value)}
                  />
                </div>
                <div className="pm-presets-actions">
                  <button className="btn" type="button" onClick={loadPreset} disabled={!selectedPresetId}>
                    {tr.presetLoad}
                  </button>
                  <button className="btn" type="button" onClick={() => void saveAsPreset()}>
                    {tr.presetSaveAs}
                  </button>
                  <button className="btn" type="button" onClick={() => void updatePreset()} disabled={!selectedPresetId}>
                    {tr.presetUpdate}
                  </button>
                  <button className="btn" type="button" onClick={() => void renamePreset()} disabled={!selectedPresetId}>
                    {tr.presetRename}
                  </button>
                  <button className="btn" type="button" onClick={() => void deletePreset()} disabled={!selectedPresetId}>
                    {tr.presetDelete}
                  </button>
                </div>
              </section>
              <section className="pm-chat-layout">
                <div className="pm-chat-group">
                  <h2>{tr.secText}</h2>
                  <div className="pm-row">
                    <label>{tr.lblRouter}</label>
                    <select
                      className="pm-select"
                      value={chat.router}
                      onChange={(e) => onChatField("router", e.target.value)}
                    >
                      {ROUTER_OPTIONS.map((o) => (
                        <option key={o.id} value={o.id}>
                          {o.label}
                        </option>
                      ))}
                    </select>
                  </div>
                  <div className="pm-row">
                    <label>{tr.lblModel}</label>
                    <div className="pm-input-row">
                      <TypeaheadInput value={chat.model} onChange={(v) => onChatField("model", v)} options={chatModelTypeahead} />
                      <ModelRefreshButton scope="router" />
                    </div>
                  </div>
                  <div className="pm-row">
                    <label>{tr.lblMaxIter}</label>
                    <input
                      type="number"
                      min={1}
                      max={99}
                      value={chat.max_iterations}
                      onChange={(e) => onChatField("max_iterations", Number(e.target.value || 8))}
                    />
                  </div>
                </div>

                <div className="pm-chat-group">
                  <h2>{tr.secImage}</h2>
                  <div className="pm-chat-subgroup">
                    <h3>{tr.secImageGeneration}</h3>
                    <div className="pm-row">
                      <label>{tr.lblImageProvider}</label>
                    <select
                      className="pm-select"
                      value={chat.image_provider}
                      onChange={(e) => {
                        const v = e.target.value;
                        onChatField("image_provider", v);
                        if (v === "replicate" && chat.image_model) void resolveReplicateCollectionFromModel("image", chat.image_model);
                      }}
                    >
                        {providerOptions.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
                      </select>
                    </div>
                    {chat.image_provider === "replicate" ? (
                      <div className="pm-row">
                        <label>Collection</label>
                        <select
                          className="pm-select"
                          value={replicateCollectionByScope.image}
                          onChange={(e) => onReplicateCollectionChange("image", e.target.value)}
                        >
                          {(replicateCollections.length ? replicateCollections : [{ slug: "official", name: "official" }]).map((c) => (
                            <option key={c.slug} value={c.slug}>
                              {c.name || c.slug}
                            </option>
                          ))}
                        </select>
                      </div>
                    ) : null}
                    <div className="pm-row">
                      <label>{tr.lblImageModel}</label>
                      <div className="pm-input-row">
                        <TypeaheadInput
                          value={chat.image_model}
                          onChange={(v) => {
                            onChatField("image_model", v);
                            if (chat.image_provider === "replicate") void resolveReplicateCollectionFromModel("image", v);
                          }}
                          options={imageModelTypeahead}
                        />
                        <ModelRefreshButton scope="image" />
                      </div>
                    </div>
                  </div>
                  <div className="pm-chat-subgroup">
                    <h3>{tr.secImageVision}</h3>
                    <div className="pm-row">
                      <label>{tr.lblRecognitionProvider}</label>
                      <select
                        className="pm-select"
                        value={chat.image_recognition_provider}
                        onChange={(e) => {
                          const v = e.target.value;
                          onChatField("image_recognition_provider", v);
                          if (v === "replicate" && chat.image_recognition_model) {
                            void resolveReplicateCollectionFromModel("vision", chat.image_recognition_model);
                          }
                        }}
                      >
                        {providerOptions.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
                      </select>
                    </div>
                    {chat.image_recognition_provider === "replicate" ? (
                      <div className="pm-row">
                        <label>Collection</label>
                        <select
                          className="pm-select"
                          value={replicateCollectionByScope.vision}
                          onChange={(e) => onReplicateCollectionChange("vision", e.target.value)}
                        >
                          {(replicateCollections.length ? replicateCollections : [{ slug: "official", name: "official" }]).map((c) => (
                            <option key={c.slug} value={c.slug}>
                              {c.name || c.slug}
                            </option>
                          ))}
                        </select>
                      </div>
                    ) : null}
                    <div className="pm-row">
                      <label>{tr.lblRecognitionModel}</label>
                      <div className="pm-input-row">
                        <TypeaheadInput
                          value={chat.image_recognition_model}
                          onChange={(v) => {
                            onChatField("image_recognition_model", v);
                            if (chat.image_recognition_provider === "replicate") void resolveReplicateCollectionFromModel("vision", v);
                          }}
                          options={visionModelTypeahead}
                        />
                        <ModelRefreshButton scope="vision" />
                      </div>
                    </div>
                  </div>
                </div>

                <div className="pm-chat-group">
                  <h2>{tr.secVideo}</h2>
                  <div className="pm-row">
                    <label>{tr.lblVideoProvider}</label>
                    <select
                      className="pm-select"
                      value={chat.video_provider}
                      onChange={(e) => {
                        const v = e.target.value;
                        onChatField("video_provider", v);
                        if (v === "replicate" && chat.video_model) void resolveReplicateCollectionFromModel("video", chat.video_model);
                      }}
                    >
                      {providerOptions.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
                    </select>
                  </div>
                  {chat.video_provider === "replicate" ? (
                    <div className="pm-row">
                      <label>Collection</label>
                      <select
                        className="pm-select"
                        value={replicateCollectionByScope.video}
                        onChange={(e) => onReplicateCollectionChange("video", e.target.value)}
                      >
                        {(replicateCollections.length ? replicateCollections : [{ slug: "official", name: "official" }]).map((c) => (
                          <option key={c.slug} value={c.slug}>
                            {c.name || c.slug}
                          </option>
                        ))}
                      </select>
                    </div>
                  ) : null}
                  <div className="pm-row">
                    <label>{tr.lblVideoModel}</label>
                    <div className="pm-input-row">
                      <TypeaheadInput
                        value={chat.video_model}
                        onChange={(v) => {
                          onChatField("video_model", v);
                          if (chat.video_provider === "replicate") void resolveReplicateCollectionFromModel("video", v);
                        }}
                        options={videoModelTypeahead}
                      />
                      <ModelRefreshButton scope="video" />
                    </div>
                  </div>
                </div>

                <div className="pm-chat-group">
                  <h2>{tr.secAudio}</h2>
                  <div className="pm-chat-subgroup">
                    <h3>{tr.grpVoiceInput}</h3>
                    <div className="pm-row">
                      <label>{tr.lblSttProvider}</label>
                      <select className="pm-select" value={chat.stt_provider} onChange={(e) => onChatField("stt_provider", e.target.value)}>
                        {STT_PROVIDER_OPTIONS.map((o) => (
                          <option key={o.id} value={o.id}>
                            {o.label}
                          </option>
                        ))}
                      </select>
                    </div>
                    <div className="pm-row">
                      <label>{tr.lblSttModel}</label>
                      <TypeaheadInput value={chat.stt_model} onChange={(v) => onChatField("stt_model", v)} options={sttModelTypeahead} />
                    </div>
                  </div>
                  <div className="pm-chat-subgroup">
                    <h3>{tr.grpVoiceOutput}</h3>
                    <div className="pm-row">
                      <label>{tr.lblTtsProvider}</label>
                      <select className="pm-select" value={chat.tts_provider} onChange={(e) => onChatField("tts_provider", e.target.value)}>
                        {TTS_PROVIDER_OPTIONS.map((o) => (
                          <option key={o.id} value={o.id}>
                            {o.label}
                          </option>
                        ))}
                      </select>
                    </div>
                    <div className="pm-row">
                      <label>{tr.lblTtsModel}</label>
                      <TypeaheadInput value={chat.tts_model} onChange={(v) => onChatField("tts_model", v)} options={ttsModelTypeahead} />
                    </div>
                    <div className="pm-row">
                      <label>{tr.lblTtsVoice}</label>
                      <TypeaheadInput
                        value={chat.tts_voice_id}
                        onChange={(v) => onChatField("tts_voice_id", v)}
                        options={ttsVoiceTypeahead}
                        disabled={chat.tts_provider !== "elevenlabs"}
                      />
                    </div>
                  </div>
                </div>
              </section>
            </div>
          )}
        </main>
      </div>
      <footer className="pm-footer">
        <div className="pm-footer-right">
        <button className="btn" type="button" onClick={() => hostPost({ t: "cweb_close" })}>
          {tr.cancel}
        </button>
        <button className="btn pm-btn-primary" type="button" disabled={busy} onClick={() => void onOk()}>
          {busy ? "..." : tr.ok}
        </button>
        </div>
      </footer>
    </div>
  );
}

const rootEl = document.getElementById("root");
if (!rootEl) throw new Error("Root element #root not found");

createRoot(rootEl).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
