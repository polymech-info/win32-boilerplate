import { useEffect, useMemo, useState } from "react";
import type React from "react";
import {
  Camera,
  Clock,
  ExternalLink,
  FileText,
  FolderTree,
  Grid2X2,
  Heart,
  ImageIcon,
  Layers,
  List,
  MessageCircle,
  Rows3,
  Search,
  TrendingUp,
} from "lucide-react";
import { commentsCount, mediaImage, type Category, type FeedItem, type MediaItem } from "@/UserPage/client";
import { fetchCategories, findCategoryById } from "@/UserPage/clients/client-categories";
import { type FeedContentType, fetchFeed, type FeedSortOption } from "@/UserPage/clients/client-feed";
import { fetchPictures } from "@/UserPage/clients/client-pictures";
import type { PageWidgetRuntimeProps } from "@/UserPage/widgetRegistry";

type ViewMode = "grid" | "large" | "list";
type VisibilityFilter = "invisible" | "private" | "";

const contentTypes: { value: "" | FeedContentType; label: string; icon: typeof Layers }[] = [
  { value: "", label: "All", icon: Layers },
  { value: "posts", label: "Posts", icon: ImageIcon },
  { value: "pages", label: "Pages", icon: FileText },
  { value: "pictures", label: "Pictures", icon: Camera },
  { value: "files", label: "Files", icon: FolderTree },
];

export function HomeWidget(props: PageWidgetRuntimeProps) {
  return <FeedWidget {...props} />;
}

export function CategoryFeedWidget(props: PageWidgetRuntimeProps) {
  const categoryId = stringProp(props.categoryId);
  const [categories, setCategories] = useState<Category[]>([]);

  useEffect(() => {
    const ac = new AbortController();
    fetchCategories({ includeChildren: true }, ac.signal).then(setCategories).catch(() => setCategories([]));
    return () => ac.abort();
  }, []);

  const category = categoryId ? findCategoryById(categories, categoryId) : undefined;
  const slugs = arrayProp(props.categorySlugs);
  if (category?.slug && !slugs.includes(category.slug)) slugs.push(category.slug);

  return (
    <FeedWidget
      {...props}
      categorySlugs={slugs}
      initialContentType={props.filterType === "all" ? "" : props.filterType}
      heading={props.heading || (props.showCategoryName ? category?.name : "")}
    />
  );
}

function FeedWidget(props: PageWidgetRuntimeProps) {
  const [sortBy, setSortBy] = useState<FeedSortOption>(props.sortBy === "top" ? "top" : "latest");
  const [viewMode, setViewMode] = useState<ViewMode>(viewModeProp(props.viewMode));
  const [contentType, setContentType] = useState<"" | FeedContentType>(contentTypeProp(props.initialContentType ?? props.contentType));
  const [visibilityFilter, setVisibilityFilter] = useState<VisibilityFilter>(visibilityProp(props.visibilityFilter));
  const [showCategories, setShowCategories] = useState(props.showCategories === true);
  const [selected, setSelected] = useState<FeedItem | null>(null);
  const [refreshKey, setRefreshKey] = useState(0);

  const categorySlugs = useMemo(() => arrayProp(props.categorySlugs), [props.categorySlugs]);
  const categoryIds = useMemo(() => arrayProp(props.categoryIds || props.categoryId), [props.categoryIds, props.categoryId]);
  const source = props.searchQuery ? "search" : props.feedSource || props.source || props.userId ? "user" : "home";
  const sourceId = stringProp(props.searchQuery || props.feedSourceId || props.sourceId || props.userId);
  const columns = props.columns ?? "auto";
  const showSortBar = props.showSortBar !== false;
  const showLayoutToggles = props.showLayoutToggles !== false;
  const showTitle = props.showTitle !== false;
  const showDescription = props.showDescription === true || objectProp(props.cardPreset).showDescription !== false;
  const showAuthor = props.showAuthor !== false;
  const showSocial = props.showSocial !== false && props.showActions !== false;

  const { data: feed, loading, error } = useAsync((signal) => {
    if (contentType === "pictures") return Promise.resolve([] as FeedItem[]);
    return fetchFeed({
      source: source as "home" | "collection" | "tag" | "user" | "widget" | "search",
      sourceId,
      sortBy,
      categorySlugs,
      categoryIds,
      contentType: contentType || undefined,
      visibilityFilter: visibilityFilter || undefined,
      limit: numberProp(props.limit, 24),
      signal,
    });
  }, [source, sourceId, sortBy, categorySlugs.join(","), categoryIds.join(","), contentType, visibilityFilter, refreshKey]);

  const { data: pictures = [], loading: picturesLoading } = useAsync((signal) => {
    if (contentType !== "pictures") return Promise.resolve([] as MediaItem[]);
    return fetchPictures({ userId: stringProp(props.userId || props.pageOwner) || undefined }, signal).then((result) => result.data ?? []);
  }, [contentType, props.userId, props.pageOwner, refreshKey]);

  const items = contentType === "pictures" ? pictures : feed ?? [];
  const Heading = headingTag(props.headingLevel);

  return (
    <section className={`pm-home-widget rounded-lg bg-white/70 p-2 dark:bg-slate-900/40 md:p-6 ${props.center ? "mx-auto max-w-7xl" : ""}`}>
      <div className="mb-4 flex flex-wrap items-center justify-between gap-3 px-1 md:px-4">
        <div className="flex min-w-0 flex-wrap items-center gap-3">
          {props.heading ? (
            <Heading className="max-w-[50vw] truncate text-xl font-bold text-slate-950 dark:text-slate-50">
              {String(props.heading)}
            </Heading>
          ) : null}
          {showSortBar ? (
            <div className="flex flex-wrap items-center gap-1">
              <SegmentButton active={sortBy === "latest"} onClick={() => setSortBy("latest")} icon={<Clock />}>Latest</SegmentButton>
              <SegmentButton active={sortBy === "top"} onClick={() => setSortBy("top")} icon={<TrendingUp />}>Top</SegmentButton>
              <SegmentButton active={showCategories} onClick={() => setShowCategories((v) => !v)} icon={<FolderTree />}>Categories</SegmentButton>
              <select className="h-8 rounded-md border border-slate-200 bg-white px-2 text-sm dark:border-slate-700 dark:bg-slate-950" value={contentType} onChange={(e) => setContentType(e.target.value as "" | FeedContentType)}>
                {contentTypes.map((type) => <option key={type.value || "all"} value={type.value}>{type.label}</option>)}
              </select>
              {props.isOwnProfile ? (
                <select className="h-8 rounded-md border border-slate-200 bg-white px-2 text-sm dark:border-slate-700 dark:bg-slate-950" value={visibilityFilter} onChange={(e) => setVisibilityFilter(e.target.value as VisibilityFilter)}>
                  <option value="">All visibility</option>
                  <option value="invisible">Invisible</option>
                  <option value="private">Private</option>
                </select>
              ) : null}
              <button type="button" className="h-8 rounded-md border border-slate-200 px-2 text-sm dark:border-slate-700" onClick={() => setRefreshKey((v) => v + 1)}>Refresh</button>
            </div>
          ) : null}
        </div>
        {showLayoutToggles ? (
          <div className="flex items-center rounded-md border border-slate-200 bg-white p-0.5 dark:border-slate-700 dark:bg-slate-950">
            <IconButton active={viewMode === "grid"} onClick={() => setViewMode("grid")} icon={<Grid2X2 />} label="Grid" />
            <IconButton active={viewMode === "large"} onClick={() => setViewMode("large")} icon={<Rows3 />} label="Large" />
            <IconButton active={viewMode === "list"} onClick={() => setViewMode("list")} icon={<List />} label="List" />
          </div>
        ) : null}
      </div>

      <div className={showCategories ? "grid gap-4 md:grid-cols-[220px_minmax(0,1fr)]" : ""}>
        {showCategories ? <CategorySidebar selectedSlugs={categorySlugs} /> : null}
        <div className={viewMode === "list" ? "min-h-[420px] overflow-hidden rounded-xl border border-slate-200 bg-white dark:border-slate-800 dark:bg-[#171717]" : ""}>
          {loading || picturesLoading ? (
            <GridSkeleton mode={viewMode} />
          ) : error ? (
            <EmptyState label={error.message} icon={<Search />} />
          ) : items.length === 0 ? (
            <EmptyState label={contentType === "pictures" ? "No pictures yet" : "No feed items yet"} icon={<ImageIcon />} />
          ) : viewMode === "list" ? (
            <ListFeed items={items} selected={selected} onSelect={setSelected} showDescription={showDescription} showAuthor={showAuthor} showSocial={showSocial} />
          ) : (
            <CardFeed items={items} mode={viewMode} columns={columns} showTitle={showTitle} showDescription={showDescription} showAuthor={showAuthor} showSocial={showSocial} />
          )}
        </div>
      </div>
    </section>
  );
}

function CategorySidebar({ selectedSlugs }: { selectedSlugs: string[] }) {
  const { data = [], loading } = useAsync((signal) => fetchCategories({ includeChildren: true }, signal), []);
  if (loading) return <div className="h-80 animate-pulse rounded-xl bg-slate-100 dark:bg-slate-800" />;
  return (
    <aside className="max-h-[70vh] overflow-auto rounded-xl border border-slate-200 bg-white p-3 text-sm dark:border-slate-800 dark:bg-[#171717]">
      <div className="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500">Categories</div>
      <CategoryList categories={data} selectedSlugs={selectedSlugs} />
    </aside>
  );
}

function CategoryList({ categories, selectedSlugs, depth = 0 }: { categories: Category[]; selectedSlugs: string[]; depth?: number }) {
  return (
    <div className={depth ? "ml-3 border-l border-slate-100 pl-2 dark:border-slate-800" : ""}>
      {categories.map((category) => (
        <div key={category.id}>
          <div className={`rounded px-2 py-1 ${selectedSlugs.includes(category.slug) ? "bg-blue-50 text-blue-700 dark:bg-blue-950/40 dark:text-blue-200" : "text-slate-600 dark:text-slate-300"}`}>
            {category.name}
          </div>
          {category.children?.length ? <CategoryList categories={category.children.map((c) => c.child)} selectedSlugs={selectedSlugs} depth={depth + 1} /> : null}
        </div>
      ))}
    </div>
  );
}

function CardFeed({ items, mode, columns, showTitle, showDescription, showAuthor, showSocial }: {
  items: Array<FeedItem | MediaItem>;
  mode: ViewMode;
  columns: unknown;
  showTitle: boolean;
  showDescription: boolean;
  showAuthor: boolean;
  showSocial: boolean;
}) {
  return (
    <div className={mode === "large" ? "mx-auto grid max-w-4xl grid-cols-1 gap-5" : gridColumns(columns)}>
      {items.map((item) => (
        <FeedCard key={item.id} item={item} large={mode === "large"} showTitle={showTitle} showDescription={showDescription} showAuthor={showAuthor} showSocial={showSocial} />
      ))}
    </div>
  );
}

function ListFeed({ items, selected, onSelect, showDescription, showAuthor, showSocial }: {
  items: Array<FeedItem | MediaItem>;
  selected: FeedItem | null;
  onSelect: (item: FeedItem) => void;
  showDescription: boolean;
  showAuthor: boolean;
  showSocial: boolean;
}) {
  return (
    <div className="grid h-full min-h-[420px] md:grid-cols-[minmax(260px,380px)_minmax(0,1fr)]">
      <div className="pm-scroll overflow-auto border-r border-slate-200 dark:border-slate-800">
        {items.map((item) => (
          <button key={item.id} type="button" onClick={() => onSelect(item as FeedItem)} className={`flex w-full items-start gap-3 border-b border-slate-100 p-3 text-left hover:bg-slate-50 dark:border-slate-800 dark:hover:bg-slate-900 ${selected?.id === item.id ? "bg-slate-50 dark:bg-slate-900" : ""}`}>
            <Thumb item={item} className="h-16 w-16" />
            <div className="min-w-0 flex-1">
              <div className="truncate text-sm font-semibold">{item.title || "Untitled"}</div>
              {showDescription && item.description ? <p className="mt-1 line-clamp-2 text-xs text-slate-500">{item.description}</p> : null}
              <MetaRow item={item} showAuthor={showAuthor} showSocial={showSocial} compact />
            </div>
          </button>
        ))}
      </div>
      <div className="hidden min-h-0 overflow-auto p-4 md:block">
        {selected ? <FeedCard item={selected} large showTitle showDescription={showDescription} showAuthor={showAuthor} showSocial={showSocial} /> : <EmptyState label="Select an item to preview it" icon={<List />} />}
      </div>
    </div>
  );
}

function FeedCard({ item, large, showTitle, showDescription, showAuthor, showSocial }: {
  item: FeedItem | MediaItem;
  large?: boolean;
  showTitle: boolean;
  showDescription: boolean;
  showAuthor: boolean;
  showSocial: boolean;
}) {
  const externalUrl = objectProp(item.meta).url;
  return (
    <article className="group overflow-hidden rounded-xl border border-slate-200 bg-white shadow-sm transition hover:-translate-y-0.5 hover:shadow-md dark:border-slate-800 dark:bg-[#171717]">
      <div className="relative bg-slate-100 dark:bg-slate-950">
        <Thumb item={item} className={large ? "h-[420px] w-full" : "h-56 w-full"} />
        {externalUrl ? (
          <a href={String(externalUrl)} className="absolute right-2 top-2 rounded-full bg-black/60 p-2 text-white" target="_blank" rel="noreferrer">
            <ExternalLink className="h-4 w-4" />
          </a>
        ) : null}
      </div>
      {(showTitle || showDescription) ? (
        <div className="p-4">
          {showTitle ? <h3 className="font-semibold text-slate-950 dark:text-slate-50">{item.title || "Untitled"}</h3> : null}
          {showDescription && item.description ? <p className="mt-1 line-clamp-3 text-sm text-slate-500 dark:text-slate-400">{item.description}</p> : null}
        </div>
      ) : null}
      <MetaRow item={item} showAuthor={showAuthor} showSocial={showSocial} />
    </article>
  );
}

function MetaRow({ item, showAuthor, showSocial, compact = false }: { item: FeedItem | MediaItem; showAuthor: boolean; showSocial: boolean; compact?: boolean }) {
  if (!showAuthor && !showSocial) return null;
  return (
    <div className={`flex items-center justify-between gap-3 border-t border-slate-100 text-xs text-slate-500 dark:border-slate-800 dark:text-slate-400 ${compact ? "mt-2 border-t-0 p-0" : "px-4 py-2"}`}>
      <span className="truncate">{showAuthor ? item.author?.display_name || item.author?.username || item.user_id || "" : ""}</span>
      {showSocial ? (
        <span className="flex shrink-0 items-center gap-3">
          <span className="inline-flex items-center gap-1"><Heart className="h-3 w-3" />{item.likes_count ?? 0}</span>
          <span className="inline-flex items-center gap-1"><MessageCircle className="h-3 w-3" />{commentsCount(item)}</span>
        </span>
      ) : null}
    </div>
  );
}

function Thumb({ item, className }: { item: FeedItem | MediaItem; className: string }) {
  const imageUrl = mediaImage(item);
  if (!imageUrl) {
    return <div className={`${className} flex items-center justify-center bg-slate-100 text-slate-400 dark:bg-slate-950`}><ImageIcon className="h-10 w-10" /></div>;
  }
  return <img src={imageUrl} alt={item.title ?? ""} loading="lazy" className={`${className} object-cover`} />;
}

function SegmentButton({ active, onClick, icon, children }: { active: boolean; onClick: () => void; icon: React.ReactNode; children: React.ReactNode }) {
  return (
    <button type="button" onClick={onClick} className={`inline-flex h-8 items-center gap-1.5 rounded-md border px-2.5 text-sm ${active ? "border-blue-500 bg-blue-50 text-blue-700 dark:bg-blue-950/40 dark:text-blue-200" : "border-slate-200 bg-white text-slate-600 hover:bg-slate-50 dark:border-slate-700 dark:bg-slate-950 dark:text-slate-300"}`}>
      <span className="[&_svg]:h-4 [&_svg]:w-4">{icon}</span>
      {children}
    </button>
  );
}

function IconButton({ active, onClick, icon, label }: { active: boolean; onClick: () => void; icon: React.ReactNode; label: string }) {
  return (
    <button type="button" onClick={onClick} title={label} className={`rounded px-2 py-1.5 ${active ? "bg-blue-600 text-white" : "text-slate-500 hover:bg-slate-100 dark:hover:bg-slate-800"}`}>
      <span className="[&_svg]:h-4 [&_svg]:w-4">{icon}</span>
    </button>
  );
}

function EmptyState({ label, icon }: { label: string; icon: React.ReactNode }) {
  return (
    <div className="flex min-h-36 flex-col items-center justify-center rounded-xl border border-dashed border-slate-300 bg-white/60 p-6 text-center text-sm text-slate-500 dark:border-slate-800 dark:bg-[#171717]/60 dark:text-slate-400">
      <div className="mb-2 [&_svg]:h-8 [&_svg]:w-8">{icon}</div>
      {label}
    </div>
  );
}

function GridSkeleton({ mode }: { mode: ViewMode }) {
  return (
    <div className={mode === "list" ? "space-y-2 p-3" : "grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-3"}>
      {Array.from({ length: 6 }).map((_, i) => <div key={i} className="h-72 animate-pulse rounded-xl bg-slate-100 dark:bg-slate-800" />)}
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

function gridColumns(columns: unknown) {
  const c = String(columns ?? "auto");
  if (c === "1") return "grid grid-cols-1 gap-4";
  if (c === "2") return "grid grid-cols-1 gap-4 md:grid-cols-2";
  if (c === "3") return "grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-3";
  if (c === "4") return "grid grid-cols-1 gap-4 md:grid-cols-2 lg:grid-cols-4";
  if (c === "5") return "grid grid-cols-2 gap-3 md:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5";
  if (c === "6") return "grid grid-cols-2 gap-3 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6";
  return "grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-3";
}

function arrayProp(value: unknown): string[] {
  if (Array.isArray(value)) return value.map(String).filter(Boolean);
  if (typeof value === "string") return value.split(/[,\s]+/).filter(Boolean);
  return [];
}

function objectProp(value: unknown): Record<string, unknown> {
  return value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : {};
}

function stringProp(value: unknown): string {
  return typeof value === "string" ? value : value === null || value === undefined ? "" : String(value);
}

function numberProp(value: unknown, fallback: number): number {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function contentTypeProp(value: unknown): "" | FeedContentType {
  return value === "posts" || value === "pages" || value === "pictures" || value === "files" || value === "places" ? value : "";
}

function visibilityProp(value: unknown): VisibilityFilter {
  return value === "invisible" || value === "private" ? value : "";
}

function viewModeProp(value: unknown): ViewMode {
  return value === "large" || value === "list" ? value : "grid";
}

function headingTag(level: unknown) {
  if (level === "h1") return "h1";
  if (level === "h3") return "h3";
  if (level === "h4") return "h4";
  return "h2";
}
