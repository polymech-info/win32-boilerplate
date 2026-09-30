import type { Page, PageLayout, UserPagePayload } from "@/UserPage/types";

export const DEV_PAGE_URL =
  "https://pixlwiz.com/user/3bb4cfbf-318b-44d3-a9d3-35680e738421/pages/nodehub.json";
export const DEV_PAGE_PROXY_URL = "/__pm_page_dev__/nodehub.json";
export const DEV_PAGE_PIXLWIZ_PROXY_PREFIX = "/__pm_page_src_pixlwiz__/";
export const DEV_PAGE_SERVICE_PROXY_PREFIX = "/__pm_page_src_service__/";

export function pageSrcFromLocation(): string {
  if (typeof window === "undefined") return "";
  return new URLSearchParams(window.location.search).get("src")?.trim() ?? "";
}

export function displayPageSrc(src: string): string {
  return src || DEV_PAGE_URL;
}

export function pageFetchUrl(src: string): string {
  if (!src) return devPageFetchUrl();
  const proxyPrefix = devProxyPrefixForPageSrc(src);
  if (proxyPrefix) {
    return `${proxyPrefix}${encodeURIComponent(src)}`;
  }
  return src;
}

function devPageFetchUrl() {
  if (typeof window !== "undefined" && /^(localhost|127\.0\.0\.1)$/.test(window.location.hostname)) {
    return DEV_PAGE_PROXY_URL;
  }
  return DEV_PAGE_URL;
}

function devProxyPrefixForPageSrc(src: string): string {
  if (typeof window === "undefined" || !/^(localhost|127\.0\.0\.1)$/.test(window.location.hostname)) return "";
  try {
    const url = new URL(src);
    if (url.protocol !== "https:") return "";
    if (url.hostname === "pixlwiz.com") return DEV_PAGE_PIXLWIZ_PROXY_PREFIX;
    if (url.hostname === "service.polymech.info") return DEV_PAGE_SERVICE_PROXY_PREFIX;
    return "";
  } catch {
    return "";
  }
}

export function isUserPagePayload(value: unknown): value is UserPagePayload {
  if (!value || typeof value !== "object") return false;
  const page = (value as { page?: unknown }).page;
  return !!page && typeof page === "object" && typeof (page as { id?: unknown }).id === "string";
}

export function resolvePageLayout(page: Page): PageLayout | null {
  const content = page.content;
  if (!content || typeof content === "string") return null;

  const pageKey = `page-${page.id}`;
  const root = content as { pages?: Record<string, unknown>; containers?: unknown };
  const byPageId = root.pages?.[pageKey];
  if (isPageLayout(byPageId)) return byPageId;

  const firstLayout = Object.values(root.pages ?? {}).find(isPageLayout);
  if (firstLayout) return firstLayout;

  if (isPageLayout(content)) return content;

  if (Array.isArray(root.containers)) {
    return {
      id: pageKey,
      name: page.title,
      containers: root.containers as PageLayout["containers"],
    };
  }

  return null;
}

export function mergePageVariables(page: Page, userVariables: Record<string, unknown> = {}): Record<string, unknown> {
  const variables: Record<string, unknown> = {
    showAuthor: true,
    showDate: true,
    showCategories: true,
    showActions: true,
    showParent: true,
    showTitle: true,
    showToc: true,
    showLastUpdated: true,
    showFooter: true,
  };

  Object.assign(variables, normalizeObject(userVariables));

  for (const path of page.category_paths ?? []) {
    for (const category of path) {
      Object.assign(variables, normalizeObject(category.meta?.variables));
    }
  }

  const meta = page.meta ?? {};
  const typeValues = meta.typeValues;
  if (typeValues && typeof typeValues === "object") {
    for (const value of Object.values(typeValues)) {
      Object.assign(variables, normalizeObject(value));
    }
  }

  Object.assign(variables, normalizeObject(meta.variables));
  return variables;
}

function isPageLayout(value: unknown): value is PageLayout {
  return !!value && typeof value === "object" && Array.isArray((value as { containers?: unknown }).containers);
}

function normalizeObject(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) return {};
  return Object.fromEntries(
    Object.entries(value as Record<string, unknown>).map(([key, val]) => [key, val ?? ""]),
  );
}
