import DOMPurify from "dompurify";
import { useEffect, useMemo, useState } from "react";
import {
  apiClient,
  fetchPage,
  fetchPicture,
  fetchPictures,
  fetchPost,
  mediaImage,
  resolvePictureUrl,
  type FeedItem,
  type MediaItem,
} from "@/UserPage/client";
import { PageLayoutView } from "@/UserPage/PageLayoutView";
import { resolvePageLayout } from "@/UserPage/pageData";
import type { Page, PageLayout } from "@/UserPage/types";
import type { PageWidgetRuntimeProps } from "@/UserPage/widgetRegistry";
import { MarkdownRenderer } from "@/viewers/markdown/MarkdownRenderer";
import { Bot, ExternalLink, Files, ImageIcon, MapPin, MessageSquare, Play } from "lucide-react";

export function MarkdownTextWidget(props: PageWidgetRuntimeProps) {
  const content = typeof props.content === "string" ? props.content : "";
  const variables = objectProp(props.variables);
  if (!content) return <EmptyCard label="No content available" icon={<ImageIcon />} />;
  return (
    <div className="pm-page-markdown-widget rounded-lg border border-slate-200 bg-white p-3 shadow-sm dark:border-slate-800 dark:bg-[#171717] md:p-6">
      <MarkdownRenderer content={content} variables={{ ...variables, ...props.contextVariables }} />
    </div>
  );
}

export function HtmlWidget(props: PageWidgetRuntimeProps) {
  const raw = substitute(typeof props.content === "string" ? props.content : "", objectProp(props.variables));
  const html = useMemo(() => DOMPurify.sanitize(raw), [raw]);
  return (
    <div
      className={`rounded-lg border border-slate-200 bg-white p-4 dark:border-slate-800 dark:bg-[#171717] ${stringProp(props.className)}`}
      dangerouslySetInnerHTML={{ __html: html }}
    />
  );
}

export function PhotoCardWidget(props: PageWidgetRuntimeProps) {
  const pictureId = stringProp(props.pictureId);
  const postId = stringProp(props.postId);
  const { data, loading } = useAsync<FeedItem | MediaItem | null>(
    (signal) => postId ? fetchPost(postId, signal) : pictureId ? fetchPicture(pictureId, signal) : Promise.resolve(null),
    [postId, pictureId],
  );
  if (loading) return <SkeletonCard />;
  const imageUrl = mediaImage(data) || props.pageThumbnail || resolvePictureUrl(props.pageOwner, pictureId);
  return (
    <MediaCard
      item={data ?? { id: pictureId, title: stringProp(props.title), description: stringProp(props.description), image_url: imageUrl }}
      imageUrl={imageUrl}
      imageFit={props.imageFit === "cover" ? "cover" : "contain"}
      showTitle={props.showTitle !== false}
      showDescription={props.showDescription !== false}
      showAuthor={props.showAuthor !== false}
      showActions={props.showActions !== false}
      overlay={props.contentDisplay === "overlay" || props.contentDisplay === "overlay-always"}
    />
  );
}

export function PhotoGridWidget(props: PageWidgetRuntimeProps) {
  const ids = arrayProp(props.pictureIds);
  const { data = [], loading } = useAsync((signal) => {
    if (ids.length > 0) return fetchPictures(ids, signal);
    const userId = stringProp(props.userId || props.pageOwner);
    return userId ? apiClient<{ data?: MediaItem[] }>(`/api/pictures?userId=${encodeURIComponent(userId)}&limit=24`, signal).then((r) => r.data ?? []) : Promise.resolve([]);
  }, [ids.join(","), props.userId, props.pageOwner]);
  if (loading) return <GridSkeleton />;
  return (
    <div className={gridColumns(props.columns)}>
      {data.map((item) => (
        <MediaCard key={item.id} item={item} imageUrl={mediaImage(item)} showAuthor={false} showActions={false} />
      ))}
    </div>
  );
}

export function GalleryWidget(props: PageWidgetRuntimeProps) {
  const ids = arrayProp(props.pictureIds);
  const { data = [], loading } = useAsync((signal) => fetchPictures(ids, signal), [ids.join(",")]);
  const [active, setActive] = useState(0);
  const current = data[active];
  if (loading) return <SkeletonCard />;
  if (!current) return <EmptyCard label="Empty gallery" icon={<ImageIcon />} />;
  return (
    <div className="rounded-xl border border-slate-200 bg-white p-3 dark:border-slate-800 dark:bg-[#171717]">
      <img
        className={`max-h-[620px] w-full rounded-lg bg-slate-100 dark:bg-slate-950 ${props.imageFit === "cover" ? "object-cover" : "object-contain"}`}
        src={mediaImage(current)}
        alt={current.title ?? ""}
      />
      {props.showTitle !== false && current.title ? <div className="mt-3 font-medium">{current.title}</div> : null}
      {props.showDescription && current.description ? <div className="text-sm text-slate-500">{current.description}</div> : null}
      <div className="pm-scroll mt-3 flex gap-2 overflow-auto">
        {data.map((item, index) => (
          <button key={item.id} type="button" onClick={() => setActive(index)} className={`h-16 w-20 shrink-0 overflow-hidden rounded border ${index === active ? "border-blue-500" : "border-slate-200 dark:border-slate-700"}`}>
            <img src={mediaImage(item)} alt="" className="h-full w-full object-cover" />
          </button>
        ))}
      </div>
    </div>
  );
}

export function PageCardWidget(props: PageWidgetRuntimeProps) {
  const pageId = stringProp(props.pageId);
  const { data, loading } = useAsync((signal) => pageId ? fetchPage(pageId, signal) : Promise.resolve(null), [pageId]);
  if (loading) return <SkeletonCard />;
  return (
    <MediaCard
      item={data ?? { id: pageId, title: "Page", description: "" }}
      imageUrl={mediaImage(data) || stringProp(data?.meta?.thumbnail)}
      showTitle={props.showTitle !== false}
      showDescription={!!props.showDescription}
      showAuthor={props.showAuthor !== false}
      showActions={props.showActions !== false}
      overlay={props.contentDisplay === "overlay" || props.contentDisplay === "overlay-always"}
    />
  );
}

export function VideoBannerWidget(props: PageWidgetRuntimeProps) {
  const imageId = stringProp(props.backgroundImageId || props.posterImageId || props.videoId);
  const imageUrl = resolvePictureUrl(props.pageOwner, imageId);
  return (
    <section className="relative overflow-hidden rounded-xl border border-slate-200 bg-slate-950 text-white dark:border-slate-800" style={{ minHeight: numberProp(props.minHeight, 500) }}>
      {imageUrl ? <img src={imageUrl} alt="" className={`absolute inset-0 h-full w-full ${props.objectFit === "contain" ? "object-contain" : "object-cover"}`} /> : null}
      <div className="absolute inset-0 bg-black/45" />
      <div className="relative flex min-h-[inherit] flex-col justify-center p-8">
        {props.heading ? <h2 className="text-3xl font-bold md:text-5xl">{String(props.heading)}</h2> : null}
        {props.description ? <p className="mt-3 max-w-2xl text-lg text-white/85">{String(props.description)}</p> : null}
        <ButtonLinks buttons={Array.isArray(props.buttons) ? props.buttons : []} />
      </div>
    </section>
  );
}

export function MenuWidget(props: PageWidgetRuntimeProps) {
  const items = Array.isArray(props.items) ? props.items : [];
  const vertical = props.orientation === "vertical";
  return (
    <nav className={`rounded-lg ${menuBg(props)} ${spacing(props.padding, "p")} ${spacing(props.margin, "m")}`}>
      <div className={`flex ${vertical ? "flex-col" : "flex-wrap"} ${alignClass(props.align)} gap-2`}>
        {items.map((item, index) => {
          const o = objectProp(item);
          return (
            <a key={index} href={menuHref(o)} className={`rounded-md px-3 py-1.5 font-medium transition hover:bg-slate-100 dark:hover:bg-slate-800 ${textSize(props.size)}`}>
              {stringProp(o.label || o.title || o.name || "Link")}
            </a>
          );
        })}
      </div>
    </nav>
  );
}

export function TabsWidget(props: PageWidgetRuntimeProps) {
  const tabs = Array.isArray(props.tabs) ? props.tabs.map(objectProp) : [];
  const [active, setActive] = useState(0);
  const current = tabs[active];
  const layout = current?.layoutData as PageLayout | undefined;
  return (
    <div className="rounded-xl border border-slate-200 bg-white dark:border-slate-800 dark:bg-[#171717]">
      <div className="flex flex-wrap gap-1 border-b border-slate-200 p-2 dark:border-slate-800">
        {tabs.map((tab, index) => (
          <button key={String(tab.id ?? index)} type="button" onClick={() => setActive(index)} className={`rounded-md px-3 py-1.5 text-sm ${active === index ? "bg-blue-600 text-white" : "text-slate-600 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800"}`}>
            {stringProp(tab.label || `Tab ${index + 1}`)}
          </button>
        ))}
      </div>
      <div className="p-3">
        {layout ? (
          <PageLayoutView layout={layout} page={pageFromContext(props)} contextVariables={props.contextVariables ?? {}} />
        ) : (
          <EmptyCard label="This tab has no layout data" icon={<Files />} />
        )}
      </div>
    </div>
  );
}

export function LayoutContainerWidget(props: PageWidgetRuntimeProps) {
  const nestedLayout = props.layoutData as PageLayout | undefined;
  const nestedPage = props.nestedPage as Page | undefined;
  const layout = nestedLayout || (nestedPage ? resolvePageLayout(nestedPage) ?? undefined : undefined);
  if (!layout) return <EmptyCard label="Nested layout is not loaded" icon={<Files />} />;
  return <PageLayoutView layout={layout} page={pageFromContext(props)} contextVariables={props.contextVariables ?? {}} />;
}

export function SupportChatWidget(props: PageWidgetRuntimeProps) {
  const buttons = Array.isArray(props.buttons) ? props.buttons.map(objectProp) : [{ label: "Ask Questions" }];
  return (
    <div className={`flex ${props.mode === "inline" ? "" : "justify-end"}`}>
      <button type="button" className="inline-flex items-center gap-2 rounded-full bg-blue-600 px-4 py-2 text-sm font-medium text-white shadow-lg">
        <MessageSquare className="h-4 w-4" />
        {stringProp(buttons[0]?.label || "Ask Questions")}
      </button>
    </div>
  );
}

export function FileBrowserWidget(props: PageWidgetRuntimeProps) {
  return (
    <div className="rounded-xl border border-slate-200 bg-white p-4 dark:border-slate-800 dark:bg-[#171717]">
      <div className="mb-3 flex items-center gap-2 font-medium">
        <Files className="h-4 w-4" />
        File Browser
      </div>
      <div className="rounded-lg bg-slate-50 p-3 font-mono text-xs text-slate-600 dark:bg-slate-950 dark:text-slate-300">
        mount={stringProp(props.mount || "root")} path={stringProp(props.path || "/")} glob={stringProp(props.glob || "*.*")}
      </div>
      <p className="mt-3 text-sm text-slate-500 dark:text-slate-400">VFS browsing is registered for page compatibility; live file access will be connected when the viewer host/client API is ported.</p>
    </div>
  );
}

export function CompetitorsMapWidget(props: PageWidgetRuntimeProps) {
  return (
    <div className="rounded-xl border border-slate-200 bg-white p-6 dark:border-slate-800 dark:bg-[#171717]">
      <div className="flex items-center gap-2 text-lg font-semibold">
        <MapPin className="h-5 w-5" />
        Competitors Map
      </div>
      <p className="mt-2 text-sm text-slate-500 dark:text-slate-400">Map widget registered. MapLibre data layers will be ported in the full interactive viewer pass.</p>
      {props.jobId ? <div className="mt-3 font-mono text-xs">jobId={String(props.jobId)}</div> : null}
    </div>
  );
}

function MediaCard({
  item,
  imageUrl,
  imageFit = "cover",
  showTitle = true,
  showDescription = false,
  showAuthor = true,
  showActions = true,
  overlay = false,
}: {
  item: Partial<FeedItem | MediaItem>;
  imageUrl: string;
  imageFit?: "cover" | "contain";
  showTitle?: boolean;
  showDescription?: boolean;
  showAuthor?: boolean;
  showActions?: boolean;
  overlay?: boolean;
}) {
  return (
    <article className="group overflow-hidden rounded-xl border border-slate-200 bg-white shadow-sm dark:border-slate-800 dark:bg-[#171717]">
      <div className="relative bg-slate-100 dark:bg-slate-950">
        {imageUrl ? (
          <img src={imageUrl} alt={item.title ?? ""} loading="lazy" className={`h-56 w-full ${imageFit === "contain" ? "object-contain" : "object-cover"}`} />
        ) : (
          <div className="flex h-56 items-center justify-center text-slate-400"><ImageIcon className="h-10 w-10" /></div>
        )}
        {overlay ? <CardText item={item} showTitle={showTitle} showDescription={showDescription} className="absolute inset-x-0 bottom-0 bg-gradient-to-t from-black/80 p-4 text-white" /> : null}
      </div>
      {!overlay ? <CardText item={item} showTitle={showTitle} showDescription={showDescription} className="p-4" /> : null}
      {(showAuthor || showActions) ? (
        <div className="flex items-center justify-between border-t border-slate-100 px-4 py-2 text-xs text-slate-500 dark:border-slate-800 dark:text-slate-400">
          <span>{showAuthor ? item.author?.display_name || item.author?.username || "" : ""}</span>
          <span>{showActions ? `${item.likes_count ?? 0} likes` : ""}</span>
        </div>
      ) : null}
    </article>
  );
}

function CardText({ item, showTitle, showDescription, className }: { item: Partial<FeedItem | MediaItem>; showTitle: boolean; showDescription: boolean; className: string }) {
  if ((!showTitle || !item.title) && (!showDescription || !item.description)) return null;
  return (
    <div className={className}>
      {showTitle && item.title ? <h3 className="font-semibold">{item.title}</h3> : null}
      {showDescription && item.description ? <p className="mt-1 line-clamp-3 text-sm opacity-80">{item.description}</p> : null}
    </div>
  );
}

function ButtonLinks({ buttons }: { buttons: unknown[] }) {
  if (buttons.length === 0) return null;
  return (
    <div className="mt-6 flex flex-wrap gap-2">
      {buttons.map((button, index) => {
        const b = objectProp(button);
        return <a key={index} href={stringProp(b.href || b.url || "#")} className="rounded-md bg-white px-4 py-2 text-sm font-medium text-slate-950">{stringProp(b.label || "Open")}</a>;
      })}
    </div>
  );
}

function EmptyCard({ label, icon }: { label: string; icon: React.ReactNode }) {
  return (
    <div className="flex min-h-36 flex-col items-center justify-center rounded-xl border border-dashed border-slate-300 bg-white/60 p-6 text-center text-sm text-slate-500 dark:border-slate-800 dark:bg-[#171717]/60 dark:text-slate-400">
      <div className="mb-2 [&_svg]:h-8 [&_svg]:w-8">{icon}</div>
      {label}
    </div>
  );
}

function SkeletonCard() {
  return <div className="h-72 animate-pulse rounded-xl bg-slate-100 dark:bg-slate-800" />;
}

function GridSkeleton() {
  return (
    <div className="grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-3">
      {Array.from({ length: 6 }).map((_, i) => <SkeletonCard key={i} />)}
    </div>
  );
}

function useAsync<T>(loader: (signal: AbortSignal) => Promise<T>, deps: React.DependencyList) {
  const [state, setState] = useState<{ data?: T; loading: boolean; error?: Error }>({ loading: true });
  useEffect(() => {
    const ac = new AbortController();
    setState((prev) => ({ data: prev.data, loading: true }));
    loader(ac.signal)
      .then((data) => !ac.signal.aborted && setState({ data, loading: false }))
      .catch((error) => !ac.signal.aborted && setState({ error: error instanceof Error ? error : new Error(String(error)), loading: false }));
    return () => ac.abort();
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
  return state;
}

export function gridColumns(columns: unknown) {
  const c = String(columns ?? "auto");
  if (c === "1") return "grid grid-cols-1 gap-4";
  if (c === "2") return "grid grid-cols-1 gap-4 md:grid-cols-2";
  if (c === "3") return "grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-3";
  if (c === "4") return "grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-4";
  return "grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-3";
}

function objectProp(value: unknown): Record<string, unknown> {
  return value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : {};
}

function arrayProp(value: unknown): string[] {
  if (Array.isArray(value)) return value.map(String).filter(Boolean);
  if (typeof value === "string") return value.split(/[,\s]+/).filter(Boolean);
  return [];
}

function stringProp(value: unknown): string {
  return typeof value === "string" ? value : value === null || value === undefined ? "" : String(value);
}

function numberProp(value: unknown, fallback: number): number {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function substitute(content: string, variables: Record<string, unknown>) {
  return content.replace(/\$\{([A-Za-z0-9_.-]+)\}|\{\{\s*([A-Za-z0-9_.-]+)\s*\}\}/g, (match, a, b) => {
    const value = variables[a || b];
    return value === null || value === undefined ? "" : typeof value === "object" ? match : String(value);
  });
}

function pageFromContext(props: PageWidgetRuntimeProps): Page {
  return {
    id: stringProp(props.pageContext?.id || "nested"),
    title: stringProp(props.pageContext?.title || "Nested Layout"),
    slug: stringProp(props.pageContext?.slug || "nested"),
    owner: props.pageOwner,
  };
}

function headingTag(level: unknown) {
  if (level === "h1") return "h1";
  if (level === "h3") return "h3";
  if (level === "h4") return "h4";
  return "h2";
}

function menuHref(item: Record<string, unknown>) {
  if (item.url) return stringProp(item.url);
  if (item.href) return stringProp(item.href);
  if (item.slug) return `/pages/${stringProp(item.slug)}`;
  return "#";
}

function alignClass(value: unknown) {
  if (value === "center") return "justify-center";
  if (value === "right") return "justify-end";
  return "justify-start";
}

function textSize(value: unknown) {
  if (value === "sm") return "text-sm";
  if (value === "lg") return "text-lg";
  if (value === "xl") return "text-xl";
  return "text-base";
}

function spacing(value: unknown, prefix: "p" | "m") {
  const map: Record<string, string> = { none: "", xs: `${prefix}-1`, sm: `${prefix}-2`, md: `${prefix}-4`, lg: `${prefix}-6`, xl: `${prefix}-8` };
  return map[String(value ?? "none")] ?? "";
}

function menuBg(props: PageWidgetRuntimeProps) {
  if (props.bg === "dark") return "bg-slate-950 text-white";
  if (props.bg === "muted") return "bg-slate-100 dark:bg-slate-900";
  if (props.bg === "primary") return "bg-blue-600 text-white";
  if (String(props.bg).startsWith("gradient")) return "bg-gradient-to-r from-blue-600 to-violet-600 text-white";
  return "";
}
