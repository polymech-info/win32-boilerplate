# API Developer Notes

How `pm-image`'s REST and IPC servers fit together, and how to extend them with a new operation.


![](./flux-5.jpg)

[Relative Markdown File](./README.md)


## Layout

```
src/core/                  pure C++ workers — no I/O glue
  resize.{hpp,cpp}         libvips resize + apply_resize_options_from_json
  compress.{hpp,cpp}       MozJPEG / PNG + apply_compress_options_from_json
  transform.{hpp,cpp}      Google Gemini edit + apply_transform_options_from_json
  meta.{hpp,cpp}           Gemini description + apply_meta_options_from_json
  find.{hpp,cpp}           Name / LLM-judge image search

src/llm/                   LLM tool surface (see docs/llm-tools.md)
  tool_catalog.{hpp,cpp}   JSON-Schema descriptions of each op
  tool_executor.{hpp,cpp}  execute(name, args) → buffer worker → envelope

src/http/serve.{hpp,cpp}   cpp-httplib endpoints (/health, /v1/*, /v1/llm/*)
src/ipc/ipc_serve.{hpp,cpp} ASIO line-JSON dispatcher (TCP + Unix socket)

src/main.cpp               CLI entry point — same workers, paths-on-disk semantics
src/cmd_kbot.cpp           kbot pipeline helpers + IPC payload entry points (used by
                           `llm agent` / duplicates via polymech::kbot::LLMClient; not a separate `kbot` CLI)
```

## Contract

- **CLI** (`pm-image resize|compress|...`) writes outputs **to disk** — that's how power users consume it.
- **REST `serve`** and **IPC `ipc`** are **microservice-oriented**: bytes in via the request, bytes out via the response. **New** `/v1/<op>` handlers must **not** touch user-provided paths; use **buffer workers** end-to-end (see below).
- REST endpoints `compress` / `transform` / `meta` are **multipart-only**. Posting an `application/json` body returns **HTTP 415**.
- The legacy `POST /v1/resize` keeps **JSON** path mode (`input` / `output` on the host) for backward compatibility; its **multipart** branch may still use **private** temp files + `resize_file` even though `media::resize_buffer()` exists — **do not** copy that pattern when adding new ops.

The target pattern for **new** multipart handlers in `serve.cpp` is:

1. Reject any request whose `Content-Type` is not `multipart/form-data` (`415`).
2. Resolve options from the multipart text fields (`multipart_fields_to_json` → `apply_<op>_options_from_json`).
3. Pass `upload.content.data() / .size()` straight into the **buffer variant** of the worker (`media::<op>_buffer`). Reference parts (`reference*`) become an in-memory `std::vector<ReferenceBuffer>`.
4. `set_content(std::move(result.bytes), result.mime)` — no temp input/output files for the op itself.

`ipc_serve.cpp` mirrors this via JSON envelopes — the input bytes can be inline, and the output bytes are base64-encoded into the JSON response.

## JSON option mapping

Each core module exposes:

```cpp
void media::apply_<op>_options_from_json(const nlohmann::json& j, <Op>Options& opts);
```

Both `serve` and `ipc` call these directly, so option names are guaranteed to match across CLI flags, REST JSON, REST multipart fields, and IPC JSON. Add a new option once, in three places: the struct, the CLI parser in `main.cpp`, and the `apply_*_options_from_json` body.

## Adding a new operation

1. **Worker** — `src/core/<op>.{hpp,cpp}` with an `OptionsT`, a `<op>_file()` function for the CLI **and** a `<op>_buffer(in_data, in_size, …)` function for REST / IPC, plus `apply_<op>_options_from_json`. The buffer variant must never read or write a path.
2. **CLI** — register a subcommand in `main.cpp` (mirror the `compress`/`meta` blocks). The CLI is the only surface that calls `<op>_file()`.
3. **REST** — add a `svr.Post("/v1/<op>", …)` handler in `src/http/serve.cpp`. Reject non-multipart bodies with `415`, parse fields with `multipart_fields_to_json`, then call `<op>_buffer()` and `set_content` the result inline. **No temp files.**
4. **IPC** — add a `make_<op>_response(json)` in `src/ipc/ipc_serve.cpp` and route it from `dispatch()` (`if (op == "<op>") return make_<op>_response(j);`).
5. **Tests** — extend `orchestrator/test-media.mjs` `suiteApiRest` (multipart success + JSON-body 415 contract test) and `suiteApiIpc`.

## Test harness

```bash
npm run test:media:api      # REST + IPC, dual contract (binary + JSON)
npm run test:media:meta     # CLI meta only (dry-run + live)
npm run test:media          # full suite (REST, IPC, multipart, glob, templates, compress, meta, api)
npm run test:all            # everything including URL fetch
```
