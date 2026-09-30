# Test image fixtures

## Generated PNGs (`build-fixtures.mjs`)

PNG files can be **regenerated** anytime (no extra npm dependencies):

```bash
# from packages/media/cpp
npm run generate:assets
# or
node tests/assets/build-fixtures.mjs
```

| File | Size | Role |
|------|------|------|
| `tiny-1x1.png` | 1×1 | Minimal decode/encode |
| `tiny-8x8.png` | 8×8 | Small resize |
| `square-64.png` | 64×64 | Gradient-ish pattern (used by `npm run test:media`) |
| `wide-320x80.png` | 320×80 | Wide aspect |
| `tall-80x320.png` | 80×320 | Tall aspect |
| `mid-256x256.png` | 256×256 | Busier pattern |
| `photo-ish-640x360.png` | 640×360 | Larger image |
| `stripes-512x64.png` | 512×64 | Horizontal bands |
| `checker-128x128.png` | 128×128 | Checkerboard (used by IPC Unix test on Linux/macOS) |

All are **RGB 8-bit** PNG (no alpha). **`media-img` uses libvips** for processing (same stack as Sharp).

## Other folders

Subdirectories such as `in/`, `out/`, `in_webp/`, `out_webp/`, `out_jpg/`, `watermark-*`, and `bg-removed/` hold extra samples for manual or future automated tests. They are independent of `build-fixtures.mjs`.

**`service/posts/`** — not populated by `build-fixtures.mjs`. Put any `*.png` / `*.jpg` / `*.jpeg` / `*.webp` you want there; `npm run test:service:posts` runs `pm-image service posts create` on **all** of them.

**Service test artifacts** (overwritten when you run the suites): `tests/test-service-upload-responses.json`, `tests/test-service-posts-responses.json` — exact API response bodies (`--dump-raw-http`).
