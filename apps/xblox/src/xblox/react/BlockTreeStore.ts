import type { MouseEvent } from "react";
import { create } from "zustand";

import type { BlockRunState } from "./xblox-builder";
import type { TreeRow } from "./BlockModel";
import type { DragItem, DropPlacement } from "./BlockUtils";

type BlockTreeStoreState = {
  rows: TreeRow[];
  selectedPaths: Set<string>;
  open: Set<string>;
  runState?: Record<string, BlockRunState>;
  selectRow: (path: string, event?: MouseEvent<HTMLDivElement>) => void;
  activateRow: (path: string) => void;
  toggleRow: (path: string) => void;
  dropLine: (targetPath: string, placement: DropPlacement, item: DragItem) => void;
  canDropOnRow: (targetPath: string, placement: DropPlacement, item: DragItem) => boolean;
  canDropInside: (targetPath: string, item: DragItem) => boolean;
  patchRow: (path: string, patch: Partial<Record<string, unknown>>) => void;
  openRow: (path: string) => void;
  contextMenuRow: (path: string, event: MouseEvent<HTMLDivElement>) => void;
  dropIntoRoot: (item: DragItem) => void;
};

const noop = () => undefined;

export const useBlockTreeStore = create<BlockTreeStoreState>(() => ({
  rows: [],
  selectedPaths: new Set(),
  open: new Set(),
  runState: undefined,
  selectRow: noop,
  activateRow: noop,
  toggleRow: noop,
  dropLine: noop,
  canDropOnRow: () => false,
  canDropInside: () => false,
  patchRow: noop,
  openRow: noop,
  contextMenuRow: noop,
  dropIntoRoot: noop,
}));

export function configureBlockTreeStore(state: Partial<BlockTreeStoreState>) {
  useBlockTreeStore.setState(state);
}
