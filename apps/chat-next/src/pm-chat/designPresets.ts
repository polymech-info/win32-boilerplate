/** LLM design / look presets — same payload shape as QuickAction rows + optional `group` for UI layout. */

import designBundled from "../../../../dist/data/design-presets.json";

export type DesignPreset = { id: string; name: string; prompt: string; icon: string; group?: string };

/** Display order for toolbar subheads (must match `group` in design-presets.json). */
const GROUP_ORDER: readonly string[] = [
  "Classic photo looks",
  "Editorial",
  "Posters & graphics",
  "Trendy looks",
  "Social graphics",
  "Merge / collage",
];

export function layoutDesignPresetGroups(presets: DesignPreset[]): { title: string; items: DesignPreset[] }[] {
  const by = new Map<string, DesignPreset[]>();
  for (const p of presets) {
    const g = p.group?.trim() ?? "";
    if (!by.has(g)) by.set(g, []);
    by.get(g)!.push(p);
  }
  const placed = new Set<string>();
  const out: { title: string; items: DesignPreset[] }[] = [];
  for (const title of GROUP_ORDER) {
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

export function normalizeDesignPresetRow(a: unknown): DesignPreset | null {
  if (!a || typeof a !== "object") return null;
  const o = a as Record<string, unknown>;
  if (typeof o.prompt !== "string" || !o.prompt.trim()) return null;
  const id = String((o.id && String(o.id).trim()) || `dp-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`);
  const name = typeof o.name === "string" && o.name.trim() ? o.name.trim() : id;
  const g = typeof o.group === "string" ? o.group.trim() : "";
  const row: DesignPreset = {
    id,
    name,
    prompt: o.prompt,
    icon: typeof o.icon === "string" ? o.icon : "\u2728",
  };
  if (g) row.group = g;
  return row;
}

/** From host `design_presets` or raw catalog `{ presets: [...] }`. */
export function designPresetsFromUnknownDoc(doc: unknown): DesignPreset[] {
  if (!doc || typeof doc !== "object") return [];
  const root = doc as Record<string, unknown>;
  if (Array.isArray(root.design_presets)) {
    return root.design_presets.map(normalizeDesignPresetRow).filter(Boolean) as DesignPreset[];
  }
  const presets = root.presets;
  if (!Array.isArray(presets)) return [];
  return presets.map(normalizeDesignPresetRow).filter(Boolean) as DesignPreset[];
}

/** Vite dev / no WebView2: load canonical catalog from `dist/data/design-presets.json`. */
export function designPresetsBundledForDev(): DesignPreset[] {
  return designPresetsFromUnknownDoc(designBundled as unknown);
}
