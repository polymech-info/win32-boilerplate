#include "cache.hpp"

#include "url_fetch.hpp"

#include <picosha2.h>

#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace media {

namespace {

std::string sha256_hex(const std::string &s) {
    std::string hex;
    picosha2::hash256_hex_string(s.begin(), s.end(), hex);
    return hex;
}

std::string serialize_options_for_cache_key(const ResizeOptions &opt) {
    std::ostringstream o;
    o << opt.max_width << '\n'
      << opt.max_height << '\n'
      << opt.format << '\n'
      << opt.fit << '\n'
      << opt.position << '\n'
      << opt.kernel << '\n'
      << opt.quality << '\n'
      << opt.png_compression << '\n'
      << (opt.without_enlargement ? 1 : 0) << '\n'
      << (opt.autorotate ? 1 : 0) << '\n'
      << (opt.strip_metadata ? 1 : 0) << '\n'
      << opt.rotate << '\n'
      << (opt.flip ? 1 : 0) << '\n'
      << (opt.flop ? 1 : 0) << '\n'
      << opt.background << '\n'
      << opt.url_timeout_sec << '\n'
      << opt.url_max_redirects << '\n';
    return o.str();
}

std::string make_cache_key_url(const std::string &url, const ResizeOptions &opt) {
    std::string blob = url;
    blob.push_back('\n');
    blob += "url\n";
    blob += serialize_options_for_cache_key(opt);
    return blob;
}

std::string make_cache_key_string(const fs::path &input_canon, std::uintmax_t size,
                                  std::uint64_t mtime_count, const ResizeOptions &opt) {
    std::string blob = input_canon.generic_string();
    blob.push_back('\n');
    blob += std::to_string(size);
    blob.push_back('\n');
    blob += std::to_string(mtime_count);
    blob.push_back('\n');
    blob += serialize_options_for_cache_key(opt);
    return blob;
}

fs::path cache_file_path(const fs::path &cache_root, const std::string &key_hex) {
    if (key_hex.size() < 4)
        return cache_root / key_hex;
    return cache_root / key_hex.substr(0, 2) / key_hex;
}

bool copy_file_overwrite(const fs::path &from, const fs::path &to, std::string &err_out) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err_out = ec.message();
        return false;
    }
    return true;
}

} // namespace

fs::path default_cache_dir() {
    return fs::absolute(fs::path("cache") / "images").lexically_normal();
}

fs::path effective_cache_dir(const ResizeOptions &opt) {
    if (opt.cache_dir.empty())
        return default_cache_dir();
    return fs::absolute(fs::path(opt.cache_dir)).lexically_normal();
}

bool try_copy_from_cache(const std::string &input_path, const std::string &output_path, const ResizeOptions &opt,
                         std::string &err_out) {
    err_out.clear();
    if (!opt.cache_enabled)
        return false;

    std::string key_hex;
    if (is_http_url(input_path)) {
        key_hex = sha256_hex(make_cache_key_url(input_path, opt));
    } else {
        std::error_code ec;
        fs::path in = fs::absolute(fs::path(input_path), ec);
        if (ec || !fs::is_regular_file(in, ec)) {
            return false;
        }
        fs::path canon = fs::weakly_canonical(in, ec);
        if (ec)
            canon = in;

        const std::uintmax_t sz = fs::file_size(canon, ec);
        if (ec)
            return false;

        const auto ft = fs::last_write_time(canon, ec);
        if (ec)
            return false;
        const std::uint64_t mtime_count = static_cast<std::uint64_t>(ft.time_since_epoch().count());

        key_hex = sha256_hex(make_cache_key_string(canon, sz, mtime_count, opt));
    }
    std::error_code ec;
    const fs::path root = effective_cache_dir(opt);
    const fs::path cached = cache_file_path(root, key_hex);

    if (!fs::is_regular_file(cached, ec) || ec)
        return false;

    return copy_file_overwrite(cached, fs::path(output_path), err_out);
}

void store_in_cache(const std::string &input_path, const std::string &output_path, const ResizeOptions &opt) {
    if (!opt.cache_enabled)
        return;

    std::string key_hex;
    if (is_http_url(input_path)) {
        key_hex = sha256_hex(make_cache_key_url(input_path, opt));
    } else {
        std::error_code ec;
        fs::path in = fs::absolute(fs::path(input_path), ec);
        if (ec || !fs::is_regular_file(in, ec))
            return;
        fs::path canon = fs::weakly_canonical(in, ec);
        if (ec)
            canon = in;

        const std::uintmax_t sz = fs::file_size(canon, ec);
        if (ec)
            return;

        const auto ft = fs::last_write_time(canon, ec);
        if (ec)
            return;
        const std::uint64_t mtime_count = static_cast<std::uint64_t>(ft.time_since_epoch().count());

        key_hex = sha256_hex(make_cache_key_string(canon, sz, mtime_count, opt));
    }
    std::error_code ec;
    const fs::path root = effective_cache_dir(opt);
    const fs::path dest = cache_file_path(root, key_hex);
    const fs::path tmp = dest.string() + ".tmp";

    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(fs::path(output_path), tmp, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return;
    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove(tmp, ec);
    }
}

} // namespace media
