import { PIXLWIZ_ORIGIN } from "@/UserPage/client";

export const serverUrl = PIXLWIZ_ORIGIN;

const cache = new Map<string, { expiresAt: number; value: Promise<unknown> }>();

export function resolveApiUrl(pathOrUrl: string): string {
  if (pathOrUrl.startsWith("http://") || pathOrUrl.startsWith("https://")) return pathOrUrl;
  const path = pathOrUrl.startsWith("/") ? pathOrUrl : `/${pathOrUrl}`;
  if (typeof window !== "undefined" && /^(localhost|127\.0\.0\.1)$/.test(window.location.hostname)) {
    return `/__pm_pixlwiz_api__${path}`;
  }
  return `${PIXLWIZ_ORIGIN}${path}`;
}

export async function apiClient<T>(endpoint: string, options: RequestInit = {}, signal?: AbortSignal): Promise<T> {
  const res = await fetch(resolveApiUrl(endpoint), {
    ...options,
    signal: signal ?? options.signal,
    headers: {
      Accept: "application/json",
      ...(options.body ? { "Content-Type": "application/json" } : {}),
      ...options.headers,
    },
  });
  if (!res.ok) throw new Error(`API Error on ${endpoint}: HTTP ${res.status}`);
  const contentType = res.headers.get("content-type") ?? "";
  if (contentType.includes("application/json")) return res.json() as Promise<T>;
  return res.text() as unknown as T;
}

export function fetchWithDeduplication<T>(key: string, fetcher: () => Promise<T>, timeout = 30000): Promise<T> {
  const now = Date.now();
  const hit = cache.get(key);
  if (hit && hit.expiresAt > now) return hit.value as Promise<T>;
  const value = fetcher();
  cache.set(key, { value, expiresAt: now + timeout });
  return value;
}
