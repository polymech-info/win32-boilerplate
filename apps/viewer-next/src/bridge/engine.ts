/**
 * Markdown / rich-preview host technology for the viewer shell.
 * Keep in sync with `markdown_preview_engine.hpp` (`k_markdown_preview_engine_default`).
 * App Settings may override later; the Win32 centre file preview will follow the same labels.
 */
export const ENGINE = "webview2" as const;

export type MarkdownPreviewEngine = typeof ENGINE | "webbrowser";
