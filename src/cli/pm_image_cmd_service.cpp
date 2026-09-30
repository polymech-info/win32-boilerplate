#include "pm_image_cmd_service.hpp"

#include "lib/pm_service_upload.hpp"
#include "lib/pm_zitadel_oauth.hpp"
#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

#include "constants.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <string>
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY) && defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
#  include "win/explorer_job_ui.hpp"
#  include "win/ui_next/PixlwizSharePostDlg.h"
#  include "file_extensions.hpp"
#  include "win/ui_next/helpers/text_conv.hpp"
#endif

namespace {

constexpr const char* k_log_pfx = "[service-upload] ";

void svlog(const std::string& line)
{
    std::cerr << k_log_pfx << line << '\n';
}

} // namespace

int pm_image_cmd_service(CLI::App& /*app*/, PmImageCliState& st)
{
    if (!st.service_upload_cmd || !st.service_upload_cmd->parsed())
        return 1;

    const std::string base = pm_resolve_service_server_base(st.service_server_url);
    if (base.empty()) {
        std::cerr << k_log_pfx
                  << "Set SERVER_URL (e.g. https://pixlwiz.com) or VITE_SERVER_IMAGE_API_URL, or pass --server-url.\n";
        return 1;
    }
    svlog("server base URL: " + base);

    std::string bearer;
    std::string aerr;
    if (!pm_zitadel_oauth_read_access_token(bearer, aerr)) {
        std::cerr << k_log_pfx << aerr << "\n";
        return 1;
    }
    svlog("loaded Bearer token from zitadel-oauth.json, length=" + std::to_string(bearer.size()));

    if (st.service_upload_files.empty()) {
        std::cerr << k_log_pfx << "Provide at least one file path (positional).\n";
        return 1;
    }

    const PmServiceUploadLogLine log = [](std::string_view line) { svlog(std::string(line)); };

    int exit_code = 0;
    for (const std::string& rel : st.service_upload_files) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path abs = fs::absolute(fs::path(rel), ec);
        if (ec || rel.empty()) {
            std::cerr << k_log_pfx << "bad path: " << rel << "\n";
            exit_code = 1;
            continue;
        }
        std::string body;
        std::string err;
        long        http = 0;
        if (!pm_service_post_image_multipart(base, bearer, abs, http, body, err, log)) {
            std::cerr << k_log_pfx << err << "\n";
            exit_code = 1;
            continue;
        }
        if (http == 413) {
            std::cerr << k_log_pfx << "HTTP 413 payload too large for " << abs.string() << "\n";
            exit_code = 1;
            continue;
        }
        if (http < 200 || http >= 300) {
            std::cerr << k_log_pfx << "upload failed HTTP " << http << " for " << abs.string() << "\n";
            exit_code = 1;
            continue;
        }
        try {
            const nlohmann::json j = nlohmann::json::parse(body);
            const std::string  u = j.value("url", std::string());
            if (u.empty()) {
                std::cerr << k_log_pfx << "response JSON missing url for " << abs.string() << "\n";
                exit_code = 1;
                continue;
            }
            svlog("OK url field length=" + std::to_string(u.size()));
            nlohmann::json line = {{"path", abs.string()}, {"url", u}};
            if (j.contains("meta"))
                line["meta"] = j["meta"];
            if (st.service_dump_raw_http) {
                line["http_status"] = http;
                line["raw_body"]     = body;
            }
            std::cout << line.dump() << "\n";
        } catch (const std::exception& e) {
            std::cerr << k_log_pfx << "JSON parse error: " << e.what() << " body=" << body.substr(0, 200) << "\n";
            exit_code = 1;
        }
    }
    return exit_code;
}

#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE

#  include "lib/pm_service_posts.hpp"

namespace {

constexpr const char* k_posts_log_pfx = "[service-posts] ";

void posts_log(const std::string& line)
{
    std::cerr << k_posts_log_pfx << line << '\n';
}

#  if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
static std::wstring path_ext_lower_fs(const std::filesystem::path& p)
{
    std::wstring e = p.extension().wstring();
    for (wchar_t& c : e)
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    return e;
}

/** Image files plus one directory level (same rule as in-app Pixlwiz share from Explorer). */
static void collect_share_cli_inputs(const std::vector<std::string>& utf8_in, std::vector<std::filesystem::path>* out)
{
    namespace fs = std::filesystem;
    out->clear();
    for (const std::string& u8 : utf8_in) {
        std::error_code ec;
        const fs::path  p = fs::u8path(u8);
        if (fs::is_regular_file(p, ec)) {
            if (pmui::is_image_ext(path_ext_lower_fs(p))) {
                const fs::path abs = fs::absolute(p, ec);
                if (!ec)
                    out->push_back(abs);
            }
            continue;
        }
        if (!fs::is_directory(p, ec))
            continue;
        const fs::directory_iterator end_it;
        for (fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec); it != end_it;
             it.increment(ec)) {
            if (ec)
                break;
            const fs::path f = it->path();
            if (!it->is_regular_file())
                continue;
            if (!pmui::is_image_ext(path_ext_lower_fs(f)))
                continue;
            std::error_code e2;
            const fs::path abs2 = fs::absolute(f, e2);
            if (!e2)
                out->push_back(abs2);
        }
    }
    std::sort(out->begin(), out->end());
    out->erase(std::unique(out->begin(), out->end()), out->end());
}

static std::string build_post_compact_view_url_utf8_job(
    const std::string& base_no_slash, const std::string& post_id, const std::vector<std::string>& picture_ids)
{
    std::string u = base_no_slash + "/post/" + post_id + "?view=compact";
    if (!picture_ids.empty() && !picture_ids[0].empty())
        u += "&pic=" + picture_ids[0];
    return u;
}
#  endif

} // namespace

int pm_image_cmd_service_posts_create(CLI::App& /*app*/, PmImageCliState& st)
{
    if (!st.service_posts_create_cmd || !st.service_posts_create_cmd->parsed())
        return 1;

    std::string base = pm_resolve_service_server_base(st.service_server_url);
    if (base.empty() && pm::k_pixlwiz_service_server_base_default_u8[0] != '\0')
        base = pm_trim_trailing_slash(std::string(pm::k_pixlwiz_service_server_base_default_u8));
    if (base.empty()) {
        std::cerr << k_posts_log_pfx
                  << "Set SERVER_URL (e.g. https://pixlwiz.com) or VITE_SERVER_IMAGE_API_URL, or pass --server-url.\n";
        return 1;
    }
    posts_log("server base URL: " + base);

    std::string bearer;
    std::string aerr;
    if (!pm_zitadel_oauth_read_access_token(bearer, aerr)) {
        std::cerr << k_posts_log_pfx << aerr << "\n";
        return 1;
    }
    posts_log("loaded Bearer token from zitadel-oauth.json, length=" + std::to_string(bearer.size()));

    if (st.service_posts_create_files.empty()) {
        std::cerr << k_posts_log_pfx << "Provide at least one image path (positional).\n";
        return 1;
    }

    namespace fs = std::filesystem;
    std::vector<fs::path> abs_files;

#  if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    if (!st.g_no_gui && (st.service_posts_create_job_ui || st.service_posts_create_files.size() > 1u)) {
        collect_share_cli_inputs(st.service_posts_create_files, &abs_files);
        if (abs_files.empty()) {
            std::cerr << k_posts_log_pfx << "No image files found in the given path(s).\n";
            return 1;
        }

        media::win::ExplorerJobRow row{};
        row.name = L"Pixlwiz post";
        row.path = abs_files[0].wstring();
        if (abs_files.size() > 1u) {
            row.path += L" \u2014 ";
            row.path += std::to_wstring(abs_files.size());
            row.path += L" images";
        }

        const bool dump_raw = st.service_dump_raw_http;
        const std::string cli_title = st.service_post_title;
        const std::string cli_desc = st.service_post_description;
        const std::string cli_vis = st.service_post_visibility;

        const bool ui_ok = media::win::run_explorer_job_ui(
            std::wstring(pm::brand::k_ui_job_title_pixlwiz_share_w), {row},
            [&](media::win::ExplorerJobHost& h) {
                h.wait_if_paused();
                if (h.cancel_requested())
                    return;

                HWND jw = h.job_window();
                if (!jw)
                    return;

                PixlwizSharePostFields fields{};
                const bool dlg_ok = media::win::explorer_job_sync_pixlwiz_share_post_dialog(jw, fields);
                if (!dlg_ok) {
                    h.set_status(0, L"Cancelled");
                    return;
                }

                media::win::explorer_job_ui_reveal_job_window(jw);

                const std::string title_u8 = !fields.title.empty() ? pmui::wide_to_utf8(fields.title) : cli_title;
                const std::string desc_u8
                    = !fields.description.empty() ? pmui::wide_to_utf8(fields.description) : cli_desc;
                const std::string vis_u8 = fields.visibility.empty() ? cli_vis : fields.visibility;

                h.set_status(0, L"Uploading\u2026");
                const PmServiceUploadLogLine log = [&](std::string_view line) {
                    std::wstring w = pmui::utf8_to_wide(std::string(line));
                    if (w.size() > 180)
                        w.resize(180);
                    h.set_status(0, w.c_str());
                };
                const PmServicePostProgressLine prog = [&](std::string_view line) {
                    std::wstring w = pmui::utf8_to_wide(std::string(line));
                    if (w.size() > 180)
                        w.resize(180);
                    h.set_status(0, w.c_str());
                };

                const PmServiceCreatePostPicturesResult r
                    = pm_service_create_post_with_pictures(base, bearer, abs_files, title_u8, desc_u8, vis_u8, log, prog);
                if (!r.ok) {
                    std::wstring w = L"Error: " + pmui::utf8_to_wide(r.err);
                    if (w.size() > 200)
                        w.resize(200);
                    h.set_status(0, w.c_str());
                    posts_log(r.err);
                    return;
                }

                posts_log("post_id=" + r.post_id + " picture_count=" + std::to_string(r.picture_ids.size()));
                const std::string url_u8 = build_post_compact_view_url_utf8_job(base, r.post_id, r.picture_ids);
                if (dump_raw) {
                    nlohmann::json out = {{"post_id", r.post_id}, {"picture_ids", r.picture_ids}};
                    nlohmann::json steps = nlohmann::json::array();
                    for (const auto& s : r.http_raw_steps) {
                        steps.push_back(
                            {{"label", s.label}, {"http_status", s.http_status}, {"body_raw", s.body_raw}});
                    }
                    out["http_raw_steps"] = std::move(steps);
                    std::cout << out.dump() << "\n";
                }
                else {
                    std::cout << nlohmann::json{{"post_id", r.post_id}, {"picture_ids", r.picture_ids}}.dump() << "\n";
                }

                h.set_status(0, L"\u2713 Shared");
                media::win::explorer_job_sync_pixlwiz_share_success_dialog(
                    jw, pmui::utf8_to_wide(url_u8), r.picture_ids.size());
            },
            true);
        if (!ui_ok)
            return 1;
        return 0;
    }
#  endif

    for (const std::string& rel : st.service_posts_create_files) {
        std::error_code ec;
        const fs::path abs = fs::absolute(fs::path(rel), ec);
        if (ec || rel.empty()) {
            std::cerr << k_posts_log_pfx << "bad path: " << rel << "\n";
            return 1;
        }
        abs_files.push_back(abs);
    }

    const PmServiceUploadLogLine log = [](std::string_view line) { posts_log(std::string(line)); };

    const PmServiceCreatePostPicturesResult r = pm_service_create_post_with_pictures(
        base, bearer, abs_files, st.service_post_title, st.service_post_description, st.service_post_visibility, log);

    if (!r.ok) {
        std::cerr << k_posts_log_pfx << r.err << "\n";
        return 1;
    }

    posts_log("post_id=" + r.post_id + " picture_count=" + std::to_string(r.picture_ids.size()));
    nlohmann::json out = {{"post_id", r.post_id}, {"picture_ids", r.picture_ids}};
    if (st.service_dump_raw_http) {
        nlohmann::json steps = nlohmann::json::array();
        for (const auto& s : r.http_raw_steps) {
            steps.push_back(
                {{"label", s.label}, {"http_status", s.http_status}, {"body_raw", s.body_raw}});
        }
        out["http_raw_steps"] = std::move(steps);
    }
    std::cout << out.dump() << "\n";
    return 0;
}

#else

int pm_image_cmd_service_posts_create(CLI::App&, PmImageCliState&)
{
    std::cerr << "[service-posts] Pixlwiz share is disabled in this build (FEATURE_PIXLWIZ_SHARE=OFF).\n";
    return 1;
}

#endif

void pm_image_register_service(CLI::App& app, PmImageCliState& s) {
    s.service_cmd = app.add_subcommand(
        "service",
        "Call the configured web service API (see pm-pics src/lib/db.ts + uploadUtils). "
        "Uses SERVER_URL or VITE_SERVER_IMAGE_API_URL and access_token from zitadel-oauth.json (run `login` first).");
    s.service_cmd->require_subcommand(1);
    s.service_upload_cmd = s.service_cmd->add_subcommand(
        "upload",
        "POST /api/images?forward=vfs&original=true — multipart field \"file\" (same as uploadUtils.uploadImage).");
    s.service_upload_cmd
        ->add_option("files", s.service_upload_files, "Local image path(s); repeat or list several")->required(true)
        ->expected(-1);
    s.service_upload_cmd->add_option(
        "--server-url", s.service_server_url,
        "Service base URL (default: env SERVER_URL, else VITE_SERVER_IMAGE_API_URL, else CLIENT_URL). No trailing slash.");
    s.service_upload_cmd->add_flag(
        "--dump-raw-http", s.service_dump_raw_http,
        "Each stdout JSON line also includes http_status and raw_body (exact /api/images response string).");

#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    s.service_posts_cmd = s.service_cmd->add_subcommand(
        "posts",
        "Post APIs (pm-pics /api/posts + /api/pictures): create a post and attach uploaded images.");
    s.service_posts_cmd->require_subcommand(1);
    s.service_posts_create_cmd = s.service_posts_cmd->add_subcommand(
        "create",
        "POST /api/posts then multipart /api/images per file, then POST /api/pictures (same as web publish flow). "
        "Default post title is the first file's filename; each picture title is that file's filename. Description optional.");
    s.service_posts_create_cmd
        ->add_option("files", s.service_posts_create_files, "Local image path(s)")->required(true)->expected(-1);
    s.service_posts_create_cmd->add_option(
        "--title", s.service_post_title,
        "Post title (default: filename of the first image, e.g. photo.png).");
    s.service_posts_create_cmd->add_option("--description", s.service_post_description, "Optional post description.");
    s.service_posts_create_cmd
        ->add_option("--visibility", s.service_post_visibility,
                     "Post visibility: public | listed | private (JSON settings.visibility; default public).")
        ->check(CLI::IsMember({"public", "listed", "private"}))
        ->default_val("public");
    s.service_posts_create_cmd->add_option(
        "--server-url", s.service_server_url,
        "Service base URL (default: env SERVER_URL, else VITE_SERVER_IMAGE_API_URL, else CLIENT_URL). No trailing slash.");
    s.service_posts_create_cmd->add_flag(
        "--dump-raw-http", s.service_dump_raw_http,
        "Stdout JSON also includes http_raw_steps: label, http_status, body_raw for each API call.");
#  if defined(_WIN32)
    s.service_posts_create_cmd->add_flag(
        "--job-ui", s.service_posts_create_job_ui,
        "Windows: list-style job window + Pixlwiz post dialog (Explorer `Share to Pixlwiz…` verb).");
#  endif
#endif
}
