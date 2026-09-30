export type CustomCommandItem = {
  type?: "button" | "dropdown" | "separator";
  enabled?: boolean;
  visible?: boolean;
  asLlmTool?: boolean;
  registerInExplorer?: boolean;
  id?: string;
  label?: string;
  tooltip?: string;
  icon?: string;
  tint?: string;
  appCommand?: string;
  cliCommand?: string;
  ribbonCommand?: string;
  args?: string[];
  cwd?: string;
  logLevel?: "trace" | "debug" | "info" | "warn" | "error" | "critical" | "off" | "";
  runOptions?: {
    newShellWindow?: boolean;
    closeOnExit?: boolean;
  };
  url?: string;
  path?: string;
  externalCommand?: {
    mode?: "argv" | "shell";
    command?: string;
    args?: string[];
    shellLine?: string;
    cwd?: string;
  };
  source?: {
    kind?: "selection" | "files" | "folders" | "custom";
    files?: string[];
    folders?: string[];
    includeSubfolders?: boolean;
  };
  output?: {
    directory?: string;
    filenamePattern?: string;
    overwrite?: boolean;
  };
  userData?: unknown;
  items?: CustomCommandItem[];
};

export type CustomCommandGroup = {
  id?: string;
  label?: string;
  items?: CustomCommandItem[];
};

export type CustomCommandsDocument = {
  version?: number;
  ribbon?: {
    groups?: CustomCommandGroup[];
  };
};

export type ChatPresetSnapshot = {
  id: string;
  name: string;
  chat: unknown;
};

export type TablerIconOption = {
  name: string;
  svg: string;
};

export type CliCommandOption = {
  id: string;
  label: string;
  available?: boolean;
};

export type AppCommandOption = CliCommandOption;

export type RibbonCommandOption = CliCommandOption;

export type CliHelpOption = {
  name: string;
  names: string[];
  positional: boolean;
  required: boolean;
  group: string;
  description: string;
  typeName: string;
  default: string;
  expectedMin: number;
  expectedMax: number;
  allowExtraArgs: boolean;
};

export type CliHelpSchema = {
  name: string;
  path?: string;
  description: string;
  allowExtras: boolean;
  options: CliHelpOption[];
  subcommands: Array<{ name: string; description: string }>;
};

export type CustomCommandOption = {
  id: string;
  label: string;
};

export type CustomCommandProviderScope = "image" | "vision" | "video" | "llm";

export type CustomCommandProviderDefault = {
  providerId: string;
  modelId: string;
  providerLabel?: string;
  modelLabel?: string;
};

export type CustomCommandProviderDefaults = Partial<Record<CustomCommandProviderScope, CustomCommandProviderDefault>>;

export type CommandVariableOption = {
  name: string;
  description: string;
  group?: string;
};
