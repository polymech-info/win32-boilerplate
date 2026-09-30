import type React from "react";
import { useMemo, useRef, useState } from "react";
import type { AppCommandOption, CliCommandOption, RibbonCommandOption, CliHelpOption, CliHelpSchema, CommandVariableOption, CustomCommandItem, CustomCommandOption, TablerIconOption } from "./types";
import {
  applyUserDataText,
  argsText,
  defaultAppCommandId,
  defaultRibbonCommandId,
  devAppCommandOptions,
  devRibbonCommandOptions,
  fromArgsText,
  fromCsv,
  toCsv,
  userDataText,
} from "./helpers";
import { CheckRow, EditorSection, FieldRow, SelectField } from "./FormControls";
import { ColorPicker, IconPicker } from "./Pickers";
import { VariableInputField, VariableTextField } from "./VariableBuilder";

const LOG_LEVEL_OPTIONS = [
  { value: "", label: "Use app/default" },
  { value: "trace", label: "Trace" },
  { value: "debug", label: "Debug" },
  { value: "info", label: "Info" },
  { value: "warn", label: "Warn" },
  { value: "error", label: "Error" },
  { value: "critical", label: "Critical" },
  { value: "off", label: "Off" },
];

type PathPickKind = "file" | "folder";

function optionPrimaryName(option: CliHelpOption) {
  return option.names.find((name) => name.startsWith("--")) || option.names[0] || option.name;
}

function normalizedOptionName(option: CliHelpOption) {
  return optionPrimaryName(option).replace(/^-+/, "");
}

function optionMatches(option: CliHelpOption, arg: string) {
  return option.names.includes(arg) || option.name === arg;
}

function isRepeatablePathOption(option: CliHelpOption) {
  const name = normalizedOptionName(option);
  return name === "src" || name === "include";
}

function optionPathKind(option: CliHelpOption): PathPickKind | "both" | null {
  const name = normalizedOptionName(option).toLowerCase();
  const haystack = `${name} ${option.name} ${option.description || ""} ${option.typeName || ""}`.toLowerCase();
  if (name === "src" || name === "include") return "both";
  if (haystack.includes("folder") || haystack.includes("directory") || name.includes("dir") || name === "cwd") return "folder";
  if (haystack.includes("file") || haystack.includes("path") || name.includes("path") || name.includes("output")) return "file";
  return null;
}

function getOptionValues(args: string[] | undefined, option: CliHelpOption) {
  const current = args || [];
  const values: string[] = [];
  for (let i = 0; i < current.length; ++i) {
    if (!optionMatches(option, current[i])) continue;
    if (option.expectedMin === 0) return ["true"];
    if (current[i + 1]) values.push(current[i + 1]);
    i += 1;
  }
  return values;
}

function getOptionValue(args: string[] | undefined, option: CliHelpOption) {
  return getOptionValues(args, option).join("\n");
}

function setOptionValue(args: string[] | undefined, option: CliHelpOption, value: string, repeat = false) {
  const current = args || [];
  const next: string[] = [];
  for (let i = 0; i < current.length; ++i) {
    if (!optionMatches(option, current[i])) {
      next.push(current[i]);
      continue;
    }
    if (option.expectedMin !== 0) i += 1;
  }
  const trimmed = value.trim();
  if (!trimmed) return next;
  const name = optionPrimaryName(option);
  if (option.expectedMin === 0) {
    next.push(name);
  } else if (repeat) {
    for (const line of fromArgsText(value)) next.push(name, line);
  } else {
    next.push(name);
    next.push(value);
  }
  return next;
}

function schemaKeyForArgs(baseCommand: string, args: string[] | undefined, schemas: Record<string, CliHelpSchema>) {
  let key = baseCommand;
  const current = args || [];
  for (const token of current) {
    if (token.startsWith("-")) break;
    const candidate = `${key} ${token}`;
    if (!schemas[candidate]) break;
    key = candidate;
  }
  return key;
}

function subcommandPathOptions(baseCommand: string, schemas: Record<string, CliHelpSchema>) {
  return Object.keys(schemas)
    .filter((key) => key.startsWith(`${baseCommand} `))
    .sort((a, b) => a.localeCompare(b))
    .map((key) => {
      const value = key.slice(baseCommand.length + 1);
      return { value, label: value };
    });
}

function replaceSubcommandPath(baseCommand: string, args: string[] | undefined, schemas: Record<string, CliHelpSchema>, nextPath: string) {
  const current = args || [];
  const currentKey = schemaKeyForArgs(baseCommand, current, schemas);
  const consumed = currentKey === baseCommand ? 0 : currentKey.slice(baseCommand.length + 1).split(/\s+/).filter(Boolean).length;
  const nextTokens = nextPath.split(/\s+/).filter(Boolean);
  return [...nextTokens, ...current.slice(consumed)];
}

function TypeaheadInput({
  value,
  options,
  onChange,
  placeholder,
}: {
  value: string;
  options: CustomCommandOption[];
  onChange: (value: string) => void;
  placeholder?: string;
}) {
  const [open, setOpen] = useState(false);
  const closeTimer = useRef<number | null>(null);
  const query = value.trim().toLowerCase();
  const filtered = useMemo(
    () => options.filter((option) => !query || option.id.toLowerCase().includes(query) || option.label.toLowerCase().includes(query)).slice(0, 30),
    [options, query]
  );

  function scheduleClose() {
    if (closeTimer.current != null) window.clearTimeout(closeTimer.current);
    closeTimer.current = window.setTimeout(() => setOpen(false), 120);
  }

  function cancelClose() {
    if (closeTimer.current != null) {
      window.clearTimeout(closeTimer.current);
      closeTimer.current = null;
    }
  }

  return (
    <div className="pm-typeahead pm-custom-cli-typeahead" onFocus={cancelClose} onBlur={scheduleClose}>
      <input
        type="text"
        value={value}
        placeholder={placeholder}
        onFocus={() => setOpen(true)}
        onChange={(e) => {
          onChange(e.target.value);
          setOpen(true);
        }}
      />
      {open ? (
        <div className="pm-typeahead-menu pm-scroll">
          {filtered.length ? (
            filtered.map((option) => (
              <button
                key={`${option.id}:${option.label}`}
                className="pm-typeahead-item"
                type="button"
                onMouseDown={(e) => {
                  e.preventDefault();
                  onChange(option.id);
                  setOpen(false);
                }}
                title={option.id}
              >
                <span className="pm-typeahead-item-id">{option.id}</span>
                {option.label !== option.id ? <span className="pm-typeahead-item-label">{option.label}</span> : null}
              </button>
            ))
          ) : (
            <div className="pm-typeahead-empty">No suggestions</div>
          )}
        </div>
      ) : null}
    </div>
  );
}

function CliArgsFields({
  baseCommand,
  schemaKey,
  schemas,
  schema,
  args,
  providerOptions,
  modelOptionsByProvider,
  variableOptions,
  onPickPath,
  onChange,
}: {
  baseCommand: string;
  schemaKey: string;
  schemas: Record<string, CliHelpSchema>;
  schema?: CliHelpSchema;
  args?: string[];
  providerOptions?: CustomCommandOption[];
  modelOptionsByProvider?: Record<string, CustomCommandOption[]>;
  variableOptions?: CommandVariableOption[];
  onPickPath?: (kind: PathPickKind) => Promise<string | undefined>;
  onChange: (args: string[]) => void;
}) {
  const editable = (schema?.options || []).filter((option) =>
    !option.positional &&
    !option.names.includes("--help") &&
    !option.names.includes("-h") &&
    option.group !== ""
  );
  if (!schema) return null;
  const nestedOptions = subcommandPathOptions(baseCommand, schemas);
  const selectedNestedPath = schemaKey === baseCommand ? "" : schemaKey.slice(baseCommand.length + 1);
  const selectedProvider = getOptionValue(args, editable.find((option) => normalizedOptionName(option) === "provider") || {
    name: "provider",
    names: ["--provider"],
    positional: false,
    required: false,
    group: "Options",
    description: "",
    typeName: "TEXT",
    default: "",
    expectedMin: 1,
    expectedMax: 1,
    allowExtraArgs: false,
  });
  const modelOptions = selectedProvider ? modelOptionsByProvider?.[selectedProvider] || [] : [];
  return (
    <details className="pm-custom-details pm-custom-cli-args-details pm-scroll" open>
      <summary>CLI arguments</summary>
      <EditorSection className="pm-custom-cli-args-section one-col" title="CLI Args" description="Fill only the options this preset should pass. Empty fields use command defaults.">
        {nestedOptions.length ? (
          <FieldRow label="Subcommand" wide>
            <select className="pm-select" value={selectedNestedPath} onChange={(e) => onChange(replaceSubcommandPath(baseCommand, args, schemas, e.target.value))}>
              <option value="">Top-level command</option>
              {nestedOptions.map((option) => (
                <option key={option.value} value={option.value}>{option.label}</option>
              ))}
            </select>
          </FieldRow>
        ) : null}
        {editable.map((option) => {
          const primary = optionPrimaryName(option);
          const normalized = normalizedOptionName(option);
          const value = getOptionValue(args, option);
          if (option.expectedMin === 0) {
            return (
              <CheckRow
                key={primary}
                label={primary}
                checked={value === "true"}
                onChange={(checked) => onChange(setOptionValue(args, option, checked ? "true" : ""))}
              />
            );
          }
          if (normalized === "provider" && providerOptions?.length) {
            return (
              <FieldRow key={primary} label={primary} wide>
                <select className="pm-select" value={value} onChange={(e) => onChange(setOptionValue(args, option, e.target.value))}>
                  <option value="">Use app default</option>
                  {providerOptions.map((provider) => (
                    <option key={provider.id} value={provider.id}>{provider.label}</option>
                  ))}
                </select>
                {option.description ? <small className="pm-custom-help">{option.description}</small> : null}
              </FieldRow>
            );
          }
          if (normalized === "model") {
            return (
              <FieldRow key={primary} label={primary} wide>
                <TypeaheadInput
                  value={value}
                  options={modelOptions}
                  onChange={(nextValue) => onChange(setOptionValue(args, option, nextValue))}
                  placeholder={option.default || "Use app default"}
                />
                {option.description ? <small className="pm-custom-help">{option.description}</small> : null}
              </FieldRow>
            );
          }
          const repeatPath = isRepeatablePathOption(option);
          const pickerKind = optionPathKind(option);
          const pickIntoOption = async (kind: PathPickKind) => {
            if (!onPickPath) return;
            const picked = await onPickPath(kind);
            if (!picked) return;
            const nextValue = repeatPath && value ? `${value}\n${picked}` : picked;
            onChange(setOptionValue(args, option, nextValue, repeatPath));
          };
          return (
            <FieldRow key={primary} label={primary} wide>
              <VariableTextField
                value={value}
                variables={variableOptions}
                rows={repeatPath ? 2 : 1}
                onChange={(nextValue) => onChange(setOptionValue(args, option, nextValue, repeatPath))}
                placeholder={repeatPath ? "one permanent file/folder per line" : option.default || option.typeName || "default"}
                title={option.description}
              />
              {onPickPath && pickerKind ? (
                <div className="pm-custom-picker-row">
                  {(pickerKind === "file" || pickerKind === "both") ? (
                    <button className="btn" type="button" onClick={() => void pickIntoOption("file")}>Pick file</button>
                  ) : null}
                  {(pickerKind === "folder" || pickerKind === "both") ? (
                    <button className="btn" type="button" onClick={() => void pickIntoOption("folder")}>Pick folder</button>
                  ) : null}
                </div>
              ) : null}
              <small className="pm-custom-help">
                {repeatPath
                  ? "Permanent preset entries. CLI/UI invocation may append more."
                  : option.description || "Leave empty to use the command default."}
              </small>
            </FieldRow>
          );
        })}
      </EditorSection>
    </details>
  );
}

export function CommandEditor({
  item,
  prefix,
  tablerIcons,
  cliCommands,
  appCommands = devAppCommandOptions(),
  ribbonCommands = devRibbonCommandOptions(),
  cliCommandSchemas,
  providerOptions,
  modelOptionsByProvider,
  variableOptions,
  compact,
  children,
  onChange,
  onReplace,
  onRemove,
  onAddChild,
  onPickPath,
}: {
  item: CustomCommandItem;
  prefix: string;
  tablerIcons: TablerIconOption[];
  cliCommands: CliCommandOption[];
  appCommands?: AppCommandOption[];
  ribbonCommands?: RibbonCommandOption[];
  cliCommandSchemas?: Record<string, CliHelpSchema>;
  providerOptions?: CustomCommandOption[];
  modelOptionsByProvider?: Record<string, CustomCommandOption[]>;
  variableOptions?: CommandVariableOption[];
  compact?: boolean;
  children?: React.ReactNode;
  onChange: (patch: Partial<CustomCommandItem>) => void;
  onReplace: (item: CustomCommandItem) => void;
  onRemove: () => void;
  onAddChild?: () => void;
  onPickPath?: (kind: PathPickKind) => Promise<string | undefined>;
}) {
  if (item.type === "separator") {
    return (
      <div className={`pm-custom-command-editor ${compact ? "compact" : ""}`}>
        <div className="pm-mcp-head">
          <strong>{prefix}: separator</strong>
          <button className="btn" type="button" onClick={onRemove}>Remove</button>
        </div>
      </div>
    );
  }

  const source = item.source || { kind: "selection" };
  const output = item.output || {};
  const external = item.externalCommand || {};
  const actionKind = item.ribbonCommand
    ? "ribbon"
    : item.cliCommand
      ? "cli"
      : item.appCommand
        ? "app"
        : item.url
          ? "url"
          : item.path
            ? "path"
          : item.externalCommand
              ? "external"
              : "none";

  function setAction(kind: string) {
    const next = { ...item };
    delete next.ribbonCommand;
    delete next.cliCommand;
    delete next.appCommand;
    delete next.url;
    delete next.path;
    delete next.externalCommand;
    if (kind === "app") next.appCommand = defaultAppCommandId(appCommands);
    if (kind === "cli") next.cliCommand = cliCommands.find((c) => c.available !== false)?.id || "resize";
    if (kind === "ribbon") next.ribbonCommand = defaultRibbonCommandId(ribbonCommands);
    if (kind === "url") next.url = "https://polymech.info";
    if (kind === "path") next.path = "";
    if (kind === "external") next.externalCommand = { command: "", args: [], cwd: "" };
    onReplace(next);
  }

  const selectedCli = item.cliCommand || cliCommands.find((c) => c.available !== false)?.id || "resize";
  const schemas = cliCommandSchemas || {};
  const selectedSchemaKey = schemaKeyForArgs(selectedCli, item.args, schemas);
  const selectedSchema = schemas[selectedSchemaKey] || schemas[selectedCli];
  const runOptions = item.runOptions || {};
  const hasProcessOptions = actionKind === "cli" || actionKind === "external";
  const pickPath = async (kind: PathPickKind, apply: (path: string) => void) => {
    if (!onPickPath) return;
    const picked = await onPickPath(kind);
    if (picked) apply(picked);
  };

  return (
    <div className={`pm-custom-command-editor ${compact ? "compact" : ""}`}>
      <div className="pm-mcp-head">
        <strong>{prefix}: {item.label || item.id || "Command"}</strong>
        <div className="pm-custom-actions">
          {onAddChild ? <button className="btn" type="button" onClick={onAddChild}>Add child</button> : null}
          <button className="btn" type="button" onClick={onRemove}>Remove</button>
        </div>
      </div>
      <EditorSection title="General" description="Visibility and identity for this ribbon item.">
        <CheckRow label="Enabled" checked={item.enabled !== false} onChange={(checked) => onChange({ enabled: checked })} />
        <CheckRow label="Visible" checked={item.visible !== false} onChange={(checked) => onChange({ visible: checked })} />
        <CheckRow label="As LLM Tool" checked={!!item.asLlmTool} onChange={(checked) => onChange({ asLlmTool: checked })} />
        <CheckRow label="Register in Explorer" checked={!!item.registerInExplorer} onChange={(checked) => onChange({ registerInExplorer: checked })} />
        <small className="pm-custom-help">Opt-in. register-explorer adds eligible checked commands to the Explorer context menu.</small>
        <FieldRow label="Name">
          <VariableInputField value={item.label || ""} variables={variableOptions} onChange={(label) => onChange({ label })} />
        </FieldRow>
        <FieldRow label="Type">
          <SelectField
            value={item.type || "button"}
            options={[
              { value: "button", label: "Button" },
              { value: "dropdown", label: "Dropdown" },
            ]}
            onChange={(value) => onChange({ type: value as CustomCommandItem["type"] })}
          />
        </FieldRow>
        <FieldRow label="ID" wide>
          <VariableInputField value={item.id || ""} variables={variableOptions} onChange={(id) => onChange({ id })} />
        </FieldRow>
      </EditorSection>

      <EditorSection title="Appearance" description="Icon, tint, and hover text.">
        <IconPicker value={item.icon || ""} icons={tablerIcons} variables={variableOptions} onChange={(icon) => onChange({ icon })} />
        <ColorPicker value={item.tint || ""} variables={variableOptions} onChange={(tint) => onChange({ tint })} />
        <FieldRow label="Tooltip" wide>
          <VariableInputField value={item.tooltip || ""} variables={variableOptions} onChange={(tooltip) => onChange({ tooltip })} />
        </FieldRow>
      </EditorSection>

      <EditorSection title="Action" description="What runs when the ribbon item is clicked.">
        <FieldRow label="Kind">
          <SelectField
            value={actionKind}
            options={[
              { value: "none", label: "None / metadata only" },
              { value: "app", label: "Internal app command" },
              { value: "cli", label: "Registered CLI command" },
              { value: "ribbon", label: "Internal ribbon command" },
              { value: "external", label: "External command" },
              { value: "url", label: "Open URL" },
              { value: "path", label: "Open path" },
            ]}
            onChange={setAction}
          />
        </FieldRow>
        {actionKind === "app" ? (
          <>
            <FieldRow label="App command">
              <SelectField
                value={item.appCommand || defaultAppCommandId(appCommands)}
                options={appCommands.map((c) => ({
                  value: c.id,
                  label: `${c.label || c.id}${c.available === false ? " (disabled in build)" : ""}`,
                  disabled: c.available === false,
                }))}
                onChange={(value) => onChange({ appCommand: value })}
              />
            </FieldRow>
            <FieldRow label="Args" wide>
              <VariableTextField value={argsText(item.args)} variables={variableOptions} rows={1} onChange={(value) => onChange({ args: fromArgsText(value) })} placeholder="one argument per line" />
            </FieldRow>
          </>
        ) : null}
        {actionKind === "cli" ? (
          <>
            <FieldRow label="CLI command">
              <SelectField
                value={selectedCli}
                options={cliCommands.map((c) => ({ value: c.id, label: `${c.label || c.id}${c.available === false ? " (disabled in build)" : ""}`, disabled: c.available === false }))}
                onChange={(value) => onChange({ cliCommand: value })}
              />
            </FieldRow>
            <FieldRow label="Args" wide>
              <VariableTextField value={argsText(item.args)} variables={variableOptions} rows={1} onChange={(value) => onChange({ args: fromArgsText(value) })} placeholder="one argument per line" />
            </FieldRow>
          </>
        ) : null}
        {actionKind === "ribbon" ? (
          <>
            <FieldRow label="Ribbon command">
              <SelectField
                value={item.ribbonCommand || defaultRibbonCommandId(ribbonCommands)}
                options={ribbonCommands.map((c) => ({
                  value: c.id,
                  label: `${c.label || c.id}${c.available === false ? " (disabled in build)" : ""}`,
                  disabled: c.available === false,
                }))}
                onChange={(value) => onChange({ ribbonCommand: value })}
              />
            </FieldRow>
            <FieldRow label="Args" wide>
              <VariableTextField value={argsText(item.args)} variables={variableOptions} rows={1} onChange={(value) => onChange({ args: fromArgsText(value) })} placeholder="one argument per line" />
            </FieldRow>
          </>
        ) : null}
        {actionKind === "url" ? (
          <FieldRow label="URL" wide>
            <VariableInputField value={item.url || ""} variables={variableOptions} onChange={(url) => onChange({ url })} />
          </FieldRow>
        ) : null}
        {actionKind === "path" ? (
          <FieldRow label="Path" wide>
            <VariableInputField value={item.path || ""} variables={variableOptions} onChange={(path) => onChange({ path })} />
            {onPickPath ? (
              <div className="pm-custom-picker-row">
                <button className="btn" type="button" onClick={() => void pickPath("file", (path) => onChange({ path }))}>Pick file</button>
                <button className="btn" type="button" onClick={() => void pickPath("folder", (path) => onChange({ path }))}>Pick folder</button>
              </div>
            ) : null}
          </FieldRow>
        ) : null}
        {actionKind === "external" ? (
          <>
            <FieldRow label="Mode">
              <select
                className="pm-select"
                value={external.mode || "argv"}
                onChange={(e) => onChange({ externalCommand: { ...external, mode: e.target.value as "argv" | "shell" } })}
              >
                <option value="argv">Command + argv args</option>
                <option value="shell">Raw shell line</option>
              </select>
            </FieldRow>
            {(external.mode || "argv") === "shell" ? (
              <FieldRow label="Shell line" wide>
                <VariableTextField
                  value={external.shellLine || ""}
                  variables={variableOptions}
                  rows={2}
                  onChange={(shellLine) => onChange({ externalCommand: { ...external, shellLine } })}
                  placeholder={'ls -l >> ls.out\nGet-ChildItem | Out-File listing.txt'}
                />
                <small className="pm-custom-help">PowerShell syntax. Use this for pipes, redirection, variables, aliases, and compound commands.</small>
              </FieldRow>
            ) : (
              <>
                <FieldRow label="External command" wide>
                  <VariableInputField value={external.command || ""} variables={variableOptions} onChange={(command) => onChange({ externalCommand: { ...external, command } })} />
                  {onPickPath ? <button className="btn" type="button" onClick={() => void pickPath("file", (command) => onChange({ externalCommand: { ...external, command } }))}>Pick file</button> : null}
                </FieldRow>
                <FieldRow label="Args" wide>
                  <VariableTextField
                    value={argsText(external.args)}
                    variables={variableOptions}
                    rows={1}
                    onChange={(value) => onChange({ externalCommand: { ...external, args: fromArgsText(value) } })}
                    placeholder="one argument per line; split shell operators into separate lines if needed"
                  />
                </FieldRow>
              </>
            )}
            <FieldRow label="CWD" wide>
              <VariableInputField value={external.cwd || ""} variables={variableOptions} onChange={(cwd) => onChange({ externalCommand: { ...external, cwd } })} />
              {onPickPath ? <button className="btn" type="button" onClick={() => void pickPath("folder", (cwd) => onChange({ externalCommand: { ...external, cwd } }))}>Pick folder</button> : null}
            </FieldRow>
          </>
        ) : null}
      </EditorSection>

      {hasProcessOptions ? (
        <EditorSection title="Run Options" description="Process-level options for command invocation.">
          {actionKind === "cli" ? (
            <>
              <FieldRow label="Working directory" wide>
                <VariableInputField
                  value={item.cwd || ""}
                  variables={variableOptions}
                  onChange={(cwd) => onChange({ cwd })}
                  placeholder="Root --cwd for pm-image-cli; empty = current/default"
                />
                {onPickPath ? <button className="btn" type="button" onClick={() => void pickPath("folder", (cwd) => onChange({ cwd }))}>Pick folder</button> : null}
              </FieldRow>
              <FieldRow label="Log level">
                <select
                  className="pm-select"
                  value={item.logLevel || ""}
                  onChange={(e) => onChange({ logLevel: e.target.value as CustomCommandItem["logLevel"] })}
                >
                  {LOG_LEVEL_OPTIONS.map((option) => (
                    <option key={option.value || "default"} value={option.value}>{option.label}</option>
                  ))}
                </select>
              </FieldRow>
            </>
          ) : null}
          <CheckRow
            label="New Shell Window"
            checked={!!runOptions.newShellWindow}
            onChange={(checked) => onChange({ runOptions: { ...runOptions, newShellWindow: checked } })}
          />
          <CheckRow
            label="Close on exit"
            checked={!!runOptions.closeOnExit}
            onChange={(checked) => onChange({ runOptions: { ...runOptions, closeOnExit: checked } })}
          />
        </EditorSection>
      ) : null}

      {actionKind === "cli" ? (
        <CliArgsFields
          baseCommand={selectedCli}
          schemaKey={selectedSchemaKey}
          schemas={schemas}
          schema={selectedSchema}
          args={item.args}
          providerOptions={providerOptions}
          modelOptionsByProvider={modelOptionsByProvider}
          variableOptions={variableOptions}
          onPickPath={onPickPath}
          onChange={(args) => onChange({ args })}
        />
      ) : null}

      <details className="pm-custom-details">
        <summary>Sources and output overrides</summary>
        <EditorSection title="Sources" description="Optional input selection and output naming overrides.">
          <FieldRow label="Source">
            <SelectField
              value={source.kind || "selection"}
              options={[
                { value: "selection", label: "Current selection" },
                { value: "files", label: "Files" },
                { value: "folders", label: "Folders" },
                { value: "custom", label: "Custom payload" },
              ]}
              onChange={(value) => onChange({ source: { ...source, kind: value as NonNullable<CustomCommandItem["source"]>["kind"] } })}
            />
          </FieldRow>
          <FieldRow label="Files" wide>
            <VariableInputField value={toCsv(source.files)} variables={variableOptions} onChange={(value) => onChange({ source: { ...source, files: fromCsv(value) } })} />
            {onPickPath ? <button className="btn" type="button" onClick={() => void pickPath("file", (file) => onChange({ source: { ...source, files: [...(source.files || []), file] } }))}>Pick file</button> : null}
          </FieldRow>
          <FieldRow label="Folders" wide>
            <VariableInputField value={toCsv(source.folders)} variables={variableOptions} onChange={(value) => onChange({ source: { ...source, folders: fromCsv(value) } })} />
            {onPickPath ? <button className="btn" type="button" onClick={() => void pickPath("folder", (folder) => onChange({ source: { ...source, folders: [...(source.folders || []), folder] } }))}>Pick folder</button> : null}
          </FieldRow>
          <CheckRow label="Include subfolders" checked={!!source.includeSubfolders} onChange={(checked) => onChange({ source: { ...source, includeSubfolders: checked } })} />
          <FieldRow label="Output directory" wide>
            <VariableInputField value={output.directory || ""} variables={variableOptions} onChange={(directory) => onChange({ output: { ...output, directory } })} />
            {onPickPath ? <button className="btn" type="button" onClick={() => void pickPath("folder", (directory) => onChange({ output: { ...output, directory } }))}>Pick folder</button> : null}
          </FieldRow>
          <FieldRow label="Filename pattern" wide>
            <VariableInputField value={output.filenamePattern || ""} variables={variableOptions} onChange={(filenamePattern) => onChange({ output: { ...output, filenamePattern } })} placeholder="{name}-{op}.{ext}" />
          </FieldRow>
          <CheckRow label="Overwrite outputs" checked={!!output.overwrite} onChange={(checked) => onChange({ output: { ...output, overwrite: checked } })} />
        </EditorSection>
      </details>
      <details className="pm-custom-details">
        <summary>Advanced user data JSON</summary>
        <EditorSection title="User Data" description="Raw JSON metadata carried by the command.">
          <FieldRow label="JSON" wide>
            <VariableTextField value={userDataText(item)} variables={variableOptions} rows={4} onChange={(value) => onReplace(applyUserDataText(item, value))} className="pm-scroll" />
          </FieldRow>
        </EditorSection>
      </details>
      {children}
    </div>
  );
}
