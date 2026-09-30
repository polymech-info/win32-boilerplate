import { useShellStore } from "./shellStore";
import { X, ChevronRight, Type, Palette, MousePointer, ScrollText } from "lucide-react";

export function SettingsPanel() {
  const { settings, showSettings, updateSettings, toggleSettings } = useShellStore();

  if (!showSettings) return null;

  return (
    <div className="settings-overlay" onClick={toggleSettings}>
      <div className="settings-panel" onClick={(e) => e.stopPropagation()}>
        <div className="settings-header">
          <h2>Terminal Settings</h2>
          <button className="settings-close-btn" onClick={toggleSettings}>
            <X size={18} />
          </button>
        </div>

        <div className="settings-content">
          {/* General Section */}
          <div className="settings-section">
            <div className="settings-section-header">
              <ChevronRight size={16} />
              <h3>General</h3>
            </div>

            <div className="settings-row">
              <label className="checkbox-label">
                <input
                  type="checkbox"
                  checked={settings.openLinksInApp}
                  onChange={(e) =>
                    updateSettings({ openLinksInApp: e.target.checked })
                  }
                />
                <span>Open URL links inside PolyMech</span>
              </label>
            </div>
          </div>

          {/* Appearance Section */}
          <div className="settings-section">
            <div className="settings-section-header">
              <Palette size={16} />
              <h3>Appearance</h3>
            </div>

            <div className="settings-row">
              <label>Theme</label>
              <select
                value={settings.theme}
                onChange={(e) =>
                  updateSettings({ theme: e.target.value as "dark" | "light" | "system" })
                }
              >
                <option value="dark">Dark</option>
                <option value="light">Light</option>
                <option value="system">System</option>
              </select>
            </div>

            <div className="settings-row">
              <label>Font Size</label>
              <div className="settings-input-group">
                <input
                  type="number"
                  min={8}
                  max={32}
                  value={settings.fontSize}
                  onChange={(e) =>
                    updateSettings({ fontSize: parseInt(e.target.value) || 14 })
                  }
                />
                <span>px</span>
              </div>
            </div>

            <div className="settings-row">
              <label>Font Family</label>
              <input
                type="text"
                value={settings.fontFamily}
                onChange={(e) => updateSettings({ fontFamily: e.target.value })}
                placeholder="Consolas, 'Courier New', monospace"
              />
            </div>
          </div>

          {/* Cursor Section */}
          <div className="settings-section">
            <div className="settings-section-header">
              <MousePointer size={16} />
              <h3>Cursor</h3>
            </div>

            <div className="settings-row">
              <label>Style</label>
              <select
                value={settings.cursorStyle}
                onChange={(e) =>
                  updateSettings({
                    cursorStyle: e.target.value as "block" | "bar" | "underline",
                  })
                }
              >
                <option value="bar">Bar</option>
                <option value="block">Block</option>
                <option value="underline">Underline</option>
              </select>
            </div>

            <div className="settings-row">
              <label className="checkbox-label">
                <input
                  type="checkbox"
                  checked={settings.cursorBlink}
                  onChange={(e) =>
                    updateSettings({ cursorBlink: e.target.checked })
                  }
                />
                <span>Blink cursor</span>
              </label>
            </div>
          </div>

          {/* Scrollback Section */}
          <div className="settings-section">
            <div className="settings-section-header">
              <ScrollText size={16} />
              <h3>Scrollback</h3>
            </div>

            <div className="settings-row">
              <label>Scrollback Lines</label>
              <div className="settings-input-group">
                <input
                  type="number"
                  min={1000}
                  max={100000}
                  step={1000}
                  value={settings.scrollback}
                  onChange={(e) =>
                    updateSettings({ scrollback: parseInt(e.target.value) || 10000 })
                  }
                />
                <span>lines</span>
              </div>
            </div>
          </div>

          {/* Advanced Section */}
          <div className="settings-section">
            <div className="settings-section-header">
              <Type size={16} />
              <h3>Advanced</h3>
            </div>

            <div className="settings-row">
              <label>Word Separators</label>
              <input
                type="text"
                value={settings.wordSeparator}
                onChange={(e) =>
                  updateSettings({ wordSeparator: e.target.value })
                }
                placeholder="Characters that define word boundaries"
              />
            </div>
          </div>

          {/* Default Shell Settings */}
          <div className="settings-section">
            <div className="settings-section-header">
              <ChevronRight size={16} />
              <h3>Default Shell</h3>
            </div>
            <div className="settings-info">
              <p>Default shell configuration will be available in a future update.</p>
            </div>
          </div>
        </div>

        <div className="settings-footer">
          <button className="settings-reset-btn" onClick={() => {
            updateSettings({
              fontSize: 14,
              fontFamily: "Consolas, 'Courier New', monospace",
              theme: "dark",
              cursorStyle: "bar",
              cursorBlink: true,
              scrollback: 10000,
              wordSeparator: " \`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?",
              openLinksInApp: false,
            });
          }}>
            Reset to Defaults
          </button>
          <button className="settings-done-btn" onClick={toggleSettings}>
            Done
          </button>
        </div>
      </div>
    </div>
  );
}
