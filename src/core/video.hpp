#pragma once

#if defined(FEATURE_VIDEO) && FEATURE_VIDEO

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pm::video {

// ── Device enumeration ────────────────────────────────────────────────────────

struct DeviceInfo {
    std::string id;          // opaque symbolic-link string (Win32 MF activation)
    std::string name;        // human-readable friendly name
    bool        is_default = false;
};

// Returns all video capture devices visible to the OS.
// Empty on failure (no camera, driver error, or unsupported platform).
std::vector<DeviceInfo> enumerate_capture_devices();

// ── Device mode enumeration ───────────────────────────────────────────────────

struct DeviceMode {
    int         width    = 0;
    int         height   = 0;
    int         fps_num  = 0;  // frame-rate numerator   (e.g. 30)
    int         fps_den  = 1;  // frame-rate denominator (e.g. 1)
    std::string format;        // "MJPEG", "NV12", "YUY2", "RGB24", …
};

// Return all native media types advertised by the named capture device.
// device_name: case-insensitive substring; empty = first device.
// Empty result on failure (device not found, no types, driver error).
std::vector<DeviceMode> enumerate_device_modes(const std::string& device_name = "");

// ── Frame model ───────────────────────────────────────────────────────────────

enum class PixelFormat {
    Unknown,
    RGB24,   // R8 G8 B8, top-down, tightly packed rows
    BGR24,   // B8 G8 R8, top-down, tightly packed rows (native MF RGB24)
    NV12,
    MJPEG,   // compressed JPEG bytes (pass-through from device)
};

struct VideoFrame {
    int         width        = 0;
    int         height       = 0;
    int         stride       = 0;    // bytes per row (>= width * bytes_per_pixel)
    PixelFormat format       = PixelFormat::Unknown;
    int64_t     timestamp_ms = 0;    // wall-clock ms at capture
    std::vector<uint8_t> data;       // pixel data
};

// ── VideoInput ────────────────────────────────────────────────────────────────
// Streams frames from a webcam/capture device.
// Win32: Media Foundation IMFSourceReader.
// Linux: V4L2 mmap streaming (video_linux.cpp).
// macOS: AVFoundation (video_osx.mm).
//
// Threading:
//   - start() spawns an internal capture thread.
//   - on_frame() is called from that thread — do not block it.
//   - stop() joins the thread; safe to call from any thread.
//
class VideoInput {
public:
    VideoInput();
    ~VideoInput();

    VideoInput(const VideoInput&)            = delete;
    VideoInput& operator=(const VideoInput&) = delete;

    // Begin capture.
    //   device_name  — case-insensitive substring matched against DeviceInfo::name;
    //                  empty string (default) → first enumerated / default device.
    //   width/height — preferred resolution; 0 = device chooses.
    //   fps          — preferred frame rate; 0 = device chooses.
    //   on_frame     — frame callback. Frames are BGR24 or MJPEG depending on
    //                  what the device negotiates (check VideoFrame::format).
    //
    // Throws std::runtime_error if the device cannot be opened.
    void start(const std::string&                         device_name,
               int                                        width,
               int                                        height,
               int                                        fps,
               std::function<void(const VideoFrame&)>    on_frame);

    // Stop capture and block until the internal thread exits.
    void stop();

    bool is_running() const;

    // Name of the opened device (populated after start()).
    const std::string& opened_device_name() const;

    // Actual resolution negotiated after start() (populated after first frame).
    int actual_width()  const;
    int actual_height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// Capture a single frame from the named device.
// Opens the device, waits up to timeout_ms for a frame, then closes it.
// Throws std::runtime_error on failure or timeout.
VideoFrame capture_still(const std::string& device_name = "",
                         int width     = 0,
                         int height    = 0,
                         int timeout_ms = 5000);

// Encode a BGR24 VideoFrame to JPEG bytes in memory.
// quality = 1..100.  Returns empty on failure.
// Win32: WIC.  Linux: libjpeg.  macOS: ImageIO.
std::vector<uint8_t> encode_jpeg(const VideoFrame& frame, int quality = 90);

// Save a VideoFrame to disk.  Format is inferred from the file extension.
// Win32: .jpg/.jpeg/.png/.bmp via WIC.
// Linux: .jpg/.jpeg via libjpeg.
// Returns true on success, or false + sets err.
bool save_frame(const VideoFrame& frame, const std::string& path,
                std::string& err, int jpeg_quality = 90);

#if defined(__APPLE__) && __APPLE__
// ── macOS native recorder ─────────────────────────────────────────────────────
// Records directly to H.264 MP4 (or H.265 HEVC) using AVCaptureMovieFileOutput.
// path extension should be .mp4 or .mov.
//
// device_name  — case-insensitive substring; empty = first/default device.
// width/height — preferred resolution; 0 = pick the highest available.
// fps          — preferred frame rate; 0 = device default.
// duration_ms  — stop after this many ms; 0 = run until stop_fn returns true.
// stop_fn      — polled ~20 Hz; return true to stop early.
// on_status    — called ~1 Hz from the capture thread: (elapsed_ms, frame_count).
//
// Blocks until recording finishes.
// Throws std::runtime_error on error.
void record_movie(const std::string& device_name,
                  int                width,
                  int                height,
                  int                fps,
                  const std::string& path,
                  int                duration_ms,
                  std::function<bool()>                   stop_fn,
                  std::function<void(int64_t, int)>       on_status = {});
#endif // __APPLE__

} // namespace pm::video

#endif // FEATURE_VIDEO
