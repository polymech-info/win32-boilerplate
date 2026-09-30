export function escapeHtml(s: unknown): string {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

export function escapeAttr(s: unknown): string {
  return escapeHtml(s);
}

/** Compact size for toolbar labels (binary KB / MB). */
export function formatCompactByteSize(bytes: number): string {
  const n = Math.max(0, Math.floor(Number(bytes) || 0));
  if (n < 1024) return `${n} B`;
  const kb = n / 1024;
  if (kb < 1024) {
    const s = kb >= 100 ? String(Math.round(kb)) : (Math.round(kb * 10) / 10).toFixed(1).replace(/\.0$/u, "");
    return `${s} KB`;
  }
  const mb = kb / 1024;
  const s = mb >= 100 ? String(Math.round(mb)) : (Math.round(mb * 10) / 10).toFixed(1).replace(/\.0$/u, "");
  return `${s} MB`;
}

export async function copyTextToClipboard(text: string): Promise<void> {
  if (!text) return;
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      throw new Error("no clipboard");
    }
  } catch {
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "fixed";
    ta.style.left = "-9999px";
    document.body.appendChild(ta);
    ta.select();
    try {
      document.execCommand("copy");
    } finally {
      document.body.removeChild(ta);
    }
  }
}
