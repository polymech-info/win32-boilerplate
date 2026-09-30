import { useCallback, useState, useSyncExternalStore } from "react";

import type { PixlwizAuthPayload } from "@pm/shared/web/hostBridge";
import { getColorSchemeDark, subscribeColorScheme, toggleColorScheme } from "@pm/shared/theme/colorScheme";
import { hasWebProviderHost, postHost } from "./hostBridge";
import { t } from "./i18n.js";
import { buildAssistantTurnMarkdown, buildUserMarkdown, folderDisplayName } from "./chatTurnFormat";
import { usePmChatStore } from "./pmChatStore";
import { formatCompactByteSize } from "./textUtils";
import {
  IconBot,
  IconMoon,
  IconPanelExpand,
  IconPastSessions,
  IconRefresh,
  IconSettings,
  IconStop,
  IconSun,
  IconTrash,
} from "./svgIcons";

/** Format a USD dollar amount compactly: "$0.0042", "$1.234", "$12.34". */
function fmtUsd(v: number): string {
  if (v < 0.01) return `$${v.toFixed(4)}`;
  if (v < 10)   return `$${v.toFixed(3)}`;
  return `$${v.toFixed(2)}`;
}

/** Inline spend/budget badge next to the auth dot. Hidden until first fetch. */
function PixlwizCreditBadge() {
  const credit = usePmChatStore((s) => s.pixlwizCredit);
  const devNoWebview = typeof window !== "undefined" && !window.chrome?.webview;

  if (!credit && devNoWebview) {
    return (
      <span
        className="shrink-0 rounded px-1.5 py-0.5 font-mono text-[10px] tabular-nums leading-none bg-slate-200/50 text-slate-400 dark:bg-slate-700/30 dark:text-slate-500"
        title="Credit balance (no host in dev)"
      >
        $?.?? / $?
      </span>
    );
  }

  if (!credit) return null;
  const spentLabel = fmtUsd(credit.spend);
  const maxLabel   = credit.max !== null ? fmtUsd(credit.max) : "\u221E";
  const title      = `Pixlwiz: ${spentLabel} spent of ${maxLabel}`;
  const nearLimit  = credit.max !== null && credit.max > 0 && credit.spend / credit.max > 0.85;
  return (
    <span
      className={`shrink-0 rounded px-1.5 py-0.5 font-mono text-[10px] tabular-nums leading-none ${
        nearLimit
          ? "bg-amber-100 text-amber-700 dark:bg-amber-900/40 dark:text-amber-300"
          : "bg-slate-200/70 text-slate-500 dark:bg-slate-700/50 dark:text-slate-400"
      }`}
      title={title}
      aria-label={title}
    >
      {spentLabel}&thinsp;/&thinsp;{maxLabel}
    </span>
  );
}

/** Colored dot only; titles / aria carry full status (no user id in UI). */
function PixlwizStatusDot({ auth }: { auth: PixlwizAuthPayload | null }) {
  const devNoWebview = typeof window !== "undefined" && !window.chrome?.webview;

  if (!auth && devNoWebview) {
    return (
      <span
        className="inline-block h-2.5 w-2.5 shrink-0 rounded-full bg-slate-400/55 ring-2 ring-slate-400/25 dark:bg-slate-500/55 dark:ring-slate-500/30"
        title={t("pixlwizPillDevTitle")}
        aria-label={t("pixlwizPillDevAria")}
      />
    );
  }

  if (auth && auth.read_ok === false && auth.read_error) {
    return (
      <span
        className="inline-block h-2.5 w-2.5 shrink-0 rounded-full bg-red-500 shadow-sm ring-2 ring-red-900/40 dark:bg-red-400 dark:ring-red-300/35"
        title={auth.read_error}
        aria-label={t("pixlwizAuthReadError", { message: auth.read_error })}
      />
    );
  }

  if (!auth || auth.read_ok === undefined) {
    return (
      <span
        className="inline-block h-2.5 w-2.5 shrink-0 animate-pulse rounded-full bg-slate-400/70 ring-2 ring-slate-300/30 dark:bg-slate-500/70 dark:ring-slate-400/25"
        aria-label={t("pixlwizAuthWaiting")}
        title={t("pixlwizAuthWaiting")}
      />
    );
  }

  if (auth.logged_in) {
    const id = auth.app_user_id || auth.zitadel_sub;
    const expired = auth.access_token_expired_est ? ` ${t("pixlwizAuthExpiredHint")}` : "";
    const title = id ? `${t("pixlwizPillInTitle")} ${id}${expired}` : `${t("pixlwizAuthSignedInToken")}${expired}`;
    return (
      <span
        className="inline-block h-2.5 w-2.5 shrink-0 rounded-full bg-emerald-500 shadow-sm ring-2 ring-emerald-700/35 dark:bg-emerald-400 dark:ring-emerald-300/30"
        title={title.trim()}
        aria-label={title.trim()}
      />
    );
  }

  if (auth.oauth_file_present) {
    return (
      <span
        className="inline-block h-2.5 w-2.5 shrink-0 rounded-full bg-amber-500 shadow-sm ring-2 ring-amber-800/35 dark:bg-amber-400 dark:ring-amber-200/25"
        title={t("pixlwizAuthFileNoToken")}
        aria-label={t("pixlwizAuthFileNoToken")}
      />
    );
  }

  return (
    <span
      className="inline-block h-2.5 w-2.5 shrink-0 rounded-full bg-rose-500 shadow-sm ring-2 ring-rose-900/35 dark:bg-rose-400 dark:ring-rose-200/25"
      title={t("pixlwizAuthSignedOut")}
      aria-label={t("pixlwizAuthSignedOut")}
    />
  );
}

export type ChatToolbarProps = {
  auth: PixlwizAuthPayload | null;
  /** When false, hide Pixlwiz account line and Refresh; other toolbar actions stay. */
  pixlwizAuthEnabled: boolean;
};

function ToolCallsToggleButton() {
  const showToolCalls = usePmChatStore((s) => s.showToolCalls);
  const toggleShowToolCalls = usePmChatStore((s) => s.toggleShowToolCalls);
  return (
    <button
      type="button"
      id="pm-tool-calls-toggle"
      className={`pm-icon-btn${showToolCalls ? " text-violet-600 dark:text-violet-300" : ""}`}
      title={showToolCalls ? t("toolCallsToggleHintOn") : t("toolCallsToggleHintOff")}
      aria-pressed={showToolCalls}
      onClick={() => toggleShowToolCalls()}
    >
      <span className="sr-only">{t("toolCallsToggleLabel")}</span>
      <IconBot />
    </button>
  );
}

function FilmstripToggleButton() {
  const filmstripVisible = usePmChatStore((s) => s.filmstripVisible);
  const toggleFilmstrip = usePmChatStore((s) => s.toggleFilmstrip);
  return (
    <button
      type="button"
      id="pm-filmstrip-toggle"
      className={`pm-icon-btn${filmstripVisible ? " text-violet-600 dark:text-violet-300" : ""}`}
      title={filmstripVisible ? "Hide filmstrip" : "Show filmstrip"}
      aria-pressed={filmstripVisible}
      onClick={() => toggleFilmstrip()}
    >
      <span className="sr-only">{filmstripVisible ? "Hide filmstrip" : "Show filmstrip"}</span>
      <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2} aria-hidden>
        {filmstripVisible ? (
          <path d="M4 6h16M4 10h16M4 14h16M4 18h16" />
        ) : (
          <><path d="M4 6h16M4 10h16M4 14h16M4 18h16" /><path d="M2 2l20 20" /></>
        )}
      </svg>
    </button>
  );
}

function CopyMarkdownButton() {
  const entries = usePmChatStore((s) => s.entries);
  const [copied, setCopied] = useState(false);

  const onCopy = useCallback(() => {
    if (!entries.length) return;
    const lines: string[] = [];
    for (const e of entries) {
      if (e.role === "user") {
        lines.push(buildUserMarkdown(e));
      } else if (e.role === "assistant") {
        const idx = entries.indexOf(e);
        lines.push(buildAssistantTurnMarkdown(entries, idx));
      } else if (e.role === "tool" || e.role === "file") {
        lines.push(`**${e.role}**: ${e.text || ""}`);
      } else if (e.role === "error") {
        lines.push(`**Error**: ${e.text || ""}`);
      } else if (e.text) {
        lines.push(e.text);
      }
      lines.push("");
    }
    const md = lines.join("\n").trim();
    void navigator.clipboard.writeText(md).then(
      () => {
        setCopied(true);
        setTimeout(() => setCopied(false), 1500);
      },
      () => {},
    );
  }, [entries]);

  if (!entries.length) return null;

  return (
    <button
      type="button"
      id="pm-copy-md"
      className={`pm-icon-btn${copied ? " text-emerald-600 dark:text-emerald-400" : ""}`}
      title={copied ? "Copied!" : "Copy transcript as Markdown"}
      onClick={onCopy}
    >
      <span className="sr-only">{copied ? "Copied!" : "Copy transcript as Markdown"}</span>
      {copied ? (
        <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2} aria-hidden>
          <path d="M20 6L9 17l-5-5" />
        </svg>
      ) : (
        <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2} aria-hidden>
          <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
          <polyline points="14 2 14 8 20 8" />
          <line x1="16" y1="13" x2="8" y2="13" />
          <line x1="16" y1="17" x2="8" y2="17" />
          <polyline points="10 9 9 9 8 9" />
        </svg>
      )}
    </button>
  );
}

function ChatToolbarIconActions() {
  const busy = usePmChatStore((s) => s.busy);
  const clearTranscript = usePmChatStore((s) => s.clearTranscript);
  const postStop = usePmChatStore((s) => s.postStop);
  const pastSessionsOpen = usePmChatStore((s) => s.pastSessionsOpen);
  const togglePastSessions = usePmChatStore((s) => s.togglePastSessions);

  const colorDark = useSyncExternalStore(subscribeColorScheme, getColorSchemeDark, () => false);

  return (
    <div className="flex shrink-0 flex-wrap items-center justify-end gap-x-1.5 gap-y-0.5">
      {hasWebProviderHost() ? (
        <button
          type="button"
          id="pm-toggle-filetree"
          className="pm-icon-btn"
          title={t("btnExplorerTip")}
          onClick={() => postHost({ kind: "toggleFileTree" })}
        >
          <span className="sr-only">{t("btnExplorer")}</span>
          <IconPanelExpand />
        </button>
      ) : null}
      <button
        type="button"
        id="pm-past-sessions"
        className={`pm-icon-btn${pastSessionsOpen ? " text-blue-600 dark:text-blue-300" : ""}`}
        title={pastSessionsOpen ? t("btnPastSessionsHide") : t("btnPastSessionsShow")}
        aria-pressed={pastSessionsOpen}
        onClick={() => togglePastSessions()}
      >
        <span className="sr-only">{pastSessionsOpen ? t("btnPastSessionsHide") : t("btnPastSessionsShow")}</span>
        <IconPastSessions />
      </button>
      <button
        type="button"
        id="pm-theme"
        className="pm-icon-btn"
        title={colorDark ? t("btnThemeToLight") : t("btnThemeToDark")}
        onClick={() => toggleColorScheme()}
      >
        <span className="sr-only">{colorDark ? t("btnThemeToLight") : t("btnThemeToDark")}</span>
        {colorDark ? <IconSun /> : <IconMoon />}
      </button>
      <button type="button" id="pm-settings" className="pm-icon-btn" title={t("btnProviderTip")} onClick={() => postHost({ kind: "settings" })}>
        <span className="sr-only">{t("btnProvider")}</span>
        <IconSettings />
      </button>
      <button type="button" id="pm-clear" className="pm-icon-btn" title={t("btnClearTip")} onClick={() => clearTranscript()}>
        <span className="sr-only">{t("btnClear")}</span>
        <IconTrash />
      </button>
      <button type="button" id="pm-stop" className={`pm-icon-btn${busy ? " text-red-500 dark:text-red-400" : ""}`} title={t("btnStop")} onClick={() => postStop()}>
        <span className="sr-only">{t("btnStop")}</span>
        <IconStop />
      </button>
      <ToolCallsToggleButton />
      <CopyMarkdownButton />
      <FilmstripToggleButton />
    </div>
  );
}

export function ChatToolbar({ auth, pixlwizAuthEnabled }: ChatToolbarProps) {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const folder = usePmChatStore((s) => s.folder);
  const selection = usePmChatStore((s) => s.selection);
  const selectionTotalBytes = usePmChatStore((s) => s.selectionTotalBytes);

  const folderFull = String(folder || "").trim();
  const folderBase = folderDisplayName(folderFull);
  const n = selection.length;
  const showContext = !!(folderBase || n > 0);
  const contextTitle = [folderFull || undefined, n > 0 ? t("chatToolbarSelectionSummary", { count: n, size: formatCompactByteSize(selectionTotalBytes) }) : undefined]
    .filter(Boolean)
    .join("\n");

  return (
    <div
      className="flex shrink-0 flex-wrap items-center gap-2 bg-slate-50/90 px-2 py-1 text-xs text-slate-600 dark:bg-surface-dark/95 dark:text-slate-400"
      role="region"
      aria-label={t("chatToolbarAria")}
    >
      <div className="flex min-w-0 flex-1 flex-wrap items-center gap-x-2 gap-y-1">
        {pixlwizAuthEnabled ? (
          <div className="flex min-w-0 shrink-0 items-center gap-2" role="status" aria-live="polite" title={t("pixlwizAuthLabel")}>
            <PixlwizStatusDot auth={auth} />
            <PixlwizCreditBadge />
          </div>
        ) : null}
        {pixlwizAuthEnabled && showContext ? (
          <span className="hidden h-4 w-px shrink-0 bg-slate-300 sm:block dark:bg-slate-600" role="separator" aria-hidden />
        ) : null}
        {showContext ? (
          <div
            className="flex min-w-0 flex-wrap items-center gap-x-1.5 text-[10px] text-slate-600 dark:text-slate-400"
            role="status"
            title={contextTitle || undefined}
          >
            {folderBase ? (
              <span className="max-w-[min(100%,14rem)] truncate font-medium text-slate-700 dark:text-slate-300">{folderBase}</span>
            ) : null}
            {folderBase && n > 0 ? <span className="shrink-0 text-slate-400 dark:text-slate-500" aria-hidden>
                ·
              </span> : null}
            {n > 0 ? (
              <span className="shrink-0 tabular-nums">
                {t("chatToolbarSelectionSummary", { count: n, size: formatCompactByteSize(selectionTotalBytes) })}
              </span>
            ) : null}
          </div>
        ) : !pixlwizAuthEnabled ? (
          <div className="min-w-0 flex-1" aria-hidden />
        ) : null}
      </div>
      <div className="flex min-w-0 shrink-0 items-center gap-x-2 gap-y-0.5">
        <ChatToolbarIconActions />
        {pixlwizAuthEnabled ? (
          <button
            type="button"
            id="pm-pixlwiz-refresh"
            className="pm-icon-btn shrink-0"
            title={t("pixlwizAuthRefresh")}
            aria-label={t("pixlwizAuthRefresh")}
            onClick={() => postHost({ kind: "pixlwizRefreshAuth" })}
          >
            <IconRefresh />
          </button>
        ) : null}
      </div>
    </div>
  );
}
