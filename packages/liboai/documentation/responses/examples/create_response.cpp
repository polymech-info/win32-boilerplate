#include "liboai.h"

using namespace liboai;

int main() {
  OpenAI oai;

  if (oai.auth.SetKeyEnv("OPENAI_API_KEY")) {
    try {
      nlohmann::json request;
      request["model"] = "gpt-5.2-pro";
      request["input"] = "Hello from the Responses API.";

      Response response = oai.Responses->create(request);

      std::string output_text;
      if (response.raw_json.contains("output")) {
        for (const auto& item : response["output"]) {
          if (item.contains("type") && item["type"] == "message") {
            if (item.contains("content") && item["content"].is_array()) {
              for (const auto& content : item["content"]) {
                if (content.contains("type") && content["type"] == "output_text" && content.contains("text")) {
                  output_text = content["text"].get<std::string>();
                  break;
                }
              }
            }
          }

          if (!output_text.empty()) {
            break;
          }
        }
      }

      if (!output_text.empty()) {
        std::cout << output_text << std::endl;
      }
      else {
        std::cout << response << std::endl;
      }
    }
    catch (std::exception& e) {
      std::cout << e.what() << std::endl;
    }
  }
}
