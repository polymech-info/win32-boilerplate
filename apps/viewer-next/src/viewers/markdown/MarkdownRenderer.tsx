import React, { useCallback, useMemo } from "react";
import ReactMarkdown from "react-markdown";
import rehypeRaw from "rehype-raw";
import remarkGfm from "remark-gfm";
import type { Components } from "react-markdown";
import Prism from "prismjs";
import { postToHost } from "@/bridge/hostBridge";
import { isMermaidCodeClass, isMermaidLanguage, MermaidBlock } from "@/viewers/markdown/MarkdownRenderer_Extensions";

// Prism grammars that use `extend()` must load after their base (`cpp` → `c` → `clike`, `ts` → `js` → `clike`).
import "prismjs/components/prism-clike";
import "prismjs/components/prism-javascript";
import "prismjs/components/prism-typescript";
import "prismjs/components/prism-c";
import "prismjs/components/prism-cpp";
import "prismjs/components/prism-json";
import "prismjs/components/prism-bash";
import "prismjs/components/prism-css";
import "prismjs/components/prism-markup";
import "prismjs/components/prism-python";
import "prismjs/components/prism-rust";
import "prismjs/themes/prism-tomorrow.min.css";

const stripFrontmatter = (text: string) =>
  text.replace(/^---\r?\n[\s\S]*?\r?\n---\r?\n?/, "").trimStart();

const slugify = (text: string) =>
  text
    .toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, "")
    .replace(/[\s_-]+/g, "-")
    .replace(/^-+|-+$/g, "");

const getPlainText = (children: React.ReactNode): string => {
  if (typeof children === "string" || typeof children === "number") return String(children);
  if (Array.isArray(children)) return children.map(getPlainText).join("");
  if (React.isValidElement(children)) return getPlainText((children.props as { children?: React.ReactNode }).children);
  return "";
};

function pasteCodeInConsole(text: string) {
  const code = text.replace(/\n$/, "");
  if (!code.trim()) return;
  postToHost({
    t: "cweb_bus",
    payload: {
      t: "cweb_host",
      cmd: "pasteInConsole",
      text: code,
    },
  });
}

function sendCodeToChat(text: string) {
  const code = text.replace(/\n$/, "");
  if (!code.trim()) return;
  postToHost({
    t: "cweb_bus",
    payload: {
      t: "cweb_host",
      cmd: "insertInChat",
      text: code,
    },
  });
}

function copyCodeToClipboard(text: string) {
  const code = text.replace(/\n$/, "");
  if (!code) return;
  void navigator.clipboard?.writeText(code).catch(() => {
    const ta = document.createElement("textarea");
    ta.value = code;
    ta.style.position = "fixed";
    ta.style.left = "-9999px";
    document.body.appendChild(ta);
    ta.select();
    try {
      document.execCommand("copy");
    } catch {
      /* clipboard unavailable */
    } finally {
      ta.remove();
    }
  });
}

export type MarkdownRendererProps = {
  content: string;
  className?: string;
  variables?: Record<string, unknown>;
  /** Optional document base (e.g. https://…/) for resolving relative image/link URLs */
  baseUrl?: string;
};

function substituteVariables(content: string, variables?: Record<string, unknown>): string {
  if (!variables || Object.keys(variables).length === 0) return content;
  return content.replace(/\{\{\s*([A-Za-z0-9_.-]+)\s*\}\}/g, (match, key) => {
    const value = variables[key];
    if (value === null || value === undefined) return "";
    if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") return String(value);
    return match;
  });
}

export function MarkdownRenderer({ content, className = "", variables, baseUrl }: MarkdownRendererProps) {
  const resolveUrl = useCallback(
    (url: string | undefined) => {
      if (!url) return "";
      if (url.startsWith("http://") || url.startsWith("https://") || url.startsWith("data:")) return url;
      if (!baseUrl) return url;
      try {
        return new URL(url, baseUrl).href;
      } catch {
        return url;
      }
    },
    [baseUrl],
  );

  const finalContent = useMemo(() => substituteVariables(stripFrontmatter(content), variables), [content, variables]);

  const components = useMemo<Components>(() => {
    const heading = (Tag: "h1" | "h2" | "h3" | "h4") => {
      const Comp = ({ children, ...props }: React.HTMLAttributes<HTMLHeadingElement>) => {
        const text = getPlainText(children);
        const id = slugify(text);
        return (
          <Tag id={id} {...props}>
            {children}
          </Tag>
        );
      };
      Comp.displayName = Tag;
      return Comp;
    };

    const hasImageInTree = (n: unknown): boolean => {
      if (!n || typeof n !== "object") return false;
      const o = n as { type?: string; tagName?: string; children?: unknown[] };
      if (o.type === "element" && o.tagName === "img") return true;
      if (Array.isArray(o.children)) return o.children.some(hasImageInTree);
      return false;
    };

    return {
      h1: heading("h1"),
      h2: heading("h2"),
      h3: heading("h3"),
      h4: heading("h4"),
      a: ({ href, children, ...props }) => {
        if (!href) return <span {...props}>{children}</span>;
        const resolved = resolveUrl(href);
        const ext =
          href.startsWith("http://") ||
          href.startsWith("https://") ||
          href.startsWith("mailto:") ||
          href.startsWith("tel:") ||
          href.startsWith("data:") ||
          href.startsWith("#");
        return (
          <a href={resolved} target={ext ? "_blank" : undefined} rel={ext ? "noopener noreferrer" : undefined} {...props}>
            {children}
          </a>
        );
      },
      img: ({ src, alt, ...props }) => (
        <img src={resolveUrl(src)} alt={alt ?? ""} loading="lazy" {...props} />
      ),
      p: ({ node, children, ...props }) => {
        if (hasImageInTree(node)) return <div {...props}>{children}</div>;
        return <p {...props}>{children}</p>;
      },
      table: (props) => (
        <div className="my-2 overflow-x-auto">
          <table {...props} />
        </div>
      ),
      code: ({ className: cn, children, ...props }) => {
        const match = /language-(\w+)/.exec(cn || "");
        const language = match ? match[1] : "";
        if (!match) {
          return (
            <code className={cn} {...props}>
              {children}
            </code>
          );
        }
        const text = String(children).replace(/\n$/, "");
        let prismLang = language;
        if (language === "ts" || language === "tsx") prismLang = "typescript";
        if (language === "js" || language === "jsx") prismLang = "javascript";
        if (language === "sh" || language === "shell") prismLang = "bash";
        if (language === "html" || language === "xml") prismLang = "markup";
        if (language === "c" || language === "h") prismLang = "cpp";
        if (isMermaidLanguage(language)) {
          return <MermaidBlock chart={text} />;
        }

        if (Prism.languages[prismLang]) {
          try {
            const html = Prism.highlight(text, Prism.languages[prismLang], prismLang);
            return <code className={cn} {...props} dangerouslySetInnerHTML={{ __html: html }} />;
          } catch {
            /* fall through */
          }
        }
        return (
          <code className={cn} {...props}>
            {children}
          </code>
        );
      },
      pre: ({ children, ...props }) => {
        const text = getPlainText(children);
        const child = Array.isArray(children) ? children[0] : children;
        if (React.isValidElement(child)) {
          const childProps = child.props as { className?: unknown };
          const cn = typeof childProps.className === "string" ? childProps.className : "";
          if (isMermaidCodeClass(cn)) {
            return <MermaidBlock chart={text.replace(/\n$/, "")} />;
          }
        }
        return (
          <div className="pm-code-block">
            <div className="pm-code-actions">
              <button
                type="button"
                className="pm-code-action-btn"
                title="Stage in console"
                aria-label="Stage code in console"
                onClick={() => pasteCodeInConsole(text)}
              >
                <svg viewBox="0 0 24 24" aria-hidden="true">
                  <path d="m7 8 4 4-4 4" />
                  <path d="M13 17h4" />
                  <path d="M4 5h16v14H4z" />
                </svg>
              </button>
              <button
                type="button"
                className="pm-code-action-btn"
                title="Send to chat"
                aria-label="Send code to chat"
                onClick={() => sendCodeToChat(text)}
              >
                <svg viewBox="0 0 24 24" aria-hidden="true">
                  <path d="M5 6h14v9H8l-3 3z" />
                  <path d="m13 9 3 2-3 2" />
                  <path d="M9 11h6" />
                </svg>
              </button>
              <button
                type="button"
                className="pm-code-action-btn"
                title="Copy code"
                aria-label="Copy code"
                onClick={() => copyCodeToClipboard(text)}
              >
                <svg viewBox="0 0 24 24" aria-hidden="true">
                  <path d="M8 8h10v12H8z" />
                  <path d="M6 16H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1" />
                </svg>
              </button>
            </div>
            <pre {...props} className={props.className}>
              {children}
            </pre>
          </div>
        );
      },
    };
  }, [resolveUrl]);

  return (
    <div className={`pm-md max-w-none leading-relaxed ${className}`.trim()}>
      <ReactMarkdown remarkPlugins={[remarkGfm]} rehypePlugins={[rehypeRaw]} components={components}>
        {finalContent}
      </ReactMarkdown>
    </div>
  );
}
