import { apiClient, fetchWithDeduplication } from "@/UserPage/clients/db";
import type { MediaItem } from "@/UserPage/client";

export interface FetchPicturesResult {
  data: MediaItem[];
  count?: number;
  page?: number;
  limit?: number;
}

export function fetchPictures(options: { userId?: string; limit?: number; page?: number } = {}, signal?: AbortSignal) {
  const params = new URLSearchParams();
  if (options.userId) params.set("userId", options.userId);
  params.set("limit", String(options.limit ?? 9999));
  params.set("page", String(options.page ?? 0));
  return apiClient<FetchPicturesResult>(`/api/pictures?${params}`, {}, signal);
}

export function fetchPictureById(id: string, signal?: AbortSignal) {
  return fetchWithDeduplication(`picture-${id}`, () => apiClient<MediaItem | null>(`/api/pictures/${id}`, {}, signal));
}
