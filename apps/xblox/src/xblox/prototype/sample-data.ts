import type { CustomCommandItem, CustomCommandsDocument } from "@pm/shared/customCommands/types";
import type { BlockNode } from "@/schema/blocks-file";

export const sampleCommandsDoc: CustomCommandsDocument = {
  version: 1,
  ribbon: {
    groups: [
      {
        label: "Pictures",
        items: [
          {
            type: "dropdown",
            id: "custom.capture",
            label: "Capture",
            tooltip: "Session and screenshot examples",
            icon: "run",
            userData: { example: "dropdown", source: "commands.js" },
            items: [
              {
                id: "custom.capture.screenshot",
                label: "Take screenshot",
                icon: "photo",
                tint: "#A855F7",
                appCommand: "takescreenshot",
                ribbonCommand: "filetree",
                userData: { kind: "screenshot" },
              },
            ],
          },
          {
            type: "dropdown",
            id: "custom.tools",
            label: "Tools",
            tooltip: "Built-in ribbon command examples",
            icon: "settings",
            items: [
              { id: "custom.tools.theme", label: "Toggle theme", ribbonCommand: "theme" },
              { id: "custom.tools.resetLayout", label: "Reset layout", ribbonCommand: "resetLayout" },
            ],
          },
        ],
      },
      {
        id: "group-dev-files",
        label: "Files",
        items: [
          {
            type: "button",
            id: "custom.command.images-list",
            label: "AI:Images List",
            icon: "folder",
            cliCommand: "llm",
            args: ["agent", "--prompt", "create directory listing, for images, in images.md"],
            cwd: "${CURRENT_PATH}",
            logLevel: "trace",
            enabled: true,
            visible: true,
            registerInExplorer: true,
            source: { kind: "selection" },
            output: {},
            runOptions: { newShellWindow: false },
            userData: {},
          },
          {
            type: "button",
            id: "custom.command.cat",
            label: "cat",
            icon: "settings",
            externalCommand: {
              mode: "shell",
              command: "cat",
              args: [],
              cwd: "",
              shellLine: "cat ${CURRENT_FILE}",
            },
            enabled: true,
            visible: true,
            source: { kind: "selection" },
            output: {},
            userData: {},
          },
          {
            type: "button",
            id: "custom.command.illustration",
            label: "AI:Illustration",
            icon: "photo",
            tint: "#EC4899",
            cliCommand: "transform",
            args: ["--prompt", "as technical illustration", "--src", "${CURRENT_FILE}"],
            enabled: true,
            visible: true,
            registerInExplorer: true,
            source: { kind: "files", files: ["tests/assets/commands/illustration.jpg"] },
            output: {},
            userData: {},
          },
        ],
      },
    ],
  },
};

function flattenCommands(items: CustomCommandItem[] = []): CustomCommandItem[] {
  return items.flatMap((item) => [item, ...flattenCommands(item.items)]);
}

export const sampleCommands = flattenCommands(sampleCommandsDoc.ribbon?.groups?.flatMap((group) => group.items ?? []) ?? []).filter(
  (item) => item.type !== "separator",
);

export function commandActionLabel(command: CustomCommandItem) {
  if (command.cliCommand) return `cli:${command.cliCommand}`;
  if (command.appCommand) return `app:${command.appCommand}`;
  if (command.ribbonCommand) return `ribbon:${command.ribbonCommand}`;
  if (command.externalCommand) return "external";
  if (command.url) return "url";
  if (command.path) return "path";
  return "metadata";
}

function commandToBlock(command: CustomCommandItem): BlockNode {
  return {
    kind: "command",
    id: command.id ?? command.label ?? "custom-command",
    command,
  };
}

const resizeCommand = sampleCommands.find((command) => command.id === "custom.command.illustration") ?? sampleCommands[0];
const chatCommand = sampleCommands.find((command) => command.id === "custom.tools.theme") ?? sampleCommands[0];
const captureCommand = sampleCommands.find((command) => command.id === "custom.capture.screenshot") ?? sampleCommands[0];
const openOutputCommand = sampleCommands.find((command) => command.id === "custom.command.cat") ?? sampleCommands[0];

export function createFaultyBlock(id = "faulty-block"): BlockNode {
  return {
    kind: "runScript",
    id,
    method: "throw new Error('XBlox demo fault: verify red error state');",
  };
}

export const sampleBlocks: BlockNode[] = [
  {
    kind: "runScript",
    id: "init-context",
    method: "return host.notify('xblox ready');",
  },
  {
    kind: "setVariable",
    name: "score",
    expression: "selectionCount + 6",
  },
  {
    kind: "getVariable",
    name: "score",
    target: "lastScore",
  },
  {
    kind: "log",
    level: "info",
    message: "lastScore",
  },
  {
    kind: "if",
    condition: "lastScore >= 7",
    consequent: [
      { kind: "wait", ms: 150 },
      commandToBlock(resizeCommand),
      {
        kind: "for",
        initial: "0",
        final: "selectionCount",
        comparator: "<",
        modifier: "+1",
        items: [
          {
            kind: "command",
            id: "per-file-command",
            command: {
              ...(resizeCommand || {}),
              id: "custom.command.illustration.per-file",
              label: "Illustrate each selected file",
              userData: { fileExpression: "selection[i]" },
            },
          },
        ],
      },
    ],
    alternate: [
      commandToBlock(chatCommand),
      {
        kind: "runScript",
        id: "missing-selection-note",
        method: "return host.notify('No selected files, opening chat instead.');",
      },
    ],
  },
  {
    kind: "switch",
    variable: "mode",
    items: [
      {
        kind: "case",
        comparator: "===",
        expression: "'capture'",
        consequent: [commandToBlock(captureCommand)],
      },
      {
        kind: "case",
        comparator: "===",
        expression: "'open-output'",
        consequent: [commandToBlock(openOutputCommand)],
      },
      {
        kind: "switchDefault",
        consequent: [
          {
            kind: "runScript",
            id: "default-mode",
            method: "return host.notify(`Unhandled mode: ${this.mode}`);",
          },
        ],
      },
    ],
  },
  createFaultyBlock("simulate-host-error"),
  {
    kind: "while",
    condition: "host.hasQueuedWork()",
    loopLimit: 3,
    items: [
      { kind: "wait", ms: 250 },
      {
        kind: "runScript",
        id: "drain-queue",
        method: "return host.runNextQueuedCommand();",
      },
      { kind: "break" },
    ],
  },
];
