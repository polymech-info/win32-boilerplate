import React, { useEffect, useRef, useState } from "react";
import { Check, Copy, Eye, FileCode, FileText, Loader2, Moon, Sun, Type, Hash, ChevronDown } from "lucide-react";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedText } from "@/bridge/hostedFileFetch";
import { resolveHostedTextSource } from "@/bridge/resolveHostedTextSource";
import { MarkdownRenderer } from "@/viewers/markdown/MarkdownRenderer";

import Prism from "prismjs";
import "prismjs/components/prism-clike";
import "prismjs/components/prism-javascript";
import "prismjs/components/prism-typescript";
import "prismjs/components/prism-c";
import "prismjs/components/prism-cpp";
import "prismjs/components/prism-json";
import "prismjs/components/prism-bash";
import "prismjs/components/prism-css";
import "prismjs/components/prism-markup";
import "prismjs/components/prism-python";
import "prismjs/components/prism-rust";
import "prismjs/components/prism-go";
import "prismjs/components/prism-sql";
import "prismjs/components/prism-yaml";
import "prismjs/components/prism-toml";
import "prismjs/components/prism-markdown";

/** Base font size for code display */
const BASE_FONT_SIZE_PX = 14;

/** Available language options for syntax highlighting override */
const LANGUAGE_OPTIONS = [
  { value: "auto", label: "Auto-detect" },
  { value: "none", label: "Plain Text" },
  { value: "typescript", label: "TypeScript" },
  { value: "javascript", label: "JavaScript" },
  { value: "json", label: "JSON" },
  { value: "python", label: "Python" },
  { value: "rust", label: "Rust" },
  { value: "go", label: "Go" },
  { value: "cpp", label: "C++" },
  { value: "c", label: "C" },
  { value: "sql", label: "SQL" },
  { value: "yaml", label: "YAML" },
  { value: "toml", label: "TOML" },
  { value: "markdown", label: "Markdown" },
  { value: "css", label: "CSS" },
  { value: "bash", label: "Bash/Shell" },
] as const;

/** Parent URL on the markdown-assets vhost for resolving relative links in Markdown mode. */
function markdownBaseFromHostedUrl(url: string): string | undefined {
  if (!url) return undefined;
  const i = url.lastIndexOf("/");
  if (i <= 0) return undefined;
  return url.slice(0, i + 1);
}

function displayLanguageLabel(fileName: string): string {
  const lower = fileName.toLowerCase();
  if (lower === "dockerfile") return "Dockerfile";
  if (lower === "makefile") return "Makefile";
  const dot = lower.lastIndexOf(".");
  if (dot < 0) return "Text";
  const ext = lower.slice(dot + 1);
  const map: Record<string, string> = {
    ts: "TypeScript",
    tsx: "TypeScript (JSX)",
    js: "JavaScript",
    mjs: "JavaScript",
    cjs: "JavaScript",
    jsx: "JavaScript (JSX)",
    py: "Python",
    rs: "Rust",
    go: "Go",
    json: "JSON",
    md: "Markdown",
    markdown: "Markdown",
    txt: "Text",
    log: "Log",
    yaml: "YAML",
    yml: "YAML",
    toml: "TOML",
    html: "HTML",
    htm: "HTML",
    css: "CSS",
    c: "C",
    cpp: "C++",
    h: "C Header",
    hpp: "C++ Header",
    sql: "SQL",
  };
  return map[ext] ?? "Text";
}

function prismLanguageForFile(fileName: string): string {
  const lower = fileName.toLowerCase();
  if (lower === "dockerfile") return "docker";
  if (lower === "makefile") return "makefile";
  const dot = lower.lastIndexOf(".");
  if (dot < 0) return "none";
  const ext = lower.slice(dot + 1);
  const map: Record<string, string> = {
    ts: "typescript",
    tsx: "typescript",
    js: "javascript",
    jsx: "javascript",
    json: "json",
    sh: "bash",
    bash: "bash",
    zsh: "bash",
    css: "css",
    c: "c",
    cpp: "cpp",
    h: "c",
    hpp: "cpp",
    py: "python",
    rs: "rust",
    go: "go",
    sql: "sql",
    yaml: "yaml",
    yml: "yaml",
    toml: "toml",
    md: "markdown",
    markdown: "markdown",
    html: "markup",
    htm: "markup",
  };
  return map[ext] ?? "none";
}

export function TextViewerPane({ status }: { status: ViewerWebStatus }) {
  const { err, url, inlineText, fileName } = resolveHostedTextSource(status);
  const f = status.features ?? {};

  // Determine initial theme from host status (default dark if not specified)
  const hostTheme = f.theme;
  const initialDark = hostTheme !== "light";

  const [content, setContent] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [parseErr, setParseErr] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);
  const [mdShowPreview, setMdShowPreview] = useState(true);
  const [showLineNumbers, setShowLineNumbers] = useState(false);
  const [isDarkTheme, setIsDarkTheme] = useState(initialDark);
  const [languageOverride, setLanguageOverride] = useState<string>("auto");
  const [langDropdownOpen, setLangDropdownOpen] = useState(false);
  const codeHostRef = useRef<HTMLDivElement>(null);
  const langDropdownRef = useRef<HTMLDivElement>(null);

  const isMarkdown = /\.md$/i.test(fileName) || /\.markdown$/i.test(fileName);
  const langLabel = displayLanguageLabel(fileName);
  const detectedPrismLang = prismLanguageForFile(fileName);
  const prismLang = languageOverride === "auto" ? detectedPrismLang : languageOverride;
  const isCode = !isMarkdown && prismLang !== "none";

  // Sync theme when host status changes (e.g., during global theme switch)
  useEffect(() => {
    if (hostTheme === "dark" || hostTheme === "light") {
      setIsDarkTheme(hostTheme === "dark");
    }
  }, [hostTheme]);

  // Close language dropdown when clicking outside
  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (langDropdownRef.current && !langDropdownRef.current.contains(event.target as Node)) {
        setLangDropdownOpen(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, []);

  useEffect(() => {
    if (typeof inlineText === "string") {
      setLoading(false);
      setParseErr(null);
      setContent(inlineText);
      return;
    }
    if (!url) {
      setContent(null);
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
        setParseErr(null);
      } catch (e: unknown) {
        if (ac.signal.aborted) return;
        setParseErr(e instanceof Error ? e.message : "Failed to load");
      } finally {
        if (!ac.signal.aborted) setLoading(false);
      }
    })();
    return () => ac.abort();
  }, [url, inlineText]);

  useEffect(() => {
    const host = codeHostRef.current;
    if (!host || loading || content === null) return;
    if (isMarkdown && mdShowPreview) return;
    const effectivePrism = isMarkdown ? "markdown" : (prismLang === "none" ? "text" : prismLang);
    host.innerHTML = "";
    const pre = document.createElement("pre");
    pre.className = "!m-0 !bg-transparent !p-0";
    pre.style.whiteSpace = "pre";
    pre.style.tabSize = "4";
    pre.style.fontSize = `${BASE_FONT_SIZE_PX}px`;
    const code = document.createElement("code");
    code.className = `language-${effectivePrism} !bg-transparent !p-0`;
    code.textContent = content;
    pre.appendChild(code);
    host.appendChild(pre);
    try {
      Prism.highlightElement(code);
    } catch {
      /* keep unhighlighted text */
    }
    return () => {
      host.innerHTML = "";
    };
  }, [content, loading, isMarkdown, mdShowPreview, prismLang]);

  const handleCopy = async () => {
    if (content == null) return;
    await navigator.clipboard.writeText(content);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const lineCount = content ? content.split("\n").length : 0;

  if (err) {
    const fsz = typeof f.hostedFileSizeBytes === "number" ? f.hostedFileSizeBytes : 0;
    const maxB = typeof f.hostedFileMaxBytes === "number" ? f.hostedFileMaxBytes : 0;
    return (
      <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center text-center text-slate-600 dark:text-slate-400">
        <p className="mb-2 text-sm">{err}</p>
        {fsz > 0 && maxB > 0 ? (
          <p className="text-xs text-slate-500 dark:text-slate-500">
            ({(fsz / (1024 * 1024)).toFixed(2)} MB / max {(maxB / (1024 * 1024)).toFixed(0)} MB)
          </p>
        ) : null}
      </div>
    );
  }

  const hasTextSource = url.length > 0 || typeof inlineText === "string";
  if (!hasTextSource) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center text-sm text-slate-500 dark:text-slate-400">
        No text file loaded.
      </div>
    );
  }

  // Determine effective display language label
  const displayLang = languageOverride === "auto"
    ? langLabel
    : LANGUAGE_OPTIONS.find(opt => opt.value === languageOverride)?.label ?? langLabel;

  return (
    <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden bg-transparent">
      {/* Dynamic theme styles for Prism */}
      <style>{`
        /* Base code rendering: keep anti-aliased font clean and effect-free */
        [data-theme] code[class*="language-"],
        [data-theme] pre[class*="language-"],
        [data-theme] .token {
          text-shadow: none !important;
          filter: none !important;
        }

        /* Light theme: use the same high-contrast white code style as dark mode */
        [data-theme="light"] code[class*="language-"],
        [data-theme="light"] pre[class*="language-"] {
          color: #0f172a;
          background: transparent;
          text-shadow: none;
        }
        /* Ensure no background on any tokens */
        [data-theme="light"] .token {
          background: transparent !important;
        }
        [data-theme="light"] .token.comment,
        [data-theme="light"] .token.prolog,
        [data-theme="light"] .token.doctype,
        [data-theme="light"] .token.cdata {
          color: #64748b;
        }
        [data-theme="light"] .token.punctuation {
          color: #334155;
        }
        [data-theme="light"] .token.property,
        [data-theme="light"] .token.tag,
        [data-theme="light"] .token.constant,
        [data-theme="light"] .token.symbol,
        [data-theme="light"] .token.deleted {
          color: #dc2626;
        }
        [data-theme="light"] .token.boolean,
        [data-theme="light"] .token.number {
          color: #2563eb;
        }
        [data-theme="light"] .token.selector,
        [data-theme="light"] .token.attr-name,
        [data-theme="light"] .token.string,
        [data-theme="light"] .token.char,
        [data-theme="light"] .token.builtin,
        [data-theme="light"] .token.inserted {
          color: #059669;
        }
        [data-theme="light"] .token.operator,
        [data-theme="light"] .token.entity,
        [data-theme="light"] .token.url,
        [data-theme="light"] .language-css .token.string,
        [data-theme="light"] .style .token.string {
          color: #7c3aed;
        }
        [data-theme="light"] .token.atrule,
        [data-theme="light"] .token.attr-value,
        [data-theme="light"] .token.keyword {
          color: #1d4ed8;
        }
        [data-theme="light"] .token.function,
        [data-theme="light"] .token.class-name {
          color: #7e22ce;
        }
        [data-theme="light"] .token.regex,
        [data-theme="light"] .token.important,
        [data-theme="light"] .token.variable {
          color: #c2410c;
        }
        /* Dark theme token colors - high contrast on dark background */
        [data-theme="dark"] code[class*="language-"],
        [data-theme="dark"] pre[class*="language-"] {
          color: #f8f8f2;
          background: transparent;
          text-shadow: none;
        }
        /* Ensure no background on any tokens */
        [data-theme="dark"] .token {
          background: transparent !important;
        }
        [data-theme="dark"] .token.comment,
        [data-theme="dark"] .token.prolog,
        [data-theme="dark"] .token.doctype,
        [data-theme="dark"] .token.cdata {
          color: #a0a0a0;
        }
        [data-theme="dark"] .token.punctuation {
          color: #d4d4d4;
        }
        [data-theme="dark"] .token.property,
        [data-theme="dark"] .token.tag,
        [data-theme="dark"] .token.constant,
        [data-theme="dark"] .token.symbol,
        [data-theme="dark"] .token.deleted {
          color: #ff6b6b;
        }
        [data-theme="dark"] .token.boolean,
        [data-theme="dark"] .token.number {
          color: #bd93f9;
        }
        [data-theme="dark"] .token.selector,
        [data-theme="dark"] .token.attr-name,
        [data-theme="dark"] .token.string,
        [data-theme="dark"] .token.char,
        [data-theme="dark"] .token.builtin,
        [data-theme="dark"] .token.inserted {
          color: #a6e22e;
        }
        [data-theme="dark"] .token.operator,
        [data-theme="dark"] .token.entity,
        [data-theme="dark"] .token.url,
        [data-theme="dark"] .language-css .token.string,
        [data-theme="dark"] .style .token.string {
          color: #f8f8f2;
        }
        [data-theme="dark"] .token.atrule,
        [data-theme="dark"] .token.attr-value,
        [data-theme="dark"] .token.keyword {
          color: #66d9ef;
        }
        [data-theme="dark"] .token.function,
        [data-theme="dark"] .token.class-name {
          color: #e6db74;
        }
        [data-theme="dark"] .token.regex,
        [data-theme="dark"] .token.important,
        [data-theme="dark"] .token.variable {
          color: #fd971f;
        }
      `}</style>
      <div
        className="flex shrink-0 items-center justify-between gap-2 border-b py-1 px-2"
        style={{ borderColor: "var(--border)" }}
      >
        <div className="flex min-w-0 flex-1 items-center gap-2">
          {isCode ? (
            <FileCode className="h-4 w-4 shrink-0 text-violet-500" aria-hidden />
          ) : (
            <FileText className="h-4 w-4 shrink-0 text-slate-600" aria-hidden />
          )}
          <span className="truncate text-sm font-medium">{fileName}</span>
        </div>

        {/* Stats and controls */}
        <div className="flex items-center gap-2">
          <span className="hidden shrink-0 text-xs text-slate-700 sm:inline">
            {displayLang}
            {lineCount > 0 && !(isMarkdown && mdShowPreview) ? ` · ${lineCount} lines` : ""}
          </span>

          {/* Language override dropdown */}
          <div className="relative" ref={langDropdownRef}>
            <button
              type="button"
              className="flex shrink-0 items-center gap-1 rounded border px-2 py-1 text-xs text-slate-700 dark:text-slate-300 hover:bg-slate-100/50"
              style={{ borderColor: "var(--border)" }}
              title="Override language"
              onClick={() => setLangDropdownOpen(v => !v)}
            >
              <Type className="h-3.5 w-3.5" />
              <ChevronDown className="h-3 w-3" />
            </button>
            {langDropdownOpen && (
              <div
                className="absolute right-0 top-full z-50 mt-1 max-h-64 min-w-[140px] overflow-auto rounded border shadow-lg"
                style={{ borderColor: "var(--border)" }}
              >
                {LANGUAGE_OPTIONS.map((opt) => (
                  <button
                    key={opt.value}
                    type="button"
                    className={`block w-full px-3 py-1.5 text-left text-xs hover:bg-slate-100/50 ${
                      languageOverride === opt.value
                        ? "bg-slate-100/70 font-medium"
                        : ""
                    }`}
                    onClick={() => {
                      setLanguageOverride(opt.value);
                      setLangDropdownOpen(false);
                    }}
                  >
                    {opt.label}
                  </button>
                ))}
              </div>
            )}
          </div>

          {/* Line numbers toggle */}
          <button
            type="button"
            className={`shrink-0 rounded border px-2 py-1 ${
              showLineNumbers
                ? "bg-violet-100 text-violet-700"
                : "text-slate-700 dark:text-slate-300 hover:bg-slate-100/50"
            }`}
            style={{ borderColor: "var(--border)" }}
            title={showLineNumbers ? "Hide line numbers" : "Show line numbers"}
            onClick={() => setShowLineNumbers(v => !v)}
          >
            <Hash className="h-3.5 w-3.5" />
          </button>

          {/* Theme toggle */}
          <button
            type="button"
            className="shrink-0 rounded border px-2 py-1 text-slate-700 dark:text-slate-300 hover:bg-slate-100/50"
            style={{ borderColor: "var(--border)" }}
            title={isDarkTheme ? "Switch to light theme" : "Switch to dark theme"}
            onClick={() => setIsDarkTheme(v => !v)}
          >
            {isDarkTheme ? <Sun className="h-3.5 w-3.5" /> : <Moon className="h-3.5 w-3.5" />}
          </button>

          {isMarkdown ? (
            <button
              type="button"
              className="shrink-0 rounded border px-2 py-1 text-slate-700 dark:text-slate-300 hover:bg-slate-100/50"
              style={{ borderColor: "var(--border)" }}
              title={mdShowPreview ? "View source" : "View preview"}
              onClick={() => setMdShowPreview((v) => !v)}
            >
              {mdShowPreview ? <FileCode className="h-3.5 w-3.5" /> : <Eye className="h-3.5 w-3.5" />}
            </button>
          ) : null}
          <button
            type="button"
            className="shrink-0 rounded border px-2 py-1 text-slate-700 dark:text-slate-300 hover:bg-slate-100/50"
            style={{ borderColor: "var(--border)" }}
            title="Copy"
            onClick={() => void handleCopy()}
          >
            {copied ? <Check className="h-3.5 w-3.5 text-emerald-500" /> : <Copy className="h-3.5 w-3.5" />}
          </button>
        </div>
      </div>
      <div className="pm-scroll min-h-0 flex-1 overflow-auto">
        {loading ? (
          <div className="flex h-full items-center justify-center gap-2 text-slate-500">
            <Loader2 className="h-5 w-5 animate-spin" />
            <span className="text-sm">Loading…</span>
          </div>
        ) : parseErr ? (
          <div className="flex h-full items-center justify-center text-sm text-red-500">{parseErr}</div>
        ) : content !== null && isMarkdown && mdShowPreview ? (
          <div className="pm-scroll">
            <MarkdownRenderer content={content} baseUrl={markdownBaseFromHostedUrl(url)} />
          </div>
        ) : content !== null && isMarkdown && !mdShowPreview ? (
          <div
            className={`flex min-h-0 font-mono leading-relaxed ${
              isDarkTheme ? "bg-slate-950 text-slate-100" : "bg-white text-slate-900"
            }`}
            style={{ fontSize: `${BASE_FONT_SIZE_PX}px` }}
            data-theme={isDarkTheme ? "dark" : "light"}
          >
            {showLineNumbers && (
              <div
                className={`sticky left-0 shrink-0 select-none border-r py-2 text-right ${
                  isDarkTheme
                    ? "border-slate-700 bg-slate-900 text-slate-400"
                    : "border-slate-200 bg-slate-50 text-slate-500"
                }`}
                style={{ minWidth: "3rem", fontSize: `${BASE_FONT_SIZE_PX}px` }}
              >
                {content.split("\n").map((_, i) => (
                  <div key={i} className="px-2 leading-relaxed" style={{ lineHeight: "1.625" }}>
                    {i + 1}
                  </div>
                ))}
              </div>
            )}
            <div ref={codeHostRef} className="pm-scroll min-w-0 flex-1 overflow-auto px-4 py-2" />
          </div>
        ) : content !== null ? (
          <div
            className={`flex min-h-0 font-mono leading-relaxed ${
              isDarkTheme ? "bg-slate-950 text-slate-100" : "bg-white text-slate-900"
            }`}
            style={{ fontSize: `${BASE_FONT_SIZE_PX}px` }}
            data-theme={isDarkTheme ? "dark" : "light"}
          >
            {showLineNumbers && (
              <div
                className={`sticky left-0 shrink-0 select-none border-r py-2 text-right ${
                  isDarkTheme
                    ? "border-slate-700 bg-slate-900 text-slate-400"
                    : "border-slate-200 bg-slate-50 text-slate-500"
                }`}
                style={{ minWidth: "3rem", fontSize: `${BASE_FONT_SIZE_PX}px` }}
              >
                {content.split("\n").map((_, i) => (
                  <div key={i} className="px-2 leading-relaxed" style={{ lineHeight: "1.625" }}>
                    {i + 1}
                  </div>
                ))}
              </div>
            )}
            <div ref={codeHostRef} className="pm-scroll min-w-0 flex-1 overflow-auto px-4 py-2" />
          </div>
        ) : null}
      </div>
    </div>
  );
}
