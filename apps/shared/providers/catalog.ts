import type { ImageModelRow, ProviderModelOption, ProviderOption, ProviderRow, ReplicateCollectionRow } from "./types";

export type ChatRouterRow = {
  id: string;
  label: string;
  defaultModel: string;
  models: string[];
};

export const PIXLWIZ_TEXT_MODELS = ["text-fast", "text-deep", "text-free", "text-free-deep"];
export const PIXLWIZ_IMAGE_MODELS = [
  "image-vision-deep",
  "image-generation-fast",
  "image-generation-deep",
  "image-vision-free",
  "multimodal-free",
];
export const PIXLWIZ_IMAGE_RECOG_MODELS = ["image-vision-deep", "image-vision-free", "multimodal-free"];
export const PIXLWIZ_VIDEO_MODELS = ["video-fast", "video-deep"];
export const PIXLWIZ_STT_MODELS = ["pixlwiz-speech-to-text", "pixlwiz-realtime"];
export const PIXLWIZ_TTS_MODELS = ["pixlwiz-speech", "pixlwiz-speech-turbo"];
export const PIXLWIZ_AUDIO_PROVIDERS = ["pixlwiz", "elevenlabs"] as const;

export const PIXLWIZ_STT_MODEL_LABELS: Record<string, string> = {
  "pixlwiz-speech-to-text": "PixlWiz STT",
  "pixlwiz-realtime": "PixlWiz Realtime",
};
export const PIXLWIZ_TTS_MODEL_LABELS: Record<string, string> = {
  "pixlwiz-speech": "PixlWiz Speech",
  "pixlwiz-speech-turbo": "PixlWiz Speech Turbo",
};

export const AUDIO_PROVIDER_LABELS: Record<string, string> = {
  pixlwiz: "PixlWiz",
  elevenlabs: "ElevenLabs",
};

export type AudioModelRow = { id: string; label: string };

export const ELEVENLABS_STT_MODELS: AudioModelRow[] = [
  { id: "scribe_v2_realtime", label: "Scribe v2 (real-time streaming)" },
];

export const ELEVENLABS_TTS_MODELS: AudioModelRow[] = [
  { id: "eleven_v3", label: "ElevenLabs v3" },
  { id: "eleven_multilingual_v2", label: "Multilingual v2" },
  { id: "eleven_turbo_v2_5", label: "Turbo v2.5 (low latency)" },
  { id: "eleven_flash_v2_5", label: "Flash v2.5 (ultra-fast)" },
];

export const ELEVENLABS_TTS_VOICES: AudioModelRow[] = [
  { id: "tLK6fPv15M0oKv4V3ACR", label: "Sarah — Mature, Reassuring, Confident" },
  { id: "Xb7hH8MSUJpSbSDYk0k2", label: "Alice — Clear, Engaging Educator" },
  { id: "XrExE9yKIg1WjnnlVkGX", label: "Matilda — Knowledgeable, Professional" },
  { id: "onwK4e9ZLuTAKqWW03F9", label: "Daniel — Steady Broadcaster" },
  { id: "nPczCjzI2devNBz1zQrb", label: "Brian — Deep, Resonant and Comforting" },
  { id: "pNInz6obpgDQGcFmaJgB", label: "Adam — Dominant, Firm" },
  { id: "JBFqnCBsd6RMkjVDRZzb", label: "George — Warm, Captivating Storyteller" },
  { id: "cgSgspJ2msm6clMCkdW9", label: "Jessica — Playful, Bright, Warm" },
  { id: "SAz9YHcvj6GT2YYXdXww", label: "River — Relaxed, Neutral, Informative" },
  { id: "TX3LPaxmHKxFdv7VOQHJ", label: "Liam — Energetic, Social Media Creator" },
];

export const CHAT_ROUTERS: ChatRouterRow[] = [
  {
    id: "openrouter",
    label: "OpenRouter (multi-provider)",
    defaultModel: "openai/gpt-4o-mini",
    models: [
      "openai/gpt-4o-mini",
      "openai/gpt-4o",
      "google/gemini-2.0-flash-001",
    ],
  },
  { id: "openai", label: "OpenAI", defaultModel: "gpt-4o-mini", models: ["gpt-4o-mini", "gpt-4o", "o1-mini"] },
  {
    id: "gemini",
    label: "Google Gemini (chat)",
    defaultModel: "gemini-3-pro-image-preview",
    models: ["gemini-3-pro-image-preview", "gemini-2.0-flash", "gemini-1.5-pro"],
  },
  {
    id: "pixlwiz",
    label: "PixlWiz",
    defaultModel: "text-fast",
    models: PIXLWIZ_TEXT_MODELS,
  },
];

export function defaultModelForRouterId(routerId: string): string {
  const row = CHAT_ROUTERS.find((r) => r.id === routerId);
  return row ? row.defaultModel : "openai/gpt-4o-mini";
}

export function modelListForRouterId(routerId: string): string[] {
  const row = CHAT_ROUTERS.find((r) => r.id === routerId);
  return row ? row.models : ["openai/gpt-4o-mini"];
}

export function sortAlpha(a: unknown, b: unknown): number {
  return String(a ?? "").localeCompare(String(b ?? ""), undefined, {
    sensitivity: "base",
    numeric: true,
  });
}

export function sortReplicateCollections(cols: ReplicateCollectionRow[]): ReplicateCollectionRow[] {
  if (!Array.isArray(cols) || cols.length === 0) return cols || [];
  return [...cols].sort((x, y) =>
    sortAlpha((x.name || x.slug || "").toString(), (y.name || y.slug || "").toString()),
  );
}

export function sortImageModelRowList(rows: ImageModelRow[]): ImageModelRow[] {
  if (!Array.isArray(rows) || rows.length === 0) return rows || [];
  return [...rows].sort((p, q) => sortAlpha(p.label || p.id, q.label || q.id));
}

export function providerDisplayLabel(row: ProviderRow): string {
  return String(row.label || row.displayName || row.name || row.id || "");
}

export function buildProviderOptions(rows: ProviderRow[]): ProviderOption[] {
  return (rows || [])
    .map((row) => {
      const id = String(row.id || row.name || "").trim();
      return id ? { id, label: providerDisplayLabel(row) || id } : null;
    })
    .filter((row): row is ProviderOption => row != null)
    .sort((a, b) => sortAlpha(a.label || a.id, b.label || b.id));
}

export function buildModelOptionsForProvider(
  providerId: string,
  rows: ProviderRow[],
  runtimeModels: Record<string, ProviderModelOption[]> = {},
): ProviderModelOption[] {
  const dyn = runtimeModels[providerId] || [];
  if (dyn.length) return sortImageModelRowList(dyn);
  const provider = (rows || []).find((row) => String(row.id || row.name || "") === providerId);
  return sortImageModelRowList((provider?.models || []).map((model) => ({ id: model, label: model })));
}

export function buildModelOptionsByProvider(
  rows: ProviderRow[],
  runtimeModels: Record<string, ProviderModelOption[]> = {},
): Record<string, ProviderModelOption[]> {
  const out: Record<string, ProviderModelOption[]> = {};
  for (const row of rows || []) {
    const id = String(row.id || row.name || "").trim();
    if (!id) continue;
    out[id] = buildModelOptionsForProvider(id, rows, runtimeModels);
  }
  return out;
}
