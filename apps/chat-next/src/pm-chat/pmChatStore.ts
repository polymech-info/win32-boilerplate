import { create } from "zustand";

import { recordSessionVisited } from "@/chat/recentSessions";
import type { StoredChatSession } from "./chatSessionStorage";
import {
  defaultModelForRouterId,
  sortImageModelRowList,
  sortReplicateCollections,
  type ImageModelRow,
  type ReplicateCollectionRow,
} from "./chatConstants";
import { createChatProviderClient, type ChatProviderClient } from "@pm/shared/providers/chatProviderClient";
import {
  buildReplicateOpenApiFieldValues,
  coerceOpenApiFieldValuesForHost,
} from "@pm/shared/providers/openApiSchema";
import type { ChatProviderFields, ProviderRow, VideoOpenApiInputFlat } from "@pm/shared/providers/types";
import { callProviderRpc, hasWebProviderHost, postHost } from "./hostBridge";
import { persistChatSessionSnapshot } from "./chatSessionsBackend";
import { rewriteLocalPath } from "./markdownSetup";
import { getDefaultQuickActions, setLocale as setUiLocale, t } from "./i18n.js";
import {
  designPresetsBundledForDev,
  designPresetsFromUnknownDoc,
  normalizeDesignPresetRow,
} from "./designPresets";
import type { DesignPreset } from "./designPresets";
import {
  expandedResizePresetPrompt,
  normalizeResizePresetRow,
  resizePresetsBundledForDev,
  resizePresetsFromUnknownDoc,
} from "./resizePresets";
import { normalizeTurnLlmUsage, type TurnLlmUsageAggregate } from "./llmUsageFormat";
import type { ResizePreset } from "./resizePresets";
export type { DesignPreset } from "./designPresets";
export type { VideoOpenApiInputFlat } from "@pm/shared/providers/types";
export type { ResizePreset, ResizeTarget } from "./resizePresets";

const chatProviders: ChatProviderClient = createChatProviderClient(callProviderRpc);

const LS_QUICK_LLM = "pm-image.chat.quickLlm";
const LS_PROMPT_HISTORY = "pm-image.chat.promptHistory";
const MAX_PROMPT_HISTORY = 50;
const LS_QUICK_ACTIONS = "pm-image.chat.quickActions";
const LS_DESIGN_PRESETS = "pm-image.chat.designPresets";
const LS_RESIZE_PRESETS = "pm-image.chat.resizePresets";
const LS_QA_UI = "pm-image.chat.qaUi";
const LS_PAST_SESSIONS_OPEN = "pm-chat-next.ui.pastSessionsOpen";
const LS_SHOW_TOOL_CALLS = "pm-chat-next.ui.showToolCalls";
const LS_AGENT_TOOLS = "pm-image.chat.agentToolsV2";
const LS_MCP_TOOLS_ENABLED = "pm-image.chat.mcpToolsEnabled";
const LS_DISABLED_MCP_SERVERS = "pm-image.chat.disabledMcpServers";
const LS_FILMSTRIP_VISIBLE = "pm-chat-next.ui.filmstripVisible";
const CHAT_WEB_VERSION = 1;

export type McpCatalogTool = { name?: string; description?: string };
export type McpCatalogServer = {
  name?: string;
  transport?: string;
  handshake_ok?: boolean;
  tools_list_ok?: boolean;
  skipped?: boolean;
  skip_reason?: string;
  tools?: McpCatalogTool[];
};
export type McpCatalogPayload = {
  servers?: McpCatalogServer[];
  mcp_json_path?: string;
  exists?: boolean;
  probe_error?: string;
};

function loadMcpToolsEnabled(): boolean {
  try {
    return localStorage.getItem(LS_MCP_TOOLS_ENABLED) !== "0";
  } catch {
    return true;
  }
}

function loadDisabledMcpServers(): string[] {
  try {
    const raw = localStorage.getItem(LS_DISABLED_MCP_SERVERS);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed.filter((x): x is string => typeof x === "string") : [];
  } catch {
    return [];
  }
}

function loadFilmstripVisible(): boolean {
  try {
    return localStorage.getItem(LS_FILMSTRIP_VISIBLE) !== "0";
  } catch {
    return true;
  }
}

/** All tool IDs present in `media::llm::path::tool_catalog()` — mirrors `constants.hpp` AgentTool enum order. */
export const ALL_AGENT_TOOL_IDS = [
  "list_images", "file_glob", "file_read", "file_search",
  "image_resize", "image_transform", "image_create", "create_video",
  "image_understand", "image_from_camera",
  "write_file", "speak",
  "schedule_at", "schedule_in", "schedule_every", "schedule_cancel", "schedule_list",
  "memory_read", "memory_write", "memory_append_event",
  "run",
] as const;

export type AgentToolId = (typeof ALL_AGENT_TOOL_IDS)[number];

function loadAgentToolEnabled(): Record<AgentToolId, boolean> {
  const allOn = () => Object.fromEntries(ALL_AGENT_TOOL_IDS.map((id) => [id, true])) as Record<AgentToolId, boolean>;
  try {
    const raw = localStorage.getItem(LS_AGENT_TOOLS);
    if (!raw) return allOn();
    const o = JSON.parse(raw) as Record<string, unknown>;
    return Object.fromEntries(ALL_AGENT_TOOL_IDS.map((id) => [id, o[id] !== false])) as Record<AgentToolId, boolean>;
  } catch {
    return allOn();
  }
}

function saveAgentToolEnabled(v: Record<AgentToolId, boolean>) {
  try {
    localStorage.setItem(LS_AGENT_TOOLS, JSON.stringify(v));
  } catch {
    /* */
  }
}

function disabledPathToolsForSend(agentToolEnabled: Record<AgentToolId, boolean>): string[] {
  return ALL_AGENT_TOOL_IDS.filter((id) => agentToolEnabled[id] === false);
}

const _agentToolEnabled = loadAgentToolEnabled();

function readPastSessionsOpen(): boolean {
  try {
    return localStorage.getItem(LS_PAST_SESSIONS_OPEN) === "1";
  } catch {
    return false;
  }
}

function readShowToolCalls(): boolean {
  try {
    return localStorage.getItem(LS_SHOW_TOOL_CALLS) === "1";
  } catch {
    return false;
  }
}

let suppressChatWebHostPost = 0;
let chatWebToHostTimer: ReturnType<typeof setTimeout> | null = null;
let saveImageChatTimer: ReturnType<typeof setTimeout> | null = null;

export type ChatEntryRole = "user" | "assistant" | "tool" | "system" | "error" | "image" | "file" | "shell";

export type { TurnLlmUsageAggregate } from "./llmUsageFormat";

export type ChatEntry = {
  id: number;
  role: ChatEntryRole;
  text: string;
  ts?: number;
  /** Explorer folder when the user sent this message (for display + Markdown paths). */
  folderHint?: string;
  /** Paths in chat context when the user sent this message (referenced files / images). */
  contextPaths?: string[];
  /** Output image paths from tools for this user turn (merged from host `appendText` role `image`). */
  resultPaths?: string[];
  /** Aggregated LLM token/cost for the turn (host `setTurnLlmUsage`; shown on user bubble + assistant footer). */
  llmUsage?: TurnLlmUsageAggregate;
  /** Execution provider for tool entries that use an internal LLM (e.g. image_understand → "replicate"). */
  toolProvider?: string;
  /** Execution model for tool entries that use an internal LLM (e.g. image_understand → "g/google/gemini-2.5-flash-image"). */
  toolModel?: string;
  /** Wall-clock milliseconds the tool took to execute (from ToolResult event; undefined for older entries). */
  durationMs?: number;
  /** Live stdout/stderr for `role === "shell"` entries (run tool streaming). */
  shellOutput?: { stdout: string; stderr: string };
  /** Correlates shell chunks with the right entry when multiple run calls happen in one turn. */
  runId?: string;
  /** The shell command being executed (set on start). */
  runCommand?: string;
  /** Whether the command is still running. */
  runActive?: boolean;
  /** Exit code when finished (undefined while running). */
  runExitCode?: number;
  /** Wall-clock ms the command took (set on finish). */
  runDurationMs?: number;
};

export type QuickAction = { id: string; name: string; prompt: string; icon: string };

export type ImageProviderRow = ProviderRow;

function findLastChatEntryIndex(entries: ChatEntry[], predicate: (entry: ChatEntry) => boolean): number {
  for (let i = entries.length - 1; i >= 0; --i) {
    if (predicate(entries[i]!)) return i;
  }
  return -1;
}

function withSuppressChatWebHostPost(fn: () => void): void {
  suppressChatWebHostPost += 1;
  try {
    fn();
  } finally {
    suppressChatWebHostPost -= 1;
  }
}

function loadQuickLlm(): { router: string; model: string } {
  try {
    const raw = localStorage.getItem(LS_QUICK_LLM);
    if (!raw) return { router: "", model: "" };
    const p = JSON.parse(raw) as { r?: string; m?: string };
    return {
      router: typeof p.r === "string" ? p.r : "",
      model: typeof p.m === "string" ? p.m : "",
    };
  } catch {
    return { router: "", model: "" };
  }
}

function loadPromptHistory(): string[] {
  try {
    const raw = localStorage.getItem(LS_PROMPT_HISTORY);
    if (!raw) return [];
    const p = JSON.parse(raw) as unknown;
    return Array.isArray(p) ? p.filter((x): x is string => typeof x === "string") : [];
  } catch {
    return [];
  }
}

function loadQaUiState() {
  const defaults = {
    toolbar: true,
    presetsGroup: true,
    providers: true,
  };
  try {
    const raw = localStorage.getItem(LS_QA_UI);
    if (!raw) return defaults;
    const o = JSON.parse(raw) as Record<string, unknown>;
    const providersMerged =
      typeof o.providers === "boolean"
        ? o.providers !== false
        : (o.textLlm !== false) || (o.image !== false) || (o.video !== false);
    const presetsGroupMerged =
      typeof o.presetsGroup === "boolean"
        ? o.presetsGroup !== false
        : (o.presets !== false) || (o.design !== false) || (o.resize !== false);
    return {
      toolbar: o.toolbar !== false,
      presetsGroup: presetsGroupMerged,
      providers: providersMerged,
    };
  } catch {
    return defaults;
  }
}

function cloneDefaultQuickActions(): QuickAction[] {
  return (getDefaultQuickActions() as QuickAction[]).map((a) => ({ ...a }));
}

export function normalizeQuickAction(a: unknown): QuickAction | null {
  if (!a || typeof a !== "object") return null;
  const o = a as Record<string, unknown>;
  if (typeof o.name !== "string" || typeof o.prompt !== "string") return null;
  return {
    id: String((o.id && String(o.id).trim()) || `qa-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`),
    name: o.name.trim(),
    prompt: o.prompt as string,
    icon: typeof o.icon === "string" ? o.icon : "",
  };
}

function loadQuickActions(): QuickAction[] {
  try {
    const raw = localStorage.getItem(LS_QUICK_ACTIONS);
    if (!raw) return cloneDefaultQuickActions();
    const p = JSON.parse(raw) as unknown;
    if (!Array.isArray(p) || p.length === 0) return cloneDefaultQuickActions();
    const out = p.map(normalizeQuickAction).filter(Boolean) as QuickAction[];
    return out.length ? out : cloneDefaultQuickActions();
  } catch {
    return cloneDefaultQuickActions();
  }
}

function newQaRowId(): string {
  return `qa-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`;
}

function newDesignRowId(): string {
  return `design-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`;
}

function newResizeRowId(): string {
  return `resize-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`;
}

function loadResizePresets(): ResizePreset[] {
  try {
    const raw = localStorage.getItem(LS_RESIZE_PRESETS);
    if (!raw) return [];
    const p = JSON.parse(raw) as unknown;
    if (!Array.isArray(p) || p.length === 0) return [];
    const out = p.map(normalizeResizePresetRow).filter(Boolean) as ResizePreset[];
    return out.length ? out : [];
  } catch {
    return [];
  }
}

function writeResizePresetsLocal(list: ResizePreset[]) {
  try {
    localStorage.setItem(LS_RESIZE_PRESETS, JSON.stringify(list));
  } catch {
    /* */
  }
}

function loadDesignPresets(): DesignPreset[] {
  try {
    const raw = localStorage.getItem(LS_DESIGN_PRESETS);
    if (!raw) return [];
    const p = JSON.parse(raw) as unknown;
    if (!Array.isArray(p) || p.length === 0) return [];
    const out = p.map(normalizeDesignPresetRow).filter(Boolean) as DesignPreset[];
    return out.length ? out : [];
  } catch {
    return [];
  }
}

function writeDesignPresetsLocal(list: DesignPreset[]) {
  try {
    localStorage.setItem(LS_DESIGN_PRESETS, JSON.stringify(list));
  } catch {
    /* */
  }
}

const _quickLlm = loadQuickLlm();
const _qa = loadQaUiState();

export type PmChatStore = {
  i18nRev: number;
  /** Merged Explorer + explicit extras (minus dismissed); used for Send and fullscreen cycle. */
  selection: string[];
  /** Explorer-only paths for the film strip (same dismissal filter as `selection`). */
  explorerSelection: string[];
  /** Drag / paste / explicit adds shown separately in the film strip. */
  contextExtraPaths: string[];
  folder: string;
  /** Sum of file sizes for current context paths (host `setStatus.selection_bytes`). */
  selectionTotalBytes: number;
  workspace: string;
  busy: boolean;
  savedChatRouter: string;
  savedChatModel: string;
  routerQuick: string;
  modelQuick: string;
  entries: ChatEntry[];
  nextId: number;
  inputDraft: string;
  /** True while the STT mic session is active (mic button shows stop state). */
  sttRecording: boolean;
  /** Live partial transcript from STT (cleared on commit or stop). */
  sttPartial: string;
  /** Normalized microphone input level from native miniaudio RMS (0..1). */
  sttLevel: number;
  /** True while a webcam still-image capture is in progress. */
  videoSnapBusy: boolean;
  /** True while the speak tool is synthesising or playing audio. */
  ttsPlaying: boolean;
  promptHistory: string[];
  historyIndex: number;
  quickActions: QuickAction[];
  /** User-editable reframe presets (defaults from `resize-templates.json` on first load). */
  resizePresets: ResizePreset[];
  /** User-editable design / look prompts (defaults from `design-presets.json` on first load). */
  designPresets: DesignPreset[];
  showQuickActionEditor: boolean;
  qaEditDraft: QuickAction[] | null;
  showDesignPresetEditor: boolean;
  designEditDraft: DesignPreset[] | null;
  showResizePresetEditor: boolean;
  resizeEditDraft: ResizePreset[] | null;
  qaToolbarOpen: boolean;
  /** Style + design + resize preset blocks under one Quick tools accordion. */
  qaPresetsGroupOpen: boolean;
  /** Single Quick tools accordion for text LLM + image + video provider/model rows. */
  qaProvidersOpen: boolean;
  imageProvider: string;
  imageModel: string;
  replicateCollection: string;
  videoProvider: string;
  videoModel: string;
  videoReplicateCollection: string;
  /** Voice input (STT) provider, e.g. "pixlwiz" or "elevenlabs". */
  sttProvider: string;
  /** Voice input model, e.g. "pixlwiz-speech-to-text". */
  sttModel: string;
  /** Voice output (TTS) provider, e.g. "pixlwiz" or "elevenlabs". */
  ttsProvider: string;
  /** Voice output model, e.g. "pixlwiz-speech". */
  ttsModel: string;
  /** ElevenLabs voice UUID (only relevant when ttsProvider === "elevenlabs"). */
  ttsVoiceId: string;
  /** Image recognition (vision) provider, e.g. "pixlwiz" or "replicate". */
  imageRecogProvider: string;
  /** Image recognition model, e.g. "image-vision-deep". */
  imageRecogModel: string;
  imageProviderRows: ImageProviderRow[];
  imageModelRows: ImageModelRow[];
  videoModelRows: ImageModelRow[];
  /** Flat Replicate OpenAPI Input (one model); null when not Replicate / no model / load error before success. */
  videoOpenApiFlat: VideoOpenApiInputFlat | null;
  videoOpenApiFlatLoading: boolean;
  videoOpenApiFlatError: string | null;
  /** UI values for visible `videoOpenApiFlat` fields (string form). */
  videoOpenApiFieldValues: Record<string, string>;
  /** Replicate image model OpenAPI Input (same RPC as video). */
  imageOpenApiFlat: VideoOpenApiInputFlat | null;
  imageOpenApiFlatLoading: boolean;
  imageOpenApiFlatError: string | null;
  imageOpenApiFieldValues: Record<string, string>;
  replicateCollections: ReplicateCollectionRow[];
  providerUiBooting: boolean;
  /** Route / composer session id for autosave (see chatSessionsBackend). */
  activeSessionId: string;
  pastSessionsOpen: boolean;
  /** Per-tool enable flags mirroring `path_tool_catalog.cpp`; false entries are sent as `disable_tools` on Send. */
  agentToolEnabled: Record<AgentToolId, boolean>;
  /** When false, Send passes mcp_tools_enabled=false — agent omits all `mcp_*` OpenAI tools for that turn. */
  mcpToolsEnabled: boolean;
  /** MCP server names (mcp.json keys) that are individually disabled. Sent as disabled_mcp_servers on Send. */
  disabledMcpServers: string[];
  /** Slim MCP probe from host setStatus (servers + tool names); null until first payload. */
  mcpCatalog: McpCatalogPayload | null;
  /** Host compile flags + optional dev overrides (`window.__PM_DEV_EMBED_FEATURES`). */
  embedFeatures: { pixlwizAuth: boolean; pixlwizShare: boolean };
  /** When false, tool-call rows are hidden in the transcript (persisted per device). */
  showToolCalls: boolean;
  /** When false, filmstrip/thumbnail strip is collapsed (persisted per device). */
  filmstripVisible: boolean;
  /** Most-recent agent.json text for the current session (set by host after each agent turn). */
  agentJsonText: string | null;
  /** Pixlwiz (LiteLLM) budget: current spend and max budget in USD. null until first fetch. */
  pixlwizCredit: { spend: number; max: number | null } | null;

  schedulePostChatWebToHost: () => void;
  setPixlwizCredit: (c: { spend: number; max: number | null } | null) => void;
  saveQuickLlmToStorage: () => void;
  savePromptHistoryToStorage: (list: string[]) => void;
  saveQuickActionsToStorage: (list: QuickAction[]) => void;
  saveDesignPresetsToStorage: (list: DesignPreset[]) => void;
  saveResizePresetsToStorage: (list: ResizePreset[]) => void;
  saveQaUiState: () => void;
  addPromptToHistory: (text: string) => void;
  navigateHistory: (dir: "up" | "down") => void;
  setInputDraft: (v: string) => void;
  setSttRecording: (v: boolean) => void;
  setSttPartial: (v: string) => void;
  setSttLevel: (v: number) => void;
  appendSttCommitted: (text: string) => void;
  setVideoSnapBusy: (v: boolean) => void;
  setTtsPlaying: (v: boolean) => void;
  effectiveRouterId: () => string;
  send: () => void;
  clearTranscript: () => void;
  deleteEntry: (id: number) => void;
  postStop: () => void;
  applyQuickAction: (action: QuickAction) => void;
  applyResizePreset: (preset: ResizePreset) => void;
  toggleQaToolbar: () => void;
  toggleQaPresetsGroup: () => void;
  toggleQaProviders: () => void;
  setRouterQuick: (v: string) => void;
  setModelQuick: (v: string) => void;
  /** Live LLM model list fetched from the native host for the current router (openrouter / openai). */
  llmModelRows: { id: string; label: string }[];
  llmModelRowsLoading: boolean;
  /** Which router the current `llmModelRows` was fetched for. */
  llmModelRowsRouter: string;
  fetchLlmModels: (router: string, forceRefresh?: boolean) => Promise<void>;
  openQaModal: () => void;
  closeQaModal: () => void;
  setQaEditDraft: (rows: QuickAction[] | null) => void;
  resetQaEditDraft: () => void;
  addQaEditRow: () => void;
  removeQaEditRow: (id: string) => void;
  saveQaModal: () => boolean;
  openDesignPresetModal: () => void;
  closeDesignPresetModal: () => void;
  setDesignEditDraft: (rows: DesignPreset[] | null) => void;
  resetDesignEditDraft: () => void;
  addDesignEditRow: () => void;
  removeDesignEditRow: (id: string) => void;
  saveDesignPresetModal: () => boolean;
  openResizePresetModal: () => void;
  closeResizePresetModal: () => void;
  setResizeEditDraft: (rows: ResizePreset[] | null) => void;
  resetResizeEditDraft: () => void;
  addResizeEditRow: () => void;
  removeResizeEditRow: (id: string) => void;
  saveResizePresetModal: () => boolean;
  setImageProvider: (next: string) => Promise<void>;
  setReplicateCollection: (slug: string) => Promise<void>;
  refreshReplicateCollectionsAndModels: () => Promise<void>;
  setImageModel: (id: string) => Promise<void>;
  setVideoProvider: (next: string) => Promise<void>;
  setVideoReplicateCollection: (slug: string) => Promise<void>;
  refreshVideoReplicateCollectionsAndModels: () => Promise<void>;
  setVideoModel: (id: string) => Promise<void>;
  setSttProvider: (v: string) => Promise<void>;
  setSttModel: (v: string) => Promise<void>;
  setTtsProvider: (v: string) => Promise<void>;
  setTtsModel: (v: string) => Promise<void>;
  setTtsVoiceId: (v: string) => Promise<void>;
  setImageRecogProvider: (v: string) => Promise<void>;
  setImageRecogModel: (v: string) => Promise<void>;
  refreshVideoOpenApiInputFlat: () => Promise<void>;
  setVideoOpenApiFieldValue: (key: string, value: string) => void;
  refreshImageOpenApiInputFlat: () => Promise<void>;
  setImageOpenApiFieldValue: (key: string, value: string) => void;
  scheduleSaveImageChatFields: () => void;
  refreshImageModelsForCurrentProvider: () => Promise<void>;
  refreshVideoModelsForCurrentProvider: () => Promise<void>;
  bootstrapProviderUi: () => Promise<void>;
  injectDevSampleTranscript: () => void;
  resetSessionSurface: () => void;
  setActiveSessionId: (id: string) => void;
  applyLoadedSession: (session: StoredChatSession) => void;
  togglePastSessions: () => void;
  setAgentToolEnabled: (id: AgentToolId, v: boolean) => void;
  setAgentToolGroupEnabled: (ids: AgentToolId[], v: boolean) => void;
  setMcpToolsEnabled: (v: boolean) => void;
  setDisabledMcpServers: (servers: string[]) => void;
  toggleShowToolCalls: () => void;
  toggleFilmstrip: () => void;
  /** Store the latest agent.json text for the Log tab (also persisted in the session). */
  setAgentJson: (text: string | null) => void;
  /** Folder the current agent turn is scoped to (shown in chat). */
  setRunFolder: (folder: string) => void;
  appendText: (o: { role?: ChatEntryRole; text?: string; toolProvider?: string; toolModel?: string; durationMs?: number }) => void;
  /** Create a shell entry when a run command starts (before any output). */
  startRun: (runId: string, command: string) => void;
  /** Append a stdout/stderr chunk from a running `run` shell tool. Creates a new shell entry on first chunk. */
  appendRunChunk: (runId: string, chunk: string, stream: "stdout" | "stderr") => void;
  /** Mark a run as finished with exit code and duration. */
  finishRun: (runId: string, exitCode: number, durationMs: number) => void;
  /** Attach aggregated usage/cost to the latest assistant bubble (WebView host after each turn). */
  setTurnLlmUsage: (raw: unknown) => void;
  appendUser: (text: string) => void;
  setStatus: (o: {
    selection?: string[];
    explorer_selection?: string[];
    context_extra?: string[];
    folder?: string;
    selection_bytes?: number;
    selectionBytes?: number;
    workspace?: string;
    saved_chat_router?: string;
    saved_chat_model?: string;
    features?: { pixlwizAuth?: boolean; pixlwizShare?: boolean };
    mcp_catalog?: McpCatalogPayload | null;
  }) => void;
  /** Drop one path from chat context (host merges; dev updates store). */
  removeSelectionPath: (path: string) => void;
  setBusy: (busy: boolean) => void;
  setLocale: (code: string) => void;
  clear: () => void;
  setFontExtraPt: (extraPt: number) => void;
  applyHostPersistence: (doc: unknown) => void;
  openGeneratedPath: (path: string) => void;
  /** Normal/shift+click: open path in the internal centre preview viewer (main CFileViewer). */
  openPathInternal: (path: string) => void;
  /**
   * Ctrl/Cmd+click: preview fills the monitor.
   */
  openGeneratedPathFullscreen: (path: string, constrainToMainFrame?: boolean) => void;
  /**
   * Same host fullscreen as `openGeneratedPathFullscreen`, but ← → cycles the current
   * chat context strip (`selection`) instead of transcript image bubbles.
   */
  openSelectionPathFullscreen: (path: string, constrainToMainFrame?: boolean) => void;
  /**
   * Select a file in the File Tree panel (shift+click on paths).
   */
  selectPathInExplorer: (path: string) => void;
};

function buildChatWebDoc(s: PmChatStore) {
  return {
    version: CHAT_WEB_VERSION,
    prompt_history: s.promptHistory,
    quick_actions: s.quickActions,
    qa_ui: {
      toolbar: s.qaToolbarOpen,
      presets_group: s.qaPresetsGroupOpen,
      presets: s.qaPresetsGroupOpen,
      design: s.qaPresetsGroupOpen,
      resize: s.qaPresetsGroupOpen,
      providers: s.qaProvidersOpen,
      text_llm: s.qaProvidersOpen,
      image: s.qaProvidersOpen,
      video: s.qaProvidersOpen,
    },
    router_quick: s.routerQuick,
    model_quick: s.modelQuick,
    design_presets: s.designPresets,
    resize_presets: s.resizePresets,
  };
}

export const usePmChatStore = create<PmChatStore>((set, get) => ({
  i18nRev: 0,
  selection: [],
  explorerSelection: [],
  contextExtraPaths: [],
  folder: "",
  selectionTotalBytes: 0,
  workspace: "",
  busy: false,
  savedChatRouter: "",
  savedChatModel: "",
  routerQuick: _quickLlm.router,
  modelQuick: _quickLlm.model,
  llmModelRows: [],
  llmModelRowsLoading: false,
  llmModelRowsRouter: "",
  entries: [],
  nextId: 1,
  inputDraft: "",
  sttRecording: false,
  ttsPlaying: false,
  sttPartial: "",
  sttLevel: 0,
  videoSnapBusy: false,
  promptHistory: loadPromptHistory(),
  historyIndex: -1,
  quickActions: loadQuickActions(),
  resizePresets: loadResizePresets(),
  designPresets: loadDesignPresets(),
  showQuickActionEditor: false,
  qaEditDraft: null,
  showDesignPresetEditor: false,
  designEditDraft: null,
  showResizePresetEditor: false,
  resizeEditDraft: null,
  qaToolbarOpen: _qa.toolbar,
  qaPresetsGroupOpen: _qa.presetsGroup,
  qaProvidersOpen: _qa.providers,
  imageProvider: "replicate",
  imageModel: "",
  replicateCollection: "official",
  videoProvider: "replicate",
  videoModel: "",
  videoReplicateCollection: "official",
  sttProvider: "",
  sttModel: "",
  ttsProvider: "",
  ttsModel: "",
  ttsVoiceId: "",
  imageRecogProvider: "",
  imageRecogModel: "",
  imageProviderRows: [],
  imageModelRows: [],
  videoModelRows: [],
  videoOpenApiFlat: null,
  videoOpenApiFlatLoading: false,
  videoOpenApiFlatError: null,
  videoOpenApiFieldValues: {},
  imageOpenApiFlat: null,
  imageOpenApiFlatLoading: false,
  imageOpenApiFlatError: null,
  imageOpenApiFieldValues: {},
  replicateCollections: [],
  providerUiBooting: false,
  activeSessionId: "",
  pastSessionsOpen: readPastSessionsOpen(),
  agentToolEnabled: _agentToolEnabled,
  mcpToolsEnabled: loadMcpToolsEnabled(),
  disabledMcpServers: loadDisabledMcpServers(),
  mcpCatalog: null,
  embedFeatures: { pixlwizAuth: true, pixlwizShare: true },
  showToolCalls: readShowToolCalls(),
  filmstripVisible: loadFilmstripVisible(),
  agentJsonText: null,
  pixlwizCredit: null,

  schedulePostChatWebToHost() {
    if (suppressChatWebHostPost) return;
    if (!hasWebProviderHost()) return;
    if (chatWebToHostTimer) clearTimeout(chatWebToHostTimer);
    chatWebToHostTimer = setTimeout(() => {
      chatWebToHostTimer = null;
      postHost({ kind: "chatWebState", doc: buildChatWebDoc(get()) });
    }, 300);
  },

  setPixlwizCredit(c) {
    set({ pixlwizCredit: c });
  },

  saveQuickLlmToStorage() {
    const s = get();
    try {
      localStorage.setItem(LS_QUICK_LLM, JSON.stringify({ r: s.routerQuick, m: s.modelQuick }));
    } catch {
      /* */
    }
    s.schedulePostChatWebToHost();
  },

  savePromptHistoryToStorage(list: string[]) {
    try {
      localStorage.setItem(LS_PROMPT_HISTORY, JSON.stringify(list));
    } catch {
      /* */
    }
    get().schedulePostChatWebToHost();
  },

  saveQuickActionsToStorage(list: QuickAction[]) {
    try {
      localStorage.setItem(LS_QUICK_ACTIONS, JSON.stringify(list));
    } catch {
      /* */
    }
    get().schedulePostChatWebToHost();
  },

  saveDesignPresetsToStorage(list: DesignPreset[]) {
    writeDesignPresetsLocal(list);
    get().schedulePostChatWebToHost();
  },

  saveResizePresetsToStorage(list: ResizePreset[]) {
    writeResizePresetsLocal(list);
    get().schedulePostChatWebToHost();
  },

  saveQaUiState() {
    const s = get();
    try {
      localStorage.setItem(
        LS_QA_UI,
        JSON.stringify({
          toolbar: s.qaToolbarOpen,
          presetsGroup: s.qaPresetsGroupOpen,
          presets: s.qaPresetsGroupOpen,
          design: s.qaPresetsGroupOpen,
          resize: s.qaPresetsGroupOpen,
          providers: s.qaProvidersOpen,
          textLlm: s.qaProvidersOpen,
          image: s.qaProvidersOpen,
          video: s.qaProvidersOpen,
        }),
      );
    } catch {
      /* */
    }
    s.schedulePostChatWebToHost();
  },

  addPromptToHistory(text: string) {
    const t0 = String(text || "").trim();
    if (!t0 || get().promptHistory[0] === t0) return;
    const list = [t0, ...get().promptHistory.filter((p) => p !== t0)].slice(0, MAX_PROMPT_HISTORY);
    set({ promptHistory: list, historyIndex: -1 });
    get().savePromptHistoryToStorage(list);
  },

  navigateHistory(dir: "up" | "down") {
    const s = get();
    if (s.promptHistory.length === 0 || s.busy) return;
    let hi = s.historyIndex;
    if (dir === "up") {
      hi = Math.min(hi + 1, s.promptHistory.length - 1);
    } else {
      hi = Math.max(hi - 1, -1);
    }
    set({
      historyIndex: hi,
      inputDraft: hi < 0 ? "" : s.promptHistory[hi] ?? "",
    });
  },

  setInputDraft(v: string) {
    set({ inputDraft: v, historyIndex: -1 });
  },

  setSttRecording(v: boolean) {
    set({ sttRecording: v, sttPartial: v ? get().sttPartial : "", sttLevel: v ? get().sttLevel : 0 });
  },
  setTtsPlaying(v: boolean) {
    set({ ttsPlaying: v });
  },

  setSttPartial(v: string) {
    set({ sttPartial: v });
  },

  setSttLevel(v: number) {
    const level = Math.max(0, Math.min(1, Number(v) || 0));
    set({ sttLevel: level });
  },

  appendSttCommitted(text: string) {
    const draft = get().inputDraft;
    const sep = draft && !draft.endsWith(" ") ? " " : "";
    set({ inputDraft: draft + sep + text, sttPartial: "", historyIndex: -1 });
  },

  setVideoSnapBusy(v: boolean) {
    set({ videoSnapBusy: v });
  },

  effectiveRouterId() {
    const s = get();
    return s.routerQuick || s.savedChatRouter || "openrouter";
  },

  send() {
    const s = get();
    const prompt = String(s.inputDraft ?? "").trim();
    if (!prompt || s.busy) return;
    s.addPromptToHistory(prompt);
    const id = s.nextId;
    const now = Date.now();
    const contextPaths = [...s.selection].filter((p) => String(p || "").trim().length > 0);
    const folderHint = String(s.folder || "").trim();
    set({
      inputDraft: "",
      entries: [
        ...s.entries,
        {
          id,
          role: "user" as const,
          text: prompt,
          ts: now,
          ...(folderHint ? { folderHint } : {}),
          ...(contextPaths.length ? { contextPaths } : {}),
        },
      ],
      nextId: id + 1,
      historyIndex: -1,
    });
    const sid = get().activeSessionId;
    if (sid) {
      if (typeof console !== "undefined" && console.log) {
        console.log("[pm-chat:store] send → persistChatSessionSnapshot", sid, "entries", get().entries.length);
      }
      void persistChatSessionSnapshot(sid, get().entries);
      // Move to top of recent sessions only when adding activity (not just opening)
      recordSessionVisited(sid, true);
    }
    const msg: Record<string, unknown> = { kind: "send", prompt };
    if (s.routerQuick) {
      msg.router = s.routerQuick;
      msg.model = s.modelQuick || defaultModelForRouterId(s.routerQuick);
    } else if (s.modelQuick) {
      msg.model = s.modelQuick;
    }
    const disabled = disabledPathToolsForSend(s.agentToolEnabled);
    if (disabled.length) msg.disable_tools = disabled;
    if (!s.mcpToolsEnabled) msg.mcp_tools_enabled = false;
    if (s.mcpToolsEnabled && s.disabledMcpServers.length > 0)
      msg.disabled_mcp_servers = s.disabledMcpServers;
    if (s.videoProvider === "replicate" && s.videoOpenApiFieldValues && typeof s.videoOpenApiFieldValues === "object") {
      const create_video_ui = coerceOpenApiFieldValuesForHost(s.videoOpenApiFieldValues);
      if (Object.keys(create_video_ui).length) msg.create_video_ui = create_video_ui;
    }
    postHost(msg);
  },

  clearTranscript() {
    set({ entries: [] });
    postHost({ kind: "clear" });
  },

  deleteEntry(id: number) {
    const s = get();
    const entries = s.entries.filter((e) => e.id !== id);
    set({ entries });
    const sid = get().activeSessionId;
    if (sid) {
      void persistChatSessionSnapshot(sid, entries);
    }
  },

  postStop() {
    postHost({ kind: "stop" });
  },

  applyQuickAction(action: QuickAction) {
    const s = get();
    if (s.busy || !action) return;
    const p = String(action.prompt || "");
    if (!p) return;
    const draft = String(s.inputDraft || "").trimEnd();
    set({ inputDraft: draft ? `${draft}\n\n${p}` : p, historyIndex: -1 });
  },

  applyResizePreset(preset: ResizePreset) {
    const s = get();
    if (s.busy || !preset) return;
    const p = expandedResizePresetPrompt(preset).trim();
    if (!p) return;
    const draft = String(s.inputDraft || "").trimEnd();
    set({ inputDraft: draft ? `${draft}\n\n${p}` : p, historyIndex: -1 });
  },

  toggleQaToolbar() {
    set((st) => ({ qaToolbarOpen: !st.qaToolbarOpen }));
    get().saveQaUiState();
  },

  toggleQaPresetsGroup() {
    set((st) => {
      const next = !st.qaPresetsGroupOpen;
      if (!next) {
        return {
          qaPresetsGroupOpen: false,
          showQuickActionEditor: false,
          qaEditDraft: null,
          showDesignPresetEditor: false,
          designEditDraft: null,
          showResizePresetEditor: false,
          resizeEditDraft: null,
        };
      }
      return { qaPresetsGroupOpen: true };
    });
    get().saveQaUiState();
  },

  toggleQaProviders() {
    set((st) => ({ qaProvidersOpen: !st.qaProvidersOpen }));
    get().saveQaUiState();
  },

  setRouterQuick(v: string) {
    const next = String(v || "");
    set({ routerQuick: next, modelQuick: "" });
    get().saveQuickLlmToStorage();
    const effective = next || get().savedChatRouter || "openrouter";
    if (effective === "openrouter" || effective === "openai") {
      void get().fetchLlmModels(effective);
    }
  },

  setModelQuick(v: string) {
    set({ modelQuick: String(v || "") });
    get().saveQuickLlmToStorage();
  },

  async fetchLlmModels(router: string, forceRefresh = false) {
    if (!router) return;
    set({ llmModelRowsLoading: true });
    try {
      const res = await chatProviders.listLlmModels(router, forceRefresh);
      if (res?.ok && Array.isArray(res.data)) {
        set({
          llmModelRows: res.data as { id: string; label: string }[],
          llmModelRowsRouter: router,
        });
      }
    } catch {
      /* best-effort */
    } finally {
      set({ llmModelRowsLoading: false });
    }
  },

  openQaModal() {
    const s = get();
    set({
      showQuickActionEditor: true,
      qaEditDraft: s.quickActions.map((a) => ({ ...a })),
      showDesignPresetEditor: false,
      designEditDraft: null,
      showResizePresetEditor: false,
      resizeEditDraft: null,
    });
  },

  closeQaModal() {
    set({ showQuickActionEditor: false, qaEditDraft: null });
  },

  setQaEditDraft(rows) {
    set({ qaEditDraft: rows });
  },

  resetQaEditDraft() {
    set({ qaEditDraft: cloneDefaultQuickActions() });
  },

  addQaEditRow() {
    const s = get();
    const base = (s.qaEditDraft ?? []).map((a) => ({ ...a }));
    base.push({ id: newQaRowId(), name: "", prompt: "", icon: "\u2728" });
    set({ qaEditDraft: base });
  },

  removeQaEditRow(id: string) {
    const s = get();
    if (!s.qaEditDraft) return;
    set({ qaEditDraft: s.qaEditDraft.filter((r) => r.id !== id) });
  },

  saveQaModal() {
    const s = get();
    const raw = s.qaEditDraft;
    if (!raw || raw.length === 0) {
      window.alert(t("alertNeedAction"));
      return false;
    }
    const missing = raw.find((r) => !String(r.name || "").trim() || !String(r.prompt || "").trim());
    if (missing) {
      window.alert(t("alertNamePrompt"));
      return false;
    }
    const list = raw.map((r) => normalizeQuickAction(r)).filter(Boolean) as QuickAction[];
    if (list.length !== raw.length) {
      window.alert(t("alertInvalid"));
      return false;
    }
    set({ quickActions: list, showQuickActionEditor: false, qaEditDraft: null });
    get().saveQuickActionsToStorage(list);
    return true;
  },

  openDesignPresetModal() {
    const s = get();
    set({
      showDesignPresetEditor: true,
      designEditDraft: s.designPresets.map((r) => ({ ...r })),
      showQuickActionEditor: false,
      qaEditDraft: null,
      showResizePresetEditor: false,
      resizeEditDraft: null,
    });
  },

  closeDesignPresetModal() {
    set({ showDesignPresetEditor: false, designEditDraft: null });
  },

  setDesignEditDraft(rows) {
    set({ designEditDraft: rows });
  },

  resetDesignEditDraft() {
    set({ designEditDraft: designPresetsBundledForDev().map((r) => ({ ...r })) });
  },

  addDesignEditRow() {
    const s = get();
    const base = (s.designEditDraft ?? []).map((r) => ({ ...r }));
    base.push({
      id: newDesignRowId(),
      name: "",
      prompt: "",
      icon: "\u2728",
    });
    set({ designEditDraft: base });
  },

  removeDesignEditRow(id: string) {
    const s = get();
    if (!s.designEditDraft) return;
    set({ designEditDraft: s.designEditDraft.filter((r) => r.id !== id) });
  },

  saveDesignPresetModal() {
    const s = get();
    const raw = s.designEditDraft;
    if (!raw) return false;
    if (raw.length === 0) {
      set({ designPresets: [], showDesignPresetEditor: false, designEditDraft: null });
      get().saveDesignPresetsToStorage([]);
      return true;
    }
    const missing = raw.find((r) => !String(r.name || "").trim() || !String(r.prompt || "").trim());
    if (missing) {
      window.alert(t("alertNamePrompt"));
      return false;
    }
    const list = raw.map((r) => normalizeDesignPresetRow(r)).filter(Boolean) as DesignPreset[];
    if (list.length !== raw.length) {
      window.alert(t("alertInvalid"));
      return false;
    }
    set({ designPresets: list, showDesignPresetEditor: false, designEditDraft: null });
    get().saveDesignPresetsToStorage(list);
    return true;
  },

  openResizePresetModal() {
    const s = get();
    set({
      showResizePresetEditor: true,
      resizeEditDraft: s.resizePresets.map((r) => ({
        ...r,
        target: r.target ? { ...r.target } : undefined,
      })),
      showQuickActionEditor: false,
      qaEditDraft: null,
      showDesignPresetEditor: false,
      designEditDraft: null,
    });
  },

  closeResizePresetModal() {
    set({ showResizePresetEditor: false, resizeEditDraft: null });
  },

  setResizeEditDraft(rows) {
    set({ resizeEditDraft: rows });
  },

  resetResizeEditDraft() {
    set({ resizeEditDraft: resizePresetsBundledForDev().map((r) => ({ ...r, target: r.target ? { ...r.target } : undefined })) });
  },

  addResizeEditRow() {
    const s = get();
    const base = (s.resizeEditDraft ?? []).map((r) => ({ ...r, target: r.target ? { ...r.target } : undefined }));
    base.push({
      id: newResizeRowId(),
      name: "",
      icon: "\u2194",
      group: "",
      prompt:
        "Reframe the attached media to {{TARGET_W}}\u00d7{{TARGET_H}} ({{TARGET_ASPECT}}). Subject: {{SUBJECT_HINT}}. Style: {{STYLE_HINT}}. Do not add text or watermarks.",
      target: { w: 1080, h: 1920, aspect: "9:16" },
    });
    set({ resizeEditDraft: base });
  },

  removeResizeEditRow(id: string) {
    const s = get();
    if (!s.resizeEditDraft) return;
    set({ resizeEditDraft: s.resizeEditDraft.filter((r) => r.id !== id) });
  },

  saveResizePresetModal() {
    const s = get();
    const raw = s.resizeEditDraft;
    if (!raw) return false;
    if (raw.length === 0) {
      set({ resizePresets: [], showResizePresetEditor: false, resizeEditDraft: null });
      get().saveResizePresetsToStorage([]);
      return true;
    }
    const missing = raw.find((r) => !String(r.name || "").trim() || !String(r.prompt || "").trim());
    if (missing) {
      window.alert(t("alertNamePrompt"));
      return false;
    }
    const list = raw.map((r) => normalizeResizePresetRow(r)).filter(Boolean) as ResizePreset[];
    if (list.length !== raw.length) {
      window.alert(t("alertInvalid"));
      return false;
    }
    set({ resizePresets: list, showResizePresetEditor: false, resizeEditDraft: null });
    get().saveResizePresetsToStorage(list);
    return true;
  },

  scheduleSaveImageChatFields() {
    if (saveImageChatTimer) clearTimeout(saveImageChatTimer);
    saveImageChatTimer = setTimeout(() => {
      saveImageChatTimer = null;
      const st = get();
      void chatProviders.saveChatFields({
        image_provider: st.imageProvider,
        image_model: st.imageModel,
        video_provider: st.videoProvider,
        video_model: st.videoModel,
        stt_provider: st.sttProvider,
        stt_model: st.sttModel,
        tts_provider: st.ttsProvider,
        tts_model: st.ttsModel,
        tts_voice_id: st.ttsVoiceId,
        image_recognition_provider: st.imageRecogProvider,
        image_recognition_model: st.imageRecogModel,
      }).catch(() => {});
    }, 450);
  },

  async refreshImageModelsForCurrentProvider() {
    const st = get();
    const prov = st.imageProvider || "replicate";
    if (prov === "replicate") {
      const cols = await chatProviders.listReplicateCollections(false);
      if (cols?.ok && cols.data && typeof cols.data === "object") {
        const d = cols.data as { collections?: ReplicateCollectionRow[] };
        if (Array.isArray(d.collections)) {
          const list = sortReplicateCollections(d.collections);
          set({ replicateCollections: list });
          const cur = get().replicateCollection;
          if (cur && !list.some((c) => (c.slug || c.name) === cur)) {
            const first = list[0];
            set({ replicateCollection: (first && (first.slug || first.name)) || "official" });
          }
        }
      }
      if (st.imageModel) {
        const res = await chatProviders.resolveReplicateCollection(st.imageModel);
        if (res?.ok && res.data && typeof res.data === "object") {
          const c = (res.data as { collection?: string }).collection;
          if (c && typeof c === "string" && c.length) set({ replicateCollection: c });
        }
      }
      const coll = get().replicateCollection || "official";
      const m = await chatProviders.listReplicateModels(coll, false);
      if (m?.ok && m.data && typeof m.data === "object") {
        const dm = m.data as { models?: { slug?: string }[] };
        if (Array.isArray(dm.models)) {
          set({
            imageModelRows: sortImageModelRowList(
              dm.models.map((x) => ({ id: String(x.slug), label: String(x.slug) })),
            ),
          });
        } else {
          set({ imageModelRows: [] });
        }
      } else {
        set({ imageModelRows: [] });
      }
    } else {
      const m = await chatProviders.listImageModels(prov);
      if (m?.ok && Array.isArray(m.data)) {
        set({ imageModelRows: sortImageModelRowList(m.data as ImageModelRow[]) });
      } else {
        set({ imageModelRows: [] });
      }
    }
    void get().refreshImageOpenApiInputFlat();
  },

  async setImageProvider(nextRaw: string) {
    const next = String(nextRaw || "").trim() || "replicate";
    set({ imageProvider: next, providerUiBooting: true });
    await get().refreshImageModelsForCurrentProvider();
    const st = get();
    if (!st.imageModel && st.imageModelRows.length) {
      set({ imageModel: st.imageModelRows[0].id });
    }
    set({ providerUiBooting: false });
    get().scheduleSaveImageChatFields();
    void get().refreshImageOpenApiInputFlat();
  },

  async setReplicateCollection(slug: string) {
    set({ replicateCollection: String(slug || "official") });
    const m = await chatProviders.listReplicateModels(get().replicateCollection, false);
    if (m?.ok && m.data && typeof m.data === "object") {
      const dm = m.data as { models?: { slug?: string }[] };
      if (Array.isArray(dm.models)) {
        set({
          imageModelRows: sortImageModelRowList(
            dm.models.map((x) => ({ id: String(x.slug), label: String(x.slug) })),
          ),
        });
      }
    }
    void get().refreshImageOpenApiInputFlat();
  },

  async refreshReplicateCollectionsAndModels() {
    set({ providerUiBooting: true });
    const cols = await chatProviders.listReplicateCollections(true);
    if (cols?.ok && cols.data && typeof cols.data === "object") {
      const d = cols.data as { collections?: ReplicateCollectionRow[] };
      if (Array.isArray(d.collections)) {
        set({ replicateCollections: sortReplicateCollections(d.collections) });
      }
    }
    const m = await chatProviders.listReplicateModels(get().replicateCollection || "official", true);
    if (m?.ok && m.data && typeof m.data === "object") {
      const dm = m.data as { models?: { slug?: string }[] };
      if (Array.isArray(dm.models)) {
        set({
          imageModelRows: sortImageModelRowList(
            dm.models.map((x) => ({ id: String(x.slug), label: String(x.slug) })),
          ),
        });
      }
    }
    set({ providerUiBooting: false });
    void get().refreshImageOpenApiInputFlat();
  },

  async setImageModel(id: string) {
    set({ imageModel: String(id || "") });
    const st = get();
    if (st.imageProvider === "replicate" && st.imageModel) {
      const res = await chatProviders.resolveReplicateCollection(st.imageModel);
      if (res?.ok && res.data && typeof res.data === "object") {
        const c = (res.data as { collection?: string }).collection;
        if (c && typeof c === "string" && c.length) set({ replicateCollection: c });
      }
    }
    get().scheduleSaveImageChatFields();
    void get().refreshImageOpenApiInputFlat();
  },

  async refreshVideoModelsForCurrentProvider() {
    const st = get();
    const prov = st.videoProvider || "replicate";
    if (prov === "replicate") {
      const cols = await chatProviders.listReplicateCollections(false);
      if (cols?.ok && cols.data && typeof cols.data === "object") {
        const d = cols.data as { collections?: ReplicateCollectionRow[] };
        if (Array.isArray(d.collections)) {
          const list = sortReplicateCollections(d.collections);
          set({ replicateCollections: list });
          const cur = get().videoReplicateCollection;
          if (cur && !list.some((c) => (c.slug || c.name) === cur)) {
            const first = list[0];
            set({ videoReplicateCollection: (first && (first.slug || first.name)) || "official" });
          }
        }
      }
      if (st.videoModel) {
        const res = await chatProviders.resolveReplicateCollection(st.videoModel);
        if (res?.ok && res.data && typeof res.data === "object") {
          const c = (res.data as { collection?: string }).collection;
          if (c && typeof c === "string" && c.length) set({ videoReplicateCollection: c });
        }
      }
      const coll = get().videoReplicateCollection || "official";
      const m = await chatProviders.listReplicateModels(coll, false);
      if (m?.ok && m.data && typeof m.data === "object") {
        const dm = m.data as { models?: { slug?: string }[] };
        if (Array.isArray(dm.models)) {
          set({
            videoModelRows: sortImageModelRowList(
              dm.models.map((x) => ({ id: String(x.slug), label: String(x.slug) })),
            ),
          });
        } else {
          set({ videoModelRows: [] });
        }
      } else {
        set({ videoModelRows: [] });
      }
    } else {
      const m = await chatProviders.listImageModels(prov);
      if (m?.ok && Array.isArray(m.data)) {
        set({ videoModelRows: sortImageModelRowList(m.data as ImageModelRow[]) });
      } else {
        set({ videoModelRows: [] });
      }
    }
    void get().refreshVideoOpenApiInputFlat();
  },

  async setVideoProvider(nextRaw: string) {
    const next = String(nextRaw || "").trim() || "replicate";
    set({ videoProvider: next, providerUiBooting: true });
    await get().refreshVideoModelsForCurrentProvider();
    const st2 = get();
    if (!st2.videoModel && st2.videoModelRows.length) {
      set({ videoModel: st2.videoModelRows[0].id });
    }
    set({ providerUiBooting: false });
    get().scheduleSaveImageChatFields();
    void get().refreshVideoOpenApiInputFlat();
  },

  async setVideoReplicateCollection(slug: string) {
    set({ videoReplicateCollection: String(slug || "official") });
    const m = await chatProviders.listReplicateModels(get().videoReplicateCollection, false);
    if (m?.ok && m.data && typeof m.data === "object") {
      const dm = m.data as { models?: { slug?: string }[] };
      if (Array.isArray(dm.models)) {
        set({
          videoModelRows: sortImageModelRowList(
            dm.models.map((x) => ({ id: String(x.slug), label: String(x.slug) })),
          ),
        });
      }
    }
  },

  async refreshVideoReplicateCollectionsAndModels() {
    set({ providerUiBooting: true });
    const cols = await chatProviders.listReplicateCollections(true);
    if (cols?.ok && cols.data && typeof cols.data === "object") {
      const d = cols.data as { collections?: ReplicateCollectionRow[] };
      if (Array.isArray(d.collections)) {
        set({ replicateCollections: sortReplicateCollections(d.collections) });
      }
    }
    const m = await chatProviders.listReplicateModels(get().videoReplicateCollection || "official", true);
    if (m?.ok && m.data && typeof m.data === "object") {
      const dm = m.data as { models?: { slug?: string }[] };
      if (Array.isArray(dm.models)) {
        set({
          videoModelRows: sortImageModelRowList(
            dm.models.map((x) => ({ id: String(x.slug), label: String(x.slug) })),
          ),
        });
      }
    }
    set({ providerUiBooting: false });
    void get().refreshVideoOpenApiInputFlat();
  },

  async setVideoModel(id: string) {
    set({ videoModel: String(id || "") });
    const st = get();
    if (st.videoProvider === "replicate" && st.videoModel) {
      const res = await chatProviders.resolveReplicateCollection(st.videoModel);
      if (res?.ok && res.data && typeof res.data === "object") {
        const c = (res.data as { collection?: string }).collection;
        if (c && typeof c === "string" && c.length) set({ videoReplicateCollection: c });
      }
    }
    get().scheduleSaveImageChatFields();
    void get().refreshVideoOpenApiInputFlat();
  },

  async setSttProvider(v: string) {
    set({ sttProvider: String(v || "") });
    get().scheduleSaveImageChatFields();
  },
  async setSttModel(v: string) {
    set({ sttModel: String(v || "") });
    get().scheduleSaveImageChatFields();
  },
  async setTtsProvider(v: string) {
    set({ ttsProvider: String(v || "") });
    get().scheduleSaveImageChatFields();
  },
  async setTtsModel(v: string) {
    set({ ttsModel: String(v || "") });
    get().scheduleSaveImageChatFields();
  },
  async setTtsVoiceId(v: string) {
    set({ ttsVoiceId: String(v || "") });
    get().scheduleSaveImageChatFields();
  },
  async setImageRecogProvider(v: string) {
    set({ imageRecogProvider: String(v || "") });
    get().scheduleSaveImageChatFields();
  },
  async setImageRecogModel(v: string) {
    set({ imageRecogModel: String(v || "") });
    get().scheduleSaveImageChatFields();
  },

  setVideoOpenApiFieldValue(key: string, value: string) {
    const k = String(key || "");
    if (!k) return;
    set((s) => ({ videoOpenApiFieldValues: { ...s.videoOpenApiFieldValues, [k]: value } }));
  },

  async refreshVideoOpenApiInputFlat() {
    const st = get();
    const slug = String(st.videoModel || "").trim();
    if (st.videoProvider !== "replicate" || !slug) {
      set({
        videoOpenApiFlat: null,
        videoOpenApiFlatLoading: false,
        videoOpenApiFlatError: null,
        videoOpenApiFieldValues: {},
      });
      return;
    }
    set({ videoOpenApiFlatLoading: true, videoOpenApiFlatError: null });
    const res = await chatProviders.getOpenApiInputFlat(slug);
    if (get().videoModel !== slug || get().videoProvider !== "replicate") {
      set({ videoOpenApiFlatLoading: false });
      void Promise.resolve().then(() => {
        void get().refreshVideoOpenApiInputFlat();
      });
      return;
    }
    if (!res?.ok) {
      set({
        videoOpenApiFlat: null,
        videoOpenApiFlatLoading: false,
        videoOpenApiFlatError: String(res?.error || t("qaVideoOpenApiRpcErr")),
      });
      return;
    }
    if (!res.data) {
      set({
        videoOpenApiFlat: null,
        videoOpenApiFlatLoading: false,
        videoOpenApiFlatError: t("qaVideoOpenApiBadPayload"),
      });
      return;
    }
    const flat = res.data;
    const prevFlat = get().videoOpenApiFlat;
    const prevVals = get().videoOpenApiFieldValues;
    const values = buildReplicateOpenApiFieldValues(flat, prevFlat, prevVals);
    set({
      videoOpenApiFlat: flat,
      videoOpenApiFlatLoading: false,
      videoOpenApiFlatError: null,
      videoOpenApiFieldValues: values,
    });
  },

  setImageOpenApiFieldValue(key: string, value: string) {
    const k = String(key || "");
    if (!k) return;
    set((s) => ({ imageOpenApiFieldValues: { ...s.imageOpenApiFieldValues, [k]: value } }));
  },

  async refreshImageOpenApiInputFlat() {
    const st = get();
    const slug = String(st.imageModel || "").trim();
    if (st.imageProvider !== "replicate" || !slug) {
      set({
        imageOpenApiFlat: null,
        imageOpenApiFlatLoading: false,
        imageOpenApiFlatError: null,
        imageOpenApiFieldValues: {},
      });
      return;
    }
    set({ imageOpenApiFlatLoading: true, imageOpenApiFlatError: null });
    const res = await chatProviders.getOpenApiInputFlat(slug);
    if (get().imageModel !== slug || get().imageProvider !== "replicate") {
      set({ imageOpenApiFlatLoading: false });
      void Promise.resolve().then(() => {
        void get().refreshImageOpenApiInputFlat();
      });
      return;
    }
    if (!res?.ok) {
      set({
        imageOpenApiFlat: null,
        imageOpenApiFlatLoading: false,
        imageOpenApiFlatError: String(res?.error || t("qaImageOpenApiRpcErr")),
      });
      return;
    }
    if (!res.data) {
      set({
        imageOpenApiFlat: null,
        imageOpenApiFlatLoading: false,
        imageOpenApiFlatError: t("qaImageOpenApiBadPayload"),
      });
      return;
    }
    const flat = res.data;
    const prevFlat = get().imageOpenApiFlat;
    const prevVals = get().imageOpenApiFieldValues;
    const values = buildReplicateOpenApiFieldValues(flat, prevFlat, prevVals);
    set({
      imageOpenApiFlat: flat,
      imageOpenApiFlatLoading: false,
      imageOpenApiFlatError: null,
      imageOpenApiFieldValues: values,
    });
  },

  async bootstrapProviderUi() {
    // Do not bail when `providerUiBooting` is already true — a prior in-flight bootstrap may belong
    // to another route/session; skipping here left the new session without a completed load chain.
    set({ providerUiBooting: true });
    try {
      const f = await chatProviders.getChatFields();
      if (f?.ok && f.data && typeof f.data === "object") {
        const d = f.data as ChatProviderFields;
        const patch: Partial<PmChatStore> = {};
        if (d.image_provider) patch.imageProvider = String(d.image_provider);
        if (d.image_model != null) patch.imageModel = String(d.image_model || "");
        if (d.video_provider) patch.videoProvider = String(d.video_provider);
        if (d.video_model != null) patch.videoModel = String(d.video_model || "");
        if (d.stt_provider != null) patch.sttProvider = String(d.stt_provider || "");
        if (d.stt_model    != null) patch.sttModel    = String(d.stt_model    || "");
        if (d.tts_provider != null) patch.ttsProvider = String(d.tts_provider || "");
        if (d.tts_model    != null) patch.ttsModel    = String(d.tts_model    || "");
        if (d.tts_voice_id != null) patch.ttsVoiceId  = String(d.tts_voice_id || "");
        if (d.image_recognition_provider != null) patch.imageRecogProvider = String(d.image_recognition_provider || "");
        if (d.image_recognition_model    != null) patch.imageRecogModel    = String(d.image_recognition_model    || "");
        if (Object.keys(patch).length) set(patch);
      }
      const plist = await chatProviders.listImageProviders();
      if (plist?.ok && Array.isArray(plist.data) && plist.data.length) {
        set({ imageProviderRows: plist.data as ImageProviderRow[] });
      }
      await get().refreshImageModelsForCurrentProvider();
      const st = get();
      if (!st.imageModel && st.imageModelRows.length) {
        set({ imageModel: st.imageModelRows[0].id });
      }
      await get().refreshVideoModelsForCurrentProvider();
      const stv = get();
      if (!stv.videoModel && stv.videoModelRows.length) {
        set({ videoModel: stv.videoModelRows[0].id });
      }
      // Fetch Pixlwiz credit balance via the same RPC channel.
      if (get().embedFeatures.pixlwizAuth) {
        try {
          const cred = await chatProviders.getCreditInfo();
          if (cred.ok && cred.data) {
            const d = cred.data as { spend?: number; max_budget?: number | null };
            set({ pixlwizCredit: {
              spend: typeof d.spend === "number" ? d.spend : 0,
              max: typeof d.max_budget === "number" ? d.max_budget : null,
            }});
          } else if (!cred.ok && !String(cred.error ?? "").startsWith("unknown method")) {
            console.warn("[pm-chat] getCreditInfo:", cred.error ?? "unknown error");
          }
        } catch { /* best-effort */ }
      }
    } catch {
      /* best-effort */
    }
    if (!hasWebProviderHost()) {
      try {
        if (!get().resizePresets.length) {
          const list = resizePresetsBundledForDev();
          if (list.length) {
            set({ resizePresets: list });
            get().saveResizePresetsToStorage(list);
          }
        }
      } catch {
        /* */
      }
      try {
        if (!get().designPresets.length) {
          const dlist = designPresetsBundledForDev();
          if (dlist.length) {
            set({ designPresets: dlist });
            get().saveDesignPresetsToStorage(dlist);
          }
        }
      } catch {
        /* */
      }
    }
    set({ providerUiBooting: false });
    void get().refreshVideoOpenApiInputFlat();
    void get().refreshImageOpenApiInputFlat();
    // Kick off LLM catalog fetch here for dev mode (no setStatus in browser).
    // In production, setStatus fires first and guards against a duplicate fetch.
    const effective = get().routerQuick || get().savedChatRouter || "openrouter";
    if (effective === "openrouter" || effective === "openai") {
      void get().fetchLlmModels(effective);
    }
  },

  injectDevSampleTranscript() {
    set((s): PmChatStore | Partial<PmChatStore> => {
      if (s.entries.length > 0) return s;
      const base = s.nextId;
      const now = Date.now();
      return {
        selection: ["/demo/IMG_0420.JPG", "/demo/IMG_0421.JPG", "/demo/IMG_0422.JPG"],
        explorerSelection: ["/demo/IMG_0420.JPG", "/demo/IMG_0421.JPG", "/demo/IMG_0422.JPG"],
        contextExtraPaths: ["/demo/extra_001.png"],
        folder: "C:\\photos\\trip",
        filmstripVisible: true,
        entries: [
          // Older conversation history
          {
            id: base,
            role: "user" as const,
            text: "Hey, can you help me organize my vacation photos?",
            ts: now - 86400000,
          },
          {
            id: base + 1,
            role: "assistant" as const,
            text: "Absolutely! I can help you organize, compress, tag, and even analyze your vacation photos. What would you like to do?\n\nOptions:\n- Compress to save space\n- Generate tags/descriptions\n- Rename in bulk\n- Create thumbnails",
            ts: now - 86395000,
          },
          {
            id: base + 2,
            role: "user" as const,
            text: "Great, let me pick some photos to work with.",
            ts: now - 43200000,
          },
          // Main workflow with context
          {
            id: base + 3,
            role: "user" as const,
            text: "Compress these as MozJPEG quality 70 and tell me what they show.",
            ts: now - 120_000,
            folderHint: "C:\\photos\\trip",
            contextPaths: ["C:\\photos\\trip\\IMG_0420.JPG", "C:\\photos\\trip\\IMG_0421.JPG"],
            llmUsage: { prompt_tokens: 1450, completion_tokens: 890 },
          },
          { id: base + 4, role: "tool" as const, text: "\u26A1 image_compress over 2 file(s) \u2014 \u2713 2/2" },
          { id: base + 5, role: "tool" as const, text: "\u26A1 image_meta over 2 file(s) \u2014 \u2713 2/2" },
          {
            id: base + 6,
            role: "assistant" as const,
            ts: now - 60_000,
            llmUsage: { prompt_tokens: 2150, completion_tokens: 340, cost: 0.0082 },
            text:
              "Done \u2014 both photos were re-encoded to MozJPEG quality 70:\n\n| File | Original | Compressed | Saved |\n|---|---:|---:|---:|\n| `IMG_0420.JPG` | 4.8 MB | 1.6 MB | 67% |\n| `IMG_0421.JPG` | 5.1 MB | 1.7 MB | 67% |\n\n**What's in the photos:**\n- `IMG_0420.JPG` \u2014 *a hiker on a switchback trail at golden hour, granite peaks in the background.* Tags: `hiking`, `mountains`, `landscape`.\n- `IMG_0421.JPG` \u2014 *a closeup of alpine wildflowers (lupine and paintbrush) against scree.* Tags: `flora`, `macro`.\n\nOutputs are saved next to the originals as `*_compressed.jpg`.",
          },
          // System message example
          {
            id: base + 7,
            role: "system" as const,
            text: "Session auto-saved at 14:32:18",
            ts: now - 55_000,
          },
          // User with generated image result
          {
            id: base + 8,
            role: "user" as const,
            text: "Create a thumbnail grid from the compressed images",
            ts: now - 45_000,
            resultPaths: ["C:\\photos\\trip\\thumbnails\\grid_0420.jpg"],
          },
          {
            id: base + 9,
            role: "tool" as const,
            text: "\u26A1 image_thumbnail over 2 file(s) \u2014 \u2713 2/2",
          },
          {
            id: base + 10,
            role: "image" as const,
            text: "C:\\photos\\trip\\thumbnails\\contact_sheet.jpg",
            ts: now - 40_000,
          },
          {
            id: base + 11,
            role: "assistant" as const,
            ts: now - 38_000,
            text: "Created a contact sheet with 4 thumbnails per row. The grid is saved as `C:\\photos\\trip\\thumbnails\\contact_sheet.jpg` in your thumbnails folder.\n\nOutput files:\n- `C:\\photos\\trip\\thumbnails\\grid_0420.jpg` (1.2 MB)\n- `C:\\photos\\trip\\thumbnails\\grid_0421.jpg` (1.4 MB)",
          },
          // Another user message with different files
          {
            id: base + 12,
            role: "user" as const,
            text: "What about these RAW files from yesterday?",
            ts: now - 25_000,
            contextPaths: ["C:\\photos\\trip\\RAW_001.CR2", "C:\\photos\\trip\\RAW_002.CR2", "C:\\photos\\trip\\RAW_003.CR2"],
          },
          // Error example
          {
            id: base + 13,
            role: "error" as const,
            text: "Failed to process RAW_002.CR2: File appears to be corrupted (truncated at 12MB)",
            ts: now - 24_000,
          },
          // Assistant recovering from error
          {
            id: base + 14,
            role: "assistant" as const,
            ts: now - 20_000,
            text: "I processed 2 out of 3 RAW files. `RAW_002.CR2` appears to be corrupted.\n\nSuccessfully converted:\n- `RAW_001.CR2` \u2192 `RAW_001.jpg` (24.5 MB \u2192 3.2 MB)\n- `RAW_003.CR2` \u2192 `RAW_003.jpg` (26.1 MB \u2192 3.4 MB)\n\nDo you want me to try to recover the corrupted file or skip it?",
          },
          // Video generation example
          {
            id: base + 15,
            role: "user" as const,
            text: "Create a short product video from the moodboard",
            ts: now - 10_000,
            contextPaths: ["C:\\photos\\trip\\thumbnails\\contact_sheet.jpg"],
            folderHint: "C:\\photos\\trip\\thumbnails",
          },
          { id: base + 16, role: "tool" as const, text: "\u26A1 create_video {\"aspect_ratio\":\"16:9\",\"duration\":8} \u2014 Replicate: https://replicate.com/p/8gfmm4pxrhrmy0cxz5bs4x8344" },
          {
            id: base + 17,
            role: "assistant" as const,
            ts: now - 5_000,
            text: "Video generated successfully! \uD83C\uDFAC\n\n**Output:** `video_product_presentation_video_a_branded_pac.mp4`\n*(same folder as the source image)*\n\nThe 8-second clip presents your moodboard as a product being unboxed inside a safari tent in Africa — warm golden light, canvas tent interior, savanna visible through the flap.\n\n[View on Replicate](https://replicate.com/p/8gfmm4pxrhrmy0cxz5bs4x8344)",
            llmUsage: { prompt_tokens: 19107, completion_tokens: 354 },
          },
          // Latest user message (busy example)
          {
            id: base + 18,
            role: "user" as const,
            text: "Generate tags for all the processed images and create a summary report",
            ts: now - 5000,
            folderHint: "C:\\photos\\trip",
          },
        ],
        nextId: base + 19,
      };
    });
  },

  resetSessionSurface() {
    set({
      entries: [],
      nextId: 1,
      inputDraft: "",
      busy: false,
      historyIndex: -1,
      showQuickActionEditor: false,
      qaEditDraft: null,
      showDesignPresetEditor: false,
      designEditDraft: null,
      showResizePresetEditor: false,
      resizeEditDraft: null,
      selection: [],
      explorerSelection: [],
      contextExtraPaths: [],
      folder: "",
      selectionTotalBytes: 0,
      workspace: "",
      agentJsonText: null,
    });
  },

  setActiveSessionId(id: string) {
    set({ activeSessionId: String(id || "") });
  },

  applyLoadedSession(session: StoredChatSession & { agentJsonText?: string }) {
    const allowed: ChatEntryRole[] = ["user", "assistant", "tool", "system", "error", "image", "file", "shell"];
    const entries: ChatEntry[] = session.entries.map((e) => {
      const ctx = (e as { contextPaths?: unknown }).contextPaths;
      const contextPaths =
        Array.isArray(ctx) && ctx.length && ctx.every((x): x is string => typeof x === "string")
          ? ctx.filter((p) => p.trim().length > 0)
          : undefined;
      const rp = (e as { resultPaths?: unknown }).resultPaths;
      const resultPaths =
        Array.isArray(rp) && rp.length && rp.every((x): x is string => typeof x === "string")
          ? rp.filter((p) => p.trim().length > 0)
          : undefined;
      const fh = (e as { folderHint?: unknown }).folderHint;
      const folderHint = typeof fh === "string" && fh.trim() ? fh.trim() : undefined;
      const rawLu = (e as { llmUsage?: unknown }).llmUsage;
      const llmUsage = normalizeTurnLlmUsage(rawLu);
      const rawTp = (e as { toolProvider?: unknown }).toolProvider;
      const toolProvider = typeof rawTp === "string" && rawTp.trim() ? rawTp.trim() : undefined;
      const rawTm = (e as { toolModel?: unknown }).toolModel;
      const toolModel = typeof rawTm === "string" && rawTm.trim() ? rawTm.trim() : undefined;
      return {
        id: e.id,
        role: allowed.includes(e.role as ChatEntryRole) ? (e.role as ChatEntryRole) : "assistant",
        text: e.text,
        ...(typeof e.ts === "number" && Number.isFinite(e.ts) ? { ts: e.ts } : {}),
        ...(folderHint ? { folderHint } : {}),
        ...(contextPaths?.length ? { contextPaths } : {}),
        ...(resultPaths?.length ? { resultPaths } : {}),
        ...(llmUsage ? { llmUsage } : {}),
        ...(toolProvider ? { toolProvider } : {}),
        ...(toolModel ? { toolModel } : {}),
      };
    });
    let maxId = 0;
    for (const e of entries) maxId = Math.max(maxId, e.id);
    const restoredAgentJson = typeof session.agentJsonText === "string" ? session.agentJsonText : null;
    console.log("[pm-chat:agentJson] applyLoadedSession", {
      sessionId: session.id,
      entriesCount: entries.length,
      agentJsonPresent: !!restoredAgentJson,
      agentJsonLen: restoredAgentJson?.length ?? 0,
      rawAgentJsonField: typeof (session as Record<string, unknown>).agentJsonText,
    });
    set({
      entries,
      nextId: maxId + 1,
      inputDraft: "",
      historyIndex: -1,
      busy: false,
      agentJsonText: restoredAgentJson,
    });
  },

  togglePastSessions() {
    set((s) => {
      const next = !s.pastSessionsOpen;
      try {
        localStorage.setItem(LS_PAST_SESSIONS_OPEN, next ? "1" : "0");
      } catch {
        /* */
      }
      return { pastSessionsOpen: next };
    });
  },

  setAgentToolEnabled(id: AgentToolId, v: boolean) {
    set((s) => {
      const next = { ...s.agentToolEnabled, [id]: !!v } as Record<AgentToolId, boolean>;
      saveAgentToolEnabled(next);
      return { agentToolEnabled: next };
    });
  },

  setAgentToolGroupEnabled(ids: AgentToolId[], v: boolean) {
    set((s) => {
      const next = { ...s.agentToolEnabled } as Record<AgentToolId, boolean>;
      for (const id of ids) next[id] = !!v;
      saveAgentToolEnabled(next);
      return { agentToolEnabled: next };
    });
  },

  setMcpToolsEnabled(v: boolean) {
    set({ mcpToolsEnabled: !!v });
    try {
      localStorage.setItem(LS_MCP_TOOLS_ENABLED, v ? "1" : "0");
    } catch {
      /* */
    }
  },

  setDisabledMcpServers(servers: string[]) {
    set({ disabledMcpServers: servers });
    try {
      localStorage.setItem(LS_DISABLED_MCP_SERVERS, JSON.stringify(servers));
    } catch {
      /* */
    }
  },

  toggleShowToolCalls() {
    set((st) => {
      const next = !st.showToolCalls;
      try {
        localStorage.setItem(LS_SHOW_TOOL_CALLS, next ? "1" : "0");
      } catch {
        /* */
      }
      return { showToolCalls: next };
    });
  },

  toggleFilmstrip() {
    set((st) => {
      const next = !st.filmstripVisible;
      try {
        localStorage.setItem(LS_FILMSTRIP_VISIBLE, next ? "1" : "0");
      } catch {
        /* */
      }
      return { filmstripVisible: next };
    });
  },

  setAgentJson(text: string | null) {
    console.log("[pm-chat:agentJson] setAgentJson called", { hasText: !!text, len: text?.length ?? 0 });
    set({ agentJsonText: text });
  },

  setRunFolder(folder: string) {
    if (!folder) return;
    const label = folder.replace(/\\/g, "/").replace(/\/$/, "").split("/").slice(-2).join("/");
    set((s) => ({
      entries: [
        ...s.entries,
        {
          id: s.nextId,
          role: "system" as const,
          text: `\u{1F4C2} ${label}`,
          folderHint: folder,
          ts: Date.now(),
        },
      ],
      nextId: s.nextId + 1,
    }));
  },

  appendText({ role, text, toolProvider, toolModel, durationMs }) {
    const r = (role || "assistant") as ChatEntryRole;
    const t0 = String(text || "").trim();
    const ts = Date.now();

    if (r === "image" && t0) {
      set((s) => {
        let userIdx = -1;
        for (let i = s.entries.length - 1; i >= 0; i -= 1) {
          if (s.entries[i].role === "user") {
            userIdx = i;
            break;
          }
        }
        if (userIdx >= 0) {
          const user = s.entries[userIdx];
          const prev = user.resultPaths ?? [];
          if (prev.some((p) => p.localeCompare(t0, undefined, { sensitivity: "base" }) === 0)) {
            return s;
          }
          const resultPaths = [...prev, t0];
          const entries = s.entries.map((e, i) => (i === userIdx ? { ...e, resultPaths } : e));
          return { entries };
        }
        return {
          entries: [...s.entries, { id: s.nextId, role: "image" as const, text: t0, ts }],
          nextId: s.nextId + 1,
        };
      });
      return;
    }

    set((s) => ({
      entries: [
        ...s.entries,
        {
          id: s.nextId,
          role: r,
          text: String(text || ""),
          ts,
          ...(toolProvider ? { toolProvider } : {}),
          ...(toolModel ? { toolModel } : {}),
          ...(durationMs != null && durationMs >= 0 ? { durationMs } : {}),
        },
      ],
      nextId: s.nextId + 1,
    }));
  },

  startRun(runId: string, command: string) {
    set((s) => {
      const idx = findLastChatEntryIndex(s.entries,
        (e) => e.role === "shell" && e.runId === runId,
      );
      if (idx >= 0) {
        const entries = s.entries.map((e, i) =>
          i === idx ? { ...e, runCommand: command, runActive: true } : e,
        );
        return { entries };
      }
      return {
        entries: [
          ...s.entries,
          {
            id: s.nextId,
            role: "shell" as const,
            text: "",
            runId,
            runCommand: command,
            runActive: true,
            shellOutput: { stdout: "", stderr: "" },
            ts: Date.now(),
          },
        ],
        nextId: s.nextId + 1,
      };
    });
  },

  appendRunChunk(runId: string, chunk: string, stream: "stdout" | "stderr") {
    set((s) => {
      const idx = findLastChatEntryIndex(s.entries,
        (e) => e.role === "shell" && e.runId === runId,
      );
      if (idx < 0) {
        const out = { stdout: "", stderr: "" };
        out[stream] = chunk;
        return {
          entries: [
            ...s.entries,
            {
              id: s.nextId,
              role: "shell" as const,
              text: "",
              runId,
              shellOutput: out,
              ts: Date.now(),
            },
          ],
          nextId: s.nextId + 1,
        };
      }
      const entry = s.entries[idx]!;
      const prev = entry.shellOutput ?? { stdout: "", stderr: "" };
      const next = { ...prev, [stream]: prev[stream] + chunk };
      const entries = s.entries.map((e, i) =>
        i === idx ? { ...e, shellOutput: next } : e,
      );
      return { entries };
    });
  },

  finishRun(runId: string, exitCode: number, durationMs: number) {
    set((s) => {
      const idx = findLastChatEntryIndex(s.entries,
        (e) => e.role === "shell" && e.runId === runId,
      );
      if (idx < 0) return {};
      const entries = s.entries.map((e, i) =>
        i === idx ? { ...e, runActive: false, runExitCode: exitCode, runDurationMs: durationMs } : e,
      );
      return { entries };
    });
  },

  setTurnLlmUsage(raw) {
    const llmUsage = normalizeTurnLlmUsage(raw);
    if (!llmUsage) return;
    set((s) => {
      let lastAsst = -1;
      for (let i = s.entries.length - 1; i >= 0; i -= 1) {
        if (s.entries[i].role === "assistant") {
          lastAsst = i;
          break;
        }
      }
      if (lastAsst < 0) return s;
      let userIdx = -1;
      for (let j = lastAsst - 1; j >= 0; j -= 1) {
        if (s.entries[j].role === "user") {
          userIdx = j;
          break;
        }
      }
      if (userIdx < 0) return s;
      const entries = s.entries.map((e, idx) =>
        idx === userIdx || idx === lastAsst ? { ...e, llmUsage } : e,
      );
      return { entries };
    });
  },

  appendUser(text: string) {
    const ts = Date.now();
    set((s) => ({
      entries: [...s.entries, { id: s.nextId, role: "user" as const, text: String(text || ""), ts }],
      nextId: s.nextId + 1,
    }));
  },

  setStatus({
    selection,
    explorer_selection,
    context_extra,
    folder,
    selection_bytes,
    selectionBytes,
    workspace: workspacePath,
    saved_chat_router,
    saved_chat_model,
    features,
    mcp_catalog,
  }) {
    set((s) => {
      const nextEmbed =
        features && typeof features === "object"
          ? {
              pixlwizAuth: features.pixlwizAuth !== false,
              pixlwizShare: features.pixlwizShare !== false,
            }
          : null;
      let nextCatalog = s.mcpCatalog;
      if (mcp_catalog === null) nextCatalog = null;
      else if (mcp_catalog !== undefined && typeof mcp_catalog === "object")
        nextCatalog = mcp_catalog as McpCatalogPayload;
      const rawBytes = selection_bytes ?? selectionBytes;
      let selectionTotalBytes = s.selectionTotalBytes;
      if (typeof rawBytes === "number" && Number.isFinite(rawBytes) && rawBytes >= 0) {
        selectionTotalBytes = Math.min(Math.floor(rawBytes), Number.MAX_SAFE_INTEGER);
      } else if (selection !== undefined) {
        selectionTotalBytes = 0;
      }
      const merged = Array.isArray(selection) ? selection : s.selection;
      const legacyMergedOnly =
        explorer_selection === undefined &&
        context_extra === undefined &&
        selection !== undefined;
      let explorerSelection = s.explorerSelection;
      let contextExtraPaths = s.contextExtraPaths;
      if (legacyMergedOnly) {
        explorerSelection = merged;
        contextExtraPaths = [];
      } else {
        if (Array.isArray(explorer_selection)) explorerSelection = explorer_selection;
        if (Array.isArray(context_extra)) contextExtraPaths = context_extra;
      }
      return {
        selection: merged,
        explorerSelection,
        contextExtraPaths,
        folder: typeof folder === "string" ? folder : s.folder,
        selectionTotalBytes,
        workspace: typeof workspacePath === "string" ? workspacePath : s.workspace,
        savedChatRouter: typeof saved_chat_router === "string" ? saved_chat_router : s.savedChatRouter,
        savedChatModel: typeof saved_chat_model === "string" ? saved_chat_model : s.savedChatModel,
        ...(nextEmbed ? { embedFeatures: nextEmbed } : {}),
        ...(mcp_catalog !== undefined ? { mcpCatalog: nextCatalog } : {}),
      };
    });
    // Fetch live LLM catalog once the host has delivered the saved router.
    // setStatus is the first reliable moment that the WebView2 bridge is live.
    if (typeof saved_chat_router === "string" && saved_chat_router) {
      const st = get();
      const effective = st.routerQuick || saved_chat_router;
      if ((effective === "openrouter" || effective === "openai") && st.llmModelRows.length === 0) {
        void get().fetchLlmModels(effective);
      }
    }
  },

  removeSelectionPath(path: string) {
    const p = String(path || "").trim();
    if (!p) return;
    if (hasWebProviderHost()) {
      postHost({ kind: "removeContextPaths", paths: [p] });
      return;
    }
    set((s) => ({
      selection: s.selection.filter((x) => x.localeCompare(p, undefined, { sensitivity: "base" }) !== 0),
      explorerSelection: s.explorerSelection.filter(
        (x) => x.localeCompare(p, undefined, { sensitivity: "base" }) !== 0,
      ),
      contextExtraPaths: s.contextExtraPaths.filter(
        (x) => x.localeCompare(p, undefined, { sensitivity: "base" }) !== 0,
      ),
      selectionTotalBytes: 0,
    }));
  },

  setBusy(busy: boolean) {
    const wasBusy = get().busy;
    set({ busy: !!busy });
    // After each completed turn, refresh the credit balance via the RPC channel.
    if (wasBusy && !busy && get().embedFeatures.pixlwizAuth && hasWebProviderHost()) {
      void chatProviders.getCreditInfo().then((cred) => {
        if (cred.ok && cred.data) {
          const d = cred.data as { spend?: number; max_budget?: number | null };
          set({ pixlwizCredit: {
            spend: typeof d.spend === "number" ? d.spend : 0,
            max: typeof d.max_budget === "number" ? d.max_budget : null,
          }});
        } else if (!cred.ok && !String(cred.error ?? "").startsWith("unknown method")) {
          console.warn("[pm-chat] getCreditInfo (post-turn):", cred.error ?? "unknown error");
        }
      }).catch(() => {});
    }
  },

  setLocale(code: string) {
    setUiLocale(code);
    set((s) => ({ i18nRev: s.i18nRev + 1 }));
  },

  clear() {
    set({ entries: [] });
  },

  setFontExtraPt(extraPt: number) {
    const v = Math.max(0, Math.min(8, Number(extraPt) || 0));
    const px = 13 + v;
    document.documentElement.style.setProperty("--pm-font-extra-pt", String(v));
    document.documentElement.style.setProperty("--pm-font-size", `${px}px`);
  },

  applyHostPersistence(doc: unknown) {
    if (!doc || typeof doc !== "object") return;
    const d = doc as Record<string, unknown>;
    withSuppressChatWebHostPost(() => {
      if (Array.isArray(d.prompt_history)) {
        const p = d.prompt_history.filter((x): x is string => typeof x === "string");
        if (p.length) {
          const list = p.slice(0, MAX_PROMPT_HISTORY);
          set({ promptHistory: list, historyIndex: -1 });
          try {
            localStorage.setItem(LS_PROMPT_HISTORY, JSON.stringify(list));
          } catch {
            /* */
          }
        }
      }
      if (Array.isArray(d.quick_actions) && d.quick_actions.length > 0) {
        const out = d.quick_actions.map(normalizeQuickAction).filter(Boolean) as QuickAction[];
        if (out.length) {
          set({ quickActions: out });
          try {
            localStorage.setItem(LS_QUICK_ACTIONS, JSON.stringify(out));
          } catch {
            /* */
          }
        }
      }
      if (Array.isArray(d.resize_presets)) {
        const rp = resizePresetsFromUnknownDoc({ resize_presets: d.resize_presets });
        set({ resizePresets: rp });
        try {
          writeResizePresetsLocal(rp);
        } catch {
          /* */
        }
      }
      if (Array.isArray(d.design_presets)) {
        const dp = designPresetsFromUnknownDoc({ design_presets: d.design_presets });
        set({ designPresets: dp });
        try {
          writeDesignPresetsLocal(dp);
        } catch {
          /* */
        }
      }
      if (d.qa_ui && typeof d.qa_ui === "object") {
        const u = d.qa_ui as Record<string, unknown>;
        const providersOpen =
          typeof u.providers === "boolean"
            ? u.providers !== false
            : (u.text_llm !== false) || (u.image !== false) || (u.video !== false);
        set({
          qaToolbarOpen: u.toolbar !== false,
          qaPresetsGroupOpen:
            typeof u.presets_group === "boolean"
              ? u.presets_group !== false
              : typeof u.presetsGroup === "boolean"
                ? u.presetsGroup !== false
                : (u.presets !== false) || (u.design !== false) || (u.resize !== false),
          qaProvidersOpen: providersOpen,
        });
        get().saveQaUiState();
      }
      if (d.router_quick !== undefined && d.router_quick !== null) {
        set({ routerQuick: String(d.router_quick) });
      }
      if (d.model_quick !== undefined && d.model_quick !== null) {
        set({ modelQuick: String(d.model_quick) });
      }
      if (d.router_quick !== undefined || d.model_quick !== undefined) {
        const st = get();
        try {
          localStorage.setItem(LS_QUICK_LLM, JSON.stringify({ r: st.routerQuick, m: st.modelQuick }));
        } catch {
          /* */
        }
      }
      set({ historyIndex: -1 });
    });
  },

  openGeneratedPath(path: string) {
    if (hasWebProviderHost()) {
      postHost({ kind: "openPathDefault", path });
    } else {
      const u = rewriteLocalPath(path);
      try {
        window.open(u, "_blank", "noopener");
      } catch {
        // eslint-disable-next-line no-console
        console.log("[pm-chat] open", path, u);
      }
    }
  },

  openPathInternal(path: string) {
    const trimmed = (path || "").trim();
    console.log("[pm-chat] openPathInternal", trimmed);
    if (!trimmed) return;
    if (hasWebProviderHost()) {
      postHost({ kind: "openPathInternal", path: trimmed });
    } else {
      const u = rewriteLocalPath(trimmed);
      try {
        window.open(u, "_blank", "noopener");
      } catch {
        // eslint-disable-next-line no-console
        console.log("[pm-chat] openPathInternal (no webview)", trimmed, u);
      }
    }
  },

  openGeneratedPathFullscreen(path: string, constrainToMainFrame?: boolean) {
    const trimmed = (path || "").trim();
    if (!trimmed) return;
    const listOrdered: string[] = [];
    const pushUnique = (p: string) => {
      const x = p.trim();
      if (!x) return;
      if (listOrdered.some((q) => q.localeCompare(x, undefined, { sensitivity: "base" }) === 0)) return;
      listOrdered.push(x);
    };
    for (const e of get().entries) {
      if (e.role === "image") pushUnique(e.text || "");
      if (e.role === "user" && e.resultPaths?.length) {
        for (const rp of e.resultPaths) pushUnique(rp);
      }
    }
    const paths = listOrdered;
    let list = paths;
    let index = list.indexOf(trimmed);
    if (index < 0) {
      list = [...paths, trimmed];
      index = list.length - 1;
    }
    if (hasWebProviderHost()) {
      const payload: Record<string, unknown> = {
        kind: "openPathFullscreen",
        path: trimmed,
        paths: list,
        index,
      };
      if (constrainToMainFrame) payload.constrainToMainFrame = true;
      postHost(payload);
    } else {
      const u = rewriteLocalPath(trimmed);
      try {
        window.open(u, "_blank", "noopener");
      } catch {
        // eslint-disable-next-line no-console
        console.log("[pm-chat] open fullscreen (no webview)", trimmed, u);
      }
    }
  },

  openSelectionPathFullscreen(path: string, constrainToMainFrame?: boolean) {
    const trimmed = (path || "").trim();
    if (!trimmed) return;
    const sel = get().selection.map((p) => p.trim()).filter(Boolean);
    let list = sel;
    let index = list.findIndex((p) => p.localeCompare(trimmed, undefined, { sensitivity: "base" }) === 0);
    if (index < 0) {
      list = [...sel, trimmed];
      index = list.length - 1;
    }
    if (hasWebProviderHost()) {
      const payload: Record<string, unknown> = {
        kind: "openPathFullscreen",
        path: trimmed,
        paths: list,
        index,
      };
      if (constrainToMainFrame) payload.constrainToMainFrame = true;
      postHost(payload);
    } else {
      const u = rewriteLocalPath(trimmed);
      try {
        window.open(u, "_blank", "noopener");
      } catch {
        // eslint-disable-next-line no-console
        console.log("[pm-chat] open fullscreen selection (no webview)", trimmed, u);
      }
    }
  },

  selectPathInExplorer(path: string) {
    const trimmed = (path || "").trim();
    if (!trimmed) return;
    if (hasWebProviderHost()) {
      postHost({ kind: "selectPathInExplorer", path: trimmed });
    }
  },
}));
