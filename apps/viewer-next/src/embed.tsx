/**
 * Webpack embed entry — single viewer.html for WebView2 RT_RCDATA.
 */
import "@/styles.css";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { ViewerApp } from "@/shell/ViewerApp";

const rootEl = document.getElementById("root");
if (!rootEl) throw new Error("#root missing");

createRoot(rootEl).render(
  <StrictMode>
    <ViewerApp />
  </StrictMode>,
);
