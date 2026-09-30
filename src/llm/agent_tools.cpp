#include "agent_tools.hpp"

#include <cctype>
#include <cstring>

namespace pm::llm {

// ── Canonical tool registry ───────────────────────────────────────────────────
//
// ORDER must match media::llm::path::tool_catalog() so that flag-based
// filtering (agent_tools_disabled_names) produces a deterministic catalog.
// When adding a new AgentTool enumerator:
//   1. Add a bit in constants.hpp  (AgentTool enum)
//   2. Add a row here              (agent_tools.cpp)
//   3. Add a make_*() in          path_tool_catalog.cpp
//   4. Append to k_agent_tools_all in constants.hpp
//
static constexpr AgentToolInfo k_registry[] = {
    // ── File / folder ─────────────────────────────────────────────────────
    { AgentTool::ListImages,        "list_images",
      "List image files in a folder or matching a glob pattern.",
      AgentToolGroup::File },

    { AgentTool::FileGlob,          "file_glob",
      "Discover files matching a glob / recursive pattern.",
      AgentToolGroup::File },

    { AgentTool::FileRead,          "file_read",
      "Read a UTF-8 text file from disk (logs, configs, source, CSV, DXF, SVG, …).",
      AgentToolGroup::File },

    { AgentTool::FileSearch,        "file_search",
      "Full-text search across files in a folder.",
      AgentToolGroup::File },

    // ── Image processing ──────────────────────────────────────────────────
    { AgentTool::ImageResize,       "image_resize",
      "Resize / re-encode images on disk (libvips).",
      AgentToolGroup::Image },

    { AgentTool::ImageTransform,    "image_transform",
      "AI image edit on disk — enhance, restyle, retouch (Gemini / Replicate).",
      AgentToolGroup::Image },

    { AgentTool::ImageCreate,       "image_create",
      "AI text-to-image generation on disk (Gemini / Replicate).",
      AgentToolGroup::Image },

    { AgentTool::CreateVideo,       "create_video",
      "Generate or convert video on disk (Replicate).",
      AgentToolGroup::Image },

    { AgentTool::ImageUnderstand,   "image_understand",
      "Visual question-answering on image files (Gemini).",
      AgentToolGroup::Image },

    { AgentTool::ImageFromCamera,   "image_from_camera",
      "Capture a frame from the webcam.",
      AgentToolGroup::Image },

    // ── Utility ───────────────────────────────────────────────────────────
    { AgentTool::WriteFile,         "write_file",
      "Write or overwrite a UTF-8 text file on disk.",
      AgentToolGroup::Utility },

    { AgentTool::Speak,             "speak",
      "Synthesize and play text-to-speech audio on the default speaker.",
      AgentToolGroup::Utility },

    // ── Scheduler ─────────────────────────────────────────────────────────
    { AgentTool::ScheduleAt,        "schedule_at",
      "Schedule a task to run once at a specific date / time.",
      AgentToolGroup::Scheduler },

    { AgentTool::ScheduleIn,        "schedule_in",
      "Schedule a task to run once after a delay.",
      AgentToolGroup::Scheduler },

    { AgentTool::ScheduleEvery,     "schedule_every",
      "Schedule a task to run on a repeating interval.",
      AgentToolGroup::Scheduler },

    { AgentTool::ScheduleCancel,    "schedule_cancel",
      "Cancel a scheduled task by ID.",
      AgentToolGroup::Scheduler },

    { AgentTool::ScheduleList,      "schedule_list",
      "List all current scheduled tasks.",
      AgentToolGroup::Scheduler },

    // ── Memory ────────────────────────────────────────────────────────────
    { AgentTool::MemoryRead,        "memory_read",
      "Read persistent session memory (facts, notes, preferences).",
      AgentToolGroup::Memory },

    { AgentTool::MemoryWrite,       "memory_write",
      "Write or update persistent session memory.",
      AgentToolGroup::Memory },

    { AgentTool::MemoryAppendEvent, "memory_append_event",
      "Append a structured event record to the session log.",
      AgentToolGroup::Memory },

    // ── Shell execution ───────────────────────────────────────────────────
    { AgentTool::Run,               "run",
      "Execute a shell command and return stdout/stderr.",
      AgentToolGroup::Utility },

    // ── Computer use / app inspection ─────────────────────────────────────
    { AgentTool::AppInspectDump,    "app_inspect_dump",
      "Inspect a desktop app and return a compact element tree.",
      AgentToolGroup::Computer },

    { AgentTool::AppInspectFind,    "app_inspect_find",
      "Find targetable controls/elements in a desktop app.",
      AgentToolGroup::Computer },

    { AgentTool::AppScreenshot,     "app_screenshot",
      "Capture a desktop app window, element, or rectangle to an image file.",
      AgentToolGroup::Computer },

    { AgentTool::AppClick,          "app_click",
      "Click a desktop coordinate or found app element.",
      AgentToolGroup::Computer },

    { AgentTool::AppDrag,           "app_drag",
      "Press, move along a straight line, release (drawing, selection, drag-and-drop).",
      AgentToolGroup::Computer },

    { AgentTool::AppOpen,           "app_open",
      "Launch a desktop application and optionally place its window.",
      AgentToolGroup::Computer },

    { AgentTool::AppType,           "app_type",
      "Type text into the focused (or activated) desktop window.",
      AgentToolGroup::Computer },

    { AgentTool::AppHotkey,         "app_hotkey",
      "Send a hotkey combination (e.g. ctrl+s, alt+f4) to the focused window.",
      AgentToolGroup::Computer },

    { AgentTool::AppClose,          "app_close",
      "Close a desktop window or terminate its process (force).",
      AgentToolGroup::Computer },

    { AgentTool::AppBatch,          "app_batch",
      "Execute a sequence of UI actions in ONE tool call (saves round-trips).",
      AgentToolGroup::Computer },
};

static constexpr std::size_t k_registry_size =
    sizeof(k_registry) / sizeof(k_registry[0]);

// ── Public API ────────────────────────────────────────────────────────────────

std::size_t agent_tool_registry_size() noexcept { return k_registry_size; }

const AgentToolInfo* agent_tool_registry_data() noexcept { return k_registry; }

const AgentToolInfo* agent_tool_find(AgentTool flag) noexcept
{
    if (flag == AgentTool::None) return nullptr;
    for (const auto& e : k_registry)
        if (e.flag == flag) return &e;
    return nullptr;
}

const AgentToolInfo* agent_tool_find_by_name(std::string_view name) noexcept
{
    for (const auto& e : k_registry) {
        const char* n = e.name;
        std::size_t i = 0;
        for (; i < name.size() && n[i]; ++i) {
            if (std::tolower(static_cast<unsigned char>(name[i])) !=
                std::tolower(static_cast<unsigned char>(n[i])))
                break;
        }
        if (i == name.size() && n[i] == '\0')
            return &e;
    }
    return nullptr;
}

} // namespace pm::llm
