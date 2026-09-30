#include "stdafx.h"
#include "ProviderModelRegistry.h"
#include "ProviderDlg_Replicate.h"
#include "core/pixlwiz_provider_models_cli.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace pmui {
namespace provider_models {

namespace {

class IProviderSource {
public:
    virtual ~IProviderSource() = default;
    virtual ProviderOption option() const = 0;
    virtual bool list_models(const std::string& api_key,
                             const std::string& base_url,
                             std::vector<ModelOption>& out,
                             std::string& err_out) const = 0;
};

class GeminiProviderSource final : public IProviderSource {
public:
    ProviderOption option() const override { return {"google", "Google / Gemini", true}; }
    bool list_models(const std::string&, const std::string&, std::vector<ModelOption>& out, std::string& err_out) const override {
        out = {
            {"gemini-3-pro-image-preview", "gemini-3-pro-image-preview"},
            {"gemini-2.5-pro", "gemini-2.5-pro"},
            {"gemini-2.5-flash", "gemini-2.5-flash"},
        };
        err_out.clear();
        return true;
    }
};

class OpenAIProviderSource final : public IProviderSource {
public:
    ProviderOption option() const override { return {"openai", "OpenAI", true}; }
    bool list_models(const std::string&, const std::string&, std::vector<ModelOption>& out, std::string& err_out) const override {
        out = {
            {"gpt-image-1", "gpt-image-1"},
            {"gpt-4.1-mini", "gpt-4.1-mini"},
            {"gpt-4o-mini", "gpt-4o-mini"},
        };
        err_out.clear();
        return true;
    }
};

class ReplicateProviderSource final : public IProviderSource {
public:
    ProviderOption option() const override { return {"replicate", "Replicate", true}; }
    bool list_models(const std::string& api_key,
                     const std::string& base_url,
                     std::vector<ModelOption>& out,
                     std::string& err_out) const override {
        std::vector<pmui::provider_dlg_replicate::ModelInfo> models;
        if (!pmui::provider_dlg_replicate::fetch_collection_models(
                api_key, base_url, "official", models, err_out, false)) {
            return false;
        }
        pmui::provider_dlg_replicate::sort_models_alpha(models);
        out.clear();
        out.reserve(models.size());
        for (const auto& m : models) out.push_back({m.slug, m.slug});
        return true;
    }
};

class PixlWizProviderSource final : public IProviderSource {
public:
    ProviderOption option() const override { return {"pixlwiz", "PixlWiz", true}; }
    bool list_models(const std::string& api_key,
                     const std::string& base_url,
                     std::vector<ModelOption>& out,
                     std::string& err_out) const override {
        std::vector<media::pixlwiz_cli::PixlWizModelRow> rows;
        if (!media::pixlwiz_cli::list_pixlwiz_catalog_models(api_key, base_url, rows, err_out, false))
            return false;
        out.clear();
        out.reserve(rows.size());
        for (const auto& r : rows) out.push_back({r.id, r.name.empty() ? r.id : r.name});
        std::sort(out.begin(), out.end(), [](const ModelOption& a, const ModelOption& b) {
            return a.label < b.label;
        });
        return true;
    }
};

static const std::vector<std::unique_ptr<IProviderSource>>& sources() {
    static const std::vector<std::unique_ptr<IProviderSource>> v = [] {
        std::vector<std::unique_ptr<IProviderSource>> s;
        s.emplace_back(std::make_unique<GeminiProviderSource>());
        s.emplace_back(std::make_unique<OpenAIProviderSource>());
        s.emplace_back(std::make_unique<ReplicateProviderSource>());
        s.emplace_back(std::make_unique<PixlWizProviderSource>());
        return s;
    }();
    return v;
}

static const IProviderSource* source_for(const std::string& id) {
    for (const auto& s : sources()) if (s->option().id == id) return s.get();
    return nullptr;
}

} // namespace

const std::vector<ProviderOption>& providers() {
    static const std::vector<ProviderOption> p = [] {
        std::vector<ProviderOption> out;
        for (const auto& s : sources()) out.push_back(s->option());
        std::sort(out.begin(), out.end(), [](const ProviderOption& a, const ProviderOption& b) {
            return a.label < b.label;
        });
        return out;
    }();
    return p;
}

bool list_models_for_provider(const std::string&         provider_id,
                              const std::string&         api_key,
                              const std::string&         base_url,
                              std::vector<ModelOption>&  out,
                              std::string&                 err_out) {
    out.clear();
    const IProviderSource* src = source_for(provider_id);
    if (!src) {
        err_out = "unsupported provider: " + provider_id;
        return false;
    }
    return src->list_models(api_key, base_url, out, err_out);
}

void populate_provider_combo(HWND combo, const std::string& selected_provider_id) {
    if (!combo) return;
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int set_idx = 0;
    int i = 0;
    for (const auto& p : providers()) {
        const std::wstring w = utf8_to_wide(p.label);
        ::SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)w.c_str());
        if (p.id == selected_provider_id) set_idx = i;
        ++i;
    }
    ::SendMessageW(combo, CB_SETCURSEL, set_idx, 0);
}

std::string selected_provider_id(HWND combo) {
    if (!combo) return "google";
    const int sel = (int)::SendMessageW(combo, CB_GETCURSEL, 0, 0);
    const auto& p = providers();
    if (sel >= 0 && sel < (int)p.size()) return p[(size_t)sel].id;
    return "google";
}

bool populate_model_combo(HWND combo,
                          const std::string& provider_id,
                          const std::string& api_key,
                          const std::string& base_url,
                          const std::string& preferred_model_id,
                          std::string& err_out) {
    if (!combo) return false;
    const IProviderSource* src = source_for(provider_id);
    if (!src) {
        err_out = "unsupported provider: " + provider_id;
        return false;
    }
    std::vector<ModelOption> models;
    if (!src->list_models(api_key, base_url, models, err_out)) return false;
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& m : models) {
        ::SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)utf8_to_wide(m.label).c_str());
    }
    if (!preferred_model_id.empty()) {
        const std::wstring pref = utf8_to_wide(preferred_model_id);
        if (::SendMessageW(combo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)pref.c_str()) == CB_ERR) {
            ::SetWindowTextW(combo, pref.c_str());
        }
    } else if (!models.empty()) {
        ::SetWindowTextW(combo, utf8_to_wide(models.front().label).c_str());
    }
    return true;
}

std::string combo_text(HWND combo) {
    wchar_t b[2048]{};
    if (combo) ::GetWindowTextW(combo, b, (int)std::size(b));
    return wide_to_utf8(b);
}

} // namespace provider_models
} // namespace pmui

