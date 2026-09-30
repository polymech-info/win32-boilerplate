/**
 * Dev-only filmstrip fixtures: add matching files under `public/` and either
 * - append `?TEST_STRIP` to the dev URL, or
 * - set `VITE_TEST_STRIP=1` when starting Vite.
 *
 * Paths are read from `/test-strip.manifest.json` (see `public/test-strip.manifest.json`).
 */

function testStripFlagFromUrl(): boolean {
  try {
    return new URLSearchParams(window.location.search).has("TEST_STRIP");
  } catch {
    return false;
  }
}

export function isTestStripDevActive(): boolean {
  if (!import.meta.env.DEV) return false;
  if (import.meta.env.VITE_TEST_STRIP === "1") return true;
  return testStripFlagFromUrl();
}

function normalizePublicPath(p: string): string {
  const t = String(p || "").trim();
  if (!t) return "";
  if (/^(https?:|data:)/i.test(t)) return t;
  return t.startsWith("/") ? t : `/${t}`;
}

/** Fetches `public/test-strip.manifest.json`; returns `/...` URLs suitable for `<img src>`. */
export async function loadTestStripPaths(): Promise<string[]> {
  const base = import.meta.env.BASE_URL || "/";
  const url = `${base}test-strip.manifest.json`;
  const res = await fetch(url, {
    cache: "no-store",
  });
  if (!res.ok) {
    // eslint-disable-next-line no-console
    console.warn("[TEST_STRIP] missing or unreadable test-strip.manifest.json:", res.status);
    return [];
  }
  const doc = (await res.json()) as { paths?: unknown };
  const raw = Array.isArray(doc.paths) ? doc.paths : [];
  const out: string[] = [];
  for (const x of raw) {
    if (typeof x !== "string") continue;
    const u = normalizePublicPath(x);
    if (u) out.push(u);
  }
  return out;
}
