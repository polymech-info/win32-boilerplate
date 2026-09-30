import { useState, useRef, useEffect } from "react";
import { useShellStore, type ShellType } from "./shellStore";
import { markShellClosed } from "./consoleRun";
import { Terminal } from "./Terminal";
import {
  Plus,
  X,
  Trash2,
  ChevronRight,
  ChevronDown,
  Settings,
  MoreHorizontal,
  Terminal as TerminalIcon,
  Moon,
  Sun,
  Monitor,
} from "lucide-react";

const shellTypes: { type: ShellType; label: string; icon: string }[] = [
  { type: "powershell", label: "PowerShell", icon: "⚡" },
  { type: "cmd", label: "Command Prompt", icon: "⌘" },
  { type: "bash", label: "Bash", icon: "$" },
  { type: "wsl", label: "WSL", icon: "🐧" },
  { type: "git-bash", label: "Git Bash", icon: "🌳" },
];

type ConsoleDraft = {
  shellId: string;
  text: string;
};

type ImagePreview = {
  path: string;
  ts: number;
};

function imageThumbSrc(path: string): string {
  if (/^(https?:|data:)/i.test(path)) return path;
  const match = path.match(/^([a-zA-Z]):[\\/](.*)$/);
  if (!match) return path;
  return `https://pm-files-${match[1].toLowerCase()}.local/${match[2].replace(/\\/g, "/")}`;
}

function fileName(path: string): string {
  return path.replace(/\\/g, "/").split("/").pop() || path;
}

function postHostCommand(payload: Record<string, unknown>): void {
  window.chrome?.webview?.postMessage?.(JSON.stringify({ t: "cweb_bus", payload: { t: "cweb_host", ...payload } }));
}

export function ShellPanel() {
  const {
    shells,
    activeShellId,
    settings,
    addShell,
    removeShell,
    setActiveShell,
    updateShellName,
    toggleSettings,
    getShellTypeLabel,
    updateSettings,
  } = useShellStore();

  const [expandedShells, setExpandedShells] = useState<Set<string>>(new Set());
  const [showNewShellMenu, setShowNewShellMenu] = useState(false);
  const [editingShellId, setEditingShellId] = useState<string | null>(null);
  const [editingName, setEditingName] = useState("");
  const [consoleDraft, setConsoleDraft] = useState<ConsoleDraft | null>(null);
  const [imagePreviewsByShell, setImagePreviewsByShell] = useState<Record<string, ImagePreview[]>>({});
  const newShellMenuRef = useRef<HTMLDivElement>(null);
  const activeImagePreviews = activeShellId ? imagePreviewsByShell[activeShellId] ?? [] : [];

  // Close menu when clicking outside
  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (
        newShellMenuRef.current &&
        !newShellMenuRef.current.contains(event.target as Node)
      ) {
        setShowNewShellMenu(false);
      }
    }

    if (showNewShellMenu) {
      document.addEventListener("mousedown", handleClickOutside);
      return () => document.removeEventListener("mousedown", handleClickOutside);
    }
  }, [showNewShellMenu]);

  useEffect(() => {
    const onDraft = (event: Event) => {
      const detail = (event as CustomEvent).detail as
        | { shellId?: string; text?: string }
        | undefined;
      if (typeof detail?.shellId === "string" && typeof detail.text === "string") {
        setConsoleDraft({ shellId: detail.shellId, text: detail.text });
      }
    };
    window.addEventListener("shell-draft", onDraft);
    return () => window.removeEventListener("shell-draft", onDraft);
  }, []);

  const toggleExpanded = (shellId: string) => {
    setExpandedShells((prev) => {
      const next = new Set(prev);
      if (next.has(shellId)) {
        next.delete(shellId);
      } else {
        next.add(shellId);
      }
      return next;
    });
  };

  const handleAddShell = (type: ShellType) => {
    addShell(type);
    setShowNewShellMenu(false);
  };

  const handleStartRename = (shellId: string, currentName: string) => {
    setEditingShellId(shellId);
    setEditingName(currentName);
  };

  const handleFinishRename = () => {
    if (editingShellId && editingName.trim()) {
      updateShellName(editingShellId, editingName.trim());
    }
    setEditingShellId(null);
    setEditingName("");
  };

  const closeShell = (shell: { id: string; type: ShellType }) => {
    if (window.chrome?.webview?.postMessage) {
      window.chrome.webview.postMessage(
        JSON.stringify({
          t: "shell_close",
          shellId: shell.id,
          shellType: shell.type,
        })
      );
    }
    markShellClosed(shell.id);
    removeShell(shell.id);
  };

  const copyDraft = () => {
    if (!consoleDraft?.text) return;
    void navigator.clipboard?.writeText(consoleDraft.text);
  };

  const sendDraft = () => {
    if (!consoleDraft?.text.trim()) return;
    const shell = shells.find((s) => s.id === consoleDraft.shellId) ?? shells.find((s) => s.id === activeShellId);
    if (!shell || !window.chrome?.webview?.postMessage) return;
    const data = consoleDraft.text.replace(/\r\n?/g, "\n").replace(/\n/g, "\r");
    window.chrome.webview.postMessage(
      JSON.stringify({
        t: "shell_data",
        shellId: shell.id,
        shellType: shell.type,
        data: data.endsWith("\r") ? data : `${data}\r`,
      })
    );
    setConsoleDraft(null);
  };

  const addImagePreview = (shellId: string, path: string) => {
    setImagePreviewsByShell((prev) => {
      const existing = prev[shellId] ?? [];
      const next = [{ path, ts: Date.now() }, ...existing.filter((item) => item.path.toLowerCase() !== path.toLowerCase())]
        .slice(0, 8);
      return { ...prev, [shellId]: next };
    });
  };

  const removeImagePreview = (shellId: string, path: string) => {
    setImagePreviewsByShell((prev) => ({
      ...prev,
      [shellId]: (prev[shellId] ?? []).filter((item) => item.path !== path),
    }));
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter") {
      handleFinishRename();
    } else if (e.key === "Escape") {
      setEditingShellId(null);
      setEditingName("");
    }
  };

  return (
    <div className="shell-panel">
      {/* Main content area */}
      <div className="shell-content">
        {/* Terminal display area */}
        <div className="terminal-display-area">
          {consoleDraft && (
            <div className="console-draft">
              <div className="console-draft-header">
                <span>Command draft</span>
                <div className="console-draft-actions">
                  <button type="button" onClick={sendDraft} title="Send draft to terminal">
                    Send
                  </button>
                  <button type="button" onClick={copyDraft} title="Copy draft">
                    Copy
                  </button>
                  <button type="button" onClick={() => setConsoleDraft(null)} title="Dismiss draft">
                    <X size={12} />
                  </button>
                </div>
              </div>
              <textarea
                value={consoleDraft.text}
                onChange={(e) => setConsoleDraft({ ...consoleDraft, text: e.target.value })}
                onKeyDown={(e) => {
                  if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
                    e.preventDefault();
                    sendDraft();
                  }
                }}
                spellCheck={false}
                aria-label="Command draft"
              />
              <div className="console-draft-note">
                Staged only. Press Ctrl+Enter or Send to run it.
              </div>
            </div>
          )}
          {activeShellId && activeImagePreviews.length > 0 && (
            <div className="terminal-image-strip" aria-label="Detected image previews">
              <div className="terminal-image-strip-head">
                <span>Images</span>
                <button
                  type="button"
                  onClick={() => setImagePreviewsByShell((prev) => ({ ...prev, [activeShellId]: [] }))}
                  title="Clear image previews"
                >
                  Clear
                </button>
              </div>
              <div className="terminal-image-list">
                {activeImagePreviews.map((item) => (
                  <div key={item.path} className="terminal-image-card">
                    <button
                      type="button"
                      className="terminal-image-open"
                      title={item.path}
                      onClick={() => postHostCommand({ cmd: "openPathInternal", path: item.path })}
                    >
                      <img src={imageThumbSrc(item.path)} alt="" loading="lazy" />
                      <span>{fileName(item.path)}</span>
                    </button>
                    <button
                      type="button"
                      className="terminal-image-remove"
                      onClick={() => removeImagePreview(activeShellId, item.path)}
                      title="Remove preview"
                    >
                      <X size={11} />
                    </button>
                  </div>
                ))}
              </div>
            </div>
          )}
          {shells.length > 0 ? (
            shells.map((shell) => (
              <Terminal
                key={shell.id}
                shellId={shell.id}
                shellType={shell.type}
                settings={settings}
                isActive={shell.id === activeShellId}
                onData={(data) => {
                  // Send data to backend/C++ host
                  if (window.chrome?.webview?.postMessage) {
                    window.chrome.webview.postMessage(
                      JSON.stringify({
                        t: "shell_data",
                        shellId: shell.id,
                        shellType: shell.type,
                        data,
                      })
                    );
                  }
                }}
                onResize={(cols, rows) => {
                  // Notify backend of resize
                  if (window.chrome?.webview?.postMessage) {
                    window.chrome.webview.postMessage(
                      JSON.stringify({
                        t: "shell_resize",
                        shellId: shell.id,
                        shellType: shell.type,
                        cols,
                        rows,
                      })
                    );
                  }
                }}
                onImagePath={addImagePreview}
              />
            ))
          ) : (
            <div className="no-terminal-message">
              <TerminalIcon size={48} opacity={0.3} />
              <p>No active terminal</p>
              <p className="hint">Click + to start a new terminal</p>
            </div>
          )}
        </div>

        {/* Accordion sidebar with shells */}
        <div className="shell-accordion">
          <div className="shell-accordion-header">
            <span>SHELLS</span>
          </div>
          <div className="shell-list">
            {shells.length === 0 ? (
              <div className="no-shells-message">
                No active shells
              </div>
            ) : (
              shells.map((shell, index) => {
                const isExpanded = expandedShells.has(shell.id);
                const isEditing = editingShellId === shell.id;

                return (
                  <div
                    key={shell.id}
                    className={`shell-item ${shell.isActive ? "active" : ""}`}
                  >
                    <div className="shell-item-header">
                      <button
                        className="shell-expand-btn"
                        onClick={() => toggleExpanded(shell.id)}
                      >
                        {isExpanded ? (
                          <ChevronDown size={12} />
                        ) : (
                          <ChevronRight size={12} />
                        )}
                      </button>

                      <div
                        className="shell-info"
                        onClick={() => setActiveShell(shell.id)}
                      >
                        {isEditing ? (
                          <input
                            className="shell-name-input"
                            value={editingName}
                            onChange={(e) => setEditingName(e.target.value)}
                            onBlur={handleFinishRename}
                            onKeyDown={handleKeyDown}
                            autoFocus
                          />
                        ) : (
                          <>
                            <span className="shell-name">{shell.name}</span>
                          </>
                        )}
                      </div>

                      <div className="shell-actions">
                        <button
                          className="shell-action-btn"
                          onClick={() => handleStartRename(shell.id, shell.name)}
                          title="Rename"
                        >
                          <MoreHorizontal size={11} />
                        </button>
                        <button
                          className="shell-action-btn"
                          onClick={() => closeShell(shell)}
                          title="Close"
                        >
                          <X size={11} />
                        </button>
                      </div>
                    </div>

                    {isExpanded && (
                      <div className="shell-item-details">
                        <div className="detail-row">
                          <span className="detail-label">Type:</span>
                          <span className="detail-value">
                            {getShellTypeLabel(shell.type)}
                          </span>
                        </div>
                        <div className="detail-row">
                          <span className="detail-label">Created:</span>
                          <span className="detail-value">
                            {new Date(shell.createdAt).toLocaleTimeString()}
                          </span>
                        </div>
                        <div className="detail-row">
                          <span className="detail-label">Status:</span>
                          <span
                            className={`detail-value ${shell.isActive ? "status-active" : "status-inactive"}`}
                          >
                            {shell.isActive ? "Active" : "Inactive"}
                          </span>
                        </div>
                      </div>
                    )}
                  </div>
                );
              })
            )}
          </div>

          <div className="shell-accordion-footer">
            {shells.length > 0 && (
              <button
                className="clear-all-btn"
                onClick={() => {
                  shells.forEach(closeShell);
                }}
              >
                <Trash2 size={11} />
              </button>
            )}
            <div className="shell-footer-actions">
              <div className="new-shell-dropdown" ref={newShellMenuRef}>
                <button
                  className="shell-btn"
                  onClick={() => setShowNewShellMenu(!showNewShellMenu)}
                  title="New Terminal"
                >
                  <Plus size={14} />
                </button>
                {showNewShellMenu && (
                  <div className="shell-type-menu">
                    {shellTypes.map((shellType) => (
                      <button
                        key={shellType.type}
                        className="shell-type-item"
                        onClick={() => handleAddShell(shellType.type)}
                      >
                        <span className="shell-type-icon">{shellType.icon}</span>
                        <span>{shellType.label}</span>
                      </button>
                    ))}
                  </div>
                )}
              </div>
              <button
                className="shell-btn theme-toggle-btn"
                onClick={() => {
                  const themes: Array<"dark" | "light" | "system"> = ["dark", "light", "system"];
                  const currentIndex = themes.indexOf(settings.theme);
                  const nextTheme = themes[(currentIndex + 1) % themes.length];
                  updateSettings({ theme: nextTheme });
                }}
                title={`Theme: ${settings.theme} (click to cycle)`}
              >
                {settings.theme === "dark" && <Moon size={14} />}
                {settings.theme === "light" && <Sun size={14} />}
                {settings.theme === "system" && <Monitor size={14} />}
              </button>
              <button
                className="shell-btn"
                onClick={toggleSettings}
                title="Settings"
              >
                <Settings size={14} />
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
