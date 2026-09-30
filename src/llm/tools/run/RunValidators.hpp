#pragma once
//
// media::llm::run — command validation for the Run Tool.
//
// Three-tier model: Deny blocks execution, Warn logs but allows, Allow is clean.
// Validators are regex-based pattern checkers applied in order; the first
// non-Allow result wins.  New validators should be added to k_validators[]
// in RunValidators.cpp.
//
#include "polymech_export.h"

#include <string>

namespace media::llm::run {

enum class ValidationTier { Allow, Warn, Deny };

struct ValidationResult {
    ValidationTier tier = ValidationTier::Allow;
    std::string    reason;
};

/// Run the full validator chain against a command string.
/// Returns the first non-Allow result, or Allow if all pass.
POLYMECH_API ValidationResult validate_command(const std::string& command,
                                               const std::string& shell_hint);

} // namespace media::llm::run
