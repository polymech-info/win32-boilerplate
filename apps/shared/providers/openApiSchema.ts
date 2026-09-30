import type { VideoOpenApiInputFlat } from "./types";

const VIDEO_OPENAPI_TOOLING_KEYS = new Set([
  "prompt",
  "first_frame_image",
  "last_frame_image",
  "last_frame",
  "start_image",
  "end_image",
  "end_frame_image",
  "init_image",
  "input_image",
  "input_images",
  "image",
  "images",
  "image_input",
  "video",
  "mask",
  "audio",
]);

export function videoOpenApiFieldVisible(key: string, prop: Record<string, unknown>): boolean {
  if (VIDEO_OPENAPI_TOOLING_KEYS.has(key)) return false;
  const fmt = prop.format;
  if (fmt === "uri" || fmt === "uri-reference" || fmt === "binary") return false;
  const ty = schemaPrimaryType(prop);
  if (ty === "array") return false;
  if (ty === "object") return false;
  return true;
}

export function schemaPrimaryType(prop: Record<string, unknown>): string {
  const t = prop.type;
  if (typeof t === "string") return t;
  if (Array.isArray(t)) {
    for (const el of t) {
      if (el === "null") continue;
      if (typeof el === "string") return el;
    }
    return "";
  }
  if (prop.enum != null) return "string";
  return "";
}

function fieldOrder(prop: Record<string, unknown>): number {
  const x = prop["x-order"];
  if (typeof x === "number" && Number.isFinite(x)) return x;
  return 1e9;
}

export function sortVideoOpenApiFieldKeys(
  keys: string[],
  properties: Record<string, Record<string, unknown>>,
): string[] {
  return [...keys].sort((a, b) => {
    const pa = properties[a] || {};
    const pb = properties[b] || {};
    const oa = fieldOrder(pa);
    const ob = fieldOrder(pb);
    if (oa !== ob) return oa - ob;
    return a.localeCompare(b);
  });
}

export function coerceDefaultString(prop: Record<string, unknown>): string | undefined {
  if (!Object.prototype.hasOwnProperty.call(prop, "default")) return undefined;
  const v = prop.default;
  if (v === undefined || v === null) return undefined;
  if (typeof v === "boolean") return v ? "true" : "false";
  return String(v);
}

function capitalizeWord(w: string): string {
  if (!w) return w;
  return w.charAt(0).toUpperCase() + w.slice(1).toLowerCase();
}

function humanizeTokens(s: string): string {
  return s
    .replace(/_/g, " ")
    .trim()
    .split(/\s+/u)
    .filter(Boolean)
    .map((w) => capitalizeWord(w))
    .join(" ");
}

export function humanizeOpenApiFieldLabel(fieldKey: string, prop: Record<string, unknown>): string {
  const rawTitle = typeof prop.title === "string" ? prop.title.trim() : "";
  if (rawTitle) {
    const looksTechnical = /_/u.test(rawTitle) || rawTitle.toLowerCase().replace(/\s+/g, "_") === fieldKey.toLowerCase();
    if (looksTechnical) return humanizeTokens(rawTitle.replace(/_/g, " "));
    return rawTitle;
  }
  return humanizeTokens(fieldKey);
}

export function buildReplicateOpenApiFieldValues(
  next: VideoOpenApiInputFlat,
  prevFlat: VideoOpenApiInputFlat | null,
  prevVals: Record<string, string>,
): Record<string, string> {
  const sameModel = prevFlat?.model_slug === next.model_slug;
  const out: Record<string, string> = {};
  for (const key of Object.keys(next.properties)) {
    const p = next.properties[key];
    if (!p || typeof p !== "object" || Array.isArray(p)) continue;
    const po = p as Record<string, unknown>;
    if (!videoOpenApiFieldVisible(key, po)) continue;
    const def = coerceDefaultString(po);
    const prim = schemaPrimaryType(po);
    if (sameModel && prevVals[key] !== undefined) {
      out[key] = prevVals[key];
      continue;
    }
    if (def !== undefined) out[key] = def;
    else if (prim === "boolean") out[key] = "false";
    else {
      const rawEnum = po.enum;
      const enumList = Array.isArray(rawEnum) ? rawEnum.map((x) => String(x)) : null;
      if (enumList && enumList.length) out[key] = enumList[0] ?? "";
      else out[key] = "";
    }
  }
  return out;
}

export function coerceOpenApiFieldValuesForHost(values: Record<string, string>): Record<string, string | number | boolean> {
  const out: Record<string, string | number | boolean> = {};
  for (const key of Object.keys(values || {})) {
    const raw = values[key];
    if (raw === undefined || raw === "") continue;
    if (raw === "true") {
      out[key] = true;
      continue;
    }
    if (raw === "false") {
      out[key] = false;
      continue;
    }
    if (/^-?\d+$/.test(String(raw))) {
      out[key] = Number.parseInt(String(raw), 10);
      continue;
    }
    out[key] = String(raw);
  }
  return out;
}

export function normalizeVideoOpenApiInputFlat(raw: unknown, fallbackSlug: string): VideoOpenApiInputFlat | null {
  if (!raw || typeof raw !== "object") return null;
  const row = raw as { model_slug?: unknown; required?: unknown; properties?: unknown };
  if (!row.properties || typeof row.properties !== "object" || Array.isArray(row.properties)) return null;
  return {
    model_slug: String(row.model_slug || fallbackSlug),
    required: Array.isArray(row.required) ? row.required.filter((x): x is string => typeof x === "string") : [],
    properties: row.properties as Record<string, Record<string, unknown>>,
  };
}
