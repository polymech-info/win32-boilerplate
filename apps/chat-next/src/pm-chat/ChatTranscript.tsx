import type { KeyboardEvent, MouseEvent, ReactNode } from "react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { buildAssistantTurnMarkdown, buildUserMarkdown, folderDisplayName, formatChatRelativeTime } from "./chatTurnFormat";
import { llmUsageFooterText, type TurnLlmUsageAggregate } from "./llmUsageFormat";
import { hasWebProviderHost, postHost } from "./hostBridge";
import { t } from "./i18n.js";
import { linkifyLocalPathsInDom, renderAssistantMarkdownToHtml, rewriteLocalPath } from "./markdownSetup";
import type { ChatEntry, ChatEntryRole } from "./pmChatStore";
import { usePmChatStore } from "./pmChatStore";
import { IconStop } from "./svgIcons";
import { copyTextToClipboard } from "./textUtils";

function contextThumbSrc(path: string): string {
  if (/^(https?:|data:)/i.test(path)) return path;
  if (path.startsWith("/")) return path;
  return rewriteLocalPath(path);
}

function contextFileLabel(path: string): string {
  const parts = path.split(/[/\\]/).filter(Boolean);
  return parts[parts.length - 1] || path;
}

function isLikelyRasterImagePath(path: string): boolean {
  return /\.(jpe?g|png|gif|webp|avif|bmp|heic|heif)(\?.*)?$/i.test(contextFileLabel(path));
}

function UserMessageContextThumbs({
  paths,
  variant,
  ariaLabel,
}: {
  paths: string[];
  variant: "context" | "result";
  ariaLabel?: string;
}) {
  const openPathInternal = usePmChatStore((s) => s.openPathInternal);
  const openGeneratedPath = usePmChatStore((s) => s.openGeneratedPath);
  const openGeneratedPathFullscreen = usePmChatStore((s) => s.openGeneratedPathFullscreen);
  const openSelectionPathFullscreen = usePmChatStore((s) => s.openSelectionPathFullscreen);
  if (!paths.length) return null;

  const onActivate = (pathRaw: string, e: MouseEvent | KeyboardEvent) => {
    // Shift+click: open with default app
    if (e.shiftKey) {
      e.preventDefault();
      openGeneratedPath(pathRaw);
      return;
    }
    // Ctrl/Cmd+click: open internally in fullscreen viewer
    if (e.ctrlKey || e.metaKey) {
      e.preventDefault();
      if (variant === "result") {
        openGeneratedPathFullscreen(pathRaw, false);
      } else {
        openSelectionPathFullscreen(pathRaw, false);
      }
      return;
    }
    // Normal click: open in internal centre viewer
    e.preventDefault();
    openPathInternal(pathRaw);
  };

  const baseTitle = `${t("openGenImageInternalHint")}\n${t("openGenImageShiftHint")}\n${t("openGenImageFullscreenHint")}`;

  const label = ariaLabel ?? (variant === "result" ? t("userMessageResultsAria") : t("userMessageRefsAria"));

  return (
    <ul
      className="mt-2 flex max-w-full list-none flex-wrap gap-1.5 p-0"
      aria-label={label}
    >
      {paths.map((p) => {
        const name = contextFileLabel(p);
        const showImg = isLikelyRasterImagePath(p);
        const url = contextThumbSrc(p);
        return (
          <li key={p} className="m-0 p-0">
            <div
              role="button"
              tabIndex={0}
              title={p ? `${baseTitle}\n${p}`.trim() : baseTitle}
              className="relative h-12 w-12 shrink-0 cursor-pointer overflow-hidden rounded-md border border-slate-400/50 bg-slate-900/25 outline-none select-none dark:border-slate-500/50 dark:bg-slate-950/40"
              onClick={(e) => {
                e.preventDefault();
                onActivate(p, e);
              }}
              onKeyDown={(e) => {
                if (e.key === "Enter" || e.key === " ") {
                  e.preventDefault();
                  onActivate(p, e);
                }
              }}
            >
              {showImg ? (
                <img src={url} alt="" className="h-full w-full object-cover" draggable={false} style={{ imageRendering: 'auto' }} />
              ) : (
                <div className="flex h-full w-full items-center justify-center px-0.5 text-center text-[9px] leading-tight text-white/90">
                  {name.length > 14 ? `${name.slice(0, 12)}…` : name}
                </div>
              )}
            </div>
          </li>
        );
      })}
    </ul>
  );
}

function lastUserEntryId(entries: ChatEntry[]): number {
  for (let i = entries.length - 1; i >= 0; i -= 1) {
    if (entries[i].role === "user") return entries[i].id;
  }
  return -1;
}

/** User bubble: prefer usage stored on the user row; else legacy data on the assistant below until the next user. */
function llmUsageForUserDisplay(entries: ChatEntry[], userSourceIndex: number): TurnLlmUsageAggregate | undefined {
  const u = entries[userSourceIndex];
  if (!u || u.role !== "user") return undefined;
  if (u.llmUsage) return u.llmUsage;
  for (let i = userSourceIndex + 1; i < entries.length; i += 1) {
    if (entries[i].role === "user") break;
    if (entries[i].role === "assistant" && entries[i].llmUsage) return entries[i].llmUsage;
  }
  return undefined;
}

/** Assistant footer: prefer row usage; else usage saved only on the originating user (older sessions). */
function llmUsageForAssistantDisplay(
  entries: ChatEntry[],
  assistantSourceIndex: number,
  row: ChatEntry,
): TurnLlmUsageAggregate | undefined {
  if (row.llmUsage) return row.llmUsage;
  for (let j = assistantSourceIndex - 1; j >= 0; j -= 1) {
    if (entries[j].role === "user") return entries[j].llmUsage;
  }
  return undefined;
}

function avatarKind(role: ChatEntryRole): "user" | "bot" | "tool" | "error" | "system" {
  if (role === "user") return "user";
  if (role === "tool" || role === "file" || role === "shell") return "tool";
  if (role === "error") return "error";
  if (role === "system") return "system";
  return "bot";
}

function bubbleClass(role: ChatEntryRole): string {
  switch (role) {
    case "user":
      return "pm-bubble--user";
    case "assistant":
      return "pm-bubble--assistant";
    case "image":
      return "pm-bubble--image";
    case "tool":
      return "pm-bubble--tool";
    case "file":
      return "pm-bubble--tool";
    case "error":
      return "pm-bubble--error";
    case "system":
      return "pm-bubble--system";
    default:
      return "pm-bubble--system";
  }
}

function copyTone(role: ChatEntryRole): "user" | "neutral" | "tool" | "error" {
  if (role === "user") return "user";
  if (role === "tool" || role === "file" || role === "shell") return "tool";
  if (role === "error") return "error";
  return "neutral";
}

function BubbleAvatar({ kind }: { kind: ReturnType<typeof avatarKind> }) {
  const base = "pm-bubble-avatar";
  if (kind === "user") {
    return (
      <div className={`${base} pm-bubble-avatar--user`} aria-hidden>
        <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
          <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2" />
          <circle cx="12" cy="7" r="4" />
        </svg>
      </div>
    );
  }
  if (kind === "tool") {
    return (
      <div className={`${base} pm-bubble-avatar--tool`} aria-hidden>
        <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
          <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z" />
        </svg>
      </div>
    );
  }
  if (kind === "error") {
    return (
      <div className={`${base} pm-bubble-avatar--error`} aria-hidden>
        <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
          <circle cx="12" cy="12" r="10" />
          <path d="M12 8v4M12 16h.01" />
        </svg>
      </div>
    );
  }
  if (kind === "system") {
    return (
      <div className={`${base} pm-bubble-avatar--system`} aria-hidden>
        <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
          <circle cx="12" cy="12" r="10" />
          <path d="M12 16v-4M12 8h.01" />
        </svg>
      </div>
    );
  }
  return (
    <div className={`${base} pm-bubble-avatar--bot`} aria-hidden>
      <svg className="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
        <path d="M12 8V4H8" />
        <rect width="16" height="12" x="4" y="8" rx="2" />
        <path d="M2 14h2M20 14h2M15 13v2M9 13v2" />
      </svg>
    </div>
  );
}

function BubbleCopyButton({
  text,
  markdown,
  tone,
}: {
  text: string;
  /** When set, clipboard receives Markdown (full turn / user details). */
  markdown?: string;
  tone: "user" | "neutral" | "tool" | "error";
}) {
  const [copied, setCopied] = useState(false);
  const payload = markdown?.trim() ? markdown : text;
  const onCopy = useCallback(() => {
    if (!payload) return;
    void navigator.clipboard.writeText(payload).then(
      () => {
        setCopied(true);
        setTimeout(() => setCopied(false), 1500);
      },
      () => {},
    );
  }, [payload]);

  if (!payload) return null;

  const toneCls =
    tone === "user"
      ? "pm-bubble-copy--user"
      : tone === "tool"
        ? "pm-bubble-copy--tool"
        : tone === "error"
          ? "pm-bubble-copy--error"
          : "pm-bubble-copy--neutral";

  const tip = markdown?.trim() ? t("bubbleCopyMarkdownTip") : t("bubbleCopyTip");

  return (
    <button
      type="button"
      className={`pm-bubble-copy ${toneCls}${copied ? " pm-bubble-copy--ok" : ""}`}
      title={copied ? t("bubbleCopied") : tip}
      aria-label={copied ? t("bubbleCopied") : tip}
      onClick={onCopy}
    >
      {copied ? (
        <svg className="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
          <path d="M20 6L9 17l-5-5" />
        </svg>
      ) : (
        <svg className="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
          <rect width="14" height="14" x="8" y="8" rx="2" ry="2" />
          <path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2" />
        </svg>
      )}
    </button>
  );
}

function BubbleStopButton({ onStop }: { onStop: () => void }) {
  return (
    <button
      type="button"
      className="pm-bubble-stop"
      title={t("btnStop")}
      aria-label={t("btnStop")}
      onClick={(ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        onStop();
      }}
    >
      <IconStop />
    </button>
  );
}

type ImgRowProps = { pathRaw: string; /** Larger preview inside user bubble (vs standalone transcript image row). */ embedded?: boolean };
function GeneratedImageRow({ pathRaw, embedded }: ImgRowProps) {
  const openPathInternal = usePmChatStore((s) => s.openPathInternal);
  const openGeneratedPath = usePmChatStore((s) => s.openGeneratedPath);
  const openGeneratedPathFullscreen = usePmChatStore((s) => s.openGeneratedPathFullscreen);
  const url = rewriteLocalPath(pathRaw);
  const fname = pathRaw.split(/[\\/]/).pop() || "";
  const [failed, setFailed] = useState(false);

  const title = (
    pathRaw
      ? `${t("openGenImageInternalHint")}\n${t("openGenImageShiftHint")}\n${t("openGenImageFullscreenHint")}\n${pathRaw}`
      : `${t("openGenImageInternalHint")}\n${t("openGenImageShiftHint")}\n${t("openGenImageFullscreenHint")}`
  ).trim();

  const onActivate = (e: MouseEvent | KeyboardEvent) => {
    // Shift+click: open with default app
    if (e.shiftKey) {
      e.preventDefault();
      openGeneratedPath(pathRaw);
      return;
    }
    // Ctrl/Cmd+click: open internally in fullscreen viewer
    if (e.ctrlKey || e.metaKey) {
      e.preventDefault();
      openGeneratedPathFullscreen(pathRaw, false);
      return;
    }
    // Normal click: open in internal centre viewer
    e.preventDefault();
    openPathInternal(pathRaw);
  };

  const wrapCls = embedded ? "min-w-0 max-w-full" : "pm-body pm-md";
  const imgCls = embedded
    ? "max-h-[min(70vh,26rem)] w-full min-h-[10rem] rounded-lg border border-white/25 object-contain shadow-sm group-hover:ring-2 group-hover:ring-white/40 dark:border-slate-500/50 dark:group-hover:ring-slate-400/50"
    : "max-w-full rounded-lg border border-slate-200/80 object-contain dark:border-slate-600/80 group-hover:ring-2 group-hover:ring-slate-400/50";
  const fnameCls = embedded
    ? "mt-1.5 truncate text-xs text-white/80 dark:text-slate-300"
    : "mt-1 truncate text-xs text-slate-500 dark:text-slate-400";

  return (
    <div className={wrapCls}>
      <div
        className={`pm-open-generated-img group ${embedded ? "block w-full" : "inline-block max-w-full"} cursor-pointer select-none`}
        role="button"
        tabIndex={0}
        title={title}
        onClick={(e) => {
          e.preventDefault();
          onActivate(e);
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            onActivate(e);
          }
        }}
      >
        {!failed ? (
          <img className={imgCls} src={url} alt={fname} draggable={false} onError={() => setFailed(true)} style={{ imageRendering: 'auto' }} />
        ) : (
          <div className="mt-1 text-xs text-red-600 dark:text-red-400">
            {t("imgLoadErrorPrefix")}
            {url}
          </div>
        )}
        <div className={fnameCls} title={pathRaw}>
          {fname}
        </div>
      </div>
    </div>
  );
}

function bodyClassFor(role: ChatEntryRole): string {
  if (role === "tool" || role === "file" || role === "shell") return "pm-body-tool";
  if (role === "error") return "pm-body-error";
  if (role === "system") return "pm-body-system";
  return "";
}

/** Suffix written by ChatWebPanel when a path tool returns `replicate_web_url` (e.g. create_video). */
const REPLICATE_URL_SUFFIX = "\u2014 Replicate: ";

function toolTextWithOptionalReplicateLink(text: string): ReactNode {
  const i = text.indexOf(REPLICATE_URL_SUFFIX);
  if (i < 0) return text;
  const url = text.slice(i + REPLICATE_URL_SUFFIX.length).trim();
  if (!/^https:\/\/.+/u.test(url)) return text;
  return (
    <>
      {text.slice(0, i + REPLICATE_URL_SUFFIX.length)}
      <a
        href={url}
        target="_blank"
        rel="noopener noreferrer"
        className="break-all underline decoration-slate-400/80 underline-offset-2 hover:text-sky-600 dark:hover:text-sky-400"
      >
        {url}
      </a>
    </>
  );
}

const SHELL_CODE_CLASS_RE = /(?:^|\s)language-(?:bash|bat|cmd|console|powershell|ps1|pwsh|sh|shell|terminal|zsh)(?:\s|$)/i;

function isShellCodeBlock(code: HTMLElement): boolean {
  if (SHELL_CODE_CLASS_RE.test(code.className)) return true;
  const pre = code.closest("pre");
  const preClass = pre instanceof HTMLElement ? pre.className : "";
  return SHELL_CODE_CLASS_RE.test(preClass);
}

function postCodeToConsoleDraft(text: string): void {
  const code = text.replace(/\n$/, "");
  if (!code.trim()) return;
  postHost({
    t: "cweb_bus",
    payload: {
      t: "cweb_host",
      cmd: "pasteInConsole",
      text: code,
    },
  });
}

function addShellCodeActions(root: HTMLElement): void {
  root.querySelectorAll<HTMLElement>(".pm-md pre > code").forEach((code) => {
    if (!isShellCodeBlock(code)) return;
    const pre = code.closest("pre");
    if (!(pre instanceof HTMLElement)) return;
    pre.classList.add("pm-code-copy-wrap");

    if (!pre.querySelector(":scope > .pm-code-send-console")) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "pm-code-send-console";
      btn.title = "Stage in console";
      btn.setAttribute("aria-label", "Stage command in console");
      btn.dataset.codeAction = "console";
      btn.innerHTML =
        '<svg class="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">' +
        '<path d="m7 8 4 4-4 4"></path>' +
        '<path d="M13 17h4"></path>' +
        '<path d="M4 5h16v14H4z"></path>' +
        "</svg>";
      pre.appendChild(btn);
    }

    if (!pre.querySelector(":scope > .pm-code-copy")) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "pm-code-copy";
      btn.title = "Copy command";
      btn.setAttribute("aria-label", "Copy command");
      btn.dataset.copyCode = "shell";
      btn.innerHTML =
        '<svg class="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">' +
        '<rect width="14" height="14" x="8" y="8" rx="2" ry="2"></rect>' +
        '<path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"></path>' +
        "</svg>";
      pre.appendChild(btn);
    }
  });
}

type DisplayRow = { entry: ChatEntry; sourceIndex: number; embeddedTools?: ChatEntry[]; folderHint?: string };

function buildDisplayRows(entries: ChatEntry[]): DisplayRow[] {
  // Tool calls are always embedded in user bubbles, never shown as standalone.
  // folderHint is propagated from each user entry to all subsequent rows in the same turn
  // so bare filenames in assistant/tool text can be resolved to absolute paths.
  const out: DisplayRow[] = [];
  let i = 0;
  let activeFolderHint: string | undefined;
  while (i < entries.length) {
    const e = entries[i];
    if (e.role === "tool" || e.role === "file") {
      i += 1;
      continue;
    }
    if (e.role === "user") {
      activeFolderHint = e.folderHint?.trim() || activeFolderHint;
      const embedded: ChatEntry[] = [];
      // Collect mid-turn rows that sit between the user prompt and the final
      // assistant reply.  System entries (setRunFolder 📂) are absorbed for their
      // folderHint; shell entries (run tool output) become standalone rows; tool/file
      // entries are embedded in the user bubble.
      const midTurnRows: DisplayRow[] = [];
      let j = i + 1;
      while (j < entries.length) {
        const r = entries[j].role;
        if (r === "tool" || r === "file") {
          embedded.push(entries[j]);
          j += 1;
        } else if (r === "system") {
          if (entries[j].folderHint?.trim()) activeFolderHint = entries[j].folderHint!.trim();
          j += 1;
        } else if (r === "shell") {
          midTurnRows.push({ entry: entries[j], sourceIndex: j, folderHint: activeFolderHint });
          j += 1;
        } else {
          break;
        }
      }
      out.push({
        entry: e,
        sourceIndex: i,
        embeddedTools: embedded.length ? embedded : undefined,
        folderHint: activeFolderHint,
      });
      for (const mr of midTurnRows) out.push(mr);
      i = j;
    } else {
      out.push({ entry: e, sourceIndex: i, folderHint: activeFolderHint });
      i += 1;
    }
  }
  return out;
}

function ShellOutputBubble({ entry }: { entry: ChatEntry }) {
  const [open, setOpen] = useState(false);
  const [copied, setCopied] = useState(false);
  const postStop = usePmChatStore((s) => s.postStop);
  const out = entry.shellOutput;
  const cmd = entry.runCommand || "";
  const active = !!entry.runActive;
  const exitCode = entry.runExitCode;
  const durMs = entry.runDurationMs;

  const hasOutput = out && (out.stdout + out.stderr).trim().length > 0;
  const hasStderr = out && !!out.stderr.trim();
  const toggleOpen = () => {
    if (hasOutput) setOpen((v) => !v);
  };

  const handleCopy = (e: MouseEvent) => {
    e.stopPropagation();
    if (!cmd) return;
    navigator.clipboard.writeText(cmd).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    });
  };

  const statusBadge = active
    ? null
    : exitCode !== undefined
      ? exitCode === 0
        ? <span className="ml-2 text-[10px] text-green-400/80">exit 0</span>
        : <span className="ml-2 text-[10px] text-red-400/80">exit {exitCode}</span>
      : null;

  const durationBadge = !active && durMs != null && durMs > 0
    ? <span className="ml-1.5 text-[10px] text-slate-500">{durMs < 1000 ? `${durMs}ms` : `${(durMs / 1000).toFixed(1)}s`}</span>
    : null;

  return (
    <div className="rounded border border-slate-700/60 bg-slate-900/80 overflow-hidden">
      {/* Header — always visible */}
      <div
        role="button"
        tabIndex={0}
        onClick={toggleOpen}
        onKeyDown={(ev) => {
          if (ev.key === "Enter" || ev.key === " ") {
            ev.preventDefault();
            toggleOpen();
          }
        }}
        className="flex w-full items-center gap-2 px-3 py-1.5 text-left hover:bg-slate-800/60 transition-colors"
      >
        {/* Expand chevron */}
        <span className={`text-[10px] text-slate-500 transition-transform ${open ? "rotate-90" : ""} ${!hasOutput ? "opacity-30" : ""}`}>
          ▶
        </span>

        {/* Spinner when active */}
        {active && (
          <span className="inline-block h-3 w-3 animate-spin rounded-full border-2 border-slate-500 border-t-blue-400 flex-shrink-0" />
        )}

        {/* Command text */}
        <code className="flex-1 truncate font-mono text-[11px] text-slate-300">
          {cmd || "(shell)"}
        </code>

        {statusBadge}
        {durationBadge}

        {active ? <BubbleStopButton onStop={postStop} /> : null}

        {/* Copy button */}
        {cmd && (
          <button
            type="button"
            onClick={(ev) => { ev.stopPropagation(); handleCopy(ev); }}
            className="ml-1 flex-shrink-0 rounded p-0.5 text-slate-500 hover:bg-slate-700 hover:text-slate-300 transition-colors"
            title={copied ? "Copied" : "Copy command"}
          >
            {copied ? (
              <svg className="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
                <path d="M20 6L9 17l-5-5" />
              </svg>
            ) : (
              <svg className="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
                <rect width="14" height="14" x="8" y="8" rx="2" ry="2" />
                <path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2" />
              </svg>
            )}
          </button>
        )}
      </div>

      {/* Collapsible output body */}
      {open && hasOutput && out && (
        <pre className="max-h-60 overflow-y-auto pm-scroll whitespace-pre-wrap break-all border-t border-slate-700/40 px-3 py-2 font-mono text-[11px] leading-snug text-green-300">
          {out.stdout}
          {hasStderr ? (
            <span className="text-red-400">{out.stderr}</span>
          ) : null}
        </pre>
      )}
    </div>
  );
}

function TranscriptRow({
  e,
  busy,
  lastUserId,
  i18nRev,
  entries,
  sourceIndex,
  embeddedTools,
  folderHint,
  nowMs,
}: {
  e: ChatEntry;
  busy: boolean;
  lastUserId: number;
  i18nRev: number;
  entries: ChatEntry[];
  sourceIndex: number;
  embeddedTools?: ChatEntry[];
  folderHint?: string;
  nowMs: number;
}) {
  void i18nRev;
  const kind = avatarKind(e.role);
  const bubbleCls = bubbleClass(e.role);
  const isUser = e.role === "user";
  const copyText = e.text || "";
  const showCopy =
    e.role !== "tool" && e.role !== "file" && (copyText.length > 0 || e.role === "user" || e.role === "assistant");
  const showCopyTool = (e.role === "tool" || e.role === "file") && copyText.length > 0;
  const mdUser = e.role === "user" ? buildUserMarkdown(e) : "";
  const mdAssistant = e.role === "assistant" ? buildAssistantTurnMarkdown(entries, sourceIndex) : "";

  const userUsageHeader = e.role === "user" ? llmUsageForUserDisplay(entries, sourceIndex) : undefined;
  const showMetaTime = typeof e.ts === "number" && Number.isFinite(e.ts);
  const metaContent =
    showMetaTime || userUsageHeader ? (
      <div
        className={
          "pm-bubble-meta flex-1 text-[10px] leading-tight tabular-nums" +
          (e.role === "user" ? " text-right" : "")
        }
      >
        {showMetaTime ? <span>{formatChatRelativeTime(e.ts!, nowMs)}</span> : null}
        {userUsageHeader ? (
          <span
            className={
              (showMetaTime ? "ml-2 " : "") +
              "text-[9px] leading-tight text-slate-500 dark:text-slate-400"
            }
            title={llmUsageFooterText(userUsageHeader)}
          >
            {llmUsageFooterText(userUsageHeader)}
          </span>
        ) : null}
      </div>
    ) : null;

  const bubbleRef = useRef<HTMLDivElement>(null);

  const openPathInternal = usePmChatStore((s) => s.openPathInternal);
  const openGeneratedPath = usePmChatStore((s) => s.openGeneratedPath);
  const openGeneratedPathFullscreen = usePmChatStore((s) => s.openGeneratedPathFullscreen);

  // Re-run linkification on the whole bubble whenever content changes
  // (covers both assistant markdown and embedded tool-call <pre> blocks)
  const linkifyKey = e.text || "";
  useEffect(() => {
    const el = bubbleRef.current;
    if (!el) {
      console.log("[pm-chat] linkify: bubbleRef.current is null, skipping");
      return;
    }
    console.log("[pm-chat] linkify: running on bubble id=", e.id, "role=", e.role, "folderHint=", folderHint, "text length=", linkifyKey.length);
    linkifyLocalPathsInDom(el, folderHint);
  }, [linkifyKey, e.id, e.role, folderHint]);

  useEffect(() => {
    const el = bubbleRef.current;
    if (!el || e.role !== "assistant") return;
    addShellCodeActions(el);

    const handler = (ev: globalThis.MouseEvent) => {
      const target = ev.target as HTMLElement | null;
      const consoleBtn = target?.closest("button.pm-code-send-console") as HTMLButtonElement | null;
      if (consoleBtn && el.contains(consoleBtn)) {
        ev.preventDefault();
        ev.stopPropagation();
        const pre = consoleBtn.closest("pre");
        const code = pre?.querySelector("code");
        postCodeToConsoleDraft(code?.textContent ?? "");
        return;
      }

      const btn = target?.closest("button.pm-code-copy") as HTMLButtonElement | null;
      if (!btn || !el.contains(btn)) return;
      ev.preventDefault();
      ev.stopPropagation();

      const pre = btn.closest("pre");
      const code = pre?.querySelector("code");
      const text = code?.textContent ?? "";
      if (!text.trim()) return;

      void copyTextToClipboard(text).then(() => {
        btn.classList.add("pm-code-copy--ok");
        btn.title = "Copied";
        btn.setAttribute("aria-label", "Copied");
        btn.innerHTML =
          '<svg class="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">' +
          '<path d="M20 6L9 17l-5-5"></path>' +
          "</svg>";
        window.setTimeout(() => {
          if (!btn.isConnected) return;
          btn.classList.remove("pm-code-copy--ok");
          btn.title = "Copy command";
          btn.setAttribute("aria-label", "Copy command");
          btn.innerHTML =
            '<svg class="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">' +
            '<rect width="14" height="14" x="8" y="8" rx="2" ry="2"></rect>' +
            '<path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"></path>' +
            "</svg>";
        }, 1500);
      });
    };

    el.addEventListener("click", handler);
    return () => el.removeEventListener("click", handler);
  }, [e.id, e.role, linkifyKey]);

  useEffect(() => {
    const el = bubbleRef.current;
    if (!el) return;
    const handler = (ev: globalThis.MouseEvent) => {
      const target = ev.target as HTMLElement;
      const anchor = target.closest("a.pm-local-link") as HTMLAnchorElement | null;
      if (!anchor) return;
      // The generated-image widget handles its own clicks (full pathRaw via onActivate).
      // If the linkified caption text inside it is clicked, skip here to avoid a
      // duplicate openPathInternal call with just the bare filename.
      if (anchor.closest(".pm-open-generated-img")) return;
      ev.preventDefault();
      const encoded = anchor.getAttribute("data-path");
      if (!encoded) return;
      let path = decodeURIComponent(encoded);
      if (!path) return;
      // Resolve bare filenames (no path separator) against folderHint at click
      // time as a fallback — the linkifier already does this at render time but
      // folderHint may not have been available then, or the handler dep was stale.
      if (folderHint && !path.includes("\\") && !path.includes("/")) {
        path = folderHint.replace(/[\\/]+$/, "") + "\\" + path;
      }
      console.log("[pm-chat] local-link click:", { path, shift: ev.shiftKey, ctrl: ev.ctrlKey });
      if (ev.shiftKey) {
        openGeneratedPath(path);
      } else if (ev.ctrlKey || ev.metaKey) {
        openGeneratedPathFullscreen(path, false);
      } else {
        openPathInternal(path);
      }
    };
    el.addEventListener("click", handler);
    return () => el.removeEventListener("click", handler);
  }, [openPathInternal, openGeneratedPath, openGeneratedPathFullscreen, folderHint, e.id]);

  const showSpinner = isUser && busy && e.id === lastUserId;
  const postStop = usePmChatStore((s) => s.postStop);

  let body: ReactNode;
  if (e.role === "assistant") {
    const html = renderAssistantMarkdownToHtml(e.text || "");
    const asstUsage = llmUsageForAssistantDisplay(entries, sourceIndex, e);
    body = (
      <div className="min-w-0">
        <div className="pm-body pm-md" dangerouslySetInnerHTML={{ __html: html }} />
        {asstUsage ? (
          <div
            className="mt-1.5 border-t border-slate-200/80 pt-1 text-[9px] leading-snug text-slate-500 tabular-nums select-none dark:border-slate-600/45 dark:text-slate-400"
            title={llmUsageFooterText(asstUsage)}
          >
            {llmUsageFooterText(asstUsage)}
          </div>
        ) : null}
      </div>
    );
  } else if (e.role === "image") {
    body = (
      <div>
        <GeneratedImageRow pathRaw={e.text || ""} />
      </div>
    );
  } else if (e.role === "shell") {
    body = <ShellOutputBubble entry={e} />;
  } else if (e.role === "user") {
    const refs = e.contextPaths?.filter((x) => x.trim()) ?? [];
    const outs = e.resultPaths?.filter((x) => x.trim()) ?? [];
    const fh = e.folderHint?.trim();
    const openFolder = () => {
      if (!fh) return;
      if (hasWebProviderHost()) postHost({ kind: "openFolderInExplorer", path: fh });
    };
    body = (
      <div className="flex min-w-0 flex-col gap-0">
        <div className="flex min-w-0 items-start gap-2">
          <div className="pm-body pm-body-user min-w-0 flex-1 whitespace-pre-wrap">{e.text}</div>
        </div>
        {refs.length ? <UserMessageContextThumbs paths={refs} variant="context" /> : null}
        {embeddedTools?.length ? (
          <div className="mt-2 space-y-1.5 border-t border-slate-400/30 pt-2 dark:border-white/10">
            <div className="text-[11px] font-semibold uppercase tracking-wide text-slate-600 dark:text-white/70">{t("toolCallsInlineLabel")}</div>
            {embeddedTools.map((te) => (
              <pre
                key={te.id}
                className="max-h-40 overflow-y-auto pm-scroll whitespace-pre-wrap break-words rounded bg-slate-200/60 px-2 py-1.5 font-mono text-[11px] leading-snug text-slate-800 dark:bg-black/25 dark:text-slate-100"
              >
                {te.text}
              </pre>
            ))}
          </div>
        ) : null}
        {outs.length ? <UserMessageContextThumbs paths={outs} variant="result" /> : null}
        {fh ? (
          <button
            type="button"
            className="mt-1.5 inline-flex max-w-full items-center self-start rounded-full border border-slate-300 bg-slate-200/50 px-2 py-0.5 text-left text-[9px] font-medium text-slate-700 hover:bg-slate-200 disabled:cursor-not-allowed disabled:opacity-50 dark:border-white/15 dark:bg-white/5 dark:text-white"
            title={`${t("folderChipExplorerTip")}: ${fh}`}
            disabled={!hasWebProviderHost()}
            onClick={openFolder}
          >
            {folderDisplayName(fh)}
          </button>
        ) : null}
      </div>
    );
  } else {
    const cls = bodyClassFor(e.role);
    const toolBody =
      e.role === "tool" && (e.text || "").includes(REPLICATE_URL_SUFFIX)
        ? toolTextWithOptionalReplicateLink(e.text || "")
        : e.text;
    body = (
      <div className={`pm-body ${cls}`}>
        {toolBody}
      </div>
    );
  }

  const deleteEntry = usePmChatStore((s) => s.deleteEntry);

  return (
    <div className={`pm-bubble-row${isUser ? " pm-bubble-row--user" : ""}`} data-id={e.id} data-role={e.role}>
      <BubbleAvatar kind={kind} />
      <div ref={bubbleRef} className={`pm-bubble group ${bubbleCls}${showSpinner ? " pm-bubble--working" : ""}`}>
        {/* Header row: timestamp + buttons on one line */}
        <div className="mb-1 flex min-w-0 items-center justify-between gap-1">
          <div className="min-w-0 flex-1">
            {metaContent}
          </div>
          <div className="flex flex-shrink-0 items-center gap-0.5">
            {showSpinner ? (
              <span
                className="inline-block h-3.5 w-3.5 animate-spin rounded-full border-2 border-blue-300/60 border-t-blue-500 dark:border-blue-500/40 dark:border-t-blue-400"
                role="status"
                aria-label={t("ariaWorking")}
              />
            ) : null}
            {showSpinner ? <BubbleStopButton onStop={postStop} /> : null}
            {showCopy ? (
              <BubbleCopyButton
                text={copyText}
                markdown={(e.role === "user" ? mdUser : e.role === "assistant" ? mdAssistant : "") || undefined}
                tone={copyTone(e.role)}
              />
            ) : null}
            {showCopyTool ? <BubbleCopyButton text={copyText} tone="tool" /> : null}
            <button
              type="button"
              className="pm-bubble-delete"
              title="Delete message"
              onClick={() => deleteEntry(e.id)}
            >
              <svg className="h-3 w-3" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
                <path d="M18 6L6 18M6 6l12 12" />
              </svg>
            </button>
          </div>
        </div>
        {body}
      </div>
    </div>
  );
}

export function ChatTranscript() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  const entries = usePmChatStore((s) => s.entries);
  const showToolCalls = usePmChatStore((s) => s.showToolCalls);
  const busy = usePmChatStore((s) => s.busy);
  const ref = useRef<HTMLDivElement>(null);
  const lastUserId = useMemo(() => lastUserEntryId(entries), [entries]);
  const [nowMs, setNowMs] = useState(() => Date.now());

  useEffect(() => {
    setNowMs(Date.now());
  }, [entries, i18nRev]);

  useEffect(() => {
    const id = window.setInterval(() => setNowMs(Date.now()), 30_000);
    return () => clearInterval(id);
  }, []);

  const displayRows = useMemo(() => buildDisplayRows(entries), [entries]);

  useEffect(() => {
    const el = ref.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [displayRows, busy]);

  const empty = displayRows.length === 0;

  if (empty) {
    return (
      <div
        id="pm-transcript"
        ref={ref}
        className="pm-scroll flex min-h-0 min-w-0 flex-1 flex-col overflow-y-auto"
        role="log"
        aria-label={t("transcriptAria")}
      >
        {/* flex-1 spacer so the scroll pane consumes column space; composer stays at column bottom */}
        <div className="min-h-0 min-w-0 flex-1" aria-hidden />
      </div>
    );
  }

  return (
    <div
      id="pm-transcript"
      ref={ref}
      className="pm-scroll flex min-h-0 min-w-0 flex-1 flex-col overflow-y-auto"
      role="log"
      aria-label={t("transcriptAria")}
    >
      <div className="flex min-h-0 min-w-0 flex-col gap-2 sm:gap-3">
        {displayRows.map((row) => (
          <TranscriptRow
            key={`${row.entry.id}-${row.sourceIndex}-${row.embeddedTools?.length ?? 0}`}
            e={row.entry}
            busy={busy}
            lastUserId={lastUserId}
            i18nRev={i18nRev}
            entries={entries}
            sourceIndex={row.sourceIndex}
            embeddedTools={showToolCalls ? row.embeddedTools : undefined}
            folderHint={row.folderHint}
            nowMs={nowMs}
          />
        ))}
      </div>
    </div>
  );
}
