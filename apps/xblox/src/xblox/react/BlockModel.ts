import type { BlockNode, SwitchItemNode } from "@/schema/blocks-file";

export type XbloxBlock = BlockNode | SwitchItemNode;
export type BlockPaletteKind = XbloxBlock["kind"];

export type TreeRow = {
  block: XbloxBlock;
  path: string;
  depth: number;
  listPath: string;
  index: number;
};

export type FolderFrame = {
  listPath: string;
  entryPath: string;
};

export function createDefaultBlock(kind: BlockPaletteKind): XbloxBlock {
  if (kind === "fetch" || kind === "network" || kind === "httpRequest") {
    return { kind, url: "https://", decode: "raw", timeoutMs: 30000, followRedirects: true, storeAs: "response" };
  }
  if (kind === "Parse" || kind === "parse") return { kind, parser: "jq", filter: ".", storeAs: "parsed" };
  if (kind === "shell" || kind === "Shell") {
    return { kind, mode: "shell", shell: "auto", command: "echo hello", timeoutMs: 30000, log: false, stdout: "info", stderr: "error", storeAs: "stdout" };
  }
  if (kind === "runScript") return { kind, method: "return undefined;" };
  if (kind === "command") return { kind, command: { id: "custom.command", label: "Command", type: "button" } };
  if (kind === "setVariable") return { kind, name: "value", expression: "1" };
  if (kind === "getVariable") return { kind, name: "value", target: "lastValue" };
  if (kind === "log") return { kind, level: "trace", message: "value" };
  if (kind === "wait") return { kind, ms: 500 };
  if (kind === "if") return { kind, condition: "true", consequent: [], alternate: [] };
  if (kind === "for") return { kind, initial: "0", final: "3", comparator: "<", modifier: "+1", items: [] };
  if (kind === "while") return { kind, condition: "false", loopLimit: 10, items: [] };
  if (kind === "switch") return { kind, variable: "mode", items: [] };
  if (kind === "case") return { kind, comparator: "===", expression: "\"value\"", consequent: [] };
  if (kind === "switchDefault") return { kind, consequent: [] };
  return { kind: "break" };
}

export function isSwitchItem(block: XbloxBlock): block is SwitchItemNode {
  return block.kind === "case" || block.kind === "switchDefault";
}

export function blockLabel(block: XbloxBlock): string {
  if (block.kind === "runScript") return block.id ? `runScript ${block.id}` : "runScript";
  if (block.kind === "command") return block.command.label || block.command.id || "command";
  if (block.kind === "setVariable") return `set ${block.name}`;
  if (block.kind === "getVariable") return block.target ? `get ${block.name} -> ${block.target}` : `get ${block.name}`;
  if (block.kind === "log") return `log ${block.level || "trace"} ${block.message}`;
  if (block.kind === "wait") return `wait ${block.ms}ms`;
  if (block.kind === "fetch" || block.kind === "network" || block.kind === "httpRequest") return `${block.kind} ${block.url}`;
  if (block.kind === "Parse" || block.kind === "parse") return `parse ${block.filter || block.query || "."}`;
  if (block.kind === "shell" || block.kind === "Shell") return `shell ${block.command || block.line || block.exe || ""}`;
  if (block.kind === "if") return `if ${block.condition}`;
  if (block.kind === "for") return `for ${block.initial}${block.comparator}${block.final}`;
  if (block.kind === "while") return `while ${block.condition}`;
  if (block.kind === "switch") return `switch ${block.variable}`;
  if (block.kind === "case") return `case ${block.comparator} ${block.expression}`;
  return block.kind;
}

export function getChildren(block: XbloxBlock): { key: string; blocks: XbloxBlock[] }[] {
  if (block.kind === "if") {
    return [
      { key: "consequent", blocks: block.consequent },
      { key: "alternate", blocks: block.alternate ?? [] },
    ];
  }
  if (block.kind === "for" || block.kind === "while" || block.kind === "switch") return [{ key: "items", blocks: block.items }];
  if (block.kind === "case" || block.kind === "switchDefault") return [{ key: "consequent", blocks: block.consequent }];
  return [];
}

export function primaryChildKey(block: XbloxBlock): "consequent" | "items" | null {
  if (block.kind === "if" || block.kind === "case" || block.kind === "switchDefault") return "consequent";
  if (block.kind === "for" || block.kind === "while" || block.kind === "switch") return "items";
  return null;
}

export function canContain(parent: XbloxBlock, child: XbloxBlock): boolean {
  if (parent.kind === "switch") return isSwitchItem(child);
  if (isSwitchItem(child)) return false;
  return primaryChildKey(parent) !== null;
}

export function itemPath(listPath: string, index: number) {
  return listPath ? `${listPath}/${index}` : String(index);
}

export function parseItemPath(path: string) {
  const parts = path.split("/");
  return {
    index: Number(parts[parts.length - 1]),
    listPath: parts.slice(0, -1).join("/"),
  };
}

export function parentPathForList(listPath: string) {
  if (!listPath) return null;
  const parts = listPath.split("/");
  return parts.slice(0, -1).join("/");
}

export function childKeyForList(listPath: string) {
  if (!listPath) return "";
  const parts = listPath.split("/");
  return parts[parts.length - 1];
}

export function hasSelectedAncestor(path: string, selected: Set<string>) {
  let current = parseItemPath(path).listPath;
  while (current) {
    const parent = parentPathForList(current);
    if (parent && selected.has(parent)) return true;
    current = parent ? parseItemPath(parent).listPath : "";
  }
  return false;
}

export function flatten(blocks: BlockNode[], open: Set<string>, depth = 0, listPath = ""): TreeRow[] {
  const rows: TreeRow[] = [];
  blocks.forEach((block, index) => {
    const path = itemPath(listPath, index);
    rows.push({ block, path, depth, listPath, index });
    if (!open.has(path)) return;
    for (const group of getChildren(block)) {
      rows.push(...flattenGroup(group.blocks, open, depth + 1, `${path}/${group.key}`));
    }
  });
  return rows;
}

export function flattenGroup(blocks: XbloxBlock[], open: Set<string>, depth: number, listPath: string): TreeRow[] {
  const rows: TreeRow[] = [];
  blocks.forEach((block, index) => {
    const path = itemPath(listPath, index);
    rows.push({ block, path, depth, listPath, index });
    if (!open.has(path)) return;
    for (const group of getChildren(block)) {
      rows.push(...flattenGroup(group.blocks, open, depth + 1, `${path}/${group.key}`));
    }
  });
  return rows;
}

export function getBlockAtPath(blocks: BlockNode[], path: string): XbloxBlock | null {
  const parts = path.split("/");
  let list: XbloxBlock[] = blocks;
  let block: XbloxBlock | undefined;
  for (let i = 0; i < parts.length; i += 1) {
    const part = parts[i];
    const index = Number(part);
    if (!Number.isNaN(index)) {
      block = list[index];
      if (!block) return null;
      continue;
    }
    if (!block) return null;
    const group = getChildren(block).find((childGroup) => childGroup.key === part);
    if (!group) return null;
    list = group.blocks;
  }
  return block ?? null;
}

export function updateAtPath(blocks: BlockNode[], path: string, patch: Partial<Record<string, unknown>>): BlockNode[] {
  const parts = path.split("/");
  const updateList = (list: XbloxBlock[], partIndex: number): XbloxBlock[] => {
    const index = Number(parts[partIndex]);
    return list.map((block, currentIndex) => {
      if (currentIndex !== index) return block;
      if (partIndex === parts.length - 1) return { ...block, ...patch } as XbloxBlock;
      const key = parts[partIndex + 1];
      if ((key === "consequent" || key === "alternate") && "consequent" in block) {
        return { ...block, [key]: updateList(((block as Extract<BlockNode, { kind: "if" }>)[key] ?? []) as BlockNode[], partIndex + 2) };
      }
      if (key === "items" && "items" in block) {
        return { ...block, items: updateList(block.items, partIndex + 2) as never };
      }
      return block;
    });
  };
  return updateList(blocks, 0) as BlockNode[];
}

export function getListAtPath(blocks: BlockNode[], listPath: string): XbloxBlock[] {
  if (!listPath) return blocks;
  const parentPath = parentPathForList(listPath);
  const key = childKeyForList(listPath);
  const parent = parentPath ? getBlockAtPath(blocks, parentPath) : null;
  return parent ? (getChildren(parent).find((group) => group.key === key)?.blocks ?? []) : [];
}

export function setListAtPath(blocks: BlockNode[], listPath: string, nextList: XbloxBlock[]): BlockNode[] {
  if (!listPath) return nextList.filter((block): block is BlockNode => !isSwitchItem(block));
  const parentPath = parentPathForList(listPath);
  const key = childKeyForList(listPath);
  if (!parentPath) return blocks;
  return updateAtPath(blocks, parentPath, { [key]: nextList });
}

export function canInsertIntoList(blocks: BlockNode[], listPath: string, block: XbloxBlock) {
  if (!listPath) return !isSwitchItem(block);
  const parentPath = parentPathForList(listPath);
  const key = childKeyForList(listPath);
  const parent = parentPath ? getBlockAtPath(blocks, parentPath) : null;
  if (!parent || !getChildren(parent).some((group) => group.key === key)) return false;
  if (parent.kind === "switch") return isSwitchItem(block);
  return canContain(parent, block);
}

export function insertIntoList(blocks: BlockNode[], listPath: string, index: number, block: XbloxBlock): BlockNode[] {
  if (!canInsertIntoList(blocks, listPath, block)) return blocks;
  const list = getListAtPath(blocks, listPath);
  const next = [...list];
  next.splice(Math.max(0, Math.min(index, next.length)), 0, block);
  return setListAtPath(blocks, listPath, next);
}

export function removeAtPath(blocks: BlockNode[], path: string): { nextBlocks: BlockNode[]; removed: XbloxBlock | null; listPath: string; index: number } {
  const { listPath, index } = parseItemPath(path);
  const list = getListAtPath(blocks, listPath);
  const removed = list[index] ?? null;
  if (!removed) return { nextBlocks: blocks, removed: null, listPath, index };
  return {
    nextBlocks: setListAtPath(blocks, listPath, list.filter((_, currentIndex) => currentIndex !== index)),
    removed,
    listPath,
    index,
  };
}

export function deleteSelectedFromList(list: XbloxBlock[], listPath: string, selected: Set<string>): XbloxBlock[] {
  const next: XbloxBlock[] = [];
  list.forEach((block, index) => {
    const path = itemPath(listPath, index);
    if (selected.has(path)) return;
    if (block.kind === "if") {
      next.push({
        ...block,
        consequent: deleteSelectedFromList(block.consequent, `${path}/consequent`, selected) as BlockNode[],
        alternate: deleteSelectedFromList(block.alternate ?? [], `${path}/alternate`, selected) as BlockNode[],
      });
      return;
    }
    if (block.kind === "for" || block.kind === "while") {
      next.push({ ...block, items: deleteSelectedFromList(block.items, `${path}/items`, selected) as BlockNode[] });
      return;
    }
    if (block.kind === "switch") {
      next.push({ ...block, items: deleteSelectedFromList(block.items, `${path}/items`, selected) as SwitchItemNode[] });
      return;
    }
    if (block.kind === "case" || block.kind === "switchDefault") {
      next.push({ ...block, consequent: deleteSelectedFromList(block.consequent, `${path}/consequent`, selected) as BlockNode[] });
      return;
    }
    next.push(block);
  });
  return next;
}

export function moveSelectedWithinList(blocks: BlockNode[], listPath: string, selected: Set<string>, direction: -1 | 1) {
  const list = getListAtPath(blocks, listPath);
  const selectedIndices = new Set<number>();
  list.forEach((_, index) => {
    if (selected.has(itemPath(listPath, index))) selectedIndices.add(index);
  });
  if (!selectedIndices.size) return { nextBlocks: blocks, nextSelected: selected };

  const entries = list.map((block, oldIndex) => ({ block, oldIndex, selected: selectedIndices.has(oldIndex) }));
  if (direction < 0) {
    for (let i = 1; i < entries.length; i += 1) {
      if (entries[i].selected && !entries[i - 1].selected) {
        [entries[i - 1], entries[i]] = [entries[i], entries[i - 1]];
      }
    }
  } else {
    for (let i = entries.length - 2; i >= 0; i -= 1) {
      if (entries[i].selected && !entries[i + 1].selected) {
        [entries[i], entries[i + 1]] = [entries[i + 1], entries[i]];
      }
    }
  }

  const nextSelected = new Set<string>();
  entries.forEach((entry, newIndex) => {
    if (entry.selected) nextSelected.add(itemPath(listPath, newIndex));
  });
  return {
    nextBlocks: setListAtPath(
      blocks,
      listPath,
      entries.map((entry) => entry.block),
    ),
    nextSelected,
  };
}

export function insertManyIntoList(blocks: BlockNode[], listPath: string, index: number, moving: XbloxBlock[]) {
  if (moving.some((block) => !canInsertIntoList(blocks, listPath, block))) return blocks;
  const list = getListAtPath(blocks, listPath);
  const next = [...list];
  next.splice(Math.max(0, Math.min(index, next.length)), 0, ...moving);
  return setListAtPath(blocks, listPath, next);
}

export function moveSelectedAcrossLists(blocks: BlockNode[], activePath: string, selected: Set<string>, direction: -1 | 1) {
  const { listPath } = parseItemPath(activePath);
  const list = getListAtPath(blocks, listPath);
  const selectedIndices = list
    .map((_, index) => index)
    .filter((index) => selected.has(itemPath(listPath, index)));

  if (!selectedIndices.length) return { nextBlocks: blocks, nextSelected: selected };

  const minIndex = Math.min(...selectedIndices);
  const maxIndex = Math.max(...selectedIndices);
  const canMoveWithinList = direction < 0 ? minIndex > 0 : maxIndex < list.length - 1;
  if (canMoveWithinList) return moveSelectedWithinList(blocks, listPath, selected, direction);
  if (!listPath) return { nextBlocks: blocks, nextSelected: selected };

  const parentPath = parentPathForList(listPath);
  if (!parentPath) return { nextBlocks: blocks, nextSelected: selected };

  const parentPosition = parseItemPath(parentPath);
  const moving = selectedIndices.map((index) => list[index]).filter((block): block is XbloxBlock => Boolean(block));
  if (moving.some((block) => !canInsertIntoList(blocks, parentPosition.listPath, block))) {
    return { nextBlocks: blocks, nextSelected: selected };
  }

  const nextChildList = list.filter((_, index) => !selectedIndices.includes(index));
  const withoutMoving = setListAtPath(blocks, listPath, nextChildList);
  const insertIndex = direction < 0 ? parentPosition.index : parentPosition.index + 1;
  if (moving.some((block) => !canInsertIntoList(withoutMoving, parentPosition.listPath, block))) {
    return { nextBlocks: blocks, nextSelected: selected };
  }
  const nextBlocks = insertManyIntoList(withoutMoving, parentPosition.listPath, insertIndex, moving);
  const nextSelected = new Set(moving.map((_, offset) => itemPath(parentPosition.listPath, insertIndex + offset)));

  return { nextBlocks, nextSelected };
}

export function moveSelectedOutOfList(blocks: BlockNode[], activePath: string, selected: Set<string>) {
  const { listPath } = parseItemPath(activePath);
  if (!listPath) return { nextBlocks: blocks, nextSelected: selected };

  const parentPath = parentPathForList(listPath);
  if (!parentPath) return { nextBlocks: blocks, nextSelected: selected };

  const parentPosition = parseItemPath(parentPath);
  const list = getListAtPath(blocks, listPath);
  const selectedIndices = list
    .map((_, index) => index)
    .filter((index) => selected.has(itemPath(listPath, index)));
  if (!selectedIndices.length) return { nextBlocks: blocks, nextSelected: selected };

  const moving = selectedIndices.map((index) => list[index]).filter((block): block is XbloxBlock => Boolean(block));
  if (moving.some((block) => !canInsertIntoList(blocks, parentPosition.listPath, block))) {
    return { nextBlocks: blocks, nextSelected: selected };
  }

  const nextChildList = list.filter((_, index) => !selectedIndices.includes(index));
  const withoutMoving = setListAtPath(blocks, listPath, nextChildList);
  const insertIndex = parentPosition.index + 1;
  if (moving.some((block) => !canInsertIntoList(withoutMoving, parentPosition.listPath, block))) {
    return { nextBlocks: blocks, nextSelected: selected };
  }

  const nextBlocks = insertManyIntoList(withoutMoving, parentPosition.listPath, insertIndex, moving);
  const nextSelected = new Set(moving.map((_, offset) => itemPath(parentPosition.listPath, insertIndex + offset)));
  return { nextBlocks, nextSelected };
}

export function moveSelectedIntoPreviousSibling(blocks: BlockNode[], activePath: string, selected: Set<string>) {
  const { listPath, index: activeIndex } = parseItemPath(activePath);
  const list = getListAtPath(blocks, listPath);
  const selectedIndices = list
    .map((_, index) => index)
    .filter((index) => selected.has(itemPath(listPath, index)));
  if (!selectedIndices.length) return { nextBlocks: blocks, nextSelected: selected, openPath: null as string | null };

  const minIndex = Math.min(...selectedIndices);
  const targetIndex = minIndex - 1;
  if (targetIndex < 0) return { nextBlocks: blocks, nextSelected: selected, openPath: null as string | null };

  const targetBlock = list[targetIndex];
  const childKey = targetBlock ? primaryChildKey(targetBlock) : null;
  if (!targetBlock || !childKey) return { nextBlocks: blocks, nextSelected: selected, openPath: null as string | null };

  const moving = selectedIndices.map((index) => list[index]).filter((block): block is XbloxBlock => Boolean(block));
  const targetPath = itemPath(listPath, targetIndex);
  const childListPath = `${targetPath}/${childKey}`;
  if (moving.some((block) => !canInsertIntoList(blocks, childListPath, block))) {
    return { nextBlocks: blocks, nextSelected: selected, openPath: null as string | null };
  }

  const nextSourceList = list.filter((_, index) => !selectedIndices.includes(index));
  let nextBlocks = setListAtPath(blocks, listPath, nextSourceList);
  const adjustedTargetIndex = selectedIndices.some((index) => index < targetIndex) ? activeIndex - selectedIndices.filter((index) => index < targetIndex).length : targetIndex;
  const openPath = itemPath(listPath, adjustedTargetIndex);
  const adjustedChildListPath = `${openPath}/${childKey}`;
  const insertIndex = getListAtPath(nextBlocks, adjustedChildListPath).length;
  if (moving.some((block) => !canInsertIntoList(nextBlocks, adjustedChildListPath, block))) {
    return { nextBlocks: blocks, nextSelected: selected, openPath: null as string | null };
  }

  nextBlocks = insertManyIntoList(nextBlocks, adjustedChildListPath, insertIndex, moving);
  const nextSelected = new Set(moving.map((_, offset) => itemPath(adjustedChildListPath, insertIndex + offset)));
  return { nextBlocks, nextSelected, openPath };
}
