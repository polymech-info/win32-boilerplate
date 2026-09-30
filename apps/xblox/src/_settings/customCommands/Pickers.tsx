import { useMemo, useState } from "react";
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import { COLOR_PRESETS } from "./helpers";
import type { CommandVariableOption, TablerIconOption } from "./types";
import { FieldRow } from "./FormControls";
import { VariableInputField } from "./VariableBuilder";

const ICON_GROUPS: Array<{ label: string; names: string[] }> = [
  { label: "Common", names: ["settings", "message-circle", "player-play", "player-pause", "player-stop", "search", "photo", "sparkles", "folder-open", "bookmark", "moon", "home", "layout", "stack-2", "tags"] },
  { label: "Files", names: ["file", "file-text", "files", "folder", "folder-open", "archive", "photo", "video", "database", "cloud"] },
  { label: "Actions", names: ["player-play", "player-pause", "player-stop", "refresh", "download", "upload", "trash", "copy", "external-link", "plus", "x"] },
  { label: "Media", names: ["photo", "camera", "video", "microphone", "volume", "palette", "crop-1-1", "zoom-in", "zoom-out", "wand"] },
];

function iconLabel(name: string) {
  return name.replace(/-/g, " ");
}

function IconPreview({ icon, className }: { icon?: TablerIconOption; className?: string }) {
  if (icon?.svg) {
    return <span className={className || "pm-icon-preview"} dangerouslySetInnerHTML={{ __html: icon.svg }} />;
  }
  return <span className={className || "pm-icon-preview"}>?</span>;
}

export function IconPicker({
  value,
  icons,
  variables,
  onChange,
}: {
  value: string;
  icons: TablerIconOption[];
  variables?: CommandVariableOption[];
  onChange: (value: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState("");
  const byName = useMemo(() => new Map(icons.map((icon) => [icon.name, icon])), [icons]);
  const selected = byName.get(value);
  const normalizedQuery = query.trim().toLowerCase();
  const searchResults = useMemo(() => {
    if (!normalizedQuery) return [];
    return icons.filter((icon) => icon.name.includes(normalizedQuery)).slice(0, 48);
  }, [icons, normalizedQuery]);

  const groups = normalizedQuery
    ? [{ label: "Search", names: searchResults.map((icon) => icon.name) }]
    : ICON_GROUPS.map((group) => ({ ...group, names: group.names.filter((name) => byName.has(name)) }));

  return (
    <FieldRow label="Icon">
      <div className="pm-icon-picker">
        <Popover open={open} onOpenChange={setOpen}>
          <PopoverTrigger asChild>
            <button className="pm-icon-picker-button" type="button">
              <IconPreview icon={selected} />
              <span>{value || "Choose icon"}</span>
            </button>
          </PopoverTrigger>
          <PopoverContent className="pm-icon-popover" align="start" sideOffset={8}>
            <input className="pm-icon-search" value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Search Tabler filled icons..." autoFocus />
            <div className="pm-icon-groups">
              {groups.map((group) => (
                <section key={group.label} className="pm-icon-group">
                  <h4>{group.label}</h4>
                  <div className="pm-icon-grid">
                    {group.names.map((name) => {
                      const icon = byName.get(name);
                      return (
                        <button
                          key={name}
                          className={`pm-icon-choice ${value === name ? "active" : ""}`}
                          type="button"
                          title={iconLabel(name)}
                          onClick={() => {
                            onChange(name);
                            setOpen(false);
                            setQuery("");
                          }}
                        >
                          <IconPreview icon={icon} />
                          <span>{iconLabel(name)}</span>
                        </button>
                      );
                    })}
                  </div>
                </section>
              ))}
              {!groups.some((group) => group.names.length) ? <div className="pm-icon-empty">No matching icons</div> : null}
            </div>
          </PopoverContent>
        </Popover>
        <VariableInputField value={value} variables={variables} onChange={onChange} placeholder="custom icon name" />
      </div>
    </FieldRow>
  );
}

export function ColorPicker({
  value,
  variables,
  onChange,
}: {
  value: string;
  variables?: CommandVariableOption[];
  onChange: (value: string) => void;
}) {
  const normalized = value || "";
  return (
    <FieldRow label="Tint">
      <div className="pm-color-picker">
        <VariableInputField value={normalized} variables={variables} onChange={onChange} placeholder="#7F33F0" />
        <input
          className="pm-native-color"
          type="color"
          value={/^#[0-9a-f]{6}$/i.test(normalized) ? normalized : "#7F33F0"}
          onChange={(e) => onChange(e.target.value.toUpperCase())}
          title="Choose custom color"
        />
        <div className="pm-color-presets">
          <button className={`pm-color-clear ${!normalized ? "active" : ""}`} type="button" onClick={() => onChange("")}>
            Auto
          </button>
          {COLOR_PRESETS.map((color) => (
            <button
              key={color}
              className={`pm-color-chip ${normalized.toUpperCase() === color ? "active" : ""}`}
              type="button"
              title={color}
              style={{ backgroundColor: color }}
              onClick={() => onChange(color)}
            />
          ))}
        </div>
      </div>
    </FieldRow>
  );
}
