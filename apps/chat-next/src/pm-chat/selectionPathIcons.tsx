/**
 * Port of pm-pics `src/modules/storage/helpers.ts` (mime / `INode` stripped — Windows path strings only).
 * Keeps extension buckets and `CATEGORY_STYLE` aligned with the file browser reference.
 */
import type { LucideIcon } from "lucide-react";
import {
  Archive,
  File,
  FileCode,
  FileSpreadsheet,
  FileText as FileTextIcon,
  Film,
  Folder,
  Image,
  Music,
  Presentation,
} from "lucide-react";

export type MimeCategory =
  | "dir"
  | "image"
  | "video"
  | "audio"
  | "code"
  | "document"
  | "archive"
  | "spreadsheet"
  | "presentation"
  | "other";

export const IMAGE_EXTS = new Set(["jpg", "jpeg", "png", "gif", "webp", "bmp", "ico"]);
export const VIDEO_EXTS = new Set(["mp4", "mov", "webm", "mkv", "avi", "flv", "wmv", "m4v", "ogv"]);
export const AUDIO_EXTS = new Set(["mp3", "wav", "ogg", "flac", "aac", "m4a", "wma", "opus"]);
export const CODE_EXTS = new Set([
  "ts",
  "tsx",
  "js",
  "jsx",
  "py",
  "rb",
  "go",
  "rs",
  "java",
  "c",
  "cpp",
  "h",
  "hpp",
  "cs",
  "swift",
  "kt",
  "sh",
  "bash",
  "zsh",
  "ps1",
  "bat",
  "cmd",
  "lua",
  "php",
  "sql",
  "r",
  "html",
  "htm",
  "css",
  "scss",
  "sass",
  "less",
  "json",
  "yaml",
  "yml",
  "toml",
  "xml",
  "vue",
  "svelte",
]);
export const DOC_EXTS = new Set(["md", "txt", "rtf", "pdf", "doc", "docx", "odt", "tex", "log"]);
export const ARCHIVE_EXTS = new Set(["zip", "rar", "7z", "tar", "gz", "bz2", "xz", "zst", "tgz"]);
export const SPREADSHEET_EXTS = new Set(["xls", "xlsx", "csv", "ods", "tsv"]);
export const PRESENTATION_EXTS = new Set(["ppt", "pptx", "odp", "key"]);

export function getExt(name?: string): string {
  if (!name) return "";
  const i = name.lastIndexOf(".");
  return i > 0 ? name.slice(i + 1).toLowerCase() : "";
}

/** Last path segment (supports `/` and `\\`). */
export function pathFileName(path: string): string {
  const parts = path.split(/[/\\]/u).filter(Boolean);
  return parts[parts.length - 1] || path;
}

/**
 * Classify a local path for strip icons. Directories: only when the path ends with `/` or `\\`
 * (host may normalize folder picks this way); otherwise extension rules match pm-pics file rows.
 */
export function getPathMimeCategory(path: string): MimeCategory {
  const p = path.trim();
  if (p.endsWith("/") || p.endsWith("\\")) return "dir";

  const name = pathFileName(p);
  const ext = getExt(name);
  if (["stl", "obj", "step", "stp", "dxf", "dwg", "iges", "igs", "blend", "3ds"].includes(ext)) {
    return "other";
  }

  if (CODE_EXTS.has(ext)) return "code";
  if (ext === "ts" || ext === "tsx") return "code";
  if (IMAGE_EXTS.has(ext)) return "image";
  if (VIDEO_EXTS.has(ext)) return "video";
  if (AUDIO_EXTS.has(ext)) return "audio";
  if (SPREADSHEET_EXTS.has(ext)) return "spreadsheet";
  if (PRESENTATION_EXTS.has(ext)) return "presentation";
  if (ARCHIVE_EXTS.has(ext)) return "archive";
  if (DOC_EXTS.has(ext)) return "document";
  return "other";
}

export const CATEGORY_STYLE: Record<MimeCategory, { icon: LucideIcon; color: string }> = {
  dir: { icon: Folder, color: "#60a5fa" },
  image: { icon: Image, color: "#22c55e" },
  video: { icon: Film, color: "#ef4444" },
  audio: { icon: Music, color: "#8b5cf6" },
  code: { icon: FileCode, color: "#3b82f6" },
  document: { icon: FileTextIcon, color: "#f59e0b" },
  archive: { icon: Archive, color: "#eab308" },
  spreadsheet: { icon: FileSpreadsheet, color: "#16a34a" },
  presentation: { icon: Presentation, color: "#ec4899" },
  other: { icon: File, color: "#94a3b8" },
};
