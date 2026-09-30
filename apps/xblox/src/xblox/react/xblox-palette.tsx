import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useDrag } from "react-dnd";
import { Moon, Sun } from "lucide-react";

import { PALETTE_GROUPS_STORAGE_KEY } from "@/xblox/prototype/constants";
import type { BlockPaletteKind } from "./BlockModel";
import type { CommandPaletteBlock, NativePaletteBlock } from "./xblox-builder";

type DragItem =
  | { source: "palette"; kind: BlockPaletteKind }
  | { source: "command"; block: CommandPaletteBlock["block"] };

type PaletteGroup = {
  id: string;
  label: string;
  kinds: BlockPaletteKind[];
  defaultOpen?: boolean;
};

export type XbloxPaletteProps = {
  palette: BlockPaletteKind[];
  nativePalette: NativePaletteBlock[];
  commandPalette: CommandPaletteBlock[];
  theme: "dark" | "light";
  onToggleTheme?: () => void;
};

const DND_TYPE = "xblox/block";
const PALETTE_KNOWN_GROUPS_STORAGE_KEY = `${PALETTE_GROUPS_STORAGE_KEY}.known`;

const paletteGroups: PaletteGroup[] = [
  {
    id: "network",
    label: "Network",
    kinds: ["fetch", "network", "httpRequest"],
    defaultOpen: true,
  },
  {
    id: "data",
    label: "Data",
    kinds: ["Parse", "parse"],
    defaultOpen: true,
  },
  {
    id: "shell",
    label: "Shell",
    kinds: ["shell", "Shell"],
    defaultOpen: true,
  },
  {
    id: "commands",
    label: "Commands",
    kinds: ["command", "runScript"],
    defaultOpen: true,
  },
  {
    id: "logic",
    label: "Logic",
    kinds: ["if", "for", "while", "switch", "case", "switchDefault", "break"],
    defaultOpen: true,
  },
  {
    id: "context",
    label: "Context",
    kinds: ["setVariable", "getVariable", "log", "wait"],
    defaultOpen: true,
  },
];

const buttonStyle = {
  width: "100%",
  border: "1px solid var(--xblox-soft-border, rgba(255, 255, 255, 0.08))",
  borderRadius: "0.45rem",
  background: "var(--xblox-row-bg, rgba(0, 0, 0, 0.34))",
  color: "var(--xblox-button-text, #d7dee9)",
  padding: "var(--xblox-palette-item-pad, 8px)",
  textAlign: "left" as const,
  cursor: "grab",
};

function readOpenPaletteGroups(): Set<string> {
  try {
    const raw = window.localStorage.getItem(PALETTE_GROUPS_STORAGE_KEY);
    const parsed = raw ? JSON.parse(raw) : null;
    if (Array.isArray(parsed)) return new Set(parsed.filter((item): item is string => typeof item === "string"));
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
  return new Set(["native:App", "native:Modbus", "network", "data", "shell", "commands", "logic", "context", "command-library"]);
}

function readKnownPaletteGroups(): Set<string> {
  try {
    const raw = window.localStorage.getItem(PALETTE_KNOWN_GROUPS_STORAGE_KEY);
    const parsed = raw ? JSON.parse(raw) : null;
    if (Array.isArray(parsed)) return new Set(parsed.filter((item): item is string => typeof item === "string"));
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
  return new Set(["native:App", "native:Modbus", "network", "data", "shell", "commands", "logic", "context", "command-library"]);
}

function persistOpenPaletteGroups(groups: Set<string>) {
  try {
    window.localStorage.setItem(PALETTE_GROUPS_STORAGE_KEY, JSON.stringify([...groups]));
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
}

function persistKnownPaletteGroups(groups: Set<string>) {
  try {
    window.localStorage.setItem(PALETTE_KNOWN_GROUPS_STORAGE_KEY, JSON.stringify([...groups]));
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
}

function nativeGroupId(label: string) {
  return `native:${label}`;
}

function PaletteButton({ kind }: { kind: BlockPaletteKind }) {
  const [, dragRef] = useDrag(
    () => ({
      type: DND_TYPE,
      item: { source: "palette", kind } satisfies DragItem,
    }),
    [kind],
  );
  const ref = useCallback(
    (el: HTMLButtonElement | null) => {
      dragRef(el);
    },
    [dragRef],
  );
  return (
    <button ref={ref} type="button" style={buttonStyle} className="xblox-palette-item">
      {kind}
    </button>
  );
}

function CommandPaletteButton({ command }: { command: CommandPaletteBlock }) {
  const [, dragRef] = useDrag(
    () => ({
      type: DND_TYPE,
      item: { source: "command", block: command.block } satisfies DragItem,
    }),
    [command],
  );
  const ref = useCallback(
    (el: HTMLButtonElement | null) => {
      dragRef(el);
    },
    [dragRef],
  );
  return (
    <button
      ref={ref}
      type="button"
      style={buttonStyle}
      className="xblox-palette-item xblox-command-palette-item"
      title={[command.id, command.description].filter(Boolean).join("\n")}
    >
      <span className="xblox-palette-item-title">{command.label}</span>
      {command.description ? <small>{command.description}</small> : null}
    </button>
  );
}

function NativePaletteButton({ item }: { item: NativePaletteBlock }) {
  const [, dragRef] = useDrag(
    () => ({
      type: DND_TYPE,
      item: { source: "command", block: item.block } satisfies DragItem,
    }),
    [item],
  );
  const ref = useCallback(
    (el: HTMLButtonElement | null) => {
      dragRef(el);
    },
    [dragRef],
  );
  const paramSummary = Array.isArray(item.params)
    ? item.params
        .map((param) => (param && typeof param === "object" && "name" in param ? String((param as { name?: unknown }).name ?? "") : ""))
        .filter(Boolean)
        .join(", ")
    : "";
  return (
    <button
      ref={ref}
      type="button"
      style={buttonStyle}
      className="xblox-palette-item xblox-native-palette-item"
      title={[item.kind, paramSummary ? `params: ${paramSummary}` : "", item.description].filter(Boolean).join("\n")}
    >
      <span className="xblox-palette-item-title">{item.label || item.kind}</span>
      {item.description ? <small>{item.description}</small> : null}
    </button>
  );
}

export function XbloxPalette({ palette, nativePalette, commandPalette, theme, onToggleTheme }: XbloxPaletteProps) {
  const [paletteQuery, setPaletteQuery] = useState("");
  const [openPaletteGroups, setOpenPaletteGroups] = useState<Set<string>>(readOpenPaletteGroups);
  const [knownPaletteGroups, setKnownPaletteGroups] = useState<Set<string>>(readKnownPaletteGroups);
  const openedNativeGroupsRef = useRef(false);
  const normalizedPaletteQuery = paletteQuery.trim().toLowerCase();

  useEffect(() => {
    persistOpenPaletteGroups(openPaletteGroups);
  }, [openPaletteGroups]);

  useEffect(() => {
    persistKnownPaletteGroups(knownPaletteGroups);
  }, [knownPaletteGroups]);

  const groupedPalette = useMemo(() => {
    const enabled = new Set(palette);
    const used = new Set<BlockPaletteKind>();
    const groups = paletteGroups
      .map((group) => {
        const kinds = group.kinds.filter((kind) => enabled.has(kind) && (!normalizedPaletteQuery || kind.toLowerCase().includes(normalizedPaletteQuery)));
        kinds.forEach((kind) => used.add(kind));
        return { ...group, kinds };
      })
      .filter((group) => group.kinds.length);
    const other = palette.filter((kind) => !used.has(kind) && (!normalizedPaletteQuery || kind.toLowerCase().includes(normalizedPaletteQuery)));
    return other.length ? [...groups, { id: "other", label: "Other", kinds: other, defaultOpen: true }] : groups;
  }, [normalizedPaletteQuery, palette]);

  const filteredCommandPalette = useMemo(() => {
    if (!normalizedPaletteQuery) return commandPalette;
    return commandPalette.filter((command) => {
      const haystack = `${command.label} ${command.id} ${command.description ?? ""}`.toLowerCase();
      return haystack.includes(normalizedPaletteQuery);
    });
  }, [commandPalette, normalizedPaletteQuery]);

  const groupedNativePalette = useMemo(() => {
    const groups = new Map<string, NativePaletteBlock[]>();
    for (const item of nativePalette) {
      const haystack = `${item.kind} ${item.label ?? ""} ${item.group ?? ""}`.toLowerCase();
      if (normalizedPaletteQuery && !haystack.includes(normalizedPaletteQuery)) continue;
      const group = item.group || "Native";
      groups.set(group, [...(groups.get(group) ?? []), item]);
    }
    return [...groups.entries()].map(([label, items]) => ({ label, items }));
  }, [nativePalette, normalizedPaletteQuery]);

  useEffect(() => {
    const visibleGroupIds = [
      ...groupedNativePalette.map((group) => nativeGroupId(group.label)),
      ...groupedPalette.map((group) => group.id),
      ...(commandPalette.length ? ["command-library"] : []),
    ];
    const unseenGroupIds = visibleGroupIds.filter((id) => !knownPaletteGroups.has(id));
    if (!unseenGroupIds.length) return;
    setKnownPaletteGroups((current) => new Set([...current, ...unseenGroupIds]));
    setOpenPaletteGroups((current) => new Set([...current, ...unseenGroupIds]));
  }, [commandPalette.length, groupedNativePalette, groupedPalette, knownPaletteGroups]);

  useEffect(() => {
    if (openedNativeGroupsRef.current || !groupedNativePalette.length) return;
    openedNativeGroupsRef.current = true;
    setOpenPaletteGroups((current) => new Set([...current, ...groupedNativePalette.map((group) => nativeGroupId(group.label))]));
  }, [groupedNativePalette]);

  const togglePaletteGroup = useCallback((groupId: string, isOpen: boolean) => {
    setKnownPaletteGroups((current) => new Set(current).add(groupId));
    setOpenPaletteGroups((current) => {
      const next = new Set(current);
      if (isOpen) next.add(groupId);
      else next.delete(groupId);
      return next;
    });
  }, []);

  return (
    <aside className="xblox-palette">
      <div className="xblox-palette-head">
        <h3>Block Palette</h3>
        {onToggleTheme ? (
          <button
            type="button"
            onClick={onToggleTheme}
            className="xblox-palette-theme-toggle"
            title={theme === "dark" ? "Switch to light theme" : "Switch to dark theme"}
            aria-label={theme === "dark" ? "Switch to light theme" : "Switch to dark theme"}
          >
            {theme === "dark" ? <Sun aria-hidden="true" /> : <Moon aria-hidden="true" />}
          </button>
        ) : null}
      </div>
      <label className="xblox-palette-search">
        <input value={paletteQuery} onChange={(event) => setPaletteQuery(event.target.value)} placeholder="Filter blocks or commands..." type="search" />
      </label>
      <div className="xblox-palette-scroll">
        {groupedNativePalette.map((group) => {
          const groupId = nativeGroupId(group.label);
          return (
          <details
            key={groupId}
            className="xblox-palette-group xblox-native-palette-group"
            open={openPaletteGroups.has(groupId)}
            onToggle={(event) => togglePaletteGroup(groupId, event.currentTarget.open)}
          >
            <summary>
              <span>{group.label}</span>
              <small>{group.items.length}</small>
            </summary>
            <div className="xblox-palette-group-list">
              {group.items.map((item) => (
                <NativePaletteButton key={`${group.label}:${item.kind}`} item={item} />
              ))}
            </div>
          </details>
        );
        })}
        {groupedPalette.map((group) => (
          <details
            key={group.id}
            className="xblox-palette-group"
            open={openPaletteGroups.has(group.id)}
              onToggle={(event) => togglePaletteGroup(group.id, event.currentTarget.open)}
          >
            <summary>
              <span>{group.label}</span>
              <small>{group.kinds.length}</small>
            </summary>
            <div className="xblox-palette-group-list">
              {group.kinds.map((kind) => (
                <PaletteButton key={kind} kind={kind} />
              ))}
            </div>
          </details>
        ))}
        {!groupedNativePalette.length && !groupedPalette.length ? <p className="xblox-palette-empty">No matching blocks.</p> : null}
      </div>
      {commandPalette.length ? (
        <details
          className="xblox-palette-group xblox-command-palette-group"
          open={openPaletteGroups.has("command-library")}
          onToggle={(event) => togglePaletteGroup("command-library", event.currentTarget.open)}
        >
          <summary>
            <span>Command Library</span>
            <small>{filteredCommandPalette.length}</small>
          </summary>
          <div className="xblox-command-palette-list">
            {filteredCommandPalette.map((command) => (
              <CommandPaletteButton key={command.id} command={command} />
            ))}
            {!filteredCommandPalette.length ? <p className="xblox-palette-empty">No matching commands.</p> : null}
          </div>
        </details>
      ) : null}
    </aside>
  );
}
