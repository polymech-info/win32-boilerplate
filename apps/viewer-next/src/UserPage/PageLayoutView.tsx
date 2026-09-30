import { PageWidget } from "@/UserPage/PageWidgets";
import type { AnyContainer, FlexibleContainer, LayoutContainer, Page, PageLayout, WidgetInstance } from "@/UserPage/types";

interface PageLayoutViewProps {
  layout: PageLayout;
  page: Page;
  contextVariables: Record<string, unknown>;
}

export function PageLayoutView({ layout, page, contextVariables }: PageLayoutViewProps) {
  const pageThumbnail = typeof page.meta?.thumbnail === "string" ? page.meta.thumbnail : undefined;
  return (
    <div className="space-y-6">
      {[...layout.containers]
        .sort(byOrder)
        .map((container) => (
          <ContainerView
            key={container.id}
            container={container}
            page={page}
            contextVariables={contextVariables}
            pageThumbnail={pageThumbnail}
          />
        ))}
    </div>
  );
}

function ContainerView({
  container,
  page,
  contextVariables,
  pageThumbnail,
}: {
  container: AnyContainer;
  page: Page;
  contextVariables: Record<string, unknown>;
  pageThumbnail?: string;
}) {
  if (container.settings?.enabled === false) return null;
  if (container.type === "flex-container") {
    return (
      <div className={container.settings?.customClassName ?? ""}>
        {container.settings?.showTitle ? <ContainerTitle container={container} /> : null}
        <FlexContainerView
          container={container}
          page={page}
          contextVariables={contextVariables}
          pageThumbnail={pageThumbnail}
        />
      </div>
    );
  }

  return (
    <div className={container.settings?.customClassName ?? ""}>
      {container.settings?.showTitle ? <ContainerTitle container={container} /> : null}
      <div className={gridClass(container.columns)} style={{ gap: `${container.gap ?? 16}px` }}>
        {[...container.widgets].sort(byOrder).map((widget) => (
          <WidgetView
            key={widget.id}
            widget={widget}
            page={page}
            contextVariables={contextVariables}
            pageThumbnail={pageThumbnail}
          />
        ))}
        {[...(container.children ?? [])].sort(byOrder).map((child) => (
          <div key={child.id} className="col-span-full">
            <ContainerView
              container={child}
              page={page}
              contextVariables={contextVariables}
              pageThumbnail={pageThumbnail}
            />
          </div>
        ))}
      </div>
    </div>
  );
}

function FlexContainerView({
  container,
  page,
  contextVariables,
  pageThumbnail,
}: {
  container: FlexibleContainer;
  page: Page;
  contextVariables: Record<string, unknown>;
  pageThumbnail?: string;
}) {
  return (
    <div className="space-y-4" style={{ gap: `${container.gap ?? 16}px` }}>
      {container.rows.map((row) => (
        <div
          key={row.id}
          className="grid grid-cols-1 md:grid-cols-[var(--pm-page-flex-cols)]"
          style={{
            gap: `${row.gap ?? container.gap ?? 16}px`,
            ["--pm-page-flex-cols" as string]: row.columns.map((col) => `${col.width}${col.unit}`).join(" "),
          }}
        >
          {row.columns.map((_, columnIndex) => (
            <div key={`${row.id}-${columnIndex}`} className={row.padding ?? ""}>
              {[...container.widgets]
                .filter((widget) => widget.rowId === row.id && (widget.column ?? 0) === columnIndex)
                .sort(byOrder)
                .map((widget) => (
                  <WidgetView
                    key={widget.id}
                    widget={widget}
                    page={page}
                    contextVariables={contextVariables}
                    pageThumbnail={pageThumbnail}
                  />
                ))}
            </div>
          ))}
        </div>
      ))}
    </div>
  );
}

function WidgetView({
  widget,
  page,
  contextVariables,
  pageThumbnail,
}: {
  widget: WidgetInstance;
  page: Page;
  contextVariables: Record<string, unknown>;
  pageThumbnail?: string;
}) {
  if (widget.props?.enabled === false) return null;
  return (
    <div className={`min-w-0 widget-type-${widget.widgetId}`}>
      <PageWidget
        widgetId={widget.widgetId}
        props={widget.props}
        contextVariables={contextVariables}
        pageOwner={page.owner}
        pageThumbnail={pageThumbnail}
      />
    </div>
  );
}

function ContainerTitle({ container }: { container: LayoutContainer | FlexibleContainer }) {
  return (
    <h3 className="mb-2 border-b border-slate-200 pb-2 text-sm font-medium text-slate-700 dark:border-slate-800 dark:text-slate-200">
      {container.settings?.title || "Container"}
    </h3>
  );
}

function gridClass(columns: number) {
  switch (columns) {
    case 1:
      return "grid grid-cols-1";
    case 2:
      return "grid grid-cols-1 md:grid-cols-2";
    case 3:
      return "grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3";
    case 4:
      return "grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4";
    default:
      return "grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4";
  }
}

function byOrder<T extends { order?: number }>(a: T, b: T) {
  return (a.order ?? 0) - (b.order ?? 0);
}
