import type { KeyboardEvent, PointerEvent } from "react";
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";

import { tryAddClipboardImagesToChatContext } from "./clipboardImages";
import { hasWebProviderHost, postHost } from "./hostBridge";
import { t } from "./i18n.js";
import { usePmChatStore } from "./pmChatStore";
import { IconCamera, IconChevDown, IconChevUp, IconMic, IconMicOff, IconSend, IconStop } from "./svgIcons";

const COMPOSER_INPUT_HEIGHT_LS = "pmImageChat.composerInputMinPx";
const MIN_COMPOSER_INPUT_MAX_PX = 34;
const COMPOSER_INPUT_MAX_VIEWPORT_RATIO = 0.5;

function composerInputMaxPxLimit(): number {
  if (typeof window === "undefined") return 240;
  return Math.max(MIN_COMPOSER_INPUT_MAX_PX, Math.floor(window.innerHeight * COMPOSER_INPUT_MAX_VIEWPORT_RATIO));
}

function readStoredComposerInputMinPx(): number | null {
  try {
    const raw = localStorage.getItem(COMPOSER_INPUT_HEIGHT_LS);
    if (raw == null) return null;
    const n = Number.parseInt(raw, 10);
    if (!Number.isFinite(n)) return null;
    return Math.min(composerInputMaxPxLimit(), Math.max(MIN_COMPOSER_INPUT_MAX_PX, n));
  } catch {
    return null;
  }
}

export function ChatComposerDock() {
  const i18nRev = usePmChatStore((s) => s.i18nRev);
  void i18nRev;
  const inputDraft = usePmChatStore((s) => s.inputDraft);
  const setInputDraft = usePmChatStore((s) => s.setInputDraft);
  const busy = usePmChatStore((s) => s.busy);
  const promptHistory = usePmChatStore((s) => s.promptHistory);
  const historyIndex = usePmChatStore((s) => s.historyIndex);
  const navigateHistory = usePmChatStore((s) => s.navigateHistory);
  const send = usePmChatStore((s) => s.send);
  const sttRecording = usePmChatStore((s) => s.sttRecording);
  const sttPartial = usePmChatStore((s) => s.sttPartial);
  const videoSnapBusy = usePmChatStore((s) => s.videoSnapBusy);
  const ttsPlaying = usePmChatStore((s) => s.ttsPlaying);

  const taRef = useRef<HTMLTextAreaElement>(null);
  const [inputMinPx, setInputMinPx] = useState(readStoredComposerInputMinPx);

  const toggleStt = useCallback(() => {
    if (!hasWebProviderHost()) return;
    if (sttRecording) {
      // Flip UI immediately; C++ will confirm via sttDone() after closing the session.
      usePmChatStore.getState().setSttRecording(false);
      usePmChatStore.getState().setSttPartial("");
      postHost({ kind: "sttStop" });
    } else {
      usePmChatStore.getState().setSttRecording(true);
      postHost({ kind: "sttStart" });
    }
  }, [sttRecording]);

  const takeSnapshot = useCallback(() => {
    if (!hasWebProviderHost() || videoSnapBusy) return;
    usePmChatStore.getState().setVideoSnapBusy(true);
    postHost({ kind: "videoSnapshot" });
  }, [videoSnapBusy]);

  const histLabel =
    promptHistory.length > 0 ? (historyIndex >= 0 ? `${historyIndex + 1}/${promptHistory.length}` : "") : "";

  const autoResize = useCallback(() => {
    const i = taRef.current;
    if (!i) return;
    const cs = getComputedStyle(i);
    const lineHeight = parseFloat(cs.lineHeight) || 20;
    const padY = (parseFloat(cs.paddingTop) || 0) + (parseFloat(cs.paddingBottom) || 0);
    const minLines = 1;
    const minH = lineHeight * minLines + padY;
    const maxH = Math.max(minH, composerInputMaxPxLimit());
    i.style.height = "auto";
    const manualMinH = inputMinPx == null ? minH : Math.max(minH, inputMinPx);
    const next = Math.min(maxH, Math.max(manualMinH, i.scrollHeight));
    i.style.height = `${next}px`;
    i.style.overflowY = i.scrollHeight > maxH + 1 ? "auto" : "hidden";
  }, [inputMinPx]);

  useLayoutEffect(() => {
    autoResize();
  }, [inputDraft, autoResize]);

  const onKeyDown = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      send();
      return;
    }
    if (e.key === "ArrowUp" && e.ctrlKey) {
      e.preventDefault();
      navigateHistory("up");
      return;
    }
    if (e.key === "ArrowDown" && e.ctrlKey) {
      e.preventDefault();
      navigateHistory("down");
    }
  };

  useEffect(() => {
    taRef.current?.focus();
  }, []);

  const onResizePointerDown = useCallback((e: PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const startY = e.clientY;
    const startPx = inputMinPx ?? taRef.current?.getBoundingClientRect().height ?? MIN_COMPOSER_INPUT_MAX_PX;
    let lastPx = startPx;
    const move = (ev: globalThis.PointerEvent) => {
      lastPx = Math.min(composerInputMaxPxLimit(), Math.max(MIN_COMPOSER_INPUT_MAX_PX, startPx + (startY - ev.clientY)));
      setInputMinPx(lastPx);
    };
    const end = () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
      try {
        localStorage.setItem(COMPOSER_INPUT_HEIGHT_LS, String(lastPx));
      } catch {
        /* private mode / quota */
      }
      document.body.style.removeProperty("cursor");
      document.body.style.removeProperty("user-select");
    };
    document.body.style.cursor = "ns-resize";
    document.body.style.userSelect = "none";
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
  }, [inputMinPx]);

  return (
    <div className="relative px-1.5 pt-2 pb-[max(0.2rem,env(safe-area-inset-bottom,0px))]">
      <div
        role="separator"
        aria-orientation="horizontal"
        aria-label="Resize message box"
        className="group absolute top-0 right-0 left-0 z-10 flex h-3 cursor-ns-resize touch-none items-start justify-center"
        onPointerDown={onResizePointerDown}
      >
        <span className="mt-1 h-1 w-12 rounded-full bg-slate-300/70 shadow-sm transition-colors group-hover:bg-violet-400/80 dark:bg-slate-600/70 dark:group-hover:bg-violet-400/70" />
      </div>
      <div className="rounded-md border border-slate-300 bg-white transition-shadow focus-within:border-transparent focus-within:ring-1 focus-within:ring-blue-500 dark:border-slate-700 dark:bg-surface-dimmed">
        <div className="px-1">
          <textarea
            ref={taRef}
            id="pm-input"
            rows={1}
            className="pm-composer-input block min-h-0 w-full min-w-0 resize-none overflow-x-hidden bg-transparent px-2 py-1.5 text-sm leading-normal break-words focus:outline-none placeholder:text-slate-400 dark:placeholder:text-slate-500"
            placeholder={t("inputPlaceholder")}
            value={inputDraft}
            onChange={(e) => {
              setInputDraft(e.target.value);
              autoResize();
            }}
            onPaste={(e) => {
              void tryAddClipboardImagesToChatContext(e);
            }}
            onKeyDown={onKeyDown}
          />
          {sttPartial ? (
            <p className="truncate px-2 pb-0.5 text-xs italic text-slate-400 dark:text-slate-500">
              {sttPartial}
            </p>
          ) : null}
        </div>
        <div className="flex items-center justify-between gap-1.5 px-1 pb-0.5 pt-0">
          <div className="flex min-w-0 flex-1 items-center">
            {histLabel ? (
              <span className="font-mono text-[10px] text-slate-500 tabular-nums dark:text-slate-500" title={t("histColTitle")}>
                {histLabel}
              </span>
            ) : null}
          </div>
          <div className="flex flex-shrink-0 items-center gap-px">
            {promptHistory.length > 0 ? (
              <div className="flex flex-row items-center gap-px" title={t("histColTitle")}>
                <button
                  type="button"
                  id="pm-hist-up"
                  className="pm-icon-btn"
                  disabled={busy || historyIndex >= promptHistory.length - 1}
                  title={t("histOlderTip")}
                  onClick={() => navigateHistory("up")}
                >
                  <span className="sr-only">{t("histOlderTip")}</span>
                  <IconChevUp />
                </button>
                <button
                  type="button"
                  id="pm-hist-down"
                  className="pm-icon-btn"
                  disabled={busy || historyIndex <= -1}
                  title={t("histNewerTip")}
                  onClick={() => navigateHistory("down")}
                >
                  <span className="sr-only">{t("histNewerTip")}</span>
                  <IconChevDown />
                </button>
              </div>
            ) : null}
            {hasWebProviderHost() ? (
              <>
                {ttsPlaying ? (
                  <button
                    type="button"
                    id="pm-speak-stop"
                    className="pm-icon-btn text-amber-500 dark:text-amber-400"
                    title="Stop speaking"
                    onClick={() => postHost({ kind: "speakStop" })}
                  >
                    <span className="sr-only">Stop speaking</span>
                    <IconStop />
                  </button>
                ) : null}
                <button
                  type="button"
                  id="pm-webcam"
                  className={`pm-icon-btn${videoSnapBusy ? " opacity-50" : ""}`}
                  title="Capture webcam still and add to context"
                  disabled={videoSnapBusy}
                  onClick={takeSnapshot}
                >
                  <span className="sr-only">Capture webcam still</span>
                  <IconCamera />
                </button>
                <button
                  type="button"
                  id="pm-stt"
                  className={`pm-icon-btn${sttRecording ? " text-red-500 dark:text-red-400" : ""}`}
                  title={sttRecording ? "Stop recording" : "Record voice (ElevenLabs STT)"}
                  onClick={toggleStt}
                >
                  <span className="sr-only">{sttRecording ? "Stop recording" : "Record voice"}</span>
                  {sttRecording ? <IconMicOff /> : <IconMic />}
                </button>
              </>
            ) : null}
            <button
              type="button"
              id="pm-send"
              className="pm-icon-btn pm-icon-btn-primary"
              disabled={busy}
              title={`${t("btnSend")} (${t("ctrlEnterHint")})`}
              onClick={() => send()}
            >
              <span className="sr-only">{t("ariaSend")}</span>
              <IconSend />
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
