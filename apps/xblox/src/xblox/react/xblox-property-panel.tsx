import type { CSSProperties } from "react";

import { CommandPropsEditor } from "@pm/shared/customCommands/CommandPropsEditor";
import { customCommandToRunScriptMethod, parseRunCustomCommandPayload } from "@pm/shared/customCommands/helpers";
import type { CustomCommandItem } from "@pm/shared/customCommands/types";
import { VariableInputField, VariableTextField } from "@/settings/customCommands/VariableBuilder";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import type { XbloxBlock } from "./BlockModel";
import { Variables } from "./Variables";

export type { XbloxBlock } from "./BlockModel";

type XbloxBlockPropertyEditorProps = {
  block: XbloxBlock | null;
  onPatch: (patch: Partial<Record<string, unknown>>) => void;
  nativeParams?: NativeBlockParam[];
};

export type XbloxPropertyPanelProps = XbloxBlockPropertyEditorProps & {
  rootScopeDraft: string;
  rootScopeError: string | null;
  onRootScopeDraftChange: (value: string) => void;
  onApplyRootScopeDraft: () => void;
  canEditRootScope: boolean;
};

const inputStyle: CSSProperties = {
  width: "100%",
  border: "1px solid var(--xblox-button-border, #334155)",
  borderRadius: 6,
  background: "var(--xblox-input-bg, #020617)",
  color: "var(--xblox-text, #d7dee9)",
  padding: 8,
};

const xbloxCommandHiddenFields = ["id", "icon", "tint", "enabled", "visible", "registerInExplorer", "source", "output", "runOptions", "rawJson"] as const;

export type NativeBlockParam = {
  name?: unknown;
  type?: unknown;
  default?: unknown;
  required?: unknown;
  description?: unknown;
};

const genericParamHidden = new Set(["enabled", "storeAs", "storeVariable", "storeResult", "continueOnError", "background"]);

function unsetWhenDefault<T>(value: T, defaultValue: T): T | undefined {
  return Object.is(value, defaultValue) ? undefined : value;
}

function commandPayloadFromBlock(block: XbloxBlock | null): CustomCommandItem | null {
  if (!block) return null;
  if (block.kind === "command") return block.command;
  if (block.kind === "runScript") return parseRunCustomCommandPayload(block.method);
  return null;
}

function blockRecord(block: XbloxBlock): Record<string, unknown> {
  return block as unknown as Record<string, unknown>;
}

function paramName(param: NativeBlockParam): string {
  return typeof param.name === "string" ? param.name : "";
}

function paramType(param: NativeBlockParam): string {
  return typeof param.type === "string" ? param.type : "string";
}

function jsonText(value: unknown) {
  if (value == null) return "";
  return typeof value === "string" ? value : JSON.stringify(value, null, 2);
}

function parseArrayText(text: string, type: string): unknown[] | undefined {
  const trimmed = text.trim();
  if (!trimmed) return undefined;
  try {
    const parsed = JSON.parse(trimmed);
    return Array.isArray(parsed) ? parsed : [parsed];
  } catch {
    const parts = trimmed.split(",").map((part) => part.trim()).filter(Boolean);
    if (type.includes("integer") || type.includes("number")) {
      return parts.map((part) => Number(part)).filter((value) => Number.isFinite(value));
    }
    return parts;
  }
}

function patchJsonValue(name: string, text: string, onPatch: (patch: Partial<Record<string, unknown>>) => void) {
  const trimmed = text.trim();
  if (!trimmed) {
    onPatch({ [name]: undefined });
    return;
  }
  try {
    onPatch({ [name]: JSON.parse(trimmed) });
  } catch {
    onPatch({ [name]: text });
  }
}

function GenericNativeParamEditor({
  block,
  params,
  onPatch,
}: {
  block: XbloxBlock;
  params?: NativeBlockParam[];
  onPatch: (patch: Partial<Record<string, unknown>>) => void;
}) {
  const record = blockRecord(block);
  const visibleParams = (params ?? []).filter((param) => {
    const name = paramName(param);
    return name && name !== "kind" && !genericParamHidden.has(name);
  });
  if (!visibleParams.length) return null;

  return (
    <>
      <hr className="xblox-property-divider" />
      {visibleParams.map((param) => {
        const name = paramName(param);
        const type = paramType(param);
        const value = record[name] ?? param.default;
        const description = typeof param.description === "string" ? param.description : "";
        const label = `${name}${param.required ? " *" : ""}`;

        if (type === "boolean") {
          return (
            <label key={name} className="xblox-checkbox-label" title={description}>
              <input type="checkbox" checked={Boolean(value)} onChange={(event) => onPatch({ [name]: event.target.checked })} />
              {label}
            </label>
          );
        }

        if (type === "integer" || type === "number") {
          return (
            <label key={name} title={description}>
              {label}
              <input
                type="number"
                value={typeof value === "number" ? value : ""}
                onChange={(event) => onPatch({ [name]: event.target.value === "" ? undefined : Number(event.target.value) })}
                style={inputStyle}
              />
            </label>
          );
        }

        if (type.endsWith("[]")) {
          return (
            <label key={name} title={description}>
              {label}
              <textarea
                value={jsonText(value)}
                onChange={(event) => onPatch({ [name]: parseArrayText(event.target.value, type) })}
                rows={3}
                placeholder={type.includes("integer") ? "[11, 22, 33]" : "[\"a\", \"b\"]"}
                style={inputStyle}
              />
            </label>
          );
        }

        if (type === "json") {
          return (
            <label key={name} title={description}>
              {label}
              <textarea value={jsonText(value)} onChange={(event) => patchJsonValue(name, event.target.value, onPatch)} rows={4} style={inputStyle} />
            </label>
          );
        }

        return (
          <label key={name} title={description}>
            {label}
            <VariableInputField value={value == null ? "" : String(value)} onChange={(next) => onPatch({ [name]: next || undefined })} inputStyle={inputStyle} />
          </label>
        );
      })}
    </>
  );
}

export function XbloxBlockPropertyEditor({ block, onPatch, nativeParams }: XbloxBlockPropertyEditorProps) {
  const selectedCommandPayload = commandPayloadFromBlock(block);

  if (!block) return <p>Select a block to edit properties.</p>;
  const blockProps = block as XbloxBlock & { enabled?: boolean; storeAs?: string; continueOnError?: boolean; background?: boolean };
  const isShellOrCommand = block.kind === "command" || block.kind === "shell" || block.kind === "Shell";
  const continueOnErrorDefault = isShellOrCommand;

  return (
    <div className="xblox-property-editor">
      <label className="xblox-checkbox-label">
        <input type="checkbox" checked={blockProps.enabled ?? true} onChange={(event) => onPatch({ enabled: event.target.checked })} />
        Enabled
      </label>
      {isShellOrCommand ? (
        <>
          <label className="xblox-checkbox-label">
            <input
              type="checkbox"
              checked={blockProps.continueOnError ?? continueOnErrorDefault}
              onChange={(event) => onPatch({ continueOnError: unsetWhenDefault(event.target.checked, continueOnErrorDefault) })}
            />
            Continue on Error
          </label>
          <label className="xblox-checkbox-label">
            <input type="checkbox" checked={blockProps.background ?? false} onChange={(event) => onPatch({ background: unsetWhenDefault(event.target.checked, false) })} />
            Background
          </label>
        </>
      ) : null}
      {!isShellOrCommand ? (
        <>
          <label className="xblox-checkbox-label">
            <input
              type="checkbox"
              checked={blockProps.continueOnError ?? false}
              onChange={(event) => onPatch({ continueOnError: unsetWhenDefault(event.target.checked, false) })}
            />
            Continue on Error
          </label>
          <label className="xblox-checkbox-label">
            <input type="checkbox" checked={blockProps.background ?? false} onChange={(event) => onPatch({ background: unsetWhenDefault(event.target.checked, false) })} />
            Background
          </label>
        </>
      ) : null}
      <label>
        Store as
        <VariableInputField value={blockProps.storeAs ?? ""} onChange={(value) => onPatch({ storeAs: value || undefined })} inputStyle={inputStyle} />
      </label>
      <hr className="xblox-property-divider" />
      {"condition" in block ? (
        <label>
          Condition
          <input value={block.condition} onChange={(event) => onPatch({ condition: event.target.value })} style={inputStyle} />
        </label>
      ) : null}
      {"method" in block ? (
        <label>
          Method
          <textarea value={block.method} onChange={(event) => onPatch({ method: event.target.value })} rows={5} style={inputStyle} />
        </label>
      ) : null}
      {block.kind === "setVariable" || block.kind === "getVariable" ? (
        <label>
          Name
          <input value={block.name} onChange={(event) => onPatch({ name: event.target.value })} style={inputStyle} />
        </label>
      ) : null}
      {block.kind === "setVariable" ? (
        <>
          <label>
            Expression
            <input value={block.expression ?? ""} onChange={(event) => onPatch({ expression: event.target.value })} style={inputStyle} />
          </label>
          <label>
            Literal JSON value
            <textarea
              value={block.value == null ? "" : JSON.stringify(block.value, null, 2)}
              onChange={(event) => {
                const text = event.target.value.trim();
                if (!text) onPatch({ value: undefined });
                else {
                  try {
                    onPatch({ value: JSON.parse(text), expression: undefined });
                  } catch {
                    onPatch({ value: text, expression: undefined });
                  }
                }
              }}
              rows={3}
              style={inputStyle}
            />
          </label>
        </>
      ) : null}
      {block.kind === "getVariable" ? (
        <label>
          Target
          <input value={block.target ?? ""} onChange={(event) => onPatch({ target: event.target.value })} style={inputStyle} />
        </label>
      ) : null}
      {block.kind === "log" ? (
        <>
          <label>
            Level
            <select value={block.level || "trace"} onChange={(event) => onPatch({ level: event.target.value })} style={inputStyle}>
              <option value="trace">trace</option>
              <option value="debug">debug</option>
              <option value="info">info</option>
              <option value="warn">warn</option>
              <option value="error">error</option>
              <option value="critical">critical</option>
            </select>
          </label>
          <label>
            Message
            <input value={block.message} onChange={(event) => onPatch({ message: event.target.value })} style={inputStyle} />
          </label>
        </>
      ) : null}
      {block.kind === "fetch" || block.kind === "network" || block.kind === "httpRequest" ? (
        <>
          <label>
            URL
            <input value={block.url} onChange={(event) => onPatch({ url: event.target.value })} style={inputStyle} />
          </label>
          <label>
            Method
            <input value={block.method ?? "GET"} onChange={(event) => onPatch({ method: event.target.value })} style={inputStyle} />
          </label>
          <label>
            Decode
            <select value={block.decode ?? "raw"} onChange={(event) => onPatch({ decode: event.target.value as "raw" | "json" })} style={inputStyle}>
              <option value="raw">raw</option>
              <option value="json">json</option>
            </select>
          </label>
          {(block.decode ?? "raw") === "json" ? (
            <label>
              Parse
              <input value={block.parse ?? block.filter ?? block.query ?? "."} onChange={(event) => onPatch({ parse: event.target.value })} style={inputStyle} />
            </label>
          ) : null}
          <label>
            Timeout ms
            <input type="number" value={block.timeoutMs ?? 30000} onChange={(event) => onPatch({ timeoutMs: Number(event.target.value) })} style={inputStyle} />
          </label>
          <label className="xblox-checkbox-label">
            <input type="checkbox" checked={block.followRedirects ?? true} onChange={(event) => onPatch({ followRedirects: event.target.checked })} />
            Follow redirects
          </label>
        </>
      ) : null}
      {block.kind === "Parse" || block.kind === "parse" ? (
        <>
          <label>
            Parser
            <input value={block.parser ?? "jq"} onChange={(event) => onPatch({ parser: event.target.value })} style={inputStyle} />
          </label>
          <label>
            Filter
            <input value={block.filter ?? block.query ?? "."} onChange={(event) => onPatch({ filter: event.target.value, query: undefined })} style={inputStyle} />
          </label>
          <label>
            Input variable
            <input value={block.input ?? block.from ?? "PREVIOUS"} onChange={(event) => onPatch({ input: event.target.value || undefined, from: undefined })} style={inputStyle} />
          </label>
        </>
      ) : null}
      {block.kind === "shell" || block.kind === "Shell" ? (
        <>
          <label>
            Mode
            <input value={block.mode ?? "shell"} onChange={(event) => onPatch({ mode: event.target.value })} style={inputStyle} />
          </label>
          <label>
            Shell
            <input value={block.shell ?? "auto"} onChange={(event) => onPatch({ shell: event.target.value })} style={inputStyle} />
          </label>
          <label>
            Command
            <VariableTextField value={block.command ?? block.line ?? ""} onChange={(value) => onPatch({ command: value, line: undefined })} rows={2} inputStyle={inputStyle} />
          </label>
          <label>
            CWD
            <VariableInputField value={block.cwd ?? ""} onChange={(value) => onPatch({ cwd: value || undefined })} inputStyle={inputStyle} />
          </label>
          <label>
            Timeout ms
            <input type="number" value={block.timeoutMs ?? 30000} onChange={(event) => onPatch({ timeoutMs: Number(event.target.value) })} style={inputStyle} />
          </label>
          <label className="xblox-checkbox-label">
            <input type="checkbox" checked={block.log ?? false} onChange={(event) => onPatch({ log: event.target.checked })} />
            Log output
          </label>
          <label>
            Stdout logger level
            <select value={block.stdout ?? "info"} onChange={(event) => onPatch({ stdout: event.target.value })} style={inputStyle}>
              <option value="trace">trace</option>
              <option value="debug">debug</option>
              <option value="info">info</option>
              <option value="warn">warn</option>
              <option value="error">error</option>
              <option value="off">off</option>
            </select>
          </label>
          <label>
            Stderr logger level
            <select value={block.stderr ?? "error"} onChange={(event) => onPatch({ stderr: event.target.value })} style={inputStyle}>
              <option value="trace">trace</option>
              <option value="debug">debug</option>
              <option value="info">info</option>
              <option value="warn">warn</option>
              <option value="error">error</option>
              <option value="off">off</option>
            </select>
          </label>
        </>
      ) : null}
      {selectedCommandPayload ? (
        <CommandPropsEditor
          value={selectedCommandPayload}
          inputStyle={inputStyle}
          hiddenFields={xbloxCommandHiddenFields}
          onChange={(command) =>
            block.kind === "command"
              ? onPatch({ id: command.id, command })
              : onPatch({ method: customCommandToRunScriptMethod(command) })
          }
        />
      ) : null}
      {"ms" in block ? (
        <label>
          Milliseconds
          <input type="number" value={block.ms} onChange={(event) => onPatch({ ms: Number(event.target.value) })} style={inputStyle} />
        </label>
      ) : null}
      {"variable" in block ? (
        <label>
          Variable
          <input value={block.variable} onChange={(event) => onPatch({ variable: event.target.value })} style={inputStyle} />
        </label>
      ) : null}
      {"expression" in block ? (
        <label>
          Expression
          <input value={block.expression} onChange={(event) => onPatch({ expression: event.target.value })} style={inputStyle} />
        </label>
      ) : null}
      <GenericNativeParamEditor block={block} params={nativeParams} onPatch={onPatch} />
    </div>
  );
}

export function XbloxPropertyPanel({
  block,
  onPatch,
  nativeParams,
  rootScopeDraft,
  rootScopeError,
  onRootScopeDraftChange,
  onApplyRootScopeDraft,
  canEditRootScope,
}: XbloxPropertyPanelProps) {
  return (
    <aside className="xblox-property-panel">
      <Tabs defaultValue="props" className="xblox-property-tabs">
        <TabsList className="xblox-property-tabs-list">
          <TabsTrigger className="xblox-property-tabs-trigger" value="props">
            Props
          </TabsTrigger>
          <TabsTrigger className="xblox-property-tabs-trigger" value="variables">
            Variables
          </TabsTrigger>
        </TabsList>
        <TabsContent className="xblox-property-tabs-content" value="props">
          <section className="xblox-property-section">
            <XbloxBlockPropertyEditor block={block} onPatch={onPatch} nativeParams={nativeParams} />
          </section>
        </TabsContent>
        <TabsContent className="xblox-property-tabs-content" value="variables">
          <Variables
            value={rootScopeDraft}
            error={rootScopeError}
            onChange={onRootScopeDraftChange}
            onApply={onApplyRootScopeDraft}
            canEdit={canEditRootScope}
            inputStyle={inputStyle}
          />
        </TabsContent>
      </Tabs>
    </aside>
  );
}
