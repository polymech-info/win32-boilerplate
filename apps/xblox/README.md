# `@polymech/ui-next`

A focused **proof-of-concept** for the next Polymech web shell: **React 18**, **TypeScript**, **native ESM**, and **TanStack Router** with patterns we intend to use when migrating away from React Router v6.

This package is a sandbox, not a published library. It exists so we can **try routing, typing, and UX behaviors in isolation** before rolling them into production apps such as `pm-pics` or shared bundles like `@polymech/ecommerce`.

---

## Why this stack

| Capability | What we gain |
|------------|----------------|
| **Static route tree + typed links** | Invalid routes and broken `to` values surface at compile time instead of in QA. |
| **`validateSearch` + Zod** | Query strings for tabs, filters, and view modes are parsed and typed once on the route. |
| **Loaders** | Data can load with the route, reducing `useEffect` + client fetch waterfalls on big pages. |
| **Scroll restoration** | First-class support via `createRouter({ scrollRestoration: true })`, with demos that prove behavior—including **lazy-split** route chunks. |
| **Router subscriptions** | `router.subscribe('onResolved', …)` gives a single place for analytics and instrumentation. |

The detailed mapping from React Router habits to TanStack APIs lives in [`src/examples/migration/MIGRATION_EXAMPLES.md`](src/examples/migration/MIGRATION_EXAMPLES.md).

---

## Quick start

This workspace uses **npm** for this package (no pnpm required here).

```bash
cd packages/ui-next
npm install
npm run dev
```

Open the app and use the header or **`/examples`** to walk through interactive migration examples (dynamic params, splat, typed search, loaders, redirects, 404, scroll proofs, lazy routes). For the **widget registry, plugins, and extension slots** scaffold (aligned with `packages/ui/docs/widgets-api.md`), open **`/examples/widgets-system`** or browse [`src/widgets/`](src/widgets/index.ts).

```bash
npm run build    # tsc --noEmit && vite build
npm run preview  # production build preview
```

---

## Project layout

| Path | Purpose |
|------|---------|
| [`src/router.tsx`](src/router.tsx) | Root layout, `createRouter`, scroll restoration, global 404 |
| [`src/main.tsx`](src/main.tsx) | `RouterProvider`, dev-only `router.subscribe` stub for analytics |
| [`src/examples/migration/`](src/examples/migration/) | Runnable demos + [`MIGRATION_EXAMPLES.md`](src/examples/migration/MIGRATION_EXAMPLES.md) |
| [`src/widgets/`](src/widgets/index.ts) | Widget registry, `PluginManager`, `PluginAPI`, hooks, `ExtensionSlot`; demo at `/examples/widgets-system` · [Architecture diagrams](docs/widgets.md) |
| [`vite.config.ts`](vite.config.ts) | Vite + React; comment shows where `TanStackRouterVite` plugs in for file-based routes later |
| [`tsconfig.json`](tsconfig.json) | Extends `../typescript-config/base.json`, tuned for Vite + `bundler` resolution |

---

## What “compelling” looks like here

- **Scroll restoration**: `/examples/scroll-restoration` uses a live `window.scrollY` HUD. Scroll, navigate away, hit **Back**—position is restored, not reset to zero.
- **Lazy route + same restoration**: `/examples/lazy-scroll-restoration` loads UI from a **separate JS chunk** (`lazyRouteComponent` + dynamic `import`). Production builds list a dedicated `LazyScrollRestorationChunk-*.js` asset; scroll behavior matches the non-lazy demo.

---

## Relationship to the monorepo

`@polymech/ui-next` is a **`packages/*`** workspace member. Host applications may stay on their current router until migration phases are scheduled; this package does not replace them—it **informs** the migration plan with working code and checklists.

---

## License

Private to the Polymech monorepo; not published to npm.
