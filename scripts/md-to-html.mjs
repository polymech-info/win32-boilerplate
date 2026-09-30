/**
 * Static HTML for bundled documentation (installer / offline help).
 * Uses marked (GFM: tables, fenced code, strikethrough, task lists) +
 * marked-highlight + highlight.js for code; github-markdown-css for layout.
 *
 *   npm run docs:html
 * Output: dist/docs-html/
 */
import { readFileSync, writeFileSync, mkdirSync, copyFileSync, existsSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
import { Marked } from 'marked';
import { markedHighlight } from 'marked-highlight';
import hljs from 'highlight.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, '..');
const outDir = join(root, 'dist', 'docs-html');

const pages = [
  {
    src: join(root, 'docs', 'product.md'),
    out: 'product.html',
    title: 'PM-Image — Overview',
    nav: 'product',
  },
  { src: join(root, 'README.md'), out: 'readme.html', title: 'pm-image — README', nav: 'readme' },
  {
    src: join(root, 'docs', 'integration.md'),
    out: 'integration.html',
    title: 'media-img — Integration (service)',
    nav: 'integration',
  },
  {
    src: join(root, 'docs', 'win32xx-ui.md'),
    out: 'win32xx-ui.html',
    title: 'pm-image — Win32++ UI (--ui-next)',
    nav: 'win32',
  },
  { src: join(root, 'docs', 'api-rest.md'), out: 'api-rest.html', title: 'pm-image — REST API', nav: 'rest' },
  { src: join(root, 'docs', 'api-uds.md'),  out: 'api-uds.html',  title: 'pm-image — IPC API',  nav: 'uds' },
  { src: join(root, 'docs', 'api-dev.md'),    out: 'api-dev.html',    title: 'pm-image — API dev notes', nav: 'apidev' },
  { src: join(root, 'docs', 'llm.md'),        out: 'llm.html',        title: 'pm-image — LLM / dev playbook', nav: 'llm' },
  { src: join(root, 'docs', 'llm-tools.md'),  out: 'llm-tools.html',  title: 'pm-image — LLM tools (catalog + executor)', nav: 'llmtools' },
];

const md = new Marked(
  markedHighlight({
    emptyLangClass: 'hljs',
    langPrefix: 'hljs language-',
    highlight(code, lang) {
      if (lang && hljs.getLanguage(lang))
        return hljs.highlight(code, { language: lang }).value;
      return hljs.highlightAuto(code).value;
    },
  })
);
md.setOptions({ gfm: true });

function escapeHtml(s) {
  return s
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function navHtml(active) {
  const items = [
    { id: 'product',     href: 'product.html',     label: 'Overview' },
    { id: 'readme',      href: 'readme.html',      label: 'README' },
    { id: 'rest',        href: 'api-rest.html',    label: 'REST API' },
    { id: 'uds',         href: 'api-uds.html',     label: 'IPC API' },
    { id: 'apidev',      href: 'api-dev.html',     label: 'API dev' },
    { id: 'llm',         href: 'llm.html',         label: 'LLM playbook' },
    { id: 'llmtools',    href: 'llm-tools.html',   label: 'LLM tools' },
    { id: 'integration', href: 'integration.html', label: 'Integration' },
    { id: 'win32',       href: 'win32xx-ui.html',  label: 'Win32++ UI' },
  ];
  return items
    .map(({ id, href, label }) =>
      id === active
        ? `<span class="doc-nav-here">${escapeHtml(label)}</span>`
        : `<a href="${href}">${escapeHtml(label)}</a>`
    )
    .join(' \u2014 ');
}

function wrapHtml(title, body, navKey) {
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>${escapeHtml(title)}</title>
  <link rel="stylesheet" href="github-markdown.css"/>
  <link rel="stylesheet" href="hljs-github.css"/>
  <style>
    body { margin: 0; background: #fff; color: #1f2328; }
    .doc-wrap { box-sizing: border-box; max-width: 980px; margin: 0 auto; padding: 24px 16px 48px; }
    .markdown-body { box-sizing: border-box; min-width: 200px; max-width: 100%; }
    .doc-nav { font-size: 14px; margin-bottom: 20px; padding-bottom: 12px; border-bottom: 1px solid #d0d7de; }
    .doc-nav a { color: #0969da; text-decoration: none; }
    .doc-nav a:hover { text-decoration: underline; }
    .doc-nav-here { font-weight: 600; }
    .markdown-body table { display: block; overflow-x: auto; }
    .markdown-body .hljs { background: transparent !important; }
  </style>
</head>
<body>
  <div class="doc-wrap markdown-body">
    <nav class="doc-nav" aria-label="Documentation">${navHtml(navKey)}</nav>
    <article>${body}</article>
  </div>
</body>
</html>
`;
}

mkdirSync(outDir, { recursive: true });

const gmfCss = join(root, 'node_modules', 'github-markdown-css', 'github-markdown.css');
const hljsCss = join(root, 'node_modules', 'highlight.js', 'styles', 'github.min.css');

if (!existsSync(gmfCss) || !existsSync(hljsCss)) {
  console.error('Missing dependencies. Run: npm install');
  process.exit(1);
}

copyFileSync(gmfCss, join(outDir, 'github-markdown.css'));
copyFileSync(hljsCss, join(outDir, 'hljs-github.css'));

for (const { src, out, title, nav } of pages) {
  if (!existsSync(src)) {
    console.warn('skip (missing):', src);
    continue;
  }
  const raw = readFileSync(src, 'utf8');
  const body = md.parse(raw);
  const html = wrapHtml(title, body, nav);
  writeFileSync(join(outDir, out), html, 'utf8');
  console.log('wrote', join('dist/docs-html', out));
}

console.log('docs:html done ->', outDir);
