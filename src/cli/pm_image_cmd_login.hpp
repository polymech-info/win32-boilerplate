#pragma once

struct PmImageCliState;
namespace CLI {
class App;
}

/** ZITADEL OIDC: `login --probe`, `login --decode-jwt`, or interactive PKCE + token save. */
int pm_image_cmd_login(CLI::App& app, PmImageCliState& st);

void pm_image_register_login(CLI::App& app, PmImageCliState& s);
