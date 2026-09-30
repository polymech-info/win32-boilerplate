import { displayPageSrc, isUserPagePayload, mergePageVariables, pageFetchUrl, pageSrcFromLocation, resolvePageLayout } from "@/UserPage/pageData";
import { PageLayoutView } from "@/UserPage/PageLayoutView";
import type { Page, UserPagePayload } from "@/UserPage/types";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedText } from "@/bridge/hostedFileFetch";
import { MarkdownRenderer } from "@/viewers/markdown/MarkdownRenderer";
import { TableOfContents } from "@/viewers/markdown/TableOfContents";
import { extractHeadings, type MarkdownHeading } from "@/viewers/markdown/toc";
import { Loader2, PanelLeftClose, PanelLeftOpen } from "lucide-react";
import { useEffect, useMemo, useState } from "react";

export function PageViewerPane({ status }: { status: ViewerWebStatus }) {
  return <PageViewerInner status={status} />;
}

function PageViewerInner({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const hostedError = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const hostedUrl = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  const routeSrc = pageSrcFromLocation();
  const routeUrl = routeSrc ? pageFetchUrl(routeSrc) : "";
  const inlineText =
    typeof f.hostedFileText === "string"
      ? f.hostedFileText
      : typeof f.markdownText === "string"
        ? f.markdownText
        : undefined;
  const fileName = typeof f.hostedFileName === "string" ? f.hostedFileName : "page.json";

  const [content, setContent] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [loadError, setLoadError] = useState<string | null>(null);

  useEffect(() => {
    if (typeof inlineText === "string") {
      setContent(inlineText);
      setLoading(false);
      setLoadError(null);
      return;
    }

    const url = hostedUrl || routeUrl || pageFetchUrl("");
    const ac = new AbortController();
    (async () => {
      setLoading(true);
      setLoadError(null);
      setContent(null);
      try {
        const text = hostedUrl ? await fetchHostedText(url, ac.signal) : await fetch(url, { signal: ac.signal }).then((r) => {
          if (!r.ok) throw new Error(`HTTP ${r.status}`);
          return r.text();
        });
        setContent(text);
      } catch (error) {
        if (ac.signal.aborted) return;
        setLoadError(error instanceof Error ? error.message : "Failed to load page");
      } finally {
        if (!ac.signal.aborted) setLoading(false);
      }
    })();
    return () => ac.abort();
  }, [hostedUrl, inlineText, routeUrl]);

  const parsed = useMemo(() => parsePagePayload(content), [content]);

  if (hostedError) {
    return <CenteredMessage message={hostedError} />;
  }
  if (loading) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center gap-2 text-slate-500">
        <Loader2 className="h-5 w-5 animate-spin" />
        <span className="text-sm">Loading page...</span>
      </div>
    );
  }
  if (loadError) {
    return <CenteredMessage message={loadError} detail={hostedUrl ? fileName : displayPageSrc(routeSrc)} tone="error" />;
  }
  if (!parsed.ok) {
    return <CenteredMessage message={parsed.error ?? "No page data loaded"} detail={fileName} tone="error" />;
  }

  return <UserPageView payload={parsed.payload} />;
}

function UserPageView({ payload }: { payload: UserPagePayload }) {
  const { page, userProfile, childPages = [], userVariables = {} } = payload;
  const contextVariables = useMemo(() => mergePageVariables(page, userVariables), [page, userVariables]);
  const showToc = contextVariables.showToc !== false && contextVariables.showToc !== "false";
  const showTitle = contextVariables.showTitle !== false && contextVariables.showTitle !== "false";
  const showLastUpdated = contextVariables.showLastUpdated !== false && contextVariables.showLastUpdated !== "false";
  const layout = useMemo(() => resolvePageLayout(page), [page]);
  const headings = useMemo(() => extractPageHeadings(page), [page]);
  const [tocOpen, setTocOpen] = useState(true);
  const hasToc = showToc && headings.some((heading) => heading.depth >= 2 && heading.depth <= 4);

  return (
    <div className={`pm-page-shell ${tocOpen && hasToc ? "pm-page-shell--toc-open" : ""}`}>
      {hasToc ? (
        <aside className="pm-page-toc-panel" aria-label="Page table of contents">
          <div className="pm-md-toc-head">
            <span>On this page</span>
            <button type="button" className="pm-md-toc-toggle" onClick={() => setTocOpen(false)} title="Hide table of contents">
              <PanelLeftClose aria-hidden="true" />
            </button>
          </div>
          {childPages.length > 0 ? (
            <div className="border-b border-slate-200 px-3 py-3 dark:border-slate-800">
              <div className="mb-2 text-[10px] font-semibold uppercase tracking-wide text-slate-500 dark:text-slate-400">
                Child Pages
              </div>
              <div className="space-y-1">
                {childPages.map((child) => (
                  <a key={child.id} className="block truncate text-xs text-slate-600 hover:text-blue-600 dark:text-slate-300" href={`#${child.slug}`}>
                    {child.title}
                  </a>
                ))}
              </div>
            </div>
          ) : null}
          <TableOfContents headings={headings} className="pm-md-toc" />
        </aside>
      ) : null}
      {hasToc ? (
        <button
          type="button"
          className="pm-md-toc-fab"
          onClick={() => setTocOpen((open) => !open)}
          title={tocOpen ? "Hide table of contents" : "Show table of contents"}
          aria-label={tocOpen ? "Hide table of contents" : "Show table of contents"}
        >
          {tocOpen ? <PanelLeftClose aria-hidden="true" /> : <PanelLeftOpen aria-hidden="true" />}
        </button>
      ) : null}

      <main className="pm-scroll min-h-0 flex-1 overflow-auto">
        <article className="mx-auto w-full max-w-5xl px-3 py-5 md:px-8 md:py-8">
          {showTitle ? (
            <header className="mb-6 border-b border-slate-200 pb-4 dark:border-slate-800">
              <h1 className="text-2xl font-semibold tracking-tight text-slate-950 dark:text-slate-50">{page.title}</h1>
              {userProfile?.display_name || userProfile?.username ? (
                <div className="mt-2 flex items-center gap-2 text-sm text-slate-500 dark:text-slate-400">
                  {userProfile.avatar_url ? <img className="h-6 w-6 rounded-full" src={userProfile.avatar_url} alt="" /> : null}
                  <span>{userProfile.display_name || userProfile.username}</span>
                </div>
              ) : null}
            </header>
          ) : null}

          {typeof page.content === "string" ? (
            <MarkdownRenderer content={page.content} variables={contextVariables} />
          ) : layout ? (
            <PageLayoutView layout={layout} page={page} contextVariables={contextVariables} />
          ) : (
            <CenteredMessage message="This page has no renderable content." />
          )}

          {showLastUpdated && page.updated_at ? (
            <footer className="mt-8 border-t border-slate-200 pt-4 text-sm text-slate-500 dark:border-slate-800 dark:text-slate-400">
              Last updated: {new Date(page.updated_at).toLocaleString()}
            </footer>
          ) : null}
        </article>
      </main>
    </div>
  );
}

function extractPageHeadings(page: Page): MarkdownHeading[] {
  if (typeof page.content === "string") return extractHeadings(page.content);
  const layout = resolvePageLayout(page);
  if (!layout) return [];
  const headings: MarkdownHeading[] = [];
  const visitContainers = (containers: PageLayout["containers"]) => {
    for (const container of containers) {
      for (const widget of container.widgets) {
        if (widget.widgetId === "markdown-text" && typeof widget.props?.content === "string") {
          headings.push(...extractHeadings(widget.props.content));
        }
      }
      if (container.type === "container") visitContainers(container.children ?? []);
    }
  };
  visitContainers(layout.containers);
  return headings;
}

function parsePagePayload(content: string | null): { ok: true; payload: UserPagePayload } | { ok: false; error?: string } {
  if (!content) return { ok: false };
  try {
    const parsed = JSON.parse(content) as unknown;
    if (!isUserPagePayload(parsed)) return { ok: false, error: "JSON does not look like a PixlWiz page payload." };
    return { ok: true, payload: parsed };
  } catch (error) {
    return { ok: false, error: error instanceof Error ? `Invalid JSON: ${error.message}` : "Invalid JSON" };
  }
}

function CenteredMessage({ message, detail, tone }: { message: string; detail?: string; tone?: "error" }) {
  return (
    <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center gap-2 px-6 text-center">
      <div className={`text-sm ${tone === "error" ? "text-red-500" : "text-slate-500 dark:text-slate-400"}`}>{message}</div>
      {detail ? <div className="max-w-xl break-all text-xs text-slate-500 dark:text-slate-500">{detail}</div> : null}
    </div>
  );
}
