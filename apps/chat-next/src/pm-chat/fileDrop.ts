import { hasWebProviderHost, postHost } from "./hostBridge";

/**
 * HTML5 `DataTransfer` (e.g. `files`, custom MIME) is unreliable for drags that originate from
 * embedded Windows shell views (`IExplorerBrowser` in the host). Those paths are delivered on the
 * Win32 side via `CF_HDROP` / `IDropTarget` on the WebView2 surface (`ChatWebPanel.cpp`), merged into
 * chat context the same way as `postHost({ kind: "addContextPaths" })`.
 */

function pushPathsFromTextBlock(raw: string, out: string[]) {
  for (const line of String(raw).split(/[\r\n]+/u)) {
    const t = line.trim();
    if (!t || t.startsWith("#")) continue;
    if (/^file:/i.test(t)) {
      let p = t.replace(/^file:\/\/\//i, "");
      if (p.startsWith("///")) p = p.slice(2);
      p = decodeURIComponent(p.replace(/^\//, ""));
      if (/^[A-Za-z]:\//.test(p)) p = p.replace(/\//g, "\\");
      if (p) out.push(p);
    } else if (/^[A-Za-z]:[\\/]/.test(t)) {
      out.push(t);
    }
  }
}

export function collectPathsFromDataTransfer(dt: DataTransfer | null | undefined): string[] {
  if (!dt) return [];
  const out: string[] = [];
  if (dt.files?.length) {
    for (let i = 0; i < dt.files.length; i += 1) {
      const f = dt.files[i] as File & { path?: string };
      if (f?.path) out.push(String(f.path));
    }
  }
  const plain = dt.getData("text/plain") || "";
  for (const line of String(plain).split(/[\r\n]+/u)) {
    const t = line.trim();
    if (t && /^[A-Za-z]:[\\/]/.test(t)) out.push(t);
  }
  const uris = dt.getData("text/uri-list") || "";
  pushPathsFromTextBlock(uris, out);

  /* WebView2 / Shell sometimes expose paths only on custom MIME types at drop time. */
  try {
    const typeList = Array.from(dt.types ?? []);
    for (let i = 0; i < typeList.length; i += 1) {
      const ty = typeList[i];
      if (!ty || ty === "Files" || ty === "text/plain" || ty === "text/uri-list") continue;
      let raw: string;
      try {
        raw = dt.getData(ty);
      } catch {
        continue;
      }
      if (typeof raw === "string" && raw.trim()) pushPathsFromTextBlock(raw, out);
    }
  } catch {
    /* */
  }

  const seen = new Set<string>();
  const norm = (s: string) => String(s).replace(/\//g, "\\").toLowerCase();
  return out
    .map((x) => String(x).trim())
    .filter((x) => x.length > 0)
    .filter((x) => {
      const k = norm(x);
      if (seen.has(k)) return false;
      seen.add(k);
      return true;
    });
}

export function isFileLikeExternalDrag(
  e: { readonly dataTransfer: DataTransfer | null } | null | undefined,
): boolean {
  const dt = e?.dataTransfer;
  if (!dt) return false;
  const types = Array.from(dt.types);
  if (types.includes("Files") || types.includes("text/uri-list")) return true;
  try {
    for (let i = 0; i < dt.items.length; i += 1) {
      if (dt.items[i]?.kind === "file") return true;
    }
  } catch {
    /* */
  }
  return false;
}

/** Capture-phase window listeners so drops on nested elements do not navigate. */
export function installContextFileDrop(): () => void {
  const onDragEnter = (e: DragEvent) => {
    if (!isFileLikeExternalDrag(e)) return;
    e.preventDefault();
  };
  const onDragOver = (e: DragEvent) => {
    if (!isFileLikeExternalDrag(e)) return;
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = "copy";
  };
  const onDrop = (e: DragEvent) => {
    if (!isFileLikeExternalDrag(e)) return;
    e.preventDefault();
    e.stopPropagation();
    const paths = collectPathsFromDataTransfer(e.dataTransfer);
    if (paths.length && hasWebProviderHost()) postHost({ kind: "addContextPaths", paths });
  };
  window.addEventListener("dragenter", onDragEnter, true);
  window.addEventListener("dragover", onDragOver, true);
  window.addEventListener("drop", onDrop, true);
  return () => {
    window.removeEventListener("dragenter", onDragEnter, true);
    window.removeEventListener("dragover", onDragOver, true);
    window.removeEventListener("drop", onDrop, true);
  };
}
