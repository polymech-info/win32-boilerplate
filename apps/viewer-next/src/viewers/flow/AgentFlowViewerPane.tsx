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
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { fetchHostedText } from "@/bridge/hostedFileFetch";
import { Play, Square, Bot, Wrench, MessageSquare, CheckCircle, XCircle, Loader2, X } from "lucide-react";

interface AgentEvent {
  kind: string;
  detail?: {
    id?: string;
    tool?: string;
    arguments?: Record<string, unknown>;
    envelope?: {
      ok: boolean;
      results?: unknown[];
      summary?: { succeeded: number; failed: number; total: number };
      error?: string;
    };
    folder?: string;
    selection?: string[];
  };
  text?: string;
  tool?: string;
}

interface AgentJson {
  cli?: { prompt?: string; paths?: string[] };
  command?: string;
  events?: AgentEvent[];
  generated_at?: string;
  provider?: { model?: string; base_url?: string };
  result?: {
    final_text?: string;
    iterations?: number;
    llm_usage?: {
      prompt_tokens?: number;
      completion_tokens?: number;
      total_tokens?: number;
      cost?: number;
      llm_rounds?: Array<{
        model?: string;
        id?: string;
        usage?: {
          prompt_tokens?: number;
          completion_tokens?: number;
          cost?: number;
        };
      }>;
    };
  };
  turn?: { user_prompt?: string; selection?: string[] };
}

type NodeData = {
  label: string;
  subtitle?: string;
  details?: Record<string, string>;
};

// Simple node components - no expand, just display
function StartNode({ data, selected }: { data: NodeData; selected?: boolean }) {
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected 
        ? "border-emerald-600 bg-emerald-200 dark:border-emerald-400 dark:bg-emerald-900 ring-2 ring-emerald-500" 
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
      selected 
        ? "border-blue-600 bg-blue-200 dark:border-blue-400 dark:bg-blue-900 ring-2 ring-blue-500" 
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
      selected 
        ? "border-amber-600 bg-amber-200 dark:border-amber-400 dark:bg-amber-900 ring-2 ring-amber-500" 
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

function ToolResultNode({ data, selected }: { data: NodeData & { ok?: boolean }; selected?: boolean }) {
  const isOk = data.ok;
  return (
    <div className={`rounded-lg border-2 px-3 py-2 shadow-sm transition-all ${
      selected 
        ? isOk 
          ? "border-green-600 bg-green-200 dark:border-green-400 dark:bg-green-900 ring-2 ring-green-500" 
          : "border-red-600 bg-red-200 dark:border-red-400 dark:bg-red-900 ring-2 ring-red-500"
        : isOk
          ? "border-green-500 bg-green-100 dark:border-green-500 dark:bg-green-950"
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
      selected 
        ? "border-purple-600 bg-purple-200 dark:border-purple-400 dark:bg-purple-900 ring-2 ring-purple-500" 
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
      selected 
        ? "border-gray-600 bg-gray-300 dark:border-gray-400 dark:bg-gray-800 ring-2 ring-gray-500" 
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

function formatArgs(args: Record<string, unknown>): string {
  try {
    return JSON.stringify(args, null, 2);
  } catch {
    return String(args);
  }
}

function transformAgentJsonToFlow(agentJson: AgentJson): { nodes: Node<NodeData>[]; edges: Edge[] } {
  const nodes: Node<NodeData>[] = [];
  const edges: Edge[] = [];
  const events = agentJson.events || [];

  // Vertical layout
  const nodeWidth = 260;
  const gapY = 24;
  const centerX = 180;
  let currentY = 20;
  let nodeId = 0;

  // Start node
  const userPrompt = agentJson.turn?.user_prompt || agentJson.cli?.prompt || "User Prompt";

  const startNode: Node<NodeData> = {
    id: `node-${nodeId}`,
    type: "start",
    position: { x: centerX, y: currentY },
    data: {
      label: "Start",
      subtitle: userPrompt.slice(0, 50),
      details: {
        Prompt: userPrompt,
        Files: (agentJson.turn?.selection || agentJson.cli?.paths || []).join(", ") || "none",
      },
    },
  };
  nodes.push(startNode);
  let lastNodeId = `node-${nodeId}`;
  nodeId++;
  currentY += 60 + gapY;

  const llmRounds = agentJson.result?.llm_usage?.llm_rounds || [];
  let llmRoundIndex = 0;
  let pendingLLM: string | null = null;
  let lastToolCallId: string | null = null;

  for (let i = 0; i < events.length; i++) {
    const event = events[i];

    switch (event.kind) {
      case "assistant_text": {
        const round = llmRounds[llmRoundIndex];
        const model = round?.model || "LLM";
        const usage = round?.usage;

        const details: Record<string, string> = {
          Model: model,
          "Round ID": round?.id || "?",
        };
        if (usage) {
          details["Prompt tokens"] = usage.prompt_tokens?.toLocaleString() ?? "?";
          details["Completion tokens"] = usage.completion_tokens?.toLocaleString() ?? "?";
          if (usage.cost) details["Cost"] = `$${usage.cost.toFixed(4)}`;
        }
        if (event.text) {
          details["Response preview"] = event.text.slice(0, 300);
        }

        const llmNode: Node<NodeData> = {
          id: `node-${nodeId}`,
          type: "llm",
          position: { x: centerX, y: currentY },
          data: {
            label: `LLM ${llmRoundIndex + 1}`,
            subtitle: `${model.split("/").pop()}${usage ? ` · ${usage.completion_tokens} tok` : ""}`,
            details,
          },
        };
        nodes.push(llmNode);

        edges.push({
          id: `edge-${lastNodeId}-${nodeId}`,
          source: lastNodeId,
          target: `node-${nodeId}`,
          type: "smoothstep",
          animated: true,
        });

        pendingLLM = `node-${nodeId}`;
        lastNodeId = `node-${nodeId}`;
        nodeId++;
        currentY += 60 + gapY;
        llmRoundIndex++;
        break;
      }

      case "tool_call": {
        const toolName = event.tool || event.detail?.tool || "unknown";
        const args = event.detail?.arguments;

        let shortSubtitle = toolName;
        if (args?.path && typeof args.path === "string") {
          const parts = (args.path as string).split(/[/\\]/);
          shortSubtitle += ` · ${parts[parts.length - 1]}`;
        }

        const details: Record<string, string> = {
          Tool: toolName,
          "Call ID": event.detail?.id || "?",
        };
        if (args) {
          details.Arguments = formatArgs(args);
        }

        const toolNode: Node<NodeData> = {
          id: `node-${nodeId}`,
          type: "toolCall",
          position: { x: centerX, y: currentY },
          data: {
            label: "Tool Call",
            subtitle: shortSubtitle.slice(0, 40),
            details,
          },
        };
        nodes.push(toolNode);

        const sourceId = pendingLLM || lastNodeId;
        edges.push({
          id: `edge-${sourceId}-${nodeId}`,
          source: sourceId,
          target: `node-${nodeId}`,
          type: "smoothstep",
          animated: true,
        });

        lastToolCallId = `node-${nodeId}`;
        lastNodeId = `node-${nodeId}`;
        pendingLLM = null;
        nodeId++;
        currentY += 60 + gapY;
        break;
      }

      case "tool_result": {
        const envelope = event.detail?.envelope;
        const ok = envelope?.ok ?? false;
        const summary = envelope?.summary;

        let shortSubtitle = ok ? "ok" : "failed";
        if (summary) {
          shortSubtitle = `${summary.succeeded}/${summary.total} ok`;
        } else if (envelope?.error) {
          shortSubtitle = envelope.error.slice(0, 40);
        }

        const details: Record<string, string> = {
          Status: ok ? "Success" : "Failed",
        };
        if (summary) {
          details.Succeeded = summary.succeeded.toString();
          details.Failed = summary.failed.toString();
          details.Total = summary.total.toString();
        }
        if (envelope?.error) {
          details.Error = envelope.error;
        }
        if (envelope?.results && Array.isArray(envelope.results)) {
          // Pretty print each result item individually for readability
          const formattedResults = envelope.results.map((result: unknown, idx: number) => {
            try {
              return `// Result ${idx + 1}\n${JSON.stringify(result, null, 2)}`;
            } catch {
              return `// Result ${idx + 1}\n${String(result)}`;
            }
          });
          details.Results = formattedResults.join("\n\n");
        }

        const resultNode: Node<NodeData & { ok?: boolean }> = {
          id: `node-${nodeId}`,
          type: "toolResult",
          position: { x: centerX, y: currentY },
          data: {
            label: ok ? "✓ Success" : "✗ Error",
            subtitle: shortSubtitle,
            ok,
            details,
          },
        };
        nodes.push(resultNode);

        const sourceId = lastToolCallId || lastNodeId;
        edges.push({
          id: `edge-${sourceId}-${nodeId}`,
          source: sourceId,
          target: `node-${nodeId}`,
          type: "smoothstep",
          style: { stroke: ok ? "#22c55e" : "#ef4444", strokeWidth: 2 },
        });

        lastNodeId = `node-${nodeId}`;
        lastToolCallId = null;
        nodeId++;
        currentY += 60 + gapY;
        break;
      }
    }
  }

  // Final response
  if (agentJson.result?.final_text) {
    const finalText = agentJson.result.final_text;

    const assistantNode: Node<NodeData> = {
      id: `node-${nodeId}`,
      type: "assistant",
      position: { x: centerX, y: currentY },
      data: {
        label: "Response",
        subtitle: `"${finalText.slice(0, 50)}${finalText.length > 50 ? "..." : ""}"`,
        details: {
          "Full response": finalText,
          Iterations: (agentJson.result.iterations ?? "?").toString(),
        },
      },
    };
    nodes.push(assistantNode);

    edges.push({
      id: `edge-${lastNodeId}-${nodeId}`,
      source: lastNodeId,
      target: `node-${nodeId}`,
      type: "smoothstep",
      animated: true,
    });

    lastNodeId = `node-${nodeId}`;
    nodeId++;
    currentY += 60 + gapY;
  }

  // End node
  const usage = agentJson.result?.llm_usage;
  const endDetails: Record<string, string> = {};
  if (usage) {
    endDetails.Rounds = (usage.iterations ?? "?").toString();
    endDetails["Total tokens"] = usage.total_tokens?.toLocaleString() ?? "?";
    if (usage.cost) endDetails["Total cost"] = `$${usage.cost.toFixed(4)}`;
  }

  const endNode: Node<NodeData> = {
    id: `node-${nodeId}`,
    type: "end",
    position: { x: centerX, y: currentY },
    data: {
      label: "End",
      subtitle: usage ? `${usage.iterations} rounds · $${usage.cost?.toFixed(3) || "?"}` : "",
      details: endDetails,
    },
  };
  nodes.push(endNode);

  edges.push({
    id: `edge-${lastNodeId}-${nodeId}`,
    source: lastNodeId,
    target: `node-${nodeId}`,
    type: "smoothstep",
  });

  return { nodes, edges };
}

// Format JSON value for display - pretty print with syntax highlighting
function formatJsonValue(value: string): React.ReactNode {
  // Handle multi-result format (with // comments as separators)
  if (value.includes("// Result")) {
    const parts = value.split("\n\n");
    return (
      <div className="space-y-4">
        {parts.map((part, idx) => {
          const commentMatch = part.match(/^\/\/ Result \d+/);
          const comment = commentMatch ? commentMatch[0] : null;
          const jsonPart = comment ? part.slice(comment.length + 1) : part;
          return (
            <div key={idx}>
              {comment && (
                <div className="mb-1 text-[10px] font-semibold uppercase tracking-wide text-slate-400">
                  {comment.replace("// ", "")}
                </div>
              )}
              <div className="rounded p-2">
                {formatSingleJsonValue(jsonPart.trim())}
              </div>
            </div>
          );
        })}
      </div>
    );
  }

  return formatSingleJsonValue(value);
}

function formatSingleJsonValue(value: string): React.ReactNode {
  // Check if value looks like JSON
  if (!value.trim().startsWith("{") && !value.trim().startsWith("[")) {
    return <span className="text-slate-700 dark:text-slate-300">{value}</span>;
  }

  try {
    const parsed = JSON.parse(value);
    const formatted = JSON.stringify(parsed, null, 2);

    // Simple syntax highlighting
    const lines = formatted.split("\n");
    return (
      <div className="font-mono text-xs leading-relaxed">
        {lines.map((line, i) => {
          // Highlight keys (property names)
          const keyMatch = line.match(/^(\s*)("[^"]+")(:)/);
          if (keyMatch) {
            const [, indent, key, colon] = keyMatch;
            const rest = line.slice(keyMatch[0].length);
            return (
              <div key={i} className="whitespace-pre">
                <span className="text-slate-400">{indent}</span>
                <span className="text-blue-600 dark:text-blue-400">{key}</span>
                <span className="text-slate-500">{colon}</span>
                <span className={rest.includes('"') ? "text-green-600 dark:text-green-400" : "text-amber-600 dark:text-amber-400"}>
                  {rest}
                </span>
              </div>
            );
          }
          // Highlight strings
          if (line.includes('"')) {
            return (
              <div key={i} className="whitespace-pre text-green-600 dark:text-green-400">
                {line}
              </div>
            );
          }
          // Numbers, booleans, null
          return (
            <div key={i} className="whitespace-pre text-amber-600 dark:text-amber-400">
              {line}
            </div>
          );
        })}
      </div>
    );
  } catch {
    return <span className="text-slate-700 dark:text-slate-300">{value}</span>;
  }
}

// Details panel component
function DetailsPanel({ node, onClose }: { node: Node<NodeData> | null; onClose: () => void }) {
  if (!node) return null;

  const details = node.data.details || {};
  const hasDetails = Object.keys(details).length > 0;

  return (
    <div className="absolute right-0 top-0 bottom-0 w-96 border-l border-slate-200 shadow-xl dark:border-slate-700 z-10 flex flex-col">
      <div className="flex items-center justify-between border-b border-slate-200 px-4 py-3 dark:border-slate-700">
        <div className="flex items-center gap-2">
          <span className="font-semibold text-slate-900 dark:text-slate-100">{node.data.label}</span>
        </div>
        <button
          type="button"
          onClick={onClose}
          className="rounded p-1 hover:bg-slate-200 dark:hover:bg-slate-700"
        >
          <X className="h-4 w-4" />
        </button>
      </div>

      <div className="flex-1 overflow-y-auto p-4">
        {node.data.subtitle && (
          <div className="mb-4 rounded p-2 text-sm text-slate-700 dark:text-slate-300">
            {node.data.subtitle}
          </div>
        )}

        {hasDetails ? (
          <div className="space-y-4">
            {Object.entries(details).map(([key, value]) => (
              <div key={key} className="rounded border border-slate-100 dark:border-slate-800">
                <div className="border-b border-slate-100 px-3 py-1.5 text-[10px] font-bold uppercase tracking-wide text-slate-500 dark:border-slate-800 dark:text-slate-400">
                  {key}
                </div>
                <div className="p-3">
                  {formatJsonValue(value)}
                </div>
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

export function AgentFlowViewerPane({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const err = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const url = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  // Support both hostedFileText (primary) and markdownText (fallback for viewer switching)
  const inlineText = typeof f.hostedFileText === "string" ? f.hostedFileText 
    : typeof f.markdownText === "string" ? f.markdownText 
    : undefined;
  const fileName = typeof f.hostedFileName === "string" ? f.hostedFileName : "agent.json";

  const [content, setContent] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [parseErr, setParseErr] = useState<string | null>(null);
  const [selectedNode, setSelectedNode] = useState<Node<NodeData> | null>(null);

  const [nodes, setNodes, onNodesChange] = useNodesState([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState([]);

  useEffect(() => {
    if (typeof inlineText === "string") {
      setLoading(false);
      setParseErr(null);
      setContent(inlineText);
      return;
    }
    if (!url) {
      setContent(null);
      setLoading(false);
      setParseErr(null);
      return;
    }
    const ac = new AbortController();
    (async () => {
      setLoading(true);
      setParseErr(null);
      setContent(null);
      try {
        const text = await fetchHostedText(url, ac.signal);
        setContent(text);
        setParseErr(null);
      } catch (e: unknown) {
        if (ac.signal.aborted) return;
        setParseErr(e instanceof Error ? e.message : "Failed to load");
      } finally {
        if (!ac.signal.aborted) setLoading(false);
      }
    })();
    return () => ac.abort();
  }, [url, inlineText]);

  useEffect(() => {
    if (!content) {
      setNodes([]);
      setEdges([]);
      return;
    }
    try {
      const parsed: AgentJson = JSON.parse(content);
      const { nodes: flowNodes, edges: flowEdges } = transformAgentJsonToFlow(parsed);
      setNodes(flowNodes);
      setEdges(flowEdges);
      setParseErr(null);
    } catch (e) {
      setNodes([]);
      setEdges([]);
      setParseErr(e instanceof Error ? `Invalid JSON: ${e.message}` : "Invalid JSON");
    }
  }, [content, setNodes, setEdges]);

  const onNodeClick: NodeMouseHandler<NodeData> = (_, node) => {
    setSelectedNode(node);
  };

  if (err) {
    return (
      <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center text-center text-slate-600 dark:text-slate-400">
        <p className="mb-2 text-sm">{err}</p>
      </div>
    );
  }

  const hasTextSource = url.length > 0 || typeof inlineText === "string";
  if (!hasTextSource) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center text-sm text-slate-500 dark:text-slate-400">
        No agent file loaded.
      </div>
    );
  }

  if (loading) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center gap-2 text-slate-500">
        <Loader2 className="h-5 w-5 animate-spin" />
        <span className="text-sm">Loading agent flow...</span>
      </div>
    );
  }

  if (parseErr) {
    return (
      <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center gap-2 text-center">
        <div className="text-sm text-red-500">{parseErr}</div>
        <div className="text-xs text-slate-500">File: {fileName}</div>
      </div>
    );
  }

  return (
    <div className="relative flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden">
      <div className="relative min-h-0 flex-1">
        <ReactFlow
          nodes={nodes}
          edges={edges}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
          onNodeClick={onNodeClick}
          nodeTypes={nodeTypes}
          fitView
          fitViewOptions={{ padding: 0.1, minZoom: 0.1 }}
          minZoom={0.05}
          maxZoom={2}
        >
          <Background gap={12} size={1} />
          <Controls />
          <MiniMap className="!bg-slate-100/50" />
        </ReactFlow>

        <DetailsPanel node={selectedNode} onClose={() => setSelectedNode(null)} />
      </div>
    </div>
  );
}
