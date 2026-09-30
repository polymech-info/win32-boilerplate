#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_find.hpp"

int pm_image_cmd_find(CLI::App& app, PmImageCliState& st) {
    media::FindOptions fopts;
    fopts.mode             = st.fd_llm ? media::FindMode::Llm : media::FindMode::Name;
    fopts.prompt           = st.fd_prompt;
    fopts.case_insensitive = !st.fd_case_sensitive;
    fopts.match_folders    = !st.fd_no_folders;
    fopts.recursive        = !st.fd_no_recursive;
    fopts.bypass_cache     = st.fd_bypass_cache;
    fopts.generate         = !st.fd_no_generate;
    fopts.use_md           = !st.fd_no_md;
    fopts.use_json         = !st.fd_no_json;
    fopts.use_exif         = !st.fd_no_exif;
    fopts.judge_prompt     = st.fd_judge_prompt;
    fopts.max_results      = st.fd_max_results;
    fopts.dry_run          = st.fd_dry_run;
    fopts.reference_images = st.fd_refs;
    if (fopts.mode == media::FindMode::Llm)
        fopts.find_semantic_judge = !st.fd_local_text;

    // Meta plumbing reused for both LLM judge and on-the-fly cache generation.
    fopts.meta.provider.clear();
    fopts.meta.model.clear();
    if (st.fd_provider_opt->count() > 0) fopts.meta.provider = st.fd_provider;
    if (st.fd_model_opt->count() > 0) fopts.meta.model = st.fd_model;
    media_cli::apply_image_recognition_cli_defaults_from_app(
        fopts.meta.provider, fopts.meta.model, st.fd_provider_opt->count() > 0, st.fd_model_opt->count() > 0);
    if (fopts.mode == media::FindMode::Llm
        && (fopts.meta.provider.empty() || fopts.meta.model.empty()))
        return media_cli::fail_image_ai_requires_provider_model("find", fopts.meta.provider, fopts.meta.model);
    fopts.meta.api_key      = st.fd_api_key;
    fopts.meta.prompt       = st.fd_meta_prompt;
    fopts.meta.resize_first = !st.fd_no_resize;
    fopts.meta.resize_width = st.fd_resize_w;
    // We want generated cache persisted (so next find is fast).
    fopts.meta.out_md       = true;
    fopts.meta.out_json     = true;
    fopts.meta.update_exif  = false;

    if (fopts.mode == media::FindMode::Llm)
        media_cli::apply_image_ai_credentials_from_app(fopts.meta.provider, fopts.dry_run, fopts.meta.api_key,
                                                        fopts.meta.base_url);

    std::cerr << "â”€â”€ find â”€â”€ mode: " << (st.fd_llm ? "llm" : "name")
              << " | inputs: "       << st.fd_inputs.size()
              << " | prompt: \""     << st.fd_prompt << "\"\n";

    auto progress = [](const std::string& s) { std::cerr << s << "\n"; };
    auto r = media::find_images(st.fd_inputs, fopts, progress);
    if (!r.ok) { std::cerr << "find error: " << r.error << "\n"; return 1; }

    if (st.fd_print_json) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : r.matches) {
            arr.push_back({
                {"path",   m.path},
                {"score",  m.score},
                {"source", m.source},
                {"reason", m.reason},
            });
        }
        std::cout << arr.dump(2) << "\n";
    } else {
        for (const auto& m : r.matches) std::cout << m.path << "\n";
    }
    std::cerr << "scanned " << r.scanned
              << " | matched " << r.matches.size()
              << " | cache_hits " << r.cache_hits
              << " | generated " << r.generated << "\n";
    return r.matches.empty() ? 1 : 0;
}

void pm_image_register_find(CLI::App& app, PmImageCliState& s) {
    s.find_cmd = app.add_subcommand("find",
        "Find images by name/folder (free) or by semantic prompt via LLM (uses meta cache or generates it)");
    s.find_cmd->add_option("input", s.fd_inputs,
                         "Input file(s), folder(s) or glob(s); repeatable")->required(true)->expected(-1);
    s.find_cmd->add_option("-p,--prompt", s.fd_prompt, "Search query (required)")->required(true);
    s.find_cmd->add_flag  ("--llm", s.fd_llm,
                         "Query mode: match prompt vs filename + .md/.json/EXIF (or generate them); default = LLM judge per file, or --local-text");
    s.find_cmd->add_flag  ("--local-text", s.fd_local_text,
                         "With --llm: no find:judge Gemini; case-insensitive substring/word match on text only");
    s.find_cmd->add_flag  ("--case-sensitive", s.fd_case_sensitive, "Name mode: case-sensitive match");
    s.find_cmd->add_flag  ("--no-folders",     s.fd_no_folders,     "Name mode: don't match parent folder names");
    s.find_cmd->add_flag  ("--no-recursive",   s.fd_no_recursive,   "Don't recurse into directory inputs");
    s.find_cmd->add_flag  ("--bypass-cache",   s.fd_bypass_cache,   "LLM: ignore existing .md/.json/EXIF, force re-generate");
    s.find_cmd->add_flag  ("--no-generate",    s.fd_no_generate,    "LLM: skip images without cached meta (don't call meta_extract)");
    s.find_cmd->add_flag  ("--no-md",          s.fd_no_md,          "LLM: don't read sidecar .md");
    s.find_cmd->add_flag  ("--no-json",        s.fd_no_json,        "LLM: don't read sidecar .json");
    s.find_cmd->add_flag  ("--no-exif",        s.fd_no_exif,        "LLM: don't read libvips EXIF tags");
    s.find_cmd->add_option("--max",            s.fd_max_results,    "Max results (0 = unlimited)")->default_val(0);
    s.find_cmd->add_flag  ("--dry-run",        s.fd_dry_run,        "Resolve candidates + cache, no LLM calls / no writes");
    s.find_cmd->add_flag  ("--json",           s.fd_print_json,     "Print JSON array {path,score,source,reason} on stdout");
    s.fd_provider_opt = s.find_cmd->add_option(
        "--provider", s.fd_provider,
        "AI provider for --llm meta/judge; omit = app Chat image_recognition_provider (aborts if unset when --llm)");
    s.fd_model_opt = s.find_cmd->add_option(
        "--model", s.fd_model,
        "Model for --llm; omit = app image_recognition_model (aborts if unset when --llm)");
    s.find_cmd->add_option("--api-key",        s.fd_api_key,        "API key (optional; default from app provider settings)");
    s.find_cmd->add_option("--judge-prompt",   s.fd_judge_prompt,   "Override the LLM judge prompt");
    s.find_cmd->add_option("--meta-prompt",    s.fd_meta_prompt,    "Override the cataloguer prompt used when generating cache");
    s.find_cmd->add_flag  ("--no-resize",      s.fd_no_resize,      "Generation: skip in-memory resize before sending to model");
    s.find_cmd->add_option("--resize-width",   s.fd_resize_w,       "Generation: resize-width for the model input")->default_val(512);
    s.find_cmd->add_option("-r,--reference",   s.fd_refs,
        "LLM: reference image(s) — examples of what you're looking for; sent "
        "as multimodal parts in every judge call. Repeatable: -r logo.png -r palette.jpg");
}
