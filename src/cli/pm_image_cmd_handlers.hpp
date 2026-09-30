// Subcommand handler declarations for `pm_image_dispatch.cpp`.
#ifndef PM_IMAGE_CMD_HANDLERS_HPP
#define PM_IMAGE_CMD_HANDLERS_HPP

#include "pm_image_cmd_resize.hpp"
#include "pm_image_cmd_find.hpp"
#include "pm_image_cmd_search.hpp"
#include "pm_image_cmd_dup.hpp"
#include "pm_image_cmd_meta.hpp"
#include "pm_image_cmd_transform.hpp"
#include "pm_image_cmd_create.hpp"
#include "pm_image_cmd_compress.hpp"

#if defined(FEATURE_SERVE) && FEATURE_SERVE
#include "pm_image_cmd_serve.hpp"
#endif

#if defined(FEATURE_IPC) && FEATURE_IPC
#include "pm_image_cmd_ipc.hpp"
#endif

#include "pm_image_cmd_provider_models.hpp"
#include "pm_image_cmd_llm_list.hpp"
#include "pm_image_cmd_llm_call.hpp"
#include "pm_image_cmd_llm_agent.hpp"
#include "pm_image_cmd_llm_info.hpp"
#include "pm_image_cmd_info.hpp"
#include "pm_image_cmd_register_explorer.hpp"
#include "pm_image_cmd_register_startmenu.hpp"
#include "pm_image_cmd_installer.hpp"
#include "pm_image_cmd_app.hpp"
#include "pm_image_cmd_batch.hpp"

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
#include "pm_image_cmd_login.hpp"
#include "pm_image_cmd_logout.hpp"
#endif
#include "pm_image_cmd_status.hpp"

#include "pm_image_cmd_service.hpp"
#include "pm_image_cmd_print_help.hpp"

#if defined(FEATURE_XBLOX) && FEATURE_XBLOX && defined(FEATURE_COMMAND_XBLOX) && FEATURE_COMMAND_XBLOX
#include "pm_image_cmd_xblox.hpp"
#endif
#include "pm_image_cmd_daemon.hpp"

#if defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT
#include "pm_image_cmd_assistant.hpp"
#endif

#if defined(FEATURE_STT) && FEATURE_STT && defined(FEATURE_COMMAND_AUDIO) && FEATURE_COMMAND_AUDIO
#include "pm_image_cmd_audio.hpp"
#endif

#if defined(FEATURE_VIDEO) && FEATURE_VIDEO && defined(FEATURE_COMMAND_VIDEO) && FEATURE_COMMAND_VIDEO
#include "pm_image_cmd_video.hpp"
#endif

int pm_image_dispatch_remaining_commands(CLI::App& app, PmImageCliState& st);

#endif
