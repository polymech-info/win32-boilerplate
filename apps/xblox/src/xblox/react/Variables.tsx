import type { CSSProperties } from "react";

import { JSONTreeWalker } from "@/components/json/JSONTreeWalker";

export type VariablesProps = {
  value: string;
  error: string | null;
  onChange: (value: string) => void;
  onApply: () => void;
  canEdit: boolean;
  inputStyle?: CSSProperties;
};

const defaultInputStyle: CSSProperties = {
  width: "100%",
  border: "1px solid var(--xblox-button-border, #334155)",
  borderRadius: 6,
  background: "var(--xblox-input-bg, #020617)",
  color: "var(--xblox-text, #d7dee9)",
  padding: 8,
};

function readVariables(value: string): Record<string, unknown> | null {
  try {
    const parsed = value.trim() ? JSON.parse(value) : {};
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed as Record<string, unknown> : null;
  } catch {
    return null;
  }
}

function displayValue(value: unknown) {
  if (typeof value === "string") return value;
  if (value == null || typeof value === "number" || typeof value === "boolean") return String(value ?? "");
  return JSON.stringify(value, null, 2);
}

function parseJsonValue(value: unknown): unknown | null {
  if (value && typeof value === "object") return value;
  if (typeof value !== "string") return null;
  if (value.trim().length < 48) return null;
  try {
    const parsed = JSON.parse(value);
    return parsed && typeof parsed === "object" ? parsed : null;
  } catch {
    return null;
  }
}

function parseEditedValue(value: string) {
  const trimmed = value.trim();
  if (!trimmed) return "";
  try {
    return JSON.parse(trimmed);
  } catch {
    return value;
  }
}

export function Variables({ value, error, onChange, onApply, canEdit, inputStyle = defaultInputStyle }: VariablesProps) {
  const variables = readVariables(value);
  const entries = variables ? Object.entries(variables) : [];
  const updateVariable = (name: string, nextValue: string) => {
    if (!variables) return;
    onChange(JSON.stringify({ ...variables, [name]: parseEditedValue(nextValue) }, null, 2));
  };

  return (
    <section className="xblox-variables-panel xblox-root-scope-panel">
      <div className="xblox-root-scope-head">
        <h3>Root Scope</h3>
        {canEdit ? (
          <button type="button" onClick={onApply}>
            Apply
          </button>
        ) : null}
      </div>
      {variables ? (
        <div className="xblox-variables-list">
          <div className="xblox-variables-row xblox-variables-row--head">
            <span>Label</span>
            <span>Value</span>
          </div>
          {entries.length ? entries.map(([name, variableValue]) => {
            const jsonValue = parseJsonValue(variableValue);
            return jsonValue ? (
              <div key={name} className="xblox-variables-row xblox-variables-row--json">
                <span title={name}>{name}</span>
                <div className="xblox-variables-json">
                  <JSONTreeWalker data={jsonValue} rootLabel={name} compact />
                </div>
              </div>
            ) : (
              <label key={name} className="xblox-variables-row">
                <span title={name}>{name}</span>
                <input
                  value={displayValue(variableValue)}
                  readOnly={!canEdit}
                  onChange={(event) => updateVariable(name, event.target.value)}
                  onBlur={onApply}
                  spellCheck={false}
                  style={inputStyle}
                />
              </label>
            );
          }) : <p className="xblox-palette-empty">No variables.</p>}
        </div>
      ) : (
        <textarea
          value={value}
          readOnly={!canEdit}
          onChange={(event) => onChange(event.target.value)}
          onBlur={onApply}
          rows={8}
          spellCheck={false}
          style={inputStyle}
        />
      )}
      {error ? <p className="xblox-root-scope-error">{error}</p> : null}
    </section>
  );
}
