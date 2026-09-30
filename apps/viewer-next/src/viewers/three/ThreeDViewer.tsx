import React, { Suspense, useEffect, useLayoutEffect, useMemo, useState, useRef } from 'react';
import { Canvas, useFrame, useThree } from '@react-three/fiber';
import { OrbitControls, Bounds, Center, useBounds, GizmoHelper, GizmoViewcube } from '@react-three/drei';
import { Loader2, Cuboid, Grid3x3, BoxSelect, MousePointer2, Move, ZoomIn, Maximize, Minimize, ListTree, ChevronRight, ChevronDown, Eye, EyeOff, Lightbulb, SlidersHorizontal, RefreshCw } from 'lucide-react';
import type { ViewerWebStatus } from '@/bridge/hostBridge';
import { onHostMessage, postToHost } from '@/bridge/hostBridge';
import { fetchHostedArrayBuffer } from '@/bridge/hostedFileFetch';
import * as THREE from 'three';
// @ts-ignore
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
// @ts-ignore
// @ts-ignore
import { OBJLoader } from 'three/examples/jsm/loaders/OBJLoader.js';
// @ts-ignore
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
// @ts-ignore
import { PLYLoader } from 'three/examples/jsm/loaders/PLYLoader.js';
import occtimportjs from 'occt-import-js';
import wasmUrl from 'occt-import-js/dist/occt-import-js.wasm';
import { DxfViewer as NativeDxfViewer } from 'dxf-viewer';

interface ThreeDViewerInnerProps {
    url: string;
    fileName?: string;
    viewerKind?: string;
    openscadSourceText?: string;
}

type ParsedParamType = 'number' | 'boolean' | 'string';
type ParsedParam = {
    name: string;
    label: string;
    description?: string;
    group: string;
    type: ParsedParamType;
    value: number | boolean | string;
    min?: number;
    max?: number;
    step?: number;
};

function useModelGeometry(url: string, extension: string) {
    const [geometry, setGeometry] = useState<THREE.BufferGeometry | THREE.Group | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [isLoading, setIsLoading] = useState<boolean>(true);

    useEffect(() => {
        let active = true;
        const controller = new AbortController();
        setIsLoading(true);
        setError(null);
        setGeometry(null);

        const load = async () => {
            try {
                // Determine format
                const ext = extension.toLowerCase();

                // DXF is drawn by `DxfNativeLayer` (separate viewer); skip fetch here so we do not set `error`.
                if (ext === 'dxf') {
                    if (active) {
                        setGeometry(null);
                        setError(null);
                        setIsLoading(false);
                    }
                    return;
                }

                if (ext === 'step' || ext === 'stp') {
                    // OpenCASCADE WASM parser for parametric boundary files
                    const fileBuffer = await fetchHostedArrayBuffer(url, controller.signal);
                    if (!active) return;

                    const occt = await occtimportjs({
                        locateFile: () => wasmUrl
                    });

                    const result = occt.ReadStepFile(new Uint8Array(fileBuffer), null);

                    if (result && result.meshes) {
                        const group = new THREE.Group();
                        for (let resultMesh of result.meshes) {
                            const geometry = new THREE.BufferGeometry();
                            geometry.setAttribute('position', new THREE.Float32BufferAttribute(resultMesh.attributes.position.array, 3));
                            if (resultMesh.attributes.normal) {
                                geometry.setAttribute('normal', new THREE.Float32BufferAttribute(resultMesh.attributes.normal.array, 3));
                            }
                            const index = Uint16Array.from(resultMesh.index.array);
                            geometry.setIndex(new THREE.BufferAttribute(index, 1));

                            geometry.computeBoundingBox();
                            geometry.computeBoundingSphere();

                            const matColor = resultMesh.color ? new THREE.Color(resultMesh.color[0], resultMesh.color[1], resultMesh.color[2]) : new THREE.Color('#cccccc');
                            group.add(new THREE.Mesh(geometry, new THREE.MeshPhongMaterial({
                                color: matColor
                            })));
                        }
                        if (active) setGeometry(group);
                    } else {
                        throw new Error("Empty step file / WASM conversion failed");
                    }
                } else if (ext === 'stl' || ext === 'obj' || ext === 'gltf' || ext === 'glb' || ext === 'ply') {
                    const buf = await fetchHostedArrayBuffer(url, controller.signal);
                    const objectUrl = URL.createObjectURL(new Blob([buf], { type: 'application/octet-stream' }));
                    try {
                        if (ext === 'stl') {
                            const loader = new STLLoader();
                            const geo = await loader.loadAsync(objectUrl);
                            if (!active) return;
                            geo.computeVertexNormals();
                            geo.computeBoundingBox();
                            geo.computeBoundingSphere();
                            if (active) setGeometry(geo);
                        } else if (ext === 'obj') {
                            const loader = new OBJLoader();
                            const obj = await loader.loadAsync(objectUrl);
                            if (!active) return;
                            obj.traverse((child: any) => {
                                if (child.isMesh && child.geometry) {
                                    child.geometry.computeVertexNormals();
                                    child.geometry.computeBoundingBox();
                                    child.geometry.computeBoundingSphere();
                                }
                            });
                            if (active) setGeometry(obj);
                        } else if (ext === 'gltf' || ext === 'glb') {
                            const loader = new GLTFLoader();
                            const gltf = await loader.loadAsync(objectUrl);
                            if (!active) return;
                            gltf.scene.traverse((child: any) => {
                                if (child.isMesh && child.geometry) {
                                    child.geometry.computeVertexNormals();
                                    child.geometry.computeBoundingBox();
                                    child.geometry.computeBoundingSphere();
                                }
                            });
                            if (active) setGeometry(gltf.scene);
                        } else {
                            const loader = new PLYLoader();
                            const geo = await loader.loadAsync(objectUrl);
                            if (!active) return;
                            geo.computeVertexNormals();
                            geo.computeBoundingBox();
                            geo.computeBoundingSphere();
                            if (active) setGeometry(geo);
                        }
                    } finally {
                        URL.revokeObjectURL(objectUrl);
                    }
                } else {
                    if (active) setError(`Unsupported 3D format: ${ext}`);
                }
            } catch (err: any) {
                if (err.name === 'AbortError') return;
                console.error("3D Load Error:", err);
                if (active) setError(err.message);
            } finally {
                if (active) setIsLoading(false);
            }
        };
        load();
        return () => {
            active = false;
            controller.abort();
        };
    }, [url, extension]);

    return { geometry, error, isLoading };
}

// -- Dynamic Headlight ------------------------------------
const Headlight: React.FC<{ enabled: boolean }> = ({ enabled }) => {
    const lightRef = useRef<THREE.SpotLight>(null);
    const { camera } = useThree();

    useFrame(() => {
        if (lightRef.current && enabled) {
            lightRef.current.position.copy(camera.position);
        }
    });

    if (!enabled) return null;

    return <spotLight ref={lightRef} intensity={2} angle={Math.PI / 4} penumbra={0.5} decay={1} distance={1000} castShadow />;
};

// -- Model Renderer ---------------------------------------
const ModelRenderer: React.FC<{ geometry: THREE.BufferGeometry | THREE.Group | null; error: string | null; renderMode: 'solid' | 'wireframe' | 'edges'; hiddenNodes: Set<string> }> = ({ geometry, error, renderMode, hiddenNodes }) => {
    const bounds = useBounds();
    const groupRef = useRef<THREE.Group>(null);

    useEffect(() => {
        if (geometry) {
            const timer = setTimeout(() => {
                bounds.refresh().clip().fit();
            }, 50);
            return () => clearTimeout(timer);
        }
    }, [geometry, bounds]);

    useEffect(() => {
        if (!geometry || !groupRef.current) return;

        // Clean up previously spawned edges
        const childrenToRemove: THREE.Object3D[] = [];
        groupRef.current.traverse((child) => {
            if (child.userData.isEdgeOverlay) {
                childrenToRemove.push(child);
            }
        });
        childrenToRemove.forEach(edgeObj => {
            if (edgeObj.parent) edgeObj.parent.remove(edgeObj);
        });

        // Apply materials and spawn new edges
        const rootObj = geometry instanceof THREE.BufferGeometry ? groupRef.current : geometry;

        rootObj.traverse((child: any) => {
            // Apply visibility from tree mapping
            if (hiddenNodes.has(child.uuid)) {
                child.visible = false;
            } else {
                child.visible = true;
            }

            if (child.isMesh && child.material) {
                child.material.wireframe = (renderMode === 'wireframe');
                child.material.needsUpdate = true;

                if (renderMode === 'edges') {
                    const edgesGeo = new THREE.EdgesGeometry(child.geometry, 30);
                    const edgesMaterial = new THREE.LineBasicMaterial({ color: 0x000000, linewidth: 1 });
                    const edgeLines = new THREE.LineSegments(edgesGeo, edgesMaterial);
                    edgeLines.userData.isEdgeOverlay = true;
                    // match transform
                    edgeLines.position.copy(child.position);
                    edgeLines.rotation.copy(child.rotation);
                    edgeLines.scale.copy(child.scale);

                    if (child.parent) {
                        child.parent.add(edgeLines);
                    } else {
                        rootObj.add(edgeLines);
                    }
                }
            }
        });
    }, [geometry, renderMode, hiddenNodes]);

    if (error) {
        return (
            <mesh>
                <boxGeometry args={[1, 1, 1]} />
                <meshStandardMaterial color="#ef4444" wireframe />
            </mesh>
        );
    }

    if (!geometry) {
        return null;
    }

    return (
        <group
            ref={groupRef}
            dispose={null}
            onDoubleClick={(e) => {
                e.stopPropagation();
                // If they click on an edge overlay, zoom to its parent mesh instead
                const target = e.object.userData.isEdgeOverlay && e.object.parent ? e.object.parent : e.object;
                bounds.refresh(target).clip().fit();
            }}
        >
            {geometry instanceof THREE.BufferGeometry ? (
                <mesh geometry={geometry}>
                    <meshStandardMaterial
                        color="#888888"
                        roughness={0.5}
                        metalness={0.5}
                    />
                </mesh>
            ) : (
                <primitive object={geometry} />
            )}
        </group>
    );
};

// -- Model Tree ---------------------------------------
const ModelTreeNode: React.FC<{ node: THREE.Object3D; depth: number; hiddenNodes: Set<string>; toggleNode: (uuid: string) => void }> = ({ node, depth, hiddenNodes, toggleNode }) => {
    const [expanded, setExpanded] = useState(depth < 2);
    const isHidden = hiddenNodes.has(node.uuid);
    const hasChildren = node.children && node.children.length > 0 && !node.children.every(c => c.userData.isEdgeOverlay);

    if (node.userData.isEdgeOverlay) return null;

    return (
        <div style={{ display: 'flex', flexDirection: 'column', fontSize: 12 }}>
            <div style={{
                display: 'flex', alignItems: 'center', padding: '4px 8px',
                paddingLeft: `${depth * 12 + 8}px`,
                borderBottom: '1px solid var(--border)',
                userSelect: 'none'
            }}>
                <div
                    onClick={() => setExpanded(!expanded)}
                    style={{ width: 16, height: 16, display: 'flex', alignItems: 'center', justifyContent: 'center', cursor: hasChildren ? 'pointer' : 'default', opacity: hasChildren ? 1 : 0, marginRight: 4 }}
                >
                    {expanded ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                </div>
                <div style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', opacity: isHidden ? 0.5 : 1 }}>
                    {node.name || node.type}
                </div>
                <div
                    onClick={() => toggleNode(node.uuid)}
                    style={{ cursor: 'pointer', padding: 2, opacity: isHidden ? 0.5 : 1 }}
                >
                    {isHidden ? <EyeOff size={14} /> : <Eye size={14} />}
                </div>
            </div>
            {expanded && hasChildren && node.children.map(child => (
                <ModelTreeNode key={child.uuid} node={child} depth={depth + 1} hiddenNodes={hiddenNodes} toggleNode={toggleNode} />
            ))}
        </div>
    );
};

// -- DXF Native Layer ---------------------------------------
const DxfNativeLayer: React.FC<{ url: string }> = ({ url }) => {
    const dxfRef = useRef<HTMLDivElement>(null);
    const [isLoading, setIsLoading] = useState(true);

    // useLayoutEffect: ref must exist before `DxfViewer` measures the host (useEffect can see null in StrictMode).
    useLayoutEffect(() => {
        const el = dxfRef.current;
        if (!el) return;

        let viewer: NativeDxfViewer | null = null;
        let objectUrl: string | null = null;
        let active = true;
        const ac = new AbortController();
        setIsLoading(true);

        const isDark = document.documentElement.classList.contains('dark');
        const clearColor = new THREE.Color(isDark ? '#020817' : '#ffffff');

        (async () => {
            try {
                const buf = await fetchHostedArrayBuffer(url, ac.signal);
                if (!active) return;
                objectUrl = URL.createObjectURL(new Blob([buf], { type: 'application/octet-stream' }));
                if (!active) {
                    URL.revokeObjectURL(objectUrl);
                    objectUrl = null;
                    return;
                }
                viewer = new NativeDxfViewer(el, {
                    clearColor,
                    clearAlpha: 0,
                    canvasAlpha: true,
                    autoResize: true,
                    colorCorrection: true,
                });
                await viewer.Load({ url: objectUrl });
                if (active) setIsLoading(false);
            } catch (e: any) {
                if (e?.name === 'AbortError') return;
                console.error('DXF Load Error', e);
                if (active) setIsLoading(false);
            }
        })();

        const resizeObserver = new ResizeObserver(() => {
            if (viewer) {
                // @ts-ignore dxf-viewer Resize when present
                if (typeof viewer.Resize === 'function') viewer.Resize();
            }
        });
        resizeObserver.observe(el);

        return () => {
            active = false;
            ac.abort();
            resizeObserver.disconnect();
            if (viewer) viewer.Destroy();
            viewer = null;
            if (objectUrl) {
                URL.revokeObjectURL(objectUrl);
                objectUrl = null;
            }
        };
    }, [url]);

    return (
        <div className="absolute inset-0 overflow-hidden bg-transparent">
            {isLoading ? (
                <div
                    className="pointer-events-none absolute inset-0 z-10 flex items-center justify-center gap-2 bg-slate-50/80 dark:bg-surface-dark/80"
                    aria-busy="true"
                >
                    <Loader2 size={24} className="animate-spin text-primary" />
                    <span className="text-sm font-medium">Loading DXF…</span>
                </div>
            ) : null}
            {/* Keep host visible during load — opacity:0 broke WebGL init / sizing in WebView2 */}
            <div ref={dxfRef} className="relative h-full w-full min-h-[1px] min-w-[1px]" />
        </div>
    );
};

// -- Main Component ---------------------------------------
const ThreeDViewerInner: React.FC<ThreeDViewerInnerProps> = ({
    url,
    fileName = '3D Model',
    viewerKind = 'three',
    openscadSourceText = '',
}) => {
    const [renderMode, setRenderMode] = useState<'solid' | 'wireframe' | 'edges'>('edges');
    const [navMode, setNavMode] = useState<'orbit' | 'pan' | 'zoom'>('orbit');
    const [isFullscreen, setIsFullscreen] = useState(false);
    const [showSidebar, setShowSidebar] = useState(false);
    const [sidebarTab, setSidebarTab] = useState<'structure' | 'properties'>('structure');
    const [extraLights, setExtraLights] = useState(true);
    const [hiddenNodes, setHiddenNodes] = useState<Set<string>>(new Set());
    const [openScadParams, setOpenScadParams] = useState<ParsedParam[]>([]);
    const [openScadApplyBusy, setOpenScadApplyBusy] = useState(false);
    const [openScadParseError, setOpenScadParseError] = useState<string | null>(null);
    const containerRef = useRef<HTMLDivElement>(null);
    const isOpenScad = viewerKind === 'openscad';

    const parts = fileName.split('.');
    const rawExt = parts.length > 1 ? parts[parts.length - 1] : '';
    const ext = rawExt.toLowerCase();
    const is3D = ['stl', 'obj', 'gltf', 'glb', 'ply', 'step', 'stp'].includes(ext);
    const canShowOpenScadPanel = useMemo(() => isOpenScad, [isOpenScad]);

    const parseOpenScadParams = (src: string): ParsedParam[] => {
        const out: ParsedParam[] = [];
        let activeGroup = 'General';
        let pendingDescription: string | undefined;
        const lines = src.split(/\r?\n/);
        for (const rawLine of lines) {
            const line = rawLine.trim();
            if (!line) continue;
            const groupMatch = line.match(/^\/\/\s*@group\s+(.+)$/i);
            if (groupMatch) {
                activeGroup = groupMatch[1].trim();
                pendingDescription = undefined;
                continue;
            }
            const descMatch = line.match(/^\/\/\s*@desc\s+(.+)$/i);
            if (descMatch) {
                pendingDescription = descMatch[1].trim();
                continue;
            }
            const rangeMatch = line.match(/^\/\/\s*@range\s+([\-0-9.]+)\s+([\-0-9.]+)(?:\s+([\-0-9.]+))?$/i);
            const sliderMeta = rangeMatch
                ? {
                    min: Number(rangeMatch[1]),
                    max: Number(rangeMatch[2]),
                    step: rangeMatch[3] !== undefined ? Number(rangeMatch[3]) : undefined,
                }
                : null;
            const assignMatch = line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*;\s*(?:\/\/\s*(.*))?$/);
            if (!assignMatch) continue;
            const name = assignMatch[1];
            const rhs = assignMatch[2].trim();
            const inlineComment = assignMatch[3]?.trim();
            const label = inlineComment && inlineComment.length > 0 ? inlineComment : name;

            if (/^(true|false)$/i.test(rhs)) {
                out.push({
                    name,
                    label,
                    description: pendingDescription,
                    group: activeGroup,
                    type: 'boolean',
                    value: /^true$/i.test(rhs),
                });
                pendingDescription = undefined;
                continue;
            }
            const strMatch = rhs.match(/^"((?:[^"\\]|\\.)*)"$/);
            if (strMatch) {
                out.push({
                    name,
                    label,
                    description: pendingDescription,
                    group: activeGroup,
                    type: 'string',
                    value: strMatch[1].replace(/\\"/g, '"'),
                });
                pendingDescription = undefined;
                continue;
            }
            const numVal = Number(rhs);
            if (Number.isFinite(numVal)) {
                const computedMin = sliderMeta?.min ?? Math.min(0, numVal);
                const computedMax = sliderMeta?.max ?? Math.max(numVal * 2 || 1, 1);
                const computedStep = sliderMeta?.step ?? Math.max((computedMax - computedMin) / 100, 0.01);
                out.push({
                    name,
                    label,
                    description: pendingDescription,
                    group: activeGroup,
                    type: 'number',
                    value: numVal,
                    min: computedMin,
                    max: computedMax,
                    step: computedStep,
                });
                pendingDescription = undefined;
            }
        }
        return out;
    };

    const toDefinesString = (params: ParsedParam[]): string => {
        const encode = (p: ParsedParam): string => {
            if (p.type === 'boolean') return `${p.name}=${p.value ? 'true' : 'false'}`;
            if (p.type === 'number') return `${p.name}=${Number(p.value)}`;
            const escaped = String(p.value).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
            return `${p.name}="${escaped}"`;
        };
        return params.map((p) => `-D ${encode(p)}`).join(' ');
    };

    const groupedParams = useMemo(() => {
        const m = new Map<string, ParsedParam[]>();
        for (const p of openScadParams) {
            const arr = m.get(p.group);
            if (arr) arr.push(p);
            else m.set(p.group, [p]);
        }
        return Array.from(m.entries());
    }, [openScadParams]);

    // Load geometry strictly in the parent space so the UI tree can bind to it
    const { geometry, error, isLoading } = useModelGeometry(url, ext);

    useEffect(() => {
        const handleFullscreenChange = () => setIsFullscreen(!!document.fullscreenElement);
        document.addEventListener('fullscreenchange', handleFullscreenChange);
        return () => document.removeEventListener('fullscreenchange', handleFullscreenChange);
    }, []);

    const toggleFullscreen = () => {
        if (!document.fullscreenElement) {
            containerRef.current?.requestFullscreen().catch(err => console.error(`Error entering fullscreen: ${err.message}`));
        } else {
            document.exitFullscreen();
        }
    };

    const toggleNode = (uuid: string) => {
        setHiddenNodes(prev => {
            const next = new Set(prev);
            if (next.has(uuid)) next.delete(uuid);
            else next.add(uuid);
            return next;
        });
    };

    useEffect(() => {
        if (!canShowOpenScadPanel) return;
        setShowSidebar(true);
        setSidebarTab('properties');
    }, [canShowOpenScadPanel]);

    useEffect(() => {
        if (!canShowOpenScadPanel) return;
        try {
            const parsed = parseOpenScadParams(openscadSourceText);
            setOpenScadParams(parsed);
            setOpenScadParseError(null);
        } catch (e: any) {
            setOpenScadParams([]);
            setOpenScadParseError(e?.message ?? 'Failed to parse OpenSCAD parameters');
        }
    }, [canShowOpenScadPanel, openscadSourceText]);

    useEffect(() => {
        if (!canShowOpenScadPanel) return;
        return onHostMessage((data) => {
            if (data.t !== 'vw_openscad_preview_result') return;
            setOpenScadApplyBusy(false);
        });
    }, [canShowOpenScadPanel]);

    const updateParam = (name: string, value: number | boolean | string) => {
        setOpenScadParams((prev) => prev.map((p) => (p.name === name ? { ...p, value } : p)));
    };

    const applyOpenScadDefines = () => {
        if (!canShowOpenScadPanel || openScadApplyBusy) return;
        setOpenScadApplyBusy(true);
        postToHost({
            t: 'vw_openscad_preview',
            defines: toDefinesString(openScadParams).trim(),
        });
    };

    return (
        <div
            ref={containerRef}
            className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden bg-transparent"
        >
            <div style={{
                padding: '6px 8px', display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                borderBottom: '1px solid var(--border)', background: 'var(--muted)', zIndex: 10
            }}>
                <span style={{ fontSize: 14, fontWeight: 500, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    {fileName}
                </span>

                <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                    {is3D && (
                        <>
                            <div style={{ display: 'flex', gap: '4px', background: 'var(--background)', padding: '2px', borderRadius: '6px' }}>
                                <button
                                    onClick={() => setNavMode('orbit')}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: navMode === 'orbit' ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Orbit Camera"
                                ><MousePointer2 size={16} className={navMode === 'orbit' ? 'text-primary' : 'text-muted-foreground'} /></button>
                                <button
                                    onClick={() => setNavMode('pan')}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: navMode === 'pan' ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Pan Camera"
                                ><Move size={16} className={navMode === 'pan' ? 'text-primary' : 'text-muted-foreground'} /></button>
                                <button
                                    onClick={() => setNavMode('zoom')}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: navMode === 'zoom' ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Zoom Camera"
                                ><ZoomIn size={16} className={navMode === 'zoom' ? 'text-primary' : 'text-muted-foreground'} /></button>
                            </div>
                            <div style={{ width: 1, height: 20, background: 'var(--border)' }} />
                            <div style={{ display: 'flex', gap: '4px', background: 'var(--background)', padding: '2px', borderRadius: '6px' }}>
                                <button
                                    onClick={() => setRenderMode('solid')}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: renderMode === 'solid' ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Solid Mesh"
                                ><Cuboid size={16} className={renderMode === 'solid' ? 'text-primary' : 'text-muted-foreground'} /></button>
                                <button
                                    onClick={() => setRenderMode('edges')}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: renderMode === 'edges' ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Faces with Edges"
                                ><BoxSelect size={16} className={renderMode === 'edges' ? 'text-primary' : 'text-muted-foreground'} /></button>
                                <button
                                    onClick={() => setRenderMode('wireframe')}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: renderMode === 'wireframe' ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Wireframe"
                                ><Grid3x3 size={16} className={renderMode === 'wireframe' ? 'text-primary' : 'text-muted-foreground'} /></button>
                            </div>
                            <div style={{ width: 1, height: 20, background: 'var(--border)' }} />
                            <div style={{ display: 'flex', gap: '4px', background: 'var(--background)', padding: '2px', borderRadius: '6px' }}>
                                <button
                                    onClick={() => setExtraLights(!extraLights)}
                                    style={{ padding: '4px 8px', borderRadius: '4px', background: extraLights ? 'var(--muted)' : 'transparent', border: 'none', cursor: 'pointer' }}
                                    title="Toggle Headlight (Follows Camera)"
                                ><Lightbulb size={16} className={extraLights ? 'text-primary' : 'text-muted-foreground'} /></button>
                            </div>
                            <div style={{ width: 1, height: 20, background: 'var(--border)' }} />
                        </>
                    )}
                    <button
                        onClick={toggleFullscreen}
                        style={{ padding: '4px 8px', borderRadius: '6px', background: 'var(--background)', border: '1px solid var(--border)', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center' }}
                        title={isFullscreen ? "Exit Fullscreen" : "Enter Fullscreen"}
                    >
                        {isFullscreen ? <Minimize size={16} className="text-muted-foreground" /> : <Maximize size={16} className="text-muted-foreground" />}
                    </button>
                    {is3D && (
                        <button
                            onClick={() => {
                                if (showSidebar && sidebarTab === 'structure') setShowSidebar(false);
                                else {
                                    setShowSidebar(true);
                                    setSidebarTab('structure');
                                }
                            }}
                            style={{ padding: '4px 8px', borderRadius: '6px', background: showSidebar && sidebarTab === 'structure' ? 'var(--muted)' : 'var(--background)', border: '1px solid var(--border)', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center', marginLeft: '4px' }}
                            title="Toggle Model Tree"
                        >
                            <ListTree size={16} className={showSidebar && sidebarTab === 'structure' ? "text-primary" : "text-muted-foreground"} />
                        </button>
                    )}
                    {canShowOpenScadPanel && (
                        <button
                            onClick={() => {
                                if (showSidebar && sidebarTab === 'properties') setShowSidebar(false);
                                else {
                                    setShowSidebar(true);
                                    setSidebarTab('properties');
                                }
                            }}
                            style={{ padding: '4px 8px', borderRadius: '6px', background: showSidebar && sidebarTab === 'properties' ? 'var(--muted)' : 'var(--background)', border: '1px solid var(--border)', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center', marginLeft: '4px' }}
                            title="OpenSCAD Parameters"
                        >
                            <SlidersHorizontal size={16} className={showSidebar && sidebarTab === 'properties' ? "text-primary" : "text-muted-foreground"} />
                        </button>
                    )}
                </div>
            </div>

            <div className="relative flex min-h-0 flex-1">
                {ext === 'dxf' ? (
                    <div className="relative min-h-0 min-w-0 flex-1">
                        <DxfNativeLayer url={url} />
                    </div>
                ) : (
                    <>
                        <div className="relative min-h-0 min-w-0 flex-1">
                            {isLoading && (
                                <div style={{ position: 'absolute', inset: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 8, zIndex: 10, background: 'rgba(0,0,0,0.05)' }}>
                                    <Loader2 size={24} className="animate-spin text-primary" />
                                    <span className="text-sm font-medium">Loading 3D Model...</span>
                                </div>
                            )}
                            <Suspense fallback={
                                <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
                                    <Loader2 size={16} className="animate-spin" /> Loading Engine...
                                </div>
                            }>
                                <Canvas
                                    className="!block h-full w-full touch-none"
                                    shadows
                                    camera={{ position: [5, 5, 5], fov: 50 }}
                                >
                                    <ambientLight intensity={0.5} />
                                    <directionalLight position={[10, 10, 10]} intensity={1} castShadow />
                                    <directionalLight position={[-10, -10, -10]} intensity={0.3} />
                                    <Headlight enabled={extraLights} />
                                    {/* No `observe` — it refits on camera moves and fights GizmoViewcube. No `clip` here — first layout can run before OrbitControls exists; ModelRenderer calls `clip()` after geometry loads. */}
                                    <Bounds fit margin={1.5}>
                                        <Center>
                                            <ModelRenderer geometry={geometry} error={error} renderMode={renderMode} hiddenNodes={hiddenNodes} />
                                        </Center>
                                    </Bounds>

                                    {navMode === 'orbit' && (
                                        <OrbitControls
                                            makeDefault
                                            enableRotate
                                            enablePan
                                            enableZoom
                                            mouseButtons={{ LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN }}
                                        />
                                    )}
                                    {navMode === 'pan' && (
                                        <OrbitControls
                                            makeDefault
                                            enableRotate
                                            enablePan
                                            enableZoom
                                            mouseButtons={{ LEFT: THREE.MOUSE.PAN, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.ROTATE }}
                                        />
                                    )}
                                    {navMode === 'zoom' && (
                                        <OrbitControls
                                            makeDefault
                                            enableRotate
                                            enablePan
                                            enableZoom
                                            mouseButtons={{ LEFT: THREE.MOUSE.DOLLY, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN }}
                                        />
                                    )}
                                    <GizmoHelper alignment="top-right" margin={[80, 72]}>
                                        <GizmoViewcube
                                            color="#334155"
                                            hoverColor="#475569"
                                            textColor="#f8fafc"
                                            strokeColor="#0f172a"
                                            font='600 22px "Segoe UI",system-ui,sans-serif'
                                        />
                                    </GizmoHelper>
                                </Canvas>
                            </Suspense>
                        </div>

                        {showSidebar && (
                            <div style={{
                                width: 340,
                                borderLeft: '1px solid var(--border)',
                                background: 'var(--background)',
                                display: 'flex',
                                flexDirection: 'column',
                                minWidth: 280
                            }}>
                                <div style={{ padding: '8px 10px', borderBottom: '1px solid var(--border)', display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
                                    <span style={{ fontSize: 12, fontWeight: 600 }}>Model Browser</span>
                                    <div style={{ display: 'flex', gap: 4 }}>
                                        <button
                                            onClick={() => setSidebarTab('structure')}
                                            style={{
                                                padding: '4px 8px',
                                                borderRadius: 6,
                                                border: '1px solid var(--border)',
                                                background: sidebarTab === 'structure' ? 'var(--muted)' : 'var(--background)',
                                                fontSize: 12,
                                                cursor: 'pointer',
                                            }}
                                        >
                                            Structure
                                        </button>
                                        {canShowOpenScadPanel ? (
                                            <button
                                                onClick={() => setSidebarTab('properties')}
                                                style={{
                                                    padding: '4px 8px',
                                                    borderRadius: 6,
                                                    border: '1px solid var(--border)',
                                                    background: sidebarTab === 'properties' ? 'var(--muted)' : 'var(--background)',
                                                    fontSize: 12,
                                                    cursor: 'pointer',
                                                }}
                                            >
                                                Properties
                                            </button>
                                        ) : null}
                                    </div>
                                </div>
                                {sidebarTab === 'structure' ? (
                                    <div className="pm-scroll" style={{ overflowY: 'auto', minHeight: 0 }}>
                                        {geometry ? (
                                            <ModelTreeNode node={geometry instanceof THREE.BufferGeometry ? new THREE.Mesh(geometry) : geometry} depth={0} hiddenNodes={hiddenNodes} toggleNode={toggleNode} />
                                        ) : (
                                            <div style={{ padding: 12, fontSize: 12, opacity: 0.75 }}>
                                                No model hierarchy available.
                                            </div>
                                        )}
                                    </div>
                                ) : (
                                    <div className="pm-scroll" style={{ display: 'flex', flexDirection: 'column', gap: 8, padding: 10, overflowY: 'auto', minHeight: 0, scrollBehavior: 'smooth' }}>
                                        <div style={{ fontSize: 12, fontWeight: 600 }}>OpenSCAD defines</div>
                                        {openScadParseError ? (
                                            <div style={{ fontSize: 12, color: '#ef4444' }}>{openScadParseError}</div>
                                        ) : null}
                                        {groupedParams.length === 0 ? (
                                            <div style={{ fontSize: 12, opacity: 0.75 }}>
                                                No parameter assignments found. Add lines like `width = 20;` in your .scad.
                                            </div>
                                        ) : (
                                            <>
                                                {groupedParams.map(([group, params]) => (
                                                    <div key={group} style={{ border: '1px solid var(--border)', borderRadius: 6, padding: 8 }}>
                                                        <div style={{ fontSize: 12, fontWeight: 600, marginBottom: 6 }}>{group}</div>
                                                        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                                                            {params.map((p) => (
                                                                <div key={p.name} style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                                                                    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
                                                                        <span style={{ fontSize: 12 }}>{p.label}</span>
                                                                        {p.type === 'boolean' ? (
                                                                            <input
                                                                                type="checkbox"
                                                                                checked={Boolean(p.value)}
                                                                                onChange={(e) => updateParam(p.name, e.target.checked)}
                                                                            />
                                                                        ) : p.type === 'string' ? (
                                                                            <input
                                                                                value={String(p.value)}
                                                                                onChange={(e) => updateParam(p.name, e.target.value)}
                                                                                style={{ width: 170, border: '1px solid var(--border)', borderRadius: 6, padding: '4px 6px', fontSize: 12 }}
                                                                            />
                                                                        ) : (
                                                                            <input
                                                                                type="number"
                                                                                value={Number(p.value)}
                                                                                step={p.step ?? 0.01}
                                                                                onChange={(e) => updateParam(p.name, Number(e.target.value))}
                                                                                style={{ width: 90, border: '1px solid var(--border)', borderRadius: 6, padding: '4px 6px', fontSize: 12 }}
                                                                            />
                                                                        )}
                                                                    </div>
                                                                    {p.type === 'number' && p.min !== undefined && p.max !== undefined ? (
                                                                        <input
                                                                            type="range"
                                                                            min={p.min}
                                                                            max={p.max}
                                                                            step={p.step ?? 0.01}
                                                                            value={Number(p.value)}
                                                                            onChange={(e) => updateParam(p.name, Number(e.target.value))}
                                                                        />
                                                                    ) : null}
                                                                    {p.description ? <span style={{ fontSize: 11, opacity: 0.75 }}>{p.description}</span> : null}
                                                                </div>
                                                            ))}
                                                        </div>
                                                    </div>
                                                ))}
                                            </>
                                        )}
                                        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8, marginTop: 4 }}>
                                            <span style={{ fontSize: 11, opacity: 0.75 }}>Updates compile into preview via -D defines.</span>
                                            <button
                                                onClick={applyOpenScadDefines}
                                                disabled={openScadApplyBusy}
                                                style={{
                                                    border: '1px solid var(--border)',
                                                    borderRadius: 6,
                                                    padding: '5px 10px',
                                                    background: 'var(--muted)',
                                                    cursor: openScadApplyBusy ? 'default' : 'pointer',
                                                    display: 'inline-flex',
                                                    alignItems: 'center',
                                                    gap: 6,
                                                    opacity: openScadApplyBusy ? 0.7 : 1,
                                                }}
                                                title="Recompile OpenSCAD preview"
                                            >
                                                {openScadApplyBusy ? <Loader2 size={14} className="animate-spin" /> : <RefreshCw size={14} />}
                                                Apply
                                            </button>
                                        </div>
                                    </div>
                                )}
                            </div>
                        )}
                    </>
                )}
            </div>
        </div>
    );
};

export function ThreeDViewerPane({ status }: { status: ViewerWebStatus }) {
    const f = status.features ?? {};
    const err = typeof f.threeError === 'string' ? f.threeError : '';
    const url = typeof f.threeModelUrl === 'string' ? f.threeModelUrl : '';
    const kind = typeof status.viewerKind === 'string' ? status.viewerKind : '';
    const openscadSourceText = typeof f.openscadSourceText === 'string' ? f.openscadSourceText : '';
    const fallbackName = kind === 'openscad' ? 'OpenSCAD preview' : '3D model';
    const fileName = typeof f.threeFileName === 'string' ? f.threeFileName : fallbackName;
    if (err) {
        const fsz = typeof f.threeFileSizeBytes === 'number' ? f.threeFileSizeBytes : 0;
        const maxB = typeof f.threeMaxBytes === 'number' ? f.threeMaxBytes : 0;
        return (
            <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center p-6 text-center text-slate-600 dark:text-slate-400">
                <p className="mb-2 text-sm">{err}</p>
                {fsz > 0 && maxB > 0 ? (
                    <p className="text-xs text-slate-500 dark:text-slate-500">
                        ({(fsz / (1024 * 1024)).toFixed(2)} MB / max {(maxB / (1024 * 1024)).toFixed(0)} MB)
                    </p>
                ) : null}
            </div>
        );
    }
    if (!url) {
        return (
            <div className="flex h-full min-h-0 flex-1 items-center justify-center p-6 text-sm text-slate-500 dark:text-slate-400">
                No 3D model loaded.
            </div>
        );
    }
    return (
        <div className="flex h-full min-h-0 w-full flex-1 flex-col overflow-hidden">
            <ThreeDViewerInner url={url} fileName={fileName} viewerKind={kind} openscadSourceText={openscadSourceText} />
        </div>
    );
}
