/** @jsxImportSource preact */
import { useMemo, useRef, useState } from "preact/hooks";
import type { ReactElement } from "react";

export type ModelStringOption = string | {
  id: string;
  label?: string;
};

export type ModelStringTypeaheadProps = {
  id?: string;
  value: string;
  options: ModelStringOption[];
  onChange: (value: string) => void;
  disabled?: boolean;
  placeholder?: string;
  title?: string;
  emptyLabel?: string;
  maxRows?: number;
  className?: string;
  inputClassName?: string;
  menuClassName?: string;
  itemClassName?: string;
};

function normalizeOption(option: ModelStringOption): { id: string; label: string } {
  if (typeof option === "string") return { id: option, label: option };
  return { id: option.id, label: option.label || option.id };
}

export function ModelStringTypeahead({
  id,
  value,
  options,
  onChange,
  disabled,
  placeholder,
  title,
  emptyLabel = "No suggestions",
  maxRows = 30,
  className = "pm-typeahead",
  inputClassName,
  menuClassName = "pm-typeahead-menu",
  itemClassName = "pm-typeahead-item",
}: ModelStringTypeaheadProps): ReactElement {
  const [open, setOpen] = useState(false);
  const closeTimer = useRef<number | null>(null);
  const query = value.trim().toLowerCase();
  const filtered = useMemo(() => {
    const seen = new Set<string>();
    const rows = options
      .map(normalizeOption)
      .filter((option) => {
        if (!option.id || seen.has(option.id)) return false;
        seen.add(option.id);
        return !query || option.id.toLowerCase().includes(query) || option.label.toLowerCase().includes(query);
      });
    return rows.slice(0, maxRows);
  }, [options, query, maxRows]);

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
    <div className={className} onFocus={cancelClose} onBlur={scheduleClose}>
      <input
        id={id}
        type="text"
        value={value}
        disabled={disabled}
        placeholder={placeholder}
        title={title}
        className={inputClassName}
        onFocus={() => setOpen(true)}
        onChange={(event) => {
          onChange((event.currentTarget as HTMLInputElement).value);
          setOpen(true);
        }}
      />
      {open && !disabled ? (
        <div className={menuClassName}>
          {filtered.length ? (
            filtered.map((option) => (
              <button
                key={`${option.id}:${option.label}`}
                className={itemClassName}
                type="button"
                onMouseDown={(event) => {
                  event.preventDefault();
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
            <div className="pm-typeahead-empty">{emptyLabel}</div>
          )}
        </div>
      ) : null}
    </div>
  ) as unknown as ReactElement;
}
