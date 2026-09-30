import DOMPurify from "dompurify";
import { marked } from "marked";

let configured = false;

export function rewriteLocalPath(p: string): string {
  if (!p) return p;
  if (/^(https?|data):/i.test(p)) return p;
  let s = p;
  if (/^pm-files:\/\//i.test(s)) {
    s = s.replace(/^pm-files:\/\//i, "");
  } else if (/^file:\/\/\//i.test(s)) {
    s = decodeURIComponent(s.replace(/^file:\/\/\//i, ""));
  }
  const m = /^([A-Za-z]):[\\/](.*)$/.exec(s);
  if (m) {
    const letter = m[1].toLowerCase();
    const rest = m[2].replace(/\\/g, "/");
    return `https://pm-files-${letter}.local/${rest}`;
  }
  if (s.startsWith("/")) {
    return "file://" + s;
  }
  return "file:///" + s.replace(/\\/g, "/").replace(/^\/+/, "");
}

/** Match Windows absolute paths (C:\...) OR bare filenames with media-relevant extensions */
const LOCAL_PATH_REGEX = /[A-Za-z]:[\\/][^\s<>"{}|\\]+(?:[\\/][^\s<>"{}|\\]*)*|[\w][\w. _-]{2,}\.(?:mp4|mov|avi|mkv|webm|jpg|jpeg|png|gif|webp|tiff?|cr2|cr3|raw|arw|nef|dng|heic|pdf|svg|psd|ai|eps|zip|mp3|wav|aac)/gi;

/** Returns true if the matched text is a bare filename (no path separator) */
function isBareFilename(p: string): boolean {
  return !p.includes("\\") && !p.includes("/");
}

/**
 * Walk DOM text nodes and linkify local paths within them (safe - never touches existing tags).
 * @param folderHint  Optional folder context (from the chat entry's folderHint) used to
 *                    resolve bare filenames into absolute paths for ShellExecuteW.
 */
function linkifyLocalPathsInDom(root: Element, folderHint?: string): void {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
    acceptNode(node) {
      const parent = node.parentElement;
      if (!parent) return NodeFilter.FILTER_SKIP;
      // Skip inside existing local-path links (already processed)
      if (parent.closest("a")) return NodeFilter.FILTER_SKIP;
      // Skip <pre> only when it's inside .pm-md (markdown fenced code blocks)
      // Tool-call <pre> elements are outside .pm-md and should be linkified.
      const pre = parent.closest("pre");
      if (pre && pre.closest(".pm-md")) return NodeFilter.FILTER_SKIP;
      // Skip <code> only when inside a markdown <pre> block
      const code = parent.closest("code");
      if (code && code.closest("pre.pm-md, .pm-md pre")) return NodeFilter.FILTER_SKIP;
      return NodeFilter.FILTER_ACCEPT;
    },
  });
  const nodes: Text[] = [];
  while (walker.nextNode()) nodes.push(walker.currentNode as Text);

  let linkCount = 0;
  for (const textNode of nodes) {
    const text = textNode.textContent ?? "";
    LOCAL_PATH_REGEX.lastIndex = 0;
    if (!LOCAL_PATH_REGEX.test(text)) continue;
    LOCAL_PATH_REGEX.lastIndex = 0;
    const frag = document.createDocumentFragment();
    let last = 0;
    let m: RegExpExecArray | null;
    while ((m = LOCAL_PATH_REGEX.exec(text)) !== null) {
      if (m.index > last) frag.appendChild(document.createTextNode(text.slice(last, m.index)));
      // Resolve bare filenames against the folder hint so ShellExecuteW gets a full path
      const rawMatch = m[0];
      const resolvedPath =
        isBareFilename(rawMatch) && folderHint
          ? folderHint.replace(/[\\/]+$/, "") + "\\" + rawMatch
          : rawMatch;
      const a = document.createElement("a");
      a.className = "pm-local-link";
      a.setAttribute("data-path", encodeURIComponent(resolvedPath));
      a.setAttribute("href", "#");
      a.textContent = rawMatch;
      frag.appendChild(a);
      linkCount++;
      last = m.index + m[0].length;
    }
    if (last < text.length) frag.appendChild(document.createTextNode(text.slice(last)));
    textNode.parentNode?.replaceChild(frag, textNode);
  }
  if (linkCount > 0) {
    console.log(`[pm-chat] linkifyLocalPathsInDom: created ${linkCount} path link(s) in`, root);
  }
}

export function ensureMarkdownConfigured(): void {
  if (configured) return;
  configured = true;
  marked.setOptions({
    gfm: true,
    breaks: false,
    pedantic: false,
  });
  DOMPurify.addHook("uponSanitizeAttribute", (node, hookEvent) => {
    if (hookEvent.attrName === "src" && node && (node as Element).tagName === "IMG") {
      hookEvent.attrValue = rewriteLocalPath(String(hookEvent.attrValue ?? ""));
    }
  });
}

export function renderAssistantMarkdownToHtml(text: string): string {
  ensureMarkdownConfigured();
  const raw = marked.parse(String(text ?? "")) as string;
  return DOMPurify.sanitize(raw);
}

/** Call this after mounting the HTML into the DOM to linkify local paths safely. */
export { linkifyLocalPathsInDom };
