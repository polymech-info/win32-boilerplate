export const PIXLWIZ_ORIGIN = "https://pixlwiz.com";
export const IMAGE_ORIGIN = "https://service.polymech.info";

export interface FeedItem {
  id: string;
  title?: string;
  description?: string | null;
  user_id?: string;
  image_url?: string;
  thumbnail_url?: string | null;
  likes_count?: number;
  comments_count?: number;
  created_at?: string;
  type?: string;
  cover?: MediaItem;
  pictures?: MediaItem[];
  author?: {
    display_name?: string | null;
    username?: string | null;
    avatar_url?: string | null;
  };
  meta?: Record<string, unknown>;
  _searchSource?: string;
}

export interface MediaItem {
  id: string;
  title?: string;
  description?: string | null;
  image_url?: string;
  thumbnail_url?: string | null;
  user_id?: string;
  type?: string;
  created_at?: string;
  likes_count?: number;
  meta?: Record<string, unknown>;
  author?: FeedItem["author"];
  comments?: { count: number }[];
}

export interface Category {
  id: string;
  name: string;
  slug: string;
  children?: { child: Category }[];
}

export async function apiClient<T>(path: string, signal?: AbortSignal): Promise<T> {
  const res = await fetch(resolveApiUrl(path), {
    signal,
    headers: { Accept: "application/json" },
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json() as Promise<T>;
}

export async function fetchFeed(options: {
  source?: "home" | "user" | "search";
  sourceId?: string;
  sortBy?: "latest" | "top";
  categorySlugs?: string[];
  categoryIds?: string[];
  contentType?: string;
  limit?: number;
  page?: number;
  signal?: AbortSignal;
}) {
  const params = new URLSearchParams();
  params.set("source", options.source ?? "home");
  params.set("page", String(options.page ?? 0));
  params.set("limit", String(options.limit ?? 24));
  params.set("sortBy", options.sortBy ?? "latest");
  if (options.sourceId) params.set("sourceId", options.sourceId);
  if (options.contentType) params.set("contentType", options.contentType);
  for (const id of options.categoryIds ?? []) params.append("categoryIds", id);
  for (const slug of options.categorySlugs ?? []) params.append("categorySlugs", slug);
  return apiClient<FeedItem[]>(`/api/feed?${params}`, options.signal);
}

export async function fetchCategories(signal?: AbortSignal) {
  return apiClient<Category[]>("/api/categories?includeChildren=true", signal);
}

export async function fetchPicture(id: string, signal?: AbortSignal) {
  return apiClient<MediaItem | null>(`/api/pictures/${id}`, signal);
}

export async function fetchPictures(ids: string[], signal?: AbortSignal) {
  const valid = ids.filter(Boolean);
  if (valid.length === 0) return [];
  return apiClient<MediaItem[]>(`/api/pictures/batch?ids=${encodeURIComponent(valid.join(","))}`, signal).catch(async () => {
    const settled = await Promise.allSettled(valid.map((id) => fetchPicture(id, signal)));
    return settled.flatMap((result) => (result.status === "fulfilled" && result.value ? [result.value] : []));
  });
}

export async function fetchPost(id: string, signal?: AbortSignal) {
  return apiClient<FeedItem | null>(`/api/posts/${id}`, signal);
}

export async function fetchPage(id: string, signal?: AbortSignal) {
  const result = await apiClient<{ page?: FeedItem } | FeedItem>(`/api/user-page/${id}/${id}`, signal);
  return "page" in result ? result.page ?? null : result;
}

export function resolvePictureUrl(owner: string | null | undefined, pictureId: string): string {
  if (!pictureId) return "";
  if (pictureId.startsWith("http://") || pictureId.startsWith("https://") || pictureId.startsWith("data:")) return pictureId;
  if (!owner) return "";
  return `${IMAGE_ORIGIN}/api/vfs/get/images/${owner}/${pictureId}.png`;
}

export function mediaImage(item: FeedItem | MediaItem | null | undefined): string {
  if (!item) return "";
  const cover = "cover" in item ? item.cover : undefined;
  const first = "pictures" in item ? item.pictures?.[0] : undefined;
  return item.image_url || item.thumbnail_url || cover?.image_url || cover?.thumbnail_url || first?.image_url || first?.thumbnail_url || "";
}

export function commentsCount(item: Partial<FeedItem | MediaItem>): number {
  if ("comments_count" in item && typeof item.comments_count === "number") return item.comments_count;
  if ("comments" in item && Array.isArray(item.comments)) return item.comments[0]?.count ?? 0;
  return 0;
}

function resolveApiUrl(path: string): string {
  if (path.startsWith("http://") || path.startsWith("https://")) return path;
  if (typeof window !== "undefined" && /^(localhost|127\.0\.0\.1)$/.test(window.location.hostname)) {
    return `/__pm_pixlwiz_api__${path.startsWith("/") ? path : `/${path}`}`;
  }
  return `${PIXLWIZ_ORIGIN}${path.startsWith("/") ? path : `/${path}`}`;
}
