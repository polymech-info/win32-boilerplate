import { widgetRegistry } from "@/UserPage/widgetRegistry";

interface WidgetProps {
  widgetId: string;
  props?: Record<string, unknown>;
  contextVariables?: Record<string, unknown>;
  pageOwner?: string | null;
  pageThumbnail?: string;
}

export function PageWidget({ widgetId, props = {}, contextVariables, pageOwner, pageThumbnail }: WidgetProps) {
  const definition = widgetRegistry.get(widgetId);
  if (definition) {
    const WidgetComponent = definition.component;
    return (
      <WidgetComponent
        {...props}
        widgetDefId={widgetId}
        isEditMode={false}
        contextVariables={contextVariables}
        pageOwner={pageOwner}
        pageThumbnail={pageThumbnail}
      />
    );
  }
  return (
    <div className="rounded-lg border border-dashed border-amber-300 bg-amber-50/70 p-4 text-sm text-amber-800 dark:border-amber-900 dark:bg-amber-950/20 dark:text-amber-200">
      Widget "{widgetId || "unknown"}" is not available in this viewer yet.
    </div>
  );
}
