import type { CSSProperties } from "react";

import { applyUserDataText, argsText, commandPayloadRows, fromArgsText, fromCsv, toCsv, userDataText } from "./helpers";
import type { CustomCommandItem } from "./types";
import { VariableInputField, VariableTextField } from "./VariableBuilder";

type CommandPropsEditorProps = {
  value: CustomCommandItem;
  onChange?: (value: CustomCommandItem) => void;
  inputStyle?: CSSProperties;
  readOnly?: boolean;
  hiddenFields?: readonly string[];
};

function setStringField(command: CustomCommandItem, key: keyof CustomCommandItem, value: string): CustomCommandItem {
  const next = { ...command };
  if (!value.trim()) {
    delete next[key];
  } else {
    (next as Record<string, unknown>)[key as string] = value;
  }
  return next;
}

function setBooleanField(command: CustomCommandItem, key: keyof CustomCommandItem, value: boolean): CustomCommandItem {
  return { ...command, [key]: value };
}

function clearActionFields(command: CustomCommandItem): CustomCommandItem {
  const next = { ...command };
  delete next.cliCommand;
  delete next.appCommand;
  delete next.ribbonCommand;
  delete next.externalCommand;
  delete next.url;
  delete next.path;
  return next;
}

function actionKind(command: CustomCommandItem) {
  if (command.cliCommand) return "cli";
  if (command.appCommand) return "app";
  if (command.ribbonCommand) return "ribbon";
  if (command.externalCommand) return "external";
  if (command.url) return "url";
  if (command.path) return "path";
  return "metadata";
}

function textRows(value: string) {
  return value.includes("\n") ? 4 : 1;
}

export function CommandPropsEditor({ value, onChange, inputStyle, readOnly, hiddenFields = [] }: CommandPropsEditorProps) {
  const source = value.source || {};
  const output = value.output || {};
  const runOptions = value.runOptions || {};
  const external = value.externalCommand || {};
  const currentAction = actionKind(value);
  const hidden = new Set(hiddenFields);
  const show = (field: string) => !hidden.has(field);

  if (readOnly || !onChange) {
    return (
      <section className="xblox-command-props">
        <h4>Command properties</h4>
        {commandPayloadRows(value as Record<string, unknown>).map(([key, rowValue]) => (
          <label key={key}>
            {key}
            <textarea readOnly value={rowValue} rows={textRows(rowValue)} style={inputStyle} />
          </label>
        ))}
      </section>
    );
  }

  return (
    <section className="xblox-command-props">
      <h4>Command properties</h4>
      {show("id") ? <label>
        id
        <input value={value.id ?? ""} onChange={(event) => onChange(setStringField(value, "id", event.target.value))} style={inputStyle} />
      </label> : null}
      {show("label") ? <label>
        label
        <input value={value.label ?? ""} onChange={(event) => onChange(setStringField(value, "label", event.target.value))} style={inputStyle} />
      </label> : null}
      {show("type") ? <label>
        type
        <select value={value.type || "button"} onChange={(event) => onChange({ ...value, type: event.target.value as CustomCommandItem["type"] })} style={inputStyle}>
          <option value="button">button</option>
          <option value="dropdown">dropdown</option>
          <option value="separator">separator</option>
        </select>
      </label> : null}
      {show("tooltip") ? <label>
        tooltip
        <input value={value.tooltip ?? ""} onChange={(event) => onChange(setStringField(value, "tooltip", event.target.value))} style={inputStyle} />
      </label> : null}
      {show("icon") ? <label>
        icon
        <input value={value.icon ?? ""} onChange={(event) => onChange(setStringField(value, "icon", event.target.value))} style={inputStyle} />
      </label> : null}
      {show("tint") ? <label>
        tint
        <input value={value.tint ?? ""} onChange={(event) => onChange(setStringField(value, "tint", event.target.value))} style={inputStyle} />
      </label> : null}
      <label>
        action
        <select
          value={currentAction}
          onChange={(event) => {
            const next = clearActionFields(value);
            const kind = event.target.value;
            if (kind === "cli") onChange({ ...next, cliCommand: "resize" });
            else if (kind === "app") onChange({ ...next, appCommand: "chat" });
            else if (kind === "ribbon") onChange({ ...next, ribbonCommand: "settings" });
            else if (kind === "external") onChange({ ...next, externalCommand: { mode: "shell", shellLine: "" } });
            else if (kind === "url") onChange({ ...next, url: "" });
            else if (kind === "path") onChange({ ...next, path: "" });
            else onChange(next);
          }}
          style={inputStyle}
        >
          <option value="metadata">metadata</option>
          <option value="cli">cli</option>
          <option value="app">app</option>
          <option value="ribbon">ribbon</option>
          <option value="external">external</option>
          <option value="url">url</option>
          <option value="path">path</option>
        </select>
      </label>
      <label>
        cliCommand
        <VariableInputField value={value.cliCommand ?? ""} onChange={(next) => onChange(setStringField(value, "cliCommand", next))} inputStyle={inputStyle} />
      </label>
      <label>
        appCommand
        <VariableInputField value={value.appCommand ?? ""} onChange={(next) => onChange(setStringField(value, "appCommand", next))} inputStyle={inputStyle} />
      </label>
      <label>
        ribbonCommand
        <VariableInputField value={value.ribbonCommand ?? ""} onChange={(next) => onChange(setStringField(value, "ribbonCommand", next))} inputStyle={inputStyle} />
      </label>
      <label>
        url
        <VariableInputField value={value.url ?? ""} onChange={(next) => onChange(setStringField(value, "url", next))} inputStyle={inputStyle} />
      </label>
      <label>
        path
        <VariableInputField value={value.path ?? ""} onChange={(next) => onChange(setStringField(value, "path", next))} inputStyle={inputStyle} />
      </label>
      <label>
        external mode
        <select
          value={external.mode || "shell"}
          onChange={(event) => onChange({ ...value, externalCommand: { ...external, mode: event.target.value as NonNullable<CustomCommandItem["externalCommand"]>["mode"] } })}
          style={inputStyle}
        >
          <option value="shell">shell</option>
          <option value="argv">argv</option>
        </select>
      </label>
      <label>
        external command
        <VariableInputField value={external.command ?? ""} onChange={(command) => onChange({ ...value, externalCommand: { ...external, command } })} inputStyle={inputStyle} />
      </label>
      <label>
        external shellLine
        <VariableTextField value={external.shellLine ?? ""} onChange={(shellLine) => onChange({ ...value, externalCommand: { ...external, shellLine } })} rows={2} inputStyle={inputStyle} />
      </label>
      <label>
        external cwd
        <VariableInputField value={external.cwd ?? ""} onChange={(cwd) => onChange({ ...value, externalCommand: { ...external, cwd } })} inputStyle={inputStyle} />
      </label>
      <label>
        args
        <textarea
          value={argsText(value.args)}
          onChange={(event) => onChange({ ...value, args: fromArgsText(event.target.value) })}
          rows={3}
          style={inputStyle}
        />
      </label>
      <label>
        external args
        <textarea
          value={argsText(external.args)}
          onChange={(event) => onChange({ ...value, externalCommand: { ...external, args: fromArgsText(event.target.value) } })}
          rows={3}
          style={inputStyle}
        />
      </label>
      <label>
        cwd
        <VariableInputField value={value.cwd ?? ""} onChange={(next) => onChange(setStringField(value, "cwd", next))} inputStyle={inputStyle} />
      </label>
      <label>
        logLevel
        <select value={value.logLevel || ""} onChange={(event) => onChange({ ...value, logLevel: event.target.value as CustomCommandItem["logLevel"] })} style={inputStyle}>
          <option value="">default</option>
          <option value="trace">trace</option>
          <option value="debug">debug</option>
          <option value="info">info</option>
          <option value="warn">warn</option>
          <option value="error">error</option>
          <option value="critical">critical</option>
          <option value="off">off</option>
        </select>
      </label>
      {show("source") ? <label>
        source kind
        <select
          value={source.kind || "selection"}
          onChange={(event) => onChange({ ...value, source: { ...source, kind: event.target.value as NonNullable<CustomCommandItem["source"]>["kind"] } })}
          style={inputStyle}
        >
          <option value="selection">selection</option>
          <option value="files">files</option>
          <option value="folders">folders</option>
          <option value="custom">custom</option>
        </select>
      </label> : null}
      {show("source") ? <label>
        source files
        <textarea value={toCsv(source.files)} onChange={(event) => onChange({ ...value, source: { ...source, files: fromCsv(event.target.value) } })} rows={2} style={inputStyle} />
      </label> : null}
      {show("source") ? <label>
        source folders
        <textarea value={toCsv(source.folders)} onChange={(event) => onChange({ ...value, source: { ...source, folders: fromCsv(event.target.value) } })} rows={2} style={inputStyle} />
      </label> : null}
      {show("output") ? <label>
        output directory
        <input value={output.directory ?? ""} onChange={(event) => onChange({ ...value, output: { ...output, directory: event.target.value } })} style={inputStyle} />
      </label> : null}
      {show("output") ? <label>
        filename pattern
        <input value={output.filenamePattern ?? ""} onChange={(event) => onChange({ ...value, output: { ...output, filenamePattern: event.target.value } })} style={inputStyle} />
      </label> : null}
      <label>
        userData JSON
        <textarea value={userDataText(value)} onChange={(event) => onChange(applyUserDataText(value, event.target.value))} rows={4} style={inputStyle} />
      </label>
      {show("enabled") ? <label>
        <input type="checkbox" checked={value.enabled ?? true} onChange={(event) => onChange(setBooleanField(value, "enabled", event.target.checked))} />
        enabled
      </label> : null}
      {show("visible") ? <label>
        <input type="checkbox" checked={value.visible ?? true} onChange={(event) => onChange(setBooleanField(value, "visible", event.target.checked))} />
        visible
      </label> : null}
      {show("registerInExplorer") ? <label>
        <input type="checkbox" checked={!!value.registerInExplorer} onChange={(event) => onChange(setBooleanField(value, "registerInExplorer", event.target.checked))} />
        registerInExplorer
      </label> : null}
      {show("source") ? <label>
        <input type="checkbox" checked={!!source.includeSubfolders} onChange={(event) => onChange({ ...value, source: { ...source, includeSubfolders: event.target.checked } })} />
        includeSubfolders
      </label> : null}
      {show("output") ? <label>
        <input type="checkbox" checked={!!output.overwrite} onChange={(event) => onChange({ ...value, output: { ...output, overwrite: event.target.checked } })} />
        overwrite outputs
      </label> : null}
      {show("runOptions") ? <label>
        <input type="checkbox" checked={!!runOptions.newShellWindow} onChange={(event) => onChange({ ...value, runOptions: { ...runOptions, newShellWindow: event.target.checked } })} />
        newShellWindow
      </label> : null}
      {show("runOptions") ? <label>
        <input type="checkbox" checked={!!runOptions.closeOnExit} onChange={(event) => onChange({ ...value, runOptions: { ...runOptions, closeOnExit: event.target.checked } })} />
        closeOnExit
      </label> : null}
      {show("rawJson") ? <label>
        raw JSON
        <textarea readOnly value={JSON.stringify(value, null, 2)} rows={6} style={inputStyle} />
      </label> : null}
    </section>
  );
}
