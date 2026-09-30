#pragma once

struct PmImageCliState;
namespace CLI {
class App;
}

int pm_image_cmd_service(CLI::App& app, PmImageCliState& st);

int pm_image_cmd_service_posts_create(CLI::App& app, PmImageCliState& st);

void pm_image_register_service(CLI::App& app, PmImageCliState& s);
