import { callProviderRpc, hasWebProviderHost, postHost } from "./hostBridge";

function arrayBufferToBase64(buffer: ArrayBuffer): string {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  for (let i = 0; i < bytes.byteLength; i += 1) binary += String.fromCharCode(bytes[i]!);
  return btoa(binary);
}

function collectImageFilesFromClipboard(dt: DataTransfer | null | undefined): File[] {
  if (!dt) return [];
  const out: File[] = [];
  const seen = new Set<string>();
  const keyOf = (f: File) => `${f.name}\0${f.size}\0${f.type}`;
  if (dt.files?.length) {
    for (let i = 0; i < dt.files.length; i += 1) {
      const f = dt.files[i]!;
      if (!f.type.startsWith("image/")) continue;
      const k = keyOf(f);
      if (seen.has(k)) continue;
      seen.add(k);
      out.push(f);
    }
  }
  for (let i = 0; i < dt.items.length; i += 1) {
    const it = dt.items[i]!;
    if (it.kind !== "file" || !it.type.startsWith("image/")) continue;
    const f = it.getAsFile();
    if (!f) continue;
    const k = keyOf(f);
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(f);
  }
  return out;
}

/** DOM or React synthetic paste: only `clipboardData` + `preventDefault` are used. */
export type ClipboardPasteLike = {
  clipboardData: DataTransfer | null;
  preventDefault(): void;
};

/**
 * If the clipboard contains image file(s), add them to the chat context film strip
 * (same as drag-drop: `saveChatPastedImage` RPC → `addContextPaths`).
 * When handled, calls `preventDefault` so image bytes are not pasted into the prompt.
 */
export async function tryAddClipboardImagesToChatContext(e: ClipboardPasteLike): Promise<boolean> {
  if (!hasWebProviderHost()) return false;
  const files = collectImageFilesFromClipboard(e.clipboardData);
  if (!files.length) return false;

  const paths: string[] = [];
  for (const f of files) {
    const withPath = f as File & { path?: string };
    if (typeof withPath.path === "string" && /^[A-Za-z]:[\\/]/.test(withPath.path)) {
      paths.push(String(withPath.path).replace(/\//g, "\\"));
      continue;
    }
    const mime = f.type && f.type.startsWith("image/") ? f.type : "image/png";
    let buf: ArrayBuffer;
    try {
      buf = await f.arrayBuffer();
    } catch {
      continue;
    }
    if (!buf.byteLength) continue;
    const base64 = arrayBufferToBase64(buf);
    const res = await callProviderRpc({ method: "saveChatPastedImage", mime, base64 });
    if (res.ok && res.data && typeof (res.data as { path?: unknown }).path === "string") {
      paths.push((res.data as { path: string }).path);
    }
  }
  if (!paths.length) return false;
  postHost({ kind: "addContextPaths", paths });
  e.preventDefault();
  return true;
}
