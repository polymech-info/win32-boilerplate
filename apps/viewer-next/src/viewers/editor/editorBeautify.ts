import type { editor, languages } from "monaco-editor";

export type BeautifyTarget =
  | "auto"
  | "json"
  | "html"
  | "css"
  | "javascript"
  | "typescript"
  | "xml";

export const BEAUTIFY_TARGETS: { id: BeautifyTarget; label: string; hint?: string }[] = [
  { id: "auto", label: "Format (current language)" },
  { id: "json", label: "JSON" },
  { id: "html", label: "HTML" },
  { id: "css", label: "CSS" },
  { id: "javascript", label: "JavaScript" },
  { id: "typescript", label: "TypeScript" },
  { id: "xml", label: "XML" },
];

const TARGET_MONACO_LANG: Record<Exclude<BeautifyTarget, "auto">, string> = {
  json: "json",
  html: "html",
  css: "css",
  javascript: "javascript",
  typescript: "typescript",
  xml: "xml",
};

/** Languages where Monaco ships a document formatter (worker or built-in). */
const MONACO_FORMAT_LANGS = new Set([
  "json",
  "jsonc",
  "html",
  "handlebars",
  "razor",
  "css",
  "scss",
  "less",
  "javascript",
  "typescript",
  "typescriptreact",
  "javascriptreact",
  "xml",
]);

function replaceAll(model: editor.ITextModel, text: string): void {
  const full = model.getFullModelRange();
  model.pushEditOperations(
    [],
    [{ range: full, text, forceMoveMarkers: true }],
    () => null,
  );
}

function formatJsonText(text: string): string {
  return `${JSON.stringify(JSON.parse(text), null, 2)}\n`;
}

export function beautifyTargetForLanguage(language: string): BeautifyTarget | null {
  switch (language) {
    case "json":
    case "jsonc":
      return "json";
    case "html":
    case "handlebars":
    case "razor":
      return "html";
    case "css":
    case "scss":
    case "less":
      return "css";
    case "javascript":
    case "javascriptreact":
      return "javascript";
    case "typescript":
    case "typescriptreact":
      return "typescript";
    case "xml":
      return "xml";
    default:
      return null;
  }
}

export async function beautifyInMonacoEditor(
  editorInst: editor.IStandaloneCodeEditor,
  monaco: typeof import("monaco-editor"),
  target: BeautifyTarget,
  currentLanguage: string,
): Promise<{ ok: true; text: string } | { ok: false; error: string }> {
  const model = editorInst.getModel();
  if (!model) return { ok: false, error: "No document open" };

  const formatLang =
    target === "auto"
      ? currentLanguage
      : TARGET_MONACO_LANG[target];

  if (formatLang === "json" || formatLang === "jsonc") {
    try {
      const formatted = formatJsonText(model.getValue());
      replaceAll(model, formatted);
      return { ok: true, text: formatted };
    } catch (e) {
      return { ok: false, error: e instanceof Error ? e.message : "Invalid JSON" };
    }
  }

  const prevLang = model.getLanguageId();
  const needsTempLang = formatLang !== prevLang;

  if (needsTempLang) {
    monaco.editor.setModelLanguage(model, formatLang as languages.LanguageId);
  }

  if (!MONACO_FORMAT_LANGS.has(formatLang)) {
    if (needsTempLang) monaco.editor.setModelLanguage(model, prevLang);
    return { ok: false, error: `No formatter for ${formatLang}` };
  }

  try {
    await editorInst.getAction("editor.action.formatDocument")?.run();
    const text = model.getValue();
    if (needsTempLang) monaco.editor.setModelLanguage(model, prevLang);
    return { ok: true, text };
  } catch (e) {
    if (needsTempLang) monaco.editor.setModelLanguage(model, prevLang);
    return {
      ok: false,
      error: e instanceof Error ? e.message : "Format failed",
    };
  }
}
