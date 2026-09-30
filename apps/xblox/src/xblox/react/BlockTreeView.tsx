import { useCallback, useEffect, type KeyboardEvent, type MouseEvent, type Ref } from "react";
import { useDrop } from "react-dnd";
import { ArrowDown, ArrowLeft, ArrowUp, FolderOpen, Play, Save, Square, Trash2, type LucideIcon } from "lucide-react";

import type { BlockNode } from "@/schema/blocks-file";
import type { BlockRunState } from "./xblox-builder";
import { getChildren, type FolderFrame, type TreeRow, type XbloxBlock } from "./BlockModel";
import { DND_TYPE, type DragItem, type DropPlacement } from "./BlockUtils";
import { BlockRow } from "./BlockRow";
import { configureBlockTreeStore, useBlockTreeStore } from "./BlockTreeStore";

export type BlockTreeToolbarAction = {
  id: string;
  label: string;
  icon?: LucideIcon;
  shortcut?: string;
  disabled?: boolean;
};

export type BlockTreeViewProps = {
  rows: TreeRow[];
  currentRootListPath: string;
  selectedPaths: Set<string>;
  open: Set<string>;
  runState?: Record<string, BlockRunState>;
  selection: string | null;
  selectedBlock: XbloxBlock | null;
  currentLevelBlocks: BlockNode[];
  folderStack: FolderFrame[];
  running: boolean;
  treeRef: Ref<HTMLDivElement>;
  onSave?: () => void;
  saveDisabled: boolean;
  onRunChain?: (blocks: BlockNode[], listPath: string) => void | Promise<void>;
  onStopRun?: () => void;
  clipboardActions: BlockTreeToolbarAction[];
  onExecuteAction: (actionId: string) => void | Promise<void>;
  onRunSelected: () => void;
  onRunCurrentChain: () => void;
  onDeleteSelected: () => void;
  onMoveSelectedRows: (direction: -1 | 1) => void;
  onEnterSelectedBlock: () => void;
  onNavigateToParentBlock: () => void;
  onKeyDown: (event: KeyboardEvent<HTMLDivElement>) => void;
  onSelectRow: (path: string, event?: MouseEvent<HTMLDivElement>) => void;
  onActivateRow: (path: string) => void;
  onToggleRow: (path: string) => void;
  onDropLine: (targetPath: string, placement: DropPlacement, item: DragItem) => void;
  onCanDropOnRow: (targetPath: string, placement: DropPlacement, item: DragItem) => boolean;
  onCanDropInside: (targetPath: string, item: DragItem) => boolean;
  onPatchRow: (path: string, patch: Partial<Record<string, unknown>>) => void;
  onOpenRow: (path: string) => void;
  onContextMenuRow: (path: string, event: MouseEvent<HTMLDivElement>) => void;
  onDropIntoRoot: (item: DragItem) => void;
};

export function BlockTreeView({
  rows,
  currentRootListPath,
  selectedPaths,
  open,
  runState,
  selection,
  selectedBlock,
  currentLevelBlocks,
  folderStack,
  running,
  treeRef,
  onSave,
  saveDisabled,
  onRunChain,
  onStopRun,
  clipboardActions,
  onExecuteAction,
  onRunSelected,
  onRunCurrentChain,
  onDeleteSelected,
  onMoveSelectedRows,
  onEnterSelectedBlock,
  onNavigateToParentBlock,
  onKeyDown,
  onSelectRow,
  onActivateRow,
  onToggleRow,
  onDropLine,
  onCanDropOnRow,
  onCanDropInside,
  onPatchRow,
  onOpenRow,
  onContextMenuRow,
  onDropIntoRoot,
}: BlockTreeViewProps) {
  useEffect(() => {
    configureBlockTreeStore({
      rows,
      selectedPaths,
      open,
      runState,
      selectRow: onSelectRow,
      activateRow: onActivateRow,
      toggleRow: onToggleRow,
      dropLine: onDropLine,
      canDropOnRow: onCanDropOnRow,
      canDropInside: onCanDropInside,
      patchRow: onPatchRow,
      openRow: onOpenRow,
      contextMenuRow: onContextMenuRow,
      dropIntoRoot: onDropIntoRoot,
    });
  }, [
    onActivateRow,
    onCanDropOnRow,
    onCanDropInside,
    onContextMenuRow,
    onDropIntoRoot,
    onDropLine,
    onOpenRow,
    onPatchRow,
    onSelectRow,
    onToggleRow,
    open,
    rows,
    runState,
    selectedPaths,
  ]);

  return (
    <div className="xblox-tree-panel">
      {currentRootListPath ? <p className="xblox-folder-path">Current: {currentRootListPath}</p> : null}
      <div className="xblox-block-toolbar" aria-label="Selected block actions">
        {onSave ? (
          <div className="xblox-toolbar-group xblox-toolbar-group--file">
            <button type="button" onClick={onSave} disabled={saveDisabled} title="Save XBlox document (Ctrl+S)">
              <Save aria-hidden="true" />
              <span>Save</span>
            </button>
          </div>
        ) : null}
        <div className="xblox-toolbar-group xblox-toolbar-group--run">
          <button type="button" onClick={onRunSelected} disabled={!selection || !selectedBlock} title="Run selected block">
            <Play aria-hidden="true" />
            <span>Run</span>
          </button>
          <button type="button" onClick={onRunCurrentChain} disabled={!onRunChain || !currentLevelBlocks.length} title="Run current chain (Ctrl+R)">
            <Play aria-hidden="true" />
            <span>Run chain</span>
          </button>
          <button type="button" onClick={onStopRun} disabled={!running || !onStopRun} title="Stop running sequence">
            <Square aria-hidden="true" />
            <span>Stop</span>
          </button>
        </div>
        <div className="xblox-toolbar-group xblox-toolbar-group--clipboard">
          {clipboardActions.map((action) => {
            const Icon = action.icon;
            return (
              <button
                key={action.id}
                type="button"
                onClick={() => void onExecuteAction(action.id)}
                disabled={action.disabled}
                title={action.shortcut ? `${action.label} (${action.shortcut})` : action.label}
              >
                {Icon ? <Icon aria-hidden="true" /> : null}
                <span>{action.label}</span>
              </button>
            );
          })}
        </div>
        <div className="xblox-toolbar-group xblox-toolbar-group--edit">
          <button type="button" onClick={onDeleteSelected} disabled={!selectedPaths.size} title="Delete selected blocks">
            <Trash2 aria-hidden="true" />
            <span>Delete</span>
          </button>
          <button type="button" onClick={() => onMoveSelectedRows(-1)} disabled={!selectedPaths.size} title="Move selected blocks up">
            <ArrowUp aria-hidden="true" />
            <span>Move up</span>
          </button>
          <button type="button" onClick={() => onMoveSelectedRows(1)} disabled={!selectedPaths.size} title="Move selected blocks down">
            <ArrowDown aria-hidden="true" />
            <span>Move down</span>
          </button>
        </div>
        <div className="xblox-toolbar-group xblox-toolbar-group--nav">
          <button type="button" onClick={onEnterSelectedBlock} disabled={!selectedBlock || !getChildren(selectedBlock).length} title="Open selected block folder">
            <FolderOpen aria-hidden="true" />
            <span>Open</span>
          </button>
          <button type="button" onClick={onNavigateToParentBlock} disabled={!folderStack.length} title="Back to parent block">
            <ArrowLeft aria-hidden="true" />
            <span>Back</span>
          </button>
        </div>
      </div>
      <div ref={treeRef} role="tree" tabIndex={0} onKeyDown={onKeyDown} style={{ outline: "none" }} className="xblox-tree-scroll">
        {rows.map((row) => (
          <BlockRow key={row.path} row={row} />
        ))}
        <RootDropZone />
      </div>
    </div>
  );
}

function RootDropZone() {
  const dropIntoRoot = useBlockTreeDropIntoRoot();
  const [{ isOver }, dropRef] = useDrop(
    () => ({
      accept: DND_TYPE,
      drop: dropIntoRoot,
      collect: (monitor) => ({ isOver: monitor.isOver({ shallow: true }) }),
    }),
    [dropIntoRoot],
  );
  const ref = useCallback(
    (el: HTMLDivElement | null) => {
      dropRef(el);
    },
    [dropRef],
  );
  return (
    <div ref={ref} className={`xblox-root-drop ${isOver ? "is-over" : ""}`}>
      Drop here to append as a root block
    </div>
  );
}

function useBlockTreeDropIntoRoot() {
  return useBlockTreeStore((state) => state.dropIntoRoot);
}
