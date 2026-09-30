import React, { useState, useEffect, useRef, useMemo, useCallback } from 'react';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Type as TypeIcon, Hash, ToggleLeft, Braces, ChevronRight, Home, List, Search, Copy, Check } from 'lucide-react';
import { cn } from '@/lib/utils';

// ─── Types ────────────────────────────────────────────────────────────────────

export interface NavDisplayItem {
    key: string;
    value: any;
    displayType: string;
    displayValue: string;
    canDrillIn: boolean;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

/**
 * Build clipboard text without using JSON.stringify on the original graph.
 * Default JSON.stringify invokes value.toJSON() when present, which often drops
 * nested fields (OpenAI-style responses, axios-like objects, etc.). We walk
 * own-enumerable properties only and clone into plain JSON data first.
 */
export function serializeForClipboard(value: unknown): string {
    if (value === undefined) return 'undefined';
    if (value === null) return 'null';
    const t = typeof value;
    if (t === 'string') return value as string;
    if (t === 'number' || t === 'boolean') return String(value);
    if (t === 'bigint') return (value as bigint).toString();
    if (t === 'symbol') return String(value);
    if (t === 'function') return String(value);

    const seen = new WeakSet<object>();

    function cloneForJson(v: any): any {
        if (v === null) return v;
        if (typeof v === 'bigint') return v.toString();
        if (typeof v !== 'object') return v;
        if (v instanceof Date) return v.toISOString();
        if (ArrayBuffer.isView(v)) {
            try {
                return Array.from(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
            } catch {
                return '[Binary]';
            }
        }
        if (v instanceof Error) {
            const o: Record<string, unknown> = {
                name: v.name,
                message: v.message,
                stack: v.stack,
            };
            const c = (v as Error & { cause?: unknown }).cause;
            if (c !== undefined) o.cause = cloneForJson(c);
            return o;
        }
        if (Array.isArray(v)) {
            if (seen.has(v)) return '[Circular]';
            seen.add(v);
            return v.map(cloneForJson);
        }
        if (seen.has(v)) return '[Circular]';
        seen.add(v);
        const out: Record<string, unknown> = {};
        for (const key of Object.keys(v)) {
            try {
                out[key] = cloneForJson(v[key]);
            } catch {
                out[key] = '[Unserializable]';
            }
        }
        return out;
    }

    const cloned = cloneForJson(value);
    if (typeof cloned === 'string') return cloned;
    return JSON.stringify(cloned, null, 2);
}

function itemValueToText(value: any): string {
    return serializeForClipboard(value);
}

function useCopyValue() {
    const [copiedKey, setCopiedKey] = useState<string | null>(null);
    const copy = useCallback((key: string, value: any) => {
        const text = itemValueToText(value);
        navigator.clipboard.writeText(text).catch(() => {
            const el = document.createElement('textarea');
            el.value = text;
            el.style.cssText = 'position:fixed;opacity:0';
            document.body.appendChild(el);
            el.select();
            document.execCommand('copy');
            document.body.removeChild(el);
        });
        setCopiedKey(key);
        setTimeout(() => setCopiedKey(k => k === key ? null : k), 1200);
    }, []);
    return { copiedKey, copy };
}

function getIconForType(type: string) {
    switch (type) {
        case 'number': return Hash;
        case 'boolean': return ToggleLeft;
        case 'json':
        case 'object':
        case 'array': return Braces;
        default: return TypeIcon;
    }
}

/** Walk `data` along `path` segments and return the value at that node (or undefined). */
export function getValueAtPath(data: any, path: string[]): unknown {
    let obj = data;
    for (const seg of path) {
        if (obj == null || typeof obj !== 'object') return undefined;
        obj = Array.isArray(obj) ? obj[Number(seg)] : (obj as Record<string, unknown>)[seg];
    }
    return obj;
}

export function resolveNavItems(data: any, path: string[]): NavDisplayItem[] | null {
    try {
        let obj = data;
        for (const seg of path) {
            if (obj == null || typeof obj !== 'object') return null;
            obj = Array.isArray(obj) ? obj[Number(seg)] : obj[seg];
        }
        if (obj == null || typeof obj !== 'object') return null;
        return Object.entries(obj).map(([key, value]) => {
            const isObj = value !== null && typeof value === 'object';
            const isArr = Array.isArray(value);
            let displayType: string = typeof value;
            if (value === null) displayType = 'null';
            else if (isArr) displayType = 'array';
            else if (isObj) displayType = 'object';
            const displayValue = isArr
                ? `[${(value as any[]).length} items]`
                : isObj
                    ? `{${Object.keys(value as object).length} keys}`
                    : String(value);
            return { key, value, displayType, displayValue, canDrillIn: isObj };
        });
    } catch {
        return null;
    }
}

// ─── NavItemRow ───────────────────────────────────────────────────────────────

const NavItemRow = React.forwardRef<HTMLDivElement, {
    item: NavDisplayItem;
    isSelected: boolean;
    isFocused: boolean;
    isCopied: boolean;
    onClick: (e: React.MouseEvent) => void;
    onDrillIn: () => void;
    onCopy: (key: string) => void;
}>(({ item, isSelected, isFocused, isCopied, onClick, onDrillIn, onCopy }, ref) => {
    const iconType = item.canDrillIn ? 'json' : item.displayType === 'number' ? 'number' : item.displayType === 'boolean' ? 'boolean' : 'string';
    const Icon = getIconForType(iconType);
    return (
        <div
            ref={ref}
            data-nav-key={item.key}
            onClick={(e) => { e.stopPropagation(); onClick(e); }}
            onDoubleClick={item.canDrillIn ? onDrillIn : undefined}
            className={cn(
                "p-3 border rounded-md mb-2 cursor-pointer flex items-center justify-between group transition-colors select-none",
                isFocused && isSelected && "ring-2 ring-primary border-primary bg-primary/5",
                isSelected && !isFocused && "bg-primary/5 border-primary/30",
                isFocused && !isSelected && "ring-2 ring-primary/50 border-primary/30 bg-background",
                !isSelected && !isFocused && "hover:border-primary/50 bg-background",
            )}
        >
            <div className="flex items-center gap-3 flex-1 min-w-0">
                <Icon className="h-4 w-4 text-muted-foreground flex-shrink-0" />
                <div className="flex flex-col flex-1 min-w-0">
                    <span className="text-sm font-medium truncate font-mono">{item.key}</span>
                    <div className="flex items-center gap-2">
                        <span className="text-xs text-muted-foreground bg-muted px-1 rounded">{item.displayType}</span>
                        <span className="text-xs text-muted-foreground truncate opacity-70">{item.displayValue}</span>
                    </div>
                </div>
            </div>
            <div className="flex items-center gap-1.5 flex-shrink-0">
                <button
                    onPointerDown={(e) => { e.stopPropagation(); e.preventDefault(); }}
                    onClick={(e) => { e.stopPropagation(); onCopy(item.key); }}
                    title="Copy value"
                    className="invisible group-hover:visible text-muted-foreground hover:text-foreground transition-colors"
                >
                    {isCopied
                        ? <Check className="h-3.5 w-3.5 text-green-500" />
                        : <Copy className="h-3.5 w-3.5" />
                    }
                </button>
                {item.canDrillIn && (
                    <ChevronRight className="h-4 w-4 text-muted-foreground opacity-40 group-hover:opacity-80 transition-opacity" />
                )}
            </div>
        </div>
    );
});
NavItemRow.displayName = 'NavItemRow';

// ─── DetailPane ───────────────────────────────────────────────────────────────

const DetailPane = ({ item, resolvedValue, selectedCount }: { item: NavDisplayItem | null; resolvedValue: unknown; selectedCount: number }) => {
    const [copied, setCopied] = useState(false);

    const handleCopy = useCallback((e: React.MouseEvent) => {
        e.stopPropagation();
        if (!item) return;
        const text = serializeForClipboard(resolvedValue);
        navigator.clipboard.writeText(text).catch(() => {
            const el = document.createElement('textarea');
            el.value = text;
            el.style.cssText = 'position:fixed;opacity:0';
            document.body.appendChild(el);
            el.select();
            document.execCommand('copy');
            document.body.removeChild(el);
        });
        setCopied(true);
        setTimeout(() => setCopied(false), 1200);
    }, [item, resolvedValue]);

    if (!item) return (
        <div className="h-full flex flex-col items-center justify-center text-muted-foreground p-4 text-center opacity-60">
            <List className="h-8 w-8 mb-2 stroke-1" />
            <p className="text-sm">Select an item to inspect</p>
            <p className="text-xs text-muted-foreground/60 mt-1">↑ ↓ navigate · Shift extend · Ctrl+A all · Ctrl+C copy</p>
        </div>
    );

    return (
        <div className="space-y-4">
            {selectedCount > 1 && (
                <div className="text-xs text-primary bg-primary/5 border border-primary/20 rounded px-2 py-1">
                    {selectedCount} items selected · Ctrl+C to copy all
                </div>
            )}
            <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Key</Label>
                <div className="text-sm font-mono font-medium bg-muted/30 px-2 py-1.5 rounded border">{item.key}</div>
            </div>
            <div className="space-y-1">
                <Label className="text-xs text-muted-foreground">Type</Label>
                <div className="text-xs bg-muted px-2 py-1 rounded inline-block">{item.displayType}</div>
            </div>
            <div className="space-y-1">
                <div className="flex items-center justify-between mb-1">
                    <Label className="text-xs text-muted-foreground">Value</Label>
                    <button
                        onClick={handleCopy}
                        title="Copy value"
                        className="text-muted-foreground hover:text-foreground transition-colors"
                    >
                        {copied
                            ? <Check className="h-3.5 w-3.5 text-green-500" />
                            : <Copy className="h-3.5 w-3.5" />
                        }
                    </button>
                </div>
                {item.canDrillIn ? (
                    <pre className="text-xs font-mono bg-muted/20 border rounded p-2 max-h-[300px] overflow-auto whitespace-pre-wrap">
                        {serializeForClipboard(resolvedValue)}
                    </pre>
                ) : (
                    <div className="text-sm font-mono bg-muted/30 px-2 py-1.5 rounded border break-all">
                        {String(resolvedValue)}
                    </div>
                )}
            </div>
        </div>
    );
};

// ─── Main Component ───────────────────────────────────────────────────────────

export interface JSONTreeWalkerProps {
    /** The JSON data to inspect. Can be an object, array, or any JSON-serializable value. */
    data: any;
    /** Optional label shown in the breadcrumb root. */
    rootLabel?: string;
    /** Extra CSS class on the outer wrapper. */
    className?: string;
    /** Hide the inspector sidebar and tighten spacing for narrow embedded panels. */
    compact?: boolean;
    /** Called when exiting from the root level (Escape or ArrowLeft at root). */
    onExit?: () => void;
}

function compactType(value: unknown) {
    if (value === null) return 'null';
    if (Array.isArray(value)) return 'array';
    return typeof value;
}

function compactSummary(value: unknown) {
    if (value === null) return 'null';
    if (Array.isArray(value)) return `[${value.length}]`;
    if (typeof value === 'object') return `{${Object.keys(value as Record<string, unknown>).length}}`;
    if (typeof value === 'string') return value;
    return String(value);
}

type CompactNode = {
    name: string;
    value: unknown;
    depth: number;
    path: string;
    segments: string[];
};

function compactPath(segments: string[]) {
    return segments.length ? `$.${segments.join('.')}` : '$';
}

function flattenCompactNodes(value: unknown, rootLabel: string, expanded: Set<string>) {
    const nodes: CompactNode[] = [];
    const walk = (name: string, nodeValue: unknown, depth: number, segments: string[]) => {
        const path = compactPath(segments);
        nodes.push({ name, value: nodeValue, depth, path, segments });
        if (nodeValue === null || typeof nodeValue !== 'object' || !expanded.has(path)) return;
        Object.entries(nodeValue as Record<string, unknown>).forEach(([key, child]) => {
            walk(key, child, depth + 1, [...segments, key]);
        });
    };
    walk(rootLabel, value, 0, []);
    return nodes;
}

function jqSegment(segment: string) {
    if (/^\d+$/.test(segment)) return `[${segment}]`;
    if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(segment)) return `.${segment}`;
    return `[${JSON.stringify(segment)}]`;
}

function jqPath(segments: string[]) {
    return segments.length ? segments.map(jqSegment).join('') : '.';
}

function CompactJSONNode({
    name,
    value,
    depth,
    path,
    segments,
    expanded,
    selectedPath,
    toggle,
    select,
}: {
    name: string;
    value: unknown;
    depth: number;
    path: string;
    segments: string[];
    expanded: Set<string>;
    selectedPath: string;
    toggle: (path: string) => void;
    select: (path: string) => void;
}) {
    const canExpand = value !== null && typeof value === 'object';
    const isOpen = expanded.has(path);
    const entries = canExpand ? Object.entries(value as Record<string, unknown>) : [];
    return (
        <div className="json-tree-compact-node">
            <button
                type="button"
                className={cn("json-tree-compact-row", canExpand && "is-expandable", selectedPath === path && "is-selected")}
                style={{ paddingLeft: 4 + depth * 12 }}
                data-compact-json-path={path}
                onClick={() => {
                    select(path);
                    if (canExpand) toggle(path);
                }}
                title={canExpand ? "Expand/collapse" : undefined}
            >
                <span className="json-tree-compact-caret">{canExpand ? (isOpen ? "v" : ">") : ""}</span>
                <span className="json-tree-compact-key">{name}</span>
                <span className={`json-tree-compact-type is-${compactType(value)}`}>{compactType(value)}</span>
                <span className="json-tree-compact-value">{compactSummary(value)}</span>
            </button>
            {canExpand && isOpen ? (
                <div className="json-tree-compact-children">
                    {entries.map(([key, child]) => (
                        <CompactJSONNode
                            key={`${path}.${key}`}
                            name={key}
                            value={child}
                            depth={depth + 1}
                            path={compactPath([...segments, key])}
                            segments={[...segments, key]}
                            expanded={expanded}
                            selectedPath={selectedPath}
                            toggle={toggle}
                            select={select}
                        />
                    ))}
                </div>
            ) : null}
        </div>
    );
}

function CompactJSONTreeWalker({ data, rootLabel = 'Root', className }: JSONTreeWalkerProps) {
    const [expanded, setExpanded] = useState<Set<string>>(() => new Set(['$']));
    const [selectedPath, setSelectedPath] = useState('$');
    const compactRef = useRef<HTMLDivElement>(null);
    const parsedData = useMemo(() => {
        if (typeof data === 'string') {
            try { return JSON.parse(data); } catch { return data; }
        }
        return data;
    }, [data]);
    const visibleNodes = useMemo(() => flattenCompactNodes(parsedData, rootLabel, expanded), [expanded, parsedData, rootLabel]);
    const selectedNode = visibleNodes.find(node => node.path === selectedPath) ?? visibleNodes[0];
    const selectedJqPath = jqPath([rootLabel, ...(selectedNode?.segments ?? [])]);
    const toggle = useCallback((path: string) => {
        setExpanded(current => {
            const next = new Set(current);
            if (next.has(path)) next.delete(path);
            else next.add(path);
            return next;
        });
    }, []);
    const select = useCallback((path: string) => {
        setSelectedPath(path);
    }, []);
    const copySelectedPath = useCallback(() => {
        navigator.clipboard.writeText(selectedJqPath).catch(() => {
            const el = document.createElement('textarea');
            el.value = selectedJqPath;
            el.style.cssText = 'position:fixed;opacity:0';
            document.body.appendChild(el);
            el.select();
            document.execCommand('copy');
            document.body.removeChild(el);
        });
    }, [selectedJqPath]);
    const selectByIndex = useCallback((index: number) => {
        const node = visibleNodes[Math.max(0, Math.min(index, visibleNodes.length - 1))];
        if (!node) return;
        setSelectedPath(node.path);
        requestAnimationFrame(() => {
            compactRef.current?.querySelector<HTMLElement>(`[data-compact-json-path="${CSS.escape(node.path)}"]`)?.scrollIntoView({ block: 'nearest' });
        });
    }, [visibleNodes]);
    const handleKeyDown = useCallback((event: React.KeyboardEvent<HTMLDivElement>) => {
        if (!visibleNodes.length) return;
        const index = Math.max(0, visibleNodes.findIndex(node => node.path === selectedPath));
        const current = visibleNodes[index];
        const canExpand = current?.value !== null && typeof current?.value === 'object';
        if (event.key === 'ArrowDown') {
            event.preventDefault();
            selectByIndex(index + 1);
        } else if (event.key === 'ArrowUp') {
            event.preventDefault();
            selectByIndex(index - 1);
        } else if (event.key === 'Home') {
            event.preventDefault();
            selectByIndex(0);
        } else if (event.key === 'End') {
            event.preventDefault();
            selectByIndex(visibleNodes.length - 1);
        } else if (event.key === 'Enter' || event.key === 'ArrowRight') {
            if (!current || !canExpand) return;
            event.preventDefault();
            if (!expanded.has(current.path)) toggle(current.path);
        } else if (event.key === 'ArrowLeft') {
            if (!current) return;
            event.preventDefault();
            if (canExpand && expanded.has(current.path)) {
                toggle(current.path);
                return;
            }
            const parentSegments = current.segments.slice(0, -1);
            selectByIndex(Math.max(0, visibleNodes.findIndex(node => node.path === compactPath(parentSegments))));
        } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'c') {
            event.preventDefault();
            copySelectedPath();
        }
    }, [copySelectedPath, expanded, selectByIndex, selectedPath, toggle, visibleNodes]);

    return (
        <div ref={compactRef} className={cn("json-tree-compact", className)} tabIndex={0} onKeyDown={handleKeyDown}>
            <CompactJSONNode
                name={rootLabel}
                value={parsedData}
                depth={0}
                path="$"
                segments={[]}
                expanded={expanded}
                selectedPath={selectedPath}
                toggle={toggle}
                select={select}
            />
            <div className="json-tree-compact-jq">
                <span title={selectedJqPath}>{selectedJqPath}</span>
                <button type="button" onClick={copySelectedPath} title="Copy jq selector">
                    <Copy aria-hidden="true" />
                </button>
            </div>
        </div>
    );
}

const FullJSONTreeWalker = ({ data, rootLabel = 'Root', className, onExit }: JSONTreeWalkerProps) => {
    const [path, setPath] = useState<string[]>([]);
    const [searchTerm, setSearchTerm] = useState('');
    const containerRef = useRef<HTMLDivElement>(null);
    const searchRef = useRef<HTMLInputElement>(null);
    const rowRefs = useRef<(HTMLDivElement | null)[]>([]);

    // ── Selection model (mirrors FileTree) ───────────────────────────────────
    const [focusIdx, setFocusIdx] = useState(0);
    const [selectedKeys, setSelectedKeys] = useState<Set<string>>(new Set());
    const anchorIdx = useRef<number>(0);
    // When navigating up (ArrowLeft), store the key to restore focus to after re-render
    const restoreKeyRef = useRef<string | null>(null);

    const { copiedKey, copy } = useCopyValue();

    // ── Data resolution ──────────────────────────────────────────────────────

    const parsedData = useMemo(() => {
        if (typeof data === 'string') {
            try { return JSON.parse(data); } catch { return data; }
        }
        return data;
    }, [data]);

    const navItems = useMemo(() => {
        if (parsedData == null || typeof parsedData !== 'object') return null;
        return resolveNavItems(parsedData, path);
    }, [parsedData, path]);

    const filteredItems = useMemo(() => {
        if (!navItems) return null;
        if (!searchTerm.trim()) return navItems;
        const term = searchTerm.toLowerCase();
        return navItems.filter(item =>
            item.key.toLowerCase().includes(term) ||
            item.displayValue.toLowerCase().includes(term)
        );
    }, [navItems, searchTerm]);

    // ── Sync: clamp focusIdx and restore after go-up ─────────────────────────

    useEffect(() => {
        if (!filteredItems) return;
        const count = filteredItems.length;

        if (restoreKeyRef.current) {
            const k = restoreKeyRef.current;
            restoreKeyRef.current = null;
            const idx = filteredItems.findIndex(i => i.key === k);
            if (idx >= 0) {
                setFocusIdx(idx);
                anchorIdx.current = idx;
                setSelectedKeys(new Set([k]));
                return;
            }
        }

        setFocusIdx(prev => (count === 0 ? 0 : Math.min(prev, count - 1)));
    }, [filteredItems]);

    // Reset selection when path changes (drilled in or backed out)
    useEffect(() => {
        setSelectedKeys(new Set());
        setFocusIdx(0);
        anchorIdx.current = 0;
        setSearchTerm('');
    }, [path]);

    // Scroll focused row into view
    useEffect(() => {
        rowRefs.current[focusIdx]?.scrollIntoView({ block: 'nearest' });
    }, [focusIdx]);

    // ── Selection helpers ────────────────────────────────────────────────────

    const selectItem = useCallback((idx: number) => {
        if (!filteredItems) return;
        const i = Math.max(0, Math.min(idx, filteredItems.length - 1));
        setFocusIdx(i);
        anchorIdx.current = i;
        const item = filteredItems[i];
        if (item) setSelectedKeys(new Set([item.key]));
    }, [filteredItems]);

    const toggleItem = useCallback((idx: number) => {
        if (!filteredItems) return;
        setFocusIdx(idx);
        anchorIdx.current = idx;
        const item = filteredItems[idx];
        if (!item) return;
        setSelectedKeys(prev => {
            const next = new Set(prev);
            if (next.has(item.key)) next.delete(item.key);
            else next.add(item.key);
            return next;
        });
    }, [filteredItems]);

    const rangeSelectTo = useCallback((idx: number) => {
        if (!filteredItems) return;
        setFocusIdx(idx);
        const start = Math.min(anchorIdx.current, idx);
        const end = Math.max(anchorIdx.current, idx);
        const keys = new Set<string>();
        for (let i = start; i <= end; i++) {
            const item = filteredItems[i];
            if (item) keys.add(item.key);
        }
        setSelectedKeys(keys);
    }, [filteredItems]);

    const handleRowClick = useCallback((idx: number, e: React.MouseEvent) => {
        if (e.ctrlKey || e.metaKey) toggleItem(idx);
        else if (e.shiftKey) rangeSelectTo(idx);
        else selectItem(idx);
        containerRef.current?.focus({ preventScroll: true });
    }, [selectItem, toggleItem, rangeSelectTo]);

    const drillIn = useCallback((item: NavDisplayItem) => {
        if (item.canDrillIn) {
            setPath(prev => [...prev, item.key]);
        }
    }, []);

    /** Copy uses root `parsedData` + path + key so clipboard matches the real subtree (not toJSON-truncated views). */
    const copyAtKey = useCallback((key: string) => {
        copy(key, getValueAtPath(parsedData, [...path, key]));
    }, [copy, parsedData, path]);

    // ── Keyboard ─────────────────────────────────────────────────────────────

    useEffect(() => {
        const el = containerRef.current;
        if (!el) return;
        const handleKeyDown = (e: KeyboardEvent) => {
            if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) {
                // Let Escape/Enter propagate out of search bar
                if (e.key === 'Escape') {
                    e.preventDefault();
                    setSearchTerm('');
                    containerRef.current?.focus();
                }
                return;
            }
            if (!filteredItems) return;
            const count = filteredItems.length;
            if (count === 0) return;

            switch (e.key) {
                case 'ArrowDown': {
                    e.preventDefault();
                    const next = Math.min(focusIdx + 1, count - 1);
                    if (e.shiftKey) rangeSelectTo(next);
                    else selectItem(next);
                    break;
                }
                case 'ArrowUp': {
                    e.preventDefault();
                    const prev = Math.max(focusIdx - 1, 0);
                    if (e.shiftKey) rangeSelectTo(prev);
                    else selectItem(prev);
                    break;
                }
                case 'Home': {
                    e.preventDefault();
                    if (e.shiftKey) rangeSelectTo(0);
                    else selectItem(0);
                    break;
                }
                case 'End': {
                    e.preventDefault();
                    if (e.shiftKey) rangeSelectTo(count - 1);
                    else selectItem(count - 1);
                    break;
                }
                case 'ArrowRight':
                case 'Enter': {
                    e.preventDefault();
                    const item = filteredItems[focusIdx];
                    if (item?.canDrillIn) drillIn(item);
                    break;
                }
                case 'ArrowLeft': {
                    e.preventDefault();
                    if (path.length > 0) {
                        restoreKeyRef.current = path[path.length - 1];
                        setPath(prev => prev.slice(0, -1));
                    } else {
                        onExit?.();
                    }
                    break;
                }
                case 'Escape': {
                    e.preventDefault();
                    if (searchTerm) {
                        setSearchTerm('');
                    } else if (selectedKeys.size > 1) {
                        const item = filteredItems[focusIdx];
                        if (item) setSelectedKeys(new Set([item.key]));
                    } else if (path.length > 0) {
                        restoreKeyRef.current = path[path.length - 1];
                        setPath([]);
                    } else {
                        onExit?.();
                    }
                    break;
                }
                case 'a': {
                    if (e.ctrlKey || e.metaKey) {
                        e.preventDefault();
                        const all = new Set<string>();
                        filteredItems.forEach(item => all.add(item.key));
                        setSelectedKeys(all);
                    }
                    break;
                }
                case 'c': {
                    if (e.ctrlKey || e.metaKey) {
                        e.preventDefault();
                        const selected = filteredItems.filter(i => selectedKeys.has(i.key));
                        if (selected.length === 0) {
                            const item = filteredItems[focusIdx];
                            if (item) copy(item.key, getValueAtPath(parsedData, [...path, item.key]));
                        } else if (selected.length === 1) {
                            const i0 = selected[0];
                            copy(i0.key, getValueAtPath(parsedData, [...path, i0.key]));
                        } else {
                            const obj: Record<string, unknown> = {};
                            selected.forEach(i => { obj[i.key] = getValueAtPath(parsedData, [...path, i.key]); });
                            copy('__multi__', obj);
                        }
                    }
                    break;
                }
                case '/':
                case 'f': {
                    if (e.key === '/' || (e.key === 'f' && (e.ctrlKey || e.metaKey))) {
                        e.preventDefault();
                        searchRef.current?.focus();
                    }
                    break;
                }
            }
        };
        el.addEventListener('keydown', handleKeyDown);
        return () => el.removeEventListener('keydown', handleKeyDown);
    }, [filteredItems, focusIdx, path, onExit, selectedKeys, searchTerm, selectItem, rangeSelectTo, drillIn, copy, parsedData]);

    // ── Detail pane item ─────────────────────────────────────────────────────

    // Always track the focused row for the detail pane (independent of selection set)
    const focusedItem = filteredItems?.[focusIdx] ?? null;
    const focusedResolvedValue = focusedItem
        ? getValueAtPath(parsedData, [...path, focusedItem.key])
        : undefined;

    // ── Primitive data passthrough ────────────────────────────────────────────

    if (parsedData == null || typeof parsedData !== 'object') {
        return (
            <div className={cn("flex flex-col h-full border rounded-lg overflow-hidden bg-background", className)}>
                <div className={cn("flex-1 flex items-center justify-center", compact ? "p-2" : "p-4")}>
                    <pre className="text-sm font-mono bg-muted/20 border rounded p-4 whitespace-pre-wrap max-w-full overflow-auto">
                        {String(parsedData)}
                    </pre>
                </div>
            </div>
        );
    }

    return (
        <div className={cn("flex flex-col h-full border rounded-lg overflow-hidden bg-background", className)}>

            {/* Search bar */}
            <div className="flex items-center gap-2 p-2 px-3 border-b bg-muted/20">
                <Search className="h-3.5 w-3.5 text-muted-foreground" />
                <Input
                    ref={searchRef}
                    placeholder="Filter keys… (/ to focus)"
                    className="h-7 text-xs bg-background flex-1"
                    value={searchTerm}
                    onChange={e => setSearchTerm(e.target.value)}
                />
                {searchTerm && (
                    <span className="text-[10px] text-muted-foreground/60 tabular-nums">
                        {filteredItems?.length ?? 0}
                    </span>
                )}
            </div>

            {/* Breadcrumb */}
            <div className={cn("flex items-center gap-1 py-1.5 bg-muted/40 border-b text-xs font-medium select-none", compact ? "px-2" : "px-4")}>
                <button
                    onClick={() => {
                        if (path.length > 0) {
                            restoreKeyRef.current = path[0];
                            setPath([]);
                        }
                    }}
                    className={cn(
                        "flex items-center gap-1 px-1.5 py-0.5 rounded transition-colors",
                        path.length === 0
                            ? "bg-primary/10 text-primary font-semibold"
                            : "hover:bg-muted text-muted-foreground hover:text-foreground"
                    )}
                >
                    <Home className="h-3 w-3" />
                    <span>{rootLabel}</span>
                </button>
                {path.map((seg, i) => (
                    <React.Fragment key={i}>
                        <ChevronRight className="h-3 w-3 text-muted-foreground/50" />
                        <button
                            onClick={() => {
                                const targetPath = path.slice(0, i + 1);
                                restoreKeyRef.current = path[i + 1] ?? null;
                                setPath(targetPath);
                            }}
                            className={cn(
                                "px-1.5 py-0.5 rounded transition-colors font-mono",
                                i === path.length - 1
                                    ? "bg-primary/10 text-primary font-semibold"
                                    : "hover:bg-muted text-muted-foreground hover:text-foreground"
                            )}
                        >
                            {seg}
                        </button>
                    </React.Fragment>
                ))}
                <div className="flex-1" />
                {!compact ? (
                    <span className="text-muted-foreground/50 text-[10px] uppercase tracking-wider">
                        ↑ ↓ · Shift range · Ctrl+A all · Ctrl+C copy
                    </span>
                ) : null}
            </div>

            {/* Body: items list + detail pane */}
            <div className="flex flex-1 min-h-0">
                {/* Items list */}
                <div
                    ref={containerRef}
                    tabIndex={0}
                    className={cn("flex-1 overflow-y-auto bg-muted/5 relative outline-none focus:ring-1 focus:ring-primary/20 focus:ring-inset", compact ? "p-2" : "p-4")}
                    onClick={() => {
                        // Click on empty area clears selection
                        setSelectedKeys(new Set());
                    }}
                >
                    {filteredItems && filteredItems.length > 0 ? (
                        <div className={compact ? "space-y-1" : "space-y-2"}>
                            {filteredItems.map((item, idx) => (
                                <NavItemRow
                                    key={item.key}
                                    ref={(el) => { rowRefs.current[idx] = el; }}
                                    item={item}
                                    isSelected={selectedKeys.has(item.key)}
                                    isFocused={focusIdx === idx}
                                    isCopied={copiedKey === item.key}
                                    onClick={(e) => handleRowClick(idx, e)}
                                    onDrillIn={() => drillIn(item)}
                                    onCopy={copyAtKey}
                                />
                            ))}
                        </div>
                    ) : (
                        <div className="h-full flex flex-col items-center justify-center text-muted-foreground border-2 border-dashed rounded-lg opacity-50 m-4 min-h-[200px]">
                            <Braces className="h-8 w-8 mb-2 stroke-1" />
                            <p className="text-sm">{searchTerm ? 'No matches' : 'Empty object'}</p>
                        </div>
                    )}
                </div>

                {!compact ? (
                    <div className="w-[260px] border-l flex flex-col bg-background shrink-0">
                        <div className="p-3 border-b bg-muted/20">
                            <span className="text-xs font-semibold text-muted-foreground uppercase tracking-wider">Inspector</span>
                        </div>
                        <div className="flex-1 p-4 overflow-y-auto">
                            <DetailPane
                                item={focusedItem}
                                resolvedValue={focusedResolvedValue}
                                selectedCount={selectedKeys.size}
                            />
                        </div>
                    </div>
                ) : null}
            </div>
        </div>
    );
};

export const JSONTreeWalker = (props: JSONTreeWalkerProps) => (
    props.compact ? <CompactJSONTreeWalker {...props} /> : <FullJSONTreeWalker {...props} />
);

export default JSONTreeWalker;
