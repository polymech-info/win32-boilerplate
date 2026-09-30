import { Link } from "@tanstack/react-router";
import { useEffect, useMemo, useState } from "react";
import type { MouseEvent } from "react";

import { MarkdownViewer } from "@viewer-next/viewers/markdown/MarkdownViewer";
import type { ViewerWebStatus } from "@viewer-next/bridge/hostBridge";

declare const __PM_HOME_HELP_README_URL__: string | undefined;

const DEFAULT_HELP_README_URL = "/help/en/readme.md";

function configuredHelpReadmeUrl(): string {
  if (typeof __PM_HOME_HELP_README_URL__ === "string" && __PM_HOME_HELP_README_URL__.trim()) {
    return __PM_HOME_HELP_README_URL__.trim();
  }
  return DEFAULT_HELP_README_URL;
}

function resolveHelpHref(href: string, currentUrl: string): string {
  try {
    return new URL(href, new URL(currentUrl, window.location.href)).toString();
  } catch {
    return href;
  }
}

function helpBreadcrumb(url: string): string[] {
  try {
    const parsed = new URL(url, window.location.href);
    const parts = parsed.pathname.split("/").filter(Boolean);
    return parts[0] === "help" ? parts : ["help", ...parts];
  } catch {
    return url.split(/[\\/]/).filter(Boolean);
  }
}

export function HomeHelp() {
  const [helpUrl, setHelpUrl] = useState(configuredHelpReadmeUrl);
  const [markdownText, setMarkdownText] = useState("");
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  useEffect(() => {
    const ac = new AbortController();
    setLoading(true);
    setError("");
    void fetch(helpUrl, { signal: ac.signal })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.text();
      })
      .then((text) => {
        if (!ac.signal.aborted) setMarkdownText(text);
      })
      .catch((err: unknown) => {
        if (ac.signal.aborted) return;
        setMarkdownText("");
        setError(err instanceof Error ? err.message : "Could not load help.");
      })
      .finally(() => {
        if (!ac.signal.aborted) setLoading(false);
      });
    return () => ac.abort();
  }, [helpUrl]);

  const status = useMemo<ViewerWebStatus>(
    () => ({
      selection: [],
      folder: "",
      viewerKind: "markdown",
      features: {
        markdownText,
        markdownBaseUrl: helpUrl,
      },
    }),
    [helpUrl, markdownText],
  );

  function onMarkdownClick(ev: MouseEvent<HTMLDivElement>) {
    const anchor = (ev.target as HTMLElement).closest("a");
    if (!anchor) return;
    const href = anchor.getAttribute("href") || "";
    if (!href || href.startsWith("#")) return;
    const nextUrl = resolveHelpHref(href, helpUrl);
    if (/\.md(?:[#?].*)?$/i.test(nextUrl)) {
      ev.preventDefault();
      setHelpUrl(nextUrl);
    }
  }

  return (
    <section className="home-help-page">
      <header className="home-help-topbar">
        <nav className="home-help-breadcrumb" aria-label="Help breadcrumb">
          <Link to="/" className="home-help-crumb">
            Home
          </Link>
          {helpBreadcrumb(helpUrl).map((part) => (
            <span key={part} className="home-help-crumb">
              {part}
            </span>
          ))}
        </nav>
      </header>
      <div className="home-help-viewer" onClick={onMarkdownClick}>
        {loading ? <p className="home-help-state">Loading help...</p> : null}
        {!loading && error ? <p className="home-help-state home-help-error">Could not load help: {error}</p> : null}
        {!loading && !error ? <MarkdownViewer status={status} /> : null}
      </div>
    </section>
  );
}
