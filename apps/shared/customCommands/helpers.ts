import type { CustomCommandGroup, CustomCommandItem, CustomCommandsDocument, AppCommandOption, RibbonCommandOption } from "./types";

/** Dev/browser fallback when the native host is unavailable. */
export const INTERNAL_APP_COMMANDS = [
  "chat",
  "takescreenshot",
  "browse",
  "recordstart",
  "recordstop",
  "videorecordstart",
  "videorecordstop",
  "videorecordpause",
  "pausebatch",
  "resumebatch",
  "cancelbatch",
];

export function devAppCommandOptions(): AppCommandOption[] {
  return INTERNAL_APP_COMMANDS.map((id) => ({ id, label: id, available: true }));
}

export function defaultAppCommandId(commands: AppCommandOption[]): string {
  return commands.find((c) => c.available !== false)?.id || commands[0]?.id || "takescreenshot";
}

/** Dev/browser fallback when the native host is unavailable. */
export const RIBBON_COMMANDS = [
  "home",
  "filetree",
  "terminal",
  "log",
  "chat",
  "theme",
  "resize",
  "compress",
  "meta",
  "transform",
  "find",
  "duplicates",
  "run",
  "pause",
  "resume",
  "cancel",
  "saveSession",
  "loadSession",
  "resetLayout",
  "settings",
  "appSettings",
];

export function devRibbonCommandOptions(): RibbonCommandOption[] {
  return RIBBON_COMMANDS.map((id) => ({ id, label: id, available: true }));
}

export function defaultRibbonCommandId(commands: RibbonCommandOption[]): string {
  return commands.find((c) => c.available !== false)?.id || commands[0]?.id || "settings";
}

export const COLOR_PRESETS = [
  "#7F33F0",
  "#06B6D4",
  "#22C55E",
  "#F59E0B",
  "#EF4444",
  "#EC4899",
  "#3B82F6",
  "#14B8A6",
  "#A855F7",
  "#94A3B8",
];

export function newId(prefix: string) {
  return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(16).slice(2, 7)}`;
}

export function ensureDoc(doc: CustomCommandsDocument): CustomCommandsDocument {
  return {
    ...doc,
    version: doc.version ?? 1,
    ribbon: {
      ...(doc.ribbon || {}),
      groups: Array.isArray(doc.ribbon?.groups) ? doc.ribbon.groups : [],
    },
  };
}

export function makeGroup(): CustomCommandGroup {
  return {
    id: newId("group"),
    label: "Custom",
    items: [],
  };
}

export function makeButton(partial: Partial<CustomCommandItem> = {}): CustomCommandItem {
  return {
    type: "button",
    enabled: true,
    visible: true,
    id: newId("custom.command"),
    label: "Command",
    icon: "settings",
    source: { kind: "selection" },
    output: {},
    userData: {},
    ...partial,
  };
}

export function makeDropdown(): CustomCommandItem {
  return {
    ...makeButton({ type: "dropdown", id: newId("custom.dropdown"), label: "Dropdown", icon: "run" }),
    items: [makeButton({ label: "Child command", icon: "chat" })],
  };
}

export function cloneGroups(doc: CustomCommandsDocument): CustomCommandGroup[] {
  return (ensureDoc(doc).ribbon?.groups || []).map((group) => ({
    ...group,
    items: (group.items || []).map((item) => ({ ...item, items: item.items ? item.items.map((child) => ({ ...child })) : undefined })),
  }));
}

export function toCsv(values?: string[]) {
  return (values || []).join(", ");
}

export function fromCsv(value: string) {
  return value.split(",").map((x) => x.trim()).filter(Boolean);
}

export function argsText(values?: string[]) {
  return (values || []).join("\n");
}

export function fromArgsText(value: string) {
  return value.split(/\r?\n/).filter((x) => x.trim().length > 0);
}

export function moveEntry<T>(list: T[], from: number, to: number) {
  const next = [...list];
  const [entry] = next.splice(from, 1);
  next.splice(Math.max(0, Math.min(to, next.length)), 0, entry);
  return next;
}

export function userDataText(item: CustomCommandItem) {
  if (item.userData == null) return "";
  try {
    return JSON.stringify(item.userData, null, 2);
  } catch {
    return String(item.userData);
  }
}

export function applyUserDataText(item: CustomCommandItem, text: string): CustomCommandItem {
  const trimmed = text.trim();
  if (!trimmed) return { ...item, userData: {} };
  try {
    return { ...item, userData: JSON.parse(trimmed) };
  } catch {
    return { ...item, userData: { raw: text } };
  }
}

export function commandTreeLabel(item: CustomCommandItem, fallback: string) {
  if (item.type === "separator") return "Separator";
  return item.label || item.id || fallback;
}

export function flattenCommandDocument(document?: CustomCommandsDocument): CustomCommandItem[] {
  const groups = document?.ribbon?.groups ?? [];
  const out: CustomCommandItem[] = [];
  const push = (items?: CustomCommandItem[]) => {
    for (const item of items ?? []) {
      if (item.type === "separator") continue;
      out.push(item);
      if (item.items?.length) push(item.items);
    }
  };
  for (const group of groups) push(group.items);
  return out;
}

export function parseRunCustomCommandPayload(method: string): CustomCommandItem | null {
  const marker = "host.runCustomCommand";
  const markerPos = method.indexOf(marker);
  if (markerPos < 0) return null;
  const objectStart = method.indexOf("{", markerPos + marker.length);
  if (objectStart < 0) return null;

  let inString = false;
  let escaped = false;
  let quote = "";
  let depth = 0;
  for (let i = objectStart; i < method.length; i += 1) {
    const ch = method[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === quote) inString = false;
      continue;
    }
    if (ch === "\"" || ch === "'") {
      inString = true;
      quote = ch;
      continue;
    }
    if (ch === "{") depth += 1;
    else if (ch === "}") {
      depth -= 1;
      if (depth === 0) {
        try {
          return JSON.parse(method.slice(objectStart, i + 1)) as CustomCommandItem;
        } catch {
          return null;
        }
      }
    }
  }
  return null;
}

export function customCommandToRunScriptMethod(command: CustomCommandItem): string {
  return `return host.runCustomCommand(${JSON.stringify(command)});`;
}

export function commandPayloadRows(payload: Record<string, unknown>) {
  const rows: Array<[string, string]> = [];
  const push = (key: string, value: unknown) => {
    if (value == null || value === "") return;
    if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") rows.push([key, String(value)]);
    else rows.push([key, JSON.stringify(value, null, 2)]);
  };
  push("id", payload.id);
  push("label", payload.label);
  push("appCommand", payload.appCommand);
  push("cliCommand", payload.cliCommand);
  push("ribbonCommand", payload.ribbonCommand);
  push("url", payload.url);
  push("path", payload.path);
  push("args", payload.args);
  push("cwd", payload.cwd);
  push("logLevel", payload.logLevel);
  push("externalCommand", payload.externalCommand);
  push("source", payload.source);
  push("output", payload.output);
  push("runOptions", payload.runOptions);
  push("userData", payload.userData);
  return rows;
}
