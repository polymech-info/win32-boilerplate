// Shared includes for `pm_image_dispatch.cpp` and subcommand handler translation units.
#ifndef PM_IMAGE_CMD_INCLUDES_HPP
#define PM_IMAGE_CMD_INCLUDES_HPP

#include "pm_image_cli_state.hpp"
#include "pm_image_dispatch.hpp"

#include <nlohmann/json.hpp>
#include <CLI/CLI.hpp>

#include "logger/logger.h"
#include "core/batch_queue.hpp"
#include "core/cli_cancel.hpp"
#include "core/compress.hpp"
#include "core/duplicates.hpp"
#include "core/find.hpp"
#include "core/search.hpp"
#include "core/input_selection.hpp"
#include "core/meta.hpp"
#include "core/app_image_provider.hpp"
#include "core/openrouter_provider_models_cli.hpp"
#include "core/openai_models_cli.hpp"
#include "core/replicate_provider_models_cli.hpp"
#include "core/pixlwiz_provider_models_cli.hpp"
#include "core/settings_portable.hpp"
#include "core/settings_runtime.hpp"
#include "core/output_path.hpp"
#include "core/resize.hpp"
#include "core/transform.hpp"
#include "http/serve.hpp"
#include "ipc/ipc_serve.hpp"
#include "llm/agent.hpp"
#include "llm/path_tool_catalog.hpp"
#include "llm/tool_catalog.hpp"
#include "llm/tool_executor.hpp"
#include "constants.hpp"

#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#if defined(_WIN32)
#include <cstdio>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#pragma comment(lib, "Shell32.lib")
#include "core/glob_paths.hpp"
#if !defined(PM_IMAGE_CLI_ONLY)
#include "win/register_explorer.hpp"
#include "win/explorer_job_ui.hpp"
#include "win/resize_ui.hpp"
#endif
#include "win/settings_store.hpp"
#if !defined(PM_IMAGE_CLI_ONLY)
#include "win/ui_next/helpers/text_conv.hpp"
#endif
#if defined(FEATURE_TRIAL_CHECK)
#include "win/trial_protection.hpp"
#endif
#if defined(FEATURE_LICENSE_FILE)
#include "win/license_file.hpp"
#include "win/machine_fingerprint.hpp"
#endif
#if !defined(PM_IMAGE_CLI_ONLY)
#include "win/ui_singleton.hpp"
#include "win/ui_next/launch_ui_next.h"
#include "win/ui_next/helpers/splash_window.hpp"
#include "win/ui_next/ui_log_file.hpp"
class CMainFrame;
#endif
#endif

#include "pm_image_cli_helpers.hpp"

#endif
