#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_serve.hpp"

int pm_image_cmd_serve(CLI::App& app, PmImageCliState& st) {
    media::CacheServerDefaults cd;
    cd.enabled = !st.serve_no_cache;
    cd.cache_dir = st.serve_cache_dir;
    return media::http::run_server(st.host, st.port, cd);
}

void pm_image_register_serve(CLI::App& app, PmImageCliState& s) {
    s.serve_cmd = app.add_subcommand("serve", "Run HTTP REST server");
    s.serve_cmd->add_option("--host", s.host, "Bind address")->default_val("127.0.0.1");
    s.serve_cmd->add_option("-p,--port", s.port, "TCP port")->default_val(8080);
    s.serve_cmd->add_flag("--no-cache", s.serve_no_cache,
                         "Server default: cache off when JSON omits \"cache\" (JSON may still enable per request)");
    s.serve_cmd->add_option("--cache-dir", s.serve_cache_dir, "Server default cache dir when JSON omits \"cache_dir\"");
}
