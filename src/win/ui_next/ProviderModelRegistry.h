#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace pmui {
namespace provider_models {

struct ModelOption {
    std::string id;
    std::string label;
};

struct ProviderOption {
    std::string id;
    std::string label;
    bool supports_base_url = false;
};

const std::vector<ProviderOption>& providers();

/// Same model list as @ref populate_model_combo but without an HWND (for web JSON bridge).
bool list_models_for_provider(const std::string&                provider_id,
                              const std::string&                api_key,
                              const std::string&                base_url,
                              std::vector<ModelOption>&         out,
                              std::string&                      err_out);

void populate_provider_combo(HWND combo, const std::string& selected_provider_id);
std::string selected_provider_id(HWND combo);

bool populate_model_combo(HWND combo,
                          const std::string& provider_id,
                          const std::string& api_key,
                          const std::string& base_url,
                          const std::string& preferred_model_id,
                          std::string& err_out);

std::string combo_text(HWND combo);

} // namespace provider_models
} // namespace pmui

