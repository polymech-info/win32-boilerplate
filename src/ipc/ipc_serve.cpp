#include "ipc_serve.hpp"
#include "constants.hpp"

#include <cstdio>

#include <asio.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include <iostream>
#include <nlohmann/json.hpp>

#include "core/compress.hpp"
#include "core/glob_paths.hpp"
#include "core/meta.hpp"
#include "core/resize.hpp"
#include "core/app_image_provider.hpp"
#include "core/transform.hpp"
#include "llm/tool_catalog.hpp"
#include "llm/tool_executor.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace media::ipc {

namespace {

// ── temp + base64 helpers ───────────────────────────────────────────────────
fs::path unique_temp_path(const std::string &suffix) {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) return {};
    static std::mutex mtx;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mtx);
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < 64; ++i) {
        fs::path p = base / (std::string(pm::brand::k_app_id_u8) + "-ipc-" + std::to_string(dist(rng)) + suffix);
        if (!fs::exists(p, ec)) return p;
    }
    return base
        / (std::string(pm::brand::k_app_id_u8) + "-ipc-" + std::to_string(dist(rng)) + suffix);
}

std::string read_all_bytes(const fs::path &p) {
    std::ifstream ifs(p, std::ios::binary);
    std::string out;
    out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return out;
}

std::string base64_encode(const unsigned char *data, std::size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? tbl[n & 0x3F] : '=');
    }
    return out;
}

nlohmann::json make_resize_response(const nlohmann::json &j, const CacheServerDefaults &cache_defaults) {
    if (!j.contains("input") || !j.contains("output"))
        return nlohmann::json{{"ok", false}, {"error", "need input and output"}};

    const std::string in = j["input"].get<std::string>();
    const std::string out = j["output"].get<std::string>();
    media::ResizeOptions opt;
    media::apply_resize_options_from_json(j, opt);
    media::apply_cache_defaults_from_json(j, opt, cache_defaults);

    bool expand_glob = true;
    if (j.contains("expand_glob") && j["expand_glob"].is_boolean())
        expand_glob = j["expand_glob"].get<bool>();

    const bool use_batch = expand_glob || media::has_dst_template(out);

    std::string err;
    if (!use_batch) {
        if (!media::resize_file(in, out, opt, err))
            return nlohmann::json{{"ok", false}, {"error", err}};
        return nlohmann::json{{"ok", true}};
    }

    media::ResizeBatchResult stats;
    if (!media::resize_batch(in, out, opt, err, &stats))
        return nlohmann::json{{"ok", false}, {"error", err}};
    if (stats.count > 1)
        return nlohmann::json{{"ok", true}, {"count", stats.count}, {"outputs", stats.outputs}};
    return nlohmann::json{{"ok", true}};
}

// IPC binary-result helpers always write the worker output to a temp file we
// own, slurp the bytes, base64-encode them into the response, then delete the
// temp file.  No host-visible files are ever produced.

nlohmann::json make_compress_response(const nlohmann::json &j) {
    if (!j.contains("input") || !j["input"].is_string())
        return nlohmann::json{{"ok", false}, {"error", "compress: input required"}};

    media::CompressOptions opts;
    media::apply_compress_options_from_json(j, opts);

    const std::string in = j["input"].get<std::string>();
    std::string in_ext   = fs::path(in).extension().string();
    std::string out_ext;
    std::string mime;
    if (opts.compressor == media::Compressor::PNG)          { out_ext = ".png"; mime = "image/png"; }
    else if (opts.compressor == media::Compressor::MozJPEG) { out_ext = ".jpg"; mime = "image/jpeg"; }
    else { out_ext = (in_ext == ".png") ? ".png" : ".jpg"; mime = (out_ext == ".png") ? "image/png" : "image/jpeg"; }

    const fs::path tmp_out = unique_temp_path(out_ext);
    std::string err = media::compress_file(in, tmp_out.string(), opts);
    if (!err.empty()) {
        std::error_code ec; fs::remove(tmp_out, ec);
        return nlohmann::json{{"ok", false}, {"error", err}};
    }
    std::string bytes = read_all_bytes(tmp_out);
    { std::error_code ec; fs::remove(tmp_out, ec); }
    return nlohmann::json{
        {"ok",    true},
        {"mime",  mime},
        {"bytes", bytes.size()},
        {"b64",   base64_encode(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size())},
    };
}

nlohmann::json make_transform_response(const nlohmann::json &j) {
    if (!j.contains("input") || !j["input"].is_string())
        return nlohmann::json{{"ok", false}, {"error", "transform: input required"}};

    media::TransformOptions opts;
    media::apply_transform_options_from_json(j, opts);
    if (opts.api_key.empty())
        media::fill_image_provider_credentials_from_app(opts.provider, false, opts.api_key, opts.base_url);
    if (opts.provider != "replicate") {
        media::fill_image_provider_base_url_from_app(opts.base_url);
    }

    const std::string in = j["input"].get<std::string>();
    const fs::path tmp_out = unique_temp_path(".png");
    auto tr = media::transform_image(in, tmp_out.string(), opts);
    if (!tr.ok) {
        std::error_code ec; fs::remove(tmp_out, ec);
        return nlohmann::json{{"ok", false}, {"error", tr.error}};
    }
    std::string bytes = read_all_bytes(tr.output_path);
    { std::error_code ec; fs::remove(tr.output_path, ec); }
    nlohmann::json ok{
        {"ok",    true},
        {"mime",  "image/png"},
        {"bytes", bytes.size()},
        {"b64",   base64_encode(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size())},
    };
    if (!tr.ai_text.empty()) ok["ai_text"] = tr.ai_text;
    return ok;
}

nlohmann::json make_meta_response(const nlohmann::json &j) {
    if (!j.contains("input") || !j["input"].is_string())
        return nlohmann::json{{"ok", false}, {"error", "meta: input required"}};

    media::MetaOptions opts;
    media::apply_meta_options_from_json(j, opts);
    if (opts.api_key.empty() && !opts.dry_run)
        media::fill_image_provider_credentials_from_app(opts.provider, opts.dry_run, opts.api_key, opts.base_url);
    media::fill_image_provider_base_url_from_app(opts.base_url);
    // Force "no disk outputs" — the JSON response is the deliverable.
    opts.out_md      = false;
    opts.out_json    = false;
    opts.update_exif = false;

    const std::string in = j["input"].get<std::string>();
    auto mr = media::meta_extract(in, opts);
    if (!mr.ok)
        return nlohmann::json{{"ok", false}, {"error", mr.error}};

    nlohmann::json out{{"ok", true}};
    nlohmann::json parsed = nlohmann::json::parse(mr.json_text, nullptr, false);
    if (parsed.is_discarded()) out["meta_text"] = mr.json_text;
    else                       out["meta"]      = std::move(parsed);
    return out;
}

/// Single dispatcher used by both TCP and Unix-socket sessions.
/// Selects the worker by the JSON `op` field; default = "resize".
nlohmann::json dispatch(const nlohmann::json &j, const CacheServerDefaults &cache_defaults) {
    std::string op = "resize";
    if (j.contains("op") && j["op"].is_string())
        op = j["op"].get<std::string>();
    if (op == "resize")    return make_resize_response(j, cache_defaults);
    if (op == "compress")  return make_compress_response(j);
    if (op == "transform") return make_transform_response(j);
    if (op == "meta")      return make_meta_response(j);
    if (op == "llm.tools/list") return media::llm::tool_catalog_json();
    if (op == "llm.tools/call") {
        if (!j.contains("name") || !j["name"].is_string())
            return nlohmann::json{{"ok", false}, {"error", "llm.tools/call: 'name' (string) required"}};
        const std::string name = j["name"].get<std::string>();
        const nlohmann::json args = j.contains("arguments") && j["arguments"].is_object()
                                        ? j["arguments"] : nlohmann::json::object();
        return media::llm::execute(name, args).envelope;
    }
    return nlohmann::json{{"ok", false}, {"error", "unknown op: " + op}};
}

static int handle_session(asio::ip::tcp::socket sock, const CacheServerDefaults &cache_defaults) {
    try {
        asio::streambuf buf;
        asio::read_until(sock, buf, '\n');
        std::istream is(&buf);
        std::string line;
        std::getline(is, line);
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (!j.is_object()) {
            std::string err = R"({"ok":false,"error":"invalid json"})";
            asio::write(sock, asio::buffer(err + "\n"));
            return 0;
        }
        nlohmann::json out = dispatch(j, cache_defaults);
        std::string payload = out.dump() + "\n";
        asio::write(sock, asio::buffer(payload));
    } catch (const std::exception &e) {
        try {
            std::string err = std::string(R"({"ok":false,"error":")") + e.what() + "\"}\n";
            asio::write(sock, asio::buffer(err));
        } catch (...) {
        }
    }
    return 0;
}

} // namespace

int run_tcp_server(const std::string &host, int port, const CacheServerDefaults &cache_defaults) {
    asio::io_context io;
    asio::ip::tcp::acceptor acc(io,
                                asio::ip::tcp::endpoint(asio::ip::make_address(host), static_cast<unsigned short>(port)));
    std::cerr << "media-img IPC (TCP) " << host << ":" << port << "\n";
    for (;;) {
        asio::ip::tcp::socket sock(io);
        acc.accept(sock);
        handle_session(std::move(sock), cache_defaults);
    }
}

#if !defined(_WIN32)
#include <asio/local/stream_protocol.hpp>
#include <unistd.h>

int run_unix_server(const std::string &path, const CacheServerDefaults &cache_defaults) {
    ::unlink(path.c_str());
    asio::io_context io;
    asio::local::stream_protocol::acceptor acc(io, asio::local::stream_protocol::endpoint(path));
    std::cerr << "media-img IPC (unix) " << path << "\n";
    for (;;) {
        asio::local::stream_protocol::socket sock(io);
        acc.accept(sock);
        try {
            asio::streambuf buf;
            asio::read_until(sock, buf, '\n');
            std::istream is(&buf);
            std::string line;
            std::getline(is, line);
            nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
            if (!j.is_object()) {
                asio::write(sock, asio::buffer(std::string(R"({"ok":false})") + "\n"));
                continue;
            }
            nlohmann::json out = dispatch(j, cache_defaults);
            asio::write(sock, asio::buffer(out.dump() + "\n"));
        } catch (const std::exception &e) {
            try {
                nlohmann::json err{{"ok", false}, {"error", e.what()}};
                asio::write(sock, asio::buffer(err.dump() + "\n"));
            } catch (...) {
            }
        }
    }
}
#endif

} // namespace media::ipc
