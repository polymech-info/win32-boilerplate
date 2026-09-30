import { z } from "zod";
import type { CustomCommandItem } from "@pm/shared/customCommands/types";

export type ElseIfBlockNode = {
  condition: string;
  consequent: BlockNode[];
};

export type CaseBlockNode = {
  kind: "case";
  comparator: string;
  expression: string;
  consequent: BlockNode[];
};

export type SwitchDefaultNode = {
  kind: "switchDefault";
  consequent: BlockNode[];
};

export type SwitchItemNode = CaseBlockNode | SwitchDefaultNode;

export type BlockCommonProps = {
  enabled?: boolean;
  storeAs?: string;
  continueOnError?: boolean;
  background?: boolean;
};

export type BlockNode = BlockCommonProps &
  (
  | {
      kind: "if";
      condition: string;
      consequent: BlockNode[];
      elseIfBlocks?: ElseIfBlockNode[];
      alternate?: BlockNode[];
    }
  | {
      kind: "runScript";
      method: string;
      id?: string;
    }
  | {
      kind: "command";
      command: CustomCommandItem;
      id?: string;
    }
  | {
      kind: "setVariable";
      name: string;
      value?: unknown;
      expression?: string;
    }
  | {
      kind: "getVariable";
      name: string;
      target?: string;
    }
  | {
      kind: "log";
      level?: "trace" | "debug" | "info" | "warn" | "error" | "critical" | "";
      message: string;
    }
  | {
      kind: "for";
      initial: string;
      final: string;
      comparator: string;
      modifier: string;
      items: BlockNode[];
      ignoreErrors?: boolean;
    }
  | {
      kind: "while";
      condition: string;
      items: BlockNode[];
      loopLimit?: number;
    }
  | {
      kind: "switch";
      variable: string;
      items: SwitchItemNode[];
    }
  | {
      kind: "break";
    }
  | {
      kind: "wait";
      ms: number;
    }
  | {
      kind: "fetch" | "network" | "httpRequest";
      id?: string;
      url: string;
      method?: string;
      decode?: "raw" | "json";
      parse?: string;
      filter?: string;
      query?: string;
      timeoutMs?: number;
      connectTimeoutMs?: number;
      followRedirects?: boolean;
      maxRedirects?: number;
      retries?: number;
      retryDelayMs?: number;
      headers?: Record<string, string>;
      body?: unknown;
      storeAs?: string;
    }
  | {
      kind: "Parse" | "parse";
      id?: string;
      parser?: string;
      filter?: string;
      query?: string;
      input?: string;
      from?: string;
      target?: string;
      storeAs?: string;
    }
  | {
      kind: "shell" | "Shell";
      id?: string;
      mode?: string;
      shell?: string;
      command?: string;
      line?: string;
      exe?: string;
      args?: string[];
      cwd?: string;
      timeoutMs?: number;
      background?: boolean;
      log?: boolean;
      stdout?: "trace" | "debug" | "info" | "warn" | "error" | "off" | "";
      stderr?: "trace" | "debug" | "info" | "warn" | "error" | "off" | "";
      storeAs?: string;
    }
  );

const blockCommonShape = {
  enabled: z.boolean().optional(),
  storeAs: z.string().optional(),
  continueOnError: z.boolean().optional(),
  background: z.boolean().optional(),
};

export const blockNodeSchema: z.ZodType<BlockNode> = z.lazy(() =>
  z.discriminatedUnion("kind", [
    z.object({
      kind: z.literal("if"),
      condition: z.string(),
      consequent: z.array(blockNodeSchema),
      elseIfBlocks: z
        .array(
          z.object({
            condition: z.string(),
            consequent: z.array(blockNodeSchema),
          }),
        )
        .optional(),
      alternate: z.array(blockNodeSchema).optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("runScript"),
      method: z.string(),
      id: z.string().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("command"),
      command: z.record(z.string(), z.unknown()) as z.ZodType<CustomCommandItem>,
      id: z.string().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("setVariable"),
      name: z.string(),
      value: z.unknown().optional(),
      expression: z.string().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("getVariable"),
      name: z.string(),
      target: z.string().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("log"),
      level: z.enum(["trace", "debug", "info", "warn", "error", "critical", ""]).optional(),
      message: z.string(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("for"),
      initial: z.string(),
      final: z.string(),
      comparator: z.string(),
      modifier: z.string(),
      items: z.array(blockNodeSchema),
      ignoreErrors: z.boolean().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("while"),
      condition: z.string(),
      items: z.array(blockNodeSchema),
      loopLimit: z.number().int().positive().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("switch"),
      variable: z.string(),
      items: z.array(
        z.discriminatedUnion("kind", [
          z.object({
            kind: z.literal("case"),
            comparator: z.string(),
            expression: z.string(),
            consequent: z.array(blockNodeSchema),
          }),
          z.object({
            kind: z.literal("switchDefault"),
            consequent: z.array(blockNodeSchema),
          }),
        ]),
      ),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("break"),
    }).extend(blockCommonShape),
    z.object({
      kind: z.literal("wait"),
      ms: z.number().nonnegative().finite(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.enum(["fetch", "network", "httpRequest"]),
      id: z.string().optional(),
      url: z.string(),
      method: z.string().optional(),
      decode: z.enum(["raw", "json"]).optional(),
      parse: z.string().optional(),
      filter: z.string().optional(),
      query: z.string().optional(),
      timeoutMs: z.number().int().positive().optional(),
      connectTimeoutMs: z.number().int().positive().optional(),
      followRedirects: z.boolean().optional(),
      maxRedirects: z.number().int().nonnegative().optional(),
      retries: z.number().int().nonnegative().optional(),
      retryDelayMs: z.number().int().nonnegative().optional(),
      headers: z.record(z.string(), z.string()).optional(),
      body: z.unknown().optional(),
      storeAs: z.string().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.enum(["Parse", "parse"]),
      id: z.string().optional(),
      parser: z.string().optional(),
      filter: z.string().optional(),
      query: z.string().optional(),
      input: z.string().optional(),
      from: z.string().optional(),
      target: z.string().optional(),
      storeAs: z.string().optional(),
    }).extend(blockCommonShape),
    z.object({
      kind: z.enum(["shell", "Shell"]),
      id: z.string().optional(),
      mode: z.string().optional(),
      shell: z.string().optional(),
      command: z.string().optional(),
      line: z.string().optional(),
      exe: z.string().optional(),
      args: z.array(z.string()).optional(),
      cwd: z.string().optional(),
      timeoutMs: z.number().int().positive().optional(),
      background: z.boolean().optional(),
      log: z.boolean().optional(),
      stdout: z.enum(["trace", "debug", "info", "warn", "error", "off", ""]).optional(),
      stderr: z.enum(["trace", "debug", "info", "warn", "error", "off", ""]).optional(),
      storeAs: z.string().optional(),
    }).extend(blockCommonShape),
  ]),
);

export const blocksFileSchema = z.object({
  version: z.literal(1),
  context: z.record(z.string(), z.unknown()).optional().default({}),
  roots: z.array(blockNodeSchema).min(1),
});

export type BlocksFile = z.infer<typeof blocksFileSchema>;
