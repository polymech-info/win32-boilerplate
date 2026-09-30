import * as Checkbox from "@radix-ui/react-checkbox";
import * as Switch from "@radix-ui/react-switch";
import { Check } from "lucide-react";
import { useMemo } from "react";

import { normalizeSkillName, type SkillsSettings } from "./types";

type Props = {
  busy: boolean;
  settings: SkillsSettings;
  search: string;
  onSearchChange: (value: string) => void;
  onToggleEnabled: (value: boolean) => void;
  onToggleRoamingEnabled: (value: boolean) => void;
  onToggleWorkspaceEnabled: (value: boolean) => void;
  onPinnedInputChange: (value: string) => void;
  onDisabledInputChange: (value: string) => void;
  onSave: () => void;
  onRefresh: () => void;
  onToggleSkillPinned: (name: string, value: boolean) => void;
  onToggleSkillDisabled: (name: string, value: boolean) => void;
};

function StatusPill({ label, state }: { label: string; state: "on" | "off" | "neutral" }) {
  return <span className={`pm-skill-status-pill ${state}`}>{label}</span>;
}

function PolicyCheckbox({
  checked,
  disabled,
  label,
  onCheckedChange,
}: {
  checked: boolean;
  disabled?: boolean;
  label: string;
  onCheckedChange: (value: boolean) => void;
}) {
  return (
    <label className={`pm-radix-check ${disabled ? "is-disabled" : ""}`}>
      <Checkbox.Root
        className="pm-radix-check-root"
        checked={checked}
        disabled={disabled}
        onCheckedChange={(next) => onCheckedChange(next === true)}
      >
        <Checkbox.Indicator className="pm-radix-check-indicator">
          <Check size={13} />
        </Checkbox.Indicator>
      </Checkbox.Root>
      <span>{label}</span>
    </label>
  );
}

function BooleanSwitch({
  checked,
  label,
  onCheckedChange,
}: {
  checked: boolean;
  label: string;
  onCheckedChange: (value: boolean) => void;
}) {
  return (
    <div className="pm-skill-switch-row">
      <span>{label}</span>
      <Switch.Root checked={checked} className="pm-radix-switch-root" onCheckedChange={onCheckedChange}>
        <Switch.Thumb className="pm-radix-switch-thumb" />
      </Switch.Root>
    </div>
  );
}

export function SkillsSettingsPanel({
  busy,
  settings,
  search,
  onSearchChange,
  onToggleEnabled,
  onToggleRoamingEnabled,
  onToggleWorkspaceEnabled,
  onPinnedInputChange,
  onDisabledInputChange,
  onSave,
  onRefresh,
  onToggleSkillPinned,
  onToggleSkillDisabled,
}: Props) {
  const normalizedPinned = useMemo(() => (settings.pinned || []).map(normalizeSkillName), [settings.pinned]);
  const normalizedDisabled = useMemo(() => (settings.disabled || []).map(normalizeSkillName), [settings.disabled]);
  const filteredSkills = useMemo(
    () =>
      (settings.skills || []).filter((s) => {
        const q = search.trim().toLowerCase();
        if (!q) return true;
        return (s.name || "").toLowerCase().includes(q) || (s.description || "").toLowerCase().includes(q);
      }),
    [settings.skills, search],
  );

  return (
    <div className="pm-settings-wrap">
      <section className="pm-provider-card pm-skill-settings-card">
        <h2>Skills</h2>

        <div className="pm-skills-switch-grid">
          <BooleanSwitch checked={settings.enabled} label="Global skills enabled" onCheckedChange={onToggleEnabled} />
          <BooleanSwitch checked={settings.roaming_enabled} label="Enable roaming source" onCheckedChange={onToggleRoamingEnabled} />
          <BooleanSwitch
            checked={settings.workspace_enabled}
            label="Enable workspace source"
            onCheckedChange={onToggleWorkspaceEnabled}
          />
        </div>

        <div className="pm-row">
          <label>Pinned skills (comma-separated)</label>
          <input value={(settings.pinned || []).join(", ")} onChange={(e) => onPinnedInputChange(e.target.value)} />
        </div>
        <div className="pm-row">
          <label>Disabled skills (comma-separated)</label>
          <input value={(settings.disabled || []).join(", ")} onChange={(e) => onDisabledInputChange(e.target.value)} />
        </div>
        <div className="pm-row">
          <label>Roaming root</label>
          <input readOnly value={settings.roots?.roaming || ""} />
        </div>
        <div className="pm-row">
          <label>Workspace root</label>
          <input readOnly value={settings.roots?.workspace || ""} />
        </div>
        <div className="pm-presets-actions">
          <button className="btn" type="button" onClick={onSave} disabled={busy}>
            Save
          </button>
          <button className="btn" type="button" onClick={onRefresh} disabled={busy}>
            Refresh
          </button>
        </div>
        <div className="pm-row">
          <label>Search discovered skills</label>
          <input value={search} onChange={(e) => onSearchChange(e.target.value)} />
        </div>
      </section>

      <div className="pm-provider-list pm-skills-list">
        {filteredSkills.map((s) => {
          const key = normalizeSkillName(s.name || "");
          const pinned = normalizedPinned.includes(key);
          const disabled = normalizedDisabled.includes(key);
          const missing = Array.isArray(s.missing_requirements) ? s.missing_requirements.join(", ") : (s.missing_requirements || "");
          return (
            <section key={`${s.name}-${s.source}`} className="pm-provider-card pm-skill-card">
              <h3 className="pm-skill-title">{s.name || "(unnamed skill)"}</h3>
              <p className="pm-skill-desc">{s.description || "No description."}</p>
              <div className="pm-skill-status-row">
                <StatusPill label={s.available ? "Available" : "Unavailable"} state={s.available ? "on" : "off"} />
                <StatusPill label={s.active ? "Active" : "Inactive"} state={s.active ? "on" : "neutral"} />
                <StatusPill label={`Source: ${s.source || "unknown"}`} state="neutral" />
              </div>
              <div className="pm-skill-policy-row">
                <PolicyCheckbox
                  checked={Boolean(s.always) || pinned}
                  disabled={Boolean(s.always)}
                  label={s.always ? "Always active (metadata)" : "Pinned (policy)"}
                  onCheckedChange={(v) => onToggleSkillPinned(s.name || "", v)}
                />
                <PolicyCheckbox
                  checked={disabled}
                  label="Disabled (policy)"
                  onCheckedChange={(v) => onToggleSkillDisabled(s.name || "", v)}
                />
              </div>
              <div className="pm-row">
                <label>Missing requirements</label>
                <input readOnly value={missing} />
              </div>
              <div className="pm-row">
                <label>Path</label>
                <input readOnly value={s.path || ""} />
              </div>
            </section>
          );
        })}
      </div>
    </div>
  );
}
