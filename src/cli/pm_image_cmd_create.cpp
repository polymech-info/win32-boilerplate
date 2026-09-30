#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_create.hpp"

int pm_image_cmd_create(CLI::App& app, PmImageCliState& st) {
    media::TransformOptions ctopts;
    ctopts.provider.clear();
    ctopts.model.clear();
    if (st.cr_provider_opt->count() > 0) ctopts.provider = st.cr_provider;
    if (st.cr_model_opt->count() > 0) ctopts.model = st.cr_model;
    media_cli::apply_image_ai_cli_defaults_from_app(ctopts.provider, ctopts.model, st.cr_provider_opt->count() > 0,
                                       st.cr_model_opt->count() > 0);
    if (ctopts.provider.empty() || ctopts.model.empty())
        return media_cli::fail_image_ai_requires_provider_model("create", ctopts.provider, ctopts.model);
    ctopts.prompt           = st.cr_prompt;
    ctopts.api_key          = st.cr_api_key;
    ctopts.aspect_ratio     = st.cr_aspect;
    ctopts.image_size       = st.cr_size;
    ctopts.reference_images = st.cr_refs;

    media_cli::apply_image_ai_credentials_from_app(ctopts.provider, false, ctopts.api_key, ctopts.base_url);

    auto progress = [](const std::string& msg) {
        logger::info(msg);
    };

    media::BatchControl batch;
    auto cresult = media::create_image(st.cr_output, ctopts, progress, &batch, {});
    if (!cresult.ok) {
        if (media::cli::cancel_requested() || cresult.error.find("cancelled") != std::string::npos) {
            batch.request_cancel();
            std::cerr << "create: interrupted\n";
            return 130;
        }
        std::cerr << "create error: " << cresult.error << "\n";
        return 1;
    }
    if (st.cr_json) {
        nlohmann::json row{
            {"kind", "image"},
            {"path", cresult.output_path},
        };
        if (!cresult.ai_text.empty())
            row["text"] = cresult.ai_text;
        if (!cresult.replicate_prediction_web_url.empty())
            row["predictionUrl"] = cresult.replicate_prediction_web_url;
        std::cout << nlohmann::json{
            {"ok", true},
            {"command", "create"},
            {"outputs", nlohmann::json::array({std::move(row)})},
            {"errors", nlohmann::json::array()},
        }.dump(2) << "\n";
    } else {
        std::cout << cresult.output_path << "\n";
    }
    if (!cresult.ai_text.empty())
        logger::info(std::string("AI: ") + cresult.ai_text);
    return 0;
}

void pm_image_register_create(CLI::App& app, PmImageCliState& s) {
    s.create_cmd = app.add_subcommand("create", "AI text-to-image (Gemini / Google, no input file)");
    s.create_cmd->add_option("output", s.cr_output, "Output path (omit = create_<slug>.png in cwd)");
    s.create_cmd->add_option("-p,--prompt", s.cr_prompt, "Generation prompt")->required(true);
    s.cr_provider_opt = s.create_cmd->add_option(
        "--provider", s.cr_provider,
        "AI provider (google, replicate, pixlwiz); omit = from app Chat image_provider (aborts if unset)");
    s.cr_model_opt = s.create_cmd->add_option(
        "--model", s.cr_model,
        "Model id; omit = from app Chat image_model (aborts if unset)");
    s.create_cmd->add_option("--api-key", s.cr_api_key, "API key (optional; default from app provider settings)");
    s.create_cmd->add_option("--aspect-ratio", s.cr_aspect, "Output aspect ratio (1:1,16:9,4:3,...)");
    s.create_cmd->add_option("--image-size", s.cr_size, "Output size (512,1K,2K,4K)");
    s.create_cmd->add_option("-r,--reference", s.cr_refs,
                           "Reference image path (style / brand). Repeat as needed.")
        ->expected(-1);
    s.create_cmd->add_flag("--json", s.cr_json, "Print machine-readable outputs JSON on stdout.");
}
