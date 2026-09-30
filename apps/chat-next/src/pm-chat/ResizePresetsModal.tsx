import { t } from "./i18n.js";
import type { ResizePreset, ResizeTarget } from "./resizePresets";
import { usePmChatStore } from "./pmChatStore";

function wInputValue(t: ResizeTarget | undefined): string {
  if (!t || !t.w || t.w <= 0) return "";
  return String(t.w);
}

function hInputValue(t: ResizeTarget | undefined): string {
  if (!t || !t.h || t.h <= 0) return "";
  return String(t.h);
}

export function ResizePresetsModal() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const draft = usePmChatStore((s) => s.resizeEditDraft);
  const closeResizePresetModal = usePmChatStore((s) => s.closeResizePresetModal);
  const resetResizeEditDraft = usePmChatStore((s) => s.resetResizeEditDraft);
  const saveResizePresetModal = usePmChatStore((s) => s.saveResizePresetModal);
  const addResizeEditRow = usePmChatStore((s) => s.addResizeEditRow);
  const removeResizeEditRow = usePmChatStore((s) => s.removeResizeEditRow);
  const setResizeEditDraft = usePmChatStore((s) => s.setResizeEditDraft);

  const rows = Array.isArray(draft) ? draft : [];

  const updateRow = (id: string, patch: Partial<ResizePreset>) => {
    const current = usePmChatStore.getState().resizeEditDraft ?? [];
    setResizeEditDraft(current.map((r) => (r.id === id ? { ...r, ...patch } : r)));
  };

  const updateTarget = (id: string, patch: Partial<ResizeTarget>) => {
    const current = usePmChatStore.getState().resizeEditDraft ?? [];
    setResizeEditDraft(
      current.map((r) => {
        if (r.id !== id) return r;
        const base = r.target ?? { w: 0, h: 0 };
        return { ...r, target: { ...base, ...patch } };
      }),
    );
  };

  return (
    <div className="pm-qa-modal fixed inset-0 z-[200] flex items-center justify-center p-3 sm:p-6" role="presentation">
      <button type="button" className="absolute inset-0 bg-slate-900/45 dark:bg-slate-950/70" aria-label={t("qaClose")} onClick={() => closeResizePresetModal()} />
      <div
        className="pm-qa-modal-panel relative z-10 flex max-h-[85vh] w-full max-w-2xl flex-col overflow-hidden rounded-2xl border border-slate-200/80 bg-white shadow-2xl dark:border-slate-500/60 dark:bg-slate-950"
        role="dialog"
        aria-modal="true"
        aria-labelledby="pm-resize-modal-title"
      >
        <div className="flex items-start justify-between border-b border-slate-200 px-4 py-3 dark:border-slate-700 sm:px-5">
          <div className="min-w-0 pr-2">
            <h2 id="pm-resize-modal-title" className="text-base font-semibold text-slate-900 dark:text-slate-100">
              {t("qaResizeEditTitle")}
            </h2>
            <p className="mt-0.5 text-sm text-slate-500 dark:text-slate-400">{t("qaResizeEditSubtitle")}</p>
          </div>
          <button
            type="button"
            className="flex h-8 w-8 flex-shrink-0 items-center justify-center rounded-md text-lg leading-none text-slate-500 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800 dark:hover:text-slate-50"
            title={t("qaClose")}
            aria-label={t("qaClose")}
            onClick={() => closeResizePresetModal()}
          >
            ×
          </button>
        </div>
        <div className="pm-qa-modal-scroll min-h-0 flex-1 space-y-2.5 overflow-y-auto px-4 py-3 sm:px-5">
          {rows.length === 0 ? (
            <p className="rounded-lg border border-dashed border-slate-200 p-4 text-center text-sm text-slate-500 dark:border-slate-600 dark:text-slate-400">
              {t("qaResizeNoRows")}
            </p>
          ) : (
            rows.map((a) => {
              return (
                <div
                  key={a.id}
                  className="flex flex-col gap-2 rounded-lg border border-slate-200 bg-slate-50/80 p-2.5 dark:border-slate-600/90 dark:bg-slate-900/80"
                >
                  <div className="flex flex-wrap items-start gap-2">
                    <input
                      type="text"
                      className="pm-qa-icon w-11 flex-shrink-0 rounded-md border border-slate-200 bg-white px-1.5 py-1.5 text-center text-sm dark:border-slate-500 dark:bg-slate-950"
                      value={a.icon}
                      maxLength={8}
                      placeholder="\u2194"
                      title={t("qaIconTitle")}
                      onChange={(e) => updateRow(a.id, { icon: e.target.value })}
                    />
                    <input
                      type="text"
                      className="min-w-0 w-28 flex-1 rounded-md border border-slate-200 bg-white px-2 py-1.5 text-sm dark:border-slate-500 dark:bg-slate-950 sm:min-w-[7rem]"
                      value={a.group ?? ""}
                      maxLength={60}
                      placeholder={t("qaResizeGroupPh")}
                      title={t("qaResizeGroupTip")}
                      onChange={(e) => updateRow(a.id, { group: e.target.value })}
                    />
                    <input
                      type="text"
                      className="min-w-0 flex-[2] rounded-md border border-slate-200 bg-white px-2 py-1.5 text-sm dark:border-slate-500 dark:bg-slate-950"
                      value={a.name}
                      maxLength={80}
                      placeholder={t("qaNamePh")}
                      onChange={(e) => updateRow(a.id, { name: e.target.value })}
                    />
                    <button
                      type="button"
                      className="pm-qa-row-del flex h-9 w-9 flex-shrink-0 items-center justify-center rounded-md text-red-600 hover:bg-red-50 dark:text-red-400 dark:hover:bg-red-950/40"
                      title={t("qaRemoveTip")}
                      onClick={() => removeResizeEditRow(a.id)}
                    >
                      <span className="sr-only">{t("qaDeleteSr")}</span>
                      <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
                        <polyline points="3 6 5 6 21 6" />
                        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                        <line x1="10" y1="11" x2="10" y2="17" />
                        <line x1="14" y1="11" x2="14" y2="17" />
                      </svg>
                    </button>
                  </div>
                  <div className="flex flex-wrap items-center gap-2 text-[11px] text-slate-600 dark:text-slate-300">
                    <span className="font-medium uppercase tracking-wide text-slate-700 dark:text-slate-200">{t("qaResizeTargetLabel")}</span>
                    <label className="inline-flex items-center gap-0.5">
                      <span className="text-slate-500 dark:text-slate-400">W</span>
                      <input
                        type="text"
                        inputMode="numeric"
                        className="w-16 rounded border border-slate-200 bg-white px-1.5 py-1 font-mono text-xs dark:border-slate-500 dark:bg-slate-950"
                        value={wInputValue(a.target)}
                        onChange={(e) => {
                          const v = e.target.value.replace(/\D/g, "");
                          updateTarget(a.id, { w: v ? parseInt(v, 10) : 0 });
                        }}
                      />
                    </label>
                    <label className="inline-flex items-center gap-0.5">
                      <span className="text-slate-500 dark:text-slate-400">H</span>
                      <input
                        type="text"
                        inputMode="numeric"
                        className="w-16 rounded border border-slate-200 bg-white px-1.5 py-1 font-mono text-xs dark:border-slate-500 dark:bg-slate-950"
                        value={hInputValue(a.target)}
                        onChange={(e) => {
                          const v = e.target.value.replace(/\D/g, "");
                          updateTarget(a.id, { h: v ? parseInt(v, 10) : 0 });
                        }}
                      />
                    </label>
                    <label className="inline-flex min-w-0 flex-1 items-center gap-0.5 sm:max-w-[12rem]">
                      <span className="flex-shrink-0 text-slate-500 dark:text-slate-400">{t("qaResizeAspectLabel")}</span>
                      <input
                        type="text"
                        className="min-w-0 flex-1 rounded border border-slate-200 bg-white px-1.5 py-1 text-xs dark:border-slate-500 dark:bg-slate-950"
                        value={a.target?.aspect ?? ""}
                        placeholder="9:16"
                        onChange={(e) => updateTarget(a.id, { aspect: e.target.value })}
                      />
                    </label>
                  </div>
                  <textarea
                    className="pm-qa-prompt min-h-[4.5rem] w-full resize-y rounded-md border border-slate-200 bg-white px-2 py-1.5 text-sm leading-snug dark:border-slate-500 dark:bg-slate-950"
                    value={a.prompt}
                    maxLength={8000}
                    placeholder={t("qaResizePromptPh")}
                    rows={4}
                    onChange={(e) => updateRow(a.id, { prompt: e.target.value })}
                  />
                  <p className="text-[10px] text-slate-500 dark:text-slate-400">{t("qaResizePromptHint")}</p>
                </div>
              );
            })
          )}
        </div>
        <div className="border-t border-slate-200 p-2 dark:border-slate-700 sm:px-4">
          <button
            type="button"
            className="flex w-full items-center justify-center gap-2 rounded-lg border-2 border-dashed border-slate-200 py-2.5 text-sm text-slate-600 transition-colors hover:border-slate-300 hover:bg-slate-50 dark:border-slate-600 dark:text-slate-300 dark:hover:border-slate-500 dark:hover:bg-slate-800/60"
            onClick={() => addResizeEditRow()}
          >
            <span className="text-base leading-none">+</span> {t("qaResizeAddRow")}
          </button>
        </div>
        <div className="flex flex-col-reverse gap-2 border-t border-slate-200 bg-slate-50/80 px-4 py-3 dark:border-slate-600/80 dark:bg-slate-950/95 sm:flex-row sm:items-center sm:justify-end sm:gap-2 sm:px-5">
          <button type="button" className="pm-btn-secondary" onClick={() => closeResizePresetModal()}>
            {t("qaCancel")}
          </button>
          <button
            type="button"
            className="rounded-lg border border-slate-200/80 bg-slate-200/60 px-4 py-2 text-sm font-medium text-slate-800 dark:border-slate-600 dark:bg-slate-700/80 dark:text-slate-200"
            onClick={() => resetResizeEditDraft()}
          >
            {t("qaResizeResetDefaults")}
          </button>
          <button type="button" className="pm-btn-primary" onClick={() => saveResizePresetModal()}>
            <span aria-hidden className="text-sm">
              {String.fromCodePoint(0x1f4be)}
            </span>{" "}
            {t("qaSave")}
          </button>
        </div>
      </div>
    </div>
  );
}
