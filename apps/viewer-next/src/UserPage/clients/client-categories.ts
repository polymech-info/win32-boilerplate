import { apiClient, fetchWithDeduplication } from "@/UserPage/clients/db";
import type { Category } from "@/UserPage/client";

export function fetchCategories(options: { includeChildren?: boolean; parentSlug?: string } = {}, signal?: AbortSignal): Promise<Category[]> {
  const params = new URLSearchParams();
  if (options.parentSlug) params.set("parentSlug", options.parentSlug);
  if (options.includeChildren) params.set("includeChildren", String(options.includeChildren));
  const key = `categories-${params.toString()}`;
  return fetchWithDeduplication(key, () => apiClient<Category[]>(`/api/categories?${params}`, {}, signal), 60000);
}

export function findCategoryById(categories: Category[], id: string): Category | undefined {
  for (const category of categories) {
    if (category.id === id) return category;
    const found = findCategoryById((category.children ?? []).map((child) => child.child), id);
    if (found) return found;
  }
  return undefined;
}
