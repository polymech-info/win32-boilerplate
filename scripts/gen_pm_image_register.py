"""Generate pm_image_register_cli body: bindings only (no local declarations), s.-prefixed, safe for --flag names."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "main.cpp"
lines = MAIN.read_text(encoding="utf-8").splitlines()
chunk = lines[387:1076]

# Drop local variable declarations; members live in PmImageCliState.
out = []
for ln in chunk:
    t = ln.rstrip()
    if not t.strip():
        out.append(ln)
        continue
    # keep comments and preprocessor
    if t.lstrip().startswith("#"):
        out.append(ln)
        continue
    if t.lstrip().startswith("//"):
        out.append(ln)
        continue
    # remove [[maybe_unused]] g_no_gui line; struct default + add_flag
    if "[[maybe_unused]]" in t and "g_no_gui" in t:
        continue
    st = t.strip()
    if (st.startswith("std::string ") or st.startswith("std::vector<") or st.startswith("int ") or st.startswith("bool ")
            or st.startswith("const std::string ")):
        if st.endswith(";") and "add_" not in t and "->add_" not in t and "require_subcommand" not in t and "default_val" not in t and "check(" not in t and "Group" not in t:
            # e.g. `int url_timeout_sec = 5;` is a local decl — drop (members have in-class init)
            if re.search(
                r"^(const\s+)?std::(string|vector<[^>]+>)\s+(\w+)(\s*=\s*[^;]+)?\s*;\s*$|"
                r"^int\s+\w+(\s*=\s*[^;]+)?\s*;\s*$|^bool\s+\w+(\s*=\s*[^;]+)?\s*;\s*$",
                st,
            ) and "pm::" not in t:
                continue
    if st.startswith("CLI::Option*"):
        pass  # keep - will become s.x = below
    if st.startswith("auto") and "add_subcommand" in t:
        pass
    if st.startswith("auto*") and "app.add_subcommand" not in t and "add_subcommand" not in t and " = " in t and "->add_subcommand" in t:
        # inner auto* for nested sub — keep, transformed later
        pass
    out.append(ln)
text = "\n".join(out) + "\n"

# 1) CLI::Option* x = sub->  ->  s.x = s.sub->  (sub already s.xxx after step 2-3)
text = re.sub(r"CLI::Option\*\s*(\w+)\s*=\s*", r"s.\1 = ", text)
# 2) auto* foo = s.bar->add_subcommand  —  handle after s.cmd assign
text = re.sub(r"auto \*(\w+) = (app\.add_subcommand\()", r"s.\1 = \2", text)
text = re.sub(r"auto\*\s*(\w+) = (app\.add_subcommand\()", r"s.\1 = \2", text)
# nested: auto* x = provider_cmd->add_subcommand
text = re.sub(
    r"auto\*\s*(\w+) = (s\.\w+->add_subcommand\()",
    r"s.\1 = \2",
    text,
)
# fix remaining auto* y = s.llm_cmd->add_subcommand
text = re.sub(
    r"auto \*(\w+) = (s\.\w+->add_subcommand\()",
    r"s.\1 = \2",
    text,
)

# cmd->  prefix
cmds = [
    "resize_cmd", "compress_cmd", "transform_cmd", "create_cmd", "meta_cmd", "find_cmd", "dup_cmd",
    "serve_cmd", "ipc_cmd", "provider_cmd", "provider_models_cmd", "provider_models_list_cmd",
    "llm_cmd", "llm_list_cmd", "llm_call_cmd", "llm_agent_cmd", "reg_cmd", "app_cmd_grp",
    "app_screenshot_cmd", "app_pause_cmd", "app_resume_cmd", "app_cancel_cmd",
    "app_browse_cmd", "app_recordstart_cmd", "app_recordstop_cmd", "app_replay_cmd",
    "test_cmd", "test_screenshot_cmd", "replay_cmd",
    "batch_cmd", "batch_list_cmd", "batch_discard_cmd", "batch_resume_cmd",
    "license_cmd", "lic_fingerprint_cmd", "lic_import_cmd", "lic_verify_cmd",
    "status_cmd", "godmod_cmd", "purgetrial_cmd",
]
for c in sorted(set(cmds), key=len, reverse=True):
    text = re.sub(r"(?<![\w.])" + c + r"(?=->)", "s." + c, text)
    text = re.sub(r"(?<![\w.])" + c + r"(?=\))", "s." + c, text)  # if (resize_cmd) rarely

# Second-arg and flag-arg binding: add_*("..", var,  OR add_( flags  , var,
# Match `, name,` where name is a known field (after comma or opening paren for add_flag 3rd arg is description - skip)
FIELDS = """
g_no_gui win_console in_path out_path src_list dst_flag max_w max_h format fit position kernel background
quality png_compression rotate flip flop no_autorotate no_strip allow_enlargement
resize_no_cache resize_cache_dir url_timeout_sec url_max_redirects
resize_ui resize_ui_next resize_open_chat resize_job_ui
cmp_input cmp_output cmp_src_list cmp_dst_flag cmp_compressor cmp_quality
cmp_no_progressive cmp_optimize_scans cmp_trellis cmp_png_level cmp_quantize
cmp_colors cmp_quant_quality cmp_zopfli cmp_zopfli_iter cmp_no_strip cmp_suffix cmp_job_ui
tf_input tf_output tf_prompt tf_provider tf_model tf_api_key tf_aspect tf_size
tf_refs tf_preset_id tf_src_list tf_job_ui
cr_output cr_prompt cr_provider cr_model cr_api_key cr_aspect cr_size cr_refs
mt_inputs mt_out_dir mt_provider mt_model mt_api_key mt_prompt mt_no_resize mt_resize_w
mt_no_md mt_no_json mt_update_exif mt_dry_run mt_job_ui
fd_inputs fd_prompt fd_llm fd_case_sensitive fd_no_folders fd_no_recursive fd_bypass_cache
fd_no_generate fd_no_md fd_no_json fd_no_exif fd_max_results fd_dry_run fd_print_json
fd_provider fd_model fd_api_key fd_judge_prompt fd_meta_prompt fd_resize_w fd_no_resize
fd_local_text fd_refs
dup_inputs dup_by dup_no_recursive dup_min_group dup_max_hamming dup_fp_same_size
dup_no_md dup_no_json dup_no_exif dup_meta_prompt dup_meta_json_llm dup_meta_json_implicit
dup_meta_json_min_sim dup_meta_json_cmp_prompt dup_llm_router dup_llm_model dup_llm_api_key
dup_llm_base_url dup_llm_timeout_ms dup_print_json dup_report_md dup_report_json
dup_load_session dup_save_session
host port serve_no_cache serve_cache_dir ipc_host ipc_port ipc_unix ipc_no_cache ipc_cache_dir
pm_provider pm_api_key pm_base_url pm_limit pm_cursor pm_sort_by pm_sort_direction
llm_call_name llm_call_args_path llm_call_image_file
ag_prompt ag_paths ag_folder ag_router ag_model ag_api_key ag_base_url ag_timeout_ms
ag_max_iter ag_json ag_dry ag_no_tools ag_disable_tools ag_log_path
reg_group reg_unregister reg_dry reg_no_refresh reg_media_bin reg_widths
app_chat_paths app_browse_paths app_replay_path
test_screenshot_out test_screenshot_wait_ms replay_session_path
batch_discard_id batch_resume_id
lic_import_path
status_json
""".split()
fields = sorted({x for x in FIELDS if x}, key=len, reverse=True)
for f in fields:
    # .add_*( "...", field,  or  '", field) or  , field) at end
    text = re.sub(
        r'("(?:[^"\\]|\\.)*"\s*,\s*)' + re.escape(f) + r"(\s*,)",
        r"\1s." + f + r"\2",
        text,
    )
    # add_flag("--x", name,
    text = re.sub(
        r"(\badd_(?:option|flag)\s*\(\s*\"[^\"]+\"\s*,\s*)" + re.escape(f) + r"(\s*,)",
        r"\1s." + f + r"\2",
        text,
    )
    # ->default_val(s.field)  already has s if field matched — skip
    # second pattern for -q,--quality style
    text = re.sub(
        r'("(?:-[^,]+,)?[^"]*"\s*,\s*)' + re.escape(f) + r"(\s*,)",
        r"\1s." + f + r"\2",
        text,
    )

# default_val and check that reference fields: .default_val(host) in original was port default — check main
# Line 426: .default_str("fingerprint") - ok
# .default_val(2) for fd_max - numbers

# Remove duplicate s. if any
text = re.sub(r"\bs\.\s*s\.", "s.", text)

# Fix s.s. in case
if "s.s." in text:
    raise SystemExit("double s. still present")

(ROOT / "src" / "cli" / "_register_gen.txt").write_text(text, encoding="utf-8")
print("ok, len", len(text))
