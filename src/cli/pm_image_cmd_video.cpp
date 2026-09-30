#if defined(FEATURE_VIDEO) && FEATURE_VIDEO

#include "pm_image_cmd_video.hpp"
#include "pm_image_cli_state.hpp"
#include "core/video.hpp"
#include "core/video_record_session.hpp"
#include "core/cli_cancel.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ── MJPEG AVI writer ──────────────────────────────────────────────────────────
// Writes a minimal RIFF/AVI file containing MJPEG frames.
// Sizes that depend on frame count are patched via fseek at finalize().

class MjpegAviWriter {
public:
    MjpegAviWriter(const std::string& path, int width, int height, int fps)
        : width_(width), height_(height), fps_(fps > 0 ? fps : 30) {
#ifdef _WIN32
        // Need wide-char fopen on Windows for unicode paths.
        auto wpath = std::wstring(path.begin(), path.end());
        fp_ = _wfopen(wpath.c_str(), L"wb");
#else
        fp_ = fopen(path.c_str(), "wb");
#endif
        if (!fp_) throw std::runtime_error("video: cannot create file: " + path);
        write_headers();
    }

    // Append one JPEG-compressed frame. jpeg_data must be a valid JFIF byte stream.
    void write_frame(const uint8_t* jpeg_data, size_t jpeg_size) {
        // Record chunk offset relative to start of 'movi' data region.
        auto chunk_pos = static_cast<uint32_t>(
            static_cast<long>(ftell(fp_)) - static_cast<long>(off_movi_data_));
        fwrite("00dc", 4, 1, fp_);
        write_u32(static_cast<uint32_t>(jpeg_size));
        fwrite(jpeg_data, 1, jpeg_size, fp_);
        if (jpeg_size & 1) fputc(0, fp_); // RIFF padding: chunks must be even-aligned
        if (jpeg_size > max_frame_bytes_) max_frame_bytes_ = jpeg_size;
        index_.push_back({ chunk_pos, static_cast<uint32_t>(jpeg_size) });
        ++frame_count_;
    }

    // Patch header counts and write idx1; called once after all frames.
    void finalize() {
        if (!fp_) return;

        // Write idx1 (legacy index for compatibility with old players).
        long idx1_start = ftell(fp_);
        uint32_t idx_size = static_cast<uint32_t>(index_.size() * 16);
        fwrite("idx1", 4, 1, fp_);
        write_u32(idx_size);
        for (auto& e : index_) {
            fwrite("00dc", 4, 1, fp_);          // ckid
            write_u32(0x10u);                   // AVIIF_KEYFRAME
            write_u32(e.movi_offset);           // offset from start of 'movi' data
            write_u32(e.data_size);
        }

        long file_end = ftell(fp_);

        // Patch RIFF chunk size (total file - 8 bytes for 'RIFF'+size).
        patch_u32(off_riff_size_,  static_cast<uint32_t>(file_end - 8));

        // Patch movi LIST size = 4('movi') + all frame chunks.
        long movi_list_end = idx1_start;
        uint32_t movi_sz = static_cast<uint32_t>(
            movi_list_end - static_cast<long>(off_movi_data_) + 4); // +4 for 'movi' fourcc
        patch_u32(off_movi_size_, movi_sz);

        // Patch AVIMAINHEADER fields.
        patch_u32(off_avih_total_frames_,   static_cast<uint32_t>(frame_count_));
        uint32_t avg_bps = static_cast<uint32_t>(max_frame_bytes_) * static_cast<uint32_t>(fps_);
        patch_u32(off_avih_max_bps_,        avg_bps);

        // Patch AVISTREAMHEADER.dwLength.
        patch_u32(off_strh_length_,         static_cast<uint32_t>(frame_count_));

        fclose(fp_);
        fp_ = nullptr;
    }

    ~MjpegAviWriter() {
        if (fp_) { fclose(fp_); fp_ = nullptr; }
    }

    MjpegAviWriter(const MjpegAviWriter&)            = delete;
    MjpegAviWriter& operator=(const MjpegAviWriter&) = delete;

private:
    FILE*  fp_       = nullptr;
    int    width_, height_, fps_;
    int    frame_count_ = 0;
    size_t max_frame_bytes_ = 1;

    // Offsets to patch at finalize():
    long off_riff_size_       = 0;
    long off_avih_total_frames_ = 0;
    long off_avih_max_bps_    = 0;
    long off_strh_length_     = 0;
    long off_movi_size_       = 0;
    long off_movi_data_       = 0; // file pos right after 'movi' fourcc

    struct FrameEntry { uint32_t movi_offset, data_size; };
    std::vector<FrameEntry> index_;

    void write_u32(uint32_t v) {
        fwrite(&v, 4, 1, fp_);
    }

    void patch_u32(long offset, uint32_t value) {
        long cur = ftell(fp_);
        fseek(fp_, offset, SEEK_SET);
        write_u32(value);
        fseek(fp_, cur, SEEK_SET);
    }

    void write_headers() {
        // Fixed sizes (computed bottom-up):
        // strf chunk  : 'strf'(4) + size(4) + BITMAPINFOHEADER(40) = 48
        // strh chunk  : 'strh'(4) + size(4) + AVISTREAMHEADER(56)  = 64
        // strl LIST   : 'LIST'(4) + size(4) + 'strl'(4) + strh(64) + strf(48) = 124; size field = 116
        // avih chunk  : 'avih'(4) + size(4) + AVIMAINHEADER(56)    = 64
        // hdrl LIST   : 'LIST'(4) + size(4) + 'hdrl'(4) + avih(64) + strl_chunk(124) = 204; size = 4+64+124=192

        const uint32_t k_strl_data_sz = 4 + 64 + 48;      // 'strl' + strh chunk + strf chunk
        const uint32_t k_hdrl_data_sz = 4 + 64 + (8 + k_strl_data_sz); // 'hdrl' + avih chunk + strl LIST chunk

        // ── RIFF AVI ──────────────────────────────────────────────────────────
        fwrite("RIFF", 4, 1, fp_);
        off_riff_size_ = ftell(fp_);
        write_u32(0);          // patch at finalize
        fwrite("AVI ", 4, 1, fp_);

        // ── LIST hdrl ─────────────────────────────────────────────────────────
        fwrite("LIST", 4, 1, fp_);
        write_u32(k_hdrl_data_sz);
        fwrite("hdrl", 4, 1, fp_);

        // ── 'avih' ────────────────────────────────────────────────────────────
        fwrite("avih", 4, 1, fp_);
        write_u32(56);
        // dwMicroSecPerFrame
        write_u32(fps_ > 0 ? (1000000u / static_cast<uint32_t>(fps_)) : 33333u);
        off_avih_max_bps_ = ftell(fp_);
        write_u32(0);          // dwMaxBytesPerSec — patch
        write_u32(0);          // dwPaddingGranularity
        write_u32(0x10u);      // dwFlags = AVIF_HASINDEX
        off_avih_total_frames_ = ftell(fp_);
        write_u32(0);          // dwTotalFrames — patch
        write_u32(0);          // dwInitialFrames
        write_u32(1);          // dwStreams
        write_u32(0);          // dwSuggestedBufferSize
        write_u32(static_cast<uint32_t>(width_));
        write_u32(static_cast<uint32_t>(height_));
        write_u32(0); write_u32(0); write_u32(0); write_u32(0); // dwReserved[4]

        // ── LIST strl ─────────────────────────────────────────────────────────
        fwrite("LIST", 4, 1, fp_);
        write_u32(k_strl_data_sz);
        fwrite("strl", 4, 1, fp_);

        // ── 'strh' ────────────────────────────────────────────────────────────
        fwrite("strh", 4, 1, fp_);
        write_u32(56);
        fwrite("vids", 4, 1, fp_); // fccType
        fwrite("MJPG", 4, 1, fp_); // fccHandler
        write_u32(0);              // dwFlags
        write_u32(0);              // wPriority | wLanguage
        write_u32(0);              // dwInitialFrames
        write_u32(1);              // dwScale
        write_u32(static_cast<uint32_t>(fps_)); // dwRate
        write_u32(0);              // dwStart
        off_strh_length_ = ftell(fp_);
        write_u32(0);              // dwLength — patch
        write_u32(0);              // dwSuggestedBufferSize
        write_u32(0xFFFFFFFFu);   // dwQuality (use default)
        write_u32(0);              // dwSampleSize
        // rcFrame: RECT as (left16|top16) (right16|bottom16)
        write_u32(0);
        write_u32((static_cast<uint32_t>(width_)  & 0xFFFFu) |
                  (static_cast<uint32_t>(height_) & 0xFFFFu) << 16);

        // ── 'strf' (BITMAPINFOHEADER) ─────────────────────────────────────────
        fwrite("strf", 4, 1, fp_);
        write_u32(40);
        write_u32(40);             // biSize
        write_u32(static_cast<uint32_t>(width_));
        write_u32(static_cast<uint32_t>(height_));
        write_u32(0x00180001u);    // biPlanes=1 (low 16) | biBitCount=24 (high 16)
        fwrite("MJPG", 4, 1, fp_); // biCompression
        write_u32(static_cast<uint32_t>(width_ * height_ * 3)); // biSizeImage
        write_u32(0);              // biXPelsPerMeter
        write_u32(0);              // biYPelsPerMeter
        write_u32(0);              // biClrUsed
        write_u32(0);              // biClrImportant

        // ── LIST movi ─────────────────────────────────────────────────────────
        fwrite("LIST", 4, 1, fp_);
        off_movi_size_ = ftell(fp_);
        write_u32(0);              // size — patch at finalize
        fwrite("movi", 4, 1, fp_);
        off_movi_data_ = ftell(fp_); // frame chunks start here
    }
};

// ── Mode index helper ─────────────────────────────────────────────────────────
// Sorts modes identically to the text output of `video info --modes`.
static std::vector<pm::video::DeviceMode> sorted_modes(const std::string& device_name) {
    auto modes = pm::video::enumerate_device_modes(device_name);
    std::sort(modes.begin(), modes.end(),
        [](const pm::video::DeviceMode& a, const pm::video::DeviceMode& b) {
            int area_a = a.width * a.height, area_b = b.width * b.height;
            if (area_a != area_b) return area_a > area_b;
            double fa = a.fps_den > 0 ? double(a.fps_num)/a.fps_den : 0;
            double fb = b.fps_den > 0 ? double(b.fps_num)/b.fps_den : 0;
            if (fa != fb) return fa > fb;
            return a.format < b.format;
        });
    return modes;
}

// If mode_idx >= 0, resolve it to width/height/fps from the sorted mode list.
// Returns false (and prints an error) if the index is out of range.
static bool resolve_mode_idx(const std::string& device_name, int mode_idx,
                              int& w, int& h, int& fps) {
    if (mode_idx < 0) return true; // nothing to do
    auto modes = sorted_modes(device_name);
    if (static_cast<size_t>(mode_idx) >= modes.size()) {
        std::cerr << "video: --mode " << mode_idx << " is out of range "
                  << "(device has " << modes.size() << " mode(s); "
                  << "use `video info --modes` to list them)\n";
        return false;
    }
    const auto& m = modes[static_cast<size_t>(mode_idx)];
    w   = m.width;
    h   = m.height;
    fps = m.fps_den > 0 ? static_cast<int>(m.fps_num / m.fps_den) : m.fps_num;
    return true;
}

// ── cmd_video_info ────────────────────────────────────────────────────────────

static int cmd_video_info(PmImageCliState& st) {
    auto devices = pm::video::enumerate_capture_devices();

    // Apply --input filter if given.
    if (!st.video_info_input.empty()) {
        std::string needle = st.video_info_input;
        for (auto& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        devices.erase(
            std::remove_if(devices.begin(), devices.end(), [&](const pm::video::DeviceInfo& d) {
                std::string name = d.name;
                for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return name.find(needle) == std::string::npos;
            }),
            devices.end());
    }

    if (st.video_info_json) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& d : devices) {
            nlohmann::json entry = {{"name", d.name}, {"id", d.id},
                                    {"is_default", d.is_default}};
            if (st.video_info_modes) {
                auto modes = pm::video::enumerate_device_modes(d.name);
                nlohmann::json marr = nlohmann::json::array();
                for (auto& m : modes) {
                    double fps = m.fps_den > 0
                                 ? static_cast<double>(m.fps_num) / m.fps_den : 0.0;
                    marr.push_back({{"width", m.width}, {"height", m.height},
                                    {"fps", fps}, {"format", m.format}});
                }
                entry["modes"] = marr;
            }
            arr.push_back(entry);
        }
        std::cout << arr.dump(2) << "\n";
        return 0;
    }

    if (devices.empty()) {
        std::cout << "video info: no capture devices found\n";
        return 0;
    }

    std::cout << "video devices  (" << devices.size() << ")\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] " << devices[i].name;
        if (devices[i].is_default) std::cout << "  (default)";
        std::cout << "\n";

        if (st.video_info_modes) {
            auto modes = pm::video::enumerate_device_modes(devices[i].name);
            if (modes.empty()) {
                std::cout << "        (no modes enumerated)\n";
            } else {
                // Group by resolution for compact display.
                // Print: width x height   @ fps  format
                // Sort: larger res first, then higher fps.
                std::sort(modes.begin(), modes.end(),
                    [](const pm::video::DeviceMode& a, const pm::video::DeviceMode& b) {
                        int area_a = a.width * a.height;
                        int area_b = b.width * b.height;
                        if (area_a != area_b) return area_a > area_b;
                        double fa = a.fps_den > 0 ? double(a.fps_num)/a.fps_den : 0;
                        double fb = b.fps_den > 0 ? double(b.fps_num)/b.fps_den : 0;
                        if (fa != fb) return fa > fb;
                        return a.format < b.format;
                    });
                for (size_t mi = 0; mi < modes.size(); ++mi) {
                    const auto& m = modes[mi];
                    char fps_buf[16];
                    if (m.fps_den == 1 || m.fps_den == 0)
                        std::snprintf(fps_buf, sizeof(fps_buf), "%d", m.fps_num);
                    else
                        std::snprintf(fps_buf, sizeof(fps_buf), "%.2f",
                                      double(m.fps_num) / m.fps_den);
                    std::cout << "     [" << std::setw(2) << mi << "] "
                              << std::setw(5) << m.width << " x "
                              << std::setw(4) << m.height
                              << "  @ " << std::setw(6) << fps_buf << " fps"
                              << "  " << m.format << "\n";
                }
            }
        }
    }
    return 0;
}

// ── cmd_video_image ───────────────────────────────────────────────────────────

static int cmd_video_image(PmImageCliState& st) {
    fs::path dst_path = fs::absolute(fs::path(st.video_image_dst));
    fs::create_directories(dst_path.parent_path());

    // Resolve --mode index → explicit width/height (fps ignored for stills).
    int img_w = st.video_image_width, img_h = st.video_image_height, img_fps = 0;
    if (!resolve_mode_idx(st.video_image_input, st.video_image_mode_idx,
                          img_w, img_h, img_fps))
        return 1;

    std::cout << "video image: connecting to device";
    if (!st.video_image_input.empty())
        std::cout << " '" << st.video_image_input << "'";
    std::cout << "...\n";

    pm::video::VideoFrame frame;
    std::string vin_name;
    {
        std::mutex             mu;
        std::condition_variable cv;
        bool                   got = false;

        pm::video::VideoInput vin;
        try {
            vin.start(st.video_image_input,
                      img_w, img_h, img_fps,
                      [&](const pm::video::VideoFrame& f) {
                          std::lock_guard<std::mutex> lk(mu);
                          if (!got) { frame = f; got = true; cv.notify_one(); }
                      });
            vin_name = vin.opened_device_name();
        } catch (const std::exception& e) {
            std::cerr << "video image: " << e.what() << "\n";
            return 1;
        }

        std::unique_lock<std::mutex> lk(mu);
        if (!cv.wait_for(lk, std::chrono::seconds(5), [&] { return got; })) {
            vin.stop();
            std::cerr << "video image: timeout waiting for frame\n";
            return 1;
        }
        vin.stop();
    }

    std::string err;
    if (!pm::video::save_frame(frame, dst_path.string(), err, 90)) {
        std::cerr << "video image: save failed: " << err << "\n";
        return 1;
    }

    std::error_code ec;
    auto shown = fs::weakly_canonical(dst_path, ec);
    const char* fmt = "unknown";
    if (frame.format == pm::video::PixelFormat::BGR24) fmt = "bgr24";
    else if (frame.format == pm::video::PixelFormat::MJPEG) fmt = "mjpeg";
    std::cout << "video image\n"
              << "  device : " << vin_name << "\n"
              << "  format : " << fmt << "\n"
              << "  size   : " << frame.width << " x " << frame.height << "\n"
              << "  file   : " << (ec ? dst_path : shown).string()
              << "  (" << fs::file_size(dst_path, ec) << " bytes)\n";
    return 0;
}

// ── cmd_video_video ───────────────────────────────────────────────────────────

// Returns true if the path extension requests a native container (.mp4 / .mov).
static bool is_native_container(const fs::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".mp4" || ext == ".mov";
}

static int cmd_video_record(PmImageCliState& st) {
    if (st.video_video_dst.empty()) {
        std::cerr << "video record: --dst is required\n";
        return 1;
    }
    fs::path dst_path = fs::absolute(fs::path(st.video_video_dst));
    fs::create_directories(dst_path.parent_path());

    // Resolve --mode index → explicit width/height/fps.
    int vid_w = st.video_video_width, vid_h = st.video_video_height;
    int fps   = st.video_video_fps > 0 ? st.video_video_fps : 30;
    if (!resolve_mode_idx(st.video_video_input, st.video_video_mode_idx,
                          vid_w, vid_h, fps))
        return 1;

    const std::string codec = is_native_container(dst_path) ? "H.264/MP4" : "MJPEG/AVI";

    std::string mode_desc;
    {
        char buf[96];
        if (st.video_video_mode_idx >= 0) {
            auto mlist = sorted_modes(st.video_video_input);
            const auto& cm = mlist[static_cast<size_t>(st.video_video_mode_idx)];
            std::snprintf(buf, sizeof(buf), "%d x %d  @ %d fps  %s  (mode %d)",
                          vid_w, vid_h, fps,
                          cm.format.c_str(), st.video_video_mode_idx);
        } else if (vid_w > 0 || vid_h > 0) {
            std::snprintf(buf, sizeof(buf), "%d x %d  @ %d fps",
                          vid_w, vid_h, fps);
        } else {
            std::snprintf(buf, sizeof(buf), "auto  @ %d fps", fps);
        }
        mode_desc = buf;
    }

    pm::video::record_session::Owner session;
    {
        pm::video::record_session::StartInfo sinfo;
        sinfo.dst   = dst_path.string();
        sinfo.mode  = mode_desc;
        sinfo.codec = codec;
        std::string sess_err;
        if (!session.acquire(sinfo, sess_err)) {
            std::cerr << "video record: " << sess_err << "\n";
            return 1;
        }
    }

#if defined(__APPLE__) && __APPLE__
    // ── macOS native path: H.264 MP4/MOV via AVCaptureMovieFileOutput ──────────
    if (is_native_container(dst_path)) {
        bool has_duration = (st.video_video_duration_ms > 0);
        std::cout << "video record: opening device";
        if (!st.video_video_input.empty())
            std::cout << " '" << st.video_video_input << "'";
        std::cout << " (H.264 " << dst_path.extension().string() << ")...\n";

        auto t_start = std::chrono::steady_clock::now();
        auto stop_fn = [&]() -> bool {
            if (session.stop_requested())
                media::cli::test_request_cancel();
            return media::cli::cancel_requested();
        };
        auto on_status = [&](int64_t /*elapsed_ms*/, int frames) {
            session.set_frames(static_cast<uint32_t>(frames));
            std::cout << "\r  " << (frames > 0 ? frames : 0)
                      << " frames" << std::flush;
        };

        try {
            pm::video::record_movie(
                st.video_video_input,
                vid_w, vid_h,
                fps,
                dst_path.string(),
                st.video_video_duration_ms,
                stop_fn,
                on_status);
        } catch (const std::exception& e) {
            std::cerr << "\nvideo record: " << e.what() << "\n";
            return 1;
        }

        std::cout << "\n";
        std::error_code ec;
        auto shown       = fs::weakly_canonical(dst_path, ec);
        auto elapsed_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t_start).count();
        std::cout << "video record: done\n"
                  << "  codec   : H.264 (web-compatible)\n"
                  << "  duration: " << (elapsed_ms / 1000.0) << " s\n";
        if (has_duration)
            std::cout << "  limit   : " << (st.video_video_duration_ms / 1000.0) << " s\n";
        std::cout << "  file    : " << (ec ? dst_path : shown).string()
                  << "  (" << fs::file_size(dst_path, ec) << " bytes)\n";
        return 0;
    }
#endif // __APPLE__

    std::cout << "video record: opening device";
    if (!st.video_video_input.empty())
        std::cout << " '" << st.video_video_input << "'";
    std::cout << "...\n";

    // First: capture one frame to get actual dimensions.
    std::mutex            mu;
    std::atomic<bool>     running{true};
    pm::video::VideoInput vin;

    // We discover actual width/height from the first frame.
    int actual_w = 0, actual_h = 0;
    std::unique_ptr<MjpegAviWriter> avi;
    int frame_count = 0;

    auto t_start = std::chrono::steady_clock::now();
    const auto duration = std::chrono::milliseconds(
        st.video_video_duration_ms > 0 ? st.video_video_duration_ms : 0);
    bool has_duration = (duration.count() > 0);

    try {
        vin.start(
            st.video_video_input,
            vid_w,
            vid_h,
            fps,
            [&](const pm::video::VideoFrame& frame) {
                if (!running.load()) return;

                // Check duration limit.
                if (has_duration) {
                    auto elapsed = std::chrono::steady_clock::now() - t_start;
                    if (elapsed >= duration) {
                        running.store(false);
                        return;
                    }
                }

                std::lock_guard<std::mutex> lk(mu);

                // Lazy-init writer on first frame (we now know actual dimensions).
                if (!avi) {
                    actual_w = frame.width;
                    actual_h = frame.height;
                    avi = std::make_unique<MjpegAviWriter>(
                        dst_path.string(), actual_w, actual_h, fps);
                }

                // Encode frame to JPEG.
                auto jpeg = pm::video::encode_jpeg(frame, st.video_video_quality);
                if (!jpeg.empty()) {
                    avi->write_frame(jpeg.data(), jpeg.size());
                    ++frame_count;
                    session.set_frames(static_cast<uint32_t>(frame_count));
                }

                // Print progress every second.
                static auto t_last_print = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (now - t_last_print >= std::chrono::seconds(1)) {
                    t_last_print = now;
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - t_start).count();
                    std::cout << "\r  " << (elapsed_ms / 1000.0)
                              << " s  " << frame_count << " frames"
                              << std::flush;
                }
            });
    } catch (const std::exception& e) {
        std::cerr << "video record: " << e.what() << "\n";
        return 1;
    }
    session.set_device(vin.opened_device_name());

    // Print device info now that we've started.
    std::cout << "video record\n"
              << "  device  : " << vin.opened_device_name() << "\n"
              << "  mode    : " << mode_desc << "\n"
              << "  codec   : " << codec << "\n";
    if (has_duration)
        std::cout << "  duration: " << (duration.count() / 1000.0) << " s\n";
    std::cout << "  output  : " << dst_path.string() << "\n"
              << "  stop    : Ctrl+C or `video record stop`"
              << (has_duration ? " or after duration" : "") << "\n";

    auto poll_session_stop = [&] {
        if (session.stop_requested())
            media::cli::test_request_cancel();
    };

    // Wait: either duration elapses, Ctrl+C, session stop, or running cleared by callback.
    while (running.load()) {
        poll_session_stop();
        if (media::cli::cancel_requested()) { running.store(false); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!vin.is_running()) break;
    }
    running.store(false);
    session.set_state(pm::video::record_session::State::Stopping);
    vin.stop();

    std::cout << "\n";

    if (!avi) {
        std::cerr << "video record: no frames captured\n";
        return 1;
    }

    {
        std::lock_guard<std::mutex> lk(mu);
        avi->finalize();
    }

    std::error_code ec;
    auto shown = fs::weakly_canonical(dst_path, ec);
    auto elapsed_total = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start);
    std::cout << "video record: done\n"
              << "  frames  : " << frame_count << "\n"
              << "  duration: " << (elapsed_total.count() / 1000.0) << " s\n"
              << "  size    : " << actual_w << " x " << actual_h << "\n"
              << "  file    : " << (ec ? dst_path : shown).string()
              << "  (" << fs::file_size(dst_path, ec) << " bytes)\n";
    return 0;
}

static uint64_t now_unix_ms_for_status()
{
    namespace chr = std::chrono;
    return static_cast<uint64_t>(chr::duration_cast<chr::milliseconds>(
        chr::system_clock::now().time_since_epoch()).count());
}

static int cmd_video_record_stop() {
    std::string err;
    if (!pm::video::record_session::request_stop(err)) {
        std::cerr << "video record stop: " << err << "\n";
        return 1;
    }
    std::cout << "video record stop: signal sent\n";
    return 0;
}

static int cmd_video_record_status(const PmImageCliState& st) {
    namespace chr = std::chrono;

    pm::video::record_session::Status info;
    std::string err;
    if (!pm::video::record_session::read_status(info, err)) {
        std::cerr << "video record status: " << err << "\n";
        return 1;
    }

    if (!info.active) {
        if (st.video_record_status_json) {
            nlohmann::json j;
            j["active"] = false;
            std::cout << j.dump(2) << "\n";
        } else {
            std::cout << "video record status: not recording\n";
        }
        return 0;
    }

    const double elapsed_s = info.started_unix_ms > 0
        ? chr::duration<double>(chr::milliseconds(
              static_cast<int64_t>(now_unix_ms_for_status() - info.started_unix_ms))).count()
        : 0.0;

    if (st.video_record_status_json) {
        nlohmann::json j;
        j["active"]    = true;
        j["pid"]       = info.pid;
        j["state"]     = static_cast<uint32_t>(info.state);
        j["frames"]    = info.frames;
        j["elapsed_s"] = elapsed_s;
        if (!info.device.empty()) j["device"] = info.device;
        if (!info.mode.empty())   j["mode"]   = info.mode;
        if (!info.codec.empty())  j["codec"]  = info.codec;
        if (!info.dst.empty())    j["dst"]    = info.dst;
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    std::cout << "video record status: recording\n"
              << "  pid     : " << info.pid << "\n"
              << "  elapsed : " << elapsed_s << " s\n"
              << "  frames  : " << info.frames << "\n";
    if (!info.device.empty())
        std::cout << "  device  : " << info.device << "\n";
    if (!info.mode.empty())
        std::cout << "  mode    : " << info.mode << "\n";
    if (!info.codec.empty())
        std::cout << "  codec   : " << info.codec << "\n";
    if (!info.dst.empty())
        std::cout << "  dst     : " << info.dst << "\n";
    return 0;
}

// ── pm_image_cmd_video ────────────────────────────────────────────────────────

int pm_image_cmd_video(CLI::App& /*app*/, PmImageCliState& st) {
    if (st.video_info_cmd && st.video_info_cmd->parsed())
        return cmd_video_info(st);
    if (st.video_image_cmd && st.video_image_cmd->parsed())
        return cmd_video_image(st);
    if (st.video_record_stop_cmd && st.video_record_stop_cmd->parsed())
        return cmd_video_record_stop();
    if (st.video_record_status_cmd && st.video_record_status_cmd->parsed())
        return cmd_video_record_status(st);
    if (st.video_record_cmd && st.video_record_cmd->parsed())
        return cmd_video_record(st);
    std::cerr << "video: no subcommand given (try: info, image, record)\n";
    return 1;
}

void pm_image_register_video(CLI::App& app, PmImageCliState& s) {
    s.video_cmd = app.add_subcommand(
        "video",
        "Video capture utilities: enumerate devices, capture still frames, and record video "
        "(Win32 Media Foundation; MJPEG AVI output). Requires FEATURE_VIDEO build.");
    s.video_cmd->require_subcommand(1);

    s.video_info_cmd = s.video_cmd->add_subcommand(
        "info",
        "List video capture devices available on this system (name, id, default flag). "
        "Use --modes to also enumerate each device's supported resolutions and frame rates.");
    s.video_info_cmd->add_flag(
        "--json", s.video_info_json,
        "Machine-readable JSON on stdout.");
    s.video_info_cmd->add_flag(
        "--modes,-m", s.video_info_modes,
        "Enumerate supported capture modes (resolution, fps, format) for each device.");
    s.video_info_cmd
        ->add_option(
            "--input", s.video_info_input,
            "Filter to a specific device (case-insensitive substring match on name). "
            "With --modes, enumerate modes for this device only.");

    s.video_image_cmd = s.video_cmd->add_subcommand(
        "image",
        "Capture a single still frame from a webcam and save it as an image file.");
    s.video_image_cmd
        ->add_option(
            "--dst", s.video_image_dst,
            "Destination image file (.jpg / .jpeg / .png / .bmp). "
            "Relative paths are resolved from the current working directory.")
        ->required(true);
    s.video_image_cmd
        ->add_option(
            "--input", s.video_image_input,
            "Capture device name (case-insensitive substring; use `video info` to list). "
            "Omit to use the first/default device.");
    s.video_image_cmd
        ->add_option("--mode", s.video_image_mode_idx,
                     "Mode index from `video info --modes` (0-based). "
                     "Overrides --width/--height when set.")
        ->default_val(-1);
    s.video_image_cmd
        ->add_option("--width",  s.video_image_width,
                     "Preferred capture width in pixels (0 = device default).")
        ->default_val(0);
    s.video_image_cmd
        ->add_option("--height", s.video_image_height,
                     "Preferred capture height in pixels (0 = device default).")
        ->default_val(0);

    s.video_record_cmd = s.video_cmd->add_subcommand(
        "record",
        "Record video from a webcam to an MJPEG AVI file (or H.264 MP4/MOV on macOS). "
        "Stops after --duration-ms, on Ctrl+C, or via `video record stop`.");
    s.video_record_cmd->alias("video");
    s.video_record_cmd->require_subcommand(0, 1);
    s.video_record_cmd
        ->add_option(
            "--dst", s.video_video_dst,
            "Destination file (.avi MJPEG, or .mp4/.mov H.264 on macOS). "
            "Relative paths are resolved from the current working directory.");
    s.video_record_cmd
        ->add_option(
            "--input", s.video_video_input,
            "Capture device name (case-insensitive substring; use `video info` to list). "
            "Omit to use the first/default device.");
    s.video_record_cmd
        ->add_option("--mode", s.video_video_mode_idx,
                     "Mode index from `video info --modes` (0-based). "
                     "Overrides --width/--height/--fps when set.")
        ->default_val(-1);
    s.video_record_cmd
        ->add_option("--width",  s.video_video_width,
                     "Preferred capture width in pixels (0 = device default).")
        ->default_val(0);
    s.video_record_cmd
        ->add_option("--height", s.video_video_height,
                     "Preferred capture height in pixels (0 = device default).")
        ->default_val(0);
    s.video_record_cmd
        ->add_option("--fps", s.video_video_fps,
                     "Frame rate for capture and AVI header (default: 30).")
        ->default_val(30)
        ->check(CLI::PositiveNumber);
    s.video_record_cmd
        ->add_option(
            "--duration-ms", s.video_video_duration_ms,
            "Stop recording after this many milliseconds (0 = run until Ctrl+C).")
        ->default_val(0)
        ->check(CLI::NonNegativeNumber);
    s.video_record_cmd
        ->add_option(
            "--quality", s.video_video_quality,
            "JPEG quality for MJPEG frames (1-100; default: 85).")
        ->default_val(85)
        ->check(CLI::Bound(1, 100));

    s.video_record_stop_cmd = s.video_record_cmd->add_subcommand(
        "stop",
        "Signal a running `video record` session in another terminal to stop cooperatively.");
    s.video_record_status_cmd = s.video_record_cmd->add_subcommand(
        "status",
        "Show the active `video record` session (device, mode, codec, elapsed time, frames, dst).");
    s.video_record_status_cmd->add_flag(
        "--json", s.video_record_status_json,
        "Machine-readable JSON on stdout.");
}

#endif // FEATURE_VIDEO
