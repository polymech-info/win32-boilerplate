import { t } from "./i18n.js";
import type { QuickAction } from "./pmChatStore";
import { usePmChatStore } from "./pmChatStore";
export function QuickActionsModal() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const draft = usePmChatStore((s) => s.qaEditDraft);
  const closeQaModal = usePmChatStore((s) => s.closeQaModal);
  const resetQaEditDraft = usePmChatStore((s) => s.resetQaEditDraft);
  const saveQaModal = usePmChatStore((s) => s.saveQaModal);
  const addQaEditRow = usePmChatStore((s) => s.addQaEditRow);
  const removeQaEditRow = usePmChatStore((s) => s.removeQaEditRow);
  const setQaEditDraft = usePmChatStore((s) => s.setQaEditDraft);

  const rows = Array.isArray(draft) ? draft : [];

  const updateRow = (id: string, patch: Partial<QuickAction>) => {
    const current = usePmChatStore.getState().qaEditDraft ?? [];
    setQaEditDraft(current.map((r) => (r.id === id ? { ...r, ...patch } : r)));
  };

  return (
    <div className="pm-qa-modal fixed inset-0 z-[200] flex items-center justify-center p-3 sm:p-6" role="presentation">
      <button type="button" className="absolute inset-0 bg-slate-900/45 dark:bg-slate-950/70" aria-label={t("qaClose")} onClick={() => closeQaModal()} />
      <div
        className="pm-qa-modal-panel relative z-10 flex max-h-[85vh] w-full max-w-2xl flex-col overflow-hidden rounded-2xl border border-slate-200/80 bg-white shadow-2xl dark:border-slate-500/60 dark:bg-slate-950"
        role="dialog"
        aria-modal="true"
        aria-labelledby="pm-qa-modal-title"
      >
        <div className="flex items-start justify-between border-b border-slate-200 px-4 py-3 dark:border-slate-700 sm:px-5">
          <div className="min-w-0 pr-2">
            <h2 id="pm-qa-modal-title" className="text-base font-semibold text-slate-900 dark:text-slate-100">
              {t("qaEditTitle")}
            </h2>
            <p className="mt-0.5 text-sm text-slate-500 dark:text-slate-400">{t("qaEditSubtitle")}</p>
          </div>
          <button
            type="button"
            className="flex h-8 w-8 flex-shrink-0 items-center justify-center rounded-md text-lg leading-none text-slate-500 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800 dark:hover:text-slate-50"
            title={t("qaClose")}
            aria-label={t("qaClose")}
            onClick={() => closeQaModal()}
          >
            ×
          </button>
        </div>
        <div className="pm-qa-modal-scroll min-h-0 flex-1 space-y-2.5 overflow-y-auto px-4 py-3 sm:px-5">
          {rows.length === 0 ? (
            <p className="rounded-lg border border-dashed border-slate-200 p-4 text-center text-sm text-slate-500 dark:border-slate-600 dark:text-slate-400">
              {t("qaNoActions")}
            </p>
          ) : (
            rows.map((a) => (
              <div
                key={a.id}
                className="flex items-start gap-2 rounded-lg border border-slate-200 bg-slate-50/80 p-2.5 dark:border-slate-600/90 dark:bg-slate-900/80"
              >
                <input
                  type="text"
                  className="pm-qa-icon w-11 flex-shrink-0 rounded-md border border-slate-200 bg-white px-1.5 py-1.5 text-center text-sm dark:border-slate-500 dark:bg-slate-950"
                  value={a.icon}
                  maxLength={4}
                  placeholder="\u2728"
                  title={t("qaIconTitle")}
                  onChange={(e) => updateRow(a.id, { icon: e.target.value })}
                />
                <input
                  type="text"
                  className="pm-qa-name min-w-0 w-24 flex-1 rounded-md border border-slate-200 bg-white px-2 py-1.5 text-sm dark:border-slate-500 dark:bg-slate-950"
                  value={a.name}
                  maxLength={30}
                  placeholder={t("qaNamePh")}
                  onChange={(e) => updateRow(a.id, { name: e.target.value })}
                />
                <input
                  type="text"
                  className="pm-qa-prompt min-w-0 flex-[2] rounded-md border border-slate-200 bg-white px-2 py-1.5 text-sm dark:border-slate-500 dark:bg-slate-950"
                  value={a.prompt}
                  maxLength={200}
                  placeholder={t("qaPromptPh")}
                  onChange={(e) => updateRow(a.id, { prompt: e.target.value })}
                />
                <button
                  type="button"
                  className="pm-qa-row-del flex h-9 w-9 flex-shrink-0 items-center justify-center rounded-md text-red-600 hover:bg-red-50 dark:text-red-400 dark:hover:bg-red-950/40"
                  title={t("qaRemoveTip")}
                  onClick={() => removeQaEditRow(a.id)}
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
            ))
          )}
        </div>
        <div className="border-t border-slate-200 p-2 dark:border-slate-700 sm:px-4">
          <button
            type="button"
            className="flex w-full items-center justify-center gap-2 rounded-lg border-2 border-dashed border-slate-200 py-2.5 text-sm text-slate-600 transition-colors hover:border-slate-300 hover:bg-slate-50 dark:border-slate-600 dark:text-slate-300 dark:hover:border-slate-500 dark:hover:bg-slate-800/60"
            onClick={() => addQaEditRow()}
          >
            <span className="text-base leading-none">+</span> {t("qaAddRow")}
          </button>
        </div>
        <div className="flex flex-col-reverse gap-2 border-t border-slate-200 bg-slate-50/80 px-4 py-3 dark:border-slate-600/80 dark:bg-slate-950/95 sm:flex-row sm:items-center sm:justify-end sm:gap-2 sm:px-5">
          <button type="button" className="pm-btn-secondary" onClick={() => closeQaModal()}>
            {t("qaCancel")}
          </button>
          <button type="button" className="rounded-lg border border-slate-200/80 bg-slate-200/60 px-4 py-2 text-sm font-medium text-slate-800 dark:border-slate-600 dark:bg-slate-700/80 dark:text-slate-200" onClick={() => resetQaEditDraft()}>
            {t("qaReset")}
          </button>
          <button type="button" className="pm-btn-primary" onClick={() => saveQaModal()}>
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
