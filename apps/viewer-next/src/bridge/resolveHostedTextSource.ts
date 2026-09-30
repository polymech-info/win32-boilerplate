import type { ViewerWebStatus } from "@/bridge/hostBridge";

/** Unified text source for Text / Editor panes (hosted file, 3D model URL, OpenSCAD source, etc.). */
export interface HostedTextSource {
  err: string;
  url: string;
  inlineText?: string;
  fileName: string;
}

export function hasHostedTextSource(source: Pick<HostedTextSource, "url" | "inlineText">): boolean {
  return source.url.length > 0 || typeof source.inlineText === "string";
}

/** Basename for preview / language detection (hosted, 3D, or selection path). */
export function hostedPreviewBaseName(status: ViewerWebStatus): string {
  const f = status.features ?? {};
  return (
    (typeof f.hostedFileName === "string" && f.hostedFileName) ||
    (typeof f.threeFileName === "string" && f.threeFileName) ||
    status.selection?.[0]?.split(/[/\\]/).pop() ||
    ""
  );
}

/** DXF opens in the 3D viewer but is ASCII source — editable in Monaco. */
export function isDxfHostPreview(status: ViewerWebStatus): boolean {
  return status.viewerKind === "three" && /\.dxf$/i.test(hostedPreviewBaseName(status));
}

/** Host preview modes that can default to / switch to the Monaco editor. */
export function isEditorEligibleHostStatus(status: ViewerWebStatus): boolean {
  const kind = status.viewerKind;
  if (kind === "text" || kind === "openscad") return true;
  if (isDxfHostPreview(status)) return true;
  return false;
}

export function resolveHostedTextSource(status: ViewerWebStatus): HostedTextSource {
  const f = status.features ?? {};

  const previewErr =
    (typeof f.hostedFileError === "string" && f.hostedFileError) ||
    (typeof f.threeError === "string" && f.threeError) ||
    "";

  const inlineText =
    typeof f.hostedFileText === "string"
      ? f.hostedFileText
      : typeof f.markdownText === "string"
        ? f.markdownText
        : typeof f.openscadSourceText === "string"
          ? f.openscadSourceText
          : undefined;

  const url =
    (typeof f.hostedFileUrl === "string" && f.hostedFileUrl) ||
    (typeof f.threeModelUrl === "string" && f.threeModelUrl) ||
    "";

  const fileName =
    hostedPreviewBaseName(status) || "untitled";

  // Mesh preview errors (compile status, too large for 3D, etc.) must not block Text/Editor
  // when source bytes are still available inline or via the hosted URL.
  const err = hasHostedTextSource({ url, inlineText }) ? "" : previewErr;

  return { err, url, inlineText, fileName };
}
