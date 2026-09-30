import { useEffect, useMemo, useState } from "react";
import { ChevronDown, ChevronRight, Search } from "lucide-react";
import type { MarkdownHeading, TocItem } from "@/viewers/markdown/toc";
import { generateToc } from "@/viewers/markdown/toc";

export function TableOfContents({
  headings,
  className = "",
  minHeadingLevel = 2,
  maxHeadingLevel = 4,
}: {
  headings: MarkdownHeading[];
  className?: string;
  minHeadingLevel?: number;
  maxHeadingLevel?: number;
}) {
  const [searchQuery, setSearchQuery] = useState("");
  const [activeId, setActiveId] = useState("");

  const fullToc = useMemo(
    () => generateToc(headings, { minHeadingLevel, maxHeadingLevel }),
    [headings, minHeadingLevel, maxHeadingLevel],
  );

  const filteredToc = useMemo(() => {
    const query = searchQuery.trim().toLowerCase();
    if (!query) return fullToc;

    const filterTree = (items: TocItem[]): TocItem[] =>
      items
        .map((item) => {
          const children = filterTree(item.children);
          if (item.text.toLowerCase().includes(query) || children.length > 0) {
            return { ...item, children };
          }
          return null;
        })
        .filter((item): item is TocItem => item !== null);

    return filterTree(fullToc);
  }, [fullToc, searchQuery]);

  useEffect(() => {
    if (!headings.length) return;
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) {
            setActiveId(entry.target.id);
            break;
          }
        }
      },
      { rootMargin: "-20% 0px -35% 0px" },
    );

    for (const { slug } of headings) {
      const element = document.getElementById(slug);
      if (element) observer.observe(element);
    }

    return () => observer.disconnect();
  }, [headings]);

  if (!fullToc.length) return null;

  return (
    <div className={className}>
      <div className="pm-toc-search">
        <Search aria-hidden="true" />
        <input
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          placeholder="Search headings..."
          autoComplete="off"
          spellCheck={false}
        />
      </div>
      <nav className="pm-toc-nav" aria-label="Table of contents">
        <TableOfContentsList toc={filteredToc} activeId={activeId} defaultOpen={!!searchQuery} />
      </nav>
    </div>
  );
}

function TableOfContentsList({
  toc,
  activeId,
  depth = 0,
  defaultOpen = false,
}: {
  toc: TocItem[];
  activeId: string;
  depth?: number;
  defaultOpen?: boolean;
}) {
  if (!toc.length) return null;
  return (
    <ul className={`pm-toc-list ${depth > 0 ? "pm-toc-list--nested" : ""}`}>
      {toc.map((heading, index) => (
        <TocItemRow
          key={`${heading.slug}-${index}`}
          heading={heading}
          activeId={activeId}
          depth={depth}
          defaultOpen={defaultOpen}
        />
      ))}
    </ul>
  );
}

function TocItemRow({
  heading,
  activeId,
  depth,
  defaultOpen,
}: {
  heading: TocItem;
  activeId: string;
  depth: number;
  defaultOpen: boolean;
}) {
  const [isOpen, setIsOpen] = useState(defaultOpen || depth < 1);
  const hasChildren = heading.children.length > 0;
  const isActive = activeId === heading.slug;
  const isChildActive = useMemo(
    () => containsActiveHeading(heading.children, activeId),
    [heading.children, activeId],
  );

  useEffect(() => {
    if (isChildActive || defaultOpen) setIsOpen(true);
  }, [isChildActive, defaultOpen]);

  return (
    <li className="pm-toc-item">
      <div className={`pm-toc-row ${isActive ? "pm-toc-row--active" : ""}`}>
        <button
          type="button"
          className={`pm-toc-disclosure ${hasChildren ? "" : "pm-toc-disclosure--empty"}`}
          aria-label={isOpen ? "Collapse heading" : "Expand heading"}
          onClick={(e) => {
            e.preventDefault();
            if (hasChildren) setIsOpen((v) => !v);
          }}
        >
          {hasChildren ? (
            isOpen ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />
          ) : (
            <span aria-hidden="true" />
          )}
        </button>
        <a href={`#${heading.slug}`} className="pm-toc-link">
          {heading.text}
        </a>
      </div>
      {hasChildren && isOpen ? (
        <TableOfContentsList
          toc={heading.children}
          activeId={activeId}
          depth={depth + 1}
          defaultOpen={defaultOpen}
        />
      ) : null}
    </li>
  );
}

function containsActiveHeading(items: TocItem[], activeId: string): boolean {
  if (!activeId) return false;
  return items.some((item) => item.slug === activeId || containsActiveHeading(item.children, activeId));
}
