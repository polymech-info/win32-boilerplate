import { useEffect, useLayoutEffect, useState } from "react";

import { DEMO_SESSION_ID } from "@/chat/demoSession";

import { persistChatSessionSnapshot, loadChatSessionFull } from "./chatSessionsBackend";
import { installPolyMechHostApi, normalizePixlwizAuthPayload, type PixlwizAuthPayload } from "@pm/shared/web/hostBridge";
import {
  attachWebviewMessageBridge,
  cancelComposerReadyRetry,
  hasWebProviderHost,
  postComposerReadyToHost,
  postHost,
} from "./hostBridge";
import { ensureMarkdownConfigured } from "./markdownSetup";
import { isTestStripDevActive, loadTestStripPaths } from "./testStripDev";
import { usePmChatStore, type McpCatalogPayload } from "./pmChatStore";
import { ChatComposerDock } from "./ChatComposerDock";
import { ChatTranscript } from "./ChatTranscript";
import { PastSessionsSidebar } from "./PastSessionsSidebar";
import { DesignPresetsModal } from "./DesignPresetsModal";
import { ResizePresetsModal } from "./ResizePresetsModal";
import { QuickActionsModal } from "./QuickActionsModal";
import { SelectionFilmStrip } from "./SelectionFilmStrip";
import { ChatToolbar } from "./ChatToolbar";
import { AgentFlowPane } from "./AgentFlowPane";
import { ToolOutputStrip } from "./ToolOutputStrip";
import { t } from "./i18n.js";
import { IconSettings } from "./svgIcons";

type ChatTab = "chat" | "log";

export type PmImageChatComposerProps = {
  sessionId: string;
};

let launchMicAutoStartPosted = false;
let launchMicAutoStartTimer: ReturnType<typeof setInterval> | null = null;

function optionHasValue(value: unknown, expected: string): boolean {
  if (Array.isArray(value)) return value.some((item) => optionHasValue(item, expected));
  return String(value ?? "").toLowerCase() === expected;
}

function shouldAutoStartMicFromArgs(): boolean {
  try {
    return optionHasValue(installPolyMechHostApi().args().options.mic, "start");
  } catch {
    return false;
  }
}

function readLaunchPromptFromArgs(): string {
  try {
    const prompt = installPolyMechHostApi().args().options["prompt"];
    if (typeof prompt === "string" && prompt.trim()) return prompt;
    if (Array.isArray(prompt)) {
      const last = prompt.filter((item): item is string => typeof item === "string" && item.trim().length > 0).pop();
      if (last) return last;
    }
  } catch {
    // no host / no argv — dev / browser mode
  }
  return "";
}

function applyLaunchPromptToComposer(): void {
  const text = readLaunchPromptFromArgs();
  if (text) usePmChatStore.getState().setInputDraft(text);
}

function postLaunchMicAutoStart(): void {
  if (launchMicAutoStartPosted || !shouldAutoStartMicFromArgs()) return;
  if (hasWebProviderHost()) {
    usePmChatStore.getState().setSttRecording(true);
    postHost({ kind: "sttStart" });
    launchMicAutoStartPosted = true;
    if (launchMicAutoStartTimer !== null) {
      clearInterval(launchMicAutoStartTimer);
      launchMicAutoStartTimer = null;
    }
    return;
  }

  if (launchMicAutoStartTimer !== null) return;
  const deadline = Date.now() + 12_000;
  launchMicAutoStartTimer = setInterval(() => {
    if (launchMicAutoStartPosted || Date.now() > deadline) {
      if (launchMicAutoStartTimer !== null) {
        clearInterval(launchMicAutoStartTimer);
        launchMicAutoStartTimer = null;
      }
      return;
    }
    postLaunchMicAutoStart();
  }, 50);
}

function cancelLaunchMicAutoStartRetry(): void {
  if (launchMicAutoStartTimer !== null) {
    clearInterval(launchMicAutoStartTimer);
    launchMicAutoStartTimer = null;
  }
}

/**
 * Full pm-image chat shell: host bridge, RPC, i18n, transcript, quick tools, composer.
 * Mirrors `ref/main.js` behaviour for WebView2 + Vite dev (mock RPC when no webview).
 */
export function PmImageChatComposer({ sessionId }: PmImageChatComposerProps) {
  const [pixlwizAuth, setPixlwizAuth] = useState<PixlwizAuthPayload | null>(null);
  const [activeTab, setActiveTab] = useState<ChatTab>("chat");
  const embedFeatures = usePmChatStore((s) => s.embedFeatures);
  const showQuickActionEditor = usePmChatStore((s) => s.showQuickActionEditor);
  const showDesignPresetEditor = usePmChatStore((s) => s.showDesignPresetEditor);
  const showResizePresetEditor = usePmChatStore((s) => s.showResizePresetEditor);
  const filmstripVisible = usePmChatStore((s) => s.filmstripVisible);
  const entries = usePmChatStore((s) => s.entries);
  const pastSessionsOpen = usePmChatStore((s) => s.pastSessionsOpen);
  const togglePastSessions = usePmChatStore((s) => s.togglePastSessions);
  const hasAgentFlow = usePmChatStore((s) => s.entries.some((e) => e.role === "user"));
  const [sidebarOverlayOpen, setSidebarOverlayOpen] = useState(false);

  useEffect(() => {
    ensureMarkdownConfigured();
  }, []);

  /** Before paint / user input: match store id to prop so `send()` can persist immediately. */
  useLayoutEffect(() => {
    usePmChatStore.getState().setActiveSessionId(sessionId);
  }, [sessionId]);

  useEffect(() => {
    const detach = attachWebviewMessageBridge({
      onHostChatWeb: (doc) => {
        usePmChatStore.getState().applyHostPersistence(doc);
      },
      onHostPixlwizAuth: (o) => {
        setPixlwizAuth(normalizePixlwizAuthPayload(o));
      },
      onHostPixlwizCredit: (c) => {
        usePmChatStore.getState().setPixlwizCredit({ spend: c.spend ?? 0, max: c.max_budget ?? null });
      },
    });

    const api = {
      appendText: (o: {
        role?: string; text?: string;
        toolProvider?: string; toolModel?: string; durationMs?: number;
      }) =>
        usePmChatStore.getState().appendText({
          role: o.role as never,
          text: o.text,
          toolProvider: o.toolProvider,
          toolModel: o.toolModel,
          durationMs: o.durationMs,
        }),
      setTurnLlmUsage: (raw: unknown) => usePmChatStore.getState().setTurnLlmUsage(raw),
      appendUser: (text: string) => usePmChatStore.getState().appendUser(text),
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
      }) => {
        installPolyMechHostApi().setStatus(o);
        usePmChatStore.getState().setStatus(o);
      },
      setBusy: (busy: boolean) => usePmChatStore.getState().setBusy(busy),
      setLocale: (code: string) => usePmChatStore.getState().setLocale(code),
      clear: () => usePmChatStore.getState().clear(),
      setFontExtraPt: (n: number) => usePmChatStore.getState().setFontExtraPt(n),
      applyHostPersistence: (doc: unknown) => usePmChatStore.getState().applyHostPersistence(doc),
      setAgentJson: (text: string | null) => usePmChatStore.getState().setAgentJson(text),
      // STT callbacks (fired by ChatWebPanel.cpp via RunJsNow)
      sttReady: () => usePmChatStore.getState().setSttRecording(true),
      sttLevel: (level: number) => usePmChatStore.getState().setSttLevel(level),
      sttPartial: (text: string) => usePmChatStore.getState().setSttPartial(text),
      sttCommitted: (text: string) => usePmChatStore.getState().appendSttCommitted(text),
      sttError: (msg: string) => {
        usePmChatStore.getState().setSttRecording(false);
        console.error("STT error:", msg);
      },
      sttDone: () => usePmChatStore.getState().setSttRecording(false),
      setRunFolder: (folder: string) =>
        usePmChatStore.getState().setRunFolder(folder),
      startRun: (runId: string, command: string) =>
        usePmChatStore.getState().startRun(runId, command),
      appendRunChunk: (runId: string, chunk: string, stream: "stdout" | "stderr") =>
        usePmChatStore.getState().appendRunChunk(runId, chunk, stream),
      finishRun: (runId: string, exitCode: number, durationMs: number) =>
        usePmChatStore.getState().finishRun(runId, exitCode, durationMs),
      // TTS speak callbacks (fired by ChatWebPanel.cpp EventCallback)
      ttsStarted: () => usePmChatStore.getState().setTtsPlaying(true),
      ttsDone:    () => usePmChatStore.getState().setTtsPlaying(false),
      // Video snapshot callbacks (fired by ChatWebPanel.cpp via RunJsNow)
      videoSnapshotDone: (path: string) => {
        usePmChatStore.getState().setVideoSnapBusy(false);
        postHost({ kind: "addContextPaths", paths: [path] });
      },
      videoSnapshotError: (msg: string) => {
        usePmChatStore.getState().setVideoSnapBusy(false);
        console.error("webcam snapshot error:", msg);
      },
      /** Pre-fill the composer text area (called externally or from --prompt argv). */
      setDraftText: (text: string) => usePmChatStore.getState().setInputDraft(text),
    };
    installPolyMechHostApi().chat = api;
    window.pmChat = api;

    postComposerReadyToHost();
    postLaunchMicAutoStart();

    return () => {
      detach();
      cancelComposerReadyRetry();
      cancelLaunchMicAutoStartRetry();
      if (window.pm?.chat === api) delete window.pm.chat;
      if (window.pmChat === api) delete window.pmChat;
    };
  }, []);

  useEffect(() => {
    let cancelled = false;
    const st = usePmChatStore.getState();
    st.setActiveSessionId(sessionId);
    st.resetSessionSurface();
    // After resetSessionSurface clears inputDraft, re-apply --prompt from pm.args().
    applyLaunchPromptToComposer();
    void (async () => {
      const loaded = await loadChatSessionFull(sessionId);
      if (cancelled) return;
      if (usePmChatStore.getState().activeSessionId !== sessionId) return;
      if (loaded?.entries?.length) {
        st.applyLoadedSession(loaded);
      } else if (!hasWebProviderHost() && sessionId === DEMO_SESSION_ID) {
        st.injectDevSampleTranscript();
      }
      if (hasWebProviderHost() && usePmChatStore.getState().embedFeatures.pixlwizAuth)
        postHost({ kind: "pixlwizRefreshAuth" });
      if (cancelled) return;
      await st.bootstrapProviderUi();
      if (cancelled) return;
      if (usePmChatStore.getState().activeSessionId !== sessionId) return;
    })();
    return () => {
      cancelled = true;
    };
  }, [sessionId]);

  /** Dev: `?TEST_STRIP` or `VITE_TEST_STRIP=1` — loads paths from `public/test-strip.manifest.json`. */
  useEffect(() => {
    if (!isTestStripDevActive()) return;
    let cancelled = false;
    void (async () => {
      const paths = await loadTestStripPaths();
      if (cancelled || !paths.length) return;
      usePmChatStore.getState().setStatus({ selection: paths });
    })();
    return () => {
      cancelled = true;
    };
  }, [sessionId]);

  // Reset tab when switching to a different session
  useEffect(() => {
    setActiveTab("chat");
  }, [sessionId]);

  useEffect(() => {
    const t = setTimeout(() => {
      void persistChatSessionSnapshot(sessionId, usePmChatStore.getState().entries);
    }, 700);
    return () => clearTimeout(t);
  }, [entries, sessionId]);

  return (
    <div className="relative flex h-full min-h-0 flex-1 flex-row items-stretch bg-white dark:bg-surface-dark">
      {pastSessionsOpen ? <PastSessionsSidebar currentSessionId={sessionId} /> : null}
      {!pastSessionsOpen ? (
        <div
          className="pm-sidebar-peek"
          data-open={sidebarOverlayOpen ? "true" : "false"}
          onMouseLeave={() => setSidebarOverlayOpen(false)}
          onFocus={() => setSidebarOverlayOpen(true)}
          onBlur={(event) => {
            const nextFocus = event.relatedTarget;
            if (!(nextFocus instanceof Node) || !event.currentTarget.contains(nextFocus)) {
              setSidebarOverlayOpen(false);
            }
          }}
        >
          <button
            type="button"
            className="pm-sidebar-peek__trigger"
            title={t("btnPastSessionsShow")}
            aria-label={t("btnPastSessionsShow")}
            aria-expanded={sidebarOverlayOpen}
            onMouseEnter={() => setSidebarOverlayOpen(true)}
            onClick={() => togglePastSessions()}
          >
            <span className="pm-sidebar-peek__gear" aria-hidden>
              <IconSettings />
            </span>
          </button>
          <div className="pm-sidebar-peek__panel">
            <PastSessionsSidebar
              currentSessionId={sessionId}
              variant="overlay"
              onPresetApplied={() => setSidebarOverlayOpen(false)}
            />
          </div>
        </div>
      ) : null}
      <div className="flex min-h-0 min-w-0 flex-1 flex-col overflow-hidden">
        {/* Tab strip — only shown above the transcript/log area */}
        <div className="flex shrink-0 items-center gap-0 border-b border-slate-200/80 px-2 dark:border-slate-700/60">
          <button
            type="button"
            onClick={() => setActiveTab("chat")}
            className={`relative px-3 py-1.5 text-xs font-medium transition-colors ${
              activeTab === "chat"
                ? "text-slate-900 dark:text-slate-100 after:absolute after:inset-x-0 after:bottom-0 after:h-0.5 after:rounded-t after:bg-slate-700 dark:after:bg-slate-300"
                : "text-slate-500 hover:text-slate-700 dark:text-slate-400 dark:hover:text-slate-200"
            }`}
          >
            Chat
          </button>
          <button
            type="button"
            onClick={() => setActiveTab("log")}
            className={`relative flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium transition-colors ${
              activeTab === "log"
                ? "text-slate-900 dark:text-slate-100 after:absolute after:inset-x-0 after:bottom-0 after:h-0.5 after:rounded-t after:bg-slate-700 dark:after:bg-slate-300"
                : "text-slate-500 hover:text-slate-700 dark:text-slate-400 dark:hover:text-slate-200"
            }`}
          >
            Log
            {hasAgentFlow ? (
              <span className="inline-block h-1.5 w-1.5 rounded-full bg-emerald-500" />
            ) : null}
          </button>
        </div>

        {/* Tab content — transcript or flow */}
        <div className="flex min-h-0 min-w-0 flex-1 flex-col overflow-hidden">
          {activeTab === "chat" ? (
            <ChatTranscript />
          ) : (
            <AgentFlowPane />
          )}
        </div>

        {filmstripVisible ? <SelectionFilmStrip forceShow /> : null}
        <ToolOutputStrip />
        <ChatComposerDock />
        <ChatToolbar auth={pixlwizAuth} pixlwizAuthEnabled={embedFeatures.pixlwizAuth} />
      </div>
      {showQuickActionEditor ? <QuickActionsModal /> : null}
      {showDesignPresetEditor ? <DesignPresetsModal /> : null}
      {showResizePresetEditor ? <ResizePresetsModal /> : null}
      <SpeechCaptureOverlay />
    </div>
  );
}

function SpeechCaptureOverlay() {
  const sttRecording = usePmChatStore((s) => s.sttRecording);
  const sttPartial = usePmChatStore((s) => s.sttPartial);
  const sttLevel = usePmChatStore((s) => s.sttLevel);
  const inputDraft = usePmChatStore((s) => s.inputDraft);
  const busy = usePmChatStore((s) => s.busy);

  const stopMic = () => {
    usePmChatStore.getState().setSttRecording(false);
    usePmChatStore.getState().setSttPartial("");
    postHost({ kind: "sttStop" });
  };

  useEffect(() => {
    if (!sttRecording) return undefined;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key !== "Escape") return;
      event.preventDefault();
      stopMic();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [sttRecording]);

  if (!sttRecording) return null;

  const sendAndStop = () => {
    const st = usePmChatStore.getState();
    const partial = st.sttPartial.trim();
    if (partial) st.appendSttCommitted(partial);
    postHost({ kind: "sttStop" });
    st.setSttRecording(false);
    window.setTimeout(() => usePmChatStore.getState().send(), 0);
  };

  const displayText = [inputDraft.trim(), sttPartial.trim()].filter(Boolean).join(" ");
  const bars = [0.35, 0.62, 0.9, 0.55, 0.78, 0.42, 0.68];

  return (
    <div className="pointer-events-none absolute inset-0 z-50 flex items-center justify-center px-4">
      <div className="pointer-events-auto flex min-h-[250px] w-[min(88vw,30rem)] flex-col justify-between rounded-3xl border border-slate-200/80 bg-white/80 p-5 text-slate-900 shadow-[0_24px_80px_rgba(15,23,42,0.18)] ring-1 ring-blue-500/10 backdrop-blur-xl dark:border-slate-700/80 dark:bg-slate-950/75 dark:text-slate-100 dark:shadow-[0_24px_80px_rgba(0,0,0,0.45)] dark:ring-cyan-300/10">
        <div className="flex items-start justify-between gap-4">
          <div>
            <div className="text-[10px] font-semibold uppercase tracking-[0.28em] text-blue-600 dark:text-cyan-300">Voice input</div>
            <div className="mt-1 text-lg font-semibold">Listening</div>
          </div>
          <div className="flex h-14 items-center gap-1.5 rounded-full border border-blue-500/10 bg-blue-500/5 px-4 dark:border-cyan-300/10 dark:bg-cyan-300/5" aria-hidden>
            {bars.map((weight, index) => (
              <span
                key={index}
                className="w-1.5 rounded-full bg-blue-600 transition-all duration-100 dark:bg-cyan-300"
                style={{ height: 8 + Math.round((sttLevel * weight) * 38) }}
              />
            ))}
          </div>
        </div>
        <div className="my-5 flex min-h-24 items-center rounded-2xl border border-slate-200/70 bg-slate-50/80 px-4 py-3 text-left dark:border-slate-700/70 dark:bg-slate-900/70">
          <p className={`max-h-28 overflow-auto whitespace-pre-wrap text-sm leading-6 ${displayText ? "text-slate-800 dark:text-slate-100" : "italic text-slate-400 dark:text-slate-500"}`}>
            {displayText || "Speak now..."}
          </p>
        </div>
        <div className="flex items-center justify-between gap-3">
          <div className="h-1.5 flex-1 overflow-hidden rounded-full bg-slate-200 dark:bg-slate-800" aria-hidden>
            <div
              className="h-full rounded-full bg-blue-600 transition-all duration-100 dark:bg-cyan-300"
              style={{ width: `${Math.max(5, Math.round(sttLevel * 100))}%` }}
            />
          </div>
          <button
            type="button"
            className="h-10 rounded-full border border-slate-300/80 px-5 text-sm font-medium text-slate-700 hover:bg-slate-100 dark:border-slate-600 dark:text-slate-200 dark:hover:bg-slate-800"
            onClick={stopMic}
          >
            Stop
          </button>
          <button
            type="button"
            className="h-10 rounded-full bg-blue-600 px-5 text-sm font-semibold text-white shadow-lg shadow-blue-600/20 hover:bg-blue-700 disabled:opacity-50 dark:bg-cyan-400 dark:text-slate-950 dark:shadow-cyan-400/10 dark:hover:bg-cyan-300"
            disabled={busy}
            onClick={sendAndStop}
          >
            Send
          </button>
        </div>
      </div>
    </div>
  );
}
