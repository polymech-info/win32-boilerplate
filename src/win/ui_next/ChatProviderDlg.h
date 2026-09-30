#ifndef PM_UI_CHATPROVIDERDLG_H
#define PM_UI_CHATPROVIDERDLG_H

#include <Windows.h>

/**
 * Show the modal "Chat Provider Settings" dialog.
 * Lets the user pick a router (openrouter / openai / anthropic / gemini /
 * ollama / deepseek / fireworks / xai / huggingface / custom) and the text
 * model, max iterations, and default image tool provider/model. API keys and
 * service base URLs are managed in the API Providers (Settings) dialog, not here.
 *
 * Router, model, image tool defaults, etc. are persisted to `settings.json["chat"]` via
 * `media::settings::save_chat_provider` (API keys are not written there — they live under
 * `settings.json["providers"]`).
 *
 * Returns true if the user pressed Save (OK), false if they cancelled.
 */
bool ShowChatProviderSettingsDlg(HWND parent);

#endif // PM_UI_CHATPROVIDERDLG_H
