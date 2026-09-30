# pm-image license-server (sample)

Development-only **Hono** service that issues **Ed25519**-signed `license.json` bodies compatible with `pm-image` (`FEATURE_LICENSE_FILE`).

## Setup

```bash
cd apps/license-server
npm install
cp .env.example .env
npm start
```

The committed `.env.example` matches `packages/media/cpp/src/win/license_pub_key.hpp`. To rotate keys:

```bash
npm run gen-keys
```

Paste the printed `constexpr unsigned char …` into `license_pub_key.hpp` and set `LICENSE_ISSUER_SECRET_HEX` in `.env`.

## Dev: issue license for this PC

With the server **running** (`npm start`) and **`../../dist/pm-image.exe`** built:

```bash
npm run issue-dev
npm run issue-dev -- --dat
```

Writes **`license-dev.json`** or **`license-dev.dat`** in this folder, then run `pm-image license import …` from `dist`. See **`docs/license-server.md`** for env overrides (`DEV_USER_EMAIL`, `PM_IMAGE_EXE`, `MACHINE_HASH`).

## API

- `GET /health` — liveness.
- `GET /v1/seed-users` — lists seeded accounts (no secrets).
- `POST /v1/license/issue` — `Authorization: Bearer <LICENSE_API_KEY>`, JSON body `{ "user_email", "machine_hash", "format"?: "json" | "dat" }`. Default: JSON envelope for `license.json`. With `"format": "dat"`: binary **`license.dat`** (noise + same envelope).

## Seed user

See `seed/users.json` — default `seed@example.com` with `max_machines: 3`. Issued machines are tracked in `data/machines.json` (created at runtime).

## Try from repo root

Use a **JSON body file** so `curl` does not mangle escaping on Windows:

```bash
# 1) Machine fingerprint (Windows)
dist/pm-image.exe license fingerprint

# 2) Write body.json (replace machine_hash with output of step 1)
#    { "user_email": "seed@example.com", "machine_hash": "<64 hex chars>" }

# 3) Issue license (from repo root; server must be running: npm run license-server)
curl -s -o license-test.json -X POST http://127.0.0.1:8787/v1/license/issue ^
  -H "Authorization: Bearer dev-api-key" ^
  -H "Content-Type: application/json" ^
  --data-binary @body.json

dist/pm-image.exe license import license-test.json
```

`license import` verifies the signature on this PC before keeping the file.

Full details: `docs/license-server.md`.
