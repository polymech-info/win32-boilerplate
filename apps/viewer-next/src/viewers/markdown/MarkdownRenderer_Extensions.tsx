import React, { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";

let mermaidRenderSeq = 0;
let mermaidBlockSeq = 0;

type MermaidTransform = {
  scale: number;
  x: number;
  y: number;
};

type MermaidSvgFit = {
  svg: string;
  width: number;
  height: number;
};

type MermaidRegistryItem = {
  id: string;
  svg: string;
};

const MERMAID_FULLSCREEN_INITIAL_SCALE = 0.9;
const mermaidRegistry: MermaidRegistryItem[] = [];
const mermaidRegistryListeners = new Set<() => void>();

export function isMermaidLanguage(language: string): boolean {
  return language === "mermaid" || language === "mmd";
}

export function isMermaidCodeClass(className: string): boolean {
  return /\blanguage-(mermaid|mmd)\b/.test(className);
}

function emitMermaidRegistryChange() {
  mermaidRegistryListeners.forEach((listener) => listener());
}

function mermaidRegistrySnapshot(): MermaidRegistryItem[] {
  return mermaidRegistry.filter((item) => item.svg);
}

function subscribeMermaidRegistry(listener: () => void): () => void {
  mermaidRegistryListeners.add(listener);
  return () => mermaidRegistryListeners.delete(listener);
}

function registerMermaidDiagram(id: string, svg: string): () => void {
  const existing = mermaidRegistry.find((item) => item.id === id);
  if (existing) {
    existing.svg = svg;
  } else {
    mermaidRegistry.push({ id, svg });
  }
  emitMermaidRegistryChange();

  return () => {
    const index = mermaidRegistry.findIndex((item) => item.id === id);
    if (index >= 0) {
      mermaidRegistry.splice(index, 1);
      emitMermaidRegistryChange();
    }
  };
}

function clampScale(scale: number): number {
  return Math.min(12, Math.max(0.1, scale));
}

function svgViewBoxSize(svgText: string): { width: number; height: number } | null {
  try {
    const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
    const svg = doc.documentElement;
    const viewBox = svg.getAttribute("viewBox");
    if (viewBox) {
      const parts = viewBox.split(/[\s,]+/).map(Number);
      if (parts.length === 4 && parts.every(Number.isFinite) && parts[2] > 0 && parts[3] > 0) {
        return { width: parts[2], height: parts[3] };
      }
    }
    const width = parseFloat(svg.getAttribute("width") || "");
    const height = parseFloat(svg.getAttribute("height") || "");
    if (Number.isFinite(width) && Number.isFinite(height) && width > 0 && height > 0) {
      return { width, height };
    }
  } catch {
    /* keep fallback below */
  }
  return null;
}

function normalizeMermaidSvg(svgText: string): string {
  try {
    const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
    const svg = doc.documentElement;
    svg.removeAttribute("style");
    svg.setAttribute("width", "100%");
    svg.setAttribute("height", "100%");
    svg.setAttribute("preserveAspectRatio", "xMidYMid meet");
    return new XMLSerializer().serializeToString(svg);
  } catch {
    return svgText;
  }
}

function tightenMermaidSvg(container: HTMLElement | null): MermaidSvgFit | null {
  const svg = container?.querySelector("svg");
  if (!(svg instanceof SVGSVGElement)) return null;

  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;

  const candidates = svg.querySelectorAll<SVGGraphicsElement>(
    "g,path,rect,circle,ellipse,line,polyline,polygon,text,foreignObject,image",
  );

  candidates.forEach((el) => {
    if (el.closest("defs,style,script,marker,clipPath,mask,pattern")) return;
    try {
      const box = el.getBBox();
      if (!Number.isFinite(box.width) || !Number.isFinite(box.height) || box.width <= 0 || box.height <= 0) return;
      minX = Math.min(minX, box.x);
      minY = Math.min(minY, box.y);
      maxX = Math.max(maxX, box.x + box.width);
      maxY = Math.max(maxY, box.y + box.height);
    } catch {
      /* Some SVG nodes cannot report a bbox in all WebView2 builds. */
    }
  });

  if (!Number.isFinite(minX) || !Number.isFinite(minY) || !Number.isFinite(maxX) || !Number.isFinite(maxY)) {
    const fallback = svgViewBoxSize(svg.outerHTML);
    return fallback ? { svg: svg.outerHTML, ...fallback } : null;
  }

  const pad = 16;
  const x = minX - pad;
  const y = minY - pad;
  const width = Math.max(1, maxX - minX + pad * 2);
  const height = Math.max(1, maxY - minY + pad * 2);
  svg.setAttribute("viewBox", `${x} ${y} ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "100%");
  svg.setAttribute("preserveAspectRatio", "xMidYMid meet");
  svg.removeAttribute("style");
  return { svg: svg.outerHTML, width, height };
}

function MermaidFullscreen({
  diagramId,
  svg,
  onClose,
}: {
  diagramId: string;
  svg: string;
  onClose: () => void;
}) {
  const canvasRef = useRef<HTMLDivElement>(null);
  const diagramRef = useRef<HTMLDivElement>(null);
  const closeRef = useRef<HTMLButtonElement>(null);
  const dragRef = useRef<{ pointerId: number; startX: number; startY: number; originX: number; originY: number } | null>(null);
  const [activeId, setActiveId] = useState(diagramId);
  const [registryItems, setRegistryItems] = useState(mermaidRegistrySnapshot);
  const activeIndex = registryItems.findIndex((item) => item.id === activeId);
  const activeItem = activeIndex >= 0 ? registryItems[activeIndex] : undefined;
  const activeSvg = activeItem?.svg ?? svg;
  const [fittedSvg, setFittedSvg] = useState<MermaidSvgFit | null>(null);
  const svgSize = fittedSvg ?? svgViewBoxSize(activeSvg) ?? { width: 800, height: 500 };
  const [transform, setTransform] = useState<MermaidTransform>({ scale: MERMAID_FULLSCREEN_INITIAL_SCALE, x: 0, y: 0 });
  const hasPrev = activeIndex > 0;
  const hasNext = activeIndex >= 0 && activeIndex < registryItems.length - 1;

  const zoomBy = useCallback((delta: number) => {
    setTransform((next) => ({ ...next, scale: clampScale(next.scale + delta) }));
  }, []);

  const fitToBounds = useCallback(() => {
    setTransform({ scale: MERMAID_FULLSCREEN_INITIAL_SCALE, x: 0, y: 0 });
  }, []);

  const reset = fitToBounds;

  const goPrev = useCallback(() => {
    setActiveId((currentId) => {
      const currentIndex = registryItems.findIndex((item) => item.id === currentId);
      if (currentIndex <= 0) return currentId;
      return registryItems[currentIndex - 1].id;
    });
  }, [registryItems]);

  const goNext = useCallback(() => {
    setActiveId((currentId) => {
      const currentIndex = registryItems.findIndex((item) => item.id === currentId);
      if (currentIndex < 0 || currentIndex >= registryItems.length - 1) return currentId;
      return registryItems[currentIndex + 1].id;
    });
  }, [registryItems]);

  useEffect(() => subscribeMermaidRegistry(() => setRegistryItems(mermaidRegistrySnapshot())), []);

  useLayoutEffect(() => {
    fitToBounds();
    const canvas = canvasRef.current;
    if (!canvas || typeof ResizeObserver === "undefined") return;
    const ro = new ResizeObserver(() => fitToBounds());
    ro.observe(canvas);
    return () => ro.disconnect();
  }, [fitToBounds]);

  useLayoutEffect(() => {
    const next = tightenMermaidSvg(diagramRef.current);
    if (!next) return;
    setFittedSvg((prev) => (
      prev?.svg === next.svg && prev.width === next.width && prev.height === next.height ? prev : next
    ));
  }, [activeSvg]);

  useEffect(() => {
    setFittedSvg(null);
    reset();
  }, [activeId, reset]);

  useEffect(() => {
    const previouslyFocused = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const prevOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    closeRef.current?.focus();

    const onKeyDown = (ev: KeyboardEvent) => {
      if (ev.key === "Escape") onClose();
      if (ev.key === "0") reset();
      if (ev.key === "+" || ev.key === "=") zoomBy(0.15);
      if (ev.key === "-" || ev.key === "_") zoomBy(-0.15);
      if (ev.key === "ArrowLeft") goPrev();
      if (ev.key === "ArrowRight") goNext();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => {
      document.body.style.overflow = prevOverflow;
      window.removeEventListener("keydown", onKeyDown);
      previouslyFocused?.focus();
    };
  }, [goNext, goPrev, onClose, reset, zoomBy]);

  const onPointerDown = (ev: React.PointerEvent<HTMLDivElement>) => {
    if (ev.button !== 0) return;
    ev.currentTarget.setPointerCapture(ev.pointerId);
    dragRef.current = {
      pointerId: ev.pointerId,
      startX: ev.clientX,
      startY: ev.clientY,
      originX: transform.x,
      originY: transform.y,
    };
  };

  const onPointerMove = (ev: React.PointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== ev.pointerId) return;
    setTransform((next) => ({
      ...next,
      x: drag.originX + ev.clientX - drag.startX,
      y: drag.originY + ev.clientY - drag.startY,
    }));
  };

  const onPointerUp = (ev: React.PointerEvent<HTMLDivElement>) => {
    if (dragRef.current?.pointerId === ev.pointerId) dragRef.current = null;
  };

  const onWheel = (ev: React.WheelEvent<HTMLDivElement>) => {
    ev.preventDefault();
    zoomBy(ev.deltaY > 0 ? -0.12 : 0.12);
  };

  return (
    <div className="pm-mermaid-modal" role="dialog" aria-modal="true" aria-label="Mermaid diagram viewer">
      <div className="pm-mermaid-modal-toolbar">
        <button type="button" onClick={goPrev} disabled={!hasPrev} aria-label="Previous diagram">
          Prev
        </button>
        <button type="button" onClick={goNext} disabled={!hasNext} aria-label="Next diagram">
          Next
        </button>
        <button type="button" onClick={() => zoomBy(-0.15)} aria-label="Zoom out">
          -
        </button>
        <button type="button" onClick={fitToBounds}>
          Fit
        </button>
        <button type="button" onClick={() => setTransform({ scale: 1, x: 0, y: 0 })}>
          100%
        </button>
        <button type="button" onClick={() => zoomBy(0.15)} aria-label="Zoom in">
          +
        </button>
        <button ref={closeRef} type="button" className="pm-mermaid-modal-close" onClick={onClose}>
          Close
        </button>
      </div>
      <div
        ref={canvasRef}
        className="pm-mermaid-modal-canvas"
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onWheel={onWheel}
      >
        <div
          ref={diagramRef}
          className="pm-mermaid-modal-diagram"
          style={{
            width: svgSize.width * transform.scale,
            height: svgSize.height * transform.scale,
            transform: `translate(${transform.x}px, ${transform.y}px)`,
          }}
          dangerouslySetInnerHTML={{ __html: fittedSvg?.svg ?? activeSvg }}
        />
      </div>
    </div>
  );
}

export function MermaidBlock({ chart }: { chart: string }) {
  const diagramIdRef = useRef(`pm-mermaid-diagram-${++mermaidBlockSeq}`);
  const inlineRef = useRef<HTMLSpanElement>(null);
  const [svg, setSvg] = useState("");
  const [fittedSvg, setFittedSvg] = useState<MermaidSvgFit | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [fullscreen, setFullscreen] = useState(false);
  const svgSize = fittedSvg ?? svgViewBoxSize(svg) ?? { width: 800, height: 500 };

  useEffect(() => {
    let cancelled = false;
    const id = `pm-mermaid-${++mermaidRenderSeq}`;

    (async () => {
      try {
        const mermaid = (await import("mermaid")).default;
        const dark = document.documentElement.classList.contains("dark");
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: dark ? "dark" : "default",
        });
        const { svg } = await mermaid.render(id, chart);
        if (cancelled) return;
        setSvg(normalizeMermaidSvg(svg));
        setFittedSvg(null);
        setError(null);
      } catch (e) {
        if (cancelled) return;
        setSvg("");
        setError(e instanceof Error ? e.message : "Failed to render Mermaid diagram");
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [chart]);

  useLayoutEffect(() => {
    if (!svg) return;
    const next = tightenMermaidSvg(inlineRef.current);
    if (!next) return;
    setFittedSvg((prev) => (
      prev?.svg === next.svg && prev.width === next.width && prev.height === next.height ? prev : next
    ));
  }, [svg]);

  useEffect(() => {
    if (!svg) return undefined;
    return registerMermaidDiagram(diagramIdRef.current, fittedSvg?.svg ?? svg);
  }, [fittedSvg?.svg, svg]);

  return (
    <div className="pm-mermaid-block">
      {error ? (
        <pre className="pm-mermaid-error">{error}</pre>
      ) : (
        <>
          <button
            type="button"
            className="pm-mermaid-open"
            onClick={() => svg && setFullscreen(true)}
            title="Open diagram fullscreen"
            aria-label="Open Mermaid diagram fullscreen"
          >
            <span className="pm-mermaid-open-label">Open fullscreen</span>
            <span
              ref={inlineRef}
              className="pm-mermaid-svg"
              style={{
                width: Math.min(svgSize.width, 960),
                aspectRatio: `${svgSize.width} / ${svgSize.height}`,
              }}
              dangerouslySetInnerHTML={{ __html: fittedSvg?.svg ?? svg }}
            />
          </button>
          {fullscreen && svg ? (
            <MermaidFullscreen diagramId={diagramIdRef.current} svg={fittedSvg?.svg ?? svg} onClose={() => setFullscreen(false)} />
          ) : null}
        </>
      )}
    </div>
  );
}
