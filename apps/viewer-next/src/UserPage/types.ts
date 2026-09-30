export interface UserPagePayload {
  page: Page;
  userProfile?: UserProfile | null;
  childPages?: ChildPage[];
  userVariables?: Record<string, unknown>;
}

export interface Page {
  id: string;
  title: string;
  slug: string;
  owner?: string | null;
  parent?: string | null;
  updated_at?: string;
  content?: string | RootLayoutData | PageLayout;
  meta?: Record<string, unknown>;
  category_paths?: Array<Array<Category>>;
  categories?: unknown[];
  tags?: string[];
  parent_page?: { slug?: string; title?: string } | null;
}

export interface UserProfile {
  username?: string | null;
  display_name?: string | null;
  avatar_url?: string | null;
}

export interface ChildPage {
  id: string;
  title: string;
  slug: string;
}

export interface Category {
  id?: string;
  name?: string;
  slug?: string;
  meta?: {
    variables?: Record<string, unknown>;
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

export interface RootLayoutData {
  pages?: Record<string, PageLayout>;
  version?: string;
  lastUpdated?: number;
  containers?: AnyContainer[];
}

export interface PageLayout {
  id: string;
  name?: string;
  containers: AnyContainer[];
  createdAt?: number;
  updatedAt?: number;
}

export interface WidgetInstance {
  id: string;
  widgetId: string;
  props?: Record<string, unknown>;
  order?: number;
  rowId?: string;
  column?: number;
}

export interface LayoutContainer {
  id: string;
  type: "container";
  columns: number;
  gap?: number;
  widgets: WidgetInstance[];
  children?: LayoutContainer[];
  order?: number;
  settings?: ContainerSettings;
}

export interface FlexibleContainer {
  id: string;
  type: "flex-container";
  rows: Array<{
    id: string;
    columns: Array<{ width: number; unit: "fr" | "px" | "rem" | "%" }>;
    gap?: number;
    sizing?: "constrained" | "unconstrained";
    padding?: string;
  }>;
  widgets: WidgetInstance[];
  gap?: number;
  order?: number;
  settings?: ContainerSettings;
}

export interface ContainerSettings {
  collapsible?: boolean;
  collapsed?: boolean;
  title?: string;
  showTitle?: boolean;
  customClassName?: string;
  enabled?: boolean;
}

export type AnyContainer = LayoutContainer | FlexibleContainer;
