import { useEffect, useRef, useState, useCallback } from "react";
import Editor, { type OnMount } from "@monaco-editor/react";
import type { editor } from "monaco-editor";
import { Check, ChevronDown, Columns2, Loader2, Save, Square, Wand2 } from "lucide-react";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { onHostMessage, postToHost } from "@/bridge/hostBridge";
import { fetchHostedText } from "@/bridge/hostedFileFetch";
import { resolveHostedTextSource } from "@/bridge/resolveHostedTextSource";
import {
  BEAUTIFY_TARGETS,
  beautifyInMonacoEditor,
  beautifyTargetForLanguage,
  type BeautifyTarget,
} from "@/viewers/editor/editorBeautify";
import { detectMonacoLanguage } from "@/viewers/editor/monacoLangMap";
import { MarkdownRenderer } from "@/viewers/markdown/MarkdownRenderer";

// ---------------------------------------------------------------------------
// Language detection + dropdown options
// ---------------------------------------------------------------------------

const LANGUAGE_OPTS = [
  { value: "auto",        label: "Auto-detect" },
  { value: "typescript",  label: "TypeScript" },
  { value: "javascript",  label: "JavaScript" },
  { value: "python",      label: "Python" },
  { value: "rust",        label: "Rust" },
  { value: "go",          label: "Go" },
  { value: "ruby",        label: "Ruby" },
  { value: "java",        label: "Java" },
  { value: "kotlin",      label: "Kotlin" },
  { value: "csharp",      label: "C#" },
  { value: "lua",         label: "Lua" },
  { value: "php",         label: "PHP" },
  { value: "cpp",         label: "C++" },
  { value: "c",           label: "C" },
  { value: "json",        label: "JSON" },
  { value: "yaml",        label: "YAML" },
  { value: "markdown",    label: "Markdown" },
  { value: "html",        label: "HTML" },
  { value: "css",         label: "CSS" },
  { value: "scss",        label: "SCSS" },
  { value: "less",        label: "Less" },
  { value: "sql",         label: "SQL" },
  { value: "shell",       label: "Shell" },
  { value: "bat",         label: "Batch" },
  { value: "powershell",  label: "PowerShell" },
  { value: "dockerfile",  label: "Dockerfile" },
  { value: "graphql",     label: "GraphQL" },
  { value: "bicep",       label: "Bicep" },
  { value: "ini",         label: "TOML / INI" },
  { value: "xml",         label: "XML" },
  { value: "plaintext",   label: "Plain Text" },
] as const;

function detectedLanguage(fileName: string): string {
  return detectMonacoLanguage(fileName);
}

// ---------------------------------------------------------------------------
// Dual-pane map: which editor languages get a live preview panel
// ---------------------------------------------------------------------------

type PreviewKind = "markdown";
const DUAL_PANE_MAP: Record<string, PreviewKind> = { markdown: "markdown" };

// ---------------------------------------------------------------------------
// Save state type
// ---------------------------------------------------------------------------

type SaveState = "idle" | "saving" | "saved" | "error";

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function MonacoEditorPane({ status }: { status: ViewerWebStatus }) {
  const { err, url, inlineText, fileName } = resolveHostedTextSource(status);
  const f = status.features ?? {};
  const isDarkTheme = f.theme !== "light";

  // ── State ─────────────────────────────────────────────────────────────────
  const [content, setContent]         = useState<string | null>(null);
  const [editorValue, setEditorValue] = useState("");
  const [savedValue, setSavedValue]   = useState("");   // last persisted snapshot
  const [loading, setLoading]         = useState(false);
  const [parseErr, setParseErr]       = useState<string | null>(null);
  const [saveState, setSaveState]     = useState<SaveState>("idle");
  const [splitView, setSplitView]     = useState(true);
  const [langOverride, setLangOverride] = useState("auto");
  const [langDropOpen, setLangDropOpen] = useState(false);
  const [formatDropOpen, setFormatDropOpen] = useState(false);
  const [formatBusy, setFormatBusy] = useState(false);
  const [formatErr, setFormatErr] = useState<string | null>(null);
  const langDropRef = useRef<HTMLDivElement>(null);
  const formatDropRef = useRef<HTMLDivElement>(null);
  const editorRef = useRef<editor.IStandaloneCodeEditor | null>(null);
  const monacoRef = useRef<Parameters<OnMount>[1] | null>(null);

  const autoLang    = detectedLanguage(fileName);
  const language    = langOverride === "auto" ? autoLang : langOverride;
  const previewKind = DUAL_PANE_MAP[language] as PreviewKind | undefined;
  const isDirty     = editorValue !== savedValue;
  const canSave     = !loading && content !== null;

  // ── Reset language override when the file changes ─────────────────────────

  useEffect(() => {
    setLangOverride("auto");
  }, [fileName]);

  // ── Load file ─────────────────────────────────────────────────────────────

  useEffect(() => {
    if (typeof inlineText === "string") {
      setContent(inlineText);
      setEditorValue(inlineText);
      setSavedValue(inlineText);
      setLoading(false);
      setParseErr(null);
      return;
    }
    if (!url) {
      setContent("");
      setEditorValue("");
      setSavedValue("");
      setLoading(false);
      setParseErr(null);
      return;
    }
    const ac = new AbortController();
    (async () => {
      setLoading(true);
      setParseErr(null);
      setContent(null);
      try {
        const text = await fetchHostedText(url, ac.signal);
        setContent(text);
        setEditorValue(text);
        setSavedValue(text);
      } catch (e: unknown) {
        if (ac.signal.aborted) return;
        setParseErr(e instanceof Error ? e.message : "Failed to load");
      } finally {
        if (!ac.signal.aborted) setLoading(false);
      }
    })();
    return () => ac.abort();
  }, [url, inlineText]);

  // ── Listen for save result from C++ host ──────────────────────────────────

  useEffect(() => {
    return onHostMessage((data) => {
      if (data.t !== "vw_save_file_result") return;
      if (data.ok === true) {
        setSavedValue(editorValue);
        setSaveState("saved");
        setTimeout(() => setSaveState("idle"), 2000);
      } else {
        setSaveState("error");
        setTimeout(() => setSaveState("idle"), 3000);
      }
    });
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [editorValue]);

  // ── Close lang dropdown on outside click ──────────────────────────────────

  useEffect(() => {
    const handler = (e: MouseEvent) => {
      if (langDropRef.current && !langDropRef.current.contains(e.target as Node))
        setLangDropOpen(false);
      if (formatDropRef.current && !formatDropRef.current.contains(e.target as Node))
        setFormatDropOpen(false);
    };
    document.addEventListener("mousedown", handler);
    return () => document.removeEventListener("mousedown", handler);
  }, []);

  // ── Save ──────────────────────────────────────────────────────────────────

  const triggerSave = useCallback(() => {
    if (!canSave) return;
    setSaveState("saving");
    postToHost({ t: "vw_save_file", content: editorValue });
  }, [canSave, editorValue]);

  // Keep a ref so the Monaco addCommand closure always calls the latest version
  const triggerSaveRef = useRef(triggerSave);
  triggerSaveRef.current = triggerSave;

  const runBeautify = useCallback(
    async (target: BeautifyTarget) => {
      const editorInst = editorRef.current;
      const monaco = monacoRef.current;
      if (!editorInst || !monaco || formatBusy || loading || content === null) return;

      setFormatDropOpen(false);
      setFormatErr(null);
      setFormatBusy(true);
      try {
        const result = await beautifyInMonacoEditor(editorInst, monaco, target, language);
        if (!result.ok) {
          setFormatErr(result.error);
          return;
        }
        setContent(result.text);
        setEditorValue(result.text);
      } finally {
        setFormatBusy(false);
      }
    },
    [content, formatBusy, language, loading],
  );

  const runBeautifyRef = useRef(runBeautify);
  runBeautifyRef.current = runBeautify;

  // ── Monaco mount ──────────────────────────────────────────────────────────

  const handleMount: OnMount = useCallback((editorInst, monacoApi) => {
    editorRef.current = editorInst;
    monacoRef.current = monacoApi;
    editorInst.addCommand(
      monacoApi.KeyMod.CtrlCmd | monacoApi.KeyCode.KeyS,
      () => triggerSaveRef.current(),
    );
    editorInst.addCommand(
      monacoApi.KeyMod.CtrlCmd | monacoApi.KeyMod.Shift | monacoApi.KeyCode.KeyF,
      () => void runBeautifyRef.current("auto"),
    );
  }, []);

  const handleChange = useCallback((value: string | undefined) => {
    setEditorValue(value ?? "");
  }, []);

  // ── Error / empty guards ──────────────────────────────────────────────────

  if (err) {
    return (
      <div className="flex h-full items-center justify-center text-sm text-slate-500 dark:text-slate-400">
        {err}
      </div>
    );
  }

  const hasSource = url.length > 0 || typeof inlineText === "string";
  if (!hasSource) {
    return (
      <div className="flex h-full items-center justify-center text-sm text-slate-500 dark:text-slate-400">
        No file loaded.
      </div>
    );
  }

  // ── Editor options ────────────────────────────────────────────────────────

  const editorOptions = {
    minimap: { enabled: false },
    scrollBeyondLastLine: false,
    lineNumbers: "on" as const,
    renderWhitespace: "none" as const,
    fontSize: 13,
    fontFamily:
      "'JetBrains Mono', 'Fira Code', 'Cascadia Code', Consolas, 'Courier New', monospace",
    padding: { top: 8, bottom: 8 },
    automaticLayout: true,
    tabSize: 2,
    insertSpaces: true,
    folding: true,
    bracketPairColorization: { enabled: true },
    renderLineHighlight: "all" as const,
    smoothScrolling: true,
    cursorBlinking: "smooth" as const,
    contextmenu: true,
    wordWrap: (language === "markdown" || language === "plaintext" ? "on" : "off") as "on" | "off",
  };

  const monacoTheme  = isDarkTheme ? "vs-dark" : "light";
  const showSplit    = !!previewKind && splitView;
  const currentLang  = LANGUAGE_OPTS.find((o) => o.value === langOverride) ?? LANGUAGE_OPTS[0];
  const quickFormatTarget = beautifyTargetForLanguage(language);

  const saveLabel =
    saveState === "saving" ? "Saving…"
    : saveState === "saved"  ? "Saved"
    : saveState === "error"  ? "Error"
    : isDirty                ? "Save"
    : "Saved";

  const saveBtnClass =
    saveState === "saved"
      ? "border-emerald-400 bg-emerald-50 text-emerald-700 dark:border-emerald-600 dark:bg-emerald-950/30 dark:text-emerald-300"
      : saveState === "error"
        ? "border-red-400 bg-red-50 text-red-700 dark:border-red-600 dark:bg-red-950/30 dark:text-red-300"
        : isDirty
          ? "border-amber-400 bg-amber-50 text-amber-700 dark:border-amber-600 dark:bg-amber-900/20 dark:text-amber-300"
          : "border-transparent text-slate-500 dark:text-slate-400 opacity-60";

  // ── Render ────────────────────────────────────────────────────────────────

  return (
    <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden">
      {/* Editor toolbar */}
      <div
        className="flex shrink-0 items-center gap-2 border-b px-2 py-1"
        style={{ borderColor: "var(--border)" }}
      >
        {/* Save button */}
        <button
          type="button"
          className={`flex items-center gap-1.5 rounded border px-2 py-1 text-xs transition-colors ${saveBtnClass}`}
          style={{ borderColor: undefined }}
          disabled={!canSave || saveState === "saving"}
          onClick={triggerSave}
          title={isDirty ? "Save (Ctrl+S)" : "No unsaved changes"}
        >
          {saveState === "saved" ? (
            <Check className="h-3.5 w-3.5" />
          ) : (
            <Save className="h-3.5 w-3.5" />
          )}
          <span>{saveLabel}</span>
          {isDirty && saveState === "idle" && (
            <span className="h-1.5 w-1.5 rounded-full bg-amber-500" title="Unsaved changes" />
          )}
        </button>

        {/* Format */}
        <div className="relative flex items-center" ref={formatDropRef}>
          <button
            type="button"
            className="flex items-center gap-1 rounded-l border border-r-0 px-2 py-1 text-xs text-slate-700 hover:bg-slate-100/50 disabled:opacity-50 dark:text-slate-300 dark:hover:bg-white/5"
            style={{ borderColor: "var(--border)" }}
            disabled={formatBusy || loading || content === null}
            onClick={() => void runBeautify("auto")}
            title="Format document (Ctrl+Shift+F)"
          >
            {formatBusy ? (
              <Loader2 className="h-3.5 w-3.5 animate-spin" />
            ) : (
              <Wand2 className="h-3.5 w-3.5" />
            )}
            <span>Format</span>
          </button>
          <button
            type="button"
            className="flex items-center rounded-r border px-1.5 py-1 text-xs text-slate-700 hover:bg-slate-100/50 disabled:opacity-50 dark:text-slate-300 dark:hover:bg-white/5"
            style={{ borderColor: "var(--border)" }}
            disabled={formatBusy || loading || content === null}
            onClick={() => setFormatDropOpen((v) => !v)}
            title="Choose formatter"
            aria-label="Format options"
          >
            <ChevronDown className="h-3 w-3 opacity-60" />
          </button>
          {formatDropOpen && (
            <div
              className="absolute left-0 top-full z-50 mt-1 min-w-[180px] rounded border bg-white py-1 shadow-lg dark:bg-zinc-900"
              style={{ borderColor: "var(--border)" }}
            >
              {BEAUTIFY_TARGETS.map((opt) => (
                <button
                  key={opt.id}
                  type="button"
                  className={`block w-full px-3 py-1.5 text-left text-xs hover:bg-slate-100/50 dark:hover:bg-white/5 ${
                    opt.id !== "auto" && opt.id === quickFormatTarget
                      ? "font-semibold text-violet-600 dark:text-violet-300"
                      : ""
                  }`}
                  onClick={() => void runBeautify(opt.id)}
                >
                  {opt.label}
                </button>
              ))}
            </div>
          )}
        </div>
        {formatErr ? (
          <span className="max-w-[12rem] truncate text-[10px] text-red-500" title={formatErr}>
            {formatErr}
          </span>
        ) : null}

        {/* Spacer */}
        <div className="flex-1" />

        {/* Language dropdown */}
        <div className="relative" ref={langDropRef}>
          <button
            type="button"
            className="flex items-center gap-1 rounded border px-2 py-1 text-xs text-slate-700 hover:bg-slate-100/50 dark:text-slate-300 dark:hover:bg-white/5"
            style={{ borderColor: "var(--border)" }}
            onClick={() => setLangDropOpen((v) => !v)}
            title="Override language"
          >
            <span>{currentLang.label}</span>
            <ChevronDown className="h-3 w-3 opacity-60" />
          </button>
          {langDropOpen && (
            <div
              className="absolute right-0 top-full z-50 mt-1 max-h-64 min-w-[140px] overflow-auto rounded border bg-white shadow-lg dark:bg-zinc-900"
              style={{ borderColor: "var(--border)" }}
            >
              {LANGUAGE_OPTS.map((opt) => (
                <button
                  key={opt.value}
                  type="button"
                  className={`block w-full px-3 py-1.5 text-left text-xs hover:bg-slate-100/50 dark:hover:bg-white/5 ${
                    langOverride === opt.value ? "font-semibold text-violet-600 dark:text-violet-300" : ""
                  }`}
                  onClick={() => {
                    setLangOverride(opt.value);
                    setLangDropOpen(false);
                  }}
                >
                  {opt.label}
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Split-view toggle (only for languages with a preview pane) */}
        {previewKind && (
          <button
            type="button"
            className={`flex items-center gap-1.5 rounded border px-2 py-1 text-xs transition-colors ${
              splitView
                ? "border-violet-400 bg-violet-50 text-violet-700 dark:border-violet-600 dark:bg-violet-950/30 dark:text-violet-300"
                : "border-transparent text-slate-600 hover:bg-slate-100/50 dark:text-slate-400 dark:hover:bg-white/5"
            }`}
            onClick={() => setSplitView((v) => !v)}
            title={splitView ? "Single editor pane" : "Split: editor + preview"}
          >
            {splitView ? <Columns2 className="h-3.5 w-3.5" /> : <Square className="h-3.5 w-3.5" />}
          </button>
        )}
      </div>

      {/* Main content */}
      {loading ? (
        <div className="flex flex-1 items-center justify-center gap-2 text-slate-500">
          <Loader2 className="h-5 w-5 animate-spin" />
          <span className="text-sm">Loading…</span>
        </div>
      ) : parseErr ? (
        <div className="flex flex-1 items-center justify-center text-sm text-red-500">{parseErr}</div>
      ) : (
        <div className={`flex min-h-0 flex-1 overflow-hidden ${showSplit ? "flex-row" : "flex-col"}`}>
          {/* Monaco editor */}
          <div
            className={`min-h-0 overflow-hidden flex-1 ${showSplit ? "border-r" : ""}`}
            style={showSplit ? { borderColor: "var(--border)" } : undefined}
          >
            <Editor
              height="100%"
              language={language}
              value={content ?? ""}
              theme={monacoTheme}
              onChange={handleChange}
              onMount={handleMount}
              loading={
                <div className="flex h-full items-center justify-center">
                  <Loader2 className="h-5 w-5 animate-spin text-slate-400" />
                </div>
              }
              options={editorOptions}
            />
          </div>

          {/* Live markdown preview */}
          {showSplit && previewKind === "markdown" && (
            <div className="pm-scroll min-h-0 flex-1 overflow-auto px-5 py-4">
              <MarkdownRenderer content={editorValue} />
            </div>
          )}
        </div>
      )}
    </div>
  );
}
