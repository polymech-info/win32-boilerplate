/**
 * Webpack embed entry (`npm run build:embed`) — same shell as Vite `main.tsx`,
 * without `import.meta` (webpack). See `webpack.config.cjs`.
 */
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { RouterProvider } from "@tanstack/react-router";

import { router } from "@/router";
import { installHostColorSchemeBridge } from "@pm/shared/theme/colorScheme";
import "@/styles.css";

installHostColorSchemeBridge();

const rootEl = document.getElementById("root");
if (!rootEl) {
  throw new Error("Root element #root not found");
}

createRoot(rootEl).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
);
