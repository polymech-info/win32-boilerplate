export type ProviderRow = {
  id?: string;
  name?: string;
  label?: string;
  displayName?: string;
  apiKey?: string;
  baseUrl?: string;
  models?: string[];
};

export type ProviderOption = {
  id: string;
  label: string;
};

export type ProviderModelOption = {
  id: string;
  label: string;
  description?: string;
  url?: string;
};

export type ImageModelRow = ProviderModelOption;

export type ReplicateCollectionRow = {
  slug?: string;
  name?: string;
  description?: string;
};

export type VideoOpenApiInputFlat = {
  model_slug: string;
  required: string[];
  properties: Record<string, Record<string, unknown>>;
};

export type ChatProviderFields = {
  image_provider?: string;
  image_model?: string | null;
  video_provider?: string;
  video_model?: string | null;
  stt_provider?: string | null;
  stt_model?: string | null;
  tts_provider?: string | null;
  tts_model?: string | null;
  tts_voice_id?: string | null;
  image_recognition_provider?: string | null;
  image_recognition_model?: string | null;
};
