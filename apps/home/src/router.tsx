import { Link, Outlet, createRootRoute, createRoute, createRouter, useRouterState } from "@tanstack/react-router";

import { HomeHelp } from "@/home/HomeHelp";
import { HomeHome } from "@/home/Home";
import { HomeSessionView } from "@/home/HomeSessionView";

/** `file://` / WKWebView `loadFileURL` — pathname is a full filesystem path, not `/home.html` like WebView2 virtual host. */
function pathnameIsEmbeddedHomeHtml(pathname: string): boolean {
  if (pathname === "/home.html") return true;
  if (pathname.endsWith("/home.html")) return true;
  if (pathname.endsWith("\\home.html")) return true;
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
  const fullHeightHome =
    pathname === "/" ||
    pathname === "/home" ||
    pathname === "/help" ||
    pathname === "/home.html" ||
    /^\/home\/.+/.test(pathname) ||
    pathnameIsEmbeddedHomeHtml(pathname);
  return (
    <div className="layout">
      <main className={fullHeightHome ? "main main--home" : "main"}>
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
  component: HomeHome,
});

const homeRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/home",
  component: HomeHome,
});

const helpRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/help",
  component: HomeHelp,
});

/** WebView2 virtual host loads `.../home.html` — pathname is `/home.html`, not `/`. */
const homeHtmlRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "home.html",
  component: HomeHome,
});

const homeSessionRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/home/$sessionId",
  component: function HomeSessionRoute() {
    const { sessionId } = homeSessionRoute.useParams();
    return <HomeSessionView sessionId={sessionId} />;
  },
});

/**
 * macOS PixelWiz / `file://` loads `home.html` with a real filesystem pathname (not WebView2's `/home.html`).
 * Catch-all via splat `$`. TanStack forbids `id` and `path` on the *same* `createRoute()` (router-core route.ts),
 * so we use a pathless layout (`id` only) + child (`path: '$'` only). WebView2 still hits `homeHtmlRoute` first.
 */
const homeHtmlFileUrlCatchLayout = createRoute({
  getParentRoute: () => rootRoute,
  id: "homeHtmlFileUrlCatch",
  component: () => <Outlet />,
});

const homeHtmlFileUrlSplatRoute = createRoute({
  getParentRoute: () => homeHtmlFileUrlCatchLayout,
  path: "$",
  component: function HomeHtmlFileUrlSplat() {
    const pathname = useRouterState({ select: (s) => s.location.pathname });
    if (pathnameIsEmbeddedHomeHtml(pathname)) return <HomeHome />;
    return <RouterNotFound />;
  },
});

const routeTree = rootRoute.addChildren([
  indexRoute,
  homeRoute,
  helpRoute,
  homeHtmlRoute,
  homeSessionRoute,
  homeHtmlFileUrlCatchLayout.addChildren([homeHtmlFileUrlSplatRoute]),
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
