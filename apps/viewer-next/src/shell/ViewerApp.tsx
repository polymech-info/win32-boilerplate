import { lazy, Suspense, useEffect, useRef, useState, type ReactNode } from "react";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { installPmViewerHost, onHostMessage, postToHost } from "@/bridge/hostBridge";
import { isEditorEligibleHostStatus } from "@/bridge/resolveHostedTextSource";
import { MarkdownViewer } from "@/viewers/markdown/MarkdownViewer";
import { PdfViewerPane } from "@/viewers/documents/PdfViewerPane";
import { SpreadsheetViewerPane } from "@/viewers/documents/SpreadsheetViewerPane";
import { TextViewerPane } from "@/viewers/text/TextViewerPane";
import { VideoViewerPane } from "@/viewers/video/VideoViewerPane";
import { HtmlViewerPane } from "@/viewers/html/HtmlViewerPane";
import { ImageViewerPane } from "@/viewers/image/ImageViewerPane";
import { AgentFlowViewerPane } from "@/viewers/flow/AgentFlowViewerPane";
import { MonacoEditorPane } from "@/viewers/editor/MonacoEditorPane";
import { PageViewerPane } from "@/UserPage/PageViewerPane";
import { ChevronDown, FileText, FileCode, FileSpreadsheet, FileVideo, FileBox, Globe, Bot, PenLine, Image, Newspaper } from "lucide-react";

const ThreeDViewerPane = lazy(() =>
  import("@/viewers/three/ThreeDViewer").then((m) => ({ default: m.ThreeDViewerPane })),
);

function defaultViewerKind() {
  if (typeof window !== "undefined") {
    const path = window.location.pathname.replace(/\/+$/, "");
    const params = new URLSearchParams(window.location.search);
    if (path === "/viewer/page" || params.get("viewer") === "page") return "page";
  }
  return "markdown";
}

const defaultStatus: ViewerWebStatus = {
  selection: [],
  folder: "",
  viewerKind: defaultViewerKind(),
  features: {},
};

type ViewerKind = string;

interface ViewerOption {
  id: ViewerKind;
  label: string;
  icon: React.ReactNode;
  description?: string;
}

const VIEWER_OPTIONS: ViewerOption[] = [
  { id: "markdown", label: "Markdown", icon: <FileText className="h-4 w-4" />, description: "Markdown preview with formatting" },
  { id: "text", label: "Text / Code", icon: <FileCode className="h-4 w-4" />, description: "Syntax highlighted source code" },
  { id: "pdf", label: "PDF", icon: <FileText className="h-4 w-4" />, description: "PDF document viewer" },
  { id: "spreadsheet", label: "Spreadsheet", icon: <FileSpreadsheet className="h-4 w-4" />, description: "CSV / Excel viewer" },
  { id: "video", label: "Video", icon: <FileVideo className="h-4 w-4" />, description: "Video player" },
  { id: "image", label: "Image", icon: <Image className="h-4 w-4" />, description: "SVG / browser image preview" },
  { id: "html", label: "HTML", icon: <Globe className="h-4 w-4" />, description: "Web page preview" },
  { id: "page", label: "Page", icon: <Newspaper className="h-4 w-4" />, description: "PixlWiz page JSON viewer" },
  { id: "three", label: "3D Model", icon: <FileBox className="h-4 w-4" />, description: "3D mesh / CAD viewer" },
  { id: "openscad", label: "OpenSCAD", icon: <FileBox className="h-4 w-4" />, description: "OpenSCAD preview via generated mesh" },
  { id: "agent-flow", label: "Agent Flow", icon: <Bot className="h-4 w-4" />, description: "Agent session visualization" },
  { id: "editor", label: "Editor", icon: <PenLine className="h-4 w-4" />, description: "Monaco code / text editor" },
];

const PREFER_EDITOR_FOR_TEXT_KEY = "pm-viewer-prefer-editor-text";

function readPreferEditorForText(): boolean {
  if (typeof window === "undefined") return false;
  return window.localStorage.getItem(PREFER_EDITOR_FOR_TEXT_KEY) === "1";
}

/** Host kind with optional default-to-editor preference for editable source files. */
function resolveHostViewerKind(status: ViewerWebStatus, preferEditorForText: boolean): string {
  const kind = status.viewerKind || defaultViewerKind();
  if (preferEditorForText && isEditorEligibleHostStatus(status)) return "editor";
  return kind;
}

export function ViewerApp() {
  const [status, setStatus] = useState<ViewerWebStatus>(defaultStatus);
  const [, setLoc] = useState("");
  const [fontExtraPt, setFontExtraPt] = useState(0);
  const [manualKind, setManualKind] = useState<ViewerKind | null>(null);
  const [dropdownOpen, setDropdownOpen] = useState(false);
  const [preferEditorForText, setPreferEditorForText] = useState(readPreferEditorForText);

  // Track the last selection path so we can distinguish a same-file reload
  // (tool write) from a genuine navigation to a new file.
  const lastSelectionPathRef = useRef("");

  useEffect(() => {
    installPmViewerHost({
      setStatus: (s) => {
        const newPath = Array.isArray(s.selection) && s.selection.length > 0
          ? s.selection[0]
          : "";
        const fileChanged = newPath !== lastSelectionPathRef.current;
        lastSelectionPathRef.current = newPath;
        setStatus({
          ...defaultStatus,
          ...s,
          selection: Array.isArray(s.selection) ? s.selection : [],
          viewerKind: s.viewerKind || defaultStatus.viewerKind,
          features:
            s.features !== undefined && s.features !== null && typeof s.features === "object"
              ? { ...defaultStatus.features, ...s.features }
              : defaultStatus.features,
        });
        // Only reset the manual viewer override when navigating to a different file.
        // Same-file reloads (e.g. triggered by an agent tool write) should preserve
        // the active viewer so the editor doesn't revert to markdown.
        if (fileChanged) setManualKind(null);
      },
      setLocale: (tag: string) => setLoc(tag),
      setFontExtraPt: (n: number) => setFontExtraPt(Number.isFinite(n) ? n : 0),
    });
  }, []);

  // Manual override wins; otherwise host kind (text → editor when preference is on).
  const hostKind = resolveHostViewerKind(status, preferEditorForText);
  const effectiveKind = manualKind || hostKind || defaultStatus.viewerKind;
  const hostEditorEligible = isEditorEligibleHostStatus(status);

  const togglePreferEditorForText = () => {
    setPreferEditorForText((prev) => {
      const next = !prev;
      if (typeof window !== "undefined") {
        window.localStorage.setItem(PREFER_EDITOR_FOR_TEXT_KEY, next ? "1" : "0");
      }
      return next;
    });
    // Let the preference apply immediately on the current editable file.
    if (hostEditorEligible) setManualKind(null);
  };

  // Notify the Win32 host when the editor is active so it can suppress
  // centre-preview arrow-key navigation (which would steal cursor movement).
  useEffect(() => {
    postToHost({ t: "vw_editor_mode", active: effectiveKind === "editor" });
  }, [effectiveKind]);

  // Apply host theme to <html> so Tailwind dark: classes activate.
  // Mirrors Win32 CViewerWebPanel::PushThemeToWeb — host is authoritative.
  useEffect(() => {
    const theme = status.features?.theme;
    if (theme === "dark" || theme === "light") {
      document.documentElement.classList.toggle("dark", theme === "dark");
    }
  }, [status.features?.theme]);

  // Log when the host confirms a file was reloaded because an agent tool wrote it.
  useEffect(() => {
    return onHostMessage((data) => {
      if (data.t !== "vw_tool_file_reloaded") return;
      const p = typeof data.path === "string" ? data.path : "";
      const name = p.replace(/\\/g, "/").split("/").pop() ?? p;
      console.info(`[viewer] reloaded by agent tool: ${name}`, p);
    });
  }, []);
  
  const basePt = 14 + Math.min(4, Math.max(0, fontExtraPt));
  
  // All viewers now use edge-to-edge layout with unified toolbar
  const edgeToEdgeShell = true;

  const currentOption = VIEWER_OPTIONS.find((v) => v.id === effectiveKind) || VIEWER_OPTIONS[0];
  const fileName = status.features?.hostedFileName || status.selection?.[0]?.split(/[/\\]/).pop() || "Viewer";

  let body: ReactNode;
  if (effectiveKind === "three" || effectiveKind === "openscad") {
    body = (
      <Suspense
        fallback={
          <div className="flex h-full min-h-0 flex-1 items-center justify-center p-6 text-sm text-slate-500 dark:text-slate-400">
            Loading 3D viewer...
          </div>
        }
      >
        <ThreeDViewerPane status={status} />
      </Suspense>
    );
  }
  else if (effectiveKind === "pdf") body = <PdfViewerPane status={status} />;
  else if (effectiveKind === "video") body = <VideoViewerPane status={status} />;
  else if (effectiveKind === "image") body = <ImageViewerPane status={status} />;
  else if (effectiveKind === "spreadsheet") body = <SpreadsheetViewerPane status={status} />;
  else if (effectiveKind === "text") body = <TextViewerPane status={status} />;
  else if (effectiveKind === "html") body = <HtmlViewerPane status={status} />;
  else if (effectiveKind === "page") body = <PageViewerPane status={status} />;
  else if (effectiveKind === "agent-flow") body = <AgentFlowViewerPane status={status} />;
  else if (effectiveKind === "editor") body = <MonacoEditorPane status={status} />;
  else body = <MarkdownViewer status={status} />;

  return (
    <div
      className={
        "flex h-full min-h-0 min-h-dvh flex-1 flex-col text-slate-900 antialiased dark:text-zinc-200"
      }
      style={{ fontSize: `${basePt}px` }}
    >
      {/* Global Toolbar */}
      <div className="flex shrink-0 items-center justify-between gap-3 border-b border-slate-200 px-3 py-2 shadow-sm dark:border-surface-mute/50">
        {/* Left: File info */}
        <div className="flex min-w-0 items-center gap-2">
          <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded">
            {currentOption.icon}
          </div>
          <div className="min-w-0">
            <div className="truncate text-sm font-medium">{fileName}</div>
          </div>
        </div>

        {/* Center: Viewer mode selector + text→editor default */}
        <div className="flex items-center gap-2">
          <div className="relative">
            <button
              type="button"
              onClick={() => setDropdownOpen(!dropdownOpen)}
              className="flex items-center gap-2 rounded-md border border-slate-300 px-3 py-1.5 text-sm hover:bg-slate-50/50 dark:border-surface-mute/50"
            >
              <span className="flex items-center gap-1.5">
                {currentOption.icon}
                {currentOption.label}
              </span>
              <ChevronDown className={`h-4 w-4 transition-transform ${dropdownOpen ? "rotate-180" : ""}`} />
            </button>

            {dropdownOpen && (
              <>
                <div
                  className="fixed inset-0 z-40"
                  onClick={() => setDropdownOpen(false)}
                />
                <div className="absolute left-1/2 top-full z-50 mt-1 w-56 -translate-x-1/2 rounded-md border border-slate-200 bg-white py-1 shadow-lg dark:border-surface-mute/50 dark:bg-surface-dimmed">
                  <div className="px-3 py-1.5 text-xs font-semibold text-slate-500 dark:text-slate-400">
                    Select viewer
                  </div>
                  {VIEWER_OPTIONS.map((option) => (
                    <button
                      key={option.id}
                      type="button"
                      onClick={() => {
                        setManualKind(option.id);
                        setDropdownOpen(false);
                      }}
                      className={`flex w-full items-center gap-2 px-3 py-2 text-left text-sm hover:bg-slate-100/50 ${
                        effectiveKind === option.id ? "bg-blue-50/70 text-blue-700 dark:text-zinc-200" : ""
                      }`}
                    >
                      <span className="flex h-5 w-5 items-center justify-center opacity-70">
                        {option.icon}
                      </span>
                      <div className="flex flex-col">
                        <span className="font-medium">{option.label}</span>
                        {option.description && (
                          <span className="text-[10px] text-slate-500 dark:text-slate-400">
                            {option.description}
                          </span>
                        )}
                      </div>
                    </button>
                  ))}
                </div>
              </>
            )}
          </div>

          <button
            type="button"
            aria-pressed={preferEditorForText}
            title={
              preferEditorForText
                ? "Text / OpenSCAD / DXF open in Editor (click for preview viewers)"
                : "Open text / OpenSCAD / DXF in Editor by default"
            }
            onClick={togglePreferEditorForText}
            className={`flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm transition-colors ${
              preferEditorForText
                ? "border-blue-500 bg-blue-50/80 text-blue-700 dark:border-blue-400 dark:bg-blue-950/40 dark:text-blue-200"
                : "border-slate-300 text-slate-700 hover:bg-slate-50/50 dark:border-surface-mute/50 dark:text-slate-300"
            } ${hostEditorEligible || effectiveKind === "editor" ? "" : "opacity-60"}`}
          >
            <PenLine className="h-4 w-4" />
            <span className="hidden sm:inline">Editor</span>
          </button>
        </div>

        {/* Right: Stats */}
        <div className="flex shrink-0 items-center gap-2 text-xs text-slate-700 dark:text-slate-400">
          {status.features?.hostedFileSizeBytes && (
            <span>{(status.features.hostedFileSizeBytes / 1024).toFixed(1)} KB</span>
          )}
        </div>
      </div>

      {/* Content area */}
      <div className={`flex min-h-0 flex-1 flex-col overflow-hidden ${edgeToEdgeShell ? "p-0" : "p-2"}`}>
        {body}
      </div>
    </div>
  );
}
