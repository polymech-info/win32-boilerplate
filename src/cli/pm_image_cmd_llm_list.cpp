#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_llm_list.hpp"

int pm_image_cmd_llm_list(CLI::App& app, PmImageCliState& st) {
    (void)app;
    if (st.llm_tools_list_path)
        std::cout << media::llm::path::tool_catalog_json().dump(2) << "\n";
    else
        std::cout << media::llm::tool_catalog_json().dump(2) << "\n";
    return 0;
}
