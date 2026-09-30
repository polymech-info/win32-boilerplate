#include "serve.hpp"
#include "constants.hpp"
#include "openapi_embed.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>

#include "core/compress.hpp"
#include "core/glob_paths.hpp"
#include "core/meta.hpp"
#include "core/resize.hpp"
#include "core/app_image_provider.hpp"
#include "core/transform.hpp"
#include "llm/tool_catalog.hpp"
#include "llm/tool_executor.hpp"

#include <cstdlib>

namespace fs = std::filesystem;

namespace media::http {


namespace {

std::string multipart_field_text(const httplib::Request &req, const std::string &key) {
    if (!req.has_file(key))
        return {};
    return req.get_file_value(key).content;
}

void to_lower_ascii(std::string &s) {
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
}

std::string extension_from_filename(const std::string &fn) {
    const auto dot = fn.rfind('.');
    if (dot == std::string::npos || dot + 1 >= fn.size())
        return {};
    return fn.substr(dot);
}

/** Output file extension (with dot) for temp path; default .jpg when format unset. */
std::string dot_ext_for_format(const std::string &fmt_raw) {
    std::string fmt = fmt_raw;
    to_lower_ascii(fmt);
    if (fmt.empty())
        return ".jpg";
    if (fmt == "jpeg")
        return ".jpg";
    return std::string(".") + fmt;
}

std::string mime_for_format(const std::string &fmt_raw) {
    std::string fmt = fmt_raw;
    to_lower_ascii(fmt);
    if (fmt == "jpg" || fmt == "jpeg")
        return "image/jpeg";
    if (fmt == "png")
        return "image/png";
    if (fmt == "webp")
        return "image/webp";
    if (fmt == "gif")
        return "image/gif";
    if (fmt == "avif" || fmt == "heic" || fmt == "heif")
        return "image/avif";
    if (fmt == "tif" || fmt == "tiff")
        return "image/tiff";
    if (fmt.empty())
        return "image/jpeg";
    return "application/octet-stream";
}

nlohmann::json resize_options_json_from_multipart(const httplib::Request &req) {
    nlohmann::json j;
    auto add_int = [&](const char *key) {
        const std::string s = multipart_field_text(req, key);
        if (s.empty())
            return;
        try {
            j[key] = std::stoi(s);
        } catch (...) {
        }
    };
    auto add_str = [&](const char *key) {
        const std::string s = multipart_field_text(req, key);
        if (!s.empty())
            j[key] = s;
    };
    auto add_bool = [&](const char *key) {
        const std::string s = multipart_field_text(req, key);
        if (s.empty())
            return;
        const std::string l = [&] {
            std::string x = s;
            to_lower_ascii(x);
            return x;
        }();
        if (l == "1" || l == "true" || l == "yes" || l == "on")
            j[key] = true;
        else if (l == "0" || l == "false" || l == "no" || l == "off")
            j[key] = false;
    };

    add_int("max_width");
    add_int("max_height");
    add_str("format");
    add_str("fit");
    add_str("position");
    add_str("kernel");
    add_int("quality");
    add_int("png_compression");
    add_int("rotate");
    add_str("background");
    add_bool("without_enlargement");
    add_bool("autorotate");
    add_bool("strip_metadata");
    add_bool("flip");
    add_bool("flop");
    return j;
}

bool pick_multipart_image(const httplib::Request &req, httplib::MultipartFormData &out) {
    for (const char *key : {"file", "image", "upload"}) {
        if (!req.has_file(key))
            continue;
        out = req.get_file_value(key);
        if (!out.content.empty())
            return true;
    }
    static const char *skip[] = {"max_width", "max_height", "format",      "fit",          "position",
                                 "kernel",   "quality",      "png_compression", "rotate",    "background",
                                 "without_enlargement", "autorotate", "strip_metadata", "flip", "flop"};
    for (const auto &kv : req.files) {
        bool is_skip = false;
        for (const char *s : skip) {
            if (kv.first == s) {
                is_skip = true;
                break;
            }
        }
        if (is_skip)
            continue;
        if (!kv.second.content.empty()) {
            out = kv.second;
            return true;
        }
    }
    return false;
}

/** Drop multipart text fields (no `filename`) into a JSON object so the same
    apply_*_options_from_json helpers can be reused for upload requests. */
nlohmann::json multipart_fields_to_json(const httplib::Request &req) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto &kv : req.files) {
        const httplib::MultipartFormData &part = kv.second;
        if (!part.filename.empty()) continue;   // skip the actual file part
        const std::string &name = kv.first;
        const std::string &val  = part.content;
        if (val.empty()) continue;
        // try int
        try {
            std::size_t pos = 0;
            int n = std::stoi(val, &pos);
            if (pos == val.size()) { j[name] = n; continue; }
        } catch (...) {}
        // bool
        std::string lo = val;
        to_lower_ascii(lo);
        if (lo == "true" || lo == "1" || lo == "yes" || lo == "on")  { j[name] = true;  continue; }
        if (lo == "false" || lo == "0" || lo == "no" || lo == "off") { j[name] = false; continue; }
        j[name] = val;
    }
    return j;
}

fs::path unique_temp_path(const std::string &suffix) {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec)
        return {};
    static std::mutex mtx;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mtx);
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < 64; ++i) {
        fs::path p
            = base
            / (std::string(pm::brand::k_app_id_u8) + "-upload-" + std::to_string(dist(rng)) + suffix);
        if (!fs::exists(p, ec))
            return p;
    }
    return base
        / (std::string(pm::brand::k_app_id_u8) + "-upload-" + std::to_string(dist(rng)) + suffix);
}

} // namespace

int run_server(const std::string &host, int port, const CacheServerDefaults &cache_defaults) {
    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(R"({"ok":true,"service":"media-img"})", "application/json");
    });

    svr.Get("/openapi.yaml", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(embedded_openapi_yaml(), "application/yaml");
    });

    svr.Get("/api-docs", [](const httplib::Request &, httplib::Response &res) {
        static const char k_page[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <title>pm-image REST — Swagger UI</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui.css" crossorigin="anonymous"/>
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui-bundle.js" crossorigin="anonymous"></script>
  <script>
    window.ui = SwaggerUIBundle({
      url: "/openapi.yaml",
      dom_id: "#swagger-ui",
      deepLinking: true,
      presets: [SwaggerUIBundle.presets.apis],
    });
  </script>
</body>
</html>
)HTML";
        res.set_content(k_page, "text/html; charset=utf-8");
    });

    svr.Post("/v1/resize", [cache_defaults](const httplib::Request &req, httplib::Response &res) {
        if (req.is_multipart_form_data()) {
            httplib::MultipartFormData upload;
            if (!pick_multipart_image(req, upload)) {
                res.status = 400;
                res.set_content(
                    "{\"error\":\"multipart image required; use form field file, image, or upload\"}",
                    "application/json");
                return;
            }

            nlohmann::json jopt = resize_options_json_from_multipart(req);
            media::ResizeOptions opt;
            media::apply_resize_options_from_json(jopt, opt);
            opt.cache_enabled = false;

            std::string in_ext = extension_from_filename(upload.filename);
            if (in_ext.empty())
                in_ext = ".bin";
            const fs::path tmp_in = unique_temp_path(in_ext);
            const std::string out_ext = dot_ext_for_format(opt.format);
            const fs::path tmp_out = unique_temp_path(out_ext);

            if (tmp_in.empty() || tmp_out.empty()) {
                res.status = 500;
                res.set_content(R"({"error":"temp path failed"})", "application/json");
                return;
            }

            {
                std::ofstream ofs(tmp_in, std::ios::binary);
                if (!ofs || !ofs.write(upload.content.data(), static_cast<std::streamsize>(upload.content.size())) ||
                    !ofs.flush()) {
                    std::error_code ec_rm;
                    fs::remove(tmp_in, ec_rm);
                    fs::remove(tmp_out, ec_rm);
                    res.status = 500;
                    res.set_content(R"({"error":"could not write temp input"})", "application/json");
                    return;
                }
            }

            {
                std::string err;
                if (!media::resize_file(tmp_in.string(), tmp_out.string(), opt, err)) {
                    std::error_code ec_rm;
                    fs::remove(tmp_in, ec_rm);
                    fs::remove(tmp_out, ec_rm);
                    res.status = 500;
                    nlohmann::json je{{"error", err}};
                    res.set_content(je.dump(), "application/json");
                    return;
                }
            }

            {
                std::error_code ec_rm;
                fs::remove(tmp_in, ec_rm);
            }

            std::string bytes;
            {
                std::ifstream ifs(tmp_out, std::ios::binary);
                if (!ifs) {
                    std::error_code ec_rm;
                    fs::remove(tmp_out, ec_rm);
                    res.status = 500;
                    res.set_content(R"({"error":"could not read resized output"})", "application/json");
                    return;
                }
                bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
            }
            {
                std::error_code ec_rm;
                fs::remove(tmp_out, ec_rm);
            }

            res.set_header("Content-Disposition", "inline; filename=\"resized" + out_ext + "\"");
            res.set_content(std::move(bytes), mime_for_format(opt.format));
            return;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }

        if (!body.contains("input") || !body.contains("output")) {
            res.status = 400;
            res.set_content(R"({"error":"input and output paths required"})", "application/json");
            return;
        }

        const std::string in = body["input"].get<std::string>();
        const std::string out = body["output"].get<std::string>();
        media::ResizeOptions opt;
        media::apply_resize_options_from_json(body, opt);
        media::apply_cache_defaults_from_json(body, opt, cache_defaults);

        bool expand_glob = true;
        if (body.contains("expand_glob") && body["expand_glob"].is_boolean())
            expand_glob = body["expand_glob"].get<bool>();

        const bool use_batch = expand_glob || media::has_dst_template(out);

        std::string err;
        if (!use_batch) {
            if (!media::resize_file(in, out, opt, err)) {
                res.status = 500;
                nlohmann::json j{{"error", err}};
                res.set_content(j.dump(), "application/json");
                return;
            }
            res.set_content(R"({"ok":true})", "application/json");
            return;
        }

        media::ResizeBatchResult stats;
        if (!media::resize_batch(in, out, opt, err, &stats)) {
            res.status = 500;
            nlohmann::json j{{"error", err}};
            res.set_content(j.dump(), "application/json");
            return;
        }
        if (stats.count > 1) {
            nlohmann::json ok{{"ok", true}, {"count", stats.count}, {"outputs", stats.outputs}};
            res.set_content(ok.dump(), "application/json");
        } else {
            res.set_content(R"({"ok":true})", "application/json");
        }
    });

    // ── /v1/compress  (multipart upload only, zero-fs) ─────────────────────
    // The request MUST be multipart/form-data with the image in a part named
    // file / image / upload, plus optional form fields (compressor, quality,
    // level, …).  The server processes everything in memory via
    // media::compress_buffer() and returns the compressed bytes inline.
    svr.Post("/v1/compress", [](const httplib::Request &req, httplib::Response &res) {
        if (!req.is_multipart_form_data()) {
            res.status = 415;
            res.set_content(R"json({"error":"multipart/form-data required (POST the file as 'file', 'image', or 'upload'); no JSON path mode"})json",
                            "application/json");
            return;
        }
        httplib::MultipartFormData upload;
        if (!pick_multipart_image(req, upload)) {
            res.status = 400;
            res.set_content(R"json({"error":"multipart image required (file/image/upload)"})json", "application/json");
            return;
        }
        nlohmann::json jopt = multipart_fields_to_json(req);
        media::CompressOptions opts;
        media::apply_compress_options_from_json(jopt, opts);

        media::CompressBufferResult cr = media::compress_buffer(
            upload.content.data(), upload.content.size(), opts);
        if (!cr.ok) {
            res.status = 500;
            nlohmann::json je{{"error", cr.error}};
            res.set_content(je.dump(), "application/json");
            return;
        }
        const std::string ext = (cr.mime == "image/png") ? ".png" : ".jpg";
        res.set_header("Content-Disposition", "inline; filename=\"compressed" + ext + "\"");
        res.set_content(std::move(cr.bytes), cr.mime);
    });

    // ── /v1/transform  (multipart upload only, zero-fs) ────────────────────
    // - Target image: part named file / image / upload.
    // - Reference images (logo / brand sheet / style swatch): any part whose
    //   name starts with "reference" or "ref" (e.g. "reference1",
    //   "reference_logo", "ref0") and carries a filename.
    // - Other form fields supply the same options as the JSON keys.
    // Everything stays in memory; the response body is the edited PNG bytes.
    svr.Post("/v1/transform", [](const httplib::Request &req, httplib::Response &res) {
        if (!req.is_multipart_form_data()) {
            res.status = 415;
            res.set_content(R"json({"error":"multipart/form-data required (POST the target image as 'file'; reference images as parts whose name starts with 'reference' or 'ref'); no JSON path mode"})json",
                            "application/json");
            return;
        }

        httplib::MultipartFormData upload;
        if (!pick_multipart_image(req, upload)) {
            res.status = 400;
            res.set_content(R"json({"error":"multipart image required (file/image/upload)"})json", "application/json");
            return;
        }
        nlohmann::json jopt = multipart_fields_to_json(req);
        media::TransformOptions opts;
        media::apply_transform_options_from_json(jopt, opts);

        // Reference images: collect in-memory buffers (zero-fs).
        std::vector<media::ReferenceBuffer> refs;
        for (const auto& kv : req.files) {
            const auto& name = kv.first;
            const auto& part = kv.second;
            if (part.filename.empty() || part.content.empty()) continue;
            const bool looks_like_ref = (name.rfind("reference", 0) == 0)
                                     || (name.rfind("ref", 0) == 0 && name != "ref");
            if (!looks_like_ref) continue;
            const std::string ext = extension_from_filename(part.filename);
            media::ReferenceBuffer rb;
            rb.mime  = mime_for_format(ext.empty() ? "png" : ext.substr(1));
            rb.bytes = part.content;
            refs.push_back(std::move(rb));
        }

        if (opts.api_key.empty())
            media::fill_image_provider_credentials_from_app(opts.provider, false, opts.api_key, opts.base_url);
        if (opts.provider != "replicate") {
            media::fill_image_provider_base_url_from_app(opts.base_url);
        }

        const std::string in_ext  = extension_from_filename(upload.filename);
        const std::string in_mime = mime_for_format(in_ext.empty() ? "png" : in_ext.substr(1));
        media::TransformBufferResult tr = media::transform_buffer(
            upload.content.data(), upload.content.size(), in_mime, opts, refs);

        if (!tr.ok) {
            res.status = 500;
            nlohmann::json je{{"error", tr.error}};
            res.set_content(je.dump(), "application/json");
            return;
        }
        res.set_header("Content-Disposition", "inline; filename=\"transformed.png\"");
        if (!tr.ai_text.empty()) res.set_header("X-Media-AI-Text", tr.ai_text);
        res.set_content(std::move(tr.bytes), tr.mime);
    });

    // ── /v1/meta  (multipart upload only, zero-fs; JSON inline) ────────────
    svr.Post("/v1/meta", [](const httplib::Request &req, httplib::Response &res) {
        if (!req.is_multipart_form_data()) {
            res.status = 415;
            res.set_content(R"json({"error":"multipart/form-data required (POST the image as 'file', 'image', or 'upload'); no JSON path mode"})json",
                            "application/json");
            return;
        }
        httplib::MultipartFormData upload;
        if (!pick_multipart_image(req, upload)) {
            res.status = 400;
            res.set_content(R"json({"error":"multipart image required (file/image/upload)"})json", "application/json");
            return;
        }
        nlohmann::json jopt = multipart_fields_to_json(req);
        media::MetaOptions opts;
        media::apply_meta_options_from_json(jopt, opts);

        if (opts.api_key.empty() && !opts.dry_run)
            media::fill_image_provider_credentials_from_app(opts.provider, opts.dry_run, opts.api_key, opts.base_url);
        media::fill_image_provider_base_url_from_app(opts.base_url);
        const std::string in_ext  = extension_from_filename(upload.filename);
        const std::string in_mime = mime_for_format(in_ext.empty() ? "jpg" : in_ext.substr(1));

        media::MetaResult mr = media::meta_extract_buffer(
            upload.content.data(), upload.content.size(), in_mime, opts);
        if (!mr.ok) {
            res.status = 500;
            nlohmann::json je{{"error", mr.error}};
            res.set_content(je.dump(), "application/json");
            return;
        }
        res.set_content(mr.json_text, "application/json");
    });

    // ── /v1/llm/tools/list  (JSON GET; returns the catalog verbatim) ───────
    svr.Get("/v1/llm/tools/list", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(media::llm::tool_catalog_json().dump(), "application/json");
    });

    // ── /v1/llm/tools/call  (JSON POST; executor delegates to buffer workers)
    // Body: { "name": "image_compress", "arguments": { ... } }
    // Returns the standard envelope (see docs/llm-tools.md §4).
    svr.Post("/v1/llm/tools/call", [](const httplib::Request &req, httplib::Response &res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"invalid JSON body"})json", "application/json");
            return;
        }
        if (!body.contains("name") || !body["name"].is_string()) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"'name' (string) is required"})json", "application/json");
            return;
        }
        const std::string name = body["name"].get<std::string>();
        const nlohmann::json args = body.contains("arguments") && body["arguments"].is_object()
                                        ? body["arguments"] : nlohmann::json::object();
        auto out = media::llm::execute(name, args);
        if (!out.ok) res.status = 200; // tool errors are returned in the envelope, not as HTTP errors
        res.set_content(out.envelope.dump(), "application/json");
    });

    std::cerr << "media-img HTTP listening on http://" << host << ":" << port << "\n";
    if (!svr.listen(host.c_str(), port)) {
        std::cerr << "listen failed\n";
        return 1;
    }
    return 0;
}

} // namespace media::http
