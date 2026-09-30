import type { BlockNode, SwitchItemNode } from "@/schema/blocks-file";

import { DEFAULT_BLOCK_RUN_DELAY_MS, MIN_WAIT_BLOCK_DELAY_MS } from "./constants";

export type ExecutableBlock = BlockNode | SwitchItemNode;

export type ExecutionStep = {
  path: string;
  block: ExecutableBlock;
};

type RuntimeContext = Record<string, unknown>;

const demoContext: RuntimeContext = {
  selection: { files: ["demo/input.png"] },
  selectionCount: 1,
  mode: "capture",
  hasQueuedWork: true,
};

function contextValue(context: RuntimeContext, name: string): string | number | boolean | undefined {
  const parts = name.replace(/^this\./, "").split(".").filter(Boolean);
  let current: unknown = context;
  for (const part of parts) {
    if (!current || typeof current !== "object" || !(part in current)) return undefined;
    current = (current as Record<string, unknown>)[part];
  }
  if (typeof current === "string" || typeof current === "number" || typeof current === "boolean") return current;
  return undefined;
}

function setContextValue(context: RuntimeContext, name: string, value: unknown) {
  const parts = name.split(".").filter(Boolean);
  if (!parts.length) return;
  let current: Record<string, unknown> = context;
  for (let i = 0; i + 1 < parts.length; i += 1) {
    const part = parts[i];
    if (!current[part] || typeof current[part] !== "object") current[part] = {};
    current = current[part] as Record<string, unknown>;
  }
  current[parts[parts.length - 1]] = value;
}

function evaluateExpression(expression: string | undefined, context: RuntimeContext): number | undefined {
  if (!expression) return undefined;
  const normalized = expression.replace(/\b[A-Za-z_][\w.]*/g, (name) => {
    const value = contextValue(context, name);
    return typeof value === "number" || typeof value === "boolean" ? String(Number(value)) : name;
  });
  try {
    if (!/^[\d\s.+\-*/%()<>!=&|]+$/.test(normalized)) return undefined;
    return Function(`"use strict"; return Number(${normalized});`)() as number;
  } catch {
    return undefined;
  }
}

function conditionIsTrue(condition: string, context: RuntimeContext) {
  const trimmed = condition.trim();
  const evaluated = evaluateExpression(trimmed, context);
  if (evaluated != null && Number.isFinite(evaluated)) return Math.abs(evaluated) > 1e-12;
  const comparison = trimmed.match(/^([A-Za-z_][\w.]*)\s*(==={0,1}|!==|!=|>=|<=|>|<)\s*(['"]?)(.*?)\3$/);
  if (comparison) {
    const lhs = contextValue(context, comparison[1]);
    const rhsRaw = comparison[4];
    const rhs = Number.isFinite(Number(rhsRaw)) ? Number(rhsRaw) : rhsRaw;
    if (comparison[2] === ">" || comparison[2] === ">=" || comparison[2] === "<" || comparison[2] === "<=") {
      const leftNumber = Number(lhs);
      const rightNumber = Number(rhs);
      if (comparison[2] === ">") return leftNumber > rightNumber;
      if (comparison[2] === ">=") return leftNumber >= rightNumber;
      if (comparison[2] === "<") return leftNumber < rightNumber;
      return leftNumber <= rightNumber;
    }
    return comparison[2].includes("!") ? String(lhs) !== String(rhs) : String(lhs) === String(rhs);
  }
  if (trimmed === "false" || trimmed === "0") return false;
  return Boolean(contextValue(context, trimmed) ?? trimmed);
}

function blockChildrenForExecution(block: ExecutableBlock, path: string, context: RuntimeContext): ExecutionStep[] {
  if (block.kind === "if") {
    const matched = conditionIsTrue(block.condition, context);
    const branchKey = matched ? "consequent" : "alternate";
    const branch = matched ? block.consequent : (block.alternate ?? []);
    return branch.flatMap((child, index) => flattenExecutable(child, `${path}/${branchKey}/${index}`, context));
  }

  if (block.kind === "for") {
    return block.items.flatMap((child, index) => flattenExecutable(child, `${path}/items/${index}`, context));
  }

  if (block.kind === "while") {
    if (!conditionIsTrue(block.condition, context)) return [];
    return block.items.flatMap((child, index) => flattenExecutable(child, `${path}/items/${index}`, context));
  }

  if (block.kind === "switch") {
    const currentValue = String(contextValue(context, block.variable) ?? block.variable);
    const matchingIndex = block.items.findIndex((item) => {
      if (item.kind === "switchDefault") return false;
      return item.expression.replace(/^['"]|['"]$/g, "") === currentValue;
    });
    const fallbackIndex = block.items.findIndex((item) => item.kind === "switchDefault");
    const itemIndex = matchingIndex >= 0 ? matchingIndex : fallbackIndex;
    if (itemIndex < 0) return [];
    return flattenExecutable(block.items[itemIndex], `${path}/items/${itemIndex}`, context);
  }

  if (block.kind === "case" || block.kind === "switchDefault") {
    return block.consequent.flatMap((child, index) => flattenExecutable(child, `${path}/consequent/${index}`, context));
  }

  return [];
}

function flattenExecutable(block: ExecutableBlock, path: string, context: RuntimeContext): ExecutionStep[] {
  const step = { path, block };
  if (block.kind === "setVariable") {
    const evaluated = evaluateExpression(block.expression, context);
    setContextValue(context, block.name, evaluated ?? block.value ?? null);
  } else if (block.kind === "getVariable" && block.target) {
    setContextValue(context, block.target, contextValue(context, block.name) ?? null);
  }
  return [step, ...blockChildrenForExecution(block, path, context)];
}

export function buildExecutionPlan(blocks: BlockNode[], context: RuntimeContext = demoContext) {
  const mutableContext: RuntimeContext = JSON.parse(JSON.stringify(context));
  return blocks.flatMap((block, index) => flattenExecutable(block, String(index), mutableContext));
}

export function blockRunDelay(block: ExecutableBlock) {
  return block.kind === "wait" ? Math.max(MIN_WAIT_BLOCK_DELAY_MS, block.ms) : DEFAULT_BLOCK_RUN_DELAY_MS;
}

export function blockShouldFail(block: ExecutableBlock) {
  return block.kind === "runScript" && /\bthrow\b|failed/i.test(block.method);
}

export function delay(ms: number) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
