import type { ProviderRpcResult } from "../web/hostBridge";
import type {
  ChatProviderFields,
  ImageModelRow,
  ProviderRow,
  ReplicateCollectionRow,
  VideoOpenApiInputFlat,
} from "./types";
import { normalizeVideoOpenApiInputFlat } from "./openApiSchema";

export type ChatProviderRpc = <T = unknown>(request: { method: string; [key: string]: unknown }) => Promise<ProviderRpcResult<T>>;

export type ReplicateModelRow = {
  slug?: string;
  description?: string;
  url?: string;
  visibility?: string;
  is_official?: boolean;
};

export type ChatProviderClient = {
  getChatFields: () => Promise<ProviderRpcResult<ChatProviderFields>>;
  saveChatFields: (fields: ChatProviderFields) => Promise<ProviderRpcResult>;
  listImageProviders: () => Promise<ProviderRpcResult<ProviderRow[]>>;
  listImageModels: (provider: string) => Promise<ProviderRpcResult<ImageModelRow[]>>;
  listReplicateCollections: (forceRefresh?: boolean) => Promise<ProviderRpcResult<{ collections: ReplicateCollectionRow[] }>>;
  listReplicateModels: (collection: string, forceRefresh?: boolean) => Promise<ProviderRpcResult<{ models: ReplicateModelRow[] }>>;
  resolveReplicateCollection: (modelSlug: string) => Promise<ProviderRpcResult<{ collection: string | null }>>;
  getOpenApiInputFlat: (modelSlug: string) => Promise<ProviderRpcResult<VideoOpenApiInputFlat>>;
  listLlmModels: (router: string, forceRefresh?: boolean) => Promise<ProviderRpcResult<ImageModelRow[]>>;
  getCreditInfo: () => Promise<ProviderRpcResult<{ spend?: number; max_budget?: number | null }>>;
};

export function createChatProviderClient(rpc: ChatProviderRpc): ChatProviderClient {
  return {
    getChatFields: () => rpc<ChatProviderFields>({ method: "getChatFields" }),
    saveChatFields: (fields) => rpc({ method: "saveChatFields", ...fields }),
    listImageProviders: () => rpc<ProviderRow[]>({ method: "imageProviders" }),
    listImageModels: (provider) => rpc<ImageModelRow[]>({ method: "imageModels", provider }),
    listReplicateCollections: (forceRefresh = false) =>
      rpc<{ collections: ReplicateCollectionRow[] }>({ method: "replicate", op: "collections", force_refresh: forceRefresh }),
    listReplicateModels: (collection, forceRefresh = false) =>
      rpc<{ models: ReplicateModelRow[] }>({ method: "replicate", op: "models", collection, force_refresh: forceRefresh }),
    resolveReplicateCollection: (modelSlug) =>
      rpc<{ collection: string | null }>({ method: "replicate", op: "resolve_collection", model_slug: modelSlug }),
    async getOpenApiInputFlat(modelSlug) {
      const res = await rpc({ method: "replicate", op: "openapi_input_flat", model_slug: modelSlug });
      if (!res.ok) return res as ProviderRpcResult<VideoOpenApiInputFlat>;
      const flat = normalizeVideoOpenApiInputFlat(res.data, modelSlug);
      if (!flat) return { ok: false, error: "bad openapi_input_flat payload" };
      return { ok: true, data: flat };
    },
    listLlmModels: (router, forceRefresh = false) =>
      rpc<ImageModelRow[]>({ method: "llmModels", router, force_refresh: forceRefresh }),
    getCreditInfo: () => rpc<{ spend?: number; max_budget?: number | null }>({ method: "getCreditInfo" }),
  };
}
