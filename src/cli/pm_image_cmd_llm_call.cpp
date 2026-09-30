#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_llm_call.hpp"

int pm_image_cmd_llm_call(CLI::App& app, PmImageCliState& st) {
    // Local base64 encoder (small enough to inline; mirrors the one in
    // src/llm/tool_executor.cpp so we don't pull a new lib dep here).
    auto b64_encode = [](const unsigned char *data, std::size_t len) {
        static const char tbl[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (std::size_t i = 0; i < len; i += 3) {
            std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
            if (i + 1 < len) n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
            if (i + 2 < len) n |= static_cast<std::uint32_t>(data[i + 2]);
            out.push_back(tbl[(n >> 18) & 0x3F]);
            out.push_back(tbl[(n >> 12) & 0x3F]);
            out.push_back((i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=');
            out.push_back((i + 2 < len) ? tbl[n & 0x3F]        : '=');
        }
        return out;
    };

    nlohmann::json args = nlohmann::json::object();
    if (!st.llm_call_args_path.empty()) {
        std::string text;
        if (st.llm_call_args_path == "-" || st.llm_call_args_path == "@-") {
            std::ostringstream oss; oss << std::cin.rdbuf(); text = oss.str();
        } else {
            std::string p = st.llm_call_args_path;
            if (!p.empty() && p[0] == '@') p = p.substr(1);
            std::ifstream ifs(p, std::ios::binary);
            if (!ifs) {
                std::cerr << "llm tools-call: cannot open --args file: " << p << "\n";
                return 1;
            }
            std::ostringstream oss; oss << ifs.rdbuf(); text = oss.str();
        }
        try {
            args = nlohmann::json::parse(text.empty() ? "{}" : text);
        } catch (const std::exception &e) {
            std::cerr << "llm tools-call: invalid JSON in --args: " << e.what() << "\n";
            return 1;
        }
        if (!args.is_object()) {
            std::cerr << "llm tools-call: --args must be a JSON object\n";
            return 1;
        }
    }

    if (!st.llm_call_image_file.empty()) {
        std::ifstream ifs(st.llm_call_image_file, std::ios::binary);
        if (!ifs) {
            std::cerr << "llm tools-call: cannot open --image-file: " << st.llm_call_image_file << "\n";
            return 1;
        }
        std::ostringstream oss; oss << ifs.rdbuf();
        const std::string raw = oss.str();
        if (raw.empty()) {
            std::cerr << "llm tools-call: --image-file is empty\n";
            return 1;
        }
        // Guess MIME from extension; default to image/png.
        std::string mime = "image/png";
        const auto dot = st.llm_call_image_file.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = st.llm_call_image_file.substr(dot + 1);
            for (auto &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if      (ext == "jpg" || ext == "jpeg") mime = "image/jpeg";
            else if (ext == "png")                  mime = "image/png";
            else if (ext == "webp")                 mime = "image/webp";
            else if (ext == "gif")                  mime = "image/gif";
        }
        args["image"] = nlohmann::json{
            {"mime", mime},
            {"b64",  b64_encode(reinterpret_cast<const unsigned char *>(raw.data()), raw.size())},
        };
    }

    auto out = media::llm::execute(st.llm_call_name, args);
    std::cout << out.envelope.dump(2) << "\n";
    return out.ok ? 0 : 1;
}
