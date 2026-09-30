# JSONTreeWalker

Keyboard-driven JSON inspector. View-only — no editing.

## Usage

```tsx
import { JSONTreeWalker } from '@/components/json';

// Basic — pass any JSON-serializable value
<JSONTreeWalker data={myObject} />

// With exit callback (e.g. return to parent view)
<JSONTreeWalker
  data={someJsonString}
  rootLabel="config"
  onExit={() => setShowWalker(false)}
/>

// Styled
<JSONTreeWalker
  data={logPayload}
  className="h-[500px] border-0 rounded-none"
/>
```

## Props

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `data` | `any` | — | JSON data to inspect. Accepts parsed objects/arrays or JSON strings (auto-parsed). |
| `rootLabel` | `string` | `"Root"` | Label shown at the breadcrumb root. |
| `className` | `string` | — | Extra CSS on the outer wrapper. |
| `onExit` | `() => void` | — | Fired on Escape / ArrowLeft at the top level. |

## Keyboard

| Key | Action |
|-----|--------|
| `↑` / `↓` | Select prev / next item |
| `→` | Drill into selected object/array |
| `←` | Go up one level (or exit at root) |
| `Home` / `End` | Jump to first / last item |
| `Escape` | Jump to root (or exit) |

The filter input at the top matches against keys and display values.
