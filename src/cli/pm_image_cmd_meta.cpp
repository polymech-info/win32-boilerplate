#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_meta.hpp"

int pm_image_cmd_meta(CLI::App& app, PmImageCliState& st) {
    media::MetaOptions mopts;
    mopts.provider.clear();
    mopts.model.clear();
    if (st.mt_provider_opt->count() > 0) mopts.provider = st.mt_provider;
    if (st.mt_model_opt->count() > 0) mopts.model = st.mt_model;
    media_cli::apply_image_recognition_cli_defaults_from_app(
        mopts.provider, mopts.model, st.mt_provider_opt->count() > 0, st.mt_model_opt->count() > 0);
    if (mopts.provider.empty() || mopts.model.empty())
        return media_cli::fail_image_ai_requires_provider_model("meta", mopts.provider, mopts.model);
    mopts.api_key      = st.mt_api_key;
    mopts.prompt       = st.mt_prompt;
    mopts.resize_first = !st.mt_no_resize;
    mopts.resize_width = st.mt_resize_w;
    mopts.out_md       = !st.mt_no_md;
    mopts.out_json     = !st.mt_no_json;
    mopts.update_exif  = st.mt_update_exif;
    mopts.out_dir      = st.mt_out_dir;
    mopts.dry_run      = st.mt_dry_run;

    media_cli::apply_image_ai_credentials_from_app(mopts.provider, mopts.dry_run, mopts.api_key, mopts.base_url);

    // CLI safety: require at least one disk output when not dry-running.
    if (!mopts.dry_run && !mopts.out_md && !mopts.out_json && !mopts.update_exif) {
        std::cerr << "meta: no outputs requested (enable .md / .json / EXIF, or use --dry-run)\n";
        return 1;
    }

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    if (!st.g_no_gui && (st.mt_job_ui || st.mt_inputs.size() > 1)) {
        namespace fs = std::filesystem;
        std::vector<media::win::ExplorerJobRow> rows;
        rows.reserve(st.mt_inputs.size());
        for (const auto& in : st.mt_inputs) {
            media::win::ExplorerJobRow row;
            const fs::path p = fs::u8path(in);
            row.name = p.filename().wstring();
            row.path = p.wstring();
            rows.push_back(std::move(row));
        }
        int ok = 0, fail = 0;
        if (!media::win::run_explorer_job_ui(std::wstring(pm::brand::k_ui_job_title_meta_w), rows, [&](media::win::ExplorerJobHost& h) {
                for (int i = 0; i < static_cast<int>(st.mt_inputs.size()); ++i) {
                    h.wait_if_paused();
                    if (h.cancel_requested()) break;
                    h.set_status(i, L"Running");
                    const std::string& in = st.mt_inputs[static_cast<std::size_t>(i)];
                    const auto         progress = [&](const std::string& msg) {
                        std::wstring w = pmui::utf8_to_wide(msg);
                        if (w.size() > 120) w.resize(120);
                        h.set_status(i, w.c_str());
                    };
                    auto r = media::meta_extract(in, mopts, progress);
                    if (!r.ok) {
                        std::wstring w = L"Error: " + pmui::utf8_to_wide(r.error);
                        if (w.size() > 200) w.resize(200);
                        h.set_status(i, w.c_str());
                        std::cerr << "meta error: " << r.error << "\n";
                        ++fail;
                        continue;
                    }
                    h.set_status(i, L"Done");
                    if (mopts.dry_run) {
                        std::cout << r.json_text << "\n";
                    } else {
                        if (!r.md_path.empty())   std::cout << r.md_path   << "\n";
                        if (!r.json_path.empty()) std::cout << r.json_path << "\n";
                        if (r.exif_updated)       std::cout << "EXIF: " << in << "\n";
                    }
                    ++ok;
                }
            }))
            return 1;
        if (st.mt_inputs.size() > 1) std::cerr << ok << " ok, " << fail << " failed\n";
        return fail > 0 ? 1 : 0;
    } else
#endif
    {
        auto progress = [](const std::string& msg) { std::cerr << msg << "\n"; };

        int ok = 0, fail = 0;
        for (const auto& in : st.mt_inputs) {
            std::cerr << "--- " << in << " ---\n";
            auto r = media::meta_extract(in, mopts, progress);
            if (!r.ok) {
                std::cerr << "meta error: " << r.error << "\n";
                ++fail;
                continue;
            }
            if (mopts.dry_run) {
                std::cout << r.json_text << "\n";
            } else {
                if (!r.md_path.empty())   std::cout << r.md_path   << "\n";
                if (!r.json_path.empty()) std::cout << r.json_path << "\n";
                if (r.exif_updated)       std::cout << "EXIF: " << in << "\n";
            }
            ++ok;
        }
        if (st.mt_inputs.size() > 1) std::cerr << ok << " ok, " << fail << " failed\n";
        return fail > 0 ? 1 : 0;
    }
}

void pm_image_register_meta(CLI::App& app, PmImageCliState& s) {
    s.meta_cmd = app.add_subcommand("meta",
        "Generate description / JSON / EXIF for images (LLM, Google Gemini)");
    s.meta_cmd->add_option("input", s.mt_inputs,
                         "Input image path(s); repeatable")->required(true)->expected(-1);
    s.meta_cmd->add_option("--out-dir", s.mt_out_dir,
                         "Output folder for .md / .json (default: next to source)");
    s.mt_provider_opt = s.meta_cmd->add_option(
        "--provider", s.mt_provider,
        "AI provider (google, replicate, pixlwiz); omit = from app Chat image_recognition_provider (aborts if unset)");
    s.mt_model_opt = s.meta_cmd->add_option(
        "--model", s.mt_model,
        "Model id; omit = from app Chat image_recognition_model (aborts if unset)");
    s.meta_cmd->add_option("--api-key", s.mt_api_key,
                         "API key (optional; default from app provider settings)");
    s.meta_cmd->add_option("-p,--prompt", s.mt_prompt,
                         "Override default prompt; empty = built-in cataloguer prompt");
    s.meta_cmd->add_flag("--no-resize", s.mt_no_resize,
                       "Do NOT pre-resize in memory before sending to the model");
    s.meta_cmd->add_option("--resize-width", s.mt_resize_w,
                         "In-memory resize target longest edge (default 512)")->default_val(512);
    s.meta_cmd->add_flag("--no-md",   s.mt_no_md,   "Skip <stem>.md output");
    s.meta_cmd->add_flag("--no-json", s.mt_no_json, "Skip <stem>.json output");
    s.meta_cmd->add_flag("--update-exif", s.mt_update_exif,
                       "Rewrite source: set EXIF ImageDescription tag (in place)");
    s.meta_cmd->add_flag("--dry-run", s.mt_dry_run,
                       "Validate args, run in-memory resize, print plan; no API call, no writes");
#if defined(_WIN32)
    s.meta_cmd->add_flag("--job-ui", s.mt_job_ui, "List-style job window (pause / cancel) — Windows");
#endif
}
