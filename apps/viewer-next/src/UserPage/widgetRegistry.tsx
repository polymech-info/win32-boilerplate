import type { ComponentType } from "react";
import {
  CompetitorsMapWidget,
  FileBrowserWidget,
  GalleryWidget,
  HtmlWidget,
  LayoutContainerWidget,
  MarkdownTextWidget,
  MenuWidget,
  PageCardWidget,
  PhotoCardWidget,
  PhotoGridWidget,
  SupportChatWidget,
  TabsWidget,
  VideoBannerWidget,
} from "@/UserPage/widgets";
import { CategoryFeedWidget, HomeWidget } from "@/UserPage/widgets/FeedWidgets";

export interface PageWidgetRuntimeProps {
  widgetInstanceId?: string;
  contextVariables?: Record<string, unknown>;
  pageContext?: Record<string, unknown>;
  pageOwner?: string | null;
  pageThumbnail?: string;
  [key: string]: unknown;
}

export interface WidgetDefinition {
  component: ComponentType<PageWidgetRuntimeProps>;
  metadata: {
    id: string;
    name: string;
    category: string;
    description: string;
    defaultProps?: Record<string, unknown>;
  };
}

class WidgetRegistry {
  private widgets = new Map<string, WidgetDefinition>();

  register(definition: WidgetDefinition) {
    this.widgets.set(definition.metadata.id, definition);
  }

  get(id: string) {
    return this.widgets.get(id);
  }

  clear() {
    this.widgets.clear();
  }
}

export const widgetRegistry = new WidgetRegistry();

export function registerAllWidgets() {
  widgetRegistry.clear();
  register("html-widget", "HTML Content", "display", HtmlWidget);
  register("photo-grid", "Photo Grid", "custom", PhotoGridWidget);
  register("photo-card", "Photo Card", "custom", PhotoCardWidget);
  register("photo-grid-widget", "Photo Grid Widget", "custom", PhotoGridWidget);
  register("tabs-widget", "Tabs Widget", "layout", TabsWidget);
  register("gallery-widget", "Gallery", "custom", GalleryWidget);
  register("page-card", "Page Card", "custom", PageCardWidget);
  register("markdown-text", "Text Block", "display", MarkdownTextWidget);
  register("layout-container-widget", "Nested Layout Container", "custom", LayoutContainerWidget);
  register("support-chat-widget", "Support Chat Widget", "custom", SupportChatWidget);
  register("file-browser", "File Browser", "custom", FileBrowserWidget);
  register("home", "Home Feed", "display", HomeWidget);
  register("video-banner", "Hero", "display", VideoBannerWidget);
  register("category-feed", "Category Feed", "display", CategoryFeedWidget);
  register("menu-widget", "Menu", "navigation", MenuWidget);
  register("competitors-map", "Competitors Map", "custom", CompetitorsMapWidget);
}

function register(id: string, name: string, category: string, component: ComponentType<PageWidgetRuntimeProps>) {
  widgetRegistry.register({
    component,
    metadata: {
      id,
      name,
      category,
      description: name,
    },
  });
}

registerAllWidgets();
