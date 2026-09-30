#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_search.hpp"

int pm_image_cmd_search(CLI::App& /*app*/, PmImageCliState& st) {
    media::SearchOptions sopts;

    sopts.query          = st.srch_query;
    sopts.is_regex       = st.srch_regex;
    sopts.case_sensitive = st.srch_case_sensitive;
    sopts.whole_word     = st.srch_whole_word;
    sopts.grep           = st.srch_grep;
    sopts.names_only     = st.srch_names_only;
    sopts.recursive      = !st.srch_no_recursive;
    sopts.include_hidden = st.srch_include_hidden;
    sopts.follow_symlinks = st.srch_follow_symlinks;
    sopts.skip_binary    = !st.srch_no_skip_binary;
    sopts.context_lines        = st.srch_context;
    sopts.context_before_lines = st.srch_context_before;
    sopts.context_after_lines  = st.srch_context_after;
    sopts.multiline            = st.srch_multiline;
    sopts.max_results          = st.srch_max_results;
    sopts.head_limit           = st.srch_head_limit;
    sopts.offset               = st.srch_offset;
    sopts.dry_run              = st.srch_dry_run;
    sopts.include_globs        = st.srch_include_globs;
    sopts.exclude_globs        = st.srch_exclude_globs;
    sopts.exclude_dirs         = st.srch_exclude_dirs;
    sopts.type_filter          = st.srch_type_filter;

    if (st.srch_output_mode == "files_with_matches")
        sopts.output_mode = media::SearchOutputMode::FilesWithMatches;
    else if (st.srch_output_mode == "count")
        sopts.output_mode = media::SearchOutputMode::Count;
    else
        sopts.output_mode = media::SearchOutputMode::Content;

    if (st.srch_max_file_size > 0)
        sopts.max_file_size_bytes = (uint64_t)st.srch_max_file_size;
    if (st.srch_max_per_file > 0)
        sopts.max_matches_per_file = (uint32_t)st.srch_max_per_file;

    // --type: "image" / "any" set SearchType; anything else is an rg-style type filter.
    if (st.srch_type_filter == "image") {
        sopts.type = media::SearchType::Image;
    } else if (st.srch_type_filter.empty() || st.srch_type_filter == "any") {
        sopts.type = media::SearchType::Any;
    } else {
        sopts.type        = media::SearchType::Any;
        sopts.type_filter = st.srch_type_filter;
    }
    sopts.indexer = (st.srch_indexer == "os") ? media::SearchIndexer::Os
                                              : media::SearchIndexer::Own;

    std::cerr << "── search ── type: " << st.srch_type_filter
              << " | mode: "   << (sopts.grep ? "grep" : "name")
              << " | indexer: " << st.srch_indexer
              << " | inputs: "  << st.srch_inputs.size()
              << " | query: \"" << sopts.query << "\"\n";

    auto progress = [](const std::string& s) { std::cerr << s << "\n"; };
    auto r = media::search_files(st.srch_inputs, sopts, progress);

    if (!r.ok) {
        std::cerr << "search error: " << r.error << "\n";
        return 1;
    }

    if (st.srch_print_json) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : r.matches) {
            nlohmann::json entry = {
                {"path",   m.path},
                {"source", m.source},
                {"score",  m.score},
            };
            if (m.line   > 0)         entry["line"]    = m.line;
            if (m.column > 0)         entry["column"]  = m.column;
            if (!m.preview.empty())   entry["preview"] = m.preview;
            if (!m.context_before.empty()) entry["context_before"] = m.context_before;
            if (!m.context_after.empty())  entry["context_after"]  = m.context_after;
            arr.push_back(std::move(entry));
        }
        std::cout << arr.dump(2) << "\n";
    } else {
        for (const auto& m : r.matches) {
            if (m.line > 0) {
                // grep-style: path:line:col: preview
                std::cout << m.path << ":" << m.line;
                if (m.column > 0) std::cout << ":" << m.column;
                std::cout << ": " << m.preview << "\n";
            } else {
                std::cout << m.path << "\n";
            }
        }
    }

    std::cerr << "visited " << r.stats.files_visited
              << " | scanned "  << r.stats.files_scanned
              << " | skipped "  << r.stats.files_skipped
              << " | binary "   << r.stats.files_binary
              << " | matches "  << r.stats.matches << "\n";

    return (r.matches.empty() && !st.srch_dry_run) ? 1 : 0;
}

void pm_image_register_search(CLI::App& app, PmImageCliState& s) {
    s.search_cmd = app.add_subcommand("search",
        "Search files by name/path or content (grep). "
        "Use --type=image to restrict to image files; --type=any (default) covers all files. "
        "Use --grep to scan file contents. "
        "Use --indexer=os to query the OS indexer (Windows Search / Spotlight / locate; "
        "falls back to own walker when unavailable).");
    s.search_cmd->add_option("input", s.srch_inputs,
                             "File(s), folder(s) or glob(s) to search in; repeatable")
        ->required(true)->expected(-1);
    s.search_cmd->add_option("-q,--query", s.srch_query,
                             "Search pattern (filename substring, regex, or grep needle)")->required(true);
    s.search_cmd
        ->add_option("--type", s.srch_type_filter,
                     "any (default) | image — limit candidates to image files; "
                     "or an rg-style file-type shorthand: cpp, c, cs, css, go, html, java, js, json, "
                     "kotlin, md, py, rs, rust, sh, swift, ts, toml, txt, xml, yaml. "
                     "Type shorthands expand to the matching filename globs automatically.")
        ->default_val("");
    s.search_cmd
        ->add_option("--indexer", s.srch_indexer,
                     "own (default) | os — use OS indexer (Windows Search / Spotlight / locate) "
                     "when set to os; falls back to own walker if unavailable")
        ->check(CLI::IsMember({"own", "os"}, CLI::ignore_case))
        ->default_val("own");
    s.search_cmd->add_flag("--grep",           s.srch_grep,
                           "Scan file contents for --query (not just filenames)");
    s.search_cmd->add_flag("--names-only",     s.srch_names_only,
                           "With --grep: report only the filepath, not individual match lines");
    s.search_cmd->add_flag("--regex",          s.srch_regex,
                           "Treat --query as an ECMAScript regex (std::regex)");
    s.search_cmd->add_flag("--case-sensitive", s.srch_case_sensitive,
                           "Case-sensitive match (default: case-insensitive)");
    s.search_cmd->add_flag("--whole-word",     s.srch_whole_word,
                           "Require word-boundary match (non-regex, literal mode)");
    s.search_cmd->add_flag("--no-recursive",   s.srch_no_recursive,
                           "Do not recurse into directory inputs");
    s.search_cmd->add_flag("--include-hidden", s.srch_include_hidden,
                           "Include dot-files and dot-directories");
    s.search_cmd->add_flag("--follow-symlinks",s.srch_follow_symlinks,
                           "Follow symbolic links during directory traversal");
    s.search_cmd->add_flag("--no-skip-binary", s.srch_no_skip_binary,
                           "Scan binary files (default: skip files that contain NUL bytes)");
    s.search_cmd
        ->add_option("-C,--context", s.srch_context,
                     "Symmetric context lines before AND after each match (--grep only; like rg -C)")
        ->default_val(0)->check(CLI::NonNegativeNumber);
    s.search_cmd
        ->add_option("-B,--context-before", s.srch_context_before,
                     "Context lines BEFORE each match (like rg -B; ignored when -C is set)")
        ->default_val(0)->check(CLI::NonNegativeNumber);
    s.search_cmd
        ->add_option("-A,--context-after", s.srch_context_after,
                     "Context lines AFTER each match (like rg -A; ignored when -C is set)")
        ->default_val(0)->check(CLI::NonNegativeNumber);
    s.search_cmd->add_flag("--multiline", s.srch_multiline,
                           "Enable multiline regex: ^ and $ match at line boundaries (like rg -U)");
    s.search_cmd
        ->add_option("--output-mode", s.srch_output_mode,
                     "Output mode: content (default) | files_with_matches | count")
        ->default_val("content")
        ->check(CLI::IsMember({"content", "files_with_matches", "count"}));
    s.search_cmd
        ->add_option("--head-limit", s.srch_head_limit,
                     "Cap output to first N entries (0 = unlimited)")->default_val(0);
    s.search_cmd
        ->add_option("--offset", s.srch_offset,
                     "Skip first N entries before applying --head-limit")->default_val(0);
    s.search_cmd
        ->add_option("--max", s.srch_max_results,
                     "Max total results (0 = unlimited)")->default_val(0);
    s.search_cmd
        ->add_option("--max-per-file", s.srch_max_per_file,
                     "Max matches per file in grep mode (0 = use default 1000)")->default_val(0);
    s.search_cmd
        ->add_option("--max-file-size", s.srch_max_file_size,
                     "Skip files larger than N bytes in grep mode (0 = use default 256 MB)")
        ->default_val(0);
    s.search_cmd
        ->add_option("--include", s.srch_include_globs,
                     "Only scan files matching this filename glob (e.g. *.cpp); repeatable")
        ->expected(-1);
    s.search_cmd
        ->add_option("--exclude", s.srch_exclude_globs,
                     "Skip files matching this filename glob; repeatable")
        ->expected(-1);
    s.search_cmd
        ->add_option("--exclude-dir", s.srch_exclude_dirs,
                     "Directory names to prune during recursion (exact match on final component); "
                     "repeatable. Default list used when not given: .git, node_modules, dist, …")
        ->expected(-1);
    s.search_cmd->add_flag("--dry-run",        s.srch_dry_run,
                           "Resolve candidates and print counts; no matching");
    s.search_cmd->add_flag("--json",           s.srch_print_json,
                           "Print JSON array {path,line,column,preview,source,score} on stdout");
}
