import type { BlockPaletteKind } from "@/xblox/react";
import type { PrototypeTheme } from "./theme";

type PrototypeHeaderProps = {
  theme: PrototypeTheme;
  mode?: "host" | "standalone";
  filePath?: string;
  dirty?: boolean;
  status?: string;
  onToggleTheme: () => void;
  onResetSample: () => void;
  onAddRoot: (kind: BlockPaletteKind) => void;
  onAddFaultyBlock: () => void;
  onRunSelectedChain: () => void;
};

export function PrototypeHeader({
  theme,
  mode = "standalone",
  filePath,
  dirty = false,
  status,
  onToggleTheme,
  onResetSample,
  onAddRoot,
  onAddFaultyBlock,
  onRunSelectedChain,
}: PrototypeHeaderProps) {
  return (
    <div className="xblox-hero">
      <div>
        <h1>XBlox</h1>
        <p>
          {mode === "host"
            ? "Editing a .xblox command-chain file from the native preview host."
            : "Standalone demo host for chaining custom commands as blocks."}{" "}
          The graph supports <code>if</code>, <code>else</code>, <code>for</code>, <code>switch</code>, <code>while</code>, and{" "}
          <code>runScript</code> blocks.
        </p>
        {filePath ? <p className="xblox-file-path">{dirty ? "* " : ""}{filePath}</p> : null}
        {status ? <p className="xblox-host-status">{status}</p> : null}
      </div>
      <div className="xblox-actions">
        {mode === "standalone" ? (
          <button type="button" onClick={onToggleTheme}>
            {theme === "dark" ? "Light theme" : "Dark theme"}
          </button>
        ) : null}
        {mode === "standalone" ? (
          <button type="button" onClick={onResetSample}>
            Reset sample
          </button>
        ) : null}
        <button type="button" onClick={() => onAddRoot("runScript")}>
          Add runScript root
        </button>
        {mode === "standalone" ? (
          <button type="button" onClick={onAddFaultyBlock}>
            Add faulty block
          </button>
        ) : null}
        <button type="button" onClick={onRunSelectedChain}>
          Run
        </button>
      </div>
    </div>
  );
}
