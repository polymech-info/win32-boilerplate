import { Link, Outlet, createRootRoute, createRoute, createRouter, useRouterState } from "@tanstack/react-router";

import { ChatHome } from "@/chat/ChatHome";
import { HomePage } from "@/chat/HomePage";
import { ChatSessionView } from "@/chat/ChatSessionView";

/** `file://` / WKWebView `loadFileURL` — pathname is a full filesystem path, not `/chat.html` like WebView2 virtual host. */
function pathnameIsEmbeddedChatHtml(pathname: string): boolean {
  if (pathname === "/chat.html") return true;
  if (pathname.endsWith("/chat.html")) return true;
  if (pathname.endsWith("\\chat.html")) return true;
  return false;
}

function RouterNotFound() {
  return (
    <article className="panel">
      <h1 className="mt-0 text-lg font-semibold">404</h1>
      <p className="muted">No route matched this URL.</p>
      <p className="mt-4">
        <Link to="/" className="link">
          Home
        </Link>
      </p>
    </article>
  );
}

function RootLayout() {
  const pathname = useRouterState({ select: (s) => s.location.pathname });
  const fullHeightChat =
    pathname === "/" ||
    pathname === "/chat" ||
    pathname === "/chat.html" ||
    /^\/chat\/.+/.test(pathname) ||
    pathnameIsEmbeddedChatHtml(pathname);
  return (
    <div className="layout">
      <main className={fullHeightChat ? "main main--chat" : "main"}>
        <Outlet />
      </main>
    </div>
  );
}

const rootRoute = createRootRoute({
  component: RootLayout,
});

const indexRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/",
  component: HomePage,
});

/** WebView2 virtual host loads `…/chat.html` — pathname is `/chat.html`, not `/`. */
const chatHtmlRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "chat.html",
  component: HomePage,
});

/** Flat `/chat` + `/chat/$id` under root so `/chat` matches (nested `path: '/'` index was flaky). */
const chatSessionRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/chat/$sessionId",
  component: function ChatSessionRoute() {
    const { sessionId } = chatSessionRoute.useParams();
    return <ChatSessionView sessionId={sessionId} />;
  },
});

const chatHubRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/chat",
  component: ChatHome,
});

/**
 * macOS PixelWiz / `file://` loads `chat.html` with a real filesystem pathname (not WebView2’s `/chat.html`).
 * Catch-all via splat `$`. TanStack forbids `id` and `path` on the *same* `createRoute()` (router-core route.ts),
 * so we use a pathless layout (`id` only) + child (`path: '$'` only). WebView2 still hits `chatHtmlRoute` first.
 */
const chatHtmlFileUrlCatchLayout = createRoute({
  getParentRoute: () => rootRoute,
  id: "chatHtmlFileUrlCatch",
  component: () => <Outlet />,
});

const chatHtmlFileUrlSplatRoute = createRoute({
  getParentRoute: () => chatHtmlFileUrlCatchLayout,
  path: "$",
  component: function ChatHtmlFileUrlSplat() {
    const pathname = useRouterState({ select: (s) => s.location.pathname });
    if (pathnameIsEmbeddedChatHtml(pathname)) return <HomePage />;
    return <RouterNotFound />;
  },
});

const routeTree = rootRoute.addChildren([
  indexRoute,
  chatHtmlRoute,
  chatSessionRoute,
  chatHubRoute,
  chatHtmlFileUrlCatchLayout.addChildren([chatHtmlFileUrlSplatRoute]),
]);

export const router = createRouter({
  routeTree,
  scrollRestoration: true,
  defaultNotFoundComponent: RouterNotFound,
});

declare module "@tanstack/react-router" {
  interface Register {
    router: typeof router;
  }
}
