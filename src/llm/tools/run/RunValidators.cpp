#include "RunValidators.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

namespace media::llm::run {

namespace {

using VR = ValidationResult;
using VT = ValidationTier;

bool icontains(const std::string& haystack, const std::string& needle_lower) {
    const auto it = std::search(
        haystack.begin(), haystack.end(),
        needle_lower.begin(), needle_lower.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    return it != haystack.end();
}

bool imatches(const std::string& text, const std::regex& re) {
    return std::regex_search(text, re, std::regex_constants::match_any);
}

// ── Deny-tier ─────────────────────────────────────────────────────────────────

VR check_download_cradle(const std::string& cmd) {
    static const std::regex re(
        R"((?:curl|wget|Invoke-WebRequest|iwr|Invoke-RestMethod|irm)\b.*\|\s*(?:ba)?sh\b)"
        R"(|(?:iwr|irm|Invoke-WebRequest|Invoke-RestMethod)\b.*\|\s*(?:iex|Invoke-Expression)\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "download-cradle: pipe-to-exec pattern detected"};
    return {};
}

VR check_encoded_command(const std::string& cmd) {
    static const std::regex re(
        R"((?:pwsh|powershell)(?:\.exe)?\b.*\s+-(?:EncodedCommand|Enc)\b)"
        R"(|base64\s.*\|\s*(?:ba)?sh\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "encoded-command: obfuscated shell execution"};
    return {};
}

VR check_nested_shell(const std::string& cmd) {
    static const std::regex re(
        R"(\beval\s+[\"'])"
        R"(|\bbash\s+-c\s+[\"']\$\()"
        R"(|\bsh\s+-c\s+[\"'])"
        R"(|\bcmd\s+/c\s+[\"'])"
        R"(|\b(?:pwsh|powershell)(?:\.exe)?\s+-(?:Command|c)\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "nested-shell: shell-in-shell execution"};
    return {};
}

VR check_privilege_escalation(const std::string& cmd) {
    static const std::regex re(
        R"(\bsudo\b)"
        R"(|\bsu\s+-\b)"
        R"(|\bStart-Process\b.*-Verb\s+RunAs)"
        R"(|\brunas\s+/user\b)"
        R"(|\bdoas\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "privilege-escalation: attempt to gain elevated privileges"};
    return {};
}

VR check_recursive_system_delete(const std::string& cmd) {
    static const std::regex re(
        R"(\brm\s+-r[f]*\s+[/~](?:\s|$))"
        R"(|\brm\s+-[f]*r[f]*\s+[/~](?:\s|$))"
        R"(|\bRemove-Item\s.*-Recurse.*(?:C:\\Windows|C:\\Program|/etc|/usr|/var|/bin)\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "recursive-system-delete: refusing to delete system paths"};
    return {};
}

VR check_registry_write(const std::string& cmd) {
    static const std::regex re(
        R"(\breg\s+(?:add|delete)\b)"
        R"(|\bregedit\s+/s\b)"
        R"(|\bSet-ItemProperty\s+HK[A-Z]{1,3}:)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "registry-write: Windows registry modification"};
    return {};
}

VR check_com_object_exec(const std::string& cmd) {
    static const std::regex re(
        R"(\bNew-Object\s+-ComObject\b)"
        R"(|\bWScript\.Shell\b)"
        R"(|\bShell\.Application\b)"
        R"(|\bMMC20\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "com-object: COM-based code execution"};
    return {};
}

VR check_wmi_spawn(const std::string& cmd) {
    static const std::regex re(
        R"(\bInvoke-WmiMethod\b)"
        R"(|\bInvoke-CimMethod\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "wmi-spawn: WMI/CIM process creation"};
    return {};
}

VR check_add_type(const std::string& cmd) {
    if (icontains(cmd, "add-type"))
        return {VT::Deny, "add-type: runtime .NET compilation"};
    return {};
}

VR check_scheduled_task_create(const std::string& cmd) {
    static const std::regex re(
        R"(\bRegister-ScheduledTask\b)"
        R"(|\bschtasks\s+/create\b)"
        R"(|\bcrontab\s+-)"
        R"(|\bat\s+\d)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "scheduled-task: task/cron creation"};
    return {};
}

VR check_module_install(const std::string& cmd) {
    static const std::regex re(
        R"(\bInstall-Module\b)"
        R"(|\bSave-Module\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "module-install: PowerShell module installation"};
    return {};
}

VR check_dynamic_invoke(const std::string& cmd) {
    static const std::regex re(
        R"(\bInvoke-Expression\b)"
        R"(|\biex\s)"
        R"(|\biex$)"
        R"(|&\s*\(\s*\$)"
        R"(|&\s*\(\s*['"])",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "dynamic-invoke: dynamic code execution"};
    return {};
}

VR check_download_utility(const std::string& cmd) {
    static const std::regex re(
        R"(\bStart-BitsTransfer\b)"
        R"(|\bcertutil\s+-urlcache\b)"
        R"(|\bbitsadmin\s+/transfer\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Deny, "download-utility: file download via system utility"};
    return {};
}

// ── Warn-tier ─────────────────────────────────────────────────────────────────

VR check_network_access(const std::string& cmd) {
    static const std::regex re(
        R"(\bcurl\b)"
        R"(|\bwget\b)"
        R"(|\bInvoke-WebRequest\b)"
        R"(|\biwr\b)"
        R"(|\bInvoke-RestMethod\b)"
        R"(|\birm\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Warn, "network-access: command may access the network"};
    return {};
}

VR check_shell_script_execution(const std::string& cmd) {
    static const std::regex re(
        R"(\./[^\s]+\.(?:sh|bash|ps1)\b)"
        R"(|\bbash\s+[^\s-])"
        R"(|\bInvoke-Command\s+-FilePath\b)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Warn, "shell-script: executing an external script file"};
    return {};
}

VR check_env_var_write(const std::string& cmd) {
    static const std::regex re(
        R"(\bexport\s+\w+=)"
        R"(|\$env:\w+\s*=)"
        R"(|\bSet-Item\s+env:)",
        std::regex::icase | std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Warn, "env-var-write: modifying environment variables"};
    return {};
}

VR check_member_invocation(const std::string& cmd) {
    static const std::regex re(
        R"(\.\w+\()"
        R"(|\[\w+\]::\w+\()",
        std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Warn, "member-invocation: .NET method or member call"};
    return {};
}

VR check_sub_expression(const std::string& cmd) {
    static const std::regex re(R"(\$\()", std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Warn, "sub-expression: $(…) may execute arbitrary code"};
    return {};
}

VR check_splatting(const std::string& cmd) {
    static const std::regex re(R"(@\w+\b)", std::regex::optimize);
    if (imatches(cmd, re))
        return {VT::Warn, "splatting: @variable splatting may hide arguments"};
    return {};
}

using ValidatorFn = VR (*)(const std::string&);

static const ValidatorFn k_validators[] = {
    check_download_cradle,
    check_encoded_command,
    check_nested_shell,
    check_privilege_escalation,
    check_recursive_system_delete,
    check_registry_write,
    check_com_object_exec,
    check_wmi_spawn,
    check_add_type,
    check_scheduled_task_create,
    check_module_install,
    check_dynamic_invoke,
    check_download_utility,
    check_network_access,
    check_shell_script_execution,
    check_env_var_write,
    check_member_invocation,
    check_sub_expression,
    check_splatting,
};

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Public API — validate_command
// ═══════════════════════════════════════════════════════════════════════════════

ValidationResult validate_command(const std::string& command,
                                  const std::string& /*shell_hint*/) {
    if (command.empty())
        return {ValidationTier::Deny, "run: command is required and non-empty"};

    for (const auto& fn : k_validators) {
        auto r = fn(command);
        if (r.tier != ValidationTier::Allow)
            return r;
    }
    return {ValidationTier::Allow, {}};
}

} // namespace media::llm::run
