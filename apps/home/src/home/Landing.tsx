import { useNavigate, useRouterState } from "@tanstack/react-router";
import { useCallback, useEffect, useMemo, useState } from "react";

import { recordSessionVisited } from "@/home/recentSessions";
import { deleteChatSession, listChatSessionsMeta, loadChatSessionFull } from "@pm/shared/chat/chatSessionsBackend";
import type { ChatSessionMeta, StoredChatSession } from "@pm/shared/chat/chatSessionStorage";
import { hasWebProviderHost, postHost } from "@pm/shared/chat/hostBridge";
import { rewriteLocalPath } from "@pm/shared/chat/pathRewrite";

type HomeTab = "history" | "discover";
type MruPath = { path: string; label: string; kind: "folder" | "file"; updatedAt: number };
type SessionArtifacts = { files: string[] };

const homeLabels = {
  newSession: "New session",
  pastSessionsTitle: "Past sessions",
  pastSessionsHint: "Continue in the Chat panel or remove old work.",
  recentFoldersTitle: "Most recently used folders",
  recentFoldersHint: "Open in Explorer or reveal in the file tree.",
  recentFilesTitle: "Most recently used files",
  recentFilesHint: "Open in the center viewer or locate in Explorer.",
};

function formatSessionDate(ts: number): string {
  if (!ts) return "now";
  const diffMs = Math.max(0, Date.now() - ts);
  const mins = Math.floor(diffMs / 60_000);
  if (mins < 1) return "now";
  if (mins < 60) return `${mins} min${mins === 1 ? "" : "s"} ago`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `${hours} hour${hours === 1 ? "" : "s"} ago`;
  const days = Math.min(3, Math.floor(hours / 24));
  return `${days} day${days === 1 ? "" : "s"} ago`;
}

function basename(path: string): string {
  const clean = path.replace(/[\\/]+$/, "");
  return clean.split(/[\\/]/).pop() || clean;
}

function thumbSrc(path: string): string {
  if (/^(https?:|data:)/i.test(path)) return path;
  if (path.startsWith("/")) return path;
  return rewriteLocalPath(path);
}

function isLikelyImagePath(path: string): boolean {
  return /\.(jpe?g|png|gif|webp|avif|svg|bmp|heic|heif)(\?.*)?$/i.test(basename(path));
}

function parentFolder(path: string): string {
  const clean = path.replace(/[\\/]+$/, "");
  const i = Math.max(clean.lastIndexOf("\\"), clean.lastIndexOf("/"));
  return i > 0 ? clean.slice(0, i) : clean;
}

function dedupeMru(paths: MruPath[], limit: number): MruPath[] {
  const seen = new Set<string>();
  const out: MruPath[] = [];
  for (const row of paths.sort((a, b) => b.updatedAt - a.updatedAt)) {
    const key = `${row.kind}:${row.path.toLowerCase()}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(row);
    if (out.length >= limit) break;
  }
  return out;
}

function collectMruFromSessions(sessions: StoredChatSession[]): { folders: MruPath[]; files: MruPath[] } {
  const folders: MruPath[] = [];
  const files: MruPath[] = [];
  for (const session of sessions) {
    for (const entry of session.entries) {
      const updatedAt = entry.ts || session.updatedAt || Date.now();
      if (entry.folderHint?.trim()) {
        const path = entry.folderHint.trim();
        folders.push({ kind: "folder", path, label: basename(path), updatedAt });
      }
      for (const path of [...(entry.contextPaths || []), ...(entry.resultPaths || [])]) {
        const p = path.trim();
        if (!p) continue;
        files.push({ kind: "file", path: p, label: basename(p), updatedAt });
        const folder = parentFolder(p);
        if (folder && folder !== p) folders.push({ kind: "folder", path: folder, label: basename(folder), updatedAt });
      }
    }
  }
  return {
    folders: dedupeMru(folders, 6),
    files: dedupeMru(files, 8),
  };
}

function collectSessionArtifacts(session: StoredChatSession): SessionArtifacts {
  const seen = new Set<string>();
  const files: string[] = [];
  const push = (path: string | undefined) => {
    const p = String(path || "").trim();
    if (!p) return;
    const key = p.toLowerCase();
    if (seen.has(key)) return;
    seen.add(key);
    files.push(p);
  };
  for (const entry of session.entries) {
    if (entry.role === "file" || entry.role === "image") push(entry.text);
    for (const path of entry.resultPaths || []) push(path);
  }
  return { files };
}

function postPathCommand(kind: string, path: string): void {
  if (hasWebProviderHost()) postHost({ kind, path });
}

function openSettings() {
  if (hasWebProviderHost()) postHost({ kind: "settings" });
}

function ArtifactChip({ path }: { path: string }) {
  const [failed, setFailed] = useState(false);
  const name = basename(path);
  const showImage = isLikelyImagePath(path) && !failed;
  return (
    <button
      type="button"
      title={path}
      className={showImage ? "home-session-file-thumb" : "home-session-file-chip"}
      onClick={() => postPathCommand("openPathInternal", path)}
    >
      {showImage ? (
        <>
          <img src={thumbSrc(path)} alt="" draggable={false} onError={() => setFailed(true)} />
          <span>{name}</span>
        </>
      ) : (
        name
      )}
    </button>
  );
}

/**
 * Home hub: recent sessions now, manuals and richer cards later.
 */
export function HomeLanding() {
  const navigate = useNavigate();
  const pathname = useRouterState({ select: (s) => s.location.pathname });
  const [tab, setTab] = useState<HomeTab>("history");
  const [sessions, setSessions] = useState<ChatSessionMeta[]>([]);
  const [sessionArtifacts, setSessionArtifacts] = useState<Record<string, SessionArtifacts>>({});
  const [mru, setMru] = useState<{ folders: MruPath[]; files: MruPath[] }>({ folders: [], files: [] });
  const [loading, setLoading] = useState(true);

  const refreshSessions = useCallback(() => {
    setLoading(true);
    void listChatSessionsMeta()
      .then(async (rows) => {
        const visibleRows = rows.slice(0, 12);
        setSessions(visibleRows);
        const loaded = await Promise.all(visibleRows.slice(0, 8).map((row) => loadChatSessionFull(row.id)));
        const loadedSessions = loaded.filter((row): row is StoredChatSession => !!row);
        setMru(collectMruFromSessions(loadedSessions));
        setSessionArtifacts(
          Object.fromEntries(loadedSessions.map((session) => [session.id, collectSessionArtifacts(session)])),
        );
      })
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    refreshSessions();
  }, [refreshSessions, pathname]);

  function newSession() {
    const sessionId = crypto.randomUUID();
    openInChat(sessionId);
  }

  function openInChat(sessionId: string) {
    if (hasWebProviderHost()) {
      postHost({ kind: "openChatSession", sessionId });
      return;
    }
    recordSessionVisited(sessionId);
    void navigate({ to: "/home/$sessionId", params: { sessionId } });
  }

  async function removeSession(sessionId: string) {
    await deleteChatSession(sessionId);
    setSessions((rows) => rows.filter((row) => row.id !== sessionId));
    setSessionArtifacts((rows) => {
      const next = { ...rows };
      delete next[sessionId];
      return next;
    });
    refreshSessions();
  }

  const sessionRows = useMemo(() => sessions, [sessions]);

  function openHelp() {
    void navigate({ to: "/help" });
  }

  return (
    <section className="home-dashboard">
      <header className="home-topbar">
        <div>
          <h1>Home</h1>
          <p>Recent work, sessions, and shortcuts.</p>
        </div>
        <div className="home-top-actions">
          <button type="button" className="home-gear" title="Info" aria-label="Info" onClick={() => openHelp()}>
            <svg viewBox="0 0 24 24" aria-hidden="true">
              <path d="M12 17v-6" />
              <path d="M12 7h.01" />
              <path d="M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20Z" />
            </svg>
          </button>
          <button type="button" className="home-gear" title="Settings" aria-label="Settings" onClick={() => openSettings()}>
            <svg viewBox="0 0 24 24" aria-hidden="true">
              <path d="M12 15.5A3.5 3.5 0 1 0 12 8a3.5 3.5 0 0 0 0 7.5Z" />
              <path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1-2.2 2.2-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5V20h-3.1v-.2a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1-2.2-2.2.1-.1A1.7 1.7 0 0 0 4.6 15a1.7 1.7 0 0 0-1.5-1H3v-3.1h.2a1.7 1.7 0 0 0 1.5-1 1.7 1.7 0 0 0-.3-1.9l-.1-.1 2.2-2.2.1.1a1.7 1.7 0 0 0 1.9.3 1.7 1.7 0 0 0 1-1.5V4h3.1v.2a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1 2.2 2.2-.1.1a1.7 1.7 0 0 0-.3 1.9 1.7 1.7 0 0 0 1.5 1h.2V14h-.2a1.7 1.7 0 0 0-1.5 1Z" />
            </svg>
          </button>
          <button type="button" className="home-primary" onClick={() => newSession()}>
            {homeLabels.newSession}
          </button>
        </div>
      </header>

      <nav className="home-tabs" aria-label="Home sections">
        <button type="button" className={tab === "history" ? "active" : ""} onClick={() => setTab("history")}>
          History
        </button>
        <button type="button" className={tab === "discover" ? "active" : ""} onClick={() => setTab("discover")}>
          Discover
        </button>
      </nav>

      {tab === "history" ? (
        <div className="home-grid">
          <section className="home-section home-section--wide">
            <div className="home-section-head">
              <div>
                <h2>{homeLabels.pastSessionsTitle}</h2>
                <p>{homeLabels.pastSessionsHint}</p>
              </div>
              <span>{loading ? "Loading" : `${sessionRows.length} shown`}</span>
            </div>
            <div className="home-card">
              <ul className="home-list">
                {!loading && sessionRows.length === 0 ? <li className="home-empty">No saved sessions yet.</li> : null}
                {sessionRows.map((s) => {
                  const files = sessionArtifacts[s.id]?.files || [];
                  const shownFiles = files.slice(0, 4);
                  return (
                    <li key={s.id} className="home-session-item">
                      <div className="home-row">
                        <button type="button" className="home-row-main" onClick={() => openInChat(s.id)}>
                          <span>{s.title || "Chat"}</span>
                          <small>{formatSessionDate(s.updatedAt)}</small>
                        </button>
                        <div className="home-row-actions">
                          <button type="button" title="Open in Chat" aria-label="Open in Chat" onClick={() => openInChat(s.id)}>
                            <svg viewBox="0 0 24 24" aria-hidden="true">
                              <path d="M5 12h12" />
                              <path d="m13 6 6 6-6 6" />
                            </svg>
                          </button>
                          <button type="button" title="Delete session" aria-label="Delete session" onClick={() => void removeSession(s.id)}>
                            <svg viewBox="0 0 24 24" aria-hidden="true">
                              <path d="M4 7h16" />
                              <path d="M10 11v6" />
                              <path d="M14 11v6" />
                              <path d="M6 7l1 14h10l1-14" />
                              <path d="M9 7V4h6v3" />
                            </svg>
                          </button>
                        </div>
                      </div>
                      {shownFiles.length ? (
                        <div className="home-session-files" aria-label="Changed files">
                          {shownFiles.map((path) => (
                            <ArtifactChip key={path} path={path} />
                          ))}
                          {files.length > shownFiles.length ? <em>+{files.length - shownFiles.length}</em> : null}
                        </div>
                      ) : null}
                    </li>
                  );
                })}
              </ul>
            </div>
          </section>

          <section className="home-section">
            <div className="home-section-head">
              <div>
                <h2>{homeLabels.recentFoldersTitle}</h2>
                <p>{homeLabels.recentFoldersHint}</p>
              </div>
            </div>
            <div className="home-card">
              <ul className="home-list home-list--compact">
                {mru.folders.length === 0 ? <li className="home-empty">Folders will appear from chat context.</li> : null}
                {mru.folders.map((row) => (
                  <li key={row.path} className="home-path-row" title={row.path}>
                    <button type="button" className="home-path-main" onClick={() => postPathCommand("openFolderInExplorer", row.path)}>
                      <span>{row.label}</span>
                      <small>{row.path}</small>
                    </button>
                    <button type="button" className="home-icon-btn" onClick={() => postPathCommand("selectPathInExplorer", row.path)}>
                      Reveal
                    </button>
                  </li>
                ))}
              </ul>
            </div>
          </section>

          <section className="home-section">
            <div className="home-section-head">
              <div>
                <h2>{homeLabels.recentFilesTitle}</h2>
                <p>{homeLabels.recentFilesHint}</p>
              </div>
            </div>
            <div className="home-card">
              <ul className="home-list home-list--compact">
                {mru.files.length === 0 ? <li className="home-empty">Files will appear from chat context.</li> : null}
                {mru.files.map((row) => (
                  <li key={row.path} className={`home-path-row${isLikelyImagePath(row.path) ? " home-path-row--image" : ""}`} title={row.path}>
                    <button type="button" className="home-path-main" onClick={() => postPathCommand("openPathInternal", row.path)}>
                      {isLikelyImagePath(row.path) ? (
                        <span className="home-path-thumb">
                          <img src={thumbSrc(row.path)} alt="" draggable={false} />
                        </span>
                      ) : null}
                      <span>{row.label}</span>
                      <small>{row.path}</small>
                    </button>
                    <button type="button" className="home-icon-btn" onClick={() => postPathCommand("selectPathInExplorer", row.path)}>
                      Reveal
                    </button>
                  </li>
                ))}
              </ul>
            </div>
          </section>
        </div>
      ) : (
        <div className="home-discover">
          <button type="button">Featured workflows</button>
          <button type="button">Templates</button>
          <button type="button">Examples</button>
          <button type="button">Integrations</button>
        </div>
      )}
    </section>
  );
}
