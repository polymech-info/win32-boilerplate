import { Link, useNavigate } from "@tanstack/react-router";
import { useCallback, useEffect, useMemo, useState } from "react";
import { ModelStringTypeahead } from "@pm/shared/components/ModelStringTypeahead";

import {
  CHAT_ROUTERS,
  PIXLWIZ_AUDIO_PROVIDERS,
  PIXLWIZ_IMAGE_MODELS,
  PIXLWIZ_IMAGE_RECOG_MODELS,
  PIXLWIZ_STT_MODELS,
  PIXLWIZ_STT_MODEL_LABELS,
  PIXLWIZ_TTS_MODELS,
  PIXLWIZ_TTS_MODEL_LABELS,
  PIXLWIZ_VIDEO_MODELS,
  AUDIO_PROVIDER_LABELS,
  ELEVENLABS_STT_MODELS,
  ELEVENLABS_TTS_MODELS,
  ELEVENLABS_TTS_VOICES,
  modelListForRouterId,
  sortAlpha,
  sortImageModelRowList,
  sortReplicateCollections,
} from "./chatConstants";
import { deleteChatSession, listChatSessionsMeta, subscribeChatSessionsChanged } from "./chatSessionsBackend";
import { layoutDesignPresetGroups } from "./designPresets";
import { t } from "./i18n.js";
import type { ChatSessionMeta } from "./chatSessionStorage";
import { usePmChatStore, type McpCatalogServer, type AgentToolId } from "./pmChatStore";
import { expandedResizePresetPrompt, layoutResizePresetGroups, type ResizePreset } from "./resizePresets";
import { IconChevLeft, IconPastSessions, IconPencil, IconRefresh } from "./svgIcons";
import { VideoOpenApiToolbarFields } from "./videoOpenApiToolbarFields";

function qaChev(open: boolean): string {
  return open ? "\u25BC" : "\u25B6";
}

// ── Individual tool entries ───────────────────────────────────────────────────
type ToolEntry =
  | { kind: "tool";  id: AgentToolId; labelKey: string }
  | { kind: "group"; id: string; labelKey: string; members: AgentToolId[] };

const TOOL_ENTRIES: ToolEntry[] = [
  { kind: "tool",  id: "list_images",        labelKey: "toolListImages" },
  { kind: "tool",  id: "file_glob",           labelKey: "toolFileGlob" },
  { kind: "tool",  id: "file_read",           labelKey: "toolFileRead" },
  { kind: "tool",  id: "file_search",         labelKey: "toolFileSearch" },
  { kind: "tool",  id: "image_resize",        labelKey: "toolImageResize" },
  { kind: "tool",  id: "image_transform",     labelKey: "toolImageTransform" },
  { kind: "tool",  id: "image_create",        labelKey: "toolImageCreate" },
  { kind: "tool",  id: "create_video",        labelKey: "toolCreateVideo" },
  { kind: "tool",  id: "image_understand",    labelKey: "toolImageUnderstand" },
  { kind: "tool",  id: "image_from_camera",   labelKey: "toolImageFromCamera" },
  { kind: "tool",  id: "write_file",          labelKey: "toolWriteFile" },
  { kind: "tool",  id: "run",                 labelKey: "toolRun" },
  { kind: "tool",  id: "speak",               labelKey: "toolSpeak" },
  { kind: "group", id: "_scheduler", labelKey: "toolGroupScheduler",
    members: ["schedule_at", "schedule_in", "schedule_every", "schedule_cancel", "schedule_list"] },
  { kind: "group", id: "_memory",    labelKey: "toolGroupMemory",
    members: ["memory_read", "memory_write", "memory_append_event"] },
];

function SidebarPathToolsSection() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const busy = usePmChatStore((s) => s.busy);
  const agentToolEnabled = usePmChatStore((s) => s.agentToolEnabled);
  const setAgentToolEnabled = usePmChatStore((s) => s.setAgentToolEnabled);
  const setAgentToolGroupEnabled = usePmChatStore((s) => s.setAgentToolGroupEnabled);
  const [open, setOpen] = useState(() => {
    try {
      return localStorage.getItem("pm-chat-next.sidebar.pathToolsOpen") === "1";
    } catch {
      return false;
    }
  });

  const persistOpen = (next: boolean) => {
    setOpen(next);
    try {
      localStorage.setItem("pm-chat-next.sidebar.pathToolsOpen", next ? "1" : "0");
    } catch {
      /* */
    }
  };

  const rowCls = `flex cursor-pointer items-center gap-2 rounded-md px-1 py-0.5 hover:bg-slate-200/70 dark:hover:bg-surface-mute/60 ${busy ? "pointer-events-none opacity-50" : ""}`;
  const cbCls = "h-4 w-4 shrink-0 rounded border-slate-400 text-violet-600 focus:ring-violet-500 dark:border-slate-500 dark:bg-surface-dark";

  return (
    <div className="border-t border-slate-200 dark:border-surface-mute" title={t("sidebarPathToolsHint")}>
      <button
        type="button"
        className="flex w-full items-center gap-1.5 px-2 py-1.5 text-left text-[13px] font-semibold leading-snug tracking-wide text-slate-500 uppercase transition-colors hover:bg-slate-200/60 dark:text-slate-400 dark:hover:bg-surface-mute/50"
        aria-expanded={open}
        onClick={() => persistOpen(!open)}
      >
        <span className="flex w-5 flex-shrink-0 justify-center text-sm text-slate-500" aria-hidden>
          {qaChev(open)}
        </span>
        <span className="truncate normal-case">{t("sidebarPathTools")}</span>
      </button>
      {open ? (
        <div className="border-t border-slate-200/70 px-2 pb-2.5 pt-1.5 dark:border-surface-mute/50">
          {TOOL_ENTRIES.map((entry) => {
            if (entry.kind === "tool") {
              const checked = agentToolEnabled[entry.id] !== false;
              return (
                <label key={entry.id} htmlFor={`pm-at-${entry.id}`} className={rowCls}>
                  <input
                    id={`pm-at-${entry.id}`}
                    type="checkbox"
                    className={cbCls}
                    checked={checked}
                    disabled={busy}
                    onChange={(e) => setAgentToolEnabled(entry.id, e.target.checked)}
                  />
                  <span className="min-w-0 flex-1 text-[13px] leading-snug text-slate-700 dark:text-slate-200">
                    {t(entry.labelKey as Parameters<typeof t>[0])}
                  </span>
                </label>
              );
            }
            // Group row — checked if all members enabled
            const allOn = entry.members.every((m) => agentToolEnabled[m] !== false);
            return (
              <label key={entry.id} htmlFor={`pm-at-${entry.id}`} className={rowCls}>
                <input
                  id={`pm-at-${entry.id}`}
                  type="checkbox"
                  className={cbCls}
                  checked={allOn}
                  disabled={busy}
                  onChange={(e) => setAgentToolGroupEnabled(entry.members, e.target.checked)}
                />
                <span className="min-w-0 flex-1 text-[13px] leading-snug text-slate-500 dark:text-slate-400">
                  {t(entry.labelKey as Parameters<typeof t>[0])}
                </span>
              </label>
            );
          })}
        </div>
      ) : null}
    </div>
  );
}

function mcpServerStatusLabel(s: McpCatalogServer): string {
  if (s.skipped) return t("mcpServerSkipped");
  if (!s.handshake_ok) return t("mcpServerHandshakeFail");
  if (!s.tools_list_ok) return t("mcpServerToolsFail");
  return t("mcpServerToolsOk");
}

function SidebarPresetsSection({ onPresetApplied }: { onPresetApplied?: () => void }) {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const busy = usePmChatStore((s) => s.busy);
  const quickActions = usePmChatStore((s) => s.quickActions);
  const designPresets = usePmChatStore((s) => s.designPresets);
  const resizePresets = usePmChatStore((s) => s.resizePresets);
  const applyQuickAction = usePmChatStore((s) => s.applyQuickAction);
  const applyResizePreset = usePmChatStore((s) => s.applyResizePreset);
  const openQaModal = usePmChatStore((s) => s.openQaModal);
  const openDesignPresetModal = usePmChatStore((s) => s.openDesignPresetModal);
  const openResizePresetModal = usePmChatStore((s) => s.openResizePresetModal);
  const [open, setOpen] = useState(() => {
    try {
      return localStorage.getItem("pm-chat-next.sidebar.presetsOpen") === "1";
    } catch {
      return true;
    }
  });

  const persistOpen = (next: boolean) => {
    setOpen(next);
    try {
      localStorage.setItem("pm-chat-next.sidebar.presetsOpen", next ? "1" : "0");
    } catch {
      /* */
    }
  };

  const designPresetGroups = useMemo(() => layoutDesignPresetGroups(designPresets), [designPresets]);
  const resizePresetGroups = useMemo(() => layoutResizePresetGroups(resizePresets), [resizePresets]);
  const applySidebarQuickAction = useCallback((action: (typeof quickActions)[number]) => {
    applyQuickAction(action);
    onPresetApplied?.();
  }, [applyQuickAction, onPresetApplied]);
  const applySidebarResizePreset = useCallback((preset: ResizePreset) => {
    applyResizePreset(preset);
    onPresetApplied?.();
  }, [applyResizePreset, onPresetApplied]);

  return (
    <div className="border-t border-slate-200 dark:border-surface-mute">
      <button
        type="button"
        className="flex w-full items-center gap-1.5 px-2 py-1.5 text-left text-[13px] font-semibold leading-snug tracking-wide text-slate-500 uppercase transition-colors hover:bg-slate-200/60 dark:text-slate-400 dark:hover:bg-surface-mute/50"
        aria-expanded={open}
        onClick={() => persistOpen(!open)}
      >
        <span className="flex w-5 flex-shrink-0 justify-center text-sm text-slate-500" aria-hidden>
          {open ? "\u25BC" : "\u25B6"}
        </span>
        <span className="truncate normal-case">{t("qaPresetsSection")}</span>
      </button>
      {open ? (
        <div className="space-y-3 border-t border-slate-200/70 px-2 pb-2.5 pt-2 dark:border-surface-mute/50">
          {/* Style Presets */}
          <div>
            <div className="mb-1.5 flex min-w-0 items-stretch gap-0.5">
              <div className="flex min-w-0 flex-1 items-center text-[11px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">
                {t("qaStylePresets")}
              </div>
              <button
                type="button"
                className="pm-icon-btn h-6 w-6 flex-shrink-0 text-slate-500 hover:text-slate-800 dark:hover:text-slate-100"
                title={t("qaEditTip")}
                onClick={() => openQaModal()}
              >
                <span className="sr-only">{t("qaEdit")}</span>
                <IconPencil />
              </button>
            </div>
            <div className="flex flex-wrap gap-1.5">
              {quickActions.length === 0 ? (
                <span className="text-xs text-slate-400">{t("qaNoPresets")}</span>
              ) : (
                quickActions.map((a) => (
                  <button
                    key={a.id}
                    type="button"
                    className="inline-flex max-w-full items-center gap-1 rounded border border-slate-300 bg-slate-100 px-2 py-1 text-left text-xs text-slate-800 transition-colors hover:bg-slate-200 disabled:pointer-events-none disabled:opacity-40 dark:border-slate-600 dark:bg-surface-dimmed dark:text-slate-100 dark:hover:bg-surface-mute"
                    title={a.prompt}
                    disabled={busy}
                    onClick={() => applySidebarQuickAction(a)}
                  >
                    <span className="flex-shrink-0 select-none" aria-hidden>{a.icon || ""}</span>
                    <span className="min-w-0 truncate">{a.name}</span>
                  </button>
                ))
              )}
            </div>
          </div>

          {/* Design Presets */}
          <div className="border-t border-slate-200/50 pt-2 dark:border-slate-600/40">
            <div className="mb-1.5 flex min-w-0 items-stretch gap-0.5">
              <div className="flex min-w-0 flex-1 items-center text-[11px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">
                {t("qaDesignPresets")}
              </div>
              <button
                type="button"
                className="pm-icon-btn h-6 w-6 flex-shrink-0 text-slate-500 hover:text-slate-800 dark:hover:text-slate-100"
                title={t("qaDesignEditTip")}
                onClick={() => openDesignPresetModal()}
              >
                <span className="sr-only">{t("qaEdit")}</span>
                <IconPencil />
              </button>
            </div>
            {designPresets.length === 0 ? (
              <span className="text-xs text-slate-400">{t("qaNoDesignPresets")}</span>
            ) : (
              <div className="space-y-2">
                {designPresetGroups.slice(0, 2).map(({ title, items }) => (
                  <div key={title || "__ungrouped__"}>
                    {title ? (
                      <div className="mb-1 text-[11px] font-medium text-slate-500 dark:text-slate-500">{title}</div>
                    ) : null}
                    <div className="flex flex-wrap gap-1.5">
                      {items.slice(0, 6).map((a) => (
                        <button
                          key={a.id}
                          type="button"
                          className="inline-flex max-w-full items-center gap-1 rounded border border-slate-300 bg-slate-100 px-2 py-1 text-left text-xs text-slate-800 transition-colors hover:bg-slate-200 disabled:pointer-events-none disabled:opacity-40 dark:border-slate-600 dark:bg-surface-dimmed dark:text-slate-100 dark:hover:bg-surface-mute"
                          title={a.prompt}
                          disabled={busy}
                          onClick={() => applySidebarQuickAction(a)}
                        >
                          <span className="flex-shrink-0 select-none" aria-hidden>{a.icon || ""}</span>
                          <span className="min-w-0 truncate">{a.name}</span>
                        </button>
                      ))}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>

          {/* Resize Presets */}
          <div className="border-t border-slate-200/50 pt-2 dark:border-slate-600/40">
            <div className="mb-1.5 flex min-w-0 items-stretch gap-0.5">
              <div className="flex min-w-0 flex-1 items-center text-[11px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">
                {t("qaResizePresets")}
              </div>
              <button
                type="button"
                className="pm-icon-btn h-6 w-6 flex-shrink-0 text-slate-500 hover:text-slate-800 dark:hover:text-slate-100"
                title={t("qaResizeEditTip")}
                onClick={() => openResizePresetModal()}
              >
                <span className="sr-only">{t("qaEdit")}</span>
                <IconPencil />
              </button>
            </div>
            {resizePresets.length === 0 ? (
              <span className="text-xs text-slate-400">{t("qaNoResizePresets")}</span>
            ) : (
              <div className="flex flex-wrap gap-1.5">
                {resizePresets.slice(0, 8).map((a) => (
                  <button
                    key={a.id}
                    type="button"
                    className="inline-flex max-w-full items-center gap-1 rounded border border-slate-300 bg-slate-100 px-2 py-1 text-left text-xs text-slate-800 transition-colors hover:bg-slate-200 disabled:pointer-events-none disabled:opacity-40 dark:border-slate-600 dark:bg-surface-dimmed dark:text-slate-100 dark:hover:bg-surface-mute"
                    title={expandedResizePresetPrompt(a)}
                    disabled={busy}
                    onClick={() => applySidebarResizePreset(a)}
                  >
                    <span className="flex-shrink-0 select-none" aria-hidden>{a.icon || ""}</span>
                    <span className="min-w-0 truncate">{a.name}</span>
                  </button>
                ))}
              </div>
            )}
          </div>
        </div>
      ) : null}
    </div>
  );
}

function SidebarProvidersSection() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const busy = usePmChatStore((s) => s.busy);
  const providerUiBooting = usePmChatStore((s) => s.providerUiBooting);

  // LLM
  const routerQuick = usePmChatStore((s) => s.routerQuick);
  const modelQuick = usePmChatStore((s) => s.modelQuick);
  const savedChatModel = usePmChatStore((s) => s.savedChatModel);
  const savedChatRouter = usePmChatStore((s) => s.savedChatRouter);
  const setRouterQuick = usePmChatStore((s) => s.setRouterQuick);
  const setModelQuick = usePmChatStore((s) => s.setModelQuick);

  // Image
  const imageProvider = usePmChatStore((s) => s.imageProvider);
  const imageModel = usePmChatStore((s) => s.imageModel);
  const replicateCollection = usePmChatStore((s) => s.replicateCollection);
  const imageProviderRows = usePmChatStore((s) => s.imageProviderRows);
  const imageModelRows = usePmChatStore((s) => s.imageModelRows);
  const replicateCollections = usePmChatStore((s) => s.replicateCollections);
  const imageOpenApiFlat = usePmChatStore((s) => s.imageOpenApiFlat);
  const imageOpenApiFlatLoading = usePmChatStore((s) => s.imageOpenApiFlatLoading);
  const imageOpenApiFlatError = usePmChatStore((s) => s.imageOpenApiFlatError);
  const imageOpenApiFieldValues = usePmChatStore((s) => s.imageOpenApiFieldValues);
  const setImageProvider = usePmChatStore((s) => s.setImageProvider);
  const setImageModel = usePmChatStore((s) => s.setImageModel);
  const setReplicateCollection = usePmChatStore((s) => s.setReplicateCollection);
  const refreshReplicateCollectionsAndModels = usePmChatStore((s) => s.refreshReplicateCollectionsAndModels);
  const setImageOpenApiFieldValue = usePmChatStore((s) => s.setImageOpenApiFieldValue);

  // Video
  const videoProvider = usePmChatStore((s) => s.videoProvider);
  const videoModel = usePmChatStore((s) => s.videoModel);
  const videoReplicateCollection = usePmChatStore((s) => s.videoReplicateCollection);
  const videoModelRows = usePmChatStore((s) => s.videoModelRows);
  const videoOpenApiFlat = usePmChatStore((s) => s.videoOpenApiFlat);
  const videoOpenApiFlatLoading = usePmChatStore((s) => s.videoOpenApiFlatLoading);
  const videoOpenApiFlatError = usePmChatStore((s) => s.videoOpenApiFlatError);
  const videoOpenApiFieldValues = usePmChatStore((s) => s.videoOpenApiFieldValues);
  const setVideoProvider = usePmChatStore((s) => s.setVideoProvider);
  const setVideoModel = usePmChatStore((s) => s.setVideoModel);
  const setVideoReplicateCollection = usePmChatStore((s) => s.setVideoReplicateCollection);
  const refreshVideoReplicateCollectionsAndModels = usePmChatStore((s) => s.refreshVideoReplicateCollectionsAndModels);
  const setVideoOpenApiFieldValue = usePmChatStore((s) => s.setVideoOpenApiFieldValue);

  // Voice / Audio
  const sttProvider = usePmChatStore((s) => s.sttProvider);
  const sttModel = usePmChatStore((s) => s.sttModel);
  const ttsProvider = usePmChatStore((s) => s.ttsProvider);
  const ttsModel = usePmChatStore((s) => s.ttsModel);
  const ttsVoiceId = usePmChatStore((s) => s.ttsVoiceId);
  const setSttProvider = usePmChatStore((s) => s.setSttProvider);
  const setSttModel = usePmChatStore((s) => s.setSttModel);
  const setTtsProvider = usePmChatStore((s) => s.setTtsProvider);
  const setTtsModel = usePmChatStore((s) => s.setTtsModel);
  const setTtsVoiceId = usePmChatStore((s) => s.setTtsVoiceId);

  // Image Recognition
  const imageRecogProvider = usePmChatStore((s) => s.imageRecogProvider);
  const imageRecogModel = usePmChatStore((s) => s.imageRecogModel);
  const setImageRecogProvider = usePmChatStore((s) => s.setImageRecogProvider);
  const setImageRecogModel = usePmChatStore((s) => s.setImageRecogModel);

  // ── Outer collapse ──────────────────────────────────────────────────────
  const [open, setOpen] = useState(() => {
    try { return localStorage.getItem("pm-chat-next.sidebar.providersOpen") !== "0"; }
    catch { return false; }
  });
  const persistOpen = (next: boolean) => {
    setOpen(next);
    try { localStorage.setItem("pm-chat-next.sidebar.providersOpen", next ? "1" : "0"); } catch { /* */ }
  };

  // ── Sub-section collapse ────────────────────────────────────────────────
  function readSub(key: string, def = true): boolean {
    try { return localStorage.getItem(key) !== "0"; } catch { return def; }
  }
  function writeSub(key: string, v: boolean) {
    try { localStorage.setItem(key, v ? "1" : "0"); } catch { /* */ }
  }
  const [llmOpen,   setLlmOpen]   = useState(() => readSub("pm-chat-next.sidebar.sub.llm"));
  const [imgOpen,   setImgOpen]   = useState(() => readSub("pm-chat-next.sidebar.sub.image"));
  const [recogOpen, setRecogOpen] = useState(() => readSub("pm-chat-next.sidebar.sub.recog"));
  const [vidOpen,   setVidOpen]   = useState(() => readSub("pm-chat-next.sidebar.sub.video"));
  const [voiceOpen, setVoiceOpen] = useState(() => readSub("pm-chat-next.sidebar.sub.voice"));

  // LLM catalog
  const llmModelRows = usePmChatStore((s) => s.llmModelRows);
  const llmModelRowsLoading = usePmChatStore((s) => s.llmModelRowsLoading);
  const llmModelRowsRouter = usePmChatStore((s) => s.llmModelRowsRouter);
  const fetchLlmModels = usePmChatStore((s) => s.fetchLlmModels);

  // ── Derived ─────────────────────────────────────────────────────────────
  const effectiveRouterId = routerQuick || savedChatRouter || "openrouter";
  const routerHasLiveCatalog = effectiveRouterId === "openrouter" || effectiveRouterId === "openai";

  const modelOptions = useMemo(() => {
    // Use live catalog when available for the effective router.
    if (routerHasLiveCatalog && llmModelRowsRouter === effectiveRouterId && llmModelRows.length > 0) {
      const ids = new Set(llmModelRows.map((r) => r.id));
      if (savedChatModel && !routerQuick) ids.add(savedChatModel);
      return [...ids].sort(sortAlpha);
    }
    // Fallback to hardcoded list.
    const list = modelListForRouterId(effectiveRouterId);
    const extra = new Set(list);
    if (savedChatModel && !routerQuick) extra.add(savedChatModel);
    return [...extra].sort(sortAlpha);
  }, [routerHasLiveCatalog, llmModelRows, llmModelRowsRouter, effectiveRouterId, routerQuick, savedChatModel]);

  const sortedProviders = useMemo(() => {
    if (!imageProviderRows?.length) return [];
    return [...imageProviderRows].sort((a, b) => sortAlpha(a.label || a.id || a.name, b.label || b.id || b.name));
  }, [imageProviderRows]);

  const sortedImageModels = useMemo(() => {
    const raw = imageModelRows || [];
    const want = imageModel || "";
    const rows = want && !raw.some((m) => m.id === want) ? [...raw, { id: want, label: want }] : raw;
    return sortImageModelRowList(rows);
  }, [imageModelRows, imageModel]);

  const sortedVideoModels = useMemo(() => {
    const raw = videoModelRows || [];
    const want = videoModel || "";
    const rows = want && !raw.some((m) => m.id === want) ? [...raw, { id: want, label: want }] : raw;
    return sortImageModelRowList(rows);
  }, [videoModelRows, videoModel]);

  const sortedRepCols = useMemo(() => sortReplicateCollections(replicateCollections || []), [replicateCollections]);
  const isRep = (imageProvider || "") === "replicate";
  const isRepVideo = (videoProvider || "") === "replicate";

  const displayImageModels = useMemo(() => {
    if (imageProvider === "pixlwiz")
      return sortedImageModels.filter((m) => PIXLWIZ_IMAGE_MODELS.includes(m.id));
    return sortedImageModels;
  }, [imageProvider, sortedImageModels]);

  const displayVideoModels = useMemo(() => {
    if (videoProvider === "pixlwiz")
      return sortedVideoModels.filter((m) => PIXLWIZ_VIDEO_MODELS.includes(m.id));
    return sortedVideoModels;
  }, [videoProvider, sortedVideoModels]);

  const imageCollectionDesc = useMemo(() => {
    if (!isRep || !replicateCollection) return "";
    return sortedRepCols.find((c) => c.slug === replicateCollection)?.description ?? "";
  }, [isRep, replicateCollection, sortedRepCols]);

  const videoCollectionDesc = useMemo(() => {
    if (!isRepVideo || !videoReplicateCollection) return "";
    return sortedRepCols.find((c) => c.slug === videoReplicateCollection)?.description ?? "";
  }, [isRepVideo, videoReplicateCollection, sortedRepCols]);

  const isRepRecog = (imageRecogProvider || "") === "replicate";
  const displayImageRecogModels = useMemo(() => {
    if (!imageRecogProvider || imageRecogProvider === "pixlwiz")
      return sortedImageModels.filter((m) => PIXLWIZ_IMAGE_RECOG_MODELS.includes(m.id));
    if (imageRecogProvider === "replicate")
      return sortedImageModels;
    return sortedImageModels;
  }, [imageRecogProvider, sortedImageModels]);

  // ── Helpers ──────────────────────────────────────────────────────────────
  const sc = "w-full rounded border border-slate-300 bg-white px-2 py-1 text-xs text-slate-800 focus:border-violet-500 focus:outline-none dark:border-slate-600 dark:bg-surface-dimmed dark:text-slate-100";

  function SubHeader({ label, open: o, onToggle, hint }: { label: string; open: boolean; onToggle: () => void; hint?: string }) {
    return (
      <button type="button" className="flex w-full items-center gap-1 py-1 text-left" onClick={onToggle} title={hint}>
        <span className="flex w-4 shrink-0 justify-center text-[9px] text-slate-400" aria-hidden>{o ? "\u25BC" : "\u25B6"}</span>
        <span className="text-[11px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">{label}</span>
      </button>
    );
  }

  return (
    <div className="border-t border-slate-200 dark:border-surface-mute">
      <button
        type="button"
        className="flex w-full items-center gap-1.5 px-2 py-1.5 text-left text-[13px] font-semibold leading-snug tracking-wide text-slate-500 uppercase transition-colors hover:bg-slate-200/60 dark:text-slate-400 dark:hover:bg-surface-mute/50"
        aria-expanded={open}
        onClick={() => persistOpen(!open)}
      >
        <span className="flex w-5 flex-shrink-0 justify-center text-sm text-slate-500" aria-hidden>
          {open ? "\u25BC" : "\u25B6"}
        </span>
        <span className="truncate normal-case">{t("qaProviderModelsSection")}</span>
      </button>
      {open ? (
        <div className="space-y-0 border-t border-slate-200/70 px-2 pb-2.5 pt-1 dark:border-surface-mute/50">

          {/* ── Text / LLM ───────────────────────────────────────────────── */}
          <div>
            <SubHeader label={t("qaLlmSection")} open={llmOpen} onToggle={() => { const n = !llmOpen; setLlmOpen(n); writeSub("pm-chat-next.sidebar.sub.llm", n); }} />
            {llmOpen ? (
              <div className="space-y-1 pb-2">
                <select className={sc} disabled={busy} value={routerQuick} onChange={(e) => setRouterQuick(e.target.value)}>
                  <option value="">{t("optChatSaved")}</option>
                  {CHAT_ROUTERS.map((r) => (<option key={r.id} value={r.id}>{r.label}</option>))}
                </select>
                <div className="flex gap-1">
                  <ModelStringTypeahead
                    className="pm-typeahead min-w-0 flex-1"
                    inputClassName={sc}
                    disabled={busy}
                    value={modelQuick}
                    onChange={setModelQuick}
                    options={modelOptions}
                    placeholder={routerQuick ? t("optChatRouterDefault") : t("optChatSaved")}
                  />
                  {routerHasLiveCatalog ? (
                    <button
                      type="button"
                      className="pm-icon-btn h-7 w-7 flex-shrink-0"
                      disabled={busy || llmModelRowsLoading}
                      title="Refresh model list"
                      onClick={() => void fetchLlmModels(effectiveRouterId, true)}
                    >
                      <IconRefresh />
                    </button>
                  ) : null}
                </div>
                {routerHasLiveCatalog && llmModelRowsLoading ? (
                  <p className="text-[11px] italic text-slate-400">{t("optImageLoading")}</p>
                ) : null}
              </div>
            ) : null}
          </div>

          {/* ── Image ────────────────────────────────────────────────────── */}
          <div className="border-t border-slate-200/40 dark:border-slate-600/30">
            <SubHeader label={t("qaImageSection")} open={imgOpen} onToggle={() => { const n = !imgOpen; setImgOpen(n); writeSub("pm-chat-next.sidebar.sub.image", n); }} />
            {imgOpen ? (
              <div className="space-y-1 pb-2">
                <select className={sc} disabled={busy} value={imageProvider} onChange={(e) => void setImageProvider(e.target.value)}>
                  {sortedProviders.length === 0 ? (<option>{t("optImageLoading")}</option>) : sortedProviders.map((r) => (<option key={r.id || r.name} value={r.id || r.name}>{r.label || r.id || r.name}</option>))}
                </select>
                {isRep ? (
                  <div className="flex gap-1">
                    <select className={`${sc} flex-1`} disabled={busy} value={replicateCollection} onChange={(e) => void setReplicateCollection(e.target.value)}>
                      {sortedRepCols.length === 0 ? (<option>{replicateCollection || "official"}</option>) : sortedRepCols.map((c) => (<option key={c.slug} value={c.slug}>{c.name || c.slug}</option>))}
                    </select>
                    <button type="button" className="pm-icon-btn h-7 w-7" disabled={busy || providerUiBooting} onClick={() => void refreshReplicateCollectionsAndModels()} title={t("btnRefreshReplicateTip")}>
                      <IconRefresh />
                    </button>
                  </div>
                ) : null}
                {isRep && imageCollectionDesc ? (
                  <p className="text-[11px] italic leading-snug text-slate-500 dark:text-slate-400">{imageCollectionDesc}</p>
                ) : null}
                <ModelStringTypeahead
                  className="pm-typeahead"
                  inputClassName={sc}
                  disabled={busy}
                  value={imageModel}
                  onChange={(value) => void setImageModel(value)}
                  options={displayImageModels}
                  placeholder={t("optImageNoModels")}
                />
                {isRep && imageModel ? (
                  <a href={`https://replicate.com/${imageModel}`} target="_blank" rel="noopener noreferrer"
                    className="inline-flex items-center gap-0.5 text-[11px] text-violet-600 hover:underline dark:text-violet-400">
                    replicate.com/{imageModel} ↗
                  </a>
                ) : null}
                {isRep ? (
                  <VideoOpenApiToolbarFields variant="image" loading={imageOpenApiFlatLoading} error={imageOpenApiFlatError} flat={imageOpenApiFlat} values={imageOpenApiFieldValues} onChange={setImageOpenApiFieldValue} />
                ) : null}
              </div>
            ) : null}
          </div>

          {/* ── Image Recognition ────────────────────────────────────────── */}
          <div className="border-t border-slate-200/40 dark:border-slate-600/30">
            <SubHeader label={t("qaImageRecogSection")} open={recogOpen} onToggle={() => { const n = !recogOpen; setRecogOpen(n); writeSub("pm-chat-next.sidebar.sub.recog", n); }} hint={t("qaImageRecogSectionTip")} />
            {recogOpen ? (
              <div className="space-y-1 pb-2">
                <select className={sc} disabled={busy} value={imageRecogProvider} onChange={(e) => void setImageRecogProvider(e.target.value)}>
                  <option value="">{t("optChatSaved")}</option>
                  {sortedProviders.map((r) => (<option key={`ir-${r.id || r.name}`} value={r.id || r.name}>{r.label || r.id || r.name}</option>))}
                </select>
                {isRepRecog ? (
                  <div className="flex gap-1">
                    <select className={`${sc} flex-1`} disabled={busy} value={replicateCollection} onChange={(e) => void setReplicateCollection(e.target.value)}>
                      {sortedRepCols.length === 0 ? (<option>{replicateCollection || "official"}</option>) : sortedRepCols.map((c) => (<option key={`irc-${c.slug}`} value={c.slug}>{c.name || c.slug}</option>))}
                    </select>
                    <button type="button" className="pm-icon-btn h-7 w-7" disabled={busy || providerUiBooting} onClick={() => void refreshReplicateCollectionsAndModels()} title={t("btnRefreshReplicateTip")}>
                      <IconRefresh />
                    </button>
                  </div>
                ) : null}
                <ModelStringTypeahead
                  className="pm-typeahead"
                  inputClassName={sc}
                  disabled={busy}
                  value={imageRecogModel}
                  onChange={(value) => void setImageRecogModel(value)}
                  options={displayImageRecogModels}
                  placeholder={t("optChatSaved")}
                />
                {isRepRecog && imageRecogModel ? (
                  <a href={`https://replicate.com/${imageRecogModel}`} target="_blank" rel="noopener noreferrer"
                    className="inline-flex items-center gap-0.5 text-[11px] text-violet-600 hover:underline dark:text-violet-400">
                    replicate.com/{imageRecogModel} ↗
                  </a>
                ) : null}
              </div>
            ) : null}
          </div>

          {/* ── Video ────────────────────────────────────────────────────── */}
          <div className="border-t border-slate-200/40 dark:border-slate-600/30">
            <SubHeader label={t("qaVideoSection")} open={vidOpen} onToggle={() => { const n = !vidOpen; setVidOpen(n); writeSub("pm-chat-next.sidebar.sub.video", n); }} />
            {vidOpen ? (
              <div className="space-y-1 pb-2">
                <select className={sc} disabled={busy} value={videoProvider} onChange={(e) => void setVideoProvider(e.target.value)}>
                  {sortedProviders.length === 0 ? (<option>{t("optImageLoading")}</option>) : sortedProviders.map((r) => (<option key={`v-${r.id || r.name}`} value={r.id || r.name}>{r.label || r.id || r.name}</option>))}
                </select>
                {isRepVideo ? (
                  <div className="flex gap-1">
                    <select className={`${sc} flex-1`} disabled={busy} value={videoReplicateCollection} onChange={(e) => void setVideoReplicateCollection(e.target.value)}>
                      {sortedRepCols.length === 0 ? (<option>{videoReplicateCollection || "official"}</option>) : sortedRepCols.map((c) => (<option key={`vrc-${c.slug}`} value={c.slug}>{c.name || c.slug}</option>))}
                    </select>
                    <button type="button" className="pm-icon-btn h-7 w-7" disabled={busy || providerUiBooting} onClick={() => void refreshVideoReplicateCollectionsAndModels()} title={t("btnRefreshReplicateTip")}>
                      <IconRefresh />
                    </button>
                  </div>
                ) : null}
                {isRepVideo && videoCollectionDesc ? (
                  <p className="text-[11px] italic leading-snug text-slate-500 dark:text-slate-400">{videoCollectionDesc}</p>
                ) : null}
                <ModelStringTypeahead
                  className="pm-typeahead"
                  inputClassName={sc}
                  disabled={busy}
                  value={videoModel}
                  onChange={(value) => void setVideoModel(value)}
                  options={displayVideoModels}
                  placeholder={t("optImageNoModels")}
                />
                {isRepVideo && videoModel ? (
                  <a href={`https://replicate.com/${videoModel}`} target="_blank" rel="noopener noreferrer"
                    className="inline-flex items-center gap-0.5 text-[11px] text-violet-600 hover:underline dark:text-violet-400">
                    replicate.com/{videoModel} ↗
                  </a>
                ) : null}
                {isRepVideo ? (
                  <VideoOpenApiToolbarFields variant="video" loading={videoOpenApiFlatLoading} error={videoOpenApiFlatError} flat={videoOpenApiFlat} values={videoOpenApiFieldValues} onChange={setVideoOpenApiFieldValue} />
                ) : null}
              </div>
            ) : null}
          </div>

          {/* ── Voice & Audio ────────────────────────────────────────────── */}
          <div className="border-t border-slate-200/40 dark:border-slate-600/30" title={t("qaVoiceSectionTip")}>
            <SubHeader label={t("qaVoiceSection")} open={voiceOpen} onToggle={() => { const n = !voiceOpen; setVoiceOpen(n); writeSub("pm-chat-next.sidebar.sub.voice", n); }} />
            {voiceOpen ? (
              <div className="space-y-1 pb-2">
                <div className="text-[9px] font-medium uppercase tracking-wider text-slate-400 dark:text-slate-500">{t("qaVoiceInput")}</div>
                <select className={sc} disabled={busy} value={sttProvider} onChange={(e) => void setSttProvider(e.target.value)}>
                  <option value="">{t("optVoiceSaved")}</option>
                  {PIXLWIZ_AUDIO_PROVIDERS.map((p) => (<option key={`stt-p-${p}`} value={p}>{AUDIO_PROVIDER_LABELS[p] ?? p}</option>))}
                </select>
                {(!sttProvider || sttProvider === "pixlwiz") ? (
                  <select className={sc} disabled={busy} value={sttModel} onChange={(e) => void setSttModel(e.target.value)}>
                    <option value="">{t("optVoiceDefault")}</option>
                    {PIXLWIZ_STT_MODELS.map((m) => (<option key={`stt-m-${m}`} value={m}>{PIXLWIZ_STT_MODEL_LABELS[m] ?? m}</option>))}
                  </select>
                ) : sttProvider === "elevenlabs" ? (
                  <select className={sc} disabled={busy} value={sttModel} onChange={(e) => void setSttModel(e.target.value)}>
                    <option value="">{t("optVoiceDefault")}</option>
                    {ELEVENLABS_STT_MODELS.map((m) => (<option key={`stt-el-${m.id}`} value={m.id}>{m.label}</option>))}
                  </select>
                ) : null}
                <div className="pt-0.5 text-[9px] font-medium uppercase tracking-wider text-slate-400 dark:text-slate-500">{t("qaVoiceOutput")}</div>
                <select className={sc} disabled={busy} value={ttsProvider} onChange={(e) => void setTtsProvider(e.target.value)}>
                  <option value="">{t("optVoiceSaved")}</option>
                  {PIXLWIZ_AUDIO_PROVIDERS.map((p) => (<option key={`tts-p-${p}`} value={p}>{AUDIO_PROVIDER_LABELS[p] ?? p}</option>))}
                </select>
                {(!ttsProvider || ttsProvider === "pixlwiz") ? (
                  <select className={sc} disabled={busy} value={ttsModel} onChange={(e) => void setTtsModel(e.target.value)}>
                    <option value="">{t("optVoiceDefault")}</option>
                    {PIXLWIZ_TTS_MODELS.map((m) => (<option key={`tts-m-${m}`} value={m}>{PIXLWIZ_TTS_MODEL_LABELS[m] ?? m}</option>))}
                  </select>
                ) : ttsProvider === "elevenlabs" ? (
                  <>
                    <select className={sc} disabled={busy} value={ttsModel} onChange={(e) => void setTtsModel(e.target.value)}>
                      <option value="">{t("optVoiceDefault")}</option>
                      {ELEVENLABS_TTS_MODELS.map((m) => (<option key={`tts-el-${m.id}`} value={m.id}>{m.label}</option>))}
                    </select>
                    <input className={sc} list="el-tts-voices" disabled={busy} value={ttsVoiceId}
                      placeholder={t("optVoiceIdPlaceholder")} onChange={(e) => void setTtsVoiceId(e.target.value)} />
                    <datalist id="el-tts-voices">
                      {ELEVENLABS_TTS_VOICES.map((v) => (<option key={v.id} value={v.id}>{v.label}</option>))}
                    </datalist>
                  </>
                ) : null}
              </div>
            ) : null}
          </div>

        </div>
      ) : null}
    </div>
  );
}

function SidebarMcpCatalogSection() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const busy = usePmChatStore((s) => s.busy);
  const mcpToolsEnabled = usePmChatStore((s) => s.mcpToolsEnabled);
  const setMcpToolsEnabled = usePmChatStore((s) => s.setMcpToolsEnabled);
  const disabledMcpServers = usePmChatStore((s) => s.disabledMcpServers);
  const setDisabledMcpServers = usePmChatStore((s) => s.setDisabledMcpServers);
  const mcpCatalog = usePmChatStore((s) => s.mcpCatalog);

  const toggleServer = (name: string, enabled: boolean) => {
    if (enabled) {
      setDisabledMcpServers(disabledMcpServers.filter((s) => s !== name));
    } else {
      if (!disabledMcpServers.includes(name))
        setDisabledMcpServers([...disabledMcpServers, name]);
    }
  };
  const [open, setOpen] = useState(() => {
    try {
      return localStorage.getItem("pm-chat-next.sidebar.mcpOpen") === "1";
    } catch {
      return false;
    }
  });

  const persistOpen = (next: boolean) => {
    setOpen(next);
    try {
      localStorage.setItem("pm-chat-next.sidebar.mcpOpen", next ? "1" : "0");
    } catch {
      /* */
    }
  };

  const servers = Array.isArray(mcpCatalog?.servers) ? mcpCatalog!.servers! : [];

  return (
    <div className="border-t border-slate-200 dark:border-surface-mute" title={t("sidebarMcpHint")}>
      <button
        type="button"
        className="flex w-full items-center gap-1.5 px-2 py-1.5 text-left text-[13px] font-semibold leading-snug tracking-wide text-slate-500 uppercase transition-colors hover:bg-slate-200/60 dark:text-slate-400 dark:hover:bg-surface-mute/50"
        aria-expanded={open}
        onClick={() => persistOpen(!open)}
      >
        <span className="flex w-5 flex-shrink-0 justify-center text-sm text-slate-500" aria-hidden>
          {qaChev(open)}
        </span>
        <span className="truncate normal-case">{t("sidebarMcp")}</span>
      </button>
      {open ? (
        <div className="space-y-2 border-t border-slate-200/70 px-2 pb-2.5 pt-2 dark:border-surface-mute/50">
          <label
            htmlFor="pm-mcp-master"
            className={`flex cursor-pointer items-start gap-2 rounded-md px-1 py-0.5 hover:bg-slate-200/70 dark:hover:bg-surface-mute/60 ${busy ? "pointer-events-none opacity-50" : ""}`}
          >
            <input
              id="pm-mcp-master"
              type="checkbox"
              className="mt-1 h-4 w-4 shrink-0 rounded border-slate-400 text-violet-600 focus:ring-violet-500 dark:border-slate-500 dark:bg-surface-dark"
              checked={mcpToolsEnabled}
              disabled={busy}
              onChange={(e) => setMcpToolsEnabled(e.target.checked)}
            />
            <span className="min-w-0 flex-1 text-[13px] leading-snug text-slate-700 dark:text-slate-200">
              {t("mcpToolsMasterLabel")}
            </span>
          </label>

          {!mcpCatalog ? (
            <p className="px-0.5 text-xs leading-relaxed text-slate-500 dark:text-slate-400">{t("mcpCatalogLoading")}</p>
          ) : servers.length === 0 ? (
            <p className="px-0.5 text-xs leading-relaxed text-slate-500 dark:text-slate-400">{t("mcpCatalogEmpty")}</p>
          ) : (
            <ul className="max-h-48 space-y-2 overflow-y-auto pm-scroll">
              {servers.map((srv, idx) => {
                const name = String(srv.name || `server_${idx}`);
                const tools = Array.isArray(srv.tools) ? srv.tools : [];
                const showTools = tools.slice(0, 24);
                const more = tools.length - showTools.length;
                const serverEnabled = !disabledMcpServers.includes(name);
                const hasTools = srv.handshake_ok && srv.tools_list_ok;
                return (
                  <li key={`${name}-${idx}`} className="rounded-md border border-slate-200/80 bg-white/60 px-1.5 py-1.5 dark:border-surface-mute dark:bg-surface-dark/40">
                    <label
                      className={`flex items-start gap-2 ${busy || !mcpToolsEnabled ? "pointer-events-none opacity-60" : "cursor-pointer"}`}
                    >
                      <input
                        type="checkbox"
                        className="mt-0.5 h-3.5 w-3.5 shrink-0 rounded border-slate-400 text-violet-600 focus:ring-violet-500 dark:border-slate-500 dark:bg-surface-dark"
                        checked={serverEnabled}
                        disabled={busy || !mcpToolsEnabled || !hasTools}
                        onChange={(e) => toggleServer(name, e.target.checked)}
                      />
                      <div className="min-w-0 flex-1">
                        <div className="flex flex-wrap items-baseline gap-x-1.5 gap-y-0.5">
                          <span className={`text-xs font-semibold ${serverEnabled && mcpToolsEnabled ? "text-slate-800 dark:text-slate-100" : "text-slate-400 dark:text-slate-500"}`}>{name}</span>
                          {srv.transport ? (
                            <span className="text-[11px] uppercase tracking-wide text-slate-400 dark:text-slate-500">
                              {srv.transport}
                            </span>
                          ) : null}
                          <span className="text-[11px] text-slate-500 dark:text-slate-400">{mcpServerStatusLabel(srv)}</span>
                        </div>
                        {srv.skip_reason ? (
                          <p className="mt-0.5 text-[11px] leading-snug text-amber-700 dark:text-amber-300/90">{srv.skip_reason}</p>
                        ) : null}
                        {showTools.length > 0 ? (
                          <ul className="mt-1 list-inside list-disc text-[11px] leading-snug text-slate-600 dark:text-slate-300">
                            {showTools.map((tool, ti) => (
                              <li key={`${name}-t-${ti}`} className="truncate">
                                {String(tool.name || "(unnamed)")}
                              </li>
                            ))}
                            {more > 0 ? <li className="list-none text-slate-400">+{more} more</li> : null}
                          </ul>
                        ) : null}
                      </div>
                    </label>
                  </li>
                );
              })}
            </ul>
          )}
        </div>
      ) : null}
    </div>
  );
}

const SIDEBAR_WIDTH_LS = "pm-chat-next.sidebar.width";
const DEFAULT_SIDEBAR_WIDTH = 280; // px
const MIN_SIDEBAR_WIDTH = 200;
const MAX_SIDEBAR_WIDTH = 480;

function readSidebarWidth(): number {
  try {
    const raw = localStorage.getItem(SIDEBAR_WIDTH_LS);
    if (!raw) return DEFAULT_SIDEBAR_WIDTH;
    const n = Number.parseInt(raw, 10);
    if (!Number.isFinite(n)) return DEFAULT_SIDEBAR_WIDTH;
    return Math.min(MAX_SIDEBAR_WIDTH, Math.max(MIN_SIDEBAR_WIDTH, n));
  } catch {
    return DEFAULT_SIDEBAR_WIDTH;
  }
}

export type PastSessionsSidebarProps = {
  currentSessionId: string;
  variant?: "permanent" | "overlay";
  onPresetApplied?: () => void;
};

export function PastSessionsSidebar({ currentSessionId, variant = "permanent", onPresetApplied }: PastSessionsSidebarProps) {
  const navigate = useNavigate();
  const togglePastSessions = usePmChatStore((s) => s.togglePastSessions);
  const [rows, setRows] = useState<ChatSessionMeta[]>([]);
  const [width, setWidth] = useState(readSidebarWidth);
  const isOverlay = variant === "overlay";

  const refresh = useCallback(() => {
    void listChatSessionsMeta().then(setRows);
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  useEffect(() => {
    return subscribeChatSessionsChanged(refresh);
  }, [refresh]);

  const onDelete = (e: React.MouseEvent, id: string) => {
    e.preventDefault();
    e.stopPropagation();
    void (async () => {
      await deleteChatSession(id);
      if (id === currentSessionId) {
        void navigate({ to: "/chat/$sessionId", params: { sessionId: crypto.randomUUID() } });
      }
      refresh();
    })();
  };

  const onResizePointerDown = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const startX = e.clientX;
    const startW = width;
    let lastW = startW;
    const move = (ev: PointerEvent) => {
      const delta = ev.clientX - startX;
      lastW = Math.min(MAX_SIDEBAR_WIDTH, Math.max(MIN_SIDEBAR_WIDTH, startW + delta));
      setWidth(lastW);
    };
    const end = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
      try {
        localStorage.setItem(SIDEBAR_WIDTH_LS, String(lastW));
      } catch {
        /* */
      }
      document.body.style.removeProperty("cursor");
      document.body.style.removeProperty("user-select");
    };
    document.body.style.cursor = "ew-resize";
    document.body.style.userSelect = "none";
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
  }, [width]);

  return (
    <aside
      className={`relative flex min-h-0 shrink-0 flex-col self-stretch border-r border-slate-200 bg-slate-50/95 dark:border-surface-mute dark:bg-surface-dark/98 ${
        isOverlay ? "h-full w-full rounded-r-2xl p-4 shadow-2xl shadow-slate-950/20 backdrop-blur-xl dark:shadow-black/45" : ""
      }`}
      style={{ width: isOverlay ? "100%" : width }}
      aria-label={t("pastSessionsAria")}
    >
      {/* Resize handle */}
      {!isOverlay ? (
        <div
          role="separator"
          aria-orientation="vertical"
          aria-label="Resize sidebar"
          className="group absolute top-0 right-0 z-10 flex h-full w-3 cursor-ew-resize touch-none items-center justify-end"
          onPointerDown={onResizePointerDown}
        >
          <span className="mr-0.5 h-8 w-0.5 rounded-full bg-slate-300 transition-colors group-hover:bg-violet-400 dark:bg-slate-600 dark:group-hover:bg-violet-500" />
        </div>
      ) : null}
      <div className="flex shrink-0 items-center justify-end border-b border-slate-200 px-1 py-1 dark:border-surface-mute">
        <button
          type="button"
          className="pm-icon-btn"
          title={isOverlay ? t("btnPastSessionsShow") : t("pastSessionsCollapseTip")}
          aria-label={isOverlay ? t("btnPastSessionsShow") : t("pastSessionsCollapseTip")}
          onClick={() => togglePastSessions()}
        >
          {isOverlay ? <IconPastSessions /> : <IconChevLeft />}
        </button>
      </div>
      {/* New button - fixed at top */}
      <div className="shrink-0">
        <button
          type="button"
          className="block w-full truncate rounded-md border border-blue-400/45 bg-blue-50 px-1.5 py-1.5 text-left text-[13px] font-medium leading-snug text-blue-900 transition-colors hover:bg-blue-100 dark:border-blue-500/35 dark:bg-blue-950/50 dark:text-blue-100 dark:hover:bg-blue-900/60"
          title={t("pastSessionsNewTip")}
          onClick={() => void navigate({ to: "/chat/$sessionId", params: { sessionId: crypto.randomUUID() } })}
        >
          {t("pastSessionsNew")}
        </button>
      </div>
      {/* Sessions list - scrollable, takes all available space */}
      <nav className="pm-scroll flex min-h-0 flex-1 flex-col gap-0.5 overflow-y-auto p-1 pt-0">
        {rows.length === 0 ? (
          <p className="px-1 py-1 text-center text-[13px] leading-snug text-slate-500 dark:text-slate-500">{t("pastSessionsEmpty")}</p>
        ) : (
          rows.map((s) => {
            const active = s.id === currentSessionId;
            return (
              <div key={s.id} className="group relative">
                <Link
                  to="/chat/$sessionId"
                  params={{ sessionId: s.id }}
                  className={`block truncate rounded-md border px-1.5 py-1.5 pr-6 text-left text-[13px] leading-snug transition-colors ${
                    active
                      ? "border-blue-400/60 bg-blue-50 text-slate-900 dark:border-blue-500/40 dark:bg-blue-950/40 dark:text-slate-100"
                      : "border-transparent text-slate-700 hover:bg-slate-200/80 dark:text-slate-200 dark:hover:bg-surface-mute/75"
                  }`}
                  title={s.title}
                >
                  {s.title || s.id}
                </Link>
                <button
                  type="button"
                  className="absolute top-1/2 right-0.5 z-10 flex h-5 w-5 -translate-y-1/2 items-center justify-center rounded text-slate-400 opacity-60 hover:text-slate-700 group-hover:opacity-100 dark:hover:text-slate-200"
                  title={t("pastSessionsDeleteTip")}
                  aria-label={t("pastSessionsDeleteTip")}
                  onMouseDown={(ev) => {
                    ev.preventDefault();
                    ev.stopPropagation();
                  }}
                  onClick={(ev) => onDelete(ev, s.id)}
                >
                  <span className="text-sm leading-none" aria-hidden>
                    ×
                  </span>
                </button>
              </div>
            );
          })
        )}
      </nav>
      {/* Tool sections - sticky at bottom, scroll within their own panel so sessions list is never squeezed */}
      <div className="pm-scroll shrink-0 overflow-y-auto border-t border-slate-200 dark:border-surface-mute" style={{ maxHeight: "45%" }}>
        <SidebarPresetsSection onPresetApplied={onPresetApplied} />
        <SidebarProvidersSection />
        <SidebarPathToolsSection />
        <SidebarMcpCatalogSection />
      </div>
    </aside>
  );
}
