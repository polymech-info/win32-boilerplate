import path from "node:path";
import { fileURLToPath } from "node:url";

import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import { visualizer } from "rollup-plugin-visualizer";
import { defineConfig } from "vite";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

/** Vite dev server + optional `vite build` (embed for WebView2 stays `npm run build:embed` / webpack). */
export default defineConfig({
  root: __dirname,
  assetsInclude: ["**/*.wasm"],
  server: {
    fs: {
      allow: [path.resolve(__dirname, "../..")],
    },
    proxy: {
      "/__pm_page_dev__/nodehub.json": {
        target: "https://pixlwiz.com",
        changeOrigin: true,
        rewrite: () => "/user/3bb4cfbf-318b-44d3-a9d3-35680e738421/pages/nodehub.json",
      },
      "/__pm_page_src_pixlwiz__": {
        target: "https://pixlwiz.com",
        changeOrigin: true,
        rewrite: (p) => {
          const encoded = p.replace(/^\/__pm_page_src_pixlwiz__\/?/, "");
          if (!encoded) return "/";
          const src = decodeURIComponent(encoded);
          const url = new URL(src);
          if (url.protocol !== "https:" || url.hostname !== "pixlwiz.com") {
            throw new Error(`Unsupported page src: ${src}`);
          }
          return `${url.pathname}${url.search}`;
        },
      },
      "/__pm_page_src_service__": {
        target: "https://service.polymech.info",
        changeOrigin: true,
        rewrite: (p) => {
          const encoded = p.replace(/^\/__pm_page_src_service__\/?/, "");
          if (!encoded) return "/";
          const src = decodeURIComponent(encoded);
          const url = new URL(src);
          if (url.protocol !== "https:" || url.hostname !== "service.polymech.info") {
            throw new Error(`Unsupported page src: ${src}`);
          }
          return `${url.pathname}${url.search}`;
        },
      },
      "/__pm_pixlwiz_api__": {
        target: "https://pixlwiz.com",
        changeOrigin: true,
        rewrite: (p) => p.replace(/^\/__pm_pixlwiz_api__/, ""),
      },
    },
  },
  resolve: {
    alias: [{ find: "@", replacement: path.resolve(__dirname, "src") }],
  },
  plugins: [
    tailwindcss(),
    react(),
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
  },
});
