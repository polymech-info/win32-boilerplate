#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace media::llm::skills {

enum class SkillSource {
    Roaming,
    Workspace,
};

struct SkillEntry {
    std::string name;
    std::string description;
    std::filesystem::path skill_md_path;
    SkillSource source = SkillSource::Workspace;
    bool available = true;
    bool always = false;
    bool pinned = false;
    bool disabled = false;
    bool active = false;
    std::vector<std::string> missing_requirements;
};

struct SkillPolicy {
    bool enabled = true;
    bool roaming_enabled = true;
    bool workspace_enabled = true;
    std::vector<std::string> pinned;
    std::vector<std::string> disabled;
};

struct SkillSnapshot {
    std::filesystem::path roaming_root;
    std::filesystem::path workspace_root;
    SkillPolicy policy;
    std::vector<SkillEntry> entries;
};

std::filesystem::path default_roaming_skills_root();
std::filesystem::path default_workspace_skills_root(const std::string& cwd_hint);
SkillSnapshot discover_skills(const SkillPolicy& policy, const std::string& cwd_hint);

} // namespace media::llm::skills

