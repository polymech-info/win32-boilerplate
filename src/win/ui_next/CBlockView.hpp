#pragma once

#include "win/web/CWebView.h"
#include "win/web/CWebViewManager.h"

#include <atomic>
#include <string>
#include <vector>

namespace pmui {

class CBlockView : public CWebView {
public:
    CBlockView();
    ~CBlockView() override;

    bool OpenXbloxFile(std::wstring xbloxPath, CWebViewManager* manager);
    void SetBusManager(CWebViewManager* manager);
    void SetContext(const std::vector<std::wstring>& selection, const std::wstring& folder);
    const std::wstring& CurrentFolder() const noexcept { return m_folder; }
    const std::vector<std::wstring>& Selection() const noexcept { return m_selection; }
    unsigned long long BeginXbloxRun();
    void FinishXbloxRun(unsigned long long generation);
    bool XbloxStopRequested() const noexcept { return m_xbloxStopRequested.load(std::memory_order_acquire); }
    void RequestStopXbloxRun();

private:
    CBlockView(const CBlockView&) = delete;
    CBlockView& operator=(const CBlockView&) = delete;

    void HandleHostMessage(const std::string& json_utf8);
    void PostInit();

    std::wstring m_path;
    std::vector<std::wstring> m_selection;
    std::wstring m_folder;
    CWebViewManager* m_busManager = nullptr;
    std::atomic<bool> m_xbloxStopRequested{false};
    std::atomic<unsigned long long> m_xbloxRunGeneration{0};
};

} // namespace pmui
