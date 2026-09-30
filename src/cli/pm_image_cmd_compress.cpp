#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_compress.hpp"

int pm_image_cmd_compress(CLI::App& app, PmImageCliState& st) {
    // â”€â”€ Build CompressOptions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    media::CompressOptions copts;

    if (st.cmp_compressor == "mozjpeg" || st.cmp_compressor == "MozJPEG")
        copts.compressor = media::Compressor::MozJPEG;
    else if (st.cmp_compressor == "png" || st.cmp_compressor == "PNG")
        copts.compressor = media::Compressor::PNG;
    else
        copts.compressor = media::Compressor::Auto;

    copts.strip_metadata            = !st.cmp_no_strip;
    copts.output_suffix             = st.cmp_suffix;
    copts.jpeg_quality              = st.cmp_quality;
    copts.jpeg_progressive          = !st.cmp_no_progressive;
    copts.jpeg_optimize_scans       = st.cmp_optimize_scans;
    copts.jpeg_trellis_quant        = st.cmp_trellis;
    copts.jpeg_overshoot_deringing  = true;
    copts.png_level                 = st.cmp_png_level;
    copts.png_quantize              = st.cmp_quantize;
    copts.png_quantize_colors       = st.cmp_colors;
    copts.png_quantize_quality      = st.cmp_quant_quality;
    copts.png_zopfli                = st.cmp_zopfli;
    copts.png_zopfli_iter           = st.cmp_zopfli_iter;

    // â”€â”€ Collect input / output pairs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    struct CmpPair { std::string in; std::string out; };
    std::vector<CmpPair> pairs;

    const bool use_src_dst = !st.cmp_src_list.empty() || !st.cmp_dst_flag.empty();
    if (use_src_dst) {
        if (st.cmp_src_list.empty() || st.cmp_dst_flag.empty()) {
            std::cerr << "compress: --src (one or more) and --dst must be used together\n";
            return 1;
        }
        media::InputSelection csel;
        csel.add_from_cli_src_list(st.cmp_src_list);
        std::string v_err;
        if (!csel.validate(v_err)) {
            std::cerr << "compress: " << v_err << "\n";
            return 1;
        }
        const std::vector<std::string> c_inputs = csel.resolve(v_err, true);
        if (!v_err.empty()) {
            std::cerr << v_err << "\n";
            return 1;
        }
        for (const std::string &s : c_inputs)
            pairs.push_back({s, media::compress_default_output(s, st.cmp_dst_flag, copts)});
    } else if (st.cmp_input.empty()) {
        std::cerr << "compress: provide input (positional) or use --src / --dst\n";
        return 1;
    } else {
        std::string out = st.cmp_output.empty()
            ? media::compress_default_output(st.cmp_input, {}, copts)
            : st.cmp_output;
        pairs.push_back({ st.cmp_input, out });
    }

    // â”€â”€ Run â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    if (!st.g_no_gui && (st.cmp_job_ui || pairs.size() > 1)) {
        namespace fs = std::filesystem;
        std::vector<media::win::ExplorerJobRow> rows;
        rows.reserve(pairs.size());
        for (const auto& p : pairs) {
            media::win::ExplorerJobRow row;
            const fs::path ip = fs::u8path(p.in);
            row.name = ip.filename().wstring();
            row.path = ip.wstring();
            rows.push_back(std::move(row));
        }
        int ok = 0, fail = 0;
        if (!media::win::run_explorer_job_ui(std::wstring(pm::brand::k_ui_job_title_compress_w), rows, [&](media::win::ExplorerJobHost& h) {
                for (std::size_t i = 0; i < pairs.size(); ++i) {
                    h.wait_if_paused();
                    if (h.cancel_requested()) break;
                    h.set_status(static_cast<int>(i), L"Running");
                    std::string err = media::compress_file(pairs[i].in, pairs[i].out, copts);
                    if (err.empty()) {
                        h.set_status(static_cast<int>(i), L"Done");
                        std::cout << pairs[i].out << "\n";
                        ++ok;
                    } else {
                        std::wstring w = L"Error: " + pmui::utf8_to_wide(err);
                        if (w.size() > 200) w.resize(200);
                        h.set_status(static_cast<int>(i), w.c_str());
                        std::cerr << "compress: " << pairs[i].in << " â†’ " << err << "\n";
                        ++fail;
                    }
                }
            }))
            return 1;
        if (pairs.size() > 1) std::cout << ok << " compressed\n";
        if (fail > 0) std::cerr << fail << " error(s)\n";
        return fail > 0 ? 1 : 0;
    } else
#endif
    {
        int ok = 0, fail = 0;
        for (auto& [in, out] : pairs) {
            std::string err = media::compress_file(in, out, copts);
            if (err.empty()) {
                std::cout << out << "\n";
                ++ok;
            } else {
                std::cerr << "compress: " << in << " â†’ " << err << "\n";
                ++fail;
            }
        }
        if (pairs.size() > 1) std::cout << ok << " compressed\n";
        if (fail > 0) std::cerr << fail << " error(s)\n";
        return fail > 0 ? 1 : 0;
    }
}

void pm_image_register_compress(CLI::App& app, PmImageCliState& s) {
    s.compress_cmd = app.add_subcommand("compress",
        "Compress images: MozJPEG re-encode or optimised PNG (+ libimagequant / zopfli)");
    s.compress_cmd->add_option("input",  s.cmp_input,  "Input file / glob")->required(false);
    s.compress_cmd->add_option("output", s.cmp_output, "Output file or directory");
    s.compress_cmd->add_option("--src", s.cmp_src_list, "Input(s) for batch; pair with --dst")->expected(-1);
    s.compress_cmd->add_option("--dst", s.cmp_dst_flag, "Output directory for batch --src");
    s.compress_cmd
        ->add_option("--compressor", s.cmp_compressor,
                     "mozjpeg | png  (default: inferred from output extension; falls back to mozjpeg)")
        ->check(CLI::IsMember({"mozjpeg", "png"}, CLI::ignore_case));
    s.compress_cmd->add_option("-q,--quality", s.cmp_quality, "MozJPEG quality 1–100")->default_val(85);
    s.compress_cmd->add_flag("--no-progressive",  s.cmp_no_progressive,
                           "Disable progressive (interlaced) JPEG");
    s.compress_cmd->add_flag("--optimize-scans",  s.cmp_optimize_scans,
                           "MozJPEG: split DCT coefficient spectrum into separate scans");
    s.compress_cmd->add_flag("--trellis-quant",   s.cmp_trellis,
                           "MozJPEG: trellis quantisation (slower, smaller)");
    s.compress_cmd->add_option("--level", s.cmp_png_level,
                             "PNG DEFLATE level 1–9")->default_val(9);
    s.compress_cmd->add_flag("--quantize", s.cmp_quantize,
                           "PNG: libimagequant palette reduction — lossy, up to ~60 % smaller"
                           " (requires FEATURE_PNG_COMPRESSOR)");
    s.compress_cmd->add_option("--colors",        s.cmp_colors,        "Palette size 8–256")->default_val(256);
    s.compress_cmd->add_option("--quant-quality", s.cmp_quant_quality, "Quantise quality 60–100")->default_val(85);
    s.compress_cmd->add_flag("--zopfli", s.cmp_zopfli,
                           "PNG: ultra-compress DEFLATE with zopfli — lossless, slow"
                           " (requires FEATURE_PNG_ZOPFLI)");
    s.compress_cmd->add_option("--zopfli-iter", s.cmp_zopfli_iter,
                             "Zopfli iteration count")->default_val(15);
    s.compress_cmd->add_flag("--no-strip", s.cmp_no_strip, "Keep metadata on output");
    s.compress_cmd->add_option("--suffix", s.cmp_suffix,
                             "Stem suffix for auto-generated output names "
                             "(default: _compressed when format unchanged)");
#if defined(_WIN32)
    s.compress_cmd->add_flag("--job-ui", s.cmp_job_ui, "List-style job window (pause / cancel) — Windows");
#endif
}
