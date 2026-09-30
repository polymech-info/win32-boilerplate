import { marked } from "marked";

export interface MarkdownHeading {
  depth: number;
  slug: string;
  text: string;
}

export interface TocItem extends MarkdownHeading {
  children: TocItem[];
}

export interface TocOptions {
  minHeadingLevel?: number;
  maxHeadingLevel?: number;
}

export function slugify(text: string): string {
  return text
    .toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, "")
    .replace(/[\s_-]+/g, "-")
    .replace(/^-+|-+$/g, "");
}

function stripInlineMarkdown(text: string): string {
  return text
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/[*_]{1,3}(.+?)[*_]{1,3}/g, "$1")
    .replace(/`([^`]+)`/g, "$1");
}

export function extractHeadings(content: string): MarkdownHeading[] {
  const tokens = marked.lexer(content);
  const headings: MarkdownHeading[] = [];

  marked.walkTokens(tokens, (token) => {
    if (token.type !== "heading") return;
    const text = stripInlineMarkdown(token.text);
    headings.push({
      depth: token.depth,
      text,
      slug: slugify(text),
    });
  });

  return headings;
}

export function generateToc(
  headings: MarkdownHeading[],
  { minHeadingLevel = 2, maxHeadingLevel = 4 }: TocOptions = {},
): TocItem[] {
  const toc: TocItem[] = [];
  for (const heading of headings) {
    if (heading.depth < minHeadingLevel || heading.depth > maxHeadingLevel) continue;
    injectChild(toc, { ...heading, children: [] });
  }
  return toc;
}

function injectChild(items: TocItem[], item: TocItem): void {
  const lastItem = items.at(-1);
  if (!lastItem || lastItem.depth >= item.depth) {
    items.push(item);
    return;
  }
  injectChild(lastItem.children, item);
}
