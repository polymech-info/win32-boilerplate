#include "pm_image_cmd_includes.hpp"

#include "lib/pm_zitadel_oauth.hpp"
#include "pm_image_cmd_logout.hpp"

#include <filesystem>
#include <iostream>

int pm_image_cmd_logout(CLI::App& /*app*/, PmImageCliState& /*st*/)
{
    namespace fs = std::filesystem;

    std::string        perr;
    const fs::path     p = pm_zitadel_oauth_json_path(perr);
    std::error_code    xec;
    const bool         had_token_file = !p.empty() && fs::is_regular_file(p, xec);

    std::string err;
    if (!pm_zitadel_oauth_clear(err)) {
        std::cerr << "logout: " << err << "\n";
        return 1;
    }
    if (had_token_file)
        std::cout << "logout: removed zitadel-oauth.json (signed out).\n";
    else
        std::cout << "logout: no zitadel-oauth.json (already signed out).\n";
    return 0;
}
