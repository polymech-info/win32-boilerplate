import path from "node:path";
import { fileURLToPath } from "node:url";

import preact from "@preact/preset-vite";
import tailwindcss from "@tailwindcss/vite";
import { visualizer } from "rollup-plugin-visualizer";
import { defineConfig } from "vite";

// Optional: file-based routes — @tanstack/router-vite-plugin + routesDirectory: "./src/routes".
// Static route tree: `src/router.tsx`.

const __dirname = path.dirname(fileURLToPath(import.meta.url));

/** Resolve a path relative to the workspace root */
const nm = (p: string) =>
  fileURLToPath(new URL(`./node_modules/${p}`, import.meta.url));

export default defineConfig({
  server: {
    fs: {
      allow: [path.resolve(__dirname, "../..")],
    },
  },
  resolve: {
    alias: [
      // Preact compat — drop-in for react + react-dom without code changes
      { find: "preact/jsx-runtime", replacement: nm("preact/jsx-runtime/dist/jsxRuntime.mjs") },
      { find: "preact/hooks", replacement: nm("preact/hooks/dist/hooks.mjs") },
      { find: "react/jsx-runtime", replacement: nm("preact/compat/jsx-runtime.mjs") },
      { find: "react/jsx-dev-runtime", replacement: nm("preact/compat/jsx-dev-runtime.mjs") },
      { find: "react-dom/client", replacement: nm("preact/compat/client.mjs") },
      { find: "react-dom",        replacement: nm("preact/compat/dist/compat.mjs") },
      { find: "react",            replacement: nm("preact/compat/dist/compat.mjs") },

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
    tailwindcss(),
    preact(),
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
});
