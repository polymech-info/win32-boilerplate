#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_batch.hpp"

int pm_image_cmd_batch(CLI::App& app, PmImageCliState& st) {
#if defined(_WIN32)
    if (st.batch_list_cmd->parsed()) {
        const auto sessions = media::settings::list_sessions();
        if (sessions.empty()) {
            std::cout << "No saved sessions.\n";
            return 0;
        }
        std::cout << std::left
                  << std::setw(38) << "session-id"
                  << std::setw(12) << "op"
                  << std::setw(7)  << "done"
                  << std::setw(7)  << "err"
                  << std::setw(9)  << "pending"
                  << "date\n"
                  << std::string(80, '-') << "\n";
        for (const auto& s : sessions) {
            int d = 0, e = 0, p = 0;
            for (const auto& it : s.items) {
                if (it.status == "done")       ++d;
                else if (it.status == "error") ++e;
                else                           ++p;
            }
            std::cout << std::setw(38) << s.session_id
                      << std::setw(12) << s.op
                      << std::setw(7)  << d
                      << std::setw(7)  << e
                      << std::setw(9)  << p
                      << s.updated_at << "\n";
        }
        return 0;
    }

    if (st.batch_discard_cmd->parsed()) {
        std::string err;
        if (!media::settings::delete_session(st.batch_discard_id, err)) {
            std::cerr << "batch discard: " << err << "\n";
            return 1;
        }
        std::cout << "Session " << st.batch_discard_id << " discarded.\n";
        return 0;
    }

    if (st.batch_resume_cmd->parsed()) {
        const auto sessions = media::settings::list_sessions();
        const media::settings::PersistedSession* ps = nullptr;
        for (const auto& s : sessions) {
            if (s.session_id == st.batch_resume_id) { ps = &s; break; }
        }
        if (!ps) {
            std::cerr << "batch resume: session not found: " << st.batch_resume_id << "\n";
            return 1;
        }

        std::cout << "Resuming session " << ps->session_id
                  << " (op=" << ps->op << ")...\n";

        int ok = 0, fail = 0, skipped = 0;
        // Run each pending item using the appropriate op.
        for (const auto& si : ps->items) {
            if (si.status == "done") { ++skipped; continue; }
            if (media::cli::cancel_requested()) {
                std::cerr << "\nInterrupted.\n";
                break;
            }
            // Verify the file hasn't changed since the session was saved.
            const std::string current_hash = media::file_sha256_fast(si.path);
            if (!si.sha256.empty() && current_hash != si.sha256) {
                std::cerr << "  [warn] file changed since session: " << si.path << "\n";
            }

            std::cout << "  processing: " << si.path << "\n";

            if (ps->op == "resize") {
                media::ResizeOptions opts;
                // apply saved options if present
                if (!ps->options.is_null())
                    media::apply_resize_options_from_json(ps->options, opts);
                std::string err2;
                const bool r = media::resize_file(si.path, si.path, opts, err2);
                if (r) { ++ok; std::cout << "    ok\n"; }
                else   { ++fail; std::cerr << "    error: " << err2 << "\n"; }
            } else if (ps->op == "compress") {
                media::CompressOptions copts;
                if (!ps->options.is_null())
                    media::apply_compress_options_from_json(ps->options, copts);
                std::string err2 = media::compress_file(si.path, si.path, copts);
                if (err2.empty()) { ++ok; std::cout << "    ok\n"; }
                else              { ++fail; std::cerr << "    error: " << err2 << "\n"; }
            } else if (ps->op == "meta") {
                media::MetaOptions mopts;
                if (!ps->options.is_null())
                    media::apply_meta_options_from_json(ps->options, mopts);
                if (mopts.base_url.empty() && !mopts.dry_run)
                    media::fill_image_provider_base_url_from_app(mopts.base_url);
                media::MetaResult r = media::meta_extract(si.path, mopts, nullptr);
                if (r.ok) { ++ok; std::cout << "    ok\n"; }
                else      { ++fail; std::cerr << "    error: " << r.error << "\n"; }
            } else if (ps->op == "transform") {
                media::TransformOptions topts;
                if (!ps->options.is_null())
                    media::apply_transform_options_from_json(ps->options, topts);
                if (topts.base_url.empty()) media::fill_image_provider_base_url_from_app(topts.base_url);
                std::string output = media::default_transform_output(si.path, topts.prompt);
                auto r = media::transform_image(si.path, output, topts, nullptr);
                if (r.ok) { ++ok; std::cout << "    ok -> " << r.output_path << "\n"; }
                else      { ++fail; std::cerr << "    error: " << r.error << "\n"; }
            } else {
                std::cerr << "  batch resume: op '" << ps->op << "' not resumable from CLI\n";
                ++fail;
            }
        }
        std::cout << "\nDone: " << ok << " ok, " << fail << " failed, "
                  << skipped << " skipped.\n";
        if (media::cli::cancel_requested() || fail > 0) {
            // Update session: mark done items, leave the rest pending.
            // For simplicity, discard the session on clean completion.
            if (!media::cli::cancel_requested() && fail == 0) {
                std::string derr;
                media::settings::delete_session(ps->session_id, derr);
                std::cout << "Session complete removed from sessions.json.\n";
            }
        } else {
            std::string derr;
            media::settings::delete_session(ps->session_id, derr);
            std::cout << "Session complete  removed from sessions.json.\n";
        }
        return (fail > 0) ? 1 : 0;
    }
    std::cerr << "batch: no subcommand selected.\n";
    return 1;
#else
    std::cerr << "media-img: batch commands are only available on Windows.\n";
    return 1;
#endif
}

void pm_image_register_batch(CLI::App& app, PmImageCliState& s) {
    s.batch_cmd = app.add_subcommand(
        "batch",
        "Manage batch sessions (save / load / resume / list / discard).");
    s.batch_cmd->require_subcommand(1);
    s.batch_list_cmd = s.batch_cmd->add_subcommand(
        "list",
        "List saved sessions from sessions.json.");
    s.batch_discard_cmd = s.batch_cmd->add_subcommand(
        "discard",
        "Remove a saved session by id.");
    s.batch_discard_cmd->add_option("session-id", s.batch_discard_id,
                                   "Session id to remove")->required(true);
    s.batch_resume_cmd = s.batch_cmd->add_subcommand(
        "resume",
        "Resume a saved session by id (auto-detects op and re-runs pending items).");
    s.batch_resume_cmd->add_option("session-id", s.batch_resume_id,
                                  "Session id to resume")->required(true);
}
