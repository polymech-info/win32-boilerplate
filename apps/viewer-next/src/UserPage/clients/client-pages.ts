import { apiClient, fetchWithDeduplication } from "@/UserPage/clients/db";
import type { Page, UserPagePayload } from "@/UserPage/types";

export type FetchUserPageResponse = UserPagePayload;

export function fetchUserPage(userId: string, slug: string, signal?: AbortSignal): Promise<FetchUserPageResponse | null> {
  return fetchWithDeduplication(`user-page-${userId}-${slug}`, async () => {
    const result = await apiClient<FetchUserPageResponse>(`/api/user-page/${userId}/${slug}`, {}, signal);
    return result ?? null;
  }, 10000);
}

export function fetchPageDetailsById(pageId: string, signal?: AbortSignal): Promise<{ page?: Page } | null> {
  return fetchWithDeduplication(`page-details-${pageId}`, async () => {
    return apiClient<{ page?: Page } | null>(`/api/user-page/${pageId}/${pageId}`, {}, signal);
  });
}

export async function fetchPageById(pageId: string, signal?: AbortSignal): Promise<Page | null> {
  const result = await fetchPageDetailsById(pageId, signal);
  return result?.page ?? (result as Page | null);
}
