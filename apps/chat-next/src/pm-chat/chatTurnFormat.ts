import type { ChatEntry } from "./pmChatStore";
import { t } from "./i18n.js";
import { llmUsageMarkdownBlock } from "./llmUsageFormat";

/** Last path segment (folder name), not the full path. */
export function folderDisplayName(folderPath: string): string {
  const s = String(folderPath || "")
    .trim()
    .replace(/[/\\]+$/gu, "");
  if (!s) return "";
  const i = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"));
  const leaf = (i >= 0 ? s.slice(i + 1) : s).trim();
  return leaf || s;
}

/** Normalize slashes for prefix compare (Windows paths). */
function normPath(p: string): string {
  return String(p || "")
    .trim()
    .replace(/\\/gu, "/");
}

/** If `abs` is under `folder`, return relative POSIX-ish path; else return original. */
export function pathRelativeToFolder(abs: string, folder: string): string {
  const a = normPath(abs);
  const f0 = normPath(folder).replace(/\/+$/u, "");
  if (!f0 || !a) return abs.trim();
  const prefix = f0.endsWith("/") ? f0 : `${f0}/`;
  if (a.length >= prefix.length && a.slice(0, prefix.length).toLowerCase() === prefix.toLowerCase()) {
    return a.slice(prefix.length) || ".";
  }
  return abs.trim();
}

export function formatChatRelativeTime(ts: number, nowMs: number): string {
  if (!Number.isFinite(ts)) return "";
  const sec = Math.max(0, Math.floor((nowMs - ts) / 1000));
  if (sec < 45) return t("timeRelativeNow");
  const min = Math.floor(sec / 60);
  if (min < 60) return min === 1 ? t("timeRelativeOneMin") : t("timeRelativeMins", { n: min });
  const hr = Math.floor(min / 60);
  if (hr < 48) return hr === 1 ? t("timeRelativeOneHr") : t("timeRelativeHrs", { n: hr });
  const day = Math.floor(hr / 24);
  return day === 1 ? t("timeRelativeOneDay") : t("timeRelativeDays", { n: day });
}

export function buildUserMarkdown(e: ChatEntry): string {
  const lines: string[] = [];
  const iso = typeof e.ts === "number" && Number.isFinite(e.ts) ? new Date(e.ts).toISOString() : "";
  const rel = typeof e.ts === "number" && Number.isFinite(e.ts) ? formatChatRelativeTime(e.ts, Date.now()) : "";
  lines.push("# User message");
  if (iso) lines.push(`**When:** ${rel} (${iso})`);
  const folder = (e.folderHint || "").trim();
  if (folder) lines.push(`**Folder:** \`${folder}\``);
  lines.push("");
  lines.push("## Prompt");
  lines.push(e.text || "");
  const ctx = e.contextPaths?.filter((x) => x.trim()) ?? [];
  if (ctx.length) {
    lines.push("");
    lines.push("## Context files");
    for (const p of ctx) lines.push(`- \`${pathRelativeToFolder(p, folder)}\``);
  }
  const out = e.resultPaths?.filter((x) => x.trim()) ?? [];
  if (out.length) {
    lines.push("");
    lines.push("## Generated / result files");
    for (const p of out) lines.push(`- \`${pathRelativeToFolder(p, folder)}\``);
  }
  if (e.llmUsage) {
    const md = llmUsageMarkdownBlock(e.llmUsage);
    if (md) lines.push(md);
  }
  return lines.join("\n");
}

export function findPrecedingUserIndex(entries: ChatEntry[], beforeIndex: number): number {
  for (let k = beforeIndex - 1; k >= 0; k -= 1) {
    if (entries[k].role === "user") return k;
  }
  return -1;
}

export function buildAssistantTurnMarkdown(entries: ChatEntry[], assistantIndex: number): string {
  const e = entries[assistantIndex];
  if (!e || e.role !== "assistant") return e?.text || "";
  const u = findPrecedingUserIndex(entries, assistantIndex);
  const lines: string[] = [];
  lines.push("# Chat turn");
  if (u >= 0) {
    const usr = entries[u];
    const folder = (usr.folderHint || "").trim();
    const isoU = typeof usr.ts === "number" && Number.isFinite(usr.ts) ? new Date(usr.ts).toISOString() : "";
    const relU = typeof usr.ts === "number" && Number.isFinite(usr.ts) ? formatChatRelativeTime(usr.ts, Date.now()) : "";
    if (isoU) lines.push(`**User sent:** ${relU} (${isoU})`);
    if (folder) lines.push(`**Folder:** \`${folder}\``);
    lines.push("");
    lines.push("## Your prompt");
    lines.push(usr.text || "");
    const ctx = usr.contextPaths?.filter((x) => x.trim()) ?? [];
    if (ctx.length) {
      lines.push("");
      lines.push("## Context files");
      for (const p of ctx) lines.push(`- \`${pathRelativeToFolder(p, folder)}\``);
    }
    const rp = usr.resultPaths?.filter((x) => x.trim()) ?? [];
    if (rp.length) {
      lines.push("");
      lines.push("## Generated / result files");
      for (const p of rp) lines.push(`- \`${pathRelativeToFolder(p, folder)}\``);
    }
    const tools: string[] = [];
    for (let k = u + 1; k < assistantIndex; k += 1) {
      const x = entries[k];
      if (x.role === "tool" || x.role === "file") tools.push(x.text || "");
    }
    if (tools.length) {
      lines.push("");
      lines.push("## Tool activity");
      for (let ti = 0; ti < tools.length; ti += 1) {
        lines.push("");
        lines.push(`### Tool ${ti + 1}`);
        lines.push("```");
        lines.push(tools[ti]);
        lines.push("```");
      }
    }
  }
  const isoA = typeof e.ts === "number" && Number.isFinite(e.ts) ? new Date(e.ts).toISOString() : "";
  const relA = typeof e.ts === "number" && Number.isFinite(e.ts) ? formatChatRelativeTime(e.ts, Date.now()) : "";
  lines.push("");
  lines.push("## Assistant");
  if (isoA) lines.push(`*${relA} (${isoA})*`);
  lines.push("");
  lines.push(e.text || "");
  const usageAgg =
    e.llmUsage ?? (u >= 0 && entries[u].llmUsage ? entries[u].llmUsage : undefined);
  if (usageAgg) {
    const md = llmUsageMarkdownBlock(usageAgg);
    if (md) lines.push(md);
  }
  return lines.join("\n");
}
