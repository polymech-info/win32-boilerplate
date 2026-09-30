import { useMemo, type ReactNode } from "react";

import {
  humanizeOpenApiFieldLabel,
  schemaPrimaryType,
  sortVideoOpenApiFieldKeys,
  videoOpenApiFieldVisible,
} from "@pm/shared/providers/openApiSchema";
import type { VideoOpenApiInputFlat } from "@pm/shared/providers/types";
import { t } from "./i18n.js";

export type { VideoOpenApiInputFlat } from "@pm/shared/providers/types";
export {
  coerceDefaultString,
  humanizeOpenApiFieldLabel,
  schemaPrimaryType,
  sortVideoOpenApiFieldKeys,
  videoOpenApiFieldVisible,
} from "@pm/shared/providers/openApiSchema";

type Props = {
  loading: boolean;
  error: string | null;
  flat: VideoOpenApiInputFlat | null;
  values: Record<string, string>;
  onChange: (key: string, value: string) => void;
  /** Which quick-tools section — picks i18n strings (Replicate only). */
  variant?: "video" | "image";
};

export function VideoOpenApiToolbarFields({
  loading,
  error,
  flat,
  values,
  onChange,
  variant = "video",
}: Props) {
  const titleKey = variant === "image" ? "qaImageOpenApiTitle" : "qaVideoOpenApiTitle";
  const loadingKey = variant === "image" ? "qaImageOpenApiLoading" : "qaVideoOpenApiLoading";
  const footKey = variant === "image" ? "qaImageOpenApiFootnote" : "qaVideoOpenApiFootnote";
  const keys = useMemo(() => {
    if (!flat?.properties) return [];
    const props = flat.properties;
    const out: string[] = [];
    for (const k of Object.keys(props)) {
      const p = props[k];
      if (p && typeof p === "object" && !Array.isArray(p) && videoOpenApiFieldVisible(k, p)) out.push(k);
    }
    return sortVideoOpenApiFieldKeys(out, props);
  }, [flat]);

  if (loading) {
    return <p className="text-[10px] text-slate-500 dark:text-slate-500">{t(loadingKey)}</p>;
  }
  if (error) {
    return <p className="text-[10px] text-amber-700 dark:text-amber-400">{error}</p>;
  }
  if (!flat || keys.length === 0) {
    return null;
  }

  const reqSet = new Set(flat.required || []);

  return (
    <div className="mt-2.5 border-t border-slate-200/70 pt-2.5 dark:border-slate-600/55">
      <div className="mb-2 text-[10px] font-semibold tracking-wide text-slate-500 uppercase dark:text-slate-400">
        {t(titleKey)}
      </div>
      <div className="overflow-hidden rounded-md border border-slate-200/90 bg-slate-50/90 dark:border-slate-600/55 dark:bg-surface-mute/35">
        {keys.map((key) => {
          const prop = flat.properties[key] || {};
          const ty = schemaPrimaryType(prop);
          const labelText = humanizeOpenApiFieldLabel(key, prop);
          const desc = typeof prop.description === "string" ? prop.description : "";
          const required = reqSet.has(key);
          const rawEnum = prop.enum;
          const enumList = Array.isArray(rawEnum) ? rawEnum.map((x) => String(x)) : null;
          const v = values[key] ?? "";
          const min = typeof prop.minimum === "number" ? prop.minimum : undefined;
          const max = typeof prop.maximum === "number" ? prop.maximum : undefined;
          const rowTitle = desc || labelText;
          const controlWrap = "min-w-0 flex-1 [&_.pm-dd]:max-w-none [&_input]:max-w-none";

          let control: ReactNode;
          if (ty === "boolean") {
            control = (
              <input
                id={`pm-voai-${key}`}
                type="checkbox"
                className="h-4 w-4 shrink-0 rounded border-slate-400 text-slate-700 accent-slate-700 dark:border-slate-500 dark:accent-slate-400"
                checked={v === "true" || v === "1"}
                onChange={(e) => onChange(key, e.target.checked ? "true" : "false")}
              />
            );
          } else if (enumList && enumList.length) {
            control = (
              <select
                id={`pm-voai-${key}`}
                className="pm-dd w-full min-w-0 text-[12px] leading-tight"
                value={enumList.includes(v) ? v : enumList[0] ?? ""}
                onChange={(e) => onChange(key, e.target.value)}
              >
                {enumList.map((opt) => (
                  <option key={opt} value={opt}>
                    {opt}
                  </option>
                ))}
              </select>
            );
          } else if (ty === "integer" || ty === "number") {
            control = (
              <input
                id={`pm-voai-${key}`}
                type="number"
                className="pm-input w-full min-w-0 text-[12px] leading-tight"
                value={v}
                min={min}
                max={max}
                step={ty === "integer" ? 1 : "any"}
                onChange={(e) => onChange(key, e.target.value)}
              />
            );
          } else {
            control = (
              <input
                id={`pm-voai-${key}`}
                type="text"
                className="pm-input w-full min-w-0 text-[12px] leading-tight"
                value={v}
                onChange={(e) => onChange(key, e.target.value)}
              />
            );
          }

          return (
            <div
              key={key}
              className="flex items-center gap-2 border-b border-slate-200/70 px-2.5 py-2 last:border-b-0 dark:border-slate-600/45 sm:gap-3 sm:px-3"
              title={rowTitle}
            >
              <label
                htmlFor={`pm-voai-${key}`}
                className="w-[38%] max-w-[11rem] shrink-0 select-none text-left text-[12px] font-medium leading-snug text-slate-700 dark:text-slate-200 sm:w-[34%] sm:max-w-[10.5rem]"
              >
                {labelText}
                {required ? <span className="whitespace-nowrap text-amber-600 dark:text-amber-400"> *</span> : null}
              </label>
              <div className={controlWrap}>{control}</div>
            </div>
          );
        })}
      </div>
      <p className="mt-2 text-[9px] leading-snug text-slate-500 dark:text-slate-500">{t(footKey)}</p>
    </div>
  );
}
