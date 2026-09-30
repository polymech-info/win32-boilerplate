import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { RouterProvider } from "@tanstack/react-router";

import { installPolyMechHostApi } from "@pm/shared/web/hostBridge";
import { attachChatHostNavigation } from "@/chat/hostNavigation";
import { router } from "@/router";
import { usePmChatStore } from "@/pm-chat/pmChatStore";
import "@/styles.css";

installPolyMechHostApi();
attachChatHostNavigation();

declare global {
  interface Window {
    /**
     * Optional Vite-only overrides for embed feature flags before the host sends `setStatus`.
     * Example: `window.__PM_DEV_EMBED_FEATURES = { pixlwizAuth: false };` in DevTools, then reload.
     */
    __PM_DEV_EMBED_FEATURES?: { pixlwizAuth?: boolean; pixlwizShare?: boolean };
  }
}

if (import.meta.env.DEV && typeof window !== "undefined" && window.__PM_DEV_EMBED_FEATURES) {
  const f = window.__PM_DEV_EMBED_FEATURES;
  usePmChatStore.setState((s) => ({
    embedFeatures: {
      pixlwizAuth: f.pixlwizAuth !== undefined ? !!f.pixlwizAuth : s.embedFeatures.pixlwizAuth,
      pixlwizShare: f.pixlwizShare !== undefined ? !!f.pixlwizShare : s.embedFeatures.pixlwizShare,
    },
  }));
}

if (import.meta.env.DEV) {
  router.subscribe("onResolved", (event) => {
    // Phase 4: replace custom route-change listeners / ScrollRestoration onRouteChange
    console.debug("[analytics]", event.toLocation.pathname);
  });
}

const rootEl = document.getElementById("root");
if (!rootEl) {
  throw new Error("Root element #root not found");
}

createRoot(rootEl).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
);
