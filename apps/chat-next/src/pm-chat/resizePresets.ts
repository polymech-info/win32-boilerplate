/** LLM resize/reframe presets — QuickAction-like rows + optional `group` + optional `target` for {{TARGET_*}} placeholders. */

import templatesBundled from "../../../../dist/data/resize-templates.json";

export type ResizeTarget = { w: number; h: number; aspect?: string };

export type ResizePreset = {
  id: string;
  name: string;
  prompt: string;
  icon: string;
  group?: string;
  /** When `prompt` contains `{{TARGET_*}}`, used by {@link expandedResizePresetPrompt}. */
  target?: ResizeTarget;
};

type BundledCatalog = { version?: number; groups?: unknown[]; templates?: unknown[] };

const bundledRoot = templatesBundled as BundledCatalog;

/** Fallback display order if catalog has no top-level `groups` array. */
const GROUP_ORDER_FALLBACK: readonly string[] = [
  "9:16 — vertical social",
  "16:9 — landscape / YouTube",
  "4:5 & 1:1 — feed & square",
  "UI & product",
  "Video",
];

function groupOrderFromCatalog(): readonly string[] {
  if (!Array.isArray(bundledRoot.groups)) return GROUP_ORDER_FALLBACK;
  const g = bundledRoot.groups
    .filter((x): x is string => typeof x === "string")
    .map((x) => x.trim())
    .filter((x) => x.length > 0);
  return g.length ? g : GROUP_ORDER_FALLBACK;
}

const CATALOG_GROUP_ORDER: readonly string[] = groupOrderFromCatalog();

/**
 * Buckets presets by `group` for rendering. Order follows catalog `groups` when present;
 * other non-empty groups sort A–Z; presets with no `group` land in `title: ""` (i18n “Other”).
 */
export function layoutResizePresetGroups(presets: ResizePreset[]): { title: string; items: ResizePreset[] }[] {
  const by = new Map<string, ResizePreset[]>();
  for (const p of presets) {
    const g = p.group?.trim() ?? "";
    if (!by.has(g)) by.set(g, []);
    by.get(g)!.push(p);
  }
  const placed = new Set<string>();
  const out: { title: string; items: ResizePreset[] }[] = [];
  for (const title of CATALOG_GROUP_ORDER) {
    const items = by.get(title);
    if (items?.length) {
      out.push({ title, items });
      placed.add(title);
    }
  }
  const extras = Array.from(by.entries())
    .filter(([k]) => k && !placed.has(k))
    .sort(([a], [b]) => a.localeCompare(b));
  for (const [title, items] of extras) {
    if (items.length) out.push({ title, items });
  }
  const loose = by.get("") ?? [];
  if (loose.length) out.push({ title: "", items: loose });
  return out;
}

const HINT_SUBJECT = "the main subject";
const HINT_STYLE = "preserve the original look, grain, and color grade";

type Target = { w?: number; h?: number; aspect?: string };

export function expandResizePlaceholders(prompt: string, target: Target | undefined): string {
  const w = typeof target?.w === "number" && Number.isFinite(target.w) ? target.w : 0;
  const h = typeof target?.h === "number" && Number.isFinite(target.h) ? target.h : 0;
  const aspect = typeof target?.aspect === "string" ? target.aspect : "";
  return prompt
    .split("{{TARGET_W}}")
    .join(String(w))
    .split("{{TARGET_H}}")
    .join(String(h))
    .split("{{TARGET_ASPECT}}")
    .join(aspect)
    .split("{{SUBJECT_HINT}}")
    .join(HINT_SUBJECT)
    .split("{{STYLE_HINT}}")
    .join(HINT_STYLE);
}

/** Composer / tooltip text: expands placeholders when `target` is present. */
export function expandedResizePresetPrompt(p: ResizePreset): string {
  if (!p.prompt?.trim()) return "";
  if (!p.target || !/\{\{TARGET_(W|H|ASPECT)\}\}/.test(p.prompt)) return p.prompt;
  return expandResizePlaceholders(p.prompt, p.target);
}

function parseResizeTarget(o: Record<string, unknown>): ResizeTarget | undefined {
  const t = o.target;
  if (!t || typeof t !== "object") return undefined;
  const rec = t as Record<string, unknown>;
  const w = typeof rec.w === "number" && Number.isFinite(rec.w) ? rec.w : Number(rec.w);
  const h = typeof rec.h === "number" && Number.isFinite(rec.h) ? rec.h : Number(rec.h);
  if (!Number.isFinite(w) || !Number.isFinite(h) || w <= 0 || h <= 0) return undefined;
  const aspect = typeof rec.aspect === "string" ? rec.aspect.trim() : "";
  const out: ResizeTarget = { w, h };
  if (aspect) out.aspect = aspect;
  return out;
}

export function normalizeResizePresetRow(a: unknown): ResizePreset | null {
  if (!a || typeof a !== "object") return null;
  const o = a as Record<string, unknown>;
  if (typeof o.prompt !== "string" || !o.prompt.trim()) return null;
  const id = String((o.id && String(o.id).trim()) || `rp-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`);
  const name = typeof o.name === "string" && o.name.trim() ? o.name.trim() : id;
  const g = typeof o.group === "string" ? o.group.trim() : "";
  const tgt = parseResizeTarget(o);
  const row: ResizePreset = {
    id,
    name,
    prompt: o.prompt,
    icon: typeof o.icon === "string" ? o.icon : "\u2194",
  };
  if (g) row.group = g;
  if (tgt) row.target = tgt;
  return row;
}

/** From host `resize_presets`, or raw catalog `{ templates: [...] }` (unexpanded + target). */
export function resizePresetsFromUnknownDoc(doc: unknown): ResizePreset[] {
  if (!doc || typeof doc !== "object") return [];
  const root = doc as Record<string, unknown>;
  if (Array.isArray(root.resize_presets)) {
    return root.resize_presets.map(normalizeResizePresetRow).filter(Boolean) as ResizePreset[];
  }
  const templates = root.templates;
  if (!Array.isArray(templates)) return [];
  const out: ResizePreset[] = [];
  for (const el of templates) {
    if (!el || typeof el !== "object") continue;
    const t = el as Record<string, unknown>;
    if (typeof t.prompt !== "string") continue;
    const tgt = parseResizeTarget(t);
    const p = normalizeResizePresetRow({ ...t, prompt: t.prompt, target: t.target });
    if (p) out.push(p);
  }
  return out;
}

/** Vite dev / no WebView2: load canonical catalog from `dist/data/resize-templates.json`. */
export function resizePresetsBundledForDev(): ResizePreset[] {
  return resizePresetsFromUnknownDoc(templatesBundled as unknown);
}
