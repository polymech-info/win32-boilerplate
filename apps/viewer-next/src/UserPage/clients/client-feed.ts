import { apiClient } from "@/UserPage/clients/db";
import type { FeedItem } from "@/UserPage/client";

export type FeedSortOption = "latest" | "top";
export type FeedContentType = "posts" | "pages" | "pictures" | "files" | "places";

export interface FetchFeedOptions {
  source?: "home" | "collection" | "tag" | "user" | "widget" | "search";
  sourceId?: string;
  page?: number;
  limit?: number;
  sortBy?: FeedSortOption;
  categoryIds?: string[];
  categorySlugs?: string[];
  contentType?: FeedContentType;
  visibilityFilter?: "invisible" | "private";
  signal?: AbortSignal;
}

export async function fetchFeed(options: FetchFeedOptions): Promise<FeedItem[]> {
  const params = new URLSearchParams();
  if (options.page !== undefined) params.set("page", String(options.page));
  if (options.limit !== undefined) params.set("limit", String(options.limit));
  if (options.sortBy) params.set("sortBy", options.sortBy);
  if (options.source) params.set("source", options.source);
  if (options.sourceId) params.set("sourceId", options.sourceId);
  if (options.categoryIds?.length) params.set("categoryIds", options.categoryIds.join(","));
  if (options.categorySlugs?.length) params.set("categorySlugs", options.categorySlugs.join(","));
  if (options.contentType) params.set("contentType", options.contentType);
  if (options.visibilityFilter) params.set("visibilityFilter", options.visibilityFilter);
  return apiClient<FeedItem[]>(`/api/feed?${params}`, {}, options.signal);
}
