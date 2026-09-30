import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import preact from "@preact/preset-vite";
import tailwindcss from "@tailwindcss/vite";
import { visualizer } from "rollup-plugin-visualizer";
import { defineConfig, loadEnv, type Plugin } from "vite";

// Optional: file-based routes — @tanstack/router-vite-plugin + routesDirectory: "./src/routes".
// Static route tree: `src/router.tsx`.

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const viewerNextSrc = path.resolve(__dirname, "..", "viewer-next", "src");
const preactCompat = path.resolve(__dirname, "node_modules", "preact", "compat", "dist", "compat.module.js");
const preactCompatClient = path.resolve(__dirname, "node_modules", "preact", "compat", "client.mjs");
const preactJsxRuntime = path.resolve(__dirname, "node_modules", "preact", "jsx-runtime", "dist", "jsxRuntime.module.js");
const preactJsxDevRuntime = path.resolve(__dirname, "node_modules", "preact", "compat", "jsx-dev-runtime.mjs");

/** Resolve a path relative to the workspace root */
const nm = (p: string) =>
  fileURLToPath(new URL(`./node_modules/${p}`, import.meta.url));

function readEnvFile(name: string): Record<string, string> {
  const file = path.resolve(__dirname, name);
  if (!fs.existsSync(file)) return {};
  const out: Record<string, string> = {};
  for (const rawLine of fs.readFileSync(file, "utf8").split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const eq = line.indexOf("=");
    if (eq <= 0) continue;
    const key = line.slice(0, eq).trim();
    let value = line.slice(eq + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    }
    out[key] = value;
  }
  return out;
}

function serveSharedHelpPlugin(): Plugin {
  const helpRoot = path.resolve(__dirname, "..", "..", "dist", "shared", "help");
  return {
    name: "pm-home-shared-help",
    configureServer(server) {
      server.middlewares.use("/help", (req, res, next) => {
        const requestPath = (req.url || "/").split(/[?#]/, 1)[0] || "/";
        let relativePath = "";
        try {
          relativePath = decodeURIComponent(requestPath).replace(/^[/\\]+/, "");
        } catch {
          res.statusCode = 400;
          res.end("Bad request");
          return;
        }

        const filePath = path.resolve(helpRoot, relativePath);
        const rel = path.relative(helpRoot, filePath);
        if (rel.startsWith("..") || path.isAbsolute(rel)) {
          res.statusCode = 403;
          res.end("Forbidden");
          return;
        }

        fs.stat(filePath, (err, stat) => {
          if (err || !stat.isFile()) {
            next();
            return;
          }
          res.setHeader("Content-Type", filePath.endsWith(".md") ? "text/markdown; charset=utf-8" : "application/octet-stream");
          fs.createReadStream(filePath).pipe(res);
        });
      });
    },
  };
}

export default defineConfig(({ mode }) => {
  const viteEnv = loadEnv(mode, __dirname, "");
  const dashedEnv = readEnvFile(".env-production");
  const homeHelpReadmeUrl =
    process.env.PM_HOME_HELP_README_URL ||
    process.env.VITE_PM_HOME_HELP_README_URL ||
    dashedEnv.PM_HOME_HELP_README_URL ||
    dashedEnv.VITE_PM_HOME_HELP_README_URL ||
    viteEnv.PM_HOME_HELP_README_URL ||
    viteEnv.VITE_PM_HOME_HELP_README_URL ||
    "/help/en/readme.md";

  return {
  define: {
    __PM_HOME_HELP_README_URL__: JSON.stringify(homeHelpReadmeUrl),
  },
  server: {
    fs: {
      allow: [path.resolve(__dirname, "../..")],
    },
  },
  resolve: {
    alias: [
      // Preact compat — drop-in for react + react-dom without code changes
      { find: "react/jsx-dev-runtime", replacement: preactJsxDevRuntime },
      { find: "react/jsx-runtime",     replacement: preactJsxRuntime },
      { find: "react-dom/client",      replacement: preactCompatClient },
      { find: "react-dom",             replacement: preactCompat },
      { find: "react",                 replacement: preactCompat },
      { find: "preact/jsx-dev-runtime", replacement: preactJsxDevRuntime },
      { find: "preact/jsx-runtime",     replacement: preactJsxRuntime },
      { find: "preact/compat/client",   replacement: preactCompatClient },
      { find: "preact/compat",          replacement: preactCompat },

      // Point every @tanstack import at its TypeScript source so Rollup
      // can tree-shake individual functions instead of entire pre-bundled files.
      // All five packages ship src/ and their source only depends on each other.
      // Subpath exports that have environment-specific variants — always use the browser/client build
      { find: "@tanstack/router-core/isServer",                 replacement: nm("@tanstack/router-core/src/isServer/client.ts") },
      { find: "@tanstack/router-core/scroll-restoration-script", replacement: nm("@tanstack/router-core/src/scroll-restoration-script/client.ts") },

      { find: "@tanstack/router-core",  replacement: nm("@tanstack/router-core/src/index.ts") },
      { find: "@tanstack/react-router", replacement: nm("@tanstack/react-router/src/index.tsx") },
      { find: "@tanstack/history",      replacement: nm("@tanstack/history/src/index.ts") },
      { find: "@tanstack/store",        replacement: nm("@tanstack/store/src/index.ts") },
      // @tanstack/react-store is NOT source-aliased: its src/useStore.ts imports
      // use-sync-external-store/shim/with-selector (CJS-only). The pre-built
      // dist/esm already has that CJS dep inlined as ESM — no interop needed.

      { find: "@viewer-next", replacement: viewerNextSrc },
      { find: "@/viewers/markdown", replacement: path.resolve(viewerNextSrc, "viewers", "markdown") },
      { find: "@/bridge", replacement: path.resolve(viewerNextSrc, "bridge") },
      { find: "@pm/shared", replacement: path.resolve(__dirname, "../shared") },
      { find: "@", replacement: path.resolve(__dirname, "src") },
    ],
  },
  // Don't pre-bundle the four source-aliased tanstack packages — let Rollup
  // see their raw TypeScript. @tanstack/react-store is intentionally omitted:
  // its source pulls in a CJS dep (use-sync-external-store/shim/with-selector)
  // that Vite can't serve as ESM without pre-bundling, so we leave it on the
  // pre-built dist/esm path where the CJS is already inlined.
  optimizeDeps: {
    exclude: [
      "@tanstack/router-core",
      "@tanstack/react-router",
      "@tanstack/history",
      "@tanstack/store",
    ],
    // use-sync-external-store ships CJS only. Vite cannot serve it as ESM
    // without pre-bundling — include the exact subpath that @tanstack/react-store
    // imports so the pre-bundler wraps it and named imports work in dev.
    include: ["use-sync-external-store/shim/with-selector"],
  },
  plugins: [
    serveSharedHelpPlugin(),
    tailwindcss(),
    preact({ reactAliasesEnabled: false }),
    visualizer({
      filename: "dist/stats.html",
      open: false,
      gzipSize: true,
      brotliSize: true,
      template: "treemap",
    }),
  ],
  build: {
    sourcemap: true,
    target: "esnext",
    modulePreload: { polyfill: false },
    // rollupOptions.output.format is a Rollup-only option not supported by Rolldown;
    // Rolldown outputs ES modules by default so it's not needed.
  },
  };
});
