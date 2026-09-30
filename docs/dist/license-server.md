# License server (pm-image)

This document describes the **sample** issuer in `apps/license-server/` and how it relates to the **Windows** `pm-image` binary. Production will replace this with a hardened deployment (Zitadel, billing, audit logs); the **cryptographic shape** and **machine limit** rules are what we standardize here.

---

## Roles

| Piece | Responsibility |
|--------|------------------|
| **Client (`pm-image`)** | Computes **machine fingerprint** (64 hex chars): `SHA256( UTF8(MachineGuid) + "\|" + volume_serial_hex )`. Verifies **Ed25519** detached signature on the license **payload** with the **embedded public key** (`src/win/license_pub_key.hpp`). Stores accepted activation as **`license.dat`** (preferred; noise-wrapped blob) or legacy **`license.json`** under `%APPDATA%\PolyMech\pm-image\`. |
| **Issuer (Hono sample)** | Authenticates requests (API key in sample). Looks up **user** in `seed/users.json`. Enforces **machines per user** (default cap + per-user override). Signs payload with **Ed25519** secret (`LICENSE_ISSUER_SECRET_HEX`). Persists issued machines in `data/machines.json`. |

---

## Server constants (product rules)

- **`MAX_MACHINES_PER_USER`** — default **3** (environment variable on the sample server). This is the **global** ceiling when a seed user does not override `max_machines`.
- **Per-user override** — `seed/users.json` may set `max_machines` per email (e.g. `3`). The issuer compares **distinct** `machine_hash` values already recorded for that user against this limit before issuing.
- **Re-issue same machine** — If the client requests a license for a `machine_hash` already on file for that user, the server **does not** consume an extra slot (idempotent re-download).

These limits are **server-side only**; the client does not encode seat count. A forged file still fails **signature** verification.

---

## License file formats

### A. `license.dat` (v1, binary)

Issuer returns **`format: "dat"`** on `POST /v1/license/issue`. The body is a **large binary blob**: random noise before and after a UTF-8 JSON **envelope** (same object as in §B). The client parses the header, skips noise, and verifies the envelope exactly as for JSON.

**Header — 32 bytes, little-endian:**

| Offset | Size | Field |
|--------|------|--------|
| 0 | 4 | Magic ASCII `PMK1` |
| 4 | 4 | Format version `1` |
| 8 | 4 | `json_offset` — byte offset from file start to first byte of envelope JSON |
| 12 | 4 | `json_length` — length of envelope JSON in bytes |
| 16 | 16 | Reserved (zero) |

**Constraints:** `json_offset >= 32`, `json_offset + json_length <= file_size`. Bytes outside `[json_offset, json_offset + json_length)` are **ignored** (random padding).

Implementation: `apps/license-server/src/license-dat.js` (issuer) and `src/win/license_file.cpp` (client).

### B. Legacy `license.json` (UTF-8 JSON)

The file is UTF-8 JSON:

```json
{
  "v": 1,
  "payload_hex": "<hex-encoded UTF-8 bytes of inner JSON>",
  "signature_hex": "<hex-encoded 64-byte Ed25519 signature>"
}
```

**Inner payload** (UTF-8 JSON string whose bytes are signed):

| Field | Type | Meaning |
|-------|------|---------|
| `v` | `1` | Payload schema version |
| `license_id` | string | Opaque id from issuer |
| `machine_hash` | string | 64 hex chars; must match client fingerprint |
| `user_email` | string | Display / support (not trusted alone) |
| `issued_at` | unix seconds | Issue time |
| `expiry` | unix seconds or `null` | If set, client rejects when `now > expiry` |

Signing uses **libsodium** `crypto_sign_detached` on the **exact** UTF-8 bytes of `JSON.stringify(inner)` (stable insertion order from the server implementation).

The **public key** baked into `pm-image` must correspond to the issuer’s **secret key** (`LICENSE_ISSUER_SECRET_HEX`).

---

## Sample API (`apps/license-server`)

| Method | Path | Notes |
|--------|------|--------|
| `GET` | `/health` | Liveness |
| `GET` | `/v1/seed-users` | Lists seeded emails and `max_machines` (no secrets) |
| `POST` | `/v1/license/issue` | Header `Authorization: Bearer <LICENSE_API_KEY>`. Body: `{ "user_email", "machine_hash", "format"?: "json" \| "dat" }`. Default **`format` omitted or `"json"`** → JSON `{ v, payload_hex, signature_hex }` for `license.json`. **`"format": "dat"`** → binary **`license.dat`** (`Content-Type: application/octet-stream`, `Content-Disposition: attachment; filename="license.dat"`). Same cryptographic envelope inside; **`LICENSE_DAT_MIN_NOISE_BYTES`** (default 8192) controls minimum random prefix+suffix size for `dat`. |

**Authentication (sample):** single shared API key (`LICENSE_API_KEY`). Production should use **Zitadel** (or similar) and bind the request to the authenticated subject instead.

**State:** `data/machines.json` maps `user_email` (lowercase) → `string[]` of machine hashes. Safe to delete in dev to reset seats.

---

## Client CLI (Windows)

| Command | Purpose |
|---------|---------|
| `pm-image license fingerprint` | Print this machine’s **64-hex fingerprint** (paste into portal / curl). |
| `pm-image license import <path>` | Copy **`license.dat`** or **`license.json`** into the app config folder; verifies on this PC before keeping. |
| `pm-image license verify` | Exit **0** if a stored license verifies on this machine, **1** otherwise (scripts / CI). |
| `pm-image status` | Trial status (unchanged); valid license **skips trial enforcement** when a stored license verifies. |

**CMake:** `FEATURE_LICENSE_FILE` (default ON) compiles license verification; `FEATURE_TRIAL_CHECK` remains the trial path when no valid license is present.

### Automated tests (repo)

- **`npm run test:license-smoke`** — runs `pm-image license fingerprint` and asserts **64 hex** output (Windows + `dist/pm-image.exe` required). Part of **`npm run test:all`**.
- **`npm run test:license-dat`** — Node **`node --test`** on `license.dat` builder/parser (no Windows binary; no network). Part of **`npm run test:all`**.
- **`npm run test:trial-protection`** — `pm-image status --json` (trial fields).  
- Full **issue → import** E2E still starts the server manually (see below).

---

## Key rotation

1. Run `npm run gen-keys` in `apps/license-server`.
2. Update **`src/win/license_pub_key.hpp`** with the printed public key array.
3. Set **`LICENSE_ISSUER_SECRET_HEX`** in the server environment to the printed secret (64-byte hex).
4. Rebuild `pm-image`. Old license files become invalid after rotation.

---

## End-to-end (dev)

1. Start server: `cd apps/license-server && npm install && npm start` (set required env vars per `apps/license-server` README).
2. **This machine (dev):** in another terminal, `cd apps/license-server` and run **`npm run issue-dev`** — runs **`dist\pm-image.exe license fingerprint`**, POSTs to the server, writes **`license-dev.json`**. **`npm run issue-dev -- --dat`** writes **`license-dev.dat`**. Needs **`packages/media/cpp/dist/pm-image.exe`** built; set **`DEV_USER_EMAIL`**, **`PM_IMAGE_EXE`**, or **`MACHINE_HASH`** (e.g. Linux CI) if needed.
3. **Manual:** `dist\pm-image.exe license fingerprint` → copy hex; create `body.json` with `user_email` and `machine_hash` (see `apps/license-server/README.md`); **`curl --data-binary @body.json`** to `/v1/license/issue`.
4. **`pm-image license import`** the saved file (`license-dev.json`, `license-test.json`, or `license.dat`) — import **re-verifies** on this PC.
5. Launch `pm-image` — trial gate skipped if license verifies.

---

## Not in the sample server

- Payments, webhooks, Zitadel OIDC, email verification
- License **revocation** lists pushed to the client
- Multi-tenant admin UI

See `docs/licensing.md` for the broader **activation (planned)** and **target** system narrative.
