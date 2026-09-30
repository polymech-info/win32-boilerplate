#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_handlers.hpp"
#include "pm_image_settings.hpp"

int pm_image_dispatch_remaining_commands(CLI::App& app, PmImageCliState& st) {
    if (st.settings_cmd && st.settings_cmd->parsed()) return pm_image_cmd_settings(app, st);
    if (st.search_cmd && st.search_cmd->parsed()) return pm_image_cmd_search(app, st);
    if (st.find_cmd && st.find_cmd->parsed()) return pm_image_cmd_find(app, st);
    if (st.dup_cmd && st.dup_cmd->parsed()) return pm_image_cmd_dup(app, st);
    if (st.meta_cmd && st.meta_cmd->parsed()) return pm_image_cmd_meta(app, st);
    if (st.transform_cmd && st.transform_cmd->parsed()) return pm_image_cmd_transform(app, st);
    if (st.create_cmd && st.create_cmd->parsed()) return pm_image_cmd_create(app, st);
    if (st.compress_cmd && st.compress_cmd->parsed()) return pm_image_cmd_compress(app, st);

#if defined(FEATURE_XBLOX) && FEATURE_XBLOX && defined(FEATURE_COMMAND_XBLOX) && FEATURE_COMMAND_XBLOX
    if (st.xblox_cmd && st.xblox_cmd->parsed()) return pm_image_cmd_xblox(app, st);
#endif
#if FEATURE_DAEMON
    if (st.daemon_cmd && st.daemon_cmd->parsed()) return pm_image_cmd_daemon(app, st);
#endif
#if defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT
    if (st.assistant_cmd && st.assistant_cmd->parsed()) return pm_image_cmd_assistant(app, st);
#endif
#if defined(FEATURE_SERVE) && FEATURE_SERVE
    if (st.serve_cmd && st.serve_cmd->parsed()) return pm_image_cmd_serve(app, st);
#endif
#if defined(FEATURE_IPC) && FEATURE_IPC
    if (st.ipc_cmd && st.ipc_cmd->parsed()) return pm_image_cmd_ipc(app, st);
#endif
    if (st.provider_models_list_cmd && st.provider_models_list_cmd->parsed()) return pm_image_cmd_provider_models(app, st);
    if (st.llm_list_cmd && st.llm_list_cmd->parsed()) return pm_image_cmd_llm_list(app, st);
    if (st.llm_call_cmd && st.llm_call_cmd->parsed()) return pm_image_cmd_llm_call(app, st);
    if (st.llm_info_cmd && st.llm_info_cmd->parsed()) return pm_image_cmd_llm_info(app, st);
    if (st.llm_agent_cmd && st.llm_agent_cmd->parsed()) return pm_image_cmd_llm_agent(app, st);
    if (st.info_cmd && st.info_cmd->parsed()) return pm_image_cmd_info(app, st);

    #if FEATURE_REGISTER_EXPLORER
    if (st.reg_cmd && st.reg_cmd->parsed()) return pm_image_cmd_register_explorer(app, st);
    if (st.startmenu_cmd && st.startmenu_cmd->parsed()) return pm_image_cmd_register_startmenu(app, st);
    if (st.installer_cmd && st.installer_cmd->parsed()) return pm_image_cmd_installer(app, st);
#endif

#if FEATURE_COMMAND_APP
    if (st.app_cmd_grp && st.app_cmd_grp->parsed()) return pm_image_cmd_app(app, st);
#endif

#if FEATURE_COMMAND_STATUS
    if (st.status_cmd && st.status_cmd->parsed()) return pm_image_cmd_status_pixlwiz(app, st);
#endif

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    if (st.login_cmd && st.login_cmd->parsed()) return pm_image_cmd_login(app, st);
    if (st.logout_cmd && st.logout_cmd->parsed()) return pm_image_cmd_logout(app, st);
    if (st.pixlwiz_status_cmd && st.pixlwiz_status_cmd->parsed())
        return pm_image_cmd_status_pixlwiz(app, st);
#endif
#if defined(FEATURE_STT) && FEATURE_STT && defined(FEATURE_COMMAND_AUDIO) && FEATURE_COMMAND_AUDIO
    if (st.audio_cmd && st.audio_cmd->parsed()) return pm_image_cmd_audio(app, st);
#endif
#if defined(FEATURE_VIDEO) && FEATURE_VIDEO && defined(FEATURE_COMMAND_VIDEO) && FEATURE_COMMAND_VIDEO
    if (st.video_cmd && st.video_cmd->parsed()) return pm_image_cmd_video(app, st);
#endif
    if (st.service_upload_cmd && st.service_upload_cmd->parsed()) return pm_image_cmd_service(app, st);
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    if (st.service_posts_create_cmd && st.service_posts_create_cmd->parsed())
        return pm_image_cmd_service_posts_create(app, st);
#endif
#if FEATURE_COMMAND_BATCH
    if (st.batch_cmd && st.batch_cmd->parsed()) return pm_image_cmd_batch(app, st);
#endif
    return pm_image_cmd_print_help(app, st);
}
