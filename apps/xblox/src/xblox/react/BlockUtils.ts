import type { BlockNode } from "@/schema/blocks-file";
import type { BlockPaletteKind, TreeRow, XbloxBlock } from "./BlockModel";
import { getBlockAtPath, hasSelectedAncestor } from "./BlockModel";

export type DragItem =
  | { source: "palette"; kind: BlockPaletteKind }
  | { source: "command"; block: BlockNode }
  | { source: "tree"; path: string };

export type BlockClipboard = {
  blocks: XbloxBlock[];
};

export type ContextMenuState = {
  x: number;
  y: number;
  path: string;
};

export type DropPlacement = "before" | "inside" | "after";

export const DND_TYPE = "xblox/block";
export const CLIPBOARD_MIME = "application/x-polymech-xblox-blocks";

export function cloneBlock<T extends XbloxBlock>(block: T): T {
  return JSON.parse(JSON.stringify(block)) as T;
}

export function selectedClipboardBlocks(blocks: BlockNode[], rows: TreeRow[], selected: Set<string>, selection: string | null): XbloxBlock[] {
  const selectedRows = rows.filter((row) => selected.has(row.path) && !hasSelectedAncestor(row.path, selected));
  if (selectedRows.length) return selectedRows.map((row) => cloneBlock(row.block));
  const selectedBlock = selection ? getBlockAtPath(blocks, selection) : null;
  return selectedBlock ? [cloneBlock(selectedBlock)] : [];
}

export function serializeClipboard(blocks: XbloxBlock[]) {
  return JSON.stringify({ type: CLIPBOARD_MIME, blocks });
}

export function parseClipboardText(text: string): BlockClipboard | null {
  try {
    const parsed = JSON.parse(text);
    if (parsed?.type !== CLIPBOARD_MIME || !Array.isArray(parsed.blocks)) return null;
    return { blocks: parsed.blocks as XbloxBlock[] };
  } catch {
    return null;
  }
}
