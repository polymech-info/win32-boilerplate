import { useEffect, useMemo, useRef, useState } from "react";
import { createPortal } from "react-dom";
import type { CommandVariableOption } from "./types";

const DEFAULT_VARIABLES: CommandVariableOption[] = [
  { name: "CURRENT_FILE", group: "Current file", description: "Current file absolute path from Explorer selection or the open preview." },
  { name: "CURRENT_FILE_NAME", group: "Current file", description: "Current file name including extension." },
  { name: "CURRENT_PATH", group: "Current file", description: "Current Explorer folder, or current file parent folder when a file is selected." },
  { name: "CURRENT_SELECTION", group: "Current file", description: "Whitespace-separated current Explorer selection list; entries are files or folders." },
  { name: "CWD", group: "Process", description: "Current command working directory." },
  { name: "PATH_SEP", group: "Process", description: "Native path separator for this platform." },
  { name: "PATH_LIST_SEP", group: "Process", description: "Native delimiter for lists of paths." },
  { name: "SRC_FILE", group: "Source", description: "Selected source file path." },
  { name: "SRC_DIR", group: "Source", description: "Selected source parent directory." },
  { name: "SRC_NAME", group: "Source", description: "Selected source filename without extension." },
  { name: "SRC_EXT", group: "Source", description: "Selected source extension." },
  { name: "SRC_FILE_EXT", group: "Source", description: "Selected source extension including the file suffix form used by pm-image templates." },
  { name: "YYYY", group: "Date / time", description: "Current four-digit year." },
  { name: "MM", group: "Date / time", description: "Current month number." },
  { name: "DD", group: "Date / time", description: "Current day of month." },
  { name: "HH", group: "Date / time", description: "Current hour." },
  { name: "SS", group: "Date / time", description: "Current seconds." },
  { name: "KNOWNFOLDER:Home", group: "Known folders - Portable", description: "User home/profile folder." },
  { name: "KNOWNFOLDER:Config", group: "Known folders - Portable", description: "User configuration folder." },
  { name: "KNOWNFOLDER:Data", group: "Known folders - Portable", description: "User data folder." },
  { name: "KNOWNFOLDER:Cache", group: "Known folders - Portable", description: "User cache folder." },
  { name: "KNOWNFOLDER:Temp", group: "Known folders - Portable", description: "Temporary files folder." },
  { name: "KNOWNFOLDER:Profile", group: "Known folders - User", description: "User profile folder." },
  { name: "KNOWNFOLDER:Desktop", group: "Known folders - User", description: "Current user's Desktop folder." },
  { name: "KNOWNFOLDER:Documents", group: "Known folders - User", description: "Current user's Documents folder." },
  { name: "KNOWNFOLDER:Downloads", group: "Known folders - User", description: "Current user's Downloads folder." },
  { name: "KNOWNFOLDER:Pictures", group: "Known folders - User", description: "Current user's Pictures folder." },
  { name: "KNOWNFOLDER:Local_App_Data", group: "Known folders - App data", description: "Current user's local application data folder." },
  { name: "KNOWNFOLDER:Roaming_App_Data", group: "Known folders - App data", description: "Current user's roaming application data folder." },
  { name: "KNOWNFOLDER:Program_Data", group: "Known folders - App data", description: "Shared program data folder." },
  { name: "KNOWNFOLDER:Public", group: "Known folders - Public", description: "Public user profile folder." },
  { name: "KNOWNFOLDER:Public_Documents", group: "Known folders - Public", description: "Public Documents folder." },
  { name: "KNOWNFOLDER:Windows", group: "Known folders - System", description: "Windows installation folder." },
  { name: "KNOWNFOLDER:Program_Files", group: "Known folders - System", description: "Program Files folder." },
  { name: "ENV:PATH", group: "Environment", description: "Environment variable lookup. Replace PATH with any variable name." },
];

type VariableBuilderButtonProps = {
  value: string;
  variables?: CommandVariableOption[];
  onChange: (value: string) => void;
  title?: string;
};

type VariableInputFieldProps = {
  value: string;
  variables?: CommandVariableOption[];
  onChange: (value: string) => void;
  placeholder?: string;
  title?: string;
};

type VariableTextFieldProps = VariableInputFieldProps & {
  rows?: number;
  className?: string;
};

function variableToken(name: string) {
  return "${" + name + "}";
}

function currentVariablePrefix(value: string, caret: number) {
  const beforeCaret = value.slice(0, caret);
  const match = beforeCaret.match(/\$\{?([A-Za-z0-9_:]*)$/);
  if (!match) return null;
  const tokenStart = beforeCaret.length - match[0].length;
  return { query: match[1].toUpperCase(), tokenStart };
}

function normalizedVariables(variables?: CommandVariableOption[]) {
  const byName = new Map<string, CommandVariableOption>();
  for (const variable of variables?.length ? variables : DEFAULT_VARIABLES) {
    const name = variable.name.trim();
    if (!name) continue;
    byName.set(name, {
      name,
      description: variable.description || "",
      group: variable.group || "Other",
    });
  }
  return Array.from(byName.values());
}

function groupedVariables(variables: CommandVariableOption[]) {
  return variables.reduce<Record<string, CommandVariableOption[]>>(
    (groups, variable) => {
      const group = variable.group || "Other";
      groups[group] = groups[group] || [];
      groups[group].push(variable);
      return groups;
    },
    {}
  );
}

export function VariableBuilderButton({ value, variables, onChange, title = "Variable Builder" }: VariableBuilderButtonProps) {
  const [open, setOpen] = useState(false);
  const [draft, setDraft] = useState(value);
  const [caret, setCaret] = useState(value.length);
  const [search, setSearch] = useState("");
  const [highlightedIndex, setHighlightedIndex] = useState(0);
  const searchRef = useRef<HTMLInputElement | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const choiceRefs = useRef<Record<string, HTMLButtonElement | null>>({});
  const availableVariables = useMemo(() => normalizedVariables(variables), [variables]);
  const searchQuery = search.trim().toLowerCase();
  const filteredVariables = useMemo(() => {
    if (!searchQuery) return availableVariables;
    return availableVariables.filter((variable) => {
      const haystack = `${variable.name} ${variable.group || ""} ${variable.description || ""}`.toLowerCase();
      return haystack.includes(searchQuery);
    });
  }, [availableVariables, searchQuery]);
  const variablesByGroup = useMemo(() => groupedVariables(filteredVariables), [filteredVariables]);
  const activePrefix = currentVariablePrefix(draft, caret);
  const suggestions = useMemo(() => {
    if (!activePrefix) return [];
    return availableVariables.filter((variable) => variable.name.toUpperCase().includes(activePrefix.query)).slice(0, 8);
  }, [activePrefix, availableVariables]);

  useEffect(() => {
    if (open) {
      setDraft(value);
      setCaret(value.length);
      setSearch("");
      setHighlightedIndex(0);
      window.setTimeout(() => {
        searchRef.current?.focus();
      }, 0);
    }
  }, [open, value]);

  useEffect(() => {
    setHighlightedIndex(0);
  }, [searchQuery, availableVariables]);

  useEffect(() => {
    if (!open) return;
    const active = filteredVariables[highlightedIndex];
    if (!active) return;
    choiceRefs.current[active.name]?.scrollIntoView({ block: "nearest" });
  }, [filteredVariables, highlightedIndex, open]);

  useEffect(() => {
    if (!open) return;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape")
        setOpen(false);
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [open]);

  function syncCaret() {
    const position = textareaRef.current?.selectionStart ?? draft.length;
    setCaret(position);
  }

  function insertVariable(name: string) {
    const token = variableToken(name);
    const prefix = currentVariablePrefix(draft, caret);
    const start = prefix?.tokenStart ?? caret;
    const nextDraft = draft.slice(0, start) + token + draft.slice(caret);
    const nextCaret = start + token.length;
    setDraft(nextDraft);
    setCaret(nextCaret);
    window.setTimeout(() => {
      textareaRef.current?.focus();
      textareaRef.current?.setSelectionRange(nextCaret, nextCaret);
    }, 0);
  }

  function applyDraft() {
    onChange(draft);
    setOpen(false);
  }

  function insertHighlightedVariable() {
    const active = filteredVariables[highlightedIndex];
    if (active)
      insertVariable(active.name);
  }

  function onSearchKeyDown(e: React.KeyboardEvent<HTMLInputElement>) {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setHighlightedIndex((index) => filteredVariables.length ? (index + 1) % filteredVariables.length : 0);
      return;
    }
    if (e.key === "ArrowUp") {
      e.preventDefault();
      setHighlightedIndex((index) => filteredVariables.length ? (index - 1 + filteredVariables.length) % filteredVariables.length : 0);
      return;
    }
    if (e.key === "Home") {
      e.preventDefault();
      setHighlightedIndex(0);
      return;
    }
    if (e.key === "End") {
      e.preventDefault();
      setHighlightedIndex(Math.max(0, filteredVariables.length - 1));
      return;
    }
    if (e.key === "Enter") {
      e.preventDefault();
      insertHighlightedVariable();
      return;
    }
    if (e.key === "Escape" && search) {
      e.preventDefault();
      setSearch("");
    }
  }

  const panel = open ? createPortal(
    <div className="pm-variable-builder-overlay" role="presentation" onMouseDown={() => setOpen(false)}>
      <div className="pm-variable-builder-popover" role="dialog" aria-modal="true" aria-label={title} onMouseDown={(e) => e.stopPropagation()}>
        <div className="pm-variable-builder">
          <aside className="pm-variable-list pm-scroll" aria-label="Built-in variables">
            <div className="pm-variable-list-head">
              <strong>Built-in Variables</strong>
              <span>{filteredVariables.length} / {availableVariables.length}</span>
            </div>
            <input
              ref={searchRef}
              className="pm-variable-search"
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              onKeyDown={onSearchKeyDown}
              placeholder="Search variables, env, folders..."
              aria-label="Search variables"
              aria-activedescendant={filteredVariables[highlightedIndex] ? `pm-variable-choice-${filteredVariables[highlightedIndex].name}` : undefined}
            />
            {filteredVariables.length ? Object.entries(variablesByGroup).map(([group, variables]) => (
              <section key={group} className="pm-variable-group">
                <h4>{group}</h4>
                {variables.map((variable) => (
                  <button
                    key={variable.name}
                    id={`pm-variable-choice-${variable.name}`}
                    ref={(node) => {
                      choiceRefs.current[variable.name] = node;
                    }}
                    className={`pm-variable-choice ${filteredVariables[highlightedIndex]?.name === variable.name ? "active" : ""}`}
                    type="button"
                    onMouseEnter={() => setHighlightedIndex(filteredVariables.findIndex((item) => item.name === variable.name))}
                    onClick={() => insertVariable(variable.name)}
                  >
                    <code>{variableToken(variable.name)}</code>
                    <span>{variable.description}</span>
                  </button>
                ))}
              </section>
            )) : (
              <div className="pm-variable-empty">No matching variables</div>
            )}
          </aside>
          <section className="pm-variable-compose">
            <label htmlFor="pm-variable-builder-text">Value</label>
            <textarea
              id="pm-variable-builder-text"
              ref={textareaRef}
              className="pm-scroll"
              rows={3}
              value={draft}
              onChange={(e) => {
                setDraft(e.target.value);
                setCaret(e.target.selectionStart);
              }}
              onClick={syncCaret}
              onKeyUp={syncCaret}
              onSelect={syncCaret}
              onKeyDown={(e) => {
                if ((e.key === "Tab" || e.key === "Enter") && suggestions[0]) {
                  e.preventDefault();
                  insertVariable(suggestions[0].name);
                }
              }}
              placeholder="Type $ for variable completion..."
            />
            {activePrefix ? (
              <div className="pm-variable-suggestions pm-scroll" role="listbox" aria-label="Variable suggestions">
                {suggestions.length ? (
                  suggestions.map((variable) => (
                    <button key={variable.name} type="button" onClick={() => insertVariable(variable.name)}>
                      <code>{variableToken(variable.name)}</code>
                      <span>{variable.description}</span>
                    </button>
                  ))
                ) : (
                  <span>No variable matches</span>
                )}
              </div>
            ) : null}
            <div className="pm-variable-builder-actions">
              <button className="btn" type="button" onClick={() => setDraft("")}>Clear</button>
              <button className="btn" type="button" onClick={() => setOpen(false)}>Cancel</button>
              <button className="btn" type="button" onClick={applyDraft}>Apply</button>
            </div>
          </section>
        </div>
      </div>
    </div>,
    document.body
  ) : null;

  return (
    <>
      <button className="pm-variable-builder-trigger" type="button" title={title} aria-label={title} onClick={() => setOpen(true)}>
        ✦
      </button>
      {panel}
    </>
  );
}

export function VariableInputField({ value, variables, onChange, placeholder, title }: VariableInputFieldProps) {
  return (
    <div className="pm-variable-control">
      <input value={value} onChange={(e) => onChange(e.target.value)} placeholder={placeholder} title={title} />
      <VariableBuilderButton value={value} variables={variables} onChange={onChange} />
    </div>
  );
}

export function VariableTextField({
  value,
  variables,
  onChange,
  rows = 1,
  placeholder,
  title,
  className = "pm-custom-args-textarea pm-scroll",
}: VariableTextFieldProps) {
  return (
    <div className="pm-variable-control">
      <textarea
        className={className}
        rows={rows}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        title={title}
      />
      <VariableBuilderButton value={value} variables={variables} onChange={onChange} />
    </div>
  );
}
