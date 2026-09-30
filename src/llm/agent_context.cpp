#include "agent_internal.hpp"

#include "constants.hpp"
#include "path_tool_catalog.hpp"
#include "agent_memory.hpp"

#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace media::llm::agent::detail {

// ── Tool list + routing block ─────────────────────────────────────────────────

// tools: the fully-assembled OpenAI tools JSON array (path catalog + MCP tools,
// already filtered for disabled entries). Passing it in avoids a second catalog
// rebuild and ensures MCP tools appear in the system prompt.
void append_agent_tooling_and_context(std::ostringstream& s,
                                      const Turn&         turn,
                                      const nlohmann::json& tools)
{
    // Build a fast enabled-tool lookup from the actual tools the LLM will see.
    // This is the single source of truth — no tool name can sneak in via any
    // other code path.
    std::unordered_set<std::string> enabled_tools;
    for (const auto& t : tools) {
        if (!t.contains("function") || !t["function"].is_object()) continue;
        std::string nm = t["function"].value("name", std::string{});
        if (nm.empty()) continue;
        str_tolower_in_place(nm);
        enabled_tools.insert(std::move(nm));
    }
    auto has = [&](const char* name) -> bool {
        return enabled_tools.count(name) > 0;
    };
    // True for any tool whose name starts with "mcp_"
    auto has_any_mcp = [&]() {
        for (const auto& nm : enabled_tools)
            if (nm.size() >= 4 && nm.compare(0, 4, "mcp_") == 0) return true;
        return false;
    };

    // -- Intro: describe capability families based on what's actually enabled --
    {
        const bool has_image  = has("list_images") || has("image_resize") || has("image_compress")
                             || has("image_transform") || has("image_create") || has("image_understand")
                             || has("create_video");
        const bool has_file   = has("file_read") || has("file_glob") || has("write_file");
        const bool has_memory = has("memory_read") || has("memory_write");

        s << "You are " << pm::brand::k_app_id_u8 << "'s chat agent.";
        if (has_image || has_file || has_memory || has_any_mcp()) {
            s << " Active tool families this session:\n";
            if (has_image) {
                std::string img_tools;
                for (const char* t : {"list_images","image_resize","image_compress",
                                      "image_transform","image_create","create_video","image_understand"}) {
                    if (has(t)) { if (!img_tools.empty()) img_tools += ", "; img_tools += t; }
                }
                s << "  \u2022 **Image / raster** \u2014 " << img_tools << "\n";
            }
            if (has_file) {
                s << "  \u2022 **Text / file** \u2014";
                if (has("file_read"))  s << " file_read;";
                if (has("file_glob"))  s << " file_glob;";
                if (has("write_file")) s << " write_file;";
                s << " (logs, configs, .md, .json, CSV, .dxf, .svg, \u2026)";
                if (has_image) s << " \u2014 image_* tools are for bitmaps; use file tools for text/vector paths.";
                s << "\n";
            }
            if (has("speak"))             s << "  \u2022 **Speech** \u2014 speak (TTS)\n";
            if (has("image_from_camera")) s << "  \u2022 **Camera** \u2014 image_from_camera\n";
            if (has_memory)               s << "  \u2022 **Memory** \u2014 memory_read / memory_write\n";
            if (has_any_mcp())            s << "  \u2022 **MCP (remote)** \u2014 see tool list below\n";
        }
        s << "\n";
    }

    // -- Tool list (uses the provided tools JSON directly) --------------------
    s << "## Available tools:\n";
    for (const auto& t : tools) {
        if (!t.contains("function") || !t["function"].is_object()) continue;
        const std::string name = t["function"].value("name", std::string{});
        const std::string desc = t["function"].value("description", std::string{});
        if (name.empty()) continue;
        s << "  - " << name;
        if (!desc.empty()) s << " \u2014 " << desc;
        s << "\n";
    }
    s << "\n";

    // -- Selection / folder context ------------------------------------------
    if (!turn.selection.empty()) {
        s << "The user currently has " << turn.selection.size() << " file(s) selected:\n";
        for (std::size_t i = 0; i < turn.selection.size(); ++i)
            s << "  [" << i << "] " << turn.selection[i] << "\n";
        s << "\n"
             "Use these paths when a tool needs actual file locations: copy the full path "
             "from **[0]**, **[1]**, \u2026 in order. Unless the user names a different path, "
             "operate on these selected files.\n";
        if (has("file_read") || has("file_glob") || has("write_file")) {
            s << "If a selection is **not** a raster image (e.g. `.dxf`, `.svg`, `.csv`, "
                 "`.txt`, `.md`, `.json`, `.log`), treat it as a **text / structured-text** path:";
            if (has("file_read"))  s << " use **file_read** to open or summarise its contents;";
            if (has("write_file")) s << " use **write_file** to save notes or exports beside it;";
            if (has("file_glob"))  s << " use **file_glob** to find related files in the same folder.";
            s << "\n";
        }
    } else if (!turn.folder_hint.empty()) {
        s << "The user is browsing folder: " << turn.folder_hint << "\n"
             "There is NO active file selection. ";
        bool any_hint = false;
        if (has("list_images") || has("file_glob") || has("image_understand")) {
            s << "When the user refers to 'this folder' / 'these files' without naming paths, use:\n";
            if (has("list_images")) {
                s << "  - **list_images** with inputs=[\"" << turn.folder_hint << "\"] to enumerate photos;\n";
                any_hint = true;
            }
            if (has("file_glob") && has("file_read")) {
                s << "  - **file_glob** (pattern relative to this folder) then **file_read** "
                     "for non-image files (logs, .md, configs, .dxf, .svg, .csv);\n";
                any_hint = true;
            } else if (has("file_glob")) {
                s << "  - **file_glob** to discover non-image files;\n";
                any_hint = true;
            }
            if (has("list_images") && has("image_understand")) {
                s << "  - **list_images** then **image_understand** to search photo contents.\n";
                any_hint = true;
            }
        }
        if (any_hint)
            s << "Use the exact folder path above in tool calls; invented paths will fail.\n";
    } else {
        s << "The user has no active selection and no current folder. "
             "If they refer to files without naming them, ask which folder or files to work on.\n";
    }
    s << "\n";

    // -- Guidelines ----------------------------------------------------------
    s << "## Guidelines:\n"
         "  - For **all** path tools, relative path strings are resolved against the current "
         "Explorer folder (or the parent of the first selected file). Invented paths will fail.\n"
         "  - Always call the tool that matches the user's intent \u2014 do not narrate what you would do.\n"
         "  - **Lean replies:** No \"Here are\u2026\" intros, no **Summary:** blocks, no closing remarks. "
         "Do not restate what a table or list already shows.\n"
         "  - If a tool reports per-file failures, state them briefly once.\n";

    if (has("list_images") || has("file_glob")) {
        s << "  - When the user asks \u2018what\u2019s in this folder\u2019 / \u2018show me\u2019 / \u2018how many images\u2019:";
        if (has("list_images")) s << " call **list_images** for photos;";
        if (has("file_glob"))   s << " call **file_glob** for other files;";
        s << "\n";
    }
    if (has("write_file"))
        s << "  - When the user asks to save a report, note, or any text file, "
             "call **write_file** with the full content \u2014 do not claim the file exists without calling the tool.\n";

    {
        std::vector<std::string> settings_tools;
        for (const char* t : {"image_create", "create_video", "image_understand"})
            if (has(t)) settings_tools.push_back(t);
        if (!settings_tools.empty()) {
            s << "  - For ";
            for (std::size_t i = 0; i < settings_tools.size(); ++i) {
                if (i) s << (i + 1 == settings_tools.size() ? " and " : ", ");
                s << "**" << settings_tools[i] << "**";
            }
            s << ": provider/model and API keys come only from **app Settings** \u2014 do not "
                 "put model/provider/api_key in tool arguments.\n";
        }
    }
    if (has("create_video"))
        s << "  - **create_video** hard rule: one call per user video request. "
             "If it fails (HTTP 429 / throttled), do not retry \u2014 explain and tell the user to wait.\n";

    // -- Tool routing --------------------------------------------------------
    s << "\nChoosing a tool from the user\u2019s intent:\n";
    if (has("file_read") || has("file_glob"))
        s << "  \u2022 'read this file' / 'open' / 'summarise' (non-raster) \u2192 "
          << (has("file_read") ? "**file_read**" : "")
          << (has("file_read") && has("file_glob") ? " (use **file_glob** to discover paths first)" : "")
          << (!has("file_read") && has("file_glob") ? "**file_glob**" : "")
          << "\n";
    if (has("image_transform"))
        s << "  \u2022 'enhance' / 'edit' / 'fix' / 'clean up' / 'remove X' / 'restyle' \u2192 **image_transform**\n";
    if (has("image_create"))
        s << "  \u2022 'generate' / 'create an image' / 'draw' \u2192 **image_create**\n";
    if (has("create_video"))
        s << "  \u2022 'video' / 'animate' / 'image-to-video' \u2192 **create_video** "
             "(once per request; never chain a second call after failure)\n";
    if (has("image_resize"))
        s << "  \u2022 'resize' / 'shrink' / 'fit in NxM' / 'downscale' \u2192 **image_resize**\n";
    if (has("list_images"))
        s << "  \u2022 'list' / 'what\u2019s here' / 'how many photos' \u2192 **list_images**\n";
    if (has("image_understand"))
        s << "  \u2022 Visual questions ('what is this' / 'describe' / 'compare') \u2192 "
             "**image_understand** (never call it before an action verb \u2014 action tools "
             "read bytes themselves). Compose the `prompt` field as a direct, specific instruction. "
             "Pass ALL relevant images in one call.\n";

    // -- Scheduler block (only when scheduler tools are present) -------------
    if (has("schedule_at") || has("schedule_in") || has("schedule_every")) {
        s << "\n## Scheduled tasks:\n"
             "  - You are called for one turn only \u2014 do not say you will wait.\n"
             "  - For repeated / delayed work, use scheduler tools.\n";
        if (has("schedule_at"))    s << "  \u2022 exact time \u2192 **schedule_at**\n";
        if (has("schedule_in"))    s << "  \u2022 delay \u2192 **schedule_in**\n";
        if (has("schedule_every")) s << "  \u2022 interval / monitor \u2192 **schedule_every**\n";
        s << "  - Store durable state through memory tools, not through chat wording.\n";
    }

    // -- MCP note (only when mcp_* tools are present) ----------------------
    {
        bool any_mcp = false;
        for (const auto& nm : enabled_tools) {
            if (nm.size() >= 4 && nm.compare(0, 4, "mcp_") == 0) { any_mcp = true; break; }
        }
        if (any_mcp)
            s << "\nTools whose names start with `mcp_` are **remote MCP tools** "
                 "(from your profile `mcp.json`); call them like any other function \u2014 "
                 "arguments follow each tool\u2019s schema.\n";
    }
}

// ── System prompt builder ─────────────────────────────────────────────────────

static std::string build_system_prompt_impl(const Turn& turn, const nlohmann::json& tools) {
    const std::string external = load_system_prompt_md_if_present();
    std::ostringstream s;

    // When a custom system-prompt.md exists it replaces the built-in intro.
    // Otherwise the intro is generated inside append_agent_tooling_and_context
    // where the enabled-tool set is known, so it can conditionally name only
    // the tools that are actually active this turn (including MCP tools).
    if (!external.empty())
        s << external << "\n\n---\n\n";

    append_runtime_environment_context(s);
    append_agent_tooling_and_context(s, turn, tools);

    if (!turn.task_id.empty()) {
        if (turn.is_scheduled_tick) {
            // ── Scheduled task context ───────────────────────────────────────
            s << "\nScheduled task context:\n"
              << "  task_id: " << turn.task_id << "\n";
            if (turn.memory_state.is_object() && !turn.memory_state.empty())
                s << "Persistent memory state (JSON):\n" << turn.memory_state.dump(2) << "\n";
            if (turn.recent_events.is_array() && !turn.recent_events.empty())
                s << "Recent task events (newest last):\n" << turn.recent_events.dump(2) << "\n";
            s << "\nFor this scheduled tick:\n"
                 "  - Do the smallest useful unit of work.\n"
                 "  - Use tools for actual capture/read/write actions.\n"
                 "  - Update memory via memory_write after meaningful state changes.\n"
                 "  - Do not ask the user to manually trigger the next tick.\n"
                 "  - Do not say you will wait; the scheduler will call you again.\n";
        } else {
            // ── Session memory context (cross-turn chat persistence) ─────────
            if (turn.recent_events.is_array() && !turn.recent_events.empty()) {
                s << "\n## Conversation History\n"
                     "Past turns in this session (oldest first):\n";
                std::size_t total_chars = 0;
                for (const auto& ev : turn.recent_events) {
                    if (!ev.is_object()) continue;
                    const std::string ts   = ev.value("ts",        std::string{});
                    std::string       user = ev.value("user",      std::string{});
                    std::string       asst = ev.value("assistant", std::string{});

                    std::string tool_line;
                    if (ev.contains("tools") && ev["tools"].is_array()) {
                        for (const auto& tn : ev["tools"]) {
                            if (!tn.is_string()) continue;
                            if (!tool_line.empty()) tool_line += ", ";
                            tool_line += tn.get<std::string>();
                        }
                    }
                    if (user.size() > pm::llm::k_session_memory_user_chars)
                        user = user.substr(0, pm::llm::k_session_memory_user_chars - 1) + "\u2026";
                    if (asst.size() > pm::llm::k_session_memory_asst_chars)
                        asst = asst.substr(0, pm::llm::k_session_memory_asst_chars - 1) + "\u2026";

                    std::string line;
                    if (!ts.empty()) line += "[" + ts + "] ";
                    line += "User: \u201c" + user + "\u201d";
                    if (!tool_line.empty())
                        line += "\n  \u2192 tools: " + tool_line;
                    if (ev.contains("spoken") && ev["spoken"].is_array()) {
                        for (const auto& sp : ev["spoken"])
                            if (sp.is_string())
                                line += "\n  \u2192 spoke: \u201c" + sp.get<std::string>() + "\u201d";
                    }
                    if (ev.contains("media_outputs") && ev["media_outputs"].is_array()) {
                        std::string mline;
                        for (const auto& mp : ev["media_outputs"]) {
                            if (!mp.is_string()) continue;
                            const std::string& p = mp.get_ref<const std::string&>();
                            const auto slash = p.find_last_of("/\\");
                            mline += (mline.empty() ? "" : ", ")
                                   + (slash != std::string::npos ? p.substr(slash + 1) : p);
                        }
                        if (!mline.empty()) line += "\n  \u2192 media: " + mline;
                    }
                    if (!asst.empty())
                        line += "\n  \u2192 Assistant: \u201c" + asst + "\u201d";
                    line += "\n";

                    total_chars += line.size();
                    if (total_chars > pm::llm::k_session_memory_inject_chars) {
                        s << "  [\u2026 earlier turns omitted to fit context \u2026]\n";
                        break;
                    }
                    s << line;
                }
            }
            if (turn.memory_state.is_object() && !turn.memory_state.empty())
                s << "\n## Stored Memory\n"
                     "Facts you have explicitly saved with memory_write:\n"
                  << turn.memory_state.dump(2) << "\n";
            s << "\nUse `memory_write` to persist facts across turns (user name, preferences, "
                 "secrets, notes). Use `memory_read` to retrieve them. Memory persists for the "
                 "lifetime of this chat session.\n";
        }
    }

    if (!turn.system_extra.empty())
        s << "\n" << turn.system_extra << "\n";

    return s.str();
}

} // namespace media::llm::agent::detail

// ── Public entry point ────────────────────────────────────────────────────────

namespace media::llm::agent {

// Primary overload — caller supplies the assembled tools JSON (path + MCP).
std::string build_system_prompt(const Turn& turn, const nlohmann::json& tools) {
    return detail::build_system_prompt_impl(turn, tools);
}

// Backward-compat overload (CLI --dry-run, tests): rebuilds tools from the
// path catalog using the Turn's disabled_path_tools filter.
std::string build_system_prompt(const Turn& turn) {
    nlohmann::json tools = turn.disabled_path_tools.empty()
        ? media::llm::path::tool_catalog_openai()
        : media::llm::path::tool_catalog_openai_excluding(turn.disabled_path_tools);
    return detail::build_system_prompt_impl(turn, tools);
}

} // namespace media::llm::agent
