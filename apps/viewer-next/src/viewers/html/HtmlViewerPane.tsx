import { useEffect } from "react";
import type { ViewerWebStatus } from "@/bridge/hostBridge";
import { postViewerWebHostLog } from "@/bridge/hostBridge";

/**
 * Centre preview for local HTML files.
 *
 * The host maps the file's parent folder to the markdown-assets virtual host
 * (SetVirtualHostNameToFolderMapping), so `hostedFileUrl` is already
 * `https://<vhost>/file.html`.  Embedding it in a same-origin <iframe> gives
 * the page a base URL equal to its parent directory: relative CSS, images, and
 * scripts in the same folder all resolve correctly without any extra work.
 */
export function HtmlViewerPane({ status }: { status: ViewerWebStatus }) {
  const f = status.features ?? {};
  const err = typeof f.hostedFileError === "string" ? f.hostedFileError : "";
  const url = typeof f.hostedFileUrl === "string" ? f.hostedFileUrl : "";
  const fileName =
    typeof f.hostedFileName === "string" ? f.hostedFileName : "HTML file";

  useEffect(() => {
    postViewerWebHostLog(
      "warn",
      `[HtmlViewerPane] render kind=${status.viewerKind} url="${url}" err="${err}" fileName="${fileName}"`,
    );
  }, [url, err, fileName, status.viewerKind]);

  if (err) {
    return (
      <div className="flex h-full min-h-0 flex-1 flex-col items-center justify-center p-6 text-center text-slate-600 dark:text-slate-400">
        <p className="mb-1 text-sm font-medium">Cannot preview HTML file</p>
        <p className="text-xs text-slate-500 dark:text-slate-500">{err}</p>
      </div>
    );
  }

  if (!url) {
    return (
      <div className="flex h-full min-h-0 flex-1 items-center justify-center p-6 text-sm text-slate-500 dark:text-slate-400">
        No HTML file loaded.
      </div>
    );
  }

  return (
    <iframe
      key={url}
      src={url}
      title={fileName}
      className="h-full w-full flex-1 border-0 bg-white"
      sandbox="allow-scripts allow-same-origin allow-forms allow-popups"
      onLoad={() =>
        postViewerWebHostLog("warn", `[HtmlViewerPane] iframe onLoad url="${url}"`)
      }
      onError={() =>
        postViewerWebHostLog("error", `[HtmlViewerPane] iframe onError url="${url}"`)
      }
    />
  );
}
