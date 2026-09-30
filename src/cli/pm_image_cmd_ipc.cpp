#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_ipc.hpp"

int pm_image_cmd_ipc(CLI::App& app, PmImageCliState& st) {
    media::CacheServerDefaults cd;
    cd.enabled = !st.ipc_no_cache;
    cd.cache_dir = st.ipc_cache_dir;
#if defined(_WIN32)
    if (!st.ipc_unix.empty()) {
        std::cerr << "media-img: --unix is not supported on Windows; use --st.host and --st.port.\n";
        return 1;
    }
    return media::ipc::run_tcp_server(st.ipc_host, st.ipc_port, cd);
#else
    if (!st.ipc_unix.empty()) {
        return media::ipc::run_unix_server(st.ipc_unix, cd);
    }
    return media::ipc::run_tcp_server(st.ipc_host, st.ipc_port, cd);
#endif
}

void pm_image_register_ipc(CLI::App& app, PmImageCliState& s) {
    s.ipc_cmd = app.add_subcommand("ipc", "Run JSON-line IPC server (TCP; Unix socket on non-Windows)");
    s.ipc_cmd->add_option("--host", s.ipc_host, "Bind address")->default_val("127.0.0.1");
    s.ipc_cmd->add_option("-p,--port", s.ipc_port, "TCP port")->default_val(9333);
    s.ipc_cmd->add_option("--unix", s.ipc_unix, "Unix domain socket path (not Windows)");
    s.ipc_cmd->add_flag("--no-cache", s.ipc_no_cache,
                      "Server default: cache off when JSON omits \"cache\" (JSON may still enable per request)");
    s.ipc_cmd->add_option("--cache-dir", s.ipc_cache_dir, "Server default cache dir when JSON omits \"cache_dir\"");
}
