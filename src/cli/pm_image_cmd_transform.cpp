#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_transform.hpp"

namespace {

nlohmann::json transform_output_row(const media::TransformResult& result, const std::string& source)
{
    nlohmann::json row{
        {"kind", "image"},
        {"path", result.output_path},
    };
    if (!source.empty())
        row["source"] = source;
    if (!result.ai_text.empty())
        row["text"] = result.ai_text;
    if (!result.replicate_prediction_web_url.empty())
        row["predictionUrl"] = result.replicate_prediction_web_url;
    return row;
}

} // namespace

int pm_image_cmd_transform(CLI::App& app, PmImageCliState& st) {
    std::vector<std::string> tf_in_list;
    {
        media::InputSelection tsel;
        if (!st.tf_src_list.empty()) {
            if (!st.tf_input.empty()) {
                std::cerr << "transform: use either positional input or --src, not both\n";
                return 1;
            }
            tsel.add_from_cli_src_list(st.tf_src_list);
        } else if (!st.tf_input.empty()) {
            tsel.add({st.tf_input, {}});
        } else {
            std::cerr << "transform: provide an input file, or one or more --src paths\n";
            return 1;
        }
        std::string v_err;
        if (!tsel.validate(v_err)) {
            std::cerr << "transform: " << v_err << "\n";
            return 1;
        }
        tf_in_list = tsel.resolve(v_err, true);
        if (!v_err.empty()) {
            std::cerr << v_err << "\n";
            return 1;
        }
    }
    if (tf_in_list.size() > 1 && !st.tf_output.empty()) {
        std::cerr << "transform: with multiple --src, omit the output path (per-input names are used)\n";
        return 1;
    }

    media::TransformOptions topts;
    // Struct defaults are google/gemini; clear so --preset-id that only supplies prompt does not block
    // chat.image_provider / image_model (see pm-image-iexecute.log correlation lines).
    topts.provider.clear();
    topts.model.clear();
    if (!st.tf_preset_id.empty()) {
        std::string perr;
        if (!media_cli::resolve_transform_preset_id(st.tf_preset_id, topts, perr)) {
            std::cerr << "transform: " << perr << "\n";
            return 1;
        }
    }
#if defined(_WIN32)
    media::settings::append_explorer_shell_correlation_log_utf8(
        std::string("transform: after --preset-id resolve preset_id=")
        + (st.tf_preset_id.empty() ? "(none)" : st.tf_preset_id) + " provider=\"" + topts.provider + "\" model=\""
        + topts.model + "\"");
#endif
    if (st.tf_provider_opt->count() > 0) topts.provider = st.tf_provider;
    if (st.tf_model_opt->count() > 0) topts.model = st.tf_model;
#if defined(_WIN32)
    {
        std::string                    errl;
        media::runtime_settings::ChatProviderSettings cs;
        std::string chat_ip, chat_im;
        if (media::runtime_settings::load_chat_provider(cs, errl)) {
            chat_ip = cs.image_provider;
            chat_im = cs.image_model;
        }
        media::runtime_settings::ProviderMap pm;
        if (media::runtime_settings::load_providers(pm, errl)) {
            media::settings::append_explorer_shell_correlation_log_utf8(
                "transform: provider_row_count=" + std::to_string(pm.size()));
        }
        media::settings::append_explorer_shell_correlation_log_utf8(
            "transform: chat.image_provider=\"" + chat_ip + "\" chat.image_model=\"" + chat_im + "\"");
        media::settings::append_explorer_shell_correlation_log_utf8(
            std::string("transform: after CLI flags provider=\"") + topts.provider + "\" model=\"" + topts.model
            + "\" user_set_provider=" + (st.tf_provider_opt->count() > 0 ? "yes" : "no") + " user_set_model="
            + (st.tf_model_opt->count() > 0 ? "yes" : "no"));
    }
#endif
    media_cli::apply_image_ai_cli_defaults_from_app(topts.provider, topts.model, st.tf_provider_opt->count() > 0,
                                         st.tf_model_opt->count() > 0);
#if defined(_WIN32)
    media::settings::append_explorer_shell_correlation_log_utf8(
        std::string("transform: after apply_image_ai_cli_defaults provider=\"") + topts.provider + "\" model=\""
        + topts.model + "\"");
#endif
    if (topts.provider.empty() || topts.model.empty())
        return media_cli::fail_image_ai_requires_provider_model("transform", topts.provider, topts.model);
    topts.api_key          = st.tf_api_key;
    topts.aspect_ratio     = st.tf_aspect;
    topts.image_size       = st.tf_size;
    topts.reference_images = st.tf_refs;
    if (st.tf_preset_id.empty()) {
        topts.prompt = st.tf_prompt;
    } else if (!st.tf_prompt.empty()) {
        topts.prompt = st.tf_prompt;
    }
    if (topts.prompt.empty()) {
        std::cerr << "transform: set --prompt or --preset-id (with prompt in settings)\n";
        return 1;
    }

    media_cli::apply_image_ai_credentials_from_app(topts.provider, false, topts.api_key, topts.base_url);

#if defined(_WIN32)
    media::settings::append_explorer_shell_correlation_log_utf8(
        std::string("transform: before transform_image (pre-merge) provider=\"") + topts.provider + "\" model=\""
        + topts.model + "\" api_key=" + (topts.api_key.empty() ? "empty" : "set") + " base_url="
        + (topts.base_url.empty() ? "empty" : "set")
        + " â€” if provider is google, Gemini HTTP is used; merge_image_provider_credentials may switch provider "
          "when portable settings + env interact");
#endif

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    if (!st.g_no_gui && (st.tf_job_ui || tf_in_list.size() > 1)) {
        namespace fs   = std::filesystem;
        media::BatchControl                       batch;
        std::vector<media::win::ExplorerJobRow>  rows;
        rows.reserve(tf_in_list.size());
        for (const std::string& p8 : tf_in_list) {
            const fs::path     pin = fs::u8path(p8);
            media::win::ExplorerJobRow row;
            row.name = pin.filename().wstring();
            row.path = pin.wstring();
            rows.push_back(std::move(row));
        }
        int     okc = 0, failc = 0;
        if (!media::win::run_explorer_job_ui(
                std::wstring(pm::brand::k_ui_job_title_transform_w), rows, [&](media::win::ExplorerJobHost& h) {
                    h.link_batch_control(&batch);
                    for (int i = 0; i < static_cast<int>(tf_in_list.size()); ++i) {
                        h.wait_if_paused();
                        if (h.cancel_requested()) break;
                        h.set_status(i, L"Running");
                        const std::string& in8 = tf_in_list[static_cast<std::size_t>(i)];
                        const std::string  out1 =
                            (tf_in_list.size() == 1) ? st.tf_output : std::string{};
                        const auto         progress = [&, i](const std::string& msg) {
                            std::wstring w = pmui::utf8_to_wide(msg);
                            if (w.size() > 160) w.resize(160);
                            h.set_status(i, w.c_str());
                        };
                        const media::TransformResult tres =
                            media::transform_image(in8, out1, topts, progress, &batch, {});
                        if (tres.ok) {
                            h.set_status(i, L"Done");
                            std::cout << tres.output_path << "\n";
                            if (!tres.ai_text.empty()) logger::info(std::string("AI: ") + tres.ai_text);
                            ++okc;
                        } else {
                            std::wstring e = L"Error: " + pmui::utf8_to_wide(tres.error);
                            if (e.size() > 200) e.resize(200);
                            h.set_status(i, e.c_str());
                            std::cerr << "transform error: " << tres.error << " (" << in8 << ")\n";
                            ++failc;
                        }
                    }
                }))
            return 1;
        if (tf_in_list.size() > 1) logger::info(std::to_string(okc) + " ok, " + std::to_string(failc) + " failed");
        return failc > 0 ? 1 : 0;
    }
#endif
    {
        media::BatchControl batch;
        int okc = 0, failc = 0;
        nlohmann::json outputs = nlohmann::json::array();
        nlohmann::json errors = nlohmann::json::array();
        for (const std::string& in8 : tf_in_list) {
            if (media::cli::cancel_requested()) {
                batch.request_cancel();
                break;
            }
            const std::string out1 = (tf_in_list.size() == 1) ? st.tf_output : std::string{};
            auto                progress = [](const std::string& msg) {
                logger::info(msg);
            };
            const media::TransformResult result = media::transform_image(in8, out1, topts, progress, &batch, {});
            if (!result.ok) {
                if (media::cli::cancel_requested() || result.error.find("cancelled") != std::string::npos) {
                    batch.request_cancel();
                    std::cerr << "transform: interrupted\n";
                    return 130;
                }
                std::cerr << "transform error: " << result.error << " (" << in8 << ")\n";
                errors.push_back({{"source", in8}, {"error", result.error}});
                ++failc;
                continue;
            }
            outputs.push_back(transform_output_row(result, in8));
            if (!st.tf_json)
                std::cout << result.output_path << "\n";
            if (!result.ai_text.empty()) logger::info(std::string("AI: ") + result.ai_text);
            ++okc;
        }
        if (media::cli::cancel_requested() || batch.cancel.load()) return 130;
        if (tf_in_list.size() > 1) logger::info(std::to_string(okc) + " ok, " + std::to_string(failc) + " failed");
        if (st.tf_json) {
            std::cout << nlohmann::json{
                {"ok", failc == 0},
                {"command", "transform"},
                {"outputs", std::move(outputs)},
                {"errors", std::move(errors)},
            }.dump(2) << "\n";
        }
        return failc > 0 ? 1 : 0;
}
}

void pm_image_register_transform(CLI::App& app, PmImageCliState& s) {
    s.transform_cmd = app.add_subcommand("transform", "AI image editing (Gemini / Google)");
    s.transform_cmd
        ->add_option("input", s.tf_input, "Input image, or use --src for a batch (Explorer multi-select uses --src)")
        ->required(false);
    s.transform_cmd
        ->add_option(
            "--src", s.tf_src_list,
            "Input path (repeat for multiple files; one job queue / one window with --job-ui on Windows)")
        ->expected(-1);
    s.transform_cmd->add_option("output", s.tf_output, "Output path (omit = auto from input + prompt)");
    s.transform_cmd->add_option(
        "-p,--prompt", s.tf_prompt,
        "Editing prompt (required if --preset-id is not set, unless preset supplies prompt)");
    s.transform_cmd->add_option(
        "--preset-id", s.tf_preset_id,
        "Preset id from settings: explorer_presets (op=transform), or chat_web quick action as chat-<id>");
    s.tf_provider_opt = s.transform_cmd->add_option(
        "--provider", s.tf_provider,
        "AI provider (google, replicate, pixlwiz); omit = from app Chat image_provider (aborts if unset)");
    s.tf_model_opt = s.transform_cmd->add_option(
        "--model", s.tf_model,
        "Model id; omit = from app Chat image_model (aborts if unset)");
    s.transform_cmd->add_option("--api-key", s.tf_api_key, "API key (optional; default from app provider settings)");
    s.transform_cmd->add_option("--aspect-ratio", s.tf_aspect, "Output aspect ratio (1:1,16:9,4:3,...)");
    s.transform_cmd->add_option("--image-size", s.tf_size, "Output size (512,1K,2K,4K)");
    s.transform_cmd->add_option("-r,--reference", s.tf_refs,
                              "Reference image path (logo / brand sheet / style swatch). "
                              "Repeatable: -r logo.png -r palette.jpg")
                  ->expected(-1);
    s.transform_cmd->add_flag("--json", s.tf_json, "Print machine-readable outputs JSON on stdout.");
#if defined(_WIN32)
    s.transform_cmd->add_flag("--job-ui", s.tf_job_ui,
                            "List-style job window (pause / cancel) — Windows");
#endif
}
