import { useEffect, useMemo } from "react";

import { CHAT_ROUTERS, modelListForRouterId, sortAlpha, sortImageModelRowList, sortReplicateCollections } from "./chatConstants";
import { layoutDesignPresetGroups } from "./designPresets";
import { expandedResizePresetPrompt, layoutResizePresetGroups } from "./resizePresets";
import { t } from "./i18n.js";
import { hasWebProviderHost } from "./hostBridge";
import { usePmChatStore } from "./pmChatStore";
import { SelectionFilmStrip } from "./SelectionFilmStrip";
import { IconPencil, IconRefresh } from "./svgIcons";
import { VideoOpenApiToolbarFields } from "./videoOpenApiToolbarFields";

function qaChev(open: boolean): string {
  return open ? "\u25BC" : "\u25B6";
}

/** Format a USD dollar amount compactly: "$0.0042" or "$1.23". */
function fmtUsd(v: number): string {
  if (v < 0.01) return `$${v.toFixed(4)}`;
  if (v < 10)   return `$${v.toFixed(3)}`;
  return `$${v.toFixed(2)}`;
}

/** Compact "spent / max" badge, e.g. "$0.023 / $5.00" or "$0.023 / ∞". */
function PixlwizCreditPill() {
  const credit = usePmChatStore((s) => s.pixlwizCredit);
  if (!credit) return null;
  const spentLabel = fmtUsd(credit.spend);
  const maxLabel   = credit.max !== null ? fmtUsd(credit.max) : "\u221E";
  const title      = `Pixlwiz credits used: ${spentLabel} of ${maxLabel}`;
  const nearLimit  = credit.max !== null && credit.max > 0 && credit.spend / credit.max > 0.85;
  return (
    <span
      className={`ml-auto shrink-0 rounded px-1.5 py-0.5 font-mono text-[10px] tabular-nums ${
        nearLimit
          ? "bg-amber-100 text-amber-700 dark:bg-amber-900/40 dark:text-amber-300"
          : "bg-slate-200/70 text-slate-500 dark:bg-slate-700/50 dark:text-slate-400"
      }`}
      title={title}
      aria-label={title}
    >
      {spentLabel} / {maxLabel}
    </span>
  );
}

export function QuickActionsToolbar() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;

  const selection = usePmChatStore((s) => s.selection);
  const busy = usePmChatStore((s) => s.busy);
  const qaToolbarOpen = usePmChatStore((s) => s.qaToolbarOpen);
  const qaPresetsGroupOpen = usePmChatStore((s) => s.qaPresetsGroupOpen);
  const qaProvidersOpen = usePmChatStore((s) => s.qaProvidersOpen);
  const quickActions = usePmChatStore((s) => s.quickActions);
  const designPresets = usePmChatStore((s) => s.designPresets);
  const resizePresets = usePmChatStore((s) => s.resizePresets);
  const routerQuick = usePmChatStore((s) => s.routerQuick);
  const modelQuick = usePmChatStore((s) => s.modelQuick);
  const savedChatModel = usePmChatStore((s) => s.savedChatModel);
  const savedChatRouter = usePmChatStore((s) => s.savedChatRouter);
  const llmModelRows = usePmChatStore((s) => s.llmModelRows);
  const llmModelRowsLoading = usePmChatStore((s) => s.llmModelRowsLoading);
  const llmModelRowsRouter = usePmChatStore((s) => s.llmModelRowsRouter);
  const fetchLlmModels = usePmChatStore((s) => s.fetchLlmModels);
  const imageProvider = usePmChatStore((s) => s.imageProvider);
  const imageModel = usePmChatStore((s) => s.imageModel);
  const replicateCollection = usePmChatStore((s) => s.replicateCollection);
  const imageProviderRows = usePmChatStore((s) => s.imageProviderRows);
  const imageModelRows = usePmChatStore((s) => s.imageModelRows);
  const videoProvider = usePmChatStore((s) => s.videoProvider);
  const videoModel = usePmChatStore((s) => s.videoModel);
  const videoReplicateCollection = usePmChatStore((s) => s.videoReplicateCollection);
  const videoModelRows = usePmChatStore((s) => s.videoModelRows);
  const replicateCollections = usePmChatStore((s) => s.replicateCollections);
  const providerUiBooting = usePmChatStore((s) => s.providerUiBooting);
  const videoOpenApiFlat = usePmChatStore((s) => s.videoOpenApiFlat);
  const videoOpenApiFlatLoading = usePmChatStore((s) => s.videoOpenApiFlatLoading);
  const videoOpenApiFlatError = usePmChatStore((s) => s.videoOpenApiFlatError);
  const videoOpenApiFieldValues = usePmChatStore((s) => s.videoOpenApiFieldValues);
  const imageOpenApiFlat = usePmChatStore((s) => s.imageOpenApiFlat);
  const imageOpenApiFlatLoading = usePmChatStore((s) => s.imageOpenApiFlatLoading);
  const imageOpenApiFlatError = usePmChatStore((s) => s.imageOpenApiFlatError);
  const imageOpenApiFieldValues = usePmChatStore((s) => s.imageOpenApiFieldValues);

  const toggleQaToolbar = usePmChatStore((s) => s.toggleQaToolbar);
  const toggleQaPresetsGroup = usePmChatStore((s) => s.toggleQaPresetsGroup);
  const openDesignPresetModal = usePmChatStore((s) => s.openDesignPresetModal);
  const openResizePresetModal = usePmChatStore((s) => s.openResizePresetModal);
  const toggleQaProviders = usePmChatStore((s) => s.toggleQaProviders);
  const setRouterQuick = usePmChatStore((s) => s.setRouterQuick);
  const setModelQuick = usePmChatStore((s) => s.setModelQuick);
  const applyQuickAction = usePmChatStore((s) => s.applyQuickAction);
  const applyResizePreset = usePmChatStore((s) => s.applyResizePreset);
  const openQaModal = usePmChatStore((s) => s.openQaModal);
  const setImageProvider = usePmChatStore((s) => s.setImageProvider);
  const setReplicateCollection = usePmChatStore((s) => s.setReplicateCollection);
  const refreshReplicateCollectionsAndModels = usePmChatStore((s) => s.refreshReplicateCollectionsAndModels);
  const setImageModel = usePmChatStore((s) => s.setImageModel);
  const setVideoProvider = usePmChatStore((s) => s.setVideoProvider);
  const setVideoReplicateCollection = usePmChatStore((s) => s.setVideoReplicateCollection);
  const refreshVideoReplicateCollectionsAndModels = usePmChatStore((s) => s.refreshVideoReplicateCollectionsAndModels);
  const setVideoModel = usePmChatStore((s) => s.setVideoModel);
  const setVideoOpenApiFieldValue = usePmChatStore((s) => s.setVideoOpenApiFieldValue);
  const setImageOpenApiFieldValue = usePmChatStore((s) => s.setImageOpenApiFieldValue);

  const effectiveRouterId = routerQuick || savedChatRouter || "openrouter";
  const routerHasLiveCatalog = effectiveRouterId === "openrouter" || effectiveRouterId === "openai";

  const modelOptions = useMemo(() => {
    if (routerHasLiveCatalog && llmModelRowsRouter === effectiveRouterId && llmModelRows.length > 0) {
      const ids = new Set(llmModelRows.map((r) => r.id));
      if (savedChatModel && !routerQuick) ids.add(savedChatModel);
      return [...ids].sort(sortAlpha);
    }
    const list = modelListForRouterId(effectiveRouterId);
    const extra = new Set(list);
    if (savedChatModel && !routerQuick) extra.add(savedChatModel);
    return [...extra].sort(sortAlpha);
  }, [routerHasLiveCatalog, llmModelRows, llmModelRowsRouter, effectiveRouterId, routerQuick, savedChatModel]);

  const sortedProviders = useMemo(() => {
    if (!imageProviderRows?.length) return [];
    return [...imageProviderRows].sort((a, b) =>
      sortAlpha(a.label || a.id || a.name, b.label || b.id || b.name),
    );
  }, [imageProviderRows]);

  const sortedImageModels = useMemo(() => {
    const raw = imageModelRows || [];
    const want = imageModel || "";
    const rows =
      want && !raw.some((m) => m.id === want) ? [...raw, { id: want, label: want }] : raw;
    return sortImageModelRowList(rows);
  }, [imageModelRows, imageModel]);

  const sortedRepCols = useMemo(() => sortReplicateCollections(replicateCollections || []), [replicateCollections]);

  const designPresetGroups = useMemo(() => layoutDesignPresetGroups(designPresets), [designPresets]);
  const resizePresetGroups = useMemo(() => layoutResizePresetGroups(resizePresets), [resizePresets]);

  const mainHint = !qaToolbarOpen ? <span className="pl-2 text-[10px] text-slate-500 dark:text-slate-500">{t("qaMainHint")}</span> : null;

  const isRep = (imageProvider || "") === "replicate";
  const isRepVideo = (videoProvider || "") === "replicate";

  const sortedVideoModels = useMemo(() => {
    const raw = videoModelRows || [];
    const want = videoModel || "";
    const rows = want && !raw.some((m) => m.id === want) ? [...raw, { id: want, label: want }] : raw;
    return sortImageModelRowList(rows);
  }, [videoModelRows, videoModel]);
  const filmstripVisible = usePmChatStore((s) => s.filmstripVisible);

  return (
    <div className="pm-qa flex min-h-0 flex-shrink-0 flex-col border-t border-slate-200 bg-slate-50/90 dark:border-surface-mute dark:bg-surface-dark/92">
      {filmstripVisible ? <SelectionFilmStrip forceShow /> : null}
      <button
        type="button"
        className="pm-qa-tgroup flex w-full items-center gap-1.5 px-3 py-1.5 text-left"
        aria-expanded={qaToolbarOpen}
        title={t("qaToggleToolsTitle")}
        onClick={() => toggleQaToolbar()}
      >
        <span className="flex w-4 flex-shrink-0 justify-center text-center text-[10px] text-slate-500" aria-hidden>
          {qaChev(qaToolbarOpen)}
        </span>
        <span className="text-xs font-semibold text-slate-700 dark:text-slate-200">{t("quickTools")}</span>
        {mainHint}
        <span className="ml-auto" />
        <PixlwizCreditPill />
      </button>

      {qaToolbarOpen ? (
        <div className="pm-qa-toolbar-body pm-scroll min-h-0 max-h-[min(48dvh,32rem,calc(100dvh-11rem))] space-y-2 overflow-y-auto overscroll-y-contain px-3 pb-2">
          <div className="overflow-hidden rounded-lg border border-slate-200/80 bg-slate-100/50 dark:border-surface-mute/60 dark:bg-surface-mute/22">
            <button
              type="button"
              className="pm-qa-tgroup flex w-full items-center gap-2 px-2 py-1.5 text-left"
              aria-expanded={qaPresetsGroupOpen}
              title={t("qaPresetsSectionTip")}
              onClick={() => toggleQaPresetsGroup()}
            >
              <span className="flex w-4 flex-shrink-0 justify-center text-[10px] text-slate-500" aria-hidden>
                {qaChev(qaPresetsGroupOpen)}
              </span>
              <span className="text-[10px] font-semibold tracking-wide text-slate-500 uppercase dark:text-slate-400">{t("qaPresetsSection")}</span>
            </button>
            {qaPresetsGroupOpen ? (
              <div className="space-y-3 border-t border-slate-200/60 px-2 py-2 dark:border-slate-600/50">
                <div>
                  <div className="mb-1.5 flex min-w-0 items-stretch gap-0.5">
                    <div className="flex min-w-0 flex-1 items-center text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">
                      {t("qaStylePresets")}
                    </div>
                    <button
                      type="button"
                      className="pm-icon-btn h-7 w-7 flex-shrink-0 text-slate-500 hover:text-slate-800 dark:hover:text-slate-100"
                      title={t("qaEditTip")}
                      onClick={() => openQaModal()}
                    >
                      <span className="sr-only">{t("qaEdit")}</span>
                      <IconPencil />
                    </button>
                  </div>
                  <div className="flex flex-wrap gap-2">
                    {quickActions.length === 0 ? (
                      <span className="text-xs text-slate-400">{t("qaNoPresets")}</span>
                    ) : (
                      quickActions.map((a) => (
                        <button
                          key={a.id}
                          type="button"
                          className="pm-qa-preset inline-flex max-w-full items-center gap-1.5 rounded-md border border-slate-300 bg-slate-200/80 px-2.5 py-1 text-left text-xs text-slate-800 transition-colors hover:bg-slate-300/80 disabled:pointer-events-none disabled:opacity-40 dark:border-slate-600 dark:bg-surface-dimmed/90 dark:text-slate-100 dark:hover:bg-surface-mute/90"
                          title={a.prompt}
                          disabled={busy}
                          onClick={() => applyQuickAction(a)}
                        >
                          <span className="flex-shrink-0 select-none" aria-hidden>
                            {a.icon || ""}
                          </span>
                          <span className="min-w-0 truncate">{a.name}</span>
                        </button>
                      ))
                    )}
                  </div>
                </div>

                <div className="border-t border-slate-200/50 pt-2 dark:border-slate-600/40">
                  <div className="mb-1.5 flex min-w-0 items-stretch gap-0.5">
                    <div className="flex min-w-0 flex-1 items-center text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400" title={t("qaDesignSectionTip")}>
                      {t("qaDesignPresets")}
                    </div>
                    <button
                      type="button"
                      className="pm-icon-btn h-7 w-7 flex-shrink-0 text-slate-500 hover:text-slate-800 dark:hover:text-slate-100"
                      title={t("qaDesignEditTip")}
                      onClick={() => openDesignPresetModal()}
                    >
                      <span className="sr-only">{t("qaEdit")}</span>
                      <IconPencil />
                    </button>
                  </div>
                  <p className="mb-2 text-[10px] text-slate-500 dark:text-slate-500">{t("qaDesignSectionHelp")}</p>
                  {designPresets.length === 0 ? (
                    <span className="text-xs text-slate-400">{t("qaNoDesignPresets")}</span>
                  ) : (
                    <div className="space-y-3">
                      {designPresetGroups.map(({ title, items }) => (
                        <div key={title || "__ungrouped__"}>
                          <div className="mb-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">
                            {title || t("qaDesignGroupOther")}
                          </div>
                          <div className="grid grid-cols-3 gap-2">
                            {items.map((a) => (
                              <button
                                key={a.id}
                                type="button"
                                className="pm-qa-preset flex min-w-0 w-full items-center gap-1 rounded-md border border-slate-300 bg-slate-200/80 px-1.5 py-1.5 text-left text-[11px] leading-snug text-slate-800 transition-colors hover:bg-slate-300/80 disabled:pointer-events-none disabled:opacity-40 dark:border-slate-600 dark:bg-surface-dimmed/90 dark:text-slate-100 dark:hover:bg-surface-mute/90"
                                title={a.prompt}
                                disabled={busy}
                                onClick={() => applyQuickAction(a)}
                              >
                                <span className="flex-shrink-0 select-none" aria-hidden>
                                  {a.icon || ""}
                                </span>
                                <span className="min-w-0 flex-1 break-words [overflow-wrap:anywhere]">{a.name}</span>
                              </button>
                            ))}
                          </div>
                        </div>
                      ))}
                    </div>
                  )}
                </div>

                <div className="border-t border-slate-200/50 pt-2 dark:border-slate-600/40">
                  <div className="mb-1.5 flex min-w-0 items-stretch gap-0.5">
                    <div className="flex min-w-0 flex-1 items-center text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400" title={t("qaResizeSectionTip")}>
                      {t("qaResizePresets")}
                    </div>
                    <button
                      type="button"
                      className="pm-icon-btn h-7 w-7 flex-shrink-0 text-slate-500 hover:text-slate-800 dark:hover:text-slate-100"
                      title={t("qaResizeEditTip")}
                      onClick={() => openResizePresetModal()}
                    >
                      <span className="sr-only">{t("qaEdit")}</span>
                      <IconPencil />
                    </button>
                  </div>
                  <p className="mb-2 text-[10px] text-slate-500 dark:text-slate-500">{t("qaResizeSectionHelp")}</p>
                  {resizePresets.length === 0 ? (
                    <span className="text-xs text-slate-400">{t("qaNoResizePresets")}</span>
                  ) : (
                    <div className="space-y-3">
                      {resizePresetGroups.map(({ title, items }) => (
                        <div key={title || "__ungrouped__"}>
                          <div className="mb-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400">
                            {title || t("qaResizeGroupOther")}
                          </div>
                          <div className="grid grid-cols-3 gap-2">
                            {items.map((a) => (
                              <button
                                key={a.id}
                                type="button"
                                className="pm-qa-preset flex min-w-0 w-full items-center gap-1 rounded-md border border-slate-300 bg-slate-200/80 px-1.5 py-1.5 text-left text-[11px] leading-snug text-slate-800 transition-colors hover:bg-slate-300/80 disabled:pointer-events-none disabled:opacity-40 dark:border-slate-600 dark:bg-surface-dimmed/90 dark:text-slate-100 dark:hover:bg-surface-mute/90"
                                title={expandedResizePresetPrompt(a)}
                                disabled={busy}
                                onClick={() => applyResizePreset(a)}
                              >
                                <span className="flex-shrink-0 select-none" aria-hidden>
                                  {a.icon || ""}
                                </span>
                                <span className="min-w-0 flex-1 break-words [overflow-wrap:anywhere]">{a.name}</span>
                              </button>
                            ))}
                          </div>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              </div>
            ) : null}
          </div>

          <div className="overflow-hidden rounded-lg border border-slate-200/80 bg-slate-100/50 dark:border-surface-mute/60 dark:bg-surface-mute/22">
            <button
              type="button"
              className="pm-qa-tgroup flex w-full items-center gap-2 px-2 py-1.5 text-left"
              aria-expanded={qaProvidersOpen}
              title={t("qaProviderModelsSectionTip")}
              onClick={() => toggleQaProviders()}
            >
              <span className="flex w-4 flex-shrink-0 justify-center text-[10px] text-slate-500" aria-hidden>
                {qaChev(qaProvidersOpen)}
              </span>
              <span className="text-[10px] font-semibold tracking-wide text-slate-500 uppercase dark:text-slate-400">{t("qaProviderModelsSection")}</span>
            </button>
            {qaProvidersOpen ? (
              <div className="space-y-3 border-t border-slate-200/60 px-2 py-2 dark:border-slate-600/50">
                <div>
                  <div className="mb-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400" title={t("qaLlmSectionTip")}>
                    {t("qaLlmSection")}
                  </div>
                  <div className="flex min-w-0 flex-row flex-wrap items-center gap-1.5 sm:gap-2">
                    <div className="flex min-w-0 flex-none items-center sm:max-w-[12rem]">
                      <label htmlFor="pm-router-quick" className="sr-only">
                        {t("ddChatRouter")}
                      </label>
                      <select
                        id="pm-router-quick"
                        className="pm-dd w-full min-w-0"
                        disabled={busy}
                        title={t("ddChatRouter")}
                        value={routerQuick}
                        onChange={(e) => setRouterQuick(e.target.value)}
                      >
                        <option value="">{t("optChatSaved")}</option>
                        {CHAT_ROUTERS.map((r) => (
                          <option key={r.id} value={r.id}>
                            {r.label}
                          </option>
                        ))}
                      </select>
                    </div>
                    <div className="flex min-w-0 flex-1 basis-[10rem] items-center gap-1">
                      <label htmlFor="pm-model-quick" className="sr-only">
                        {t("ddChatModel")}
                      </label>
                      <select
                        id="pm-model-quick"
                        className="pm-dd w-full min-w-0"
                        disabled={busy}
                        title={t("ddChatModel")}
                        value={modelQuick}
                        onChange={(e) => setModelQuick(e.target.value)}
                      >
                        <option value="">{routerQuick ? t("optChatRouterDefault") : t("optChatSaved")}</option>
                        {modelOptions.map((m) => (
                          <option key={m} value={m}>
                            {m}
                          </option>
                        ))}
                      </select>
                      {routerHasLiveCatalog && (
                        <button
                          type="button"
                          className="flex-none rounded p-0.5 text-slate-400 hover:text-slate-600 dark:hover:text-slate-300"
                          title={t("btnRefreshModels")}
                          disabled={busy || llmModelRowsLoading}
                          onClick={() => void fetchLlmModels(effectiveRouterId, true)}
                        >
                          <IconRefresh />
                        </button>
                      )}
                    </div>
                  </div>
                  <p className="mt-1.5 text-[10px] text-slate-500 dark:text-slate-500">{t("qaLlmSectionHelp")}</p>
                </div>

                <div className="border-t border-slate-200/50 pt-2 dark:border-slate-600/40">
                  <div className="mb-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400" title={t("qaImageSectionTip")}>
                    {t("qaImageSection")}
                  </div>
                  <p className="mb-2 text-[10px] text-slate-500 dark:text-slate-500">{t("qaImageSectionHelp")}</p>
                  <div className="flex min-w-0 flex-row flex-wrap items-center gap-1.5 sm:gap-2">
                    <div className="flex min-w-0 shrink-0 items-center">
                      <label htmlFor="pm-qa-image-provider" className="sr-only">
                        {t("ddImageProvider")}
                      </label>
                      <select
                        id="pm-qa-image-provider"
                        className="pm-dd max-w-full"
                        disabled={busy}
                        title={t("ddImageProvider")}
                        value={imageProvider}
                        onChange={(e) => void setImageProvider(e.target.value)}
                      >
                        {sortedProviders.length === 0 ? (
                          <option value="">{t("optImageLoading")}</option>
                        ) : (
                          sortedProviders.map((r) => {
                            const id = r.id || r.name || "";
                            return (
                              <option key={id} value={id}>
                                {r.label || id}
                              </option>
                            );
                          })
                        )}
                      </select>
                    </div>
                    {isRep ? (
                      <div className="flex min-w-0 shrink-0 items-center gap-0.5">
                        <label htmlFor="pm-qa-rep-collection" className="sr-only">
                          {t("ddReplicateCollection")}
                        </label>
                        <select
                          id="pm-qa-rep-collection"
                          className="pm-dd max-w-[9.5rem] min-w-0 sm:max-w-[11rem]"
                          disabled={busy}
                          title={t("ddReplicateCollection")}
                          value={replicateCollection}
                          onChange={(e) => void setReplicateCollection(e.target.value)}
                        >
                          {sortedRepCols.length === 0 ? (
                            <option value={replicateCollection || "official"}>{replicateCollection || "official"}</option>
                          ) : (
                            sortedRepCols
                              .map((col) => {
                                const slug = col.slug || col.name;
                                if (!slug) return null;
                                return (
                                  <option key={slug} value={slug}>
                                    {col.name || col.slug}
                                  </option>
                                );
                              })
                              .filter(Boolean)
                          )}
                        </select>
                        <button
                          type="button"
                          className="pm-icon-btn flex-shrink-0"
                          disabled={busy || providerUiBooting}
                          title={t("btnRefreshReplicateTip")}
                          onClick={() => void refreshReplicateCollectionsAndModels()}
                        >
                          <span className="sr-only">{t("btnRefreshReplicateTip")}</span>
                          <IconRefresh />
                        </button>
                      </div>
                    ) : null}
                    <div className="flex min-w-0 flex-1 basis-[12rem] items-center">
                      <label htmlFor="pm-qa-image-model" className="sr-only">
                        {t("ddImageModel")}
                      </label>
                      <select
                        id="pm-qa-image-model"
                        className="pm-dd max-w-full min-w-0 w-full flex-1"
                        disabled={busy}
                        title={t("ddImageModel")}
                        value={imageModel}
                        onChange={(e) => void setImageModel(e.target.value)}
                      >
                        {sortedImageModels.length === 0 ? (
                          <option value="">{t("optImageNoModels")}</option>
                        ) : (
                          sortedImageModels.map((m) => (
                            <option key={m.id} value={m.id}>
                              {m.label || m.id}
                            </option>
                          ))
                        )}
                      </select>
                    </div>
                  </div>
                  {isRep ? (
                    <VideoOpenApiToolbarFields
                      variant="image"
                      loading={imageOpenApiFlatLoading}
                      error={imageOpenApiFlatError}
                      flat={imageOpenApiFlat}
                      values={imageOpenApiFieldValues}
                      onChange={setImageOpenApiFieldValue}
                    />
                  ) : null}
                </div>

                <div className="border-t border-slate-200/50 pt-2 dark:border-slate-600/40">
                  <div className="mb-1.5 text-[10px] font-semibold uppercase tracking-wide text-slate-600 dark:text-slate-400" title={t("qaVideoSectionTip")}>
                    {t("qaVideoSection")}
                  </div>
                  <p className="mb-2 text-[10px] text-slate-500 dark:text-slate-500">{t("qaVideoSectionHelp")}</p>
                  <div className="flex min-w-0 flex-row flex-wrap items-center gap-1.5 sm:gap-2">
                    <div className="flex min-w-0 shrink-0 items-center">
                      <label htmlFor="pm-qa-video-provider" className="sr-only">
                        {t("ddVideoProvider")}
                      </label>
                      <select
                        id="pm-qa-video-provider"
                        className="pm-dd max-w-full"
                        disabled={busy}
                        title={t("ddVideoProvider")}
                        value={videoProvider}
                        onChange={(e) => void setVideoProvider(e.target.value)}
                      >
                        {sortedProviders.length === 0 ? (
                          <option value="">{t("optImageLoading")}</option>
                        ) : (
                          sortedProviders.map((r) => {
                            const id = r.id || r.name || "";
                            return (
                              <option key={`v-${id}`} value={id}>
                                {r.label || id}
                              </option>
                            );
                          })
                        )}
                      </select>
                    </div>
                    {isRepVideo ? (
                      <div className="flex min-w-0 shrink-0 items-center gap-0.5">
                        <label htmlFor="pm-qa-video-rep-collection" className="sr-only">
                          {t("ddReplicateCollection")}
                        </label>
                        <select
                          id="pm-qa-video-rep-collection"
                          className="pm-dd max-w-[9.5rem] min-w-0 sm:max-w-[11rem]"
                          disabled={busy}
                          title={t("ddReplicateCollection")}
                          value={videoReplicateCollection}
                          onChange={(e) => void setVideoReplicateCollection(e.target.value)}
                        >
                          {sortedRepCols.length === 0 ? (
                            <option value={videoReplicateCollection || "official"}>{videoReplicateCollection || "official"}</option>
                          ) : (
                            sortedRepCols
                              .map((col) => {
                                const slug = col.slug || col.name;
                                if (!slug) return null;
                                return (
                                  <option key={`vv-${slug}`} value={slug}>
                                    {col.name || col.slug}
                                  </option>
                                );
                              })
                              .filter(Boolean)
                          )}
                        </select>
                        <button
                          type="button"
                          className="pm-icon-btn flex-shrink-0"
                          disabled={busy || providerUiBooting}
                          title={t("btnRefreshReplicateTip")}
                          onClick={() => void refreshVideoReplicateCollectionsAndModels()}
                        >
                          <span className="sr-only">{t("btnRefreshReplicateTip")}</span>
                          <IconRefresh />
                        </button>
                      </div>
                    ) : null}
                    <div className="flex min-w-0 flex-1 basis-[12rem] items-center">
                      <label htmlFor="pm-qa-video-model" className="sr-only">
                        {t("ddVideoModel")}
                      </label>
                      <select
                        id="pm-qa-video-model"
                        className="pm-dd max-w-full min-w-0 w-full flex-1"
                        disabled={busy}
                        title={t("ddVideoModel")}
                        value={videoModel}
                        onChange={(e) => void setVideoModel(e.target.value)}
                      >
                        {sortedVideoModels.length === 0 ? (
                          <option value="">{t("optImageNoModels")}</option>
                        ) : (
                          sortedVideoModels.map((m) => (
                            <option key={`vm-${m.id}`} value={m.id}>
                              {m.label || m.id}
                            </option>
                          ))
                        )}
                      </select>
                    </div>
                  </div>
                  {isRepVideo ? (
                    <VideoOpenApiToolbarFields
                      variant="video"
                      loading={videoOpenApiFlatLoading}
                      error={videoOpenApiFlatError}
                      flat={videoOpenApiFlat}
                      values={videoOpenApiFieldValues}
                      onChange={setVideoOpenApiFieldValue}
                    />
                  ) : null}
                </div>
              </div>
            ) : null}
          </div>
        </div>
      ) : null}
    </div>
  );
}
