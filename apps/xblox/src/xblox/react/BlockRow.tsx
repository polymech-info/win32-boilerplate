import { useCallback, useRef, useState, type KeyboardEvent, type MouseEvent } from "react";
import { useDrag, useDrop } from "react-dnd";

import { blockLabel, getChildren, type TreeRow, type XbloxBlock } from "./BlockModel";
import { DND_TYPE, type DragItem, type DropPlacement } from "./BlockUtils";
import { useBlockTreeStore } from "./BlockTreeStore";

type BlockRowProps = {
  row: TreeRow;
};

export function BlockRow({ row }: BlockRowProps) {
  const selected = useBlockTreeStore((state) => state.selectedPaths.has(row.path));
  const expanded = useBlockTreeStore((state) => state.open.has(row.path));
  const runState = useBlockTreeStore((state) => state.runState);
  const selectRow = useBlockTreeStore((state) => state.selectRow);
  const activateRow = useBlockTreeStore((state) => state.activateRow);
  const toggleRow = useBlockTreeStore((state) => state.toggleRow);
  const dropLine = useBlockTreeStore((state) => state.dropLine);
  const canDropOnRow = useBlockTreeStore((state) => state.canDropOnRow);
  const canDropInside = useBlockTreeStore((state) => state.canDropInside);
  const patchRow = useBlockTreeStore((state) => state.patchRow);
  const openRow = useBlockTreeStore((state) => state.openRow);
  const contextMenuRow = useBlockTreeStore((state) => state.contextMenuRow);
  const status = runState?.[row.path] ?? ("id" in row.block && typeof row.block.id === "string" ? runState?.[row.block.id] : undefined);
  const enabled = (row.block as XbloxBlock & { enabled?: boolean }).enabled !== false;
  const hasChildren = getChildren(row.block).some((group) => group.blocks.length > 0);
  const [dropPlacement, setDropPlacement] = useState<DropPlacement | null>(null);
  const [dropValid, setDropValid] = useState(true);
  const rowRef = useRef<HTMLDivElement | null>(null);
  const [, dragRef] = useDrag(
    () => ({
      type: DND_TYPE,
      item: { source: "tree", path: row.path } satisfies DragItem,
    }),
    [row.path],
  );
  const [{ isOver }, dropRef] = useDrop(
    () => ({
      accept: DND_TYPE,
      hover: (item: DragItem, monitor) => {
        const clientOffset = monitor.getClientOffset();
        const node = rowRef.current;
        if (!clientOffset || !node) return;
        const rect = node.getBoundingClientRect();
        const relativeY = (clientOffset.y - rect.top) / Math.max(rect.height, 1);
        const canNest = canDropInside(row.path, item);
        if (canNest && relativeY > 0.28 && relativeY < 0.72) {
          setDropPlacement("inside");
          setDropValid(canDropOnRow(row.path, "inside", item));
          return;
        }
        const nextPlacement = relativeY < 0.5 ? "before" : "after";
        setDropPlacement(nextPlacement);
        setDropValid(canDropOnRow(row.path, nextPlacement, item));
      },
      drop: (item: DragItem) => {
        if (dropValid) dropLine(row.path, dropPlacement ?? "after", item);
        setDropPlacement(null);
        setDropValid(true);
      },
      collect: (monitor) => ({ isOver: monitor.isOver({ shallow: true }) }),
    }),
    [canDropInside, canDropOnRow, dropLine, dropPlacement, dropValid, row.path],
  );
  const ref = useCallback(
    (el: HTMLDivElement | null) => {
      rowRef.current = el;
      dragRef(dropRef(el));
    },
    [dragRef, dropRef],
  );
  const border =
    status?.status === "ok"
      ? "#22c55e"
      : status?.status === "error" || status?.status === "skipped"
        ? "#ef4444"
        : status?.status === "running"
          ? "var(--xblox-running-color, #38bdf8)"
          : selected
            ? "var(--xblox-selected-row-border, transparent)"
            : "transparent";
  const background =
    status?.status === "error" || status?.status === "skipped"
      ? "var(--xblox-error-row-bg, #2f0f16)"
      : isOver
        ? "var(--xblox-drop-row-bg, #082f49)"
        : selected
          ? "var(--xblox-selected-row-bg, #1e293b)"
          : "var(--xblox-row-bg, #020617)";
  return (
    <div
      ref={ref}
      role="treeitem"
      aria-selected={selected}
      aria-expanded={hasChildren ? expanded : undefined}
      onClick={(event) => selectRow(row.path, event)}
      onContextMenu={(event) => contextMenuRow(row.path, event)}
      onDoubleClick={() => openRow(row.path)}
      title={isOver && !dropValid ? "Cannot drop here" : isOver && dropPlacement === "inside" ? "Drop inside this block" : status?.error}
      className="xblox-tree-row"
      data-run-status={status?.status}
      data-enabled={String(enabled)}
      data-drop-placement={isOver ? (dropPlacement ?? "after") : undefined}
      data-drop-valid={isOver ? String(dropValid) : undefined}
      style={{
        paddingLeft: 3 + row.depth * 10,
        borderColor: border,
        background,
        filter: enabled ? undefined : "grayscale(0.45)",
        opacity: enabled ? undefined : 0.48,
      }}
    >
      <button
        type="button"
        onClick={(event) => {
          event.stopPropagation();
          toggleRow(row.path);
        }}
      >
        {hasChildren ? (expanded ? "v" : ">") : ""}
      </button>
      <span className="xblox-tree-row-kind">{row.block.kind}</span>
      <InlineBlockEditor
        block={row.block}
        path={row.path}
        onActivate={() => activateRow(row.path)}
        onPatch={(patch) => patchRow(row.path, patch)}
        onEnter={() => openRow(row.path)}
      />
      {status?.status && status.status !== "idle" ? (
        <small className={`xblox-status-pill xblox-status-pill--${status.status}`} style={{ marginLeft: "auto" }}>
          {status.status}
        </small>
      ) : null}
    </div>
  );
}

function InlineBlockEditor({
  block,
  path,
  onActivate,
  onPatch,
  onEnter,
}: {
  block: XbloxBlock;
  path: string;
  onActivate: () => void;
  onPatch: (patch: Partial<Record<string, unknown>>) => void;
  onEnter: () => void;
}) {
  const stopTreeMouse = (event: MouseEvent<HTMLInputElement | HTMLSelectElement>) => {
    event.stopPropagation();
  };
  const handleFocus = () => {
    onActivate();
  };
  const stopTreeKeys = (event: KeyboardEvent<HTMLInputElement | HTMLSelectElement>) => {
    event.stopPropagation();
    if (event.key === "Enter") {
      event.preventDefault();
      event.currentTarget.blur();
      onEnter();
    }
  };
  const inputClass = "xblox-inline-input";

  if (block.kind === "if" || block.kind === "while") {
    return (
      <label className="xblox-inline-expr">
        <span>{block.kind === "if" ? "condition" : "while"}</span>
        <input
          data-xblox-inline-path={path}
          className={inputClass}
          value={block.condition}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ condition: event.target.value })}
        />
      </label>
    );
  }

  if (block.kind === "switch") {
    return (
      <label className="xblox-inline-expr">
        <span>switch</span>
        <input
          data-xblox-inline-path={path}
          className={inputClass}
          value={block.variable}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ variable: event.target.value })}
        />
      </label>
    );
  }

  if (block.kind === "setVariable") {
    return (
      <span className="xblox-inline-expr">
        <span>set</span>
        <input
          data-xblox-inline-path={path}
          className="xblox-inline-input small"
          value={block.name}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ name: event.target.value })}
          title="Variable name"
        />
        <input
          data-xblox-inline-path={path}
          className={inputClass}
          value={block.expression ?? (typeof block.value === "string" || typeof block.value === "number" || typeof block.value === "boolean" ? String(block.value) : "")}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ expression: event.target.value })}
          title="Expression"
        />
      </span>
    );
  }

  if (block.kind === "getVariable") {
    return (
      <span className="xblox-inline-expr">
        <span>get</span>
        <input
          data-xblox-inline-path={path}
          className="xblox-inline-input small"
          value={block.name}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ name: event.target.value })}
          title="Variable name"
        />
        <input
          data-xblox-inline-path={path}
          className={inputClass}
          value={block.target ?? ""}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ target: event.target.value })}
          title="Optional target variable"
        />
      </span>
    );
  }

  if (block.kind === "log") {
    return (
      <span className="xblox-inline-expr">
        <span>log</span>
        <select
          data-xblox-inline-path={path}
          className="xblox-inline-select"
          value={block.level || "trace"}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ level: event.target.value })}
        >
          <option value="trace">trace</option>
          <option value="debug">debug</option>
          <option value="info">info</option>
          <option value="warn">warn</option>
          <option value="error">error</option>
          <option value="critical">critical</option>
        </select>
        <input
          data-xblox-inline-path={path}
          className={inputClass}
          value={block.message}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ message: event.target.value })}
          title="Message or expression"
        />
      </span>
    );
  }

  if (block.kind === "case") {
    return (
      <span className="xblox-inline-expr">
        <span>case</span>
        <select
          data-xblox-inline-path={path}
          className="xblox-inline-select"
          value={block.comparator}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ comparator: event.target.value })}
        >
          <option value="===">===</option>
          <option value="!==">!==</option>
          <option value=">">&gt;</option>
          <option value=">=">&gt;=</option>
          <option value="<">&lt;</option>
          <option value="<=">&lt;=</option>
        </select>
        <input
          data-xblox-inline-path={path}
          className={inputClass}
          value={block.expression}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ expression: event.target.value })}
        />
      </span>
    );
  }

  if (block.kind === "for") {
    return (
      <span className="xblox-inline-expr">
        <span>for</span>
        <input
          data-xblox-inline-path={path}
          className="xblox-inline-input small"
          value={block.initial}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ initial: event.target.value })}
          title="Initial"
        />
        <input
          data-xblox-inline-path={path}
          className="xblox-inline-input tiny"
          value={block.comparator}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ comparator: event.target.value })}
          title="Comparator"
        />
        <input
          data-xblox-inline-path={path}
          className="xblox-inline-input small"
          value={block.final}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ final: event.target.value })}
          title="Final"
        />
        <input
          data-xblox-inline-path={path}
          className="xblox-inline-input tiny"
          value={block.modifier}
          onMouseDown={stopTreeMouse}
          onClick={stopTreeMouse}
          onFocus={handleFocus}
          onKeyDown={stopTreeKeys}
          onChange={(event) => onPatch({ modifier: event.target.value })}
          title="Modifier"
        />
      </span>
    );
  }

  return <span className="xblox-tree-row-label">{blockLabel(block)}</span>;
}
