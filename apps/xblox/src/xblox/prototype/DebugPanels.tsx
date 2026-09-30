import { useCallback, useEffect, useMemo, useState, type CSSProperties } from "react";

type DebugPanelsProps = {
  eventLog: string;
  blocksJson: string;
};

const DEBUG_DETAIL_STORAGE_KEY = "pm.xblox.debugDetailRightPct.v1";

function readRightPct() {
  try {
    const raw = window.localStorage.getItem(DEBUG_DETAIL_STORAGE_KEY);
    const parsed = raw == null ? NaN : Number(raw);
    if (Number.isFinite(parsed)) return Math.max(20, Math.min(60, parsed));
  } catch {
    /* localStorage can be unavailable in embedded or private contexts. */
  }
  return 30;
}

export function DebugPanels({ eventLog, blocksJson }: DebugPanelsProps) {
  const [rightPct, setRightPct] = useState(readRightPct);
  const gridStyle = useMemo(
    () => ({ "--xblox-debug-detail-width": `${rightPct}%` }) as CSSProperties,
    [rightPct],
  );

  useEffect(() => {
    try {
      window.localStorage.setItem(DEBUG_DETAIL_STORAGE_KEY, String(rightPct));
    } catch {
      /* localStorage can be unavailable in embedded or private contexts. */
    }
  }, [rightPct]);

  const startResize = useCallback((startX: number, startRightPct: number) => {
    const onPointerMove = (event: PointerEvent) => {
      const container = document.querySelector<HTMLElement>(".xblox-debug-grid");
      const width = container?.getBoundingClientRect().width ?? window.innerWidth;
      const deltaPct = ((startX - event.clientX) / Math.max(width, 1)) * 100;
      setRightPct(Math.max(20, Math.min(60, startRightPct + deltaPct)));
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

  return (
    <section className="xblox-debug-grid" style={gridStyle}>
      <div className="xblox-debug-panel">
        <h2>Host Run Log</h2>
        <pre>{eventLog}</pre>
      </div>
      <div
        className="xblox-debug-resizer"
        role="separator"
        aria-label="Resize log detail"
        aria-orientation="vertical"
        onPointerDown={(event) => {
          event.preventDefault();
          startResize(event.clientX, rightPct);
        }}
      />
      <div className="xblox-debug-panel">
        <h2>Blocks File Preview</h2>
        <pre>{blocksJson}</pre>
      </div>
    </section>
  );
}
