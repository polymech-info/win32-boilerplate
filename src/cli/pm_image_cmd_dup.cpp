#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_dup.hpp"

int pm_image_cmd_dup(CLI::App& app, PmImageCliState& st) {
    if (!st.dup_load_session.empty()) {
        if (!st.dup_inputs.empty()) {
            std::cerr << "duplicates: --load-session: omit input path arguments\n";
            return 1;
        }
        std::ifstream load_ifs(st.dup_load_session, std::ios::in | std::ios::binary);
        if (!load_ifs) {
            std::cerr << "duplicates: cannot open --load-session: " << st.dup_load_session << "\n";
            return 1;
        }
        std::ostringstream lbuf;
        lbuf << load_ifs.rdbuf();
        nlohmann::json j_sess;
        try {
            j_sess = nlohmann::json::parse(lbuf.str());
        } catch (const std::exception& e) {
            std::cerr << "duplicates: --load-session: JSON parse error: " << e.what() << "\n";
            return 1;
        }
        std::string ses_err;
        if (!media::validate_duplicates_session_json(j_sess, ses_err)) {
            std::cerr << "duplicates: " << ses_err << "\n";
            return 1;
        }
        int n_groups = 0;
        if (j_sess.contains("groups") && j_sess["groups"].is_array())
            n_groups = static_cast<int>(j_sess["groups"].size());
        int n_cand = 0;
        if (j_sess.contains("candidates") && j_sess["candidates"].is_array())
            n_cand = static_cast<int>(j_sess["candidates"].size());
        std::cerr << "â”€â”€ duplicates session â”€â”€ " << st.dup_load_session << "\n";
        if (j_sess.contains("generated_utc") && j_sess["generated_utc"].is_string())
            std::cerr << "  generated_utc: " << j_sess["generated_utc"].get<std::string>() << "\n";
        if (j_sess.contains("format_version")) std::cerr << "  format_version: " << j_sess["format_version"] << "\n";
        if (j_sess.contains("options") && j_sess["options"].is_object()) {
            const auto& o = j_sess["options"];
            if (o.contains("mode") && o["mode"].is_string())
                std::cerr << "  options.mode: " << o["mode"].get<std::string>() << "\n";
            if (o.contains("recursive"))
                std::cerr << "  options.recursive: " << (o["recursive"].get<bool>() ? "true" : "false")
                          << " (scan that produced this file; candidates[] is fixed)\n";
        }
        if (j_sess.contains("inputs") && j_sess["inputs"].is_array()) {
            const int ni = static_cast<int>(j_sess["inputs"].size());
            std::cerr << "  input_specs: " << ni << "\n";
        }
        std::cerr << "  groups: " << n_groups << "  candidates: " << n_cand << "\n";
        if (st.dup_print_json) std::cout << j_sess.dump(2) << "\n";
        return 0;
    }

    if (st.dup_inputs.empty()) {
        std::cerr << "duplicates: need at least one input path (or use --load-session)\n";
        return 1;
    }

    media::DuplicatesOptions dopts;
    if (st.dup_by == "size")
        dopts.mode = media::DuplicatesMode::Size;
    else if (st.dup_by == "fingerprint")
        dopts.mode = media::DuplicatesMode::Fingerprint;
    else
        dopts.mode = media::DuplicatesMode::Meta;
    dopts.recursive = !st.dup_no_recursive;
    dopts.min_group_size  = st.dup_min_group;
    dopts.max_hamming     = st.dup_max_hamming;
    dopts.fingerprint_same_size_only = st.dup_fp_same_size;
    dopts.use_md  = !st.dup_no_md;
    dopts.use_json= !st.dup_no_json;
    dopts.use_exif= !st.dup_no_exif;
    dopts.meta_prompt = st.dup_meta_prompt;
    dopts.detailed_report
        = !st.dup_report_md.empty() || !st.dup_report_json.empty() || !st.dup_save_session.empty();
    dopts.meta_json_llm_compare   = st.dup_meta_json_llm;
    dopts.meta_json_implicit_generate = st.dup_meta_json_implicit;
    dopts.meta_json_min_similarity = st.dup_meta_json_min_sim;
    dopts.meta_json_compare_prompt = st.dup_meta_json_cmp_prompt;
    dopts.llm_router    = st.dup_llm_router;
    dopts.llm_model     = st.dup_llm_model;
    dopts.llm_api_key   = st.dup_llm_api_key;
    dopts.llm_base_url  = st.dup_llm_base_url;
    dopts.llm_timeout_ms = st.dup_llm_timeout_ms;

    if (st.dup_meta_json_llm) {
        // Same resolution as `llm agent`: settings.json chat + providers (no environment).
        {
            std::string cs_err;
            media::runtime_settings::ChatProviderSettings cs;
            if (media::runtime_settings::load_chat_provider(cs, cs_err)) {
                if (dopts.llm_router.empty()) dopts.llm_router = cs.router;
                if (dopts.llm_model.empty()) dopts.llm_model = cs.model;
                if (dopts.llm_timeout_ms <= 0 && cs.timeout_ms > 0) dopts.llm_timeout_ms = cs.timeout_ms;
            }
        }
        if (dopts.llm_timeout_ms <= 0) dopts.llm_timeout_ms = 60'000;
        media_cli::fill_chat_llm_credentials_from_app_settings(
            dopts.llm_api_key, dopts.llm_base_url, dopts.llm_router);
    }

    std::cerr << "â”€â”€ duplicates â”€â”€ by: " << st.dup_by << " | inputs: " << st.dup_inputs.size() << "\n";
    auto dprogress = [](const std::string& s) { std::cerr << s << "\n"; };
    auto dr        = media::find_duplicates(st.dup_inputs, dopts, dprogress);
    if (!dr.ok) {
        std::cerr << "duplicates: " << dr.error << "\n";
        return 1;
    }

    if (!st.dup_report_json.empty()) {
        std::ofstream rf(st.dup_report_json, std::ios::out | std::ios::trunc);
        if (!rf) {
            std::cerr << "duplicates: cannot open --report-json: " << st.dup_report_json << "\n";
            return 1;
        }
        rf << dr.report.dump(2) << "\n";
    }
    if (!st.dup_save_session.empty()) {
        std::ofstream sf(st.dup_save_session, std::ios::out | std::ios::trunc);
        if (!sf) {
            std::cerr << "duplicates: cannot open --save-session: " << st.dup_save_session << "\n";
            return 1;
        }
        sf << dr.report.dump(2) << "\n";
    }
    if (!st.dup_report_md.empty()) {
        std::ofstream mf(st.dup_report_md, std::ios::out | std::ios::trunc);
        if (!mf) {
            std::cerr << "duplicates: cannot open --report-md: " << st.dup_report_md << "\n";
            return 1;
        }
        mf << media::format_duplicates_markdown(dr.report) << "\n";
    }

    if (st.dup_print_json) {
        nlohmann::json j;
        j["scanned"] = dr.scanned;
        j["skipped_fingerprint"] = dr.skipped_fingerprint;
        j["skipped_meta"] = dr.skipped_meta;
        j["meta_empty_files"] = dr.meta_empty_files;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& g : dr.groups) {
            nlohmann::json row;
            row["method"]  = g.method;
            row["key"]     = g.key;
            row["paths"]   = g.paths;
            row["count"]   = g.paths.size();
            arr.push_back(std::move(row));
        }
        j["n_groups"] = static_cast<int>(dr.groups.size());
        j["groups"]   = std::move(arr);
        if (dr.report.contains("duplicate_map") && dr.report["duplicate_map"].is_object())
            j["duplicate_map"] = dr.report["duplicate_map"];
        else
            j["duplicate_map"] = nlohmann::json::object();
        std::cout << j.dump(2) << "\n";
    } else {
        for (const auto& g : dr.groups) {
            std::cout << "[" << g.method << " " << g.key << "] " << g.paths.size() << " file(s)\n";
            for (const auto& pth : g.paths) std::cout << "  " << pth << "\n";
            std::cout << "\n";
        }
    }
    std::cerr << "scanned " << dr.scanned
              << " | groups " << dr.groups.size()
              << " | meta_empty " << dr.meta_empty_files
              << " | fp_skipped " << dr.skipped_fingerprint << "\n";
    return dr.groups.empty() ? 1 : 0;
}

void pm_image_register_dup(CLI::App& app, PmImageCliState& s) {
    s.dup_cmd = app.add_subcommand("duplicates",
        "Group duplicate images: by file size, perceptual dHash, or sidecar+EXIF hash (see docs/duplicates.md)");
    s.dup_cmd->add_option("input", s.dup_inputs,
                        "Input file(s), folder(s) or glob(s); repeatable (omit with --load-session)")->expected(-1);
    s.dup_cmd->add_option("--by", s.dup_by, "size | fingerprint | meta")
        ->check(CLI::IsMember({"size", "fingerprint", "meta"}))
        ->default_str("fingerprint");
    s.dup_cmd->add_flag("--no-recursive", s.dup_no_recursive, "Do not recurse into directory inputs");
    s.dup_cmd->add_option("--min-group", s.dup_min_group, "Only show groups with at least this many files (≥2)")->default_val(2)
        ->check(CLI::Range(2, 1000000));
    s.dup_cmd->add_option("--max-hamming", s.dup_max_hamming, "fingerprint: max Hamming distance 0..64 (0 = exact hash)")->default_val(0)
        ->check(CLI::Range(0, 64));
    s.dup_cmd->add_flag("--fingerprint-same-size-only", s.dup_fp_same_size,
                      "fingerprint: only compare files with equal byte size (faster for --max-hamming > 0)");
    s.dup_cmd->add_flag("--no-md", s.dup_no_md, "meta: ignore <stem>.md");
    s.dup_cmd->add_flag("--no-json", s.dup_no_json, "meta: ignore <stem>.json");
    s.dup_cmd->add_flag("--no-exif", s.dup_no_exif, "meta: do not include libvips EXIF in the hash");
    s.dup_cmd->add_option("--meta-prompt", s.dup_meta_prompt,
                        "meta: when set, prepended to the sidecar+EXIF corpus before hashing (same prompt ⇒ same bucket)");
    s.dup_cmd->add_flag(
        "--meta-compare-json-llm", s.dup_meta_json_llm,
        "meta: compare <stem>.json fields (alt, description, …) with the chat LLM (OpenRouter from settings) "
        "and group by similarity (see --meta-json-min-sim); not byte hash");
    s.dup_cmd->add_flag(
        "--meta-json-implicit-generate", s.dup_meta_json_implicit,
        "with --meta-compare-json-llm: for images without usable .json, run the Meta cataloguer first "
        "(resize + Google Gemini in options.meta from app provider settings) to create sidecars");
    s.dup_cmd->add_option("--meta-json-min-sim", s.dup_meta_json_min_sim,
                        "meta+LLM: min pairwise similarity 0..10 to link images (default 7)")
        ->check(CLI::Range(0, 10))
        ->default_val(7);
    s.dup_cmd->add_option(
        "--meta-json-compare-prompt", s.dup_meta_json_cmp_prompt,
        "meta+LLM: optional extra instruction text (preamble) for the compare call");
    s.dup_cmd->add_option(
        "--llm-router", s.dup_llm_router,
        "meta+LLM: router (default: chat provider in app settings)");
    s.dup_cmd->add_option("--llm-model", s.dup_llm_model,
                        "meta+LLM: model id (default: chat provider in app settings)");
    s.dup_cmd->add_option("--llm-api-key", s.dup_llm_api_key,
                        "meta+LLM: API key (optional; default from app chat / API Keys settings)");
    s.dup_cmd->add_option("--llm-base-url", s.dup_llm_base_url,
                        "meta+LLM: override OpenAI-compatible base URL (default: from app API Providers settings)");
    s.dup_cmd->add_option("--llm-timeout-ms", s.dup_llm_timeout_ms,
                        "meta+LLM: HTTP timeout per compare (ms, 0 = from settings or 60000)")
        ->default_val(0);
    s.dup_cmd->add_flag("--json", s.dup_print_json, "Print JSON (groups, paths, keys) on stdout");
    s.dup_cmd->add_option("--report-md", s.dup_report_md,
                        "Write a detailed per-category report (markdown) to this file; turns on full internal diagnostics");
    s.dup_cmd->add_option("--report-json", s.dup_report_json,
                        "Write the same full diagnostic payload as JSON to this file (see also --report-md)");
    s.dup_cmd->add_option(
        "--save-session", s.dup_save_session,
        "Write a reopenable session file (full report JSON, v2). Implies full diagnostics. Same as --report-json to this path for tooling.");
    s.dup_cmd->add_option("--load-session", s.dup_load_session,
                        "Load a session JSON from a prior --save-session / --report-json (validate + summary; no scan)");
}
