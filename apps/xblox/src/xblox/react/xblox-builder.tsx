import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent, type MouseEvent, type ReactNode } from "react";
import { DndProvider } from "react-dnd";
import { HTML5Backend } from "react-dnd-html5-backend";
import { ClipboardPaste, Copy, FolderOpen, Play, Trash2 } from "lucide-react";

import { COPY_ACTION_ID, PASTE_ACTION_ID } from "@pm/shared/actions/default-actions";
import { actionStore } from "@pm/shared/actions/store";
import type { CustomCommandItem } from "@pm/shared/customCommands/types";
import type { BlockNode, SwitchItemNode } from "@/schema/blocks-file";
import {
  canInsertIntoList,
  createDefaultBlock,
  deleteSelectedFromList,
  flatten,
  flattenGroup,
  getBlockAtPath,
  getChildren,
  getListAtPath,
  hasSelectedAncestor,
  insertIntoList,
  isSwitchItem,
  itemPath,
  moveSelectedAcrossLists,
  moveSelectedIntoPreviousSibling,
  moveSelectedOutOfList,
  parentPathForList,
  parseItemPath,
  primaryChildKey,
  removeAtPath,
  updateAtPath,
  type BlockPaletteKind,
  type FolderFrame,
  type XbloxBlock,
} from "./BlockModel";
import {
  cloneBlock,
  parseClipboardText,
  selectedClipboardBlocks,
  serializeClipboard,
  type BlockClipboard,
  type ContextMenuState,
  type DragItem,
  type DropPlacement,
} from "./BlockUtils";
import { BlockTreeView } from "./BlockTreeView";
import { XbloxPalette } from "./xblox-palette";
import { XbloxBlockPropertyEditor, XbloxPropertyPanel, type NativeBlockParam } from "./xblox-property-panel";

export type { BlockPaletteKind, XbloxBlock } from "./BlockModel";
export type BlockRunStatus = "idle" | "running" | "ok" | "error" | "skipped";
export type BlockRunState = {
  status: BlockRunStatus;
  error?: string;
};

export type CommandPaletteBlock = {
  id: string;
  label: string;
  description?: string;
  block: BlockNode;
};

export type NativePaletteBlock = {
  kind: string;
  label?: string;
  description?: string;
  group?: string;
  params?: NativeBlockParam[];
  block: BlockNode;
};

export interface XbloxBuilderProps {
  value: BlockNode[];
  onChange: (value: BlockNode[]) => void;
  rootScope?: Record<string, unknown>;
  onRootScopeChange?: (value: Record<string, unknown>) => void;
  runState?: Record<string, BlockRunState>;
  onRunBlock?: (block: BlockNode | SwitchItemNode, path: string) => void | Promise<void>;
  onRunChain?: (blocks: BlockNode[], listPath: string) => void | Promise<void>;
  onStopRun?: () => void;
  running?: boolean;
  selectedPath?: string;
  onSelectionChange?: (path: string | null, block: BlockNode | SwitchItemNode | null) => void;
  onSave?: () => void;
  saveDisabled?: boolean;
  theme?: "dark" | "light";
  onToggleTheme?: () => void;
  palette?: BlockPaletteKind[];
  nativePalette?: NativePaletteBlock[];
  commandPalette?: CommandPaletteBlock[];
  className?: string;
}

const PANEL_WIDTHS_STORAGE_KEY = "pm.xblox.panelWidths.v1";
const PALETTE_MIN_WIDTH = 180;
const PALETTE_MAX_WIDTH = 550;
const PROPS_MIN_WIDTH = 240;
const PROPS_MAX_WIDTH = 730;
const BUILDER_CENTER_MIN_WIDTH = 180;
const BUILDER_RESIZE_CHROME_WIDTH = 16;

const defaultPalette: BlockPaletteKind[] = [
  "command",
  "setVariable",
  "getVariable",
  "log",
  "runScript",
  "wait",
  "if",
  "for",
  "while",
  "switch",
  "case",
  "switchDefault",
  "break",
];

type PanelWidths = {
  palette: number;
  props: number;
};

function clamp(n: number, min: number, max: number) {
  return Math.max(min, Math.min(max, n));
}

function constrainPanelWidths(widths: PanelWidths, builderWidth: number, preserveSide?: keyof PanelWidths): PanelWidths {
  const availableForPanels = Math.max(PALETTE_MIN_WIDTH + PROPS_MIN_WIDTH, builderWidth - BUILDER_CENTER_MIN_WIDTH - BUILDER_RESIZE_CHROME_WIDTH);
  let palette = clamp(widths.palette, PALETTE_MIN_WIDTH, PALETTE_MAX_WIDTH);
  let props = clamp(widths.props, PROPS_MIN_WIDTH, PROPS_MAX_WIDTH);

  if (palette + props <= availableForPanels) return { palette, props };

  if (preserveSide === "props") {
    props = clamp(props, PROPS_MIN_WIDTH, Math.min(PROPS_MAX_WIDTH, availableForPanels - PALETTE_MIN_WIDTH));
    palette = clamp(palette, PALETTE_MIN_WIDTH, availableForPanels - props);
    return { palette, props };
  }

  if (preserveSide === "palette") {
    palette = clamp(palette, PALETTE_MIN_WIDTH, Math.min(PALETTE_MAX_WIDTH, availableForPanels - PROPS_MIN_WIDTH));
    props = clamp(props, PROPS_MIN_WIDTH, availableForPanels - palette);
    return { palette, props };
  }

  const paletteExcess = Math.max(0, palette - PALETTE_MIN_WIDTH);
  const propsExcess = Math.max(0, props - PROPS_MIN_WIDTH);
  const overflow = palette + props - availableForPanels;
  const shrinkable = paletteExcess + propsExcess;
  if (shrinkable <= 0) return { palette, props };

  palette -= overflow * (paletteExcess / shrinkable);
  props -= overflow * (propsExcess / shrinkable);
  return {
    palette: clamp(Math.round(palette), PALETTE_MIN_WIDTH, PALETTE_MAX_WIDTH),
    props: clamp(Math.round(props), PROPS_MIN_WIDTH, PROPS_MAX_WIDTH),
  };
}

function readPanelWidths(): PanelWidths {
  try {
    const raw = window.localStorage.getItem(PANEL_WIDTHS_STORAGE_KEY);
    const parsed = raw ? JSON.parse(raw) : null;
    if (parsed && typeof parsed === "object") {
      return {
        palette: clamp(Number(parsed.palette) || 220, PALETTE_MIN_WIDTH, PALETTE_MAX_WIDTH),
        props: clamp(Number(parsed.props) || 320, PROPS_MIN_WIDTH, PROPS_MAX_WIDTH),
      };
    }
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
  return { palette: 210, props: 280 };
}

function persistPanelWidths(widths: PanelWidths) {
  try {
    window.localStorage.setItem(PANEL_WIDTHS_STORAGE_KEY, JSON.stringify(widths));
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
}

const styles = {
  shell: {
    display: "grid",
    gridTemplateColumns: "var(--xblox-builder-columns, 220px minmax(320px, 1fr) 320px)",
    gap: "var(--xblox-builder-gap, 12px)",
    minHeight: 480,
    color: "var(--xblox-text, #d7dee9)",
  },
  panel: {
    border: "1px solid var(--xblox-button-border, #334155)",
    borderRadius: "var(--xblox-panel-radius, 12px)",
    background: "var(--xblox-panel-bg, #0f172a)",
    padding: "var(--xblox-panel-pad, 12px)",
    minHeight: 0,
  },
};

export function XbloxDndProvider({ children }: { children: ReactNode }) {
  return <DndProvider backend={HTML5Backend}>{children}</DndProvider>;
}

export function XbloxBuilder({
  value,
  onChange,
  rootScope = {},
  onRootScopeChange,
  runState,
  onRunBlock,
  onRunChain,
  onStopRun,
  running = false,
  selectedPath,
  onSelectionChange,
  onSave,
  saveDisabled = false,
  theme = "dark",
  onToggleTheme,
  palette = defaultPalette,
  nativePalette = [],
  commandPalette = [],
  className,
}: XbloxBuilderProps) {
  const [internalSelectedPath, setInternalSelectedPath] = useState<string | null>(value.length ? "0" : null);
  const [selectedPaths, setSelectedPaths] = useState<Set<string>>(() => new Set(value.length ? ["0"] : []));
  const [anchorPath, setAnchorPath] = useState<string | null>(value.length ? "0" : null);
  const [open, setOpen] = useState<Set<string>>(() => new Set(["1", "1/consequent/1", "1/alternate/1", "2", "2/items/0", "2/items/1", "4"]));
  const [folderStack, setFolderStack] = useState<FolderFrame[]>([]);
  const [propsDialogOpen, setPropsDialogOpen] = useState(false);
  const [panelWidths, setPanelWidths] = useState<PanelWidths>(readPanelWidths);
  const rootScopeJson = useMemo(() => JSON.stringify(rootScope, null, 2), [rootScope]);
  const [rootScopeDraft, setRootScopeDraft] = useState(rootScopeJson);
  const [rootScopeError, setRootScopeError] = useState<string | null>(null);
  const [clipboard, setClipboard] = useState<BlockClipboard | null>(null);
  const [contextMenu, setContextMenu] = useState<ContextMenuState | null>(null);
  const clipboardRef = useRef<BlockClipboard | null>(null);
  const builderRef = useRef<HTMLElement | null>(null);
  const treeRef = useRef<HTMLDivElement>(null);
  const locallyPublishedSelectionRef = useRef<string | null>(null);
  const refocusTree = useCallback(() => {
    window.requestAnimationFrame(() => {
      treeRef.current?.focus({ preventScroll: true });
    });
  }, []);
  const shellStyle = useMemo(
    () => ({
      ...styles.shell,
      gridTemplateColumns: `${panelWidths.palette}px 2px minmax(0, 1fr) 2px ${panelWidths.props}px`,
      "--xblox-palette-width": `${panelWidths.palette}px`,
      "--xblox-props-width": `${panelWidths.props}px`,
    }),
    [panelWidths],
  );
  const selection = selectedPath ?? internalSelectedPath;
  const currentRootListPath = folderStack[folderStack.length - 1]?.listPath ?? "";
  const rows = useMemo(
    () =>
      currentRootListPath
        ? flattenGroup(getListAtPath(value, currentRootListPath), open, 0, currentRootListPath)
        : flatten(value, open),
    [currentRootListPath, open, value],
  );
  const currentLevelBlocks = useMemo(() => {
    const list = currentRootListPath ? getListAtPath(value, currentRootListPath) : value;
    return list.filter((block): block is BlockNode => !isSwitchItem(block));
  }, [currentRootListPath, value]);
  const selectedBlock = selection ? getBlockAtPath(value, selection) : null;
  const executeAction = useCallback(async (actionId: string, context?: unknown) => {
    const action = actionStore.getAction(actionId);
    if (!action || action.disabled) return;
    await action.handler(context);
  }, []);

  useEffect(() => {
    if (!selectedPath) return;
    setInternalSelectedPath(selectedPath);
    if (locallyPublishedSelectionRef.current === selectedPath) {
      locallyPublishedSelectionRef.current = null;
      return;
    }
    setSelectedPaths((current) => (current.has(selectedPath) ? current : new Set([selectedPath])));
    setAnchorPath(selectedPath);
  }, [selectedPath]);

  useEffect(() => {
    persistPanelWidths(panelWidths);
  }, [panelWidths]);

  useEffect(() => {
    const builder = builderRef.current;
    if (!builder) return;

    const syncPanelWidthsToContainer = () => {
      const builderWidth = builder.getBoundingClientRect().width;
      setPanelWidths((current) => constrainPanelWidths(current, builderWidth));
    };

    syncPanelWidthsToContainer();
    const observer = new ResizeObserver(syncPanelWidthsToContainer);
    observer.observe(builder);
    window.addEventListener("resize", syncPanelWidthsToContainer);

    return () => {
      observer.disconnect();
      window.removeEventListener("resize", syncPanelWidthsToContainer);
    };
  }, []);

  useEffect(() => {
    if (!selectedBlock) setPropsDialogOpen(false);
  }, [selectedBlock]);

  useEffect(() => {
    setRootScopeDraft(rootScopeJson);
    setRootScopeError(null);
  }, [rootScopeJson]);

  useEffect(() => {
    clipboardRef.current = clipboard;
  }, [clipboard]);

  useEffect(() => {
    if (!contextMenu) return;
    const close = () => setContextMenu(null);
    const closeOnEscape = (event: globalThis.KeyboardEvent) => {
      if (event.key === "Escape") close();
    };
    window.addEventListener("click", close);
    window.addEventListener("keydown", closeOnEscape);
    window.addEventListener("resize", close);
    return () => {
      window.removeEventListener("click", close);
      window.removeEventListener("keydown", closeOnEscape);
      window.removeEventListener("resize", close);
    };
  }, [contextMenu]);

  const publishSelection = useCallback(
    (path: string | null) => {
      const block = path ? getBlockAtPath(value, path) : null;
      locallyPublishedSelectionRef.current = path;
      setInternalSelectedPath(path);
      onSelectionChange?.(path, block);
    },
    [onSelectionChange, value],
  );

  const selectRow = useCallback(
    (path: string, event?: MouseEvent<HTMLDivElement>) => {
      if (event?.shiftKey && anchorPath) {
        const from = rows.findIndex((row) => row.path === anchorPath);
        const to = rows.findIndex((row) => row.path === path);
        if (from >= 0 && to >= 0) {
          const [start, end] = from < to ? [from, to] : [to, from];
          const next = new Set(rows.slice(start, end + 1).map((row) => row.path));
          setSelectedPaths(next);
          publishSelection(path);
          return;
        }
      }

      if (event?.ctrlKey || event?.metaKey) {
        setSelectedPaths((current) => {
          const next = new Set(current);
          if (next.has(path)) next.delete(path);
          else next.add(path);
          return next.size ? next : new Set([path]);
        });
        setAnchorPath(path);
        publishSelection(path);
        return;
      }

      setSelectedPaths(new Set([path]));
      setAnchorPath(path);
      publishSelection(path);
    },
    [anchorPath, publishSelection, rows],
  );

  const moveSelection = useCallback(
    (delta: number) => {
      if (!rows.length) return;
      const index = Math.max(0, rows.findIndex((row) => row.path === selection));
      selectRow(rows[(index + delta + rows.length) % rows.length].path);
    },
    [rows, selectRow, selection],
  );

  const extendKeyboardSelection = useCallback(
    (delta: number) => {
      if (!rows.length) return;
      const focusIndex = Math.max(0, rows.findIndex((row) => row.path === selection));
      const anchorIndex = anchorPath ? rows.findIndex((row) => row.path === anchorPath) : focusIndex;
      const nextFocusIndex = Math.max(0, Math.min(rows.length - 1, focusIndex + delta));
      const resolvedAnchorIndex = anchorIndex >= 0 ? anchorIndex : focusIndex;
      const [start, end] =
        resolvedAnchorIndex < nextFocusIndex ? [resolvedAnchorIndex, nextFocusIndex] : [nextFocusIndex, resolvedAnchorIndex];
      const nextPath = rows[nextFocusIndex].path;
      setSelectedPaths(new Set(rows.slice(start, end + 1).map((row) => row.path)));
      if (!anchorPath) setAnchorPath(rows[resolvedAnchorIndex].path);
      publishSelection(nextPath);
    },
    [anchorPath, publishSelection, rows, selection],
  );

  const runSelected = useCallback(() => {
    if (!onRunBlock) return;
    const targets = rows
      .filter((row) => selectedPaths.has(row.path) && !hasSelectedAncestor(row.path, selectedPaths))
      .map((row) => ({ path: row.path, block: row.block }));
    if (!targets.length && selection && selectedBlock) targets.push({ path: selection, block: selectedBlock });
    void (async () => {
      for (const target of targets) {
        await onRunBlock(target.block, target.path);
      }
    })();
  }, [onRunBlock, rows, selectedBlock, selectedPaths, selection]);

  const runCurrentChain = useCallback(() => {
    if (!onRunChain || !currentLevelBlocks.length) return;
    void onRunChain(currentLevelBlocks, currentRootListPath);
  }, [currentLevelBlocks, currentRootListPath, onRunChain]);

  const enterSelectedBlock = useCallback(() => {
    if (!selection || !selectedBlock) return;
    const childGroups = getChildren(selectedBlock);
    if (!childGroups.length) return;

    setOpen((current) => new Set(current).add(selection));
    const firstChildGroup = childGroups.find((group) => group.blocks.length > 0) ?? childGroups[0];
    const nextRootListPath = `${selection}/${firstChildGroup.key}`;
    setFolderStack((current) => [...current, { listPath: nextRootListPath, entryPath: selection }]);

    const childPath = firstChildGroup.blocks.length ? `${nextRootListPath}/0` : null;
    setSelectedPaths(childPath ? new Set([childPath]) : new Set());
    setAnchorPath(childPath);
    publishSelection(childPath);
    refocusTree();
  }, [publishSelection, refocusTree, selectedBlock, selection]);

  const navigateToParentBlock = useCallback(() => {
    if (folderStack.length) {
      const frame = folderStack[folderStack.length - 1];
      setFolderStack((current) => current.slice(0, -1));
      setSelectedPaths(new Set([frame.entryPath]));
      setAnchorPath(frame.entryPath);
      publishSelection(frame.entryPath);
      refocusTree();
      return;
    }

    if (!selection) return;
    const parentPath = parentPathForList(parseItemPath(selection).listPath);
    if (!parentPath) return;
    setSelectedPaths(new Set([parentPath]));
    setAnchorPath(parentPath);
    publishSelection(parentPath);
    refocusTree();
  }, [folderStack, publishSelection, refocusTree, selection]);

  const selectSinglePath = useCallback(
    (path: string) => {
      setSelectedPaths(new Set([path]));
      setAnchorPath(path);
      publishSelection(path);
      refocusTree();
    },
    [publishSelection, refocusTree],
  );

  const moveRightThroughTree = useCallback(() => {
    if (!selection) return;
    const block = getBlockAtPath(value, selection);
    const childGroups = block ? getChildren(block) : [];
    const firstChildGroup = childGroups.find((group) => group.blocks.length > 0);

    if (firstChildGroup) {
      setOpen((current) => new Set(current).add(selection));
      selectSinglePath(`${selection}/${firstChildGroup.key}/0`);
      return;
    }

    const index = rows.findIndex((row) => row.path === selection);
    if (index >= 0 && index < rows.length - 1) selectSinglePath(rows[index + 1].path);
  }, [rows, selectSinglePath, selection, value]);

  const moveLeftThroughTree = useCallback(() => {
    if (!selection) return;
    const block = getBlockAtPath(value, selection);
    const childGroups = block ? getChildren(block) : [];
    const hasVisibleChildren = childGroups.some((group) => group.blocks.length > 0);

    if (hasVisibleChildren && open.has(selection)) {
      setOpen((current) => {
        const next = new Set(current);
        next.delete(selection);
        return next;
      });
      return;
    }

    const index = rows.findIndex((row) => row.path === selection);
    if (index > 0) selectSinglePath(rows[index - 1].path);
  }, [open, rows, selectSinglePath, selection, value]);

  const focusInlineEditor = useCallback(() => {
    if (!selection || !treeRef.current) return;
    const controls = Array.from(treeRef.current.querySelectorAll<HTMLElement>("[data-xblox-inline-path]"));
    const editor = controls.find((control) => control.dataset.xbloxInlinePath === selection);
    editor?.focus();
    if (editor instanceof HTMLInputElement) editor.select();
  }, [selection]);

  const deleteSelected = useCallback(() => {
    if (!selectedPaths.size) return;
    const nextBlocks = deleteSelectedFromList(value, "", selectedPaths).filter((block): block is BlockNode => !isSwitchItem(block));
    onChange(nextBlocks);
    const nextRows = currentRootListPath
      ? flattenGroup(getListAtPath(nextBlocks, currentRootListPath), open, 0, currentRootListPath)
      : flatten(nextBlocks, open);
    const nextPath = nextRows[0]?.path ?? null;
    setSelectedPaths(nextPath ? new Set([nextPath]) : new Set());
    setAnchorPath(nextPath);
    publishSelection(nextPath);
  }, [currentRootListPath, onChange, open, publishSelection, selectedPaths, value]);

  const moveSelectedRows = useCallback(
    (direction: -1 | 1) => {
      const activePath = selection ?? [...selectedPaths][0];
      if (!activePath) return;
      const { nextBlocks, nextSelected } = moveSelectedAcrossLists(value, activePath, selectedPaths, direction);
      onChange(nextBlocks);
      setSelectedPaths(nextSelected);
      const nextPath = [...nextSelected][direction < 0 ? 0 : nextSelected.size - 1] ?? null;
      setAnchorPath(nextPath);
      publishSelection(nextPath);
    },
    [onChange, publishSelection, selectedPaths, selection, value],
  );

  const moveSelectedTreeLevel = useCallback(
    (direction: "out" | "in") => {
      const activePath = selection ?? [...selectedPaths][0];
      if (!activePath) return;
      const result =
        direction === "out"
          ? { ...moveSelectedOutOfList(value, activePath, selectedPaths), openPath: null as string | null }
          : moveSelectedIntoPreviousSibling(value, activePath, selectedPaths);
      onChange(result.nextBlocks);
      setSelectedPaths(result.nextSelected);
      const nextPath = [...result.nextSelected][0] ?? null;
      setAnchorPath(nextPath);
      const openPath = result.openPath;
      if (openPath) setOpen((current) => new Set(current).add(openPath));
      publishSelection(nextPath);
    },
    [onChange, publishSelection, selectedPaths, selection, value],
  );

  const copySelected = useCallback(
    async (context?: { path?: string }) => {
      const contextPath = context?.path;
      const contextBlock = contextPath && !selectedPaths.has(contextPath) ? getBlockAtPath(value, contextPath) : null;
      const blocks = contextBlock ? [cloneBlock(contextBlock)] : selectedClipboardBlocks(value, rows, selectedPaths, selection);
      if (!blocks.length) return;

      const nextClipboard = { blocks };
      setClipboard(nextClipboard);
      clipboardRef.current = nextClipboard;
      try {
        await navigator.clipboard?.writeText(serializeClipboard(blocks));
      } catch {
        /* Native clipboard writes are best-effort in WebView/dev contexts. */
      }
    },
    [rows, selectedPaths, selection, value],
  );

  const pasteClipboard = useCallback(
    async (context?: { path?: string }) => {
      let source = clipboardRef.current;
      if (!source) {
        try {
          const text = await navigator.clipboard?.readText();
          source = text ? parseClipboardText(text) : null;
        } catch {
          source = null;
        }
      }
      if (!source?.blocks.length) return;

      const targetPath = context?.path ?? selection;
      const targetBlock = targetPath ? getBlockAtPath(value, targetPath) : null;
      let targetListPath = currentRootListPath;
      let targetIndex = getListAtPath(value, targetListPath).length;
      let openPath: string | null = null;

      if (targetPath && targetBlock) {
        const childKey = primaryChildKey(targetBlock);
        const childListPath = childKey ? `${targetPath}/${childKey}` : null;
        if (childListPath && source.blocks.every((block) => canInsertIntoList(value, childListPath, block))) {
          targetListPath = childListPath;
          targetIndex = getListAtPath(value, childListPath).length;
          openPath = targetPath;
        } else {
          const parsed = parseItemPath(targetPath);
          targetListPath = parsed.listPath;
          targetIndex = parsed.index + 1;
        }
      }

      if (!source.blocks.every((block) => canInsertIntoList(value, targetListPath, block))) return;
      const blocksToPaste = source.blocks.map((block) => cloneBlock(block));
      let nextBlocks = value;
      blocksToPaste.forEach((block, offset) => {
        nextBlocks = insertIntoList(nextBlocks, targetListPath, targetIndex + offset, block);
      });

      const nextSelected = new Set(blocksToPaste.map((_, offset) => itemPath(targetListPath, targetIndex + offset)));
      const nextPath = [...nextSelected][0] ?? null;
      onChange(nextBlocks);
      if (openPath) setOpen((current) => new Set(current).add(openPath));
      setSelectedPaths(nextSelected);
      setAnchorPath(nextPath);
      publishSelection(nextPath);
      refocusTree();
    },
    [currentRootListPath, onChange, publishSelection, refocusTree, selection, value],
  );

  const clipboardActions = useMemo(
    () => [
      {
        id: COPY_ACTION_ID,
        label: "Copy",
        icon: Copy,
        shortcut: "ctrl+c",
        disabled: !selectedPaths.size,
      },
      {
        id: PASTE_ACTION_ID,
        label: "Paste",
        icon: ClipboardPaste,
        shortcut: "ctrl+v",
        disabled: false,
      },
    ],
    [selectedPaths.size],
  );

  useEffect(() => {
    actionStore.registerAction({
      id: COPY_ACTION_ID,
      label: "Copy",
      icon: Copy,
      group: "Clipboard",
      shortcut: "ctrl+c",
      visibilities: { Toolbar: true, ContextMenu: true },
      disabled: !selectedPaths.size,
      handler: copySelected,
    });
    actionStore.registerAction({
      id: PASTE_ACTION_ID,
      label: "Paste",
      icon: ClipboardPaste,
      group: "Clipboard",
      shortcut: "ctrl+v",
      visibilities: { Toolbar: true, ContextMenu: true },
      disabled: false,
      handler: pasteClipboard,
    });
    return () => {
      actionStore.unregisterAction(COPY_ACTION_ID);
      actionStore.unregisterAction(PASTE_ACTION_ID);
    };
  }, [copySelected, pasteClipboard, selectedPaths.size]);

  const dropIntoRoot = useCallback(
    (item: DragItem) => {
      if (item.source === "palette" || item.source === "command") {
        const block = item.source === "palette" ? createDefaultBlock(item.kind) : item.block;
        if (!canInsertIntoList(value, currentRootListPath, block)) return;
        const targetIndex = getListAtPath(value, currentRootListPath).length;
        onChange(insertIntoList(value, currentRootListPath, targetIndex, block));
        selectRow(itemPath(currentRootListPath, targetIndex));
        refocusTree();
        return;
      }
      const removed = removeAtPath(value, item.path);
      if (!removed.removed || !canInsertIntoList(removed.nextBlocks, currentRootListPath, removed.removed)) return;
      const targetIndex = getListAtPath(removed.nextBlocks, currentRootListPath).length;
      const nextBlocks = insertIntoList(removed.nextBlocks, currentRootListPath, targetIndex, removed.removed);
      onChange(nextBlocks);
      selectRow(itemPath(currentRootListPath, targetIndex));
      refocusTree();
    },
    [currentRootListPath, onChange, refocusTree, selectRow, value],
  );

  const handleDropLine = useCallback(
    (targetPath: string, placement: DropPlacement, item: DragItem) => {
      const targetPosition = parseItemPath(targetPath);
      const targetList = targetPosition.listPath;
      const targetIndex = targetPosition.index + (placement === "after" ? 1 : 0);
      let movingBlock =
        item.source === "palette"
          ? createDefaultBlock(item.kind)
          : item.source === "command"
            ? cloneBlock(item.block)
            : getBlockAtPath(value, item.path);
      if (!movingBlock) return;

      let nextBlocks = value;
      let insertIndex = targetIndex;
      let nextTargetPath = targetPath;
      if (item.source === "tree") {
        if (item.path === targetPath) return;
        if (targetList === item.path || targetList.startsWith(`${item.path}/`) || targetPath.startsWith(`${item.path}/`)) return;
        const removed = removeAtPath(value, item.path);
        if (!removed.removed) return;
        movingBlock = removed.removed;
        nextBlocks = removed.nextBlocks;
        if (removed.listPath === targetList && removed.index < targetPosition.index) {
          nextTargetPath = itemPath(targetList, targetPosition.index - 1);
        }
        if (placement !== "inside" && removed.listPath === targetList && removed.index < targetIndex) insertIndex -= 1;
        if (placement !== "inside" && removed.listPath === targetList && (insertIndex === removed.index || insertIndex === removed.index + 1)) return;
      }

      if (placement === "inside") {
        const targetBlock = getBlockAtPath(nextBlocks, nextTargetPath);
        const childKey = targetBlock ? primaryChildKey(targetBlock) : null;
        const childListPath = childKey ? `${nextTargetPath}/${childKey}` : "";
        if (!targetBlock || !childKey || !canInsertIntoList(nextBlocks, childListPath, movingBlock)) return;
        insertIndex = getListAtPath(nextBlocks, childListPath).length;
        nextBlocks = insertIntoList(nextBlocks, childListPath, insertIndex, movingBlock);
        const nextPath = itemPath(childListPath, insertIndex);
        onChange(nextBlocks);
        setOpen((current) => new Set(current).add(nextTargetPath));
        setSelectedPaths(new Set([nextPath]));
        setAnchorPath(nextPath);
        publishSelection(nextPath);
        refocusTree();
        return;
      }

      if (!canInsertIntoList(nextBlocks, targetList, movingBlock)) return;
      nextBlocks = insertIntoList(nextBlocks, targetList, insertIndex, movingBlock);
      const nextPath = itemPath(targetList, insertIndex);
      onChange(nextBlocks);
      setSelectedPaths(new Set([nextPath]));
      setAnchorPath(nextPath);
      publishSelection(nextPath);
      refocusTree();
    },
    [onChange, publishSelection, refocusTree, value],
  );

  const canDropInsideRow = useCallback(
    (targetPath: string, item: DragItem) => {
      if (item.source === "tree" && (item.path === targetPath || targetPath.startsWith(`${item.path}/`))) return false;
      const targetBlock = getBlockAtPath(value, targetPath);
      const childKey = targetBlock ? primaryChildKey(targetBlock) : null;
      if (!targetBlock || !childKey) return false;
      const movingBlock =
        item.source === "palette"
          ? createDefaultBlock(item.kind)
          : item.source === "command"
            ? item.block
            : getBlockAtPath(value, item.path);
      return Boolean(movingBlock && canInsertIntoList(value, `${targetPath}/${childKey}`, movingBlock));
    },
    [value],
  );

  const canDropOnRow = useCallback(
    (targetPath: string, placement: DropPlacement, item: DragItem) => {
      const targetPosition = parseItemPath(targetPath);
      const targetList = targetPosition.listPath;
      let movingBlock =
        item.source === "palette"
          ? createDefaultBlock(item.kind)
          : item.source === "command"
            ? item.block
            : getBlockAtPath(value, item.path);
      if (!movingBlock) return false;
      if (item.source === "tree") {
        if (item.path === targetPath) return false;
        if (targetList === item.path || targetList.startsWith(`${item.path}/`) || targetPath.startsWith(`${item.path}/`)) return false;
        const removed = removeAtPath(value, item.path);
        if (!removed.removed) return false;
        movingBlock = removed.removed;
        if (placement !== "inside") {
          const targetIndex = targetPosition.index + (placement === "after" ? 1 : 0);
          let insertIndex = targetIndex;
          if (removed.listPath === targetList && removed.index < targetIndex) insertIndex -= 1;
          if (removed.listPath === targetList && (insertIndex === removed.index || insertIndex === removed.index + 1)) return false;
          return canInsertIntoList(removed.nextBlocks, targetList, movingBlock);
        }
        const nextTargetPath = removed.listPath === targetList && removed.index < targetPosition.index ? itemPath(targetList, targetPosition.index - 1) : targetPath;
        const targetBlock = getBlockAtPath(removed.nextBlocks, nextTargetPath);
        const childKey = targetBlock ? primaryChildKey(targetBlock) : null;
        return Boolean(childKey && canInsertIntoList(removed.nextBlocks, `${nextTargetPath}/${childKey}`, movingBlock));
      }
      if (placement === "inside") return canDropInsideRow(targetPath, item);
      return canInsertIntoList(value, targetList, movingBlock);
    },
    [canDropInsideRow, value],
  );

  const activateRow = useCallback(
    (path: string) => {
      setSelectedPaths(new Set([path]));
      setAnchorPath(path);
      publishSelection(path);
    },
    [publishSelection],
  );

  const toggleRow = useCallback((path: string) => {
    setOpen((current) => {
      const next = new Set(current);
      if (next.has(path)) next.delete(path);
      else next.add(path);
      return next;
    });
  }, []);

  const openRow = useCallback(
    (path: string) => {
      activateRow(path);
      const block = getBlockAtPath(value, path);
      const childGroups = block ? getChildren(block) : [];
      if (!block || !childGroups.length) return;
      setOpen((current) => new Set(current).add(path));
      const firstChildGroup = childGroups.find((group) => group.blocks.length > 0) ?? childGroups[0];
      const nextRootListPath = `${path}/${firstChildGroup.key}`;
      setFolderStack((current) => [...current, { listPath: nextRootListPath, entryPath: path }]);
      const childPath = firstChildGroup.blocks.length ? `${nextRootListPath}/0` : null;
      setSelectedPaths(childPath ? new Set([childPath]) : new Set());
      setAnchorPath(childPath);
      publishSelection(childPath);
    },
    [activateRow, publishSelection, value],
  );

  const contextMenuRow = useCallback(
    (path: string, event: MouseEvent<HTMLDivElement>) => {
      event.preventDefault();
      event.stopPropagation();
      if (!selectedPaths.has(path)) {
        setSelectedPaths(new Set([path]));
        setAnchorPath(path);
        publishSelection(path);
      }
      setContextMenu({ x: event.clientX, y: event.clientY, path });
    },
    [publishSelection, selectedPaths],
  );

  const handleTreeKeyDown = useCallback(
    (event: KeyboardEvent<HTMLDivElement>) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "c") {
        event.preventDefault();
        void executeAction(COPY_ACTION_ID);
      } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "v") {
        event.preventDefault();
        void executeAction(PASTE_ACTION_ID);
      } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
        event.preventDefault();
        if (onSave && !saveDisabled) onSave();
      } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "r") {
        event.preventDefault();
        runCurrentChain();
      } else if (event.key === "Delete") {
        event.preventDefault();
        deleteSelected();
      } else if (event.key === "F2") {
        event.preventDefault();
        focusInlineEditor();
      } else if (event.key === "Backspace") {
        event.preventDefault();
        navigateToParentBlock();
      } else if (event.key === "Home") {
        event.preventDefault();
        if (rows.length) selectSinglePath(rows[0].path);
      } else if (event.key === "End") {
        event.preventDefault();
        if (rows.length) selectSinglePath(rows[rows.length - 1].path);
      } else if ((event.ctrlKey || event.metaKey) && event.key === "ArrowUp") {
        event.preventDefault();
        moveSelectedRows(-1);
      } else if ((event.ctrlKey || event.metaKey) && event.key === "ArrowDown") {
        event.preventDefault();
        moveSelectedRows(1);
      } else if ((event.ctrlKey || event.metaKey) && event.key === "ArrowLeft") {
        event.preventDefault();
        moveSelectedTreeLevel("out");
      } else if ((event.ctrlKey || event.metaKey) && event.key === "ArrowRight") {
        event.preventDefault();
        moveSelectedTreeLevel("in");
      } else if (event.shiftKey && event.key === "ArrowDown") {
        event.preventDefault();
        extendKeyboardSelection(1);
      } else if (event.shiftKey && event.key === "ArrowUp") {
        event.preventDefault();
        extendKeyboardSelection(-1);
      } else if (event.key === "ArrowDown") {
        event.preventDefault();
        moveSelection(1);
      } else if (event.key === "ArrowUp") {
        event.preventDefault();
        moveSelection(-1);
      } else if (event.key === "ArrowRight" && selection) {
        event.preventDefault();
        moveRightThroughTree();
      } else if (event.key === "ArrowLeft" && selection) {
        event.preventDefault();
        moveLeftThroughTree();
      } else if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
        event.preventDefault();
        if (selectedBlock) setPropsDialogOpen(true);
      } else if (event.key === "Enter") {
        event.preventDefault();
        enterSelectedBlock();
      } else if (event.key.toLowerCase() === "r") {
        event.preventDefault();
        runSelected();
      }
    },
    [
      deleteSelected,
      enterSelectedBlock,
      executeAction,
      extendKeyboardSelection,
      focusInlineEditor,
      moveLeftThroughTree,
      moveRightThroughTree,
      moveSelectedRows,
      moveSelectedTreeLevel,
      moveSelection,
      navigateToParentBlock,
      onSave,
      rows,
      runSelected,
      runCurrentChain,
      saveDisabled,
      selectSinglePath,
      selectedBlock,
      selection,
    ],
  );

  const updateSelected = useCallback(
    (patch: Partial<Record<string, unknown>>) => {
      if (!selection) return;
      onChange(updateAtPath(value, selection, patch));
    },
    [onChange, selection, value],
  );

  const applyRootScopeDraft = useCallback(() => {
    if (!onRootScopeChange) return;
    try {
      const parsed = rootScopeDraft.trim() ? JSON.parse(rootScopeDraft) : {};
      if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
        setRootScopeError("Root scope must be a JSON object.");
        return;
      }
      setRootScopeError(null);
      onRootScopeChange(parsed as Record<string, unknown>);
    } catch (error) {
      setRootScopeError(error instanceof Error ? error.message : "Invalid JSON.");
    }
  }, [onRootScopeChange, rootScopeDraft]);

  const startResize = useCallback((side: "palette" | "props", startX: number, startWidth: number) => {
    const onPointerMove = (event: globalThis.PointerEvent) => {
      const delta = event.clientX - startX;
      setPanelWidths((current) => {
        const builderWidth = builderRef.current?.getBoundingClientRect().width ?? window.innerWidth;
        if (side === "palette") {
          return constrainPanelWidths({ ...current, palette: startWidth + delta }, builderWidth, "props");
        }
        return constrainPanelWidths({ ...current, props: startWidth - delta }, builderWidth, "palette");
      });
    };
    const onPointerUp = () => {
      window.removeEventListener("pointermove", onPointerMove);
      window.removeEventListener("pointerup", onPointerUp);
      document.body.classList.remove("xblox-resizing-panels");
    };
    document.body.classList.add("xblox-resizing-panels");
    window.addEventListener("pointermove", onPointerMove);
    window.addEventListener("pointerup", onPointerUp);
  }, []);

  const selectedNativeBlock = selectedBlock ? nativePalette.find((item) => item.kind === selectedBlock.kind) : undefined;
  const propertyEditor = <XbloxBlockPropertyEditor block={selectedBlock} onPatch={updateSelected} nativeParams={selectedNativeBlock?.params} />;

  return (
    <>
    <section ref={builderRef} className={className ?? "xblox-builder"} style={shellStyle}>
      <XbloxPalette
        palette={palette}
        nativePalette={nativePalette}
        commandPalette={commandPalette}
        theme={theme}
        onToggleTheme={onToggleTheme}
      />

      <div
        className="xblox-panel-resizer"
        role="separator"
        aria-label="Resize block palette"
        aria-orientation="vertical"
        onPointerDown={(event) => {
          event.preventDefault();
          startResize("palette", event.clientX, panelWidths.palette);
        }}
      />

      <BlockTreeView
        rows={rows}
        currentRootListPath={currentRootListPath}
        selectedPaths={selectedPaths}
        open={open}
        runState={runState}
        selection={selection}
        selectedBlock={selectedBlock}
        currentLevelBlocks={currentLevelBlocks}
        folderStack={folderStack}
        running={running}
        treeRef={treeRef}
        onSave={onSave}
        saveDisabled={saveDisabled}
        onRunChain={onRunChain}
        onStopRun={onStopRun}
        clipboardActions={clipboardActions}
        onExecuteAction={executeAction}
        onRunSelected={runSelected}
        onRunCurrentChain={runCurrentChain}
        onDeleteSelected={deleteSelected}
        onMoveSelectedRows={moveSelectedRows}
        onEnterSelectedBlock={enterSelectedBlock}
        onNavigateToParentBlock={navigateToParentBlock}
        onKeyDown={handleTreeKeyDown}
        onSelectRow={selectRow}
        onActivateRow={activateRow}
        onToggleRow={toggleRow}
        onDropLine={handleDropLine}
        onCanDropOnRow={canDropOnRow}
        onCanDropInside={canDropInsideRow}
        onPatchRow={(path, patch) => onChange(updateAtPath(value, path, patch))}
        onOpenRow={openRow}
        onContextMenuRow={contextMenuRow}
        onDropIntoRoot={dropIntoRoot}
      />

      <div
        className="xblox-panel-resizer"
        role="separator"
        aria-label="Resize properties panel"
        aria-orientation="vertical"
        onPointerDown={(event) => {
          event.preventDefault();
          startResize("props", event.clientX, panelWidths.props);
        }}
      />

      <XbloxPropertyPanel
        block={selectedBlock}
        onPatch={updateSelected}
        nativeParams={selectedNativeBlock?.params}
        rootScopeDraft={rootScopeDraft}
        rootScopeError={rootScopeError}
        onRootScopeDraftChange={(draft) => {
          setRootScopeDraft(draft);
          setRootScopeError(null);
        }}
        onApplyRootScopeDraft={applyRootScopeDraft}
        canEditRootScope={Boolean(onRootScopeChange)}
      />
    </section>
    {contextMenu ? (
      <div
        className="xblox-context-menu"
        role="menu"
        style={{ left: contextMenu.x, top: contextMenu.y }}
        onClick={(event) => event.stopPropagation()}
        onContextMenu={(event) => event.preventDefault()}
      >
        {clipboardActions.map((action) => {
          const Icon = action.icon;
          return (
            <button
              key={action.id}
              type="button"
              role="menuitem"
              disabled={action.disabled}
              onClick={() => {
                setContextMenu(null);
                void executeAction(action.id, { path: contextMenu.path });
              }}
            >
              {Icon ? <Icon aria-hidden="true" /> : null}
              <span>{action.label}</span>
              {action.shortcut ? <small>{action.shortcut}</small> : null}
            </button>
          );
        })}
        <hr />
        <button
          type="button"
          role="menuitem"
          disabled={!onRunBlock}
          onClick={() => {
            setContextMenu(null);
            const block = getBlockAtPath(value, contextMenu.path);
            if (block) void onRunBlock?.(block, contextMenu.path);
          }}
        >
          <Play aria-hidden="true" />
          <span>Run</span>
          <small>r</small>
        </button>
        <button
          type="button"
          role="menuitem"
          onClick={() => {
            setContextMenu(null);
            setSelectedPaths(new Set([contextMenu.path]));
            setAnchorPath(contextMenu.path);
            publishSelection(contextMenu.path);
            setPropsDialogOpen(true);
          }}
        >
          <FolderOpen aria-hidden="true" />
          <span>Properties</span>
          <small>ctrl+enter</small>
        </button>
        <button
          type="button"
          role="menuitem"
          disabled={!selectedPaths.size}
          onClick={() => {
            setContextMenu(null);
            deleteSelected();
          }}
        >
          <Trash2 aria-hidden="true" />
          <span>Delete</span>
          <small>del</small>
        </button>
      </div>
    ) : null}
    {propsDialogOpen ? (
      <div className="xblox-props-dialog-backdrop" role="presentation" onMouseDown={() => setPropsDialogOpen(false)}>
        <div
          className="xblox-props-dialog"
          role="dialog"
          aria-modal="true"
          aria-label="Block properties"
          onMouseDown={(event) => event.stopPropagation()}
          onKeyDown={(event) => {
            if (event.key === "Escape") {
              event.preventDefault();
              setPropsDialogOpen(false);
            }
          }}
        >
          <div className="xblox-props-dialog-head">
            <h3>Block Properties</h3>
            <button type="button" onClick={() => setPropsDialogOpen(false)} aria-label="Close block properties">
              Close
            </button>
          </div>
          <div className="xblox-props-dialog-body">{propertyEditor}</div>
        </div>
      </div>
    ) : null}
    </>
  );
}
