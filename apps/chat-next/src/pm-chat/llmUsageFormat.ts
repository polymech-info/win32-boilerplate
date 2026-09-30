import { t } from "./i18n.js";

/** Aggregated usage from all chat completion HTTP rounds in one user turn (host / OpenAI-style APIs). */
export type TurnLlmUsageAggregate = {
  prompt_tokens?: number;
  completion_tokens?: number;
  total_tokens?: number;
  /** OpenRouter-style credits in USD when present. */
  cost?: number;
  llm_rounds?: unknown[];
};

function numField(v: unknown): number | undefined {
  if (typeof v === "number" && Number.isFinite(v)) return v;
  if (typeof v === "string" && /^\d+$/u.test(v.trim())) return Number(v.trim());
  return undefined;
}

/** Coerce host JSON into a safe aggregate; returns undefined when nothing to show. */
export function normalizeTurnLlmUsage(raw: unknown): TurnLlmUsageAggregate | undefined {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const o = raw as Record<string, unknown>;
  const prompt_tokens = numField(o.prompt_tokens);
  const completion_tokens = numField(o.completion_tokens);
  const total_tokens = numField(o.total_tokens);
  const cost = typeof o.cost === "number" && Number.isFinite(o.cost) ? o.cost : undefined;
  const rounds = Array.isArray(o.llm_rounds) ? o.llm_rounds : undefined;
  const hasRounds = rounds && rounds.length > 0;
  const hasTok =
    (prompt_tokens != null && prompt_tokens > 0) ||
    (completion_tokens != null && completion_tokens > 0) ||
    (total_tokens != null && total_tokens > 0);
  const hasCost = cost != null && Number.isFinite(cost);
  if (!hasTok && !hasCost && !hasRounds) return undefined;
  return {
    ...(prompt_tokens != null ? { prompt_tokens } : {}),
    ...(completion_tokens != null ? { completion_tokens } : {}),
    ...(total_tokens != null ? { total_tokens } : {}),
    ...(hasCost ? { cost } : {}),
    ...(rounds ? { llm_rounds: rounds } : {}),
  };
}

function fmtCost(c: number): string {
  if (!Number.isFinite(c) || c <= 0) return "0";
  if (c < 0.0001) return c.toExponential(2);
  if (c < 1) return c.toFixed(4).replace(/\.?0+$/u, "") || "0";
  return c.toLocaleString(undefined, { maximumFractionDigits: 4, minimumFractionDigits: 0 });
}

/** One muted line for the transcript bubble footer. */
export function llmUsageFooterText(u: TurnLlmUsageAggregate): string {
  const pi = u.prompt_tokens ?? 0;
  const co = u.completion_tokens ?? 0;
  const to = u.total_tokens ?? pi + co;
  const n = Array.isArray(u.llm_rounds) ? u.llm_rounds.length : 0;
  if (to <= 0 && pi <= 0 && co <= 0 && (u.cost == null || !Number.isFinite(u.cost)) && n > 0) {
    return t("llmUsageFooterRoundsOnly", { n: String(n) });
  }
  const costLine =
    u.cost != null && Number.isFinite(u.cost) && u.cost !== 0
      ? t("llmUsageFooterCost", { cost: fmtCost(u.cost) })
      : "";
  return t("llmUsageFooter", {
    in: String(pi),
    out: String(co),
    total: String(to),
    costLine,
  });
}

/** Markdown block appended when copying assistant turn as Markdown. */
export function llmUsageMarkdownBlock(u: TurnLlmUsageAggregate): string {
  const bullets: string[] = [];
  const pi = u.prompt_tokens;
  const co = u.completion_tokens;
  const to = u.total_tokens;
  if (pi != null && pi > 0) bullets.push(`- **${t("llmUsageMdPrompt")}:** ${pi}`);
  if (co != null && co > 0) bullets.push(`- **${t("llmUsageMdCompletion")}:** ${co}`);
  if (to != null && to > 0) bullets.push(`- **${t("llmUsageMdTotal")}:** ${to}`);
  if (u.cost != null && Number.isFinite(u.cost) && u.cost !== 0) {
    bullets.push(`- **${t("llmUsageMdCost")}:** ${fmtCost(u.cost)} USD`);
  }
  const n = Array.isArray(u.llm_rounds) ? u.llm_rounds.length : 0;
  if (n > 0) bullets.push(`- **${t("llmUsageMdRounds")}:** ${n}`);
  if (!bullets.length) return "";
  return `\n## ${t("llmUsageMdTitle")}\n${bullets.join("\n")}`;
}
