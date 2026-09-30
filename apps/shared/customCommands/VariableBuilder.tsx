import { useMemo, useState, type CSSProperties } from "react";
import type { CommandVariableOption } from "./types";

const DEFAULT_VARIABLES: CommandVariableOption[] = [
  { name: "CURRENT_FILE", group: "Current file", description: "Current file absolute path." },
  { name: "CURRENT_FILE_NAME", group: "Current file", description: "Current file name including extension." },
  { name: "CURRENT_PATH", group: "Current file", description: "Current Explorer folder or parent folder." },
  { name: "CURRENT_SELECTION", group: "Current file", description: "Whitespace-separated current Explorer selection." },
  { name: "CWD", group: "Process", description: "Current command working directory." },
  { name: "PATH_SEP", group: "Process", description: "Native path separator." },
  { name: "PATH_LIST_SEP", group: "Process", description: "Native path-list separator." },
  { name: "SRC_FILE", group: "Source", description: "Selected source file path." },
  { name: "SRC_DIR", group: "Source", description: "Selected source parent directory." },
  { name: "SRC_NAME", group: "Source", description: "Selected source filename without extension." },
  { name: "SRC_EXT", group: "Source", description: "Selected source extension." },
  { name: "KNOWNFOLDER:Home", group: "Known folders", description: "User home/profile folder." },
  { name: "KNOWNFOLDER:Config", group: "Known folders", description: "User configuration folder." },
  { name: "KNOWNFOLDER:Data", group: "Known folders", description: "User data folder." },
  { name: "KNOWNFOLDER:Temp", group: "Known folders", description: "Temporary files folder." },
  { name: "KNOWNFOLDER:Desktop", group: "Known folders", description: "Current user's Desktop folder." },
  { name: "KNOWNFOLDER:Documents", group: "Known folders", description: "Current user's Documents folder." },
  { name: "KNOWNFOLDER:Downloads", group: "Known folders", description: "Current user's Downloads folder." },
  { name: "KNOWNFOLDER:Pictures", group: "Known folders", description: "Current user's Pictures folder." },
  { name: "ENV:PATH", group: "Environment", description: "Environment lookup. Replace PATH with a variable name." },
];

type VariableFieldProps = {
  value: string;
  variables?: CommandVariableOption[];
  onChange: (value: string) => void;
  placeholder?: string;
  title?: string;
  inputStyle?: CSSProperties;
};

type VariableTextFieldProps = VariableFieldProps & {
  rows?: number;
  className?: string;
};

function token(name: string) {
  return "${" + name + "}";
}

function normalizeVariables(variables?: CommandVariableOption[]) {
  return variables?.length ? variables : DEFAULT_VARIABLES;
}

function insertAtEnd(value: string, variable: string) {
  const suffix = value && !/\s$/.test(value) ? " " : "";
  return `${value}${suffix}${token(variable)}`;
}

function VariableButton({ value, variables, onChange }: VariableFieldProps) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState("");
  const options = useMemo(() => normalizeVariables(variables), [variables]);
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return options;
    return options.filter((option) => `${option.name} ${option.group || ""} ${option.description}`.toLowerCase().includes(q));
  }, [options, query]);
  return (
    <span className="pm-variable-inline">
      <button className="pm-variable-builder-trigger" type="button" title="Variable Builder" onClick={() => setOpen((current) => !current)}>
        {"${}"}
      </button>
      {open ? (
        <span className="pm-variable-inline-menu">
          <input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Search variables..." />
          <span className="pm-variable-inline-list">
            {filtered.map((option) => (
              <button
                key={option.name}
                type="button"
                title={option.description}
                onClick={() => {
                  onChange(insertAtEnd(value, option.name));
                  setOpen(false);
                }}
              >
                <code>{token(option.name)}</code>
                <small>{option.group}</small>
              </button>
            ))}
          </span>
        </span>
      ) : null}
    </span>
  );
}

export function VariableInputField({ value, variables, onChange, placeholder, title, inputStyle }: VariableFieldProps) {
  return (
    <span className="pm-variable-control">
      <input value={value} onChange={(event) => onChange(event.target.value)} placeholder={placeholder} title={title} style={inputStyle} />
      <VariableButton value={value} variables={variables} onChange={onChange} />
    </span>
  );
}

export function VariableTextField({ value, variables, onChange, rows = 1, placeholder, title, inputStyle, className }: VariableTextFieldProps) {
  return (
    <span className="pm-variable-control">
      <textarea
        className={className}
        rows={rows}
        value={value}
        onChange={(event) => onChange(event.target.value)}
        placeholder={placeholder}
        title={title}
        style={inputStyle}
      />
      <VariableButton value={value} variables={variables} onChange={onChange} />
    </span>
  );
}
