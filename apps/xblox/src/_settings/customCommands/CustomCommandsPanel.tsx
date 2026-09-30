import type React from "react";
import { useState } from "react";
import { CommandEditor } from "./CommandEditor";
import { GroupEditor } from "./GroupEditor";
import {
  cloneGroups,
  commandTreeLabel,
  ensureDoc,
  makeButton,
  makeDropdown,
  makeGroup,
  moveEntry,
  newId,
} from "./helpers";
import type {
  ChatPresetSnapshot,
  CliCommandOption,
  CliHelpSchema,
  CommandVariableOption,
  CustomCommandOption,
  CustomCommandGroup,
  CustomCommandItem,
  CustomCommandsDocument,
  TablerIconOption,
} from "./types";
export type { CliCommandOption, CliHelpSchema, CommandVariableOption, CustomCommandOption, CustomCommandsDocument, TablerIconOption } from "./types";

type Props = {
  busy: boolean;
  commandsPath: string;
  document: CustomCommandsDocument;
  chatPresets: ChatPresetSnapshot[];
  currentChat: unknown;
  tablerIcons: TablerIconOption[];
  cliCommands: CliCommandOption[];
  cliCommandSchemas?: Record<string, CliHelpSchema>;
  providerOptions?: CustomCommandOption[];
  modelOptionsByProvider?: Record<string, CustomCommandOption[]>;
  variableOptions?: CommandVariableOption[];
  onPickPath?: (kind: "file" | "folder") => Promise<string | undefined>;
  onChange: (doc: CustomCommandsDocument) => void;
  onReload: () => void;
};

type DragNode =
  | { kind: "group"; groupIndex: number }
  | { kind: "item"; groupIndex: number; itemIndex: number }
  | { kind: "child"; groupIndex: number; itemIndex: number; childIndex: number };

export function CustomCommandsPanel({
  busy,
  document,
  chatPresets,
  currentChat,
  tablerIcons,
  cliCommands,
  cliCommandSchemas,
  providerOptions,
  modelOptionsByProvider,
  variableOptions,
  onPickPath,
  onChange,
  onReload,
}: Props) {
  const doc = ensureDoc(document);
  const groups = doc.ribbon?.groups || [];
  const [selection, setSelection] = useState<CommandSelection>({ kind: "group", groupIndex: 0 });
  const [expandedGroups, setExpandedGroups] = useState<Record<string, boolean>>({});

  function commitGroups(nextGroups: CustomCommandGroup[]) {
    onChange({ ...doc, ribbon: { ...(doc.ribbon || {}), groups: nextGroups } });
  }

  function updateGroup(groupIndex: number, patch: Partial<CustomCommandGroup>) {
    const next = cloneGroups(doc);
    next[groupIndex] = { ...next[groupIndex], ...patch };
    commitGroups(next);
  }

  function addGroup() {
    const next = [...cloneGroups(doc), makeGroup()];
    commitGroups(next);
    setSelection({ kind: "group", groupIndex: next.length - 1 });
  }

  function removeGroup(groupIndex: number) {
    const next = cloneGroups(doc).filter((_, i) => i !== groupIndex);
    commitGroups(next.length ? next : [makeGroup()]);
    setSelection({ kind: "group", groupIndex: Math.max(0, groupIndex - 1) });
  }

  function addItem(groupIndex: number, kind: "button" | "dropdown" | "separator") {
    const next = cloneGroups(doc);
    const items = next[groupIndex].items || [];
    const item = kind === "separator" ? { type: "separator" as const } : kind === "dropdown" ? makeDropdown() : makeButton();
    next[groupIndex].items = [...items, item];
    commitGroups(next);
    setSelection({ kind: "item", groupIndex, itemIndex: next[groupIndex].items.length - 1 });
  }

  function updateItem(groupIndex: number, itemIndex: number, patch: Partial<CustomCommandItem>) {
    const next = cloneGroups(doc);
    const items = next[groupIndex].items || [];
    items[itemIndex] = { ...items[itemIndex], ...patch };
    next[groupIndex].items = items;
    commitGroups(next);
  }

  function replaceItem(groupIndex: number, itemIndex: number, item: CustomCommandItem) {
    const next = cloneGroups(doc);
    const items = next[groupIndex].items || [];
    items[itemIndex] = item;
    next[groupIndex].items = items;
    commitGroups(next);
  }

  function removeItem(groupIndex: number, itemIndex: number) {
    const next = cloneGroups(doc);
    next[groupIndex].items = (next[groupIndex].items || []).filter((_, i) => i !== itemIndex);
    commitGroups(next);
    setSelection({ kind: "group", groupIndex });
  }

  function addChild(groupIndex: number, itemIndex: number) {
    const next = cloneGroups(doc);
    const item = next[groupIndex].items?.[itemIndex];
    if (!item) return;
    item.type = "dropdown";
    item.items = [...(item.items || []), makeButton({ label: "Child command" })];
    commitGroups(next);
    setSelection({ kind: "child", groupIndex, itemIndex, childIndex: item.items.length - 1 });
  }

  function updateChild(groupIndex: number, itemIndex: number, childIndex: number, patch: Partial<CustomCommandItem>) {
    const next = cloneGroups(doc);
    const item = next[groupIndex].items?.[itemIndex];
    if (!item) return;
    const children = item.items || [];
    children[childIndex] = { ...children[childIndex], ...patch };
    item.items = children;
    commitGroups(next);
  }

  function replaceChild(groupIndex: number, itemIndex: number, childIndex: number, child: CustomCommandItem) {
    const next = cloneGroups(doc);
    const item = next[groupIndex].items?.[itemIndex];
    if (!item) return;
    const children = item.items || [];
    children[childIndex] = child;
    item.items = children;
    commitGroups(next);
  }

  function removeChild(groupIndex: number, itemIndex: number, childIndex: number) {
    const next = cloneGroups(doc);
    const item = next[groupIndex].items?.[itemIndex];
    if (!item) return;
    item.items = (item.items || []).filter((_, i) => i !== childIndex);
    commitGroups(next);
    setSelection({ kind: "item", groupIndex, itemIndex });
  }

  function addCurrentChatAsCommand(groupIndex: number) {
    const next = cloneGroups(doc);
    const items = next[groupIndex].items || [];
    const label = "Chat preset";
    next[groupIndex].items = [
      ...items,
      makeButton({
        id: newId("custom.chat"),
        label,
        icon: "chat",
        appCommand: "chat",
        userData: { source: "settings", presetType: "chat", chat: currentChat },
      }),
    ];
    commitGroups(next);
    setSelection({ kind: "item", groupIndex, itemIndex: next[groupIndex].items.length - 1 });
  }

  function addSavedChatPreset(groupIndex: number, presetId: string) {
    const preset = chatPresets.find((p) => p.id === presetId);
    if (!preset) return;
    const next = cloneGroups(doc);
    const items = next[groupIndex].items || [];
    next[groupIndex].items = [
      ...items,
      makeButton({
        id: newId("custom.chat"),
        label: preset.name || "Chat preset",
        icon: "chat",
        appCommand: "chat",
        userData: { source: "settings", presetType: "chat", presetId: preset.id, chat: preset.chat },
      }),
    ];
    commitGroups(next);
    setSelection({ kind: "item", groupIndex, itemIndex: next[groupIndex].items.length - 1 });
  }

  function moveDraggedNode(src: DragNode, dst: DragNode) {
    const next = cloneGroups(doc);
    if (src.kind === "group" && dst.kind === "group" && src.groupIndex !== dst.groupIndex) {
      commitGroups(moveEntry(next, src.groupIndex, dst.groupIndex));
      setSelection({ kind: "group", groupIndex: dst.groupIndex });
      return;
    }

    if (src.kind === "item" && dst.kind === "group") {
      const sourceItems = next[src.groupIndex]?.items || [];
      const [moved] = sourceItems.splice(src.itemIndex, 1);
      if (!moved) return;
      next[src.groupIndex].items = sourceItems;
      const targetItems = next[dst.groupIndex]?.items || [];
      targetItems.push(moved);
      next[dst.groupIndex].items = targetItems;
      commitGroups(next);
      setSelection({ kind: "item", groupIndex: dst.groupIndex, itemIndex: targetItems.length - 1 });
      return;
    }

    if (src.kind === "child" && dst.kind === "group") {
      const parent = next[src.groupIndex]?.items?.[src.itemIndex];
      if (!parent?.items) return;
      const [moved] = parent.items.splice(src.childIndex, 1);
      if (!moved) return;
      const targetItems = next[dst.groupIndex]?.items || [];
      targetItems.push(moved);
      next[dst.groupIndex].items = targetItems;
      commitGroups(next);
      setSelection({ kind: "item", groupIndex: dst.groupIndex, itemIndex: targetItems.length - 1 });
      return;
    }

    if (src.kind === "item" && dst.kind === "item") {
      const sourceItems = next[src.groupIndex]?.items || [];
      const [moved] = sourceItems.splice(src.itemIndex, 1);
      if (!moved) return;
      next[src.groupIndex].items = sourceItems;
      const targetItems = next[dst.groupIndex]?.items || [];
      const insertAt = src.groupIndex === dst.groupIndex && src.itemIndex < dst.itemIndex ? dst.itemIndex - 1 : dst.itemIndex;
      targetItems.splice(Math.max(0, Math.min(insertAt, targetItems.length)), 0, moved);
      next[dst.groupIndex].items = targetItems;
      commitGroups(next);
      setSelection({ kind: "item", groupIndex: dst.groupIndex, itemIndex: Math.max(0, Math.min(insertAt, targetItems.length - 1)) });
      return;
    }

    if (
      src.kind === "child" &&
      dst.kind === "child" &&
      src.groupIndex === dst.groupIndex &&
      src.itemIndex === dst.itemIndex
    ) {
      const parent = next[src.groupIndex]?.items?.[src.itemIndex];
      if (!parent?.items) return;
      parent.items = moveEntry(parent.items, src.childIndex, dst.childIndex);
      commitGroups(next);
      setSelection({ kind: "child", groupIndex: dst.groupIndex, itemIndex: dst.itemIndex, childIndex: dst.childIndex });
    }
  }

  function onDragStart(e: React.DragEvent<HTMLElement>, node: DragNode) {
    e.stopPropagation();
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("application/x-pm-command-node", JSON.stringify(node));
  }

  function onDropNode(e: React.DragEvent<HTMLElement>, node: DragNode) {
    e.preventDefault();
    e.stopPropagation();
    const raw = e.dataTransfer.getData("application/x-pm-command-node");
    if (!raw) return;
    try {
      moveDraggedNode(JSON.parse(raw) as DragNode, node);
    } catch {
      // Ignore malformed drag payloads.
    }
  }

  const active = resolveSelection(groups, selection);
  const selectedGroupIndex = active?.groupIndex ?? 0;
  const isGroupExpanded = (group: CustomCommandGroup, groupIndex: number) =>
    expandedGroups[group.id || String(groupIndex)] !== false;
  const toggleGroupExpanded = (group: CustomCommandGroup, groupIndex: number) => {
    const key = group.id || String(groupIndex);
    setExpandedGroups((prev) => ({ ...prev, [key]: prev[key] === false }));
  };

  return (
    <div className="pm-settings-wrap pm-custom-panel">
      <div className="pm-custom-workbench">
        <aside className="pm-custom-tree" aria-label="Command tree">
          <div className="pm-custom-tree-head">
            <strong>Commands</strong>
            <span>{groups.length} groups</span>
          </div>
          <div className="pm-custom-tree-scroll">
          {groups.map((group, groupIndex) => {
            const expanded = isGroupExpanded(group, groupIndex);
            return (
            <div
              key={group.id || groupIndex}
              className={`pm-custom-tree-group ${expanded ? "expanded" : "collapsed"}`}
              draggable
              onDragStart={(e) => onDragStart(e, { kind: "group", groupIndex })}
              onDragOver={(e) => e.preventDefault()}
              onDrop={(e) => onDropNode(e, { kind: "group", groupIndex })}
            >
              <button
                className={`pm-custom-tree-row group ${selection.kind === "group" && selection.groupIndex === groupIndex ? "active" : ""}`}
                type="button"
                onClick={() => {
                  setSelection({ kind: "group", groupIndex });
                  toggleGroupExpanded(group, groupIndex);
                }}
              >
                <span className="pm-custom-tree-label"><span className="pm-custom-tree-caret">{expanded ? "▾" : "▸"}</span>{group.label || "Group"}</span>
                <span className="pm-custom-node-meta">
                  <small>{(group.items || []).length}</small>
                  <span className="pm-custom-node-actions">
                    <span title="Add button" onClick={(e) => { e.stopPropagation(); addItem(groupIndex, "button"); }}>＋</span>
                    <span title="Remove group" onClick={(e) => { e.stopPropagation(); removeGroup(groupIndex); }}>×</span>
                  </span>
                </span>
              </button>
              {expanded ? (group.items || []).map((item, itemIndex) => (
                <div
                  key={`${item.id || itemIndex}-${itemIndex}`}
                  draggable
                  onDragStart={(e) => onDragStart(e, { kind: "item", groupIndex, itemIndex })}
                  onDragOver={(e) => e.preventDefault()}
                  onDrop={(e) => onDropNode(e, { kind: "item", groupIndex, itemIndex })}
                >
                  <button
                    className={`pm-custom-tree-row item ${selection.kind === "item" && selection.groupIndex === groupIndex && selection.itemIndex === itemIndex ? "active" : ""}`}
                    type="button"
                    onClick={() => setSelection({ kind: "item", groupIndex, itemIndex })}
                  >
                    <span className="pm-custom-tree-label">{commandTreeLabel(item, `Item ${itemIndex + 1}`)}</span>
                    <span className="pm-custom-node-meta">
                      <small>{item.type || "button"}</small>
                      <span className="pm-custom-node-actions">
                        {item.type === "dropdown" ? <span title="Add child" onClick={(e) => { e.stopPropagation(); addChild(groupIndex, itemIndex); }}>＋</span> : null}
                        <span title="Remove" onClick={(e) => { e.stopPropagation(); removeItem(groupIndex, itemIndex); }}>×</span>
                      </span>
                    </span>
                  </button>
                  {item.type === "dropdown" && (item.items || []).map((child, childIndex) => (
                    <button
                      key={`${child.id || childIndex}-${childIndex}`}
                      className={`pm-custom-tree-row child ${selection.kind === "child" && selection.groupIndex === groupIndex && selection.itemIndex === itemIndex && selection.childIndex === childIndex ? "active" : ""}`}
                      type="button"
                      draggable
                      onDragStart={(e) => onDragStart(e, { kind: "child", groupIndex, itemIndex, childIndex })}
                      onDragOver={(e) => e.preventDefault()}
                      onDrop={(e) => onDropNode(e, { kind: "child", groupIndex, itemIndex, childIndex })}
                      onClick={() => setSelection({ kind: "child", groupIndex, itemIndex, childIndex })}
                    >
                      <span className="pm-custom-tree-label">{commandTreeLabel(child, `Child ${childIndex + 1}`)}</span>
                      <span className="pm-custom-node-meta">
                        <small>{child.type || "button"}</small>
                        <span className="pm-custom-node-actions">
                          <span title="Remove" onClick={(e) => { e.stopPropagation(); removeChild(groupIndex, itemIndex, childIndex); }}>×</span>
                        </span>
                      </span>
                    </button>
                  ))}
                </div>
              )) : null}
            </div>
          );
          })}
          </div>
          <div className="pm-custom-tree-toolbar" aria-label="Command tree actions">
            <button type="button" title="Add group" onClick={addGroup}>＋</button>
            <button type="button" title="Add button" onClick={() => addItem(selectedGroupIndex, "button")} disabled={!groups[selectedGroupIndex]}>●</button>
            <button type="button" title="Add dropdown" onClick={() => addItem(selectedGroupIndex, "dropdown")} disabled={!groups[selectedGroupIndex]}>▾</button>
            <button type="button" title="Add separator" onClick={() => addItem(selectedGroupIndex, "separator")} disabled={!groups[selectedGroupIndex]}>│</button>
            <button type="button" title="Reload" onClick={onReload} disabled={busy}>↻</button>
          </div>
        </aside>

        <section className="pm-provider-card pm-custom-editor-pane">
          {!active ? (
            <p className="muted">Add a group to start editing commands.</p>
          ) : active.kind === "group" ? (
            <GroupEditor
              group={active.group}
              groupIndex={active.groupIndex}
              chatPresets={chatPresets}
              variableOptions={variableOptions}
              onChange={(patch) => updateGroup(active.groupIndex, patch)}
              onRemove={() => removeGroup(active.groupIndex)}
              onAddItem={(kind) => addItem(active.groupIndex, kind)}
              onAddCurrentChat={() => addCurrentChatAsCommand(active.groupIndex)}
              onAddSavedChat={(presetId) => addSavedChatPreset(active.groupIndex, presetId)}
            />
          ) : active.kind === "item" ? (
            <CommandEditor
              item={active.item}
              prefix={`Item ${active.itemIndex + 1}`}
              tablerIcons={tablerIcons}
              cliCommands={cliCommands}
              cliCommandSchemas={cliCommandSchemas}
              providerOptions={providerOptions}
              modelOptionsByProvider={modelOptionsByProvider}
              variableOptions={variableOptions}
              onPickPath={onPickPath}
              onChange={(patch) => updateItem(active.groupIndex, active.itemIndex, patch)}
              onReplace={(nextItem) => replaceItem(active.groupIndex, active.itemIndex, nextItem)}
              onRemove={() => removeItem(active.groupIndex, active.itemIndex)}
              onAddChild={() => addChild(active.groupIndex, active.itemIndex)}
            />
          ) : (
            <CommandEditor
              item={active.item}
              prefix={`Child ${active.childIndex + 1}`}
              tablerIcons={tablerIcons}
              cliCommands={cliCommands}
              cliCommandSchemas={cliCommandSchemas}
              providerOptions={providerOptions}
              modelOptionsByProvider={modelOptionsByProvider}
              variableOptions={variableOptions}
              onPickPath={onPickPath}
              compact
              onChange={(patch) => updateChild(active.groupIndex, active.itemIndex, active.childIndex, patch)}
              onReplace={(nextItem) => replaceChild(active.groupIndex, active.itemIndex, active.childIndex, nextItem)}
              onRemove={() => removeChild(active.groupIndex, active.itemIndex, active.childIndex)}
            />
          )}
          {groups[selectedGroupIndex] ? (
            <div className="pm-custom-quick-add">
              <button className="btn" type="button" onClick={() => addItem(selectedGroupIndex, "button")}>Add button</button>
              <button className="btn" type="button" onClick={() => addItem(selectedGroupIndex, "dropdown")}>Add dropdown</button>
              <button className="btn" type="button" onClick={() => addItem(selectedGroupIndex, "separator")}>Add separator</button>
            </div>
          ) : null}
        </section>
      </div>
    </div>
  );
}

type CommandSelection =
  | { kind: "group"; groupIndex: number }
  | { kind: "item"; groupIndex: number; itemIndex: number }
  | { kind: "child"; groupIndex: number; itemIndex: number; childIndex: number };

type ResolvedSelection =
  | { kind: "group"; groupIndex: number; group: CustomCommandGroup }
  | { kind: "item"; groupIndex: number; itemIndex: number; group: CustomCommandGroup; item: CustomCommandItem }
  | { kind: "child"; groupIndex: number; itemIndex: number; childIndex: number; group: CustomCommandGroup; parent: CustomCommandItem; item: CustomCommandItem };

function resolveSelection(groups: CustomCommandGroup[], selection: CommandSelection): ResolvedSelection | null {
  const group = groups[selection.groupIndex] || groups[0];
  const groupIndex = groups[selection.groupIndex] ? selection.groupIndex : 0;
  if (!group) return null;
  if (selection.kind === "group") return { kind: "group", groupIndex, group };
  const item = group.items?.[selection.itemIndex];
  if (!item) return { kind: "group", groupIndex, group };
  if (selection.kind === "item") return { kind: "item", groupIndex, itemIndex: selection.itemIndex, group, item };
  const child = item.items?.[selection.childIndex];
  if (!child) return { kind: "item", groupIndex, itemIndex: selection.itemIndex, group, item };
  return { kind: "child", groupIndex, itemIndex: selection.itemIndex, childIndex: selection.childIndex, group, parent: item, item: child };
}
