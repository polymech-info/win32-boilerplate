// cmd_kbot_uds.cpp — UDS/TCP worker for KBot LLM IPC (length-prefixed JSON frames).
// Framing matches orchestrator tests: [uint32_le length][utf-8 JSON object with id, type, payload].

#include "cmd_kbot.h"
#include "concurrentqueue.h"
#include "logger/logger.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <taskflow/taskflow.hpp>
#include <thread>
#include <unordered_map>

namespace polymech {
namespace {

#ifdef _WIN32
using ipc_endpoint = asio::ip::tcp::endpoint;
using ipc_acceptor = asio::ip::tcp::acceptor;
using ipc_socket = asio::ip::tcp::socket;
#else
using ipc_endpoint = asio::local::stream_protocol::endpoint;
using ipc_acceptor = asio::local::stream_protocol::acceptor;
using ipc_socket = asio::local::stream_protocol::socket;
#endif

std::shared_ptr<ipc_socket> g_active_uds_socket;
std::mutex g_uds_socket_mutex;

std::string json_escape_log_line(const std::string &s) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  w.String(s.c_str(), static_cast<rapidjson::SizeType>(s.length()));
  std::string out(buf.GetString(), buf.GetSize());
  if (out.size() >= 2 && out.front() == '"' && out.back() == '"')
    return out.substr(1, out.size() - 2);
  return out;
}

template <typename Mutex>
class kbot_uds_sink : public spdlog::sinks::base_sink<Mutex> {
protected:
  void sink_it_(const spdlog::details::log_msg &msg) override {
    spdlog::memory_buf_t formatted;
    this->formatter_->format(msg, formatted);
    std::string text = fmt::to_string(formatted);
    if (!text.empty() && text.back() == '\n')
      text.pop_back();

    std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
    if (!g_active_uds_socket)
      return;
    try {
      std::string escaped = json_escape_log_line(text);
      std::string frame = "{\"type\":\"log\",\"data\":\"" + escaped + "\"}";
      uint32_t len = static_cast<uint32_t>(frame.size());
      asio::write(*g_active_uds_socket, asio::buffer(&len, 4));
      asio::write(*g_active_uds_socket, asio::buffer(frame));
    } catch (...) {
    }
  }
  void flush_() override {}
};

using kbot_uds_sink_mt = kbot_uds_sink<std::mutex>;

struct KbotUdsJob {
  std::string payload;
  std::string job_id;
  std::shared_ptr<ipc_socket> socket;
  std::shared_ptr<std::atomic<bool>> cancel_token;
};

std::string request_id_string(const rapidjson::Document &doc) {
  if (doc.HasMember("id") && doc["id"].IsString())
    return doc["id"].GetString();
  if (doc.HasMember("jobId") && doc["jobId"].IsString())
    return doc["jobId"].GetString();
  return "kbot-uds-" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count());
}

void write_raw_frame(const std::shared_ptr<ipc_socket> &sock,
                     const std::string &json_body) {
  uint32_t len = static_cast<uint32_t>(json_body.size());
  std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
  asio::write(*sock, asio::buffer(&len, 4));
  asio::write(*sock, asio::buffer(json_body));
}

} // namespace

int run_cmd_kbot_uds(const std::string &pipe_path) {
  logger::info("Starting KBot UDS on " + pipe_path);
  std::atomic<bool> running{true};
  asio::io_context io_context;
  std::shared_ptr<ipc_acceptor> acceptor;

  try {
#ifdef _WIN32
    int port = 4000;
    try {
      port = std::stoi(pipe_path);
    } catch (...) {
    }
    ipc_endpoint ep(asio::ip::tcp::v4(), static_cast<unsigned short>(port));
    acceptor = std::make_shared<ipc_acceptor>(io_context, ep);
    logger::info("KBot UDS: bound TCP 127.0.0.1:" + std::to_string(port));
#else
    std::remove(pipe_path.c_str());
    ipc_endpoint ep(pipe_path);
    acceptor = std::make_shared<ipc_acceptor>(io_context, ep);
#endif
  } catch (const std::exception &e) {
    logger::error(std::string("KBot UDS bind failed: ") + e.what());
    return 1;
  }

  const int k_frame_max = 50 * 1024 * 1024;
  const int k_queue_depth_max = 10000;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  if (threads <= 0)
    threads = 2;
  tf::Executor executor(threads);

  moodycamel::ConcurrentQueue<KbotUdsJob> queue;

  auto log_sink = std::make_shared<kbot_uds_sink_mt>();
  log_sink->set_pattern("%^%l%$ %v");
  spdlog::default_logger()->sinks().push_back(log_sink);

  std::thread uds_job_queue_thread([&]() {
    KbotUdsJob job;
    while (running.load()) {
      if (!queue.try_dequeue(job)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }

      tf::Taskflow tf;
      tf.emplace([job]() {
        {
          std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
          g_active_uds_socket = job.socket;
        }

        kbot::KBotCallbacks cb;
        cb.onEvent = [sock = job.socket, jid = job.job_id](
                         const std::string &type, const std::string &json) {
          try {
            std::string resolved_id =
                (type == "job_result" || type == "error") ? jid : "0";
            std::string msg = "{\"id\":\"" + resolved_id +
                              "\",\"type\":\"" + type + "\",\"payload\":" +
                              json + "}";
            uint32_t len = static_cast<uint32_t>(msg.size());
            std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
            asio::write(*sock, asio::buffer(&len, 4));
            asio::write(*sock, asio::buffer(msg));
          } catch (...) {
          }
        };

        rapidjson::Document doc;
        doc.Parse(job.payload.c_str());
        if (doc.HasParseError()) {
          cb.onEvent("error", "\"invalid JSON payload\"");
          std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
          g_active_uds_socket.reset();
          return;
        }

        std::string job_type;
        if (doc.HasMember("type") && doc["type"].IsString())
          job_type = doc["type"].GetString();

        if (job_type == "job") {
          std::string payload_str = "{}";
          if (doc.HasMember("payload")) {
            rapidjson::StringBuffer sbuf;
            rapidjson::Writer<rapidjson::StringBuffer> writer(sbuf);
            doc["payload"].Accept(writer);
            payload_str = sbuf.GetString();
          }
          cb.onEvent("job_result", payload_str);
        } else if (job_type == "kbot-ai") {
          kbot::KBotCallbacks ai_cb;
          ai_cb.onEvent = [&cb](const std::string &t, const std::string &j) {
            cb.onEvent(t, j);
          };
          std::string payload_str = "{}";
          if (doc.HasMember("payload")) {
            if (doc["payload"].IsString()) {
              payload_str = doc["payload"].GetString();
            } else {
              rapidjson::StringBuffer sbuf;
              rapidjson::Writer<rapidjson::StringBuffer> writer(sbuf);
              doc["payload"].Accept(writer);
              payload_str = sbuf.GetString();
            }
          }
          polymech::run_kbot_ai_ipc(payload_str, job.job_id, ai_cb);
        } else if (job_type == "kbot-run") {
          kbot::KBotCallbacks run_cb;
          run_cb.onEvent = [&cb](const std::string &t, const std::string &j) {
            cb.onEvent(t, j);
          };
          std::string payload_str = "{}";
          if (doc.HasMember("payload")) {
            if (doc["payload"].IsString()) {
              payload_str = doc["payload"].GetString();
            } else {
              rapidjson::StringBuffer sbuf;
              rapidjson::Writer<rapidjson::StringBuffer> writer(sbuf);
              doc["payload"].Accept(writer);
              payload_str = sbuf.GetString();
            }
          }
          polymech::run_kbot_run_ipc(payload_str, job.job_id, run_cb);
        } else {
          rapidjson::StringBuffer sbuf;
          rapidjson::Writer<rapidjson::StringBuffer> w(sbuf);
          w.StartObject();
          w.Key("message");
          std::string m = "unsupported type: " + job_type;
          w.String(m.c_str(), static_cast<rapidjson::SizeType>(m.size()));
          w.EndObject();
          cb.onEvent("error", sbuf.GetString());
        }

        {
          std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
          g_active_uds_socket.reset();
        }
      });

      executor.run(tf).wait();
    }
  });

  logger::info("KBot UDS ready; waiting for connections…");
  while (running.load()) {
    auto socket = std::make_shared<ipc_socket>(io_context);
    asio::error_code ec;
    acceptor->accept(*socket, ec);
    if (ec || !running.load())
      break;

    logger::info("KBot UDS client connected");

    std::thread(
        [socket, &queue, &running, acceptor, k_frame_max, k_queue_depth_max]() {
          std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
              socket_jobs;

          try {
            {
              std::string ready =
                  R"({"id":"0","type":"ready","payload":{}})";
              uint32_t rlen = static_cast<uint32_t>(ready.size());
              std::lock_guard<std::mutex> lock(g_uds_socket_mutex);
              asio::write(*socket, asio::buffer(&rlen, 4));
              asio::write(*socket, asio::buffer(ready));
            }

            while (true) {
              uint32_t len = 0;
              asio::read(*socket, asio::buffer(&len, 4));
              if (len == 0 ||
                  len > static_cast<uint32_t>(k_frame_max))
                break;

              std::string raw(len, '\0');
              asio::read(*socket, asio::buffer(raw.data(), len));

              rapidjson::Document doc;
              doc.Parse(raw.c_str());

              if (!doc.HasParseError()) {
                std::string action;
                if (doc.HasMember("action") && doc["action"].IsString())
                  action = doc["action"].GetString();
                else if (doc.HasMember("type") && doc["type"].IsString())
                  action = doc["type"].GetString();

                auto id_for = [&doc]() -> std::string {
                  if (doc.HasMember("id") && doc["id"].IsString())
                    return doc["id"].GetString();
                  return "0";
                };

                if (action == "ping") {
                  std::string res_id = id_for();
                  std::string ack = "{\"id\":\"" + res_id +
                                      "\",\"type\":\"pong\",\"payload\":{}}";
                  write_raw_frame(socket, ack);
                  continue;
                }
                if (action == "nonsense") {
                  std::string res_id = id_for();
                  std::string ack = "{\"id\":\"" + res_id +
                                      "\",\"type\":\"error\",\"payload\":{}}";
                  write_raw_frame(socket, ack);
                  continue;
                }
                if (action == "cancel") {
                  if (doc.HasMember("jobId") && doc["jobId"].IsString()) {
                    std::string jid = doc["jobId"].GetString();
                    if (socket_jobs.count(jid) && socket_jobs[jid]) {
                      *socket_jobs[jid] = true;
                      std::string ack =
                          "{\"type\":\"cancel_ack\",\"data\":\"" + jid + "\"}";
                      write_raw_frame(socket, ack);
                    }
                  }
                  continue;
                }
                if (action == "stop" || action == "shutdown") {
                  logger::info("KBot UDS: shutdown requested");
                  std::string res_id = id_for();
                  std::string ack = "{\"id\":\"" + res_id +
                                      "\",\"type\":\"shutdown_ack\","
                                      "\"payload\":{}}";
                  write_raw_frame(socket, ack);
                  running.store(false);
                  try {
                    acceptor->close();
                  } catch (...) {
                  }
                  break;
                }
              } else {
                continue;
              }

              std::string jid = request_id_string(doc);
              auto cancel_token = std::make_shared<std::atomic<bool>>(false);
              socket_jobs[jid] = cancel_token;

              KbotUdsJob job{raw, jid, socket, cancel_token};
              while (queue.size_approx() >=
                     static_cast<size_t>(k_queue_depth_max)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
              }
              queue.enqueue(std::move(job));
            }
          } catch (const std::exception &) {
            for (auto &kv : socket_jobs) {
              if (kv.second)
                *kv.second = true;
            }
          }
        })
        .detach();
  }

  running.store(false);
  uds_job_queue_thread.join();
  return 0;
}

} // namespace polymech
