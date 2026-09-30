import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { MarkdownRenderer } from "@/viewers/markdown/MarkdownRenderer";
import { TableOfContents } from "@/viewers/markdown/TableOfContents";
import { extractHeadings } from "@/viewers/markdown/toc";
import { PanelLeftClose, PanelLeftOpen } from "lucide-react";
import { useMemo, useState } from "react";

const TOC_OPEN_KEY = "pm.viewer.markdown.toc.open";

function readTocOpen(): boolean {
  if (typeof window === "undefined") return true;
  const stored = window.localStorage.getItem(TOC_OPEN_KEY);
  return stored === null ? true : stored === "1";
}

export function MarkdownViewer({ status }: { status: ViewerWebStatus }) {
  const [tocOpen, setTocOpen] = useState(readTocOpen);
  // Support both markdownText (primary) and hostedFileText (fallback for viewer switching)
  const text = typeof status.features?.markdownText === "string" 
    ? status.features.markdownText 
    : typeof status.features?.hostedFileText === "string" 
    ? status.features.hostedFileText 
    : undefined;
  const headings = useMemo(() => (typeof text === "string" ? extractHeadings(text) : []), [text]);
  if (typeof text === "string") {
    const base = status.features?.markdownBaseUrl;
    const showToc = headings.some((h) => h.depth >= 2 && h.depth <= 4);
    const toggleToc = () => {
      setTocOpen((open) => {
        const next = !open;
        window.localStorage.setItem(TOC_OPEN_KEY, next ? "1" : "0");
        return next;
      });
    };
    return (
      <div className={`pm-md-shell ${tocOpen && showToc ? "pm-md-shell--toc-open" : ""}`}>
        {showToc ? (
          <aside className="pm-md-toc-panel" aria-label="Markdown table of contents">
            <div className="pm-md-toc-head">
              <span>On this page</span>
              <button type="button" className="pm-md-toc-toggle" onClick={toggleToc} title="Hide table of contents">
                <PanelLeftClose aria-hidden="true" />
              </button>
            </div>
            <TableOfContents headings={headings} className="pm-md-toc" />
          </aside>
        ) : null}
        {showToc ? (
          <button
            type="button"
            className="pm-md-toc-fab"
            onClick={toggleToc}
            title={tocOpen ? "Hide table of contents" : "Show table of contents"}
            aria-label={tocOpen ? "Hide table of contents" : "Show table of contents"}
          >
            {tocOpen ? <PanelLeftClose aria-hidden="true" /> : <PanelLeftOpen aria-hidden="true" />}
          </button>
        ) : null}
        <div className="pm-scroll pm-md-scroll min-h-0 flex-1 overflow-auto px-1 pb-4 pt-0.5 sm:px-2">
          <MarkdownRenderer content={text} baseUrl={typeof base === "string" ? base : undefined} />
        </div>
      </div>
    );
  }

  const embed = status.features?.markdownEmbedUrl;
  if (typeof embed === "string" && embed.length > 0) {
    return (
      <iframe
        title="Markdown preview"
        src={embed}
        className="block h-[calc(100dvh-12px)] w-full rounded-md border-0 bg-[#0d0d0f]"
        sandbox="allow-scripts allow-same-origin"
      />
    );
  }

  return (
    <div/>
  );
}
