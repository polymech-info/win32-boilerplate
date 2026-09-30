import React, { useEffect, useState } from "react";
import {
  ReactFlow,
  Background,
  Controls,
  MiniMap,
  type Node,
  type Edge,
  useNodesState,
  useEdgesState,
  type NodeMouseHandler,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import {
  Play, Square, Bot, Wrench, CheckCircle, XCircle, MessageSquare, X,
  ChevronDown, ChevronRight, Link, Zap, User, Brain,
} from "lucide-react";
import { renderAssistantMarkdownToHtml } from "./markdownSetup";
import type { ChatEntry, TurnLlmUsageAggregate } from "./pmChatStore";
import { usePmChatStore } from "./pmChatStore";

function useIsDark(): boolean {
  const [dark, setDark] = useState(() => document.documentElement.classList.contains("dark"));
  useEffect(() => {
    const obs = new MutationObserver(() => setDark(document.documentElement.classList.contains("dark")));
    obs.observe(document.documentElement, { attributes: true, attributeFilter: ["class"] });
    return () => obs.disconnect();
  }, []);
  return dark;
}

// ── Node data type ────────────────────────────────────────────────────────────

type NodeData = Record<string, unknown> & {
  label: string;
  subtitle?: string;
  details?: Record<string, string>;
  ok?: boolean;
};

// ── Node components ───────────────────────────────────────────────────────────

function StartNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected ? "border-emerald-600 bg-emerald-200 dark:border-emerald-400 dark:bg-emerald-900 ring-2 ring-emerald-500"
               : "border-emerald-500 bg-emerald-100 dark:border-emerald-500 dark:bg-emerald-950"
    } text-emerald-900 dark:text-emerald-100`}>
      <div className="flex items-center gap-2">
        <Play className="h-4 w-4 shrink-0" />
        <span className="text-sm font-semibold">{data.label}</span>
      </div>
      {data.subtitle && <div className="mt-1 truncate text-[10px] opacity-80">{data.subtitle}</div>}
    </div>
  );
}

function LLMNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected ? "border-blue-600 bg-blue-200 dark:border-blue-400 dark:bg-blue-900 ring-2 ring-blue-500"
               : "border-blue-500 bg-blue-100 dark:border-blue-500 dark:bg-blue-950"
    } text-blue-900 dark:text-blue-100`}>
      <div className="flex items-center gap-2">
        <Bot className="h-4 w-4 shrink-0" />
        <span className="text-sm font-semibold">{data.label}</span>
      </div>
      {data.subtitle && <div className="mt-1 truncate text-[10px] opacity-80">{data.subtitle}</div>}
    </div>
  );
}

function ToolCallNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected ? "border-amber-600 bg-amber-200 dark:border-amber-400 dark:bg-amber-900 ring-2 ring-amber-500"
               : "border-amber-500 bg-amber-100 dark:border-amber-500 dark:bg-amber-950"
    } text-amber-900 dark:text-amber-100`}>
      <div className="flex items-center gap-2">
        <Wrench className="h-4 w-4 shrink-0" />
        <span className="text-sm font-semibold">{data.label}</span>
      </div>
      {data.subtitle && <div className="mt-1 truncate text-[10px] opacity-80">{data.subtitle}</div>}
    </div>
  );
}

function ToolResultNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  const isOk = data.ok;
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected
        ? isOk ? "border-green-600 bg-green-200 dark:border-green-400 dark:bg-green-900 ring-2 ring-green-500"
               : "border-red-600 bg-red-200 dark:border-red-400 dark:bg-red-900 ring-2 ring-red-500"
        : isOk ? "border-green-500 bg-green-100 dark:border-green-500 dark:bg-green-950"
               : "border-red-500 bg-red-100 dark:border-red-500 dark:bg-red-950"
    } ${isOk ? "text-green-900 dark:text-green-100" : "text-red-900 dark:text-red-100"}`}>
      <div className="flex items-center gap-2">
        {isOk ? <CheckCircle className="h-4 w-4 shrink-0" /> : <XCircle className="h-4 w-4 shrink-0" />}
        <span className="text-sm font-semibold">{data.label}</span>
      </div>
      {data.subtitle && <div className="mt-1 truncate text-[10px] opacity-80">{data.subtitle}</div>}
    </div>
  );
}

function AssistantNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected ? "border-purple-600 bg-purple-200 dark:border-purple-400 dark:bg-purple-900 ring-2 ring-purple-500"
               : "border-purple-500 bg-purple-100 dark:border-purple-500 dark:bg-purple-950"
    } text-purple-900 dark:text-purple-100`}>
      <div className="flex items-center gap-2">
        <MessageSquare className="h-4 w-4 shrink-0" />
        <span className="text-sm font-semibold">{data.label}</span>
      </div>
      {data.subtitle && <div className="mt-1 truncate text-[10px] opacity-80">{data.subtitle}</div>}
    </div>
  );
}

function EndNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected ? "border-gray-600 bg-gray-300 dark:border-gray-400 dark:bg-gray-800 ring-2 ring-gray-500"
               : "border-gray-500 bg-gray-200 dark:border-gray-500 dark:bg-gray-900"
    } text-gray-900 dark:text-gray-100`}>
      <div className="flex items-center gap-2">
        <Square className="h-4 w-4 shrink-0" />
        <span className="text-sm font-semibold">{data.label}</span>
      </div>
      {data.subtitle && <div className="mt-1 truncate text-[10px] opacity-80">{data.subtitle}</div>}
    </div>
  );
}

const nodeTypes = {
  start: StartNode,
  llm: LLMNode,
  toolCall: ToolCallNode,
  toolResult: ToolResultNode,
  assistant: AssistantNode,
  end: EndNode,
};

// ── Tool text parsing ─────────────────────────────────────────────────────────
// ToolCall entries: "⚡ image_compress · /path/to/file"  (no ✓/✗)
// ToolResult entries: "⚡ image_compress ✓ 2/2"          (has ✓ or ✗)

function parseToolEntry(text: string): { toolName: string; isResult: boolean; ok: boolean; status: string } {
  const stripped = text.replace(/^⚡\s*/, "").trim();
  // Detect result by ✓ or ✗ markers
  const okMatch = stripped.match(/^(\S+)\s+✓\s*(.*)$/u);
  if (okMatch) return { toolName: okMatch[1], isResult: true, ok: true,  status: okMatch[2].trim() || "ok" };
  const failMatch = stripped.match(/^(\S+)\s+✗\s*(.*)$/u) ?? stripped.match(/^(\S+)\s+\((\d+ failed.*)\)$/);
  if (failMatch) return { toolName: failMatch[1], isResult: true, ok: false, status: failMatch[2]?.trim() || "failed" };
  // It's a call entry — extract tool name (first word)
  const callMatch = stripped.match(/^(\S+)\s*(.*)/);
  const toolName = callMatch ? callMatch[1] : stripped;
  const args = callMatch ? callMatch[2].replace(/^·\s*/, "").trim() : "";
  return { toolName, isResult: false, ok: true, status: args };
}

// ── LLM round helpers ─────────────────────────────────────────────────────────

type LlmRound = {
  provider?: string;  // stamped by agent.cpp from ProviderSettings::router (e.g. "openrouter")
  model?: string;     // model id from the API response body (e.g. "anthropic/claude-4.6-sonnet-20260217")
  id?: string;
  usage?: { prompt_tokens?: number; completion_tokens?: number; cost?: number };
};

/**
 * Resolved routing provider (e.g. "openrouter", "anthropic", "openai").
 * Only uses the explicitly stamped `provider` field — never infers from the model string,
 * because "anthropic/claude-..." is OpenRouter's model-slug convention, not the router name.
 */
function resolveProvider(r: LlmRound): string {
  return r.provider?.trim() ?? "";
}

/**
 * Model id as returned by the API. Shown as-is — it may include an org prefix
 * (e.g. "anthropic/claude-4.6-sonnet-20260217") which is the model slug, not the provider.
 */
function resolveModel(r: LlmRound): string {
  return r.model ?? "";
}

function roundDetails(r: LlmRound): Record<string, string> {
  const d: Record<string, string> = {};
  const provider = resolveProvider(r);
  const model    = resolveModel(r);
  if (provider) d.Provider = provider;
  if (model)    d.Model    = model;
  if (r.id) d["Round ID"] = r.id;
  if (r.usage?.prompt_tokens != null) d["Prompt tokens"] = r.usage.prompt_tokens.toLocaleString();
  if (r.usage?.completion_tokens != null) d["Completion tokens"] = r.usage.completion_tokens.toLocaleString();
  if (r.usage?.cost) d.Cost = `$${r.usage.cost.toFixed(4)}`;
  return d;
}

function roundSubtitle(r: LlmRound): string {
  const m   = resolveModel(r);
  const tok = r.usage?.completion_tokens;
  if (m && tok) return `${m} · ${tok} tok`;
  if (m)        return m;
  if (tok)      return `${tok} tok`;
  return "";
}

// ── Turn grouping ─────────────────────────────────────────────────────────────

type Turn = { user: ChatEntry; tools: ChatEntry[]; assistant: ChatEntry | null };

function groupIntoTurns(entries: ChatEntry[]): Turn[] {
  const turns: Turn[] = [];
  let i = 0;
  while (i < entries.length) {
    const e = entries[i];
    if (e.role === "user") {
      const tools: ChatEntry[] = [];
      let j = i + 1;
      // Collect tool/file entries for this turn, skipping over mid-turn
      // entries like system (setRunFolder), shell (run tool), image, error
      // that can appear between the user prompt and the final assistant reply.
      while (j < entries.length) {
        const r = entries[j].role;
        if (r === "user" || r === "assistant") break;
        if (r === "tool" || r === "file") tools.push(entries[j]);
        j++;
      }
      const assistant =
        j < entries.length && entries[j].role === "assistant" ? entries[j] : null;
      turns.push({ user: e, tools, assistant });
      i = assistant ? j + 1 : j;
    } else {
      i++;
    }
  }
  return turns;
}

// ── Flow builder from entries ─────────────────────────────────────────────────

function buildFlowFromEntries(entries: ChatEntry[]): { nodes: Node<NodeData>[]; edges: Edge[] } {
  const turns = groupIntoTurns(entries);
  if (turns.length === 0) return { nodes: [], edges: [] };

  const nodes: Node<NodeData>[] = [];
  const edges: Edge[] = [];
  let nodeId = 0;
  const centerX = 180;
  let currentY = 20;
  const gapY = 24;

  function addNode(type: string, data: NodeData): string {
    const id = `node-${nodeId++}`;
    nodes.push({ id, type, position: { x: centerX, y: currentY }, data });
    currentY += 60 + gapY;
    return id;
  }

  function addEdge(source: string, target: string, opts?: Partial<Edge>) {
    edges.push({ id: `e-${source}-${target}`, source, target, type: "smoothstep", ...opts });
  }

  // Start node from first user prompt
  const firstUser = turns[0].user;
  let lastId = addNode("start", {
    label: "Start",
    subtitle: firstUser.text.slice(0, 50),
    details: { Prompt: firstUser.text },
  });

  for (let ti = 0; ti < turns.length; ti++) {
    const turn = turns[ti];

    // If this is not the first turn, show the user message as a continuation node
    if (ti > 0) {
      const contId = addNode("start", {
        label: `Turn ${ti + 1}`,
        subtitle: turn.user.text.slice(0, 50),
        details: { Prompt: turn.user.text },
      });
      addEdge(lastId, contId);
      lastId = contId;
    }

    // llm_rounds — prefer from user (set by setTurnLlmUsage), fall back to assistant
    const usage: TurnLlmUsageAggregate | undefined = turn.user.llmUsage ?? turn.assistant?.llmUsage;
    const rawRounds = Array.isArray(usage?.llm_rounds) ? (usage!.llm_rounds as LlmRound[]) : [];

    // Parse tool entries into call/result pairs
    type ToolPair = { call: ChatEntry | null; result: ChatEntry | null; toolName: string; ok: boolean; status: string; callArgs: string };
    const toolPairs: ToolPair[] = [];
    {
      let pending: ChatEntry | null = null;
      let pendingParsed: ReturnType<typeof parseToolEntry> | null = null;
      for (const te of turn.tools) {
        const parsed = parseToolEntry(te.text);
        if (!parsed.isResult) {
          // New call — flush any orphan pending call without a result
          if (pending) toolPairs.push({ call: pending, result: null, toolName: pendingParsed!.toolName, ok: true, status: "", callArgs: pendingParsed!.status });
          pending = te;
          pendingParsed = parsed;
        } else {
          // Result — pair with pending call (or standalone if none)
          toolPairs.push({ call: pending, result: te, toolName: parsed.toolName, ok: parsed.ok, status: parsed.status, callArgs: pendingParsed?.status ?? "" });
          pending = null;
          pendingParsed = null;
        }
      }
      if (pending) toolPairs.push({ call: pending, result: null, toolName: pendingParsed!.toolName, ok: true, status: "pending", callArgs: pendingParsed!.status });
    }

    // Build LLM + tool nodes
    // Strategy:
    //   - If 0 rounds info: single LLM → tools → Response
    //   - If N rounds: LLM_1 → all tools → LLM_2..N (final round) → Response
    //     (we don't know per-round tool split from entries, so all tools go after round 1)
    const numRounds = rawRounds.length;

    // Model that drove the tool calls (round 0, or empty if unknown)
    const triggerRound: LlmRound | null = numRounds > 0 ? rawRounds[0] : null;

    if (numRounds === 0) {
      // No round data — single LLM block with no details
      const llmId = addNode("llm", { label: "LLM" });
      addEdge(lastId, llmId, { animated: true });
      lastId = llmId;
    } else {
      // First round
      const r0 = rawRounds[0];
      const llmId = addNode("llm", {
        label: numRounds > 1 ? "LLM 1" : "LLM",
        subtitle: roundSubtitle(r0),
        details: roundDetails(r0),
      });
      addEdge(lastId, llmId, { animated: true });
      lastId = llmId;
    }

    // Base details for a tool call node (no provider — agent LLM is on the preceding LLM node).
    // For result nodes, we additionally show the tool's own execution provider/model when
    // available (stamped by tools like image_understand that invoke a vision LLM internally).
    function toolCallDetails(toolName: string): Record<string, string> {
      const d: Record<string, string> = { Tool: toolName };
      if (triggerRound?.id) d["Triggered by"] = triggerRound.id;
      return d;
    }
    function toolResultDetails(toolName: string, resultEntry: ChatEntry | null): Record<string, string> {
      const d: Record<string, string> = { Tool: toolName };
      if (resultEntry?.toolProvider) d.Provider = resultEntry.toolProvider;
      if (resultEntry?.toolModel)    d.Model    = resultEntry.toolModel;
      if (triggerRound?.id) d["Triggered by"] = triggerRound.id;
      return d;
    }

    // Tool call / result pairs
    for (const pair of toolPairs) {
      const callDetails = toolCallDetails(pair.toolName);
      if (pair.callArgs) callDetails.Args = pair.callArgs;
      const callId = addNode("toolCall", {
        label: "Tool Call",
        subtitle: pair.toolName,
        details: callDetails,
      });
      addEdge(lastId, callId, { animated: true });
      lastId = callId;

      if (pair.result !== null) {
        const resultDetails = toolResultDetails(pair.toolName, pair.result);
        resultDetails.Status = pair.ok ? "Success" : "Failed";
        if (pair.status) resultDetails.Summary = pair.status;
        const resultId = addNode("toolResult", {
          label: pair.ok ? "✓ Success" : "✗ Error",
          subtitle: pair.status,
          ok: pair.ok,
          details: resultDetails,
        });
        addEdge(lastId, resultId, { style: { stroke: pair.ok ? "#22c55e" : "#ef4444", strokeWidth: 2 } });
        lastId = resultId;
      }
    }

    // Remaining LLM rounds (rounds 2..N)
    for (let ri = 1; ri < numRounds; ri++) {
      const r = rawRounds[ri];
      const llmId = addNode("llm", {
        label: `LLM ${ri + 1}`,
        subtitle: roundSubtitle(r),
        details: roundDetails(r),
      });
      addEdge(lastId, llmId, { animated: true });
      lastId = llmId;
    }

    // Response (assistant text)
    if (turn.assistant) {
      const text = turn.assistant.text;
      const respId = addNode("assistant", {
        label: "Response",
        subtitle: `"${text.slice(0, 50)}${text.length > 50 ? "…" : ""}"`,
        details: { "Full response": text },
      });
      addEdge(lastId, respId, { animated: true });
      lastId = respId;
    }
  }

  // End node with aggregate usage
  const lastTurn = turns[turns.length - 1];
  const totalUsage = lastTurn.user.llmUsage ?? lastTurn.assistant?.llmUsage;
  const endDetails: Record<string, string> = {};
  if (totalUsage) {
    if (totalUsage.total_tokens) endDetails["Total tokens"] = totalUsage.total_tokens.toLocaleString();
    if (totalUsage.cost) endDetails["Total cost"] = `$${totalUsage.cost.toFixed(4)}`;
    const nRounds = Array.isArray(totalUsage.llm_rounds) ? totalUsage.llm_rounds.length : 0;
    if (nRounds) endDetails.Rounds = String(nRounds);
  }
  const totalTurns = turns.length;
  const endId = addNode("end", {
    label: "End",
    subtitle: [
      totalTurns > 1 ? `${totalTurns} turns` : "",
      totalUsage?.cost ? `$${totalUsage.cost.toFixed(3)}` : "",
    ].filter(Boolean).join(" · "),
    details: endDetails,
  });
  addEdge(lastId, endId);

  return { nodes, edges };
}

// ── Details panel ─────────────────────────────────────────────────────────────

function formatJsonValue(value: string): React.ReactNode {
  if (!value.trim().startsWith("{") && !value.trim().startsWith("[")) {
    return <span className="text-slate-700 dark:text-slate-300 whitespace-pre-wrap break-words">{value}</span>;
  }
  try {
    const parsed = JSON.parse(value);
    const formatted = JSON.stringify(parsed, null, 2);
    const lines = formatted.split("\n");
    return (
      <div className="font-mono text-xs leading-relaxed">
        {lines.map((line, i) => {
          const keyMatch = line.match(/^(\s*)("[^"]+")(:)/);
          if (keyMatch) {
            const [, indent, key, colon] = keyMatch;
            const rest = line.slice(keyMatch[0].length);
            return (
              <div key={i} className="whitespace-pre">
                <span className="text-slate-400">{indent}</span>
                <span className="text-blue-600 dark:text-blue-400">{key}</span>
                <span className="text-slate-500">{colon}</span>
                <span className={rest.includes('"') ? "text-green-600 dark:text-green-400" : "text-amber-600 dark:text-amber-400"}>{rest}</span>
              </div>
            );
          }
          return <div key={i} className={`whitespace-pre ${line.includes('"') ? "text-green-600 dark:text-green-400" : "text-amber-600 dark:text-amber-400"}`}>{line}</div>;
        })}
      </div>
    );
  } catch {
    return <span className="text-slate-700 dark:text-slate-300 whitespace-pre-wrap break-words">{value}</span>;
  }
}

function DetailsPanel({ node, onClose }: { node: Node<NodeData> | null; onClose: () => void }) {
  if (!node) return null;
  const details = node.data.details ?? {};
  const hasDetails = Object.keys(details).length > 0;
  return (
    <div className="absolute right-0 top-0 bottom-0 w-80 border-l border-slate-200 shadow-xl dark:border-slate-700 z-10 flex flex-col bg-white dark:bg-surface-dark">
      <div className="flex items-center justify-between border-b border-slate-200 px-4 py-3 dark:border-slate-700">
        <span className="font-semibold text-slate-900 dark:text-slate-100">{node.data.label}</span>
        <button type="button" onClick={onClose} className="rounded p-1 hover:bg-slate-200 dark:hover:bg-slate-700">
          <X className="h-4 w-4" />
        </button>
      </div>
      <div className="flex-1 overflow-y-auto p-4">
        {node.data.subtitle && (
          <div className="mb-4 rounded p-2 text-sm text-slate-700 dark:text-slate-300">{node.data.subtitle}</div>
        )}
        {hasDetails ? (
          <div className="space-y-4">
            {Object.entries(details).map(([key, value]) => (
              <div key={key} className="rounded border border-slate-100 dark:border-slate-800">
                <div className="border-b border-slate-100 px-3 py-1.5 text-[10px] font-bold uppercase tracking-wide text-slate-500 dark:border-slate-800 dark:text-slate-400">{key}</div>
                <div className="p-3">{formatJsonValue(value)}</div>
              </div>
            ))}
          </div>
        ) : (
          <div className="text-sm text-slate-500 italic">No details available</div>
        )}
      </div>
    </div>
  );
}

// ── Sequence diagram ──────────────────────────────────────────────────────────

/** One LLM round, assembled from llm_rounds[] + tool pairs. */
type SeqRound = {
  index: number;
  model: string;
  responseId?: string;
  /** true for round 2+ — stateful chain via previous_response_id */
  stateful: boolean;
  tokens: { input?: number; output?: number; reasoning?: number; cost?: number };
  durationMs?: number;
  tools: Array<{ name: string; args?: string; ok: boolean; summary?: string; durationMs?: number }>;
  thinking?: string;
};

function fmtTok(n: number | undefined): string {
  if (n == null) return "—";
  return n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n);
}

function fmtDuration(ms: number | undefined): string {
  if (ms == null || ms < 0) return "";
  if (ms < 1000) return `${ms}ms`;
  return `${(ms / 1000).toFixed(1)}s`;
}

function buildSeqRounds(turn: Turn): SeqRound[] {
  const usage = turn.user.llmUsage ?? turn.assistant?.llmUsage;
  const rawRounds = (Array.isArray(usage?.llm_rounds) ? usage!.llm_rounds : []) as LlmRound[];

  // Parse tool pairs from the turn.
  const pairs: Array<{ name: string; args?: string; ok: boolean; summary?: string; durationMs?: number }> = [];
  {
    let pending: { name: string; args?: string } | null = null;
    for (const te of turn.tools) {
      const p = parseToolEntry(te.text);
      if (!p.isResult) {
        pending = { name: p.toolName, args: p.status || undefined };
      } else {
        pairs.push({
          name: p.toolName,
          args: pending?.args,
          ok: p.ok,
          summary: p.status || undefined,
          durationMs: te.durationMs,
        });
        pending = null;
      }
    }
    if (pending) pairs.push({ name: pending.name, args: pending.args, ok: true });
  }

  if (rawRounds.length === 0 && pairs.length === 0) return [];

  if (rawRounds.length === 0) {
    return [{ index: 0, model: "", stateful: false, tokens: {}, tools: pairs }];
  }

  // Map: round i requests tool[i], round i+1 sees the result and requests tool[i+1], etc.
  return rawRounds.map((r, i) => {
    const u = r.usage ?? ({} as NonNullable<LlmRound["usage"]>);
    const anyU = u as Record<string, unknown>;
    const anyR = r as Record<string, unknown>;
    return {
      index: i,
      model: r.model ?? "",
      responseId: r.id,
      stateful: i > 0,
      durationMs: typeof anyR.duration_ms === "number" ? anyR.duration_ms : undefined,
      tokens: {
        input:     (anyU.input_tokens  as number | undefined) ?? (anyU.prompt_tokens as number | undefined),
        output:    (anyU.output_tokens as number | undefined) ?? u.completion_tokens,
        reasoning: (anyU.reasoning_tokens as number | undefined)
                   ?? ((anyU.output_tokens_details as Record<string, unknown> | undefined)?.reasoning_tokens as number | undefined),
        cost: u.cost,
      },
      tools: i < pairs.length ? [pairs[i]] : [],
    };
  });
}

// ── Sequence row components ───────────────────────────────────────────────────

function SeqUserRow({ text }: { text: string }) {
  return (
    <div className="flex items-start gap-3 py-2">
      <div className="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-slate-200 dark:bg-slate-700">
        <User className="h-3.5 w-3.5 text-slate-600 dark:text-slate-300" />
      </div>
      <div className="min-w-0 flex-1 rounded border border-slate-200 bg-slate-50 px-3 py-2 text-sm dark:border-slate-700 dark:bg-slate-800/60">
        <span className="text-slate-700 dark:text-slate-200">{text}</span>
      </div>
    </div>
  );
}

function SeqRoundHeader({ round, prevInput }: { round: SeqRound; prevInput?: number }) {
  const [expanded, setExpanded] = useState(false);
  const inputSaved = prevInput != null && round.tokens.input != null && prevInput > round.tokens.input
    ? Math.round((1 - round.tokens.input / prevInput) * 100) : null;
  const shortModel = round.model.split("/").pop() ?? round.model;
  const dur = fmtDuration(round.durationMs);

  return (
    <div className={`my-2 rounded border-l-4 ${
      round.stateful
        ? "border-blue-400 bg-blue-50/60 dark:border-blue-500 dark:bg-blue-950/40"
        : "border-slate-400 bg-slate-50/80 dark:border-slate-500 dark:bg-slate-800/60"
    }`}>
      <div className="flex items-center gap-2 px-3 py-1.5">
        {/* Round badge */}
        <span className="rounded bg-slate-200 px-1.5 py-0.5 text-[10px] font-bold text-slate-600 dark:bg-slate-700 dark:text-slate-300">
          R{round.index + 1}
        </span>

        {/* Stateful/stateless */}
        {round.stateful ? (
          <span className="flex items-center gap-1 rounded bg-blue-100 px-1.5 py-0.5 text-[10px] font-semibold text-blue-700 dark:bg-blue-900/60 dark:text-blue-300">
            <Link className="h-2.5 w-2.5" /> stateful
          </span>
        ) : (
          <span className="rounded bg-slate-100 px-1.5 py-0.5 text-[10px] text-slate-500 dark:bg-slate-700/60 dark:text-slate-400">
            stateless
          </span>
        )}

        {/* Model */}
        {shortModel && (
          <span className="truncate text-[11px] text-slate-500 dark:text-slate-400">{shortModel}</span>
        )}

        <div className="ml-auto flex items-center gap-3">
          {/* Duration + token counts */}
          <span className="font-mono text-[10px] text-slate-500 dark:text-slate-400">
            {dur && (
              <span className="mr-2 font-semibold text-slate-600 dark:text-slate-300">{dur}</span>
            )}
            in {fmtTok(round.tokens.input)}
            {inputSaved != null && (
              <span className="ml-1 text-emerald-600 dark:text-emerald-400">↓{inputSaved}%</span>
            )}
            {" · "}out {fmtTok(round.tokens.output)}
            {round.tokens.reasoning != null && round.tokens.reasoning > 0 && (
              <span className="ml-1 text-violet-600 dark:text-violet-400">
                · <Brain className="inline h-2.5 w-2.5" /> {fmtTok(round.tokens.reasoning)}
              </span>
            )}
            {round.tokens.cost != null && (
              <span className="ml-1 text-amber-600 dark:text-amber-400">
                · ${round.tokens.cost.toFixed(4)}
              </span>
            )}
          </span>

          {/* Expand for response_id */}
          {round.responseId && (
            <button
              type="button"
              onClick={() => setExpanded(v => !v)}
              className="text-slate-400 hover:text-slate-600 dark:hover:text-slate-200"
            >
              {expanded ? <ChevronDown className="h-3 w-3" /> : <ChevronRight className="h-3 w-3" />}
            </button>
          )}
        </div>
      </div>

      {/* Expanded: response_id */}
      {expanded && round.responseId && (
        <div className="border-t border-slate-200 px-3 py-1.5 dark:border-slate-700">
          <span className="font-mono text-[10px] text-slate-400 dark:text-slate-500">
            resp_id: {round.responseId}
          </span>
        </div>
      )}

      {/* Thinking text (when available) */}
      {round.thinking && (
        <div className="border-t border-violet-200/60 px-3 py-1.5 dark:border-violet-900/40">
          <div className="flex items-start gap-1.5">
            <Brain className="mt-0.5 h-3 w-3 shrink-0 text-violet-500" />
            <span className="text-[11px] italic text-violet-700 dark:text-violet-300 line-clamp-3">
              {round.thinking}
            </span>
          </div>
        </div>
      )}
    </div>
  );
}

function SeqToolRow({ tool, roundIndex }: {
  tool: SeqRound["tools"][number];
  roundIndex: number;
}) {
  const [open, setOpen] = useState(false);
  const isMcp = tool.name.startsWith("mcp_");
  const dur = fmtDuration(tool.durationMs);
  return (
    <div className="ml-8 mb-1">
      {/* Call */}
      <div className="flex items-center gap-2 rounded px-2 py-1 hover:bg-slate-100/60 dark:hover:bg-slate-800/40">
        <Zap className="h-3 w-3 shrink-0 text-amber-500" />
        <span className={`text-[11px] font-medium ${isMcp ? "text-indigo-600 dark:text-indigo-400" : "text-amber-700 dark:text-amber-300"}`}>
          {tool.name}
        </span>
        {tool.args && (
          <button type="button" onClick={() => setOpen(v => !v)} className="text-slate-400 hover:text-slate-600">
            {open ? <ChevronDown className="h-3 w-3" /> : <ChevronRight className="h-3 w-3" />}
          </button>
        )}
        <div className="ml-auto flex items-center gap-2">
          {dur && (
            <span className="font-mono text-[10px] font-semibold text-slate-500 dark:text-slate-400">{dur}</span>
          )}
          {tool.ok
            ? <CheckCircle className="h-3.5 w-3.5 text-green-500" />
            : <XCircle className="h-3.5 w-3.5 text-red-500" />}
          {tool.summary && (
            <span className="max-w-[160px] truncate text-[10px] text-slate-500 dark:text-slate-400">
              {tool.summary}
            </span>
          )}
        </div>
      </div>
      {/* Args expanded */}
      {open && tool.args && (
        <div className="ml-5 my-0.5 rounded border border-slate-200 bg-slate-50 px-2 py-1 dark:border-slate-700 dark:bg-slate-800/60">
          <pre className="whitespace-pre-wrap break-all text-[10px] text-slate-600 dark:text-slate-300">
            {tool.args}
          </pre>
        </div>
      )}
    </div>
  );
}

function SeqAssistantRow({ text, error }: { text?: string; error?: string }) {
  if (error) {
    return (
      <div className="flex items-start gap-3 py-2">
        <div className="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-red-100 dark:bg-red-900/40">
          <XCircle className="h-3.5 w-3.5 text-red-500" />
        </div>
        <div className="min-w-0 flex-1 rounded border border-red-200 bg-red-50 px-3 py-2 text-sm dark:border-red-900/60 dark:bg-red-950/30">
          <span className="font-medium text-red-700 dark:text-red-300">Error: </span>
          <span className="text-red-600 dark:text-red-400">{error}</span>
        </div>
      </div>
    );
  }
  if (!text) return null;
  const html = renderAssistantMarkdownToHtml(text);
  return (
    <div className="flex items-start gap-3 py-2">
      <div className="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-purple-100 dark:bg-purple-900/40">
        <MessageSquare className="h-3.5 w-3.5 text-purple-600 dark:text-purple-400" />
      </div>
      <div className="pm-body pm-md min-w-0 flex-1 rounded border border-purple-200 bg-purple-50 px-3 py-2 dark:border-purple-900/40 dark:bg-purple-950/20"
        dangerouslySetInnerHTML={{ __html: html }}
      />
    </div>
  );
}

function SeqConnector() {
  return <div className="ml-3 my-0.5 h-3 w-px border-l-2 border-dashed border-slate-300 dark:border-slate-600" />;
}

// ── Turn sequence renderer ────────────────────────────────────────────────────

function TurnSequence({ turn, turnIndex }: { turn: Turn; turnIndex: number }) {
  const rounds = buildSeqRounds(turn);

  return (
    <div className={turnIndex > 0 ? "mt-6 border-t border-slate-200 pt-4 dark:border-slate-700" : ""}>
      {turnIndex > 0 && (
        <div className="mb-2 text-[10px] font-bold uppercase tracking-widest text-slate-400">
          Turn {turnIndex + 1}
        </div>
      )}

      {/* User prompt */}
      <SeqUserRow text={turn.user.text} />
      <SeqConnector />

      {rounds.length === 0 ? (
        /* No round data — show a placeholder LLM block */
        <div className="my-2 rounded border border-dashed border-slate-300 px-3 py-2 text-[11px] text-slate-400 dark:border-slate-700">
          LLM (no telemetry)
        </div>
      ) : (
        rounds.map((round, ri) => (
          <React.Fragment key={ri}>
            <SeqRoundHeader
              round={round}
              prevInput={ri > 0 ? rounds[ri - 1].tokens.input : undefined}
            />
            {round.tools.map((tool, ti) => (
              <SeqToolRow key={ti} tool={tool} roundIndex={ri} />
            ))}
            {ri < rounds.length - 1 && <SeqConnector />}
          </React.Fragment>
        ))
      )}

      <SeqConnector />

      {/* Final response or error */}
      {turn.assistant ? (
        <SeqAssistantRow text={turn.assistant.text} />
      ) : (
        <SeqAssistantRow error="No response (timeout or error)" />
      )}
    </div>
  );
}

// ── Sequence view summary footer ──────────────────────────────────────────────

function SeqSummaryFooter({ entries }: { entries: ChatEntry[] }) {
  const turns = groupIntoTurns(entries);
  let totalIn = 0, totalOut = 0, totalReasoning = 0, totalCost = 0;
  let statefulRounds = 0, totalRounds = 0;
  let totalLlmMs = 0, totalToolMs = 0;
  for (const t of turns) {
    const usage = t.user.llmUsage ?? t.assistant?.llmUsage;
    const rounds = (Array.isArray(usage?.llm_rounds) ? usage!.llm_rounds : []) as LlmRound[];
    totalRounds += rounds.length;
    for (let i = 0; i < rounds.length; i++) {
      if (i > 0) statefulRounds++;
      const anyR = rounds[i] as Record<string, unknown>;
      if (typeof anyR.duration_ms === "number") totalLlmMs += anyR.duration_ms;
      const u = rounds[i].usage ?? ({} as Record<string, unknown>);
      const anyU = u as Record<string, unknown>;
      totalIn += (anyU.input_tokens as number ?? anyU.prompt_tokens as number ?? 0);
      totalOut += (anyU.output_tokens as number ?? anyU.completion_tokens as number ?? 0);
      totalReasoning += (anyU.reasoning_tokens as number ?? 0);
      totalCost += (u.cost as number ?? 0);
    }
    for (const te of t.tools) {
      if (te.durationMs != null && te.durationMs >= 0) totalToolMs += te.durationMs;
    }
  }
  if (totalRounds === 0) return null;
  return (
    <div className="shrink-0 border-t border-slate-200 bg-slate-50/80 px-4 py-2 dark:border-slate-700 dark:bg-slate-800/60">
      <div className="flex flex-wrap gap-x-4 gap-y-1 text-[10px] text-slate-500 dark:text-slate-400">
        <span><span className="font-semibold">{totalRounds}</span> rounds</span>
        <span><Link className="inline h-2.5 w-2.5 text-blue-400" /> <span className="font-semibold">{statefulRounds}</span> stateful</span>
        {totalLlmMs > 0 && (
          <span>LLM <span className="font-semibold">{fmtDuration(totalLlmMs)}</span></span>
        )}
        {totalToolMs > 0 && (
          <span>tools <span className="font-semibold">{fmtDuration(totalToolMs)}</span></span>
        )}
        <span>in <span className="font-semibold">{fmtTok(totalIn)}</span> tok</span>
        <span>out <span className="font-semibold">{fmtTok(totalOut)}</span> tok</span>
        {totalReasoning > 0 && (
          <span><Brain className="inline h-2.5 w-2.5 text-violet-400" /> <span className="font-semibold">{fmtTok(totalReasoning)}</span> reasoning</span>
        )}
        {totalCost > 0 && (
          <span>cost <span className="font-semibold">${totalCost.toFixed(4)}</span></span>
        )}
      </div>
    </div>
  );
}

function SequenceView({ entries }: { entries: ChatEntry[] }) {
  const turns = groupIntoTurns(entries);
  if (turns.length === 0) return (
    <div className="flex h-full items-center justify-center text-sm text-slate-500">
      No turns yet.
    </div>
  );
  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
        {turns.map((turn, ti) => (
          <TurnSequence key={ti} turn={turn} turnIndex={ti} />
        ))}
      </div>
      <SeqSummaryFooter entries={entries} />
    </div>
  );
}

// ── Main component ────────────────────────────────────────────────────────────

export function AgentFlowPane() {
  const entries = usePmChatStore((s) => s.entries);
  const hasEntries = entries.length > 0;
  const hasUserEntries = entries.some((e) => e.role === "user");
  const isDark = useIsDark();
  const [viewMode, setViewMode] = useState<"flow" | "sequence">("sequence");

  const [nodes, setNodes, onNodesChange] = useNodesState<Node<NodeData>>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Edge>([]);
  const [selectedNode, setSelectedNode] = useState<Node<NodeData> | null>(null);
  const [parseErr, setParseErr] = useState<string | null>(null);
  const isSequenceMode = viewMode === "sequence";

  useEffect(() => {
    if (isSequenceMode) return; // sequence view rebuilds on render; no node state needed
    if (!hasUserEntries) {
      setNodes([]);
      setEdges([]);
      setParseErr(null);
      return;
    }
    try {
      const { nodes: n, edges: e } = buildFlowFromEntries(entries);
      console.log("[pm-chat:agentFlow] built from entries", { turns: groupIntoTurns(entries).length, nodes: n.length, edges: e.length });
      setNodes(n);
      setEdges(e);
      setParseErr(null);
    } catch (err) {
      console.error("[pm-chat:agentFlow] build error", err);
      setParseErr(err instanceof Error ? err.message : "Flow build error");
    }
  }, [entries, hasUserEntries, isSequenceMode, setNodes, setEdges]);

  const onNodeClick: NodeMouseHandler<Node<NodeData>> = (_, node) => setSelectedNode(node);

  const emptyMessage = !hasEntries
    ? "No agent log yet — run a prompt to see the flow."
    : !hasUserEntries
      ? "No agent log for this session."
      : null;

  return (
    <div className="flex h-full min-h-0 w-full flex-col overflow-hidden">
      {/* Tab bar */}
      <div className="flex shrink-0 items-center gap-1 border-b border-slate-200 px-3 py-1.5 dark:border-slate-700">
        {(["sequence", "flow"] as const).map((mode) => (
          <button
            key={mode}
            type="button"
            onClick={() => setViewMode(mode)}
            className={`rounded px-3 py-1 text-xs font-medium transition-colors ${
              viewMode === mode
                ? "bg-slate-200 text-slate-900 dark:bg-slate-700 dark:text-slate-100"
                : "text-slate-500 hover:bg-slate-100 hover:text-slate-700 dark:text-slate-400 dark:hover:bg-slate-800 dark:hover:text-slate-200"
            }`}
          >
            {mode === "sequence" ? "Sequence" : "Flow"}
          </button>
        ))}
        <span className="ml-2 text-[10px] text-slate-400 dark:text-slate-500">
          {viewMode === "sequence" ? "/responses · LLM rounds" : "Node graph"}
        </span>
      </div>

      {emptyMessage ? (
        <div className="flex flex-1 items-center justify-center text-sm text-slate-500 dark:text-slate-400">
          {emptyMessage}
        </div>
      ) : viewMode === "sequence" ? (
        <SequenceView entries={entries} />
      ) : (
        <>
          {parseErr ? (
            <div className="flex flex-1 items-center justify-center text-sm text-red-500">{parseErr}</div>
          ) : (
            <div className="relative min-h-0 flex-1">
              <ReactFlow
                nodes={nodes}
                edges={edges}
                onNodesChange={onNodesChange}
                onEdgesChange={onEdgesChange}
                onNodeClick={onNodeClick}
                nodeTypes={nodeTypes}
                colorMode={isDark ? "dark" : "light"}
                fitView
                fitViewOptions={{ padding: 0.1, minZoom: 0.1 }}
                minZoom={0.05}
                maxZoom={2}
              >
                <Background gap={12} size={1} />
                <Controls />
                <MiniMap
                  style={{
                    background: isDark ? "#2d2d2d" : "#f1f5f9",
                    border: `1px solid ${isDark ? "#444" : "#cbd5e1"}`,
                  }}
                  maskColor={isDark ? "rgba(0,0,0,0.5)" : "rgba(240,244,248,0.6)"}
                />
              </ReactFlow>
              <DetailsPanel node={selectedNode} onClose={() => setSelectedNode(null)} />
            </div>
          )}
        </>
      )}
    </div>
  );
}
