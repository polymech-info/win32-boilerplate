#include "lib/pm_service_posts.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

static std::string normalize_post_visibility(std::string v)
{
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
        v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n'))
        v.pop_back();
    for (char& c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "private")
        return "private";
    if (v == "listed" || v == "unlisted" || v == "link")
        return "listed";
    return "public";
}

namespace {

size_t curl_write_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

void emit_log(const PmServiceUploadLogLine& log_fn, std::string_view s)
{
    if (log_fn)
        log_fn(s);
}

void append_http_trace(std::vector<PmServiceHttpRawStep>& steps, std::string label, long http, const std::string& body)
{
    steps.push_back({std::move(label), http, body});
}

bool http_post_json(const std::string& base_no_slash, const std::string& path, const std::string& bearer,
                    const std::string& json_body, long& http_out, std::string& body_out, std::string& err_out,
                    const PmServiceUploadLogLine& log, std::vector<PmServiceHttpRawStep>* trace_steps,
                    const std::string& trace_label)
{
    http_out = 0;
    body_out.clear();
    err_out.clear();
    const std::string url = base_no_slash + path;
    emit_log(log, "POST " + url);
    emit_log(log, "  JSON body length: " + std::to_string(json_body.size()));

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "curl_easy_init failed";
        return false;
    }
    struct curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + bearer;
    hdr                      = curl_slist_append(hdr, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_out);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("curl_easy_perform: ") + curl_easy_strerror(cc);
        if (trace_steps && !trace_label.empty())
            append_http_trace(*trace_steps, trace_label, http_out, body_out);
        return false;
    }
    emit_log(log, "  HTTP status: " + std::to_string(http_out));
    emit_log(log, "  response length: " + std::to_string(body_out.size()));
    if (body_out.size() <= 600)
        emit_log(log, std::string("  response: ") + body_out);
    if (trace_steps && !trace_label.empty())
        append_http_trace(*trace_steps, trace_label, http_out, body_out);
    return true;
}

} // namespace

PmServiceCreatePostPicturesResult pm_service_create_post_with_pictures(
    const std::string& base_url, const std::string& bearer_token, const std::vector<std::filesystem::path>& image_files,
    const std::string& title_override, const std::string& description, const std::string& settings_visibility,
    const PmServiceUploadLogLine& log_line, const PmServicePostProgressLine& progress)
{
    PmServiceCreatePostPicturesResult out;
    if (image_files.empty()) {
        out.err = "no image files";
        return out;
    }

    std::string post_title = title_override;
    if (post_title.empty())
        post_title = image_files[0].filename().string();

    nlohmann::json create_body;
    create_body["title"]         = post_title;
    create_body["description"] = description.empty() ? nullptr : nlohmann::json(description);
    nlohmann::json settings_json;
    settings_json["visibility"] = normalize_post_visibility(settings_visibility);
    create_body["settings"]     = settings_json;
    const std::string create_json = create_body.dump();

    if (progress)
        progress("Creating post…");

    long        http = 0;
    std::string body;
    std::string err;
    if (!http_post_json(base_url, "/api/posts", bearer_token, create_json, http, body, err, log_line, &out.http_raw_steps,
                        std::string("POST /api/posts"))) {
        out.err = err;
        return out;
    }
    if (http < 200 || http >= 300) {
        out.err = "create post HTTP " + std::to_string(http) + " body=" + body.substr(0, 400);
        return out;
    }

    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        if (!j.value("success", false) || !j.contains("post") || !j["post"].is_object()) {
            out.err = "create post: unexpected JSON (expected success + post)";
            return out;
        }
        const auto& post = j["post"];
        if (!post.contains("id") || !post["id"].is_string()) {
            out.err = "create post: post.id missing";
            return out;
        }
        out.post_id = post["id"].get<std::string>();
    } catch (const std::exception& e) {
        out.err = std::string("create post JSON: ") + e.what();
        return out;
    }

    emit_log(log_line, "created post id=" + out.post_id);

    const size_t n_files = image_files.size();
    for (size_t i = 0; i < n_files; ++i) {
        const auto& path = image_files[i];
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            out.err = "not a file: " + path.string();
            return out;
        }
        if (progress) {
            std::string line = "Uploading ";
            line += std::to_string(i + 1);
            line += "/";
            line += std::to_string(n_files);
            line += ": ";
            line += path.filename().string();
            progress(line);
        }
        long        uh = 0;
        std::string upload_body;
        std::string uerr;
        if (!pm_service_post_image_multipart(base_url, bearer_token, path, uh, upload_body, uerr, log_line)) {
            out.err = uerr;
            return out;
        }
        append_http_trace(out.http_raw_steps,
                          std::string("POST /api/images?forward=vfs&original=true file=") + path.filename().string(),
                          uh, upload_body);
        if (uh < 200 || uh >= 300) {
            out.err = "upload HTTP " + std::to_string(uh) + " for " + path.string();
            return out;
        }

        nlohmann::json uj;
        try {
            uj = nlohmann::json::parse(upload_body);
        } catch (const std::exception& e) {
            out.err = std::string("upload JSON: ") + e.what();
            return out;
        }
        const std::string image_url = uj.value("url", std::string());
        if (image_url.empty()) {
            out.err = "upload response missing url for " + path.string();
            return out;
        }

        const std::string pic_title = path.filename().string();
        nlohmann::json    pic_body;
        pic_body["post_id"]   = out.post_id;
        pic_body["title"]     = pic_title;
        pic_body["description"] = nullptr;
        pic_body["image_url"] = image_url;
        pic_body["position"]  = static_cast<int>(i);
        pic_body["type"]      = "supabase-image";
        if (uj.contains("meta") && !uj["meta"].is_null())
            pic_body["meta"] = uj["meta"];
        else
            pic_body["meta"] = nlohmann::json::object();

        if (progress) {
            std::string line = "Registering picture ";
            line += std::to_string(i + 1);
            line += "/";
            line += std::to_string(n_files);
            line += "…";
            progress(line);
        }

        long        ph = 0;
        std::string pic_resp;
        std::string perr;
        if (!http_post_json(base_url, "/api/pictures", bearer_token, pic_body.dump(), ph, pic_resp, perr, log_line,
                            &out.http_raw_steps, std::string("POST /api/pictures position=") + std::to_string(i))) {
            out.err = perr;
            return out;
        }
        if (ph < 200 || ph >= 300) {
            out.err = "create picture HTTP " + std::to_string(ph) + " body=" + pic_resp.substr(0, 400);
            return out;
        }
        try {
            const nlohmann::json pj = nlohmann::json::parse(pic_resp);
            if (!pj.contains("id") || !pj["id"].is_string()) {
                out.err = "create picture: missing id in response";
                return out;
            }
            out.picture_ids.push_back(pj["id"].get<std::string>());
        } catch (const std::exception& e) {
            out.err = std::string("picture response JSON: ") + e.what();
            return out;
        }
    }

    out.ok = true;
    return out;
}
