/**
 * Webpack embed entry (`npm run build:embed`) — same shell as Vite `main.tsx`,
 * without `import.meta` (webpack). See `webpack.config.cjs`.
 */
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { RouterProvider } from "@tanstack/react-router";

import { attachChatHostNavigation } from "@/chat/hostNavigation";
import { router } from "@/router";
import "@/styles.css";

attachChatHostNavigation();

const rootEl = document.getElementById("root");
if (!rootEl) {
  throw new Error("Root element #root not found");
}

createRoot(rootEl).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
);
