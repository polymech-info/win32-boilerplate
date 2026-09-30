/**
 * Extension → Monaco built-in language id.
 * Only ids shipped in monaco-editor (see register.all.ts); unknown ext → plaintext.
 */
export const MONACO_LANG_MAP: Record<string, string> = {
  // TypeScript / JavaScript
  ts: "typescript",
  tsx: "typescript",
  js: "javascript",
  mjs: "javascript",
  cjs: "javascript",
  jsx: "javascript",

  // C / C++
  c: "c",
  h: "c",
  cpp: "cpp",
  cc: "cpp",
  cxx: "cpp",
  hpp: "cpp",
  hxx: "cpp",
  inl: "cpp",

  // Systems / native
  rs: "rust",
  go: "go",
  swift: "swift",
  java: "java",
  kt: "kotlin",
  kts: "kotlin",
  cs: "csharp",
  fs: "fsharp",
  fsi: "fsharp",
  fsx: "fsharp",
  vb: "vb",
  m: "objective-c",
  mm: "objective-c",

  // Scripting
  py: "python",
  rb: "ruby",
  pl: "perl",
  pm: "perl",
  php: "php",
  lua: "lua",
  r: "r",
  jl: "julia",
  ex: "elixir",
  exs: "elixir",
  coffee: "coffee",
  dart: "dart",
  scala: "scala",
  sc: "scala",
  clj: "clojure",
  cljs: "clojure",
  edn: "clojure",
  groovy: "java", // no groovy tokenizer; close enough for highlighting

  // Shell / batch
  sh: "shell",
  bash: "shell",
  zsh: "shell",
  fish: "shell",
  ps1: "powershell",
  psm1: "powershell",
  bat: "bat",
  cmd: "bat",

  // Web
  html: "html",
  htm: "html",
  xhtml: "html",
  css: "css",
  scss: "scss",
  sass: "scss",
  less: "less",
  svg: "xml",
  xml: "xml",
  xsl: "xml",
  xsd: "xml",
  rss: "xml",
  atom: "xml",
  mdx: "mdx",
  hbs: "handlebars",
  handlebars: "handlebars",
  pug: "pug",
  jade: "pug",
  twig: "twig",
  liquid: "liquid",

  // Data / config
  json: "json",
  jsonc: "json",
  jsonl: "json",
  ndjson: "json",
  yaml: "yaml",
  yml: "yaml",
  toml: "ini",
  ini: "ini",
  conf: "ini",
  cfg: "ini",
  env: "ini",
  properties: "ini",

  // Docs / markup
  md: "markdown",
  markdown: "markdown",
  rst: "restructuredtext",
  tex: "plaintext",

  // SQL / query
  sql: "sql",
  mysql: "mysql",
  pgsql: "pgsql",
  psql: "pgsql",
  cypher: "cypher",
  sparql: "sparql",
  redis: "redis",

  // Infra / IaC
  dockerfile: "dockerfile",
  bicep: "bicep",
  hcl: "hcl",
  tf: "hcl",
  tfvars: "hcl",
  graphql: "graphql",
  gql: "graphql",
  proto: "protobuf",
  azcli: "azcli",

  // Other monaco built-ins
  apex: "apex",
  abap: "abap",
  cameligo: "cameligo",
  csp: "csp",
  ecl: "ecl",
  flow9: "flow9",
  lexon: "lexon",
  m3: "m3",
  mips: "mips",
  msdax: "msdax",
  pascal: "pascal",
  pas: "pascal",
  pascaligo: "pascaligo",
  pla: "pla",
  postiats: "postiats",
  powerquery: "powerquery",
  pq: "powerquery",
  qsharp: "qsharp",
  qs: "qsharp",
  razor: "razor",
  cshtml: "razor",
  redshift: "redshift",
  sb: "sb",
  scheme: "scheme",
  scm: "scheme",
  ss: "scheme",
  solidity: "solidity",
  sol: "solidity",
  sophia: "sophia",
  st: "st",
  sv: "systemverilog",
  systemverilog: "systemverilog",
  tcl: "tcl",
  typespec: "typespec",
  tsp: "typespec",
  wgsl: "wgsl",

  // CAD / misc (no built-in tokenizer yet)
  scad: "plaintext",
  dxf: "plaintext",
  dot: "plaintext",
  gv: "plaintext",
  cmake: "plaintext",
  diff: "plaintext",
  patch: "plaintext",
  txt: "plaintext",
  log: "plaintext",
  csv: "plaintext",
  tsv: "plaintext",
};

/** Basename / extension → Monaco language id for auto-detect. */
export function detectMonacoLanguage(fileName: string): string {
  const lower = fileName.toLowerCase();
  if (lower === "dockerfile") return "dockerfile";
  if (lower === "makefile" || lower === "gmakefile" || lower === "cmakelists.txt") return "shell";
  if (lower === ".gitignore" || lower === ".gitattributes" || lower === ".editorconfig") return "plaintext";
  const dot = lower.lastIndexOf(".");
  if (dot < 0) return "plaintext";
  const ext = lower.slice(dot + 1);
  return MONACO_LANG_MAP[ext] ?? "plaintext";
}
