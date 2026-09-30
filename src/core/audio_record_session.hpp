#pragma once
//
// Cross-process audio record session (mmap file under config dir).
// Used by `audio record` (owner), `audio record stop`, and `audio record status`.
//
#if defined(FEATURE_STT) && FEATURE_STT

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace pm::audio::record_session {

constexpr uint32_t k_magic   = 0x53435241u; // 'ARCS' LE
constexpr uint32_t k_version = 2u;
constexpr std::size_t k_path_cap = 512;
constexpr std::size_t k_stt_cap  = 2048;
constexpr std::size_t k_name_cap = 128;
constexpr std::size_t k_format_cap = 64;
constexpr std::size_t k_provider_cap = 64;
constexpr std::size_t k_model_cap = 128;
constexpr std::size_t k_file_size = 4096;

#pragma pack(push, 1)
struct SessionShared {
    uint32_t magic;
    uint32_t version;
    uint32_t stop;
    uint32_t state;
    uint32_t use_stt;
    uint32_t samples;
    uint64_t pid;
    uint64_t started_unix_ms;
    char     device[k_name_cap];
    char     format[k_format_cap];
    char     stt_provider[k_provider_cap];
    char     stt_model[k_model_cap];
    char     dst[k_path_cap];
    char     text_out[k_path_cap];
    char     stt_buffer[k_stt_cap];
};
#pragma pack(pop)

static_assert(sizeof(SessionShared) <= k_file_size, "SessionShared must fit in session file");

enum class State : uint32_t {
    Recording = 1,
    Stopping  = 2,
    Done      = 3,
};

struct StartInfo {
    std::string dst;       // empty when scratch WAV only
    std::string text_out;  // empty when not requested
    bool        use_stt = false;
    std::string format;        // e.g. "s16le mono 16000 Hz"
    std::string stt_provider;  // empty when not using STT
    std::string stt_model;     // provider-specific model id/alias
};

struct Status {
    bool        active = false;
    State       state  = State::Done;
    bool        use_stt = false;
    uint64_t    pid = 0;
    uint32_t    samples = 0;
    uint64_t    started_unix_ms = 0;
    std::string device;
    std::string format;
    std::string stt_provider;
    std::string stt_model;
    std::string dst;
    std::string text_out;
    std::string stt_buffer;
};

std::filesystem::path session_file_path();

/// Recorder: create exclusive session file + mmap. Fails if another live recorder exists.
class Owner {
public:
    Owner() = default;
    ~Owner();

    Owner(const Owner&)            = delete;
    Owner& operator=(const Owner&) = delete;

    [[nodiscard]] bool acquire(const StartInfo& info, std::string& err);
    void set_device(std::string_view device);
    void set_samples(uint32_t samples);
    void set_stt_buffer(std::string_view text);
    void set_state(State state);
    [[nodiscard]] bool stop_requested() const;

private:
    void release() noexcept;

    SessionShared* shared_ = nullptr;
    void*       view_   = nullptr;
    std::size_t view_size_ = 0;
#if defined(_WIN32)
    void* file_handle_ = nullptr;
    void* map_handle_  = nullptr;
#else
    int fd_ = -1;
#endif
    bool owned_ = false;
};

[[nodiscard]] bool request_stop(std::string& err);
[[nodiscard]] bool read_status(Status& out, std::string& err);
[[nodiscard]] bool cleanup_stale(std::string& err);

} // namespace pm::audio::record_session

#endif // FEATURE_STT
